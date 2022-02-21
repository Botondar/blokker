#include "Renderer.hpp"

#include <cassert>
#include <Platform.hpp>
#include <Profiler.hpp>

#include <imgui/imgui.h>

PFN_vkCmdBeginRenderingKHR vkCmdBeginRenderingKHR_ = nullptr;
PFN_vkCmdEndRenderingKHR   vkCmdEndRenderingKHR_   = nullptr;
PFN_vkCmdSetPrimitiveTopologyEXT vkCmdSetPrimitiveTopologyEXT_ = nullptr;

static PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT_ = nullptr;
#define vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT_

static VkBool32 VKAPI_PTR VulkanDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT Severity,
    VkDebugUtilsMessageTypeFlagsEXT Type,
    const VkDebugUtilsMessengerCallbackDataEXT* CallbackData,
    void* UserData)
{
    //DebugPrint("%s\n", CallbackData->pMessage);
    return VK_FALSE;
}

bool RTHeap_Create(
    vulkan_rt_heap* Heap, 
    u64 Size, u32 MemoryTypeBase,
    u32 MemoryRequirementCount,
    const VkMemoryRequirements* MemoryRequirements,
    VkDevice Device)
{
    assert(Heap);

    bool Result = false;
    memset(Heap, 0, sizeof(vulkan_rt_heap));
    
    u32 SuitableMemoryTypes = MemoryTypeBase;
    for (u32 i = 0; i < MemoryRequirementCount; i++)
    {
        SuitableMemoryTypes &= MemoryRequirements[i].memoryTypeBits;
    }

    u32 MemoryTypeIndex;
    if(BitScanForward(&MemoryTypeIndex, SuitableMemoryTypes) != 0)
    {
        VkMemoryAllocateInfo AllocInfo =
        {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = nullptr,
            .allocationSize = Size,
            .memoryTypeIndex = MemoryTypeIndex,
        };
        if (vkAllocateMemory(Device, &AllocInfo, nullptr, &Heap->Heap) == VK_SUCCESS)
        {
            Heap->Device = Device;
            Heap->HeapSize = Size;
            Heap->HeapOffset = 0;
            Heap->MemoryTypeIndex = MemoryTypeIndex;
            Result = true;
        }
    }

    return Result;
}

bool RTHeap_PushImage(
    vulkan_rt_heap* Heap,
    VkImage Image)
{
    assert(Heap);
    bool Result = false;

    VkMemoryRequirements MemoryRequirements = {};
    vkGetImageMemoryRequirements(Heap->Device, Image, &MemoryRequirements);

    if (MemoryRequirements.memoryTypeBits & (1 << Heap->MemoryTypeIndex))
    {
        u64 Offset = Heap->HeapOffset;
        Offset = AlignToPow2(Offset, MemoryRequirements.alignment);

        u64 End = Offset + MemoryRequirements.size;
        if (End <= Heap->HeapSize)
        {
            VkResult vkResult = vkBindImageMemory(Heap->Device, Image, Heap->Heap, Offset);
            if (vkResult == VK_SUCCESS)
            {
                Heap->HeapOffset = End;
                Result = true;
            }
        }
    }
    return Result;
}

bool StagingHeap_Create(
    vulkan_staging_heap* Heap,
    u64 Size,
    vulkan_renderer* Renderer)
{
    assert(Heap);

    *Heap = {};
    bool Result = false;

    VkBufferCreateInfo BufferInfo = 
    {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size = Size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
    };

    VkBuffer Buffer;
    if (vkCreateBuffer(Renderer->Device, &BufferInfo, nullptr, &Buffer) == VK_SUCCESS)
    {
        VkMemoryRequirements MemoryRequirements = {};
        vkGetBufferMemoryRequirements(Renderer->Device, Buffer, &MemoryRequirements);

        u32 MemoryTypes = Renderer->HostVisibleMemoryTypes & MemoryRequirements.memoryTypeBits;
        u32 MemoryTypeIndex;
        if (BitScanForward(&MemoryTypeIndex, MemoryTypes) != 0)
        {
            VkMemoryAllocateInfo AllocInfo = 
            {
                .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                .pNext = nullptr,
                .allocationSize = Size,
                .memoryTypeIndex = MemoryTypeIndex,
            };

            VkDeviceMemory Memory;
            if (vkAllocateMemory(Renderer->Device, &AllocInfo, nullptr, &Memory) == VK_SUCCESS)
            {
                if (vkBindBufferMemory(Renderer->Device, Buffer, Memory, 0) == VK_SUCCESS)
                {
                    VkSemaphore Semaphores[64];
                    bool SemaphoreCreationFailed = false;
                    for (u32 i = 0; i < 64; i++)
                    {
                        VkSemaphoreCreateInfo SemaphoreInfo = 
                        {
                            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
                            .pNext = nullptr,
                            .flags = 0,
                        };

                        if (vkCreateSemaphore(Renderer->Device, &SemaphoreInfo, nullptr, Semaphores + i) != VK_SUCCESS)
                        {
                            SemaphoreCreationFailed = true;
                            for (u32 j = ((i != 0) ? i - 1 : 0); j > 0; j--)
                            {
                                vkDestroySemaphore(Renderer->Device, Semaphores[i], nullptr);
                            }
                            break;
                        }
                    }

                    if (!SemaphoreCreationFailed)
                    {
                        Heap->Device = Renderer->Device;
                        
                        Heap->HeapSize = Size;
                        Heap->HeapOffset = 0;
                        Heap->Granularity = Renderer->NonCoherentAtomSize;
                        Heap->Heap = Memory;
                        Heap->Buffer = Buffer;

                        Heap->OutstandingCopyOps = 0;
                        for (u32 i = 0; i < 64; i++)
                        {
                            Heap->CopyOps[i].Semaphore = Semaphores[i];
                        }
                        Result = true;
                    }
                    else 
                    {
                        vkFreeMemory(Renderer->Device, Memory, nullptr);
                        vkDestroyBuffer(Renderer->Device, Buffer, nullptr);
                    }
                }
                else
                {
                    vkFreeMemory(Renderer->Device, Memory, nullptr);
                    vkDestroyBuffer(Renderer->Device, Buffer, nullptr);
                }
            }
            else 
            {
                vkDestroyBuffer(Renderer->Device, Buffer, nullptr);
            }
        }
        else 
        {
            vkDestroyBuffer(Renderer->Device, Buffer, nullptr);
        }
    }

    return Result;
}

bool StagingHeap_Copy(
    vulkan_staging_heap* Heap,
    VkQueue Queue,
    VkCommandBuffer CmdBuffer,
    u64 DestOffset, VkBuffer Dest,
    u64 SrcSize, const void* Src)
{
    assert(Heap);
    
    bool Result = false;

    // TODO: For now there's only one copy in-flight at maximum,
    //       but we'll want to use the staging heap as a ring-buffer
    //       and check the outstanding copy operations to find where to allocate the memory from
    u64 Offset = Heap->HeapOffset; // For now this is always 0
    u64 MapSize = AlignToPow2(SrcSize, Heap->Granularity);

    VkMappedMemoryRange Range = 
    {
        .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .pNext = nullptr,
        .memory = Heap->Heap,
        .offset = Offset,
        .size = MapSize,
    };
    
    if (SrcSize <= Heap->HeapSize)
    {
        void* MappedData = nullptr;
        if (vkMapMemory(Heap->Device, Range.memory, Range.offset, Range.size, 0, &MappedData) == VK_SUCCESS)
        {
            memcpy(MappedData, Src, SrcSize);

            vkFlushMappedMemoryRanges(Heap->Device, 1, &Range);
            vkUnmapMemory(Heap->Device, Range.memory);

            VkCommandBufferBeginInfo BeginInfo = 
            {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                .pNext = nullptr,
                .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
                .pInheritanceInfo = nullptr,
            };
            vkBeginCommandBuffer(CmdBuffer, &BeginInfo);
            VkBufferCopy CopyDesc = 
            {
                .srcOffset = Offset,
                .dstOffset = DestOffset,
                .size = SrcSize,
            };
            vkCmdCopyBuffer(CmdBuffer, Heap->Buffer, Dest, 1, &CopyDesc);
            vkEndCommandBuffer(CmdBuffer);

            VkSubmitInfo SubmitInfo = 
            {
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .pNext = nullptr,
                .waitSemaphoreCount = 0,
                .pWaitSemaphores = nullptr,
                .pWaitDstStageMask = nullptr,
                .commandBufferCount = 1,
                .pCommandBuffers = &CmdBuffer,
                .signalSemaphoreCount = 0,// TODO
                .pSignalSemaphores = nullptr, 
            };

            vkQueueSubmit(Queue, 1, &SubmitInfo, nullptr);
            vkQueueWaitIdle(Queue);

            Result = true;
        }
    }
    return Result;
}

bool StagingHeap_CopyImage(
    vulkan_staging_heap* Heap,
    VkQueue Queue,
    VkCommandBuffer CmdBuffer,
    VkImage Image,
    u32 Width, u32 Height, u32 MipCount, u32 ArrayCount,
    VkFormat Format,
    const void* Src)
{
    assert(Heap);

    bool Result = false;

    // TODO: For now there's only one copy in-flight at maximum,
    //       but we'll want to use the staging heap as a ring-buffer
    //       and check the outstanding copy operations to find where to allocate the memory from
    u64 BaseOffset = Heap->HeapOffset; // For now this is always 0

    u32 Stride = 0;
    switch (Format)
    {
        case VK_FORMAT_R8G8B8A8_SRGB: Stride = 4; break;
        case VK_FORMAT_R8_UNORM: Stride = 1; break;
    }

    if (Stride != 0)
    {
#if 1
        u64 SrcSize = 0;
        {
            u64 CurrentWidth = (u64)Width;
            u64 CurrentHeight = (u64)Height;

            for (u32 i = 0; i < MipCount; i++)
            {
                SrcSize += CurrentWidth*CurrentHeight * Stride;
            }

            SrcSize *= ArrayCount;
        }
#else
        u64 SrcSize = (u64)Width * Height * ArrayCount * 4;
#endif
        u64 MapSize = AlignToPow2(SrcSize, Heap->Granularity);

        VkMappedMemoryRange Range = 
        {
            .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .pNext = nullptr,
            .memory = Heap->Heap,
            .offset = BaseOffset,
            .size = MapSize,
        };

        void* MappedData = nullptr;
        if (vkMapMemory(Heap->Device, Heap->Heap, Range.offset, Range.size, 0, &MappedData) == VK_SUCCESS)
        {
            memcpy(MappedData, Src, SrcSize);

            vkFlushMappedMemoryRanges(Heap->Device, 1, &Range);
            vkUnmapMemory(Heap->Device, Heap->Heap);

            VkCommandBufferBeginInfo BeginInfo = 
            {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                .pNext = nullptr,
                .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
                .pInheritanceInfo = nullptr,
            };
            vkBeginCommandBuffer(CmdBuffer, &BeginInfo);

            VkImageMemoryBarrier BeginBarrier = 
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext = nullptr,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = Image,
                .subresourceRange = 
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = MipCount,
                    .baseArrayLayer = 0,
                    .layerCount = ArrayCount,
                },
            };

            vkCmdPipelineBarrier(
                CmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 
                0, nullptr,
                0, nullptr,
                1, &BeginBarrier);

            u64 Offset = BaseOffset;
            for (u32 Layer = 0; Layer < ArrayCount; Layer++)
            {
                u32 CurrentWidth = Width;
                u32 CurrentHeight = Height;
                for (u32 MipLevel = 0; MipLevel < MipCount; MipLevel++)
                {
                    VkBufferImageCopy CopyDesc = 
                    {
                        .bufferOffset = Offset,
                        .bufferRowLength = 0, // Tightly packed
                        .bufferImageHeight = 0,
                        .imageSubresource = 
                        {
                            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                            .mipLevel = MipLevel,
                            .baseArrayLayer = Layer,
                            .layerCount = 1,
                        },
                        .imageOffset = { 0, 0, 0 },
                        .imageExtent = { CurrentWidth, CurrentHeight, 1 },
                    };
                    vkCmdCopyBufferToImage(CmdBuffer, Heap->Buffer, Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &CopyDesc);

                    Offset += CurrentWidth*CurrentHeight * Stride;
                    CurrentWidth /= 2;
                    CurrentHeight /= 2;
                }
            }
            VkImageMemoryBarrier EndBarrier = 
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext = nullptr,
                .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = Image,
                .subresourceRange = 
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = MipCount,
                    .baseArrayLayer = 0,
                    .layerCount = ArrayCount,
                },
            };
            vkCmdPipelineBarrier(
                CmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 
                0, nullptr,
                0, nullptr,
                1, &EndBarrier);

            vkEndCommandBuffer(CmdBuffer);

             VkSubmitInfo SubmitInfo = 
            {
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .pNext = nullptr,
                .waitSemaphoreCount = 0,
                .pWaitSemaphores = nullptr,
                .pWaitDstStageMask = nullptr,
                .commandBufferCount = 1,
                .pCommandBuffers = &CmdBuffer,
                .signalSemaphoreCount = 0,// TODO
                .pSignalSemaphores = nullptr, 
            };

            vkQueueSubmit(Queue, 1, &SubmitInfo, nullptr);
            vkQueueWaitIdle(Queue);

            Result = true;
        }
    }
    else
    {
        assert(!"Unimplemented texture format");
    }

    return Result;
}

bool VB_Create(vulkan_vertex_buffer* VB, u32 MemoryTypes, u64 Size, VkDevice Device)
{
    assert(VB);
    memset(VB, 0, sizeof(vulkan_vertex_buffer));
    bool Result = false;

    Size = AlignTo(Size, sizeof(vertex));
    u64 VertexCount64 = Size / sizeof(vertex);
    assert(VertexCount64 <= 0xFFFFFFFF);

    u32 VertexCount = (u32)VertexCount64;

    VkBufferCreateInfo BufferInfo = 
    {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size = Size,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
    };

    VkBuffer Buffer = VK_NULL_HANDLE;
    if (vkCreateBuffer(Device, &BufferInfo, nullptr, &Buffer) == VK_SUCCESS)
    {
        VkMemoryRequirements MemoryRequirements = {};
        vkGetBufferMemoryRequirements(Device, Buffer, &MemoryRequirements);
        if (Size < MemoryRequirements.size)
        {
            Size = MemoryRequirements.size;
        }

        MemoryTypes &= MemoryRequirements.memoryTypeBits;
        u32 MemoryType = 0;
        if (BitScanForward(&MemoryType, MemoryTypes) != 0)
        {

            VkMemoryAllocateInfo AllocInfo = 
            {
                .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                .pNext = nullptr,
                .allocationSize = Size,
                .memoryTypeIndex = MemoryType,
            };

            VkDeviceMemory Memory = VK_NULL_HANDLE;
            if (vkAllocateMemory(Device, &AllocInfo, nullptr, &Memory) == VK_SUCCESS)
            {
                if (vkBindBufferMemory(Device, Buffer, Memory, 0) == VK_SUCCESS)
                {
                    VB->MemorySize = Size;
                    VB->Memory = Memory;
                    VB->Buffer = Buffer;
                    VB->MaxVertexCount = VertexCount;
                    
                    // Init memory blocks
                    {
                        VB->BlockCount = 1;
                        VB->Blocks[0] = 
                        {
                            .VertexCount = VB->MaxVertexCount,
                            .VertexOffset = 0,
                            .Flags = 0,
                        };
                    }

                    // Init allocation queue
                    VB->FreeAllocationRead = 0;
                    VB->FreeAllocationWrite = 0;
                    VB->FreeAllocationCount = vulkan_vertex_buffer::MaxAllocationCount;
                    for (u32 i = 0; i < vulkan_vertex_buffer::MaxAllocationCount; i++)
                    {
                        VB->FreeAllocationIndices[i] = i;
                    }

                    Result = true;
                }
                else
                {
                    vkFreeMemory(Device, Memory, nullptr);
                    vkDestroyBuffer(Device, Buffer, nullptr);
                }
            }
            else
            {
                vkDestroyBuffer(Device, Buffer, nullptr);
            }
        }
    }

    return Result;
}

u32 VB_Allocate(vulkan_vertex_buffer* VB, u32 VertexCount)
{
    assert(VB);
    
    u32 Result = INVALID_INDEX_U32;
    if (VB->FreeAllocationCount)
    {
        u32 AllocationIndex = VB->FreeAllocationIndices[VB->FreeAllocationRead];
        vulkan_vertex_buffer_allocation* Allocation = VB->Allocations + AllocationIndex;
        Allocation->BlockIndex = INVALID_INDEX_U32;

        auto FindAllocation = [&]() -> void
        {
            for (u32 i = 0; i < VB->BlockCount; i++)
            {
                vulkan_vertex_buffer_block* Block = VB->Blocks + i;
                if (!(Block->Flags & VBBLOCK_ALLOCATED_BIT) && (VertexCount <= Block->VertexCount))
                {
                    if (Block->VertexCount == VertexCount)
                    {
                        Block->Flags |= VBBLOCK_ALLOCATED_BIT;
                        Allocation->BlockIndex = i;
                    }
                    else
                    {
                        if (VB->BlockCount != vulkan_vertex_buffer::MaxAllocationCount)
                        {
                            vulkan_vertex_buffer_block* NextBlock = &VB->Blocks[VB->BlockCount++];
                            assert(!(NextBlock->Flags & VBBLOCK_ALLOCATED_BIT));

                            NextBlock->VertexCount = Block->VertexCount - VertexCount;
                            NextBlock->VertexOffset = Block->VertexOffset + VertexCount;
                            NextBlock->Flags &= ~VBBLOCK_ALLOCATED_BIT;
                            NextBlock->AllocationIndex = INVALID_INDEX_U32;

                            Block->VertexCount = VertexCount;
                            Block->Flags |= VBBLOCK_ALLOCATED_BIT;
                            Block->AllocationIndex = AllocationIndex;

                            Allocation->BlockIndex = i;
                        }
                        else
                        {
                            assert(!"Unimplemented code path");
                        }
                    }

                    break;
                }
            }
        };
        FindAllocation();

        // Defragment and try again
        if (Allocation->BlockIndex == INVALID_INDEX_U32)
        {
            VB_Defragment(VB);
            FindAllocation();
        }

        if (Allocation->BlockIndex != INVALID_INDEX_U32)
        {
            VB->FreeAllocationRead++;
            VB->FreeAllocationRead %= vulkan_vertex_buffer::MaxAllocationCount;
            VB->FreeAllocationCount--;

            VB->MemoryUsage += (u64)VertexCount * sizeof(vertex);

            Result = AllocationIndex;
        }
        else
        {
            assert(!"Fatal error");
        }
    }
    return Result;
}

void VB_Free(vulkan_vertex_buffer* VB, u32 AllocationIndex)
{
    assert(VB);
    
    if (AllocationIndex != INVALID_INDEX_U32)
    {
        assert(AllocationIndex < vulkan_vertex_buffer::MaxAllocationCount);
        
        vulkan_vertex_buffer_block* Block = VB->Blocks + VB->Allocations[AllocationIndex].BlockIndex;

        Block->Flags &= ~VBBLOCK_ALLOCATED_BIT;
        Block->AllocationIndex = INVALID_INDEX_U32;

        VB->FreeAllocationIndices[VB->FreeAllocationWrite++] = AllocationIndex;
        VB->FreeAllocationWrite %= vulkan_vertex_buffer::MaxAllocationCount;
        VB->FreeAllocationCount++;

        VB->MemoryUsage -= (u64)Block->VertexCount * sizeof(vertex);
    }
}

void VB_Defragment(vulkan_vertex_buffer* VB)
{
    TIMED_FUNCTION();

    assert(VB);

    u32 FreeBlockCount = 0;
    u32 FreeBlocks[VB->MaxAllocationCount];
    u32 AllocatedBlockCount = 0;
    u32 AllocatedBlocks[VB->MaxAllocationCount];

    for (u32 i = 0; i < VB->BlockCount; i++)
    {
        vulkan_vertex_buffer_block* Block = VB->Blocks + i;
        if (Block->Flags & VBBLOCK_ALLOCATED_BIT)
        {
            AllocatedBlocks[AllocatedBlockCount++] = i;
        }
        else
        {
            FreeBlocks[FreeBlockCount++] = i;
        }
    }

    assert(!"Unimplemented code path");
}

u64 VB_GetAllocationMemoryOffset(const vulkan_vertex_buffer* VB, u32 AllocationIndex)
{
    assert(VB);

    u32 BlockIndex = VB->Allocations[AllocationIndex].BlockIndex;
    u64 Result = (u64)VB->Blocks[BlockIndex].VertexOffset * sizeof(vertex);
    return Result;
}

bool Renderer_ResizeRenderTargets(vulkan_renderer* Renderer)
{
    assert(Renderer);
    assert(Renderer->Device);

    bool Result = false;

    vkDeviceWaitIdle(Renderer->Device);

    VkSurfaceCapabilitiesKHR SurfaceCaps = {};
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(Renderer->PhysicalDevice, Renderer->Surface, &SurfaceCaps) == VK_SUCCESS)
    {
        VkExtent2D CurrentExtent = SurfaceCaps.currentExtent;

        if ((CurrentExtent.width == 0) || (CurrentExtent.height == 0) ||
            ((CurrentExtent.width == Renderer->SwapchainSize.width) && (CurrentExtent.height == Renderer->SwapchainSize.height)))
        {
            return true;
        }

        VkSwapchainKHR OldSwapchain = Renderer->Swapchain;
        VkSwapchainCreateInfoKHR SwapchainInfo = 
        {
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .pNext = nullptr,
            .flags = 0,
            .surface = Renderer->Surface,
            .minImageCount = 2,
            .imageFormat = Renderer->SurfaceFormat.format,
            .imageColorSpace = Renderer->SurfaceFormat.colorSpace,
            .imageExtent = CurrentExtent,
            .imageArrayLayers = 1,
            .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
            .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
            .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            .presentMode = 
#if 1
                VK_PRESENT_MODE_FIFO_KHR,
#else
                VK_PRESENT_MODE_MAILBOX_KHR,
#endif
            .clipped = VK_FALSE,
            .oldSwapchain = OldSwapchain,
        };
        if (vkCreateSwapchainKHR(Renderer->Device, &SwapchainInfo, nullptr, &Renderer->Swapchain) == VK_SUCCESS)
        {
            // NOTE: this is safe because we waited for device idle
            vkDestroySwapchainKHR(Renderer->Device, OldSwapchain, nullptr);
            Renderer->SwapchainSize = CurrentExtent;

            vkGetSwapchainImagesKHR(
                Renderer->Device, 
                Renderer->Swapchain, 
                &Renderer->SwapchainImageCount, 
                nullptr);
            assert(Renderer->SwapchainImageCount <= 16);

            vkGetSwapchainImagesKHR(
                Renderer->Device, 
                Renderer->Swapchain, 
                &Renderer->SwapchainImageCount,
                Renderer->SwapchainImages);

            bool ImageViewCreationFailed = false;
            for (u32 i = 0; i < Renderer->SwapchainImageCount; i++)
            {
                if (Renderer->SwapchainImageViews[i]) 
                {
                    vkDestroyImageView(Renderer->Device, Renderer->SwapchainImageViews[i], nullptr);
                    Renderer->SwapchainImageViews[i] = VK_NULL_HANDLE;
                }

                VkImageViewCreateInfo ImageViewInfo = 
                {
                    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                    .pNext = nullptr,
                    .flags = 0,
                    .image = Renderer->SwapchainImages[i],
                    .viewType = VK_IMAGE_VIEW_TYPE_2D,
                    .format = Renderer->SurfaceFormat.format,
                    .components = {},
                    .subresourceRange = 
                    {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    },
                };

                if (vkCreateImageView(Renderer->Device, &ImageViewInfo, nullptr, &Renderer->SwapchainImageViews[i]) == VK_SUCCESS)
                {
                }
                else
                {
                    ImageViewCreationFailed = true;
                    // TODO: cleanup
                    break;
                }
            }

            if (!ImageViewCreationFailed)
            {
                // Create rendertarget images
                bool ImageCreationFailed = false;
                
                constexpr u32 RTMemoryRequirementsMaxCount = 32;
                u32 RTMemoryRequirementCount = 0;
                VkMemoryRequirements RTMemoryRequirements[RTMemoryRequirementsMaxCount];
                
                u32 RenderTargetSuitableMemoryTypes = Renderer->DeviceLocalMemoryTypes;
                // Depth buffers
                for (u32 i = 0; i < Renderer->SwapchainImageCount; i++)
                {
                    VkImageCreateInfo Info = 
                    {
                        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                        .pNext = nullptr,
                        .flags = 0,
                        .imageType = VK_IMAGE_TYPE_2D,
                        .format = VK_FORMAT_D32_SFLOAT,
                        .extent = { Renderer->SwapchainSize.width, Renderer->SwapchainSize.height, 1 },
                        .mipLevels = 1,
                        .arrayLayers = 1,
                        .samples = VK_SAMPLE_COUNT_1_BIT,
                        .tiling = VK_IMAGE_TILING_OPTIMAL,
                        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
                        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                        .queueFamilyIndexCount = 0,
                        .pQueueFamilyIndices = nullptr,
                        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                    };
                    if (vkCreateImage(Renderer->Device, &Info, nullptr, &Renderer->DepthBuffers[i]) == VK_SUCCESS)
                    {
                        Renderer->FrameParams[i].DepthBuffer = Renderer->DepthBuffers[i];
                    }
                    else
                    {
                        ImageCreationFailed = true;
                        // TODO: cleanup
                        break;
                    }
                    
                    assert(RTMemoryRequirementCount < 32);
                    vkGetImageMemoryRequirements(Renderer->Device, Renderer->DepthBuffers[i], &RTMemoryRequirements[RTMemoryRequirementCount++]);
                }

                if (!ImageCreationFailed)
                {
                    if ((Renderer->RTHeap.Heap != VK_NULL_HANDLE) ||
                        (RTHeap_Create(&Renderer->RTHeap, 64*1024*1024,
                                       Renderer->DeviceLocalMemoryTypes,
                                       RTMemoryRequirementCount, RTMemoryRequirements,
                                       Renderer->Device)))
                    {
                        // Reset the RT heap
                        Renderer->RTHeap.HeapOffset = 0;

                        bool MemoryAllocationFailed = false;
                        for (u32 i = 0; i < Renderer->SwapchainImageCount; i++)
                        {
                            if (!RTHeap_PushImage(&Renderer->RTHeap, Renderer->DepthBuffers[i]))
                            {
                                MemoryAllocationFailed = true;
                                // TODO: cleanup
                                break;
                            }
                            
                            if (Renderer->DepthBufferViews[i] != VK_NULL_HANDLE)
                            {
                                vkDestroyImageView(Renderer->Device, Renderer->DepthBufferViews[i], nullptr);
                                Renderer->DepthBufferViews[i] = VK_NULL_HANDLE;
                            }

                            VkImageViewCreateInfo ViewInfo = 
                            {
                                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                .pNext = nullptr,
                                .flags = 0,
                                .image = Renderer->DepthBuffers[i],
                                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                                .format = VK_FORMAT_D32_SFLOAT,
                                .components = {},
                                .subresourceRange = 
                                {
                                    .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                                    .baseMipLevel = 0,
                                    .levelCount = 1,
                                    .baseArrayLayer = 0,
                                    .layerCount = 1,
                                },
                            };
                            if (vkCreateImageView(Renderer->Device, &ViewInfo, nullptr, &Renderer->DepthBufferViews[i]) == VK_SUCCESS)
                            {
                                Renderer->FrameParams[i].DepthBufferView = Renderer->DepthBufferViews[i];
                            }
                            else
                            {
                                MemoryAllocationFailed = true;
                                // TODO: cleanup
                                break;
                            }
                        }
                        
                        if (!MemoryAllocationFailed)
                        {
                            Result = true;
                        }
                    }
                }
            }
        }
    }

    return Result;
}

bool Renderer_Initialize(vulkan_renderer* Renderer)
{
    assert(Renderer);
    memset(Renderer, 0, sizeof(vulkan_renderer));
    Renderer->GraphicsFamilyIndex = INVALID_INDEX_U32;
    Renderer->TransferFamilyIndex = INVALID_INDEX_U32;

    // Length for temporary buffers (enumeration results, etc.)
    constexpr size_t CommonBufferLength = 256;

    static const char* RequiredInstanceLayers[] = 
    {
        "VK_LAYER_KHRONOS_validation",
        //"VK_LAYER_KHRONOS_synchronization2",
    };
    static const char* RequiredInstanceExtensions[] = 
    {
        "VK_KHR_surface",
        "VK_KHR_get_physical_device_properties2",
        "VK_EXT_debug_report",
        "VK_EXT_debug_utils",
        "VK_EXT_validation_features",
#if defined(PLATFORM_WIN32)
        "VK_KHR_win32_surface",
#elif defined(PLATFORM_LINUX)
        "VK_KHR_xlib_surface",
#endif
    };
    static const char* RequiredDeviceLayers[] = 
    {
        "VK_LAYER_KHRONOS_validation",
        //"VK_LAYER_KHRONOS_synchronization2",
    };
    static const char* RequiredDeviceExtensions[] = 
    {
        "VK_KHR_swapchain",
        "VK_KHR_dynamic_rendering",
        "VK_EXT_extended_dynamic_state",
        "VK_EXT_extended_dynamic_state2",
        //"VK_KHR_synchronization2",
    };

    constexpr u32 RequiredInstanceLayerCount     = CountOf(RequiredInstanceLayers);
    constexpr u32 RequiredInstanceExtensionCount = CountOf(RequiredInstanceExtensions);
    constexpr u32 RequiredDeviceLayerCount       = CountOf(RequiredDeviceLayers);
    constexpr u32 RequiredDeviceExtensionCount   = CountOf(RequiredDeviceExtensions);

    // Layer extensions are the extensions that belong to a specific instance or device layer
    struct layer_extensions
    {
        u32 Count;
        VkExtensionProperties Extensions[CommonBufferLength];
    };

    u32 Version;
    vkEnumerateInstanceVersion(&Version);

    const u32 VersionMajor = VK_API_VERSION_MAJOR(Version);
    const u32 VersionMinor = VK_API_VERSION_MINOR(Version);
    const u32 VersionPatch = VK_API_VERSION_PATCH(Version);

    if (VersionMajor == 1 && VersionMinor < 2)
    {
        return false;
    }

    u32 InstanceLayerCount = 0;
    VkLayerProperties InstanceLayers[CommonBufferLength];
    {
        vkEnumerateInstanceLayerProperties(&InstanceLayerCount, nullptr);
        if (InstanceLayerCount > CommonBufferLength)
        {
            return false;
        }

        vkEnumerateInstanceLayerProperties(&InstanceLayerCount, InstanceLayers);
    }

    u32 InstanceExtensionCount = 0;
    VkExtensionProperties InstanceExtensions[CommonBufferLength];
    layer_extensions InstanceLayerExtensions[RequiredInstanceLayerCount];
    {
        vkEnumerateInstanceExtensionProperties(nullptr, &InstanceExtensionCount, nullptr);
        if (InstanceExtensionCount > CommonBufferLength)
        {
            return false;
        }
        vkEnumerateInstanceExtensionProperties(nullptr, &InstanceExtensionCount, InstanceExtensions);

        for (u32 i = 0; i < RequiredInstanceLayerCount; i++)
        {
            layer_extensions* LayerExtensions = InstanceLayerExtensions + i;

            vkEnumerateInstanceExtensionProperties(RequiredInstanceLayers[i], &LayerExtensions->Count, nullptr);
            if (LayerExtensions->Count > CommonBufferLength) 
            {
                return false;
            }
            vkEnumerateInstanceExtensionProperties(RequiredInstanceLayers[i], 
                                                   &LayerExtensions->Count, 
                                                   LayerExtensions->Extensions);
        }
    }

    VkResult Result = VK_SUCCESS;

    // Create instance
    {
        VkApplicationInfo AppInfo = 
        {
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pNext = nullptr,
            .pApplicationName = "blokker",
            .applicationVersion = 1,
            .pEngineName = "blokker",
            .engineVersion = 1,
            .apiVersion = Version,
        };

        VkInstanceCreateInfo CreateInfo = 
        {
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .pApplicationInfo = &AppInfo,
            .enabledLayerCount = RequiredInstanceLayerCount,
            .ppEnabledLayerNames = RequiredInstanceLayers,
            .enabledExtensionCount = RequiredInstanceExtensionCount,
            .ppEnabledExtensionNames = RequiredInstanceExtensions,
        };

        Result = vkCreateInstance(&CreateInfo, nullptr, &Renderer->Instance);
        if (Result != VK_SUCCESS) 
        {
            return false;
        }
    }

    // Enable debugging
    {
        vkCreateDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(Renderer->Instance, "vkCreateDebugUtilsMessengerEXT");
        if (!vkCreateDebugUtilsMessengerEXT)
        {
            return false;
        }

        VkDebugUtilsMessengerCreateInfoEXT Info = 
        {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .pNext = nullptr,
            .flags = 0,
            .messageSeverity = 
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT|
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType = 
                VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|
                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|
                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = &VulkanDebugCallback,
            .pUserData = nullptr,
        };

        Result = vkCreateDebugUtilsMessengerEXT(Renderer->Instance, &Info, nullptr, &Renderer->DebugMessenger);
        if (Result != VK_SUCCESS)
        {
            return false;
        }
    }

    // Enumerate and choose physical device
    {
        constexpr u32 MaxPhysicalDeviceCount = 8;

        u32 PhysicalDeviceCount;
        VkPhysicalDevice PhysicalDevices[MaxPhysicalDeviceCount];
        vulkan_physical_device_desc PhysicalDeviceDescs[MaxPhysicalDeviceCount];

        vkEnumeratePhysicalDevices(Renderer->Instance, &PhysicalDeviceCount, nullptr);
        if (PhysicalDeviceCount > MaxPhysicalDeviceCount) 
        {
            return false;
        }
        vkEnumeratePhysicalDevices(Renderer->Instance, &PhysicalDeviceCount, PhysicalDevices);

        for (u32 i = 0; i < PhysicalDeviceCount; i++)
        {
            vulkan_physical_device_desc* Desc = PhysicalDeviceDescs + i;
            vkGetPhysicalDeviceProperties(PhysicalDevices[i], &Desc->Props);
            vkGetPhysicalDeviceMemoryProperties(PhysicalDevices[i], &Desc->MemoryProps);

            vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevices[i], &Desc->QueueFamilyCount, nullptr);
            if (Desc->QueueFamilyCount > vulkan_physical_device_desc::MaxQueueFamilyCount)
            {
                return false;
            }
            vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevices[i], &Desc->QueueFamilyCount, Desc->QueueFamilyProps);
        }

        // Choose device
        {
            u32 SelectedDeviceIndex = INVALID_INDEX_U32;
            for (u32 i = 0; i < PhysicalDeviceCount; i++)
            {
                if (PhysicalDeviceDescs[i].Props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
                {
                    SelectedDeviceIndex = i;
                    break;
                }
            }

            if (SelectedDeviceIndex == INVALID_INDEX_U32)
            {
                DebugPrint("Error: no suitable GPU found\n");
                return false;
            }
            Renderer->PhysicalDevice = PhysicalDevices[SelectedDeviceIndex];
            Renderer->DeviceDesc = PhysicalDeviceDescs[SelectedDeviceIndex];
            

            Renderer->NonCoherentAtomSize = Renderer->DeviceDesc.Props.limits.nonCoherentAtomSize;

            // Create memory type bit masks
            for (u32 i = 0; i < Renderer->DeviceDesc.MemoryProps.memoryTypeCount; i++)
            {
                const VkMemoryType* MemoryType = Renderer->DeviceDesc.MemoryProps.memoryTypes + i;
                bool IsDeviceLocal = (MemoryType->propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;
                bool IsHostVisible = (MemoryType->propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
                bool IsHostCoherent = (MemoryType->propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
                if (IsDeviceLocal && !IsHostVisible)
                {
                    Renderer->DeviceLocalMemoryTypes |= (1 << i);
                }
                if (!IsDeviceLocal && IsHostVisible)
                {
                    Renderer->HostVisibleMemoryTypes |= (1 << i);
                }
                if (!IsDeviceLocal && IsHostVisible && IsHostCoherent)
                {
                    Renderer->HostVisibleCoherentMemoryTypes |= (1 << i);
                }
                if (IsDeviceLocal && IsHostVisible)
                {
                    Renderer->DeviceLocalAndHostVisibleMemoryTypes |= (1 << i);
                }
            }
        }
    }

    // Create logical device
    {
        // TODO: check for required layers + extensions
        u32 DeviceLayerCount;
        VkLayerProperties DeviceLayers[CommonBufferLength];
        {
            vkEnumerateDeviceLayerProperties(Renderer->PhysicalDevice, &DeviceLayerCount, nullptr);
            if (DeviceLayerCount > CommonBufferLength)
            {
                return false;
            }
            vkEnumerateDeviceLayerProperties(Renderer->PhysicalDevice, &DeviceLayerCount, DeviceLayers);
        }

        u32 DeviceExtensionCount;
        VkExtensionProperties DeviceExtensions[CommonBufferLength];
        {
            vkEnumerateDeviceExtensionProperties(Renderer->PhysicalDevice, nullptr, 
                                                 &DeviceExtensionCount, nullptr);
            if (DeviceExtensionCount > CommonBufferLength)
            {
                return false;
            }
            vkEnumerateDeviceExtensionProperties(Renderer->PhysicalDevice, nullptr, 
                                                 &DeviceExtensionCount, DeviceExtensions);
        }

        layer_extensions DeviceLayerExtensions[RequiredDeviceLayerCount];
        for (u32 i = 0; i < RequiredDeviceLayerCount; i++)
        {
            vkEnumerateDeviceExtensionProperties(Renderer->PhysicalDevice, RequiredDeviceLayers[i],
                                                 &DeviceLayerExtensions[i].Count, nullptr);
            if (DeviceLayerExtensions[i].Count > CommonBufferLength)
            {
                return false;
            }
            vkEnumerateDeviceExtensionProperties(Renderer->PhysicalDevice, RequiredDeviceLayers[i],
                                                 &DeviceLayerExtensions[i].Count, DeviceLayerExtensions[i].Extensions);
        }


        
        VkPhysicalDeviceDynamicRenderingFeaturesKHR DynamicRenderingFeatures = 
        {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR,
            .pNext = nullptr,
            .dynamicRendering = VK_FALSE,
        };

        VkPhysicalDeviceExtendedDynamicState2FeaturesEXT DynamicStateFeatures2 = 
        {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_2_FEATURES_EXT,
            .pNext = &DynamicRenderingFeatures,
        };

        VkPhysicalDeviceExtendedDynamicStateFeaturesEXT DynamicStateFeatures = 
        {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT,
            .pNext = &DynamicStateFeatures2,
        };

        VkPhysicalDeviceFeatures2 DeviceFeatures = 
        {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .pNext = &DynamicStateFeatures,
        };

        vkGetPhysicalDeviceFeatures2(Renderer->PhysicalDevice, &DeviceFeatures);
        if (!(DynamicRenderingFeatures.dynamicRendering) ||
            !(DynamicStateFeatures.extendedDynamicState) ||
            !(DynamicStateFeatures2.extendedDynamicState2))
        {
            return false;
        }

        for (u32 i = 0; i < Renderer->DeviceDesc.QueueFamilyCount; i++)
        {
            VkQueueFlags Flags = Renderer->DeviceDesc.QueueFamilyProps[i].queueFlags;
            
            if ((Flags & VK_QUEUE_TRANSFER_BIT) &&
                (Flags & VK_QUEUE_COMPUTE_BIT) &&
                (Flags & VK_QUEUE_SPARSE_BINDING_BIT))\
            {
                Renderer->GraphicsFamilyIndex = i;
            }

            if ((Flags & VK_QUEUE_TRANSFER_BIT) &&
                !(Flags & VK_QUEUE_COMPUTE_BIT) &&
                !(Flags & VK_QUEUE_COMPUTE_BIT))
            {
                Renderer->TransferFamilyIndex = i;
            }
        }

        if ((Renderer->GraphicsFamilyIndex == INVALID_INDEX_U32) ||
            (Renderer->TransferFamilyIndex == INVALID_INDEX_U32))
        {
            return false;
        }

        const f32 MaxPriority = 1.0f;
        VkDeviceQueueCreateInfo QueueCreateInfos[] = 
        {
            {
                .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .queueFamilyIndex = Renderer->GraphicsFamilyIndex,
                .queueCount = 1,
                .pQueuePriorities = &MaxPriority,
            },
            {
                .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .queueFamilyIndex = Renderer->TransferFamilyIndex,
                .queueCount = 1,
                .pQueuePriorities = &MaxPriority,
            },
        };
        constexpr u32 QueueCreateInfoCount = CountOf(QueueCreateInfos);

        VkDeviceCreateInfo DeviceCreateInfo = 
        {
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .pNext = &DeviceFeatures,
            .flags = 0,
            .queueCreateInfoCount = QueueCreateInfoCount,
            .pQueueCreateInfos = QueueCreateInfos,
            .enabledLayerCount = RequiredDeviceLayerCount,
            .ppEnabledLayerNames = RequiredDeviceLayers,
            .enabledExtensionCount = RequiredDeviceExtensionCount,
            .ppEnabledExtensionNames = RequiredDeviceExtensions,
            .pEnabledFeatures = nullptr,
        };

        Result = vkCreateDevice(Renderer->PhysicalDevice, &DeviceCreateInfo, nullptr, &Renderer->Device);
        if (Result != VK_SUCCESS)
        {
            return false;
        }

        vkGetDeviceQueue(Renderer->Device, Renderer->GraphicsFamilyIndex, 0, &Renderer->GraphicsQueue);
        vkGetDeviceQueue(Renderer->Device, Renderer->TransferFamilyIndex, 0, &Renderer->TransferQueue);
    }

    vkCmdBeginRenderingKHR = (PFN_vkCmdBeginRenderingKHR)vkGetInstanceProcAddr(Renderer->Instance, "vkCmdBeginRenderingKHR");
    vkCmdEndRenderingKHR   = (PFN_vkCmdEndRenderingKHR  )vkGetInstanceProcAddr(Renderer->Instance, "vkCmdEndRenderingKHR");
    if (!vkCmdBeginRenderingKHR || !vkCmdEndRenderingKHR)
    {
        return false;
    }

    vkCmdSetPrimitiveTopologyEXT = (PFN_vkCmdSetPrimitiveTopologyEXT)vkGetInstanceProcAddr(Renderer->Instance, "vkCmdSetPrimitiveTopologyEXT");
    if (!vkCmdSetPrimitiveTopologyEXT)
    {
        return false;
    }

    Renderer->Surface = CreateVulkanSurface(Renderer->Instance);
    if (Renderer->Surface == VK_NULL_HANDLE)
    {
        return false;
    }

    {
        VkSurfaceCapabilitiesKHR SurfaceCaps;
        Result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(Renderer->PhysicalDevice, Renderer->Surface, &SurfaceCaps);
        if (Result != VK_SUCCESS)
        {
            return false;
        }

        // TODO
        if ((SurfaceCaps.currentExtent.width == 0) || (SurfaceCaps.currentExtent.height == 0))
        {
            return false;
        }

        u32 SurfaceFormatCount = 0;
        VkSurfaceFormatKHR SurfaceFormats[64];
        {
            vkGetPhysicalDeviceSurfaceFormatsKHR(Renderer->PhysicalDevice, Renderer->Surface, &SurfaceFormatCount, nullptr);
            if (SurfaceFormatCount > 64)
            {
                return false;
            }
            vkGetPhysicalDeviceSurfaceFormatsKHR(Renderer->PhysicalDevice, Renderer->Surface, &SurfaceFormatCount, SurfaceFormats);
        }
        
        // NOTE: the order of this array defines which formats are preferred over others
        const VkFormat Formats[] = 
        {
            VK_FORMAT_R8G8B8A8_SRGB,
            VK_FORMAT_B8G8R8A8_SRGB,
        };
        constexpr u32 FormatCount = CountOf(Formats);

        bool FormatFound = false;
        for (u32 i = 0; i < FormatCount && !FormatFound; i++)
        {
            for (u32 j = 0; j < SurfaceFormatCount && !FormatFound; j++)
            {
                if (SurfaceFormats[j].format == Formats[i])
                {
                    FormatFound = true;
                    Renderer->SurfaceFormat = SurfaceFormats[i];
                }
            }
        }
        if (!FormatFound)
        {
            return false;
        }
    }

    Renderer_ResizeRenderTargets(Renderer);

    {
        VkCommandPoolCreateInfo CmdPoolInfo =
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = Renderer->TransferFamilyIndex,
        };

        Result = vkCreateCommandPool(Renderer->Device, &CmdPoolInfo, nullptr, &Renderer->TransferCmdPool);
        if (Result != VK_SUCCESS)
        {
            return false;
        }

        VkCommandBufferAllocateInfo AllocInfo = 
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext = nullptr,
            .commandPool = Renderer->TransferCmdPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        Result = vkAllocateCommandBuffers(Renderer->Device, &AllocInfo, &Renderer->TransferCmdBuffer);
        if (Result != VK_SUCCESS)
        {
            return false;
        }
    }

    for (u32 i = 0; i < Renderer->SwapchainImageCount;i ++)
    {
        {
            VkCommandPoolCreateInfo Info = 
            {
                .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                .pNext = nullptr,
                .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                .queueFamilyIndex = Renderer->GraphicsFamilyIndex,
            };
        
            Result = vkCreateCommandPool(Renderer->Device, &Info, nullptr, &Renderer->FrameParams[i].CmdPool);
            if (Result != VK_SUCCESS)
            {
                return false;
            }
        }
        
        {
            VkCommandBufferAllocateInfo Info = 
            {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                .pNext = nullptr,
                .commandPool = Renderer->FrameParams[i].CmdPool,
                .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                .commandBufferCount = 1,
            };
            Result = vkAllocateCommandBuffers(Renderer->Device, &Info, &Renderer->FrameParams[i].CmdBuffer);
            if (Result != VK_SUCCESS)
            {
                return false;
            }
        }

        {
            VkFenceCreateInfo Info = 
            {
                .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                .pNext = nullptr,
                .flags = VK_FENCE_CREATE_SIGNALED_BIT,
            };

            Result = vkCreateFence(Renderer->Device, &Info, nullptr, &Renderer->FrameParams[i].RenderFinishedFence);
            if (Result != VK_SUCCESS)
            {
                return false;
            }
        }
        {
            VkSemaphoreCreateInfo Info = 
            {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
            };

            Result = vkCreateSemaphore(Renderer->Device, &Info, nullptr, &Renderer->FrameParams[i].ImageAcquiredSemaphore);
            if (Result != VK_SUCCESS)
            {
                return false;
            }
            Result = vkCreateSemaphore(Renderer->Device, &Info, nullptr, &Renderer->FrameParams[i].RenderFinishedSemaphore);
        }
    }

    
    if (!StagingHeap_Create(&Renderer->StagingHeap, 256*1024*1024, Renderer))
    {
        return false;
    }

    // Vertex buffer
    if (!VB_Create(&Renderer->VB, Renderer->DeviceLocalMemoryTypes, 1024*1024*1024, Renderer->Device))
    {
        return false;
    }

    // Create per frame vertex stack
    for (u32 i = 0; i < Renderer->SwapchainImageCount; i++)
    {
        constexpr u64 VertexStackSize = 64*1024*1024;
        renderer_frame_params* Frame = Renderer->FrameParams + i;

        VkBufferCreateInfo BufferInfo = 
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .size = VertexStackSize,
            .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
        };
        VkBuffer Buffer;
        if (vkCreateBuffer(Renderer->Device, &BufferInfo, nullptr, &Buffer) == VK_SUCCESS)
        {
            VkMemoryRequirements MemoryRequirements = {};
            vkGetBufferMemoryRequirements(Renderer->Device, Buffer, &MemoryRequirements);

            assert(MemoryRequirements.size == VertexStackSize);

            u32 MemoryTypes = MemoryRequirements.memoryTypeBits & Renderer->HostVisibleCoherentMemoryTypes;
            u32 MemoryType = 0;
            if (BitScanForward(&MemoryType, MemoryTypes) != 0)
            {
                VkMemoryAllocateInfo AllocInfo = 
                {
                    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                    .pNext = nullptr,
                    .allocationSize = VertexStackSize,
                    .memoryTypeIndex = MemoryType,
                };

                VkDeviceMemory Memory;
                if (vkAllocateMemory(Renderer->Device, &AllocInfo, nullptr, &Memory) == VK_SUCCESS)
                {
                    if (vkBindBufferMemory(Renderer->Device, Buffer, Memory, 0) == VK_SUCCESS)
                    {
                        // NOTE(boti): persistent mapping
                        void* Mapping = nullptr;
                        if (vkMapMemory(Renderer->Device, Memory, 0, VK_WHOLE_SIZE, 0, &Mapping) == VK_SUCCESS)
                        {
                            Frame->VertexStack.Memory = Memory;
                            Frame->VertexStack.Buffer = Buffer;
                            Frame->VertexStack.Size = VertexStackSize;
                            Frame->VertexStack.At = 0;
                            Frame->VertexStack.Mapping = Mapping;
                        }
                        else
                        {
                            vkFreeMemory(Renderer->Device, Memory, nullptr);
                            vkDestroyBuffer(Renderer->Device, Buffer, nullptr);
                            return false;
                        }
                    }
                    else
                    {
                        vkFreeMemory(Renderer->Device, Memory, nullptr);
                        vkDestroyBuffer(Renderer->Device, Buffer, nullptr);
                        return false;
                    }
                }
                else
                {
                    vkDestroyBuffer(Renderer->Device, Buffer, nullptr);
                    return false;
                }
            }
            else
            {
                vkDestroyBuffer(Renderer->Device, Buffer, nullptr);
                return false;
            }
        }
        else
        {
            return false;
        }
    }

    // Descriptors
    {
        // Static sampler
        {
            VkSamplerCreateInfo SamplerInfo = 
            {
                .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .magFilter = VK_FILTER_NEAREST,
                .minFilter = VK_FILTER_NEAREST,
                .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
                .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                .mipLodBias = 0.0f,
                .anisotropyEnable = VK_TRUE,
                .maxAnisotropy = 8.0f,
                .compareEnable = VK_FALSE,
                .compareOp = VK_COMPARE_OP_ALWAYS,
                .minLod = 0.0f,
                .maxLod = VK_LOD_CLAMP_NONE,
                .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
                .unnormalizedCoordinates = VK_FALSE,
            };
            VkSampler Sampler = VK_NULL_HANDLE;
            if (vkCreateSampler(Renderer->Device, &SamplerInfo, nullptr, &Sampler) == VK_SUCCESS)
            {
                Renderer->Sampler = Sampler;
            }
            else
            {
                return false;
            }
        }

        // Set layout
        {
            VkDescriptorSetLayoutBinding Bindings[] = 
            {
                {
                    .binding = 0,
                    .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    .descriptorCount = 1,
                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                    .pImmutableSamplers = nullptr,
                },
                {
                    .binding = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                    .descriptorCount = 1,
                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                    .pImmutableSamplers = &Renderer->Sampler,
                },
            };
            constexpr u32 BindingCount = CountOf(Bindings);

            VkDescriptorSetLayoutCreateInfo CreateInfo = 
            {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .bindingCount = BindingCount,
                .pBindings = Bindings,
            };
            VkDescriptorSetLayout Layout = VK_NULL_HANDLE;
            if (vkCreateDescriptorSetLayout(Renderer->Device, &CreateInfo, nullptr, &Layout) == VK_SUCCESS)
            {
                Renderer->DescriptorSetLayout = Layout;
            }
            else
            {
                return false;
            }
        }

        // Descriptor pool + set
        {
            VkDescriptorPoolSize PoolSizes[] = 
            {
                { .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount = 1, },
                { .type = VK_DESCRIPTOR_TYPE_SAMPLER, .descriptorCount = 1, },
            };
            constexpr u32 PoolSizeCount = CountOf(PoolSizes);

            VkDescriptorPoolCreateInfo PoolInfo = 
            {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .maxSets = 1,
                .poolSizeCount = PoolSizeCount,
                .pPoolSizes = PoolSizes,
            };

            VkDescriptorPool Pool = VK_NULL_HANDLE;
            if (vkCreateDescriptorPool(Renderer->Device, &PoolInfo, nullptr, &Pool) == VK_SUCCESS)
            {
                VkDescriptorSetAllocateInfo AllocInfo =
                {
                    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                    .pNext = nullptr,
                    .descriptorPool = Pool,
                    .descriptorSetCount = 1,
                    .pSetLayouts = &Renderer->DescriptorSetLayout,
                };

                VkDescriptorSet DescriptorSet = VK_NULL_HANDLE;
                if (vkAllocateDescriptorSets(Renderer->Device, &AllocInfo, &DescriptorSet) == VK_SUCCESS)
                {
                    Renderer->DescriptorPool = Pool;
                    Renderer->DescriptorSet = DescriptorSet;
                }
                else
                {
                    vkDestroyDescriptorPool(Renderer->Device, Pool, nullptr);
                    return false;
                }
            }
            else
            {
                return false;
            }
        }
    }

    // Textures
    {
        constexpr u32 TexWidth = 16;
        constexpr u32 TexHeight = 16;
        constexpr u32 TexMaxArrayCount = 64;
        constexpr u32 TexMipCount = 4;
        
        // Pixel count for all mip levels
        u32 TexturePixelCount = 0;
        {
            u32 CurrentWidth = TexWidth;
            u32 CurrentHeight = TexHeight;
            for (u32 i = 0; i < TexMipCount; i++)
            {
                TexturePixelCount += CurrentWidth*CurrentHeight;
                CurrentWidth /= 2;
                CurrentHeight /= 2;
            }
        }
        u32* PixelBuffer = new u32[TexWidth*TexHeight*TexMaxArrayCount];
        u32* PixelBufferAt = PixelBuffer;
        struct image
        {
            u32 Width;
            u32 Height;
            VkFormat Format;
            u32* Pixels;
        };

        auto LoadBMP = [&PixelBufferAt](const char* Path, image* Image) -> bool
        {
            bool Result = false;

            CBuffer Buffer = LoadEntireFile(Path);
            if (Buffer.Data && Buffer.Size)
            {
                bmp_file* Bitmap = (bmp_file*)Buffer.Data;
                if ((Bitmap->File.Tag == BMP_FILE_TAG) && (Bitmap->File.Offset == offsetof(bmp_file, Data)))
                {
                    if ((Bitmap->Info.HeaderSize == sizeof(bmp_info_header)) &&
                        (Bitmap->Info.Planes == 1) &&
                        (Bitmap->Info.BitCount == 24) &&
                        (Bitmap->Info.Compression == BMP_COMPRESSION_NONE))
                    {
                        Image->Width = (u32)Bitmap->Info.Width;
                        Image->Height = (u32)Abs(Bitmap->Info.Height); // TODO: flip on negative height
                        Image->Format = VK_FORMAT_R8G8B8A8_SRGB;

                        assert((Image->Width == TexWidth) && (Image->Height == TexHeight));

                        u32 PixelCount = Image->Width * Image->Height;
                        Image->Pixels = PixelBufferAt;
                        PixelBufferAt += PixelCount;

                        if (Image->Pixels)
                        {
                            u8* Src = Bitmap->Data;
                            u32* Dest = Image->Pixels;

                            for (u32 i = 0; i < PixelCount; i++)
                            {
                                u8 B = *Src++;
                                u8 G = *Src++;
                                u8 R = *Src++;

                                *Dest++ = PackColor(R, G, B);
                            }

                            // Generate mips
                            {
                                u32* PrevMipLevel = Image->Pixels;
                                
                                u32 PrevWidth = TexWidth;
                                u32 PrevHeight = TexHeight;
                                u32 CurrentWidth = TexWidth / 2;
                                u32 CurrentHeight = TexHeight / 2;
                                for (u32 i = 0; i < TexMipCount - 1; i++)
                                {
                                    u32 MipSize = CurrentWidth*CurrentHeight;
                                    u32* CurrentMipLevel = PixelBufferAt;
                                    PixelBufferAt += MipSize;
                                    
                                    for (u32 y = 0; y < CurrentHeight; y++)
                                    {
                                        for (u32 x = 0; x < CurrentWidth; x++)
                                        {
                                            u32 Index00 = (2*x + 0) + (2*y + 0)*PrevWidth;
                                            u32 Index10 = (2*x + 1) + (2*y + 0)*PrevWidth;
                                            u32 Index01 = (2*x + 0) + (2*y + 1)*PrevWidth;
                                            u32 Index11 = (2*x + 1) + (2*y + 1)*PrevWidth;

                                            u32 c00 = PrevMipLevel[Index00];
                                            u32 c10 = PrevMipLevel[Index10];
                                            u32 c01 = PrevMipLevel[Index01];
                                            u32 c11 = PrevMipLevel[Index11];

                                            vec3 C00 = UnpackColor3(c00);
                                            vec3 C10 = UnpackColor3(c10);
                                            vec3 C01 = UnpackColor3(c01);
                                            vec3 C11 = UnpackColor3(c11);

                                            vec3 Out = 0.25f * (C00 + C10 + C01 + C11);

                                            u32 OutIndex = x + y*CurrentWidth;
                                            CurrentMipLevel[OutIndex] = PackColor(Out);
                                        }
                                    }

                                    PrevMipLevel = CurrentMipLevel;

                                    PrevWidth = CurrentWidth;
                                    PrevHeight = CurrentHeight;

                                    CurrentWidth /= 2;
                                    CurrentHeight /= 2;
                                }

                                Result = true;
                            }
                        }
                    }
                }
            }

            return Result;
        };

        static const char* TexturePaths[] = 
        {
            "texture/ground_side.bmp",
            "texture/ground_top.bmp",
            "texture/ground_bottom.bmp",
        };
        constexpr u32 TextureCount = CountOf(TexturePaths);
        static_assert(TextureCount <= TexMaxArrayCount);
        image Images[TextureCount];
        for (u32 i = 0; i < TextureCount; i++)
        {
            if (!LoadBMP(TexturePaths[i], &Images[i]))
            {
                return false;
            }
        }

        u32 TotalTexMemorySize = (u32)(PixelBufferAt - PixelBuffer) * sizeof(u32);

        {
            VkImageCreateInfo CreateInfo = 
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .imageType = VK_IMAGE_TYPE_2D,
                .format = VK_FORMAT_R8G8B8A8_SRGB,
                .extent = { TexWidth, TexHeight, 1 },
                .mipLevels = TexMipCount,
                .arrayLayers = TextureCount,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .tiling = VK_IMAGE_TILING_OPTIMAL,
                .usage = VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                .queueFamilyIndexCount = 0,
                .pQueueFamilyIndices = nullptr,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            };

            VkImage Image = VK_NULL_HANDLE;
            if (vkCreateImage(Renderer->Device, &CreateInfo, nullptr, &Image) == VK_SUCCESS)
            {
                VkMemoryRequirements MemoryRequirements = {};
                vkGetImageMemoryRequirements(Renderer->Device, Image, &MemoryRequirements);

                u32 MemoryType = 0;
                if (BitScanForward(&MemoryType, MemoryRequirements.memoryTypeBits & Renderer->DeviceLocalMemoryTypes))
                {
                    VkMemoryAllocateInfo AllocInfo = 
                    {
                        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                        .pNext = nullptr,
                        .allocationSize = MemoryRequirements.size,
                        .memoryTypeIndex = MemoryType,
                    };

                    VkDeviceMemory Memory;
                    if (vkAllocateMemory(Renderer->Device, &AllocInfo, nullptr, &Memory) == VK_SUCCESS)
                    {
                        if (vkBindImageMemory(Renderer->Device, Image, Memory, 0) == VK_SUCCESS)
                        {
                            if (StagingHeap_CopyImage(&Renderer->StagingHeap, 
                                Renderer->TransferQueue,
                                Renderer->TransferCmdBuffer,
                                Image, 
                                TexWidth, TexHeight, TexMipCount, TextureCount,
                                VK_FORMAT_R8G8B8A8_SRGB,
                                PixelBuffer))
                            {
                                VkImageViewCreateInfo ViewInfo = 
                                {
                                    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                    .pNext = nullptr,
                                    .flags = 0,
                                    .image = Image,
                                    .viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
                                    .format = VK_FORMAT_R8G8B8A8_SRGB,
                                    .components = {},
                                    .subresourceRange = 
                                    {
                                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                        .baseMipLevel = 0,
                                        .levelCount = TexMipCount,
                                        .baseArrayLayer = 0,
                                        .layerCount = TextureCount,
                                    },
                                };

                                VkImageView ImageView = VK_NULL_HANDLE;
                                if (vkCreateImageView(Renderer->Device, &ViewInfo, nullptr, &ImageView) == VK_SUCCESS)
                                {
                                    VkDescriptorImageInfo ImageInfo = 
                                    {
                                        .sampler = VK_NULL_HANDLE,
                                        .imageView = ImageView,
                                        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                    };
                                    VkWriteDescriptorSet DescriptorWrite = 
                                    {
                                        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                        .pNext = nullptr,
                                        .dstSet = Renderer->DescriptorSet,
                                        .dstBinding = 0,
                                        .dstArrayElement = 0,
                                        .descriptorCount = 1,
                                        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                        .pImageInfo = &ImageInfo,
                                        .pBufferInfo = nullptr,
                                        .pTexelBufferView = nullptr,
                                    };
                                    vkUpdateDescriptorSets(Renderer->Device, 1, &DescriptorWrite, 0, nullptr);

                                    Renderer->Tex = Image;
                                    Renderer->TexView = ImageView;
                                    Renderer->TexMemory = Memory;
                                }
                                else
                                {
                                    vkFreeMemory(Renderer->Device, Memory, nullptr);
                                    vkDestroyImage(Renderer->Device, Image, nullptr);
                                    return false;
                                }

                            }
                            else
                            {
                                vkFreeMemory(Renderer->Device, Memory, nullptr);
                                vkDestroyImage(Renderer->Device, Image, nullptr);
                                return false;
                            }
                        }
                        else
                        {
                            vkFreeMemory(Renderer->Device, Memory, nullptr);
                            vkDestroyImage(Renderer->Device, Image, nullptr);
                            return false;
                        }
                    }
                    else
                    {
                        vkDestroyImage(Renderer->Device, Image, nullptr);
                        return false;
                    }
                }
                else
                {
                    vkDestroyImage(Renderer->Device, Image, nullptr);
                    return false;
                }
            }
            else
            {
                return false;
            }
        }

        delete[] PixelBuffer;
    }

    // Main pipeline
    {
        // Create pipeline layout
        {
            VkPushConstantRange PushConstants = 
            {
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                .offset = 0,
                .size = sizeof(mat4),
            };
            VkPipelineLayoutCreateInfo Info = 
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .setLayoutCount = 1,
                .pSetLayouts = &Renderer->DescriptorSetLayout,
                .pushConstantRangeCount = 1,
                .pPushConstantRanges = &PushConstants,
            };
            
            Result = vkCreatePipelineLayout(Renderer->Device, &Info, nullptr, &Renderer->PipelineLayout);
            if (Result != VK_SUCCESS)
            {
                return false;
            }
        }
        
        //  Create shaders
        VkShaderModule VSModule, FSModule;
        {
            CBuffer VSBin = LoadEntireFile("shader/shader.vs");
            CBuffer FSBin = LoadEntireFile("shader/shader.fs");
            
            assert((VSBin.Size > 0) && (FSBin.Size > 0));
            
            VkShaderModuleCreateInfo VSInfo = 
            {
                .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .codeSize = VSBin.Size,
                .pCode = (u32*)VSBin.Data,
            };
            VkShaderModuleCreateInfo FSInfo =
            {
                .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .codeSize = FSBin.Size,
                .pCode = (u32*)FSBin.Data,
            };
            
            Result = vkCreateShaderModule(Renderer->Device, &VSInfo, nullptr, &VSModule);
            if (Result != VK_SUCCESS)
            {
                return false;
            }
            Result = vkCreateShaderModule(Renderer->Device, &FSInfo, nullptr, &FSModule);
            if (Result != VK_SUCCESS)
            {
                return false;
            }
        }
        
        VkPipelineShaderStageCreateInfo ShaderStages[] = 
        {
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                    .pNext = nullptr,
                    .flags = 0,
                    .stage = VK_SHADER_STAGE_VERTEX_BIT,
                    .module = VSModule,
                    .pName = "main",
                    .pSpecializationInfo = nullptr,
            },
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                    .pNext = nullptr,
                    .flags = 0,
                    .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                    .module = FSModule,
                    .pName = "main",
                    .pSpecializationInfo = nullptr,
            },
        };
        constexpr u32 ShaderStageCount = CountOf(ShaderStages);
        
        VkVertexInputBindingDescription VertexBinding = 
        {
            .binding = 0,
            .stride = sizeof(vertex),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        };
        
        VkVertexInputAttributeDescription VertexAttribs[] = 
        {
            // Pos
            {
                .location = ATTRIB_POS,
                .binding = 0,
                .format = VK_FORMAT_R32G32B32_SFLOAT,
                .offset = offsetof(vertex, P),
            },
            // UVW
            {
                .location = ATTRIB_TEXCOORD,
                .binding = 0,
                .format = VK_FORMAT_R32G32B32_SFLOAT,
                .offset = offsetof(vertex, UVW),
            },
            // Color
            {
                .location = ATTRIB_COLOR,
                .binding = 0,
                .format = VK_FORMAT_R8G8B8A8_UNORM,
                .offset = offsetof(vertex, Color),
            },
        };
        constexpr u32 VertexAttribCount = CountOf(VertexAttribs);
        
        VkPipelineVertexInputStateCreateInfo VertexInputState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .vertexBindingDescriptionCount = 1,
            .pVertexBindingDescriptions = &VertexBinding,
            .vertexAttributeDescriptionCount = VertexAttribCount,
            .pVertexAttributeDescriptions = VertexAttribs,
        };
        
        VkPipelineInputAssemblyStateCreateInfo InputAssemblyState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            .primitiveRestartEnable = VK_FALSE,
        };
        
        VkViewport Viewport = {};
        VkRect2D Scissor = {};
        
        VkPipelineViewportStateCreateInfo ViewportState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .viewportCount = 1,
            .pViewports = &Viewport,
            .scissorCount = 1,
            .pScissors = &Scissor,
        };
        
        VkPipelineRasterizationStateCreateInfo RasterizationState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .depthClampEnable = VK_FALSE,
            .rasterizerDiscardEnable = VK_FALSE,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_BACK_BIT,
            .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .depthBiasEnable = VK_FALSE,
            .depthBiasConstantFactor = 0.0f,
            .depthBiasClamp = 0.0f,
            .depthBiasSlopeFactor = 0.0f,
            .lineWidth = 1.0f,
        };
        
        VkPipelineMultisampleStateCreateInfo MultisampleState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
            .sampleShadingEnable = VK_FALSE,
            .minSampleShading = 1.0f,
            .pSampleMask = nullptr,
            .alphaToCoverageEnable = VK_FALSE,
            .alphaToOneEnable = VK_FALSE,
        };
        
        VkPipelineDepthStencilStateCreateInfo DepthStencilState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .depthTestEnable = VK_TRUE,
            .depthWriteEnable = VK_TRUE,
            .depthCompareOp = VK_COMPARE_OP_LESS,
            .depthBoundsTestEnable = VK_FALSE,
            .stencilTestEnable = VK_FALSE,
            .front = 
            { 
                .failOp = VK_STENCIL_OP_KEEP,
                .passOp = VK_STENCIL_OP_KEEP,
                .depthFailOp = VK_STENCIL_OP_KEEP,
                .compareOp = VK_COMPARE_OP_ALWAYS,
                .compareMask = 0,
                .writeMask = 0,
                .reference = 0,
            },
            .back = 
            {
                .failOp = VK_STENCIL_OP_KEEP,
                .passOp = VK_STENCIL_OP_KEEP,
                .depthFailOp = VK_STENCIL_OP_KEEP,
                .compareOp = VK_COMPARE_OP_ALWAYS,
                .compareMask = 0,
                .writeMask = 0,
                .reference = 0,
            },
            .minDepthBounds = 0.0f,
            .maxDepthBounds = 1.0f,
        };
        
        VkPipelineColorBlendAttachmentState Attachment = 
        {
            .blendEnable = VK_FALSE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
            .alphaBlendOp = VK_BLEND_OP_ADD,
            .colorWriteMask = 
                VK_COLOR_COMPONENT_R_BIT|
                VK_COLOR_COMPONENT_G_BIT|
                VK_COLOR_COMPONENT_B_BIT|
                VK_COLOR_COMPONENT_A_BIT,
        };
        
        VkPipelineColorBlendStateCreateInfo ColorBlendState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .logicOpEnable = VK_FALSE,
            .logicOp = VK_LOGIC_OP_CLEAR,
            .attachmentCount = 1,
            .pAttachments = &Attachment,
            .blendConstants = { 1.0f, 1.0f, 1.0f, 1.0f },
        };
        
        VkDynamicState DynamicStates[] = 
        {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };
        constexpr u32 DynamicStateCount = CountOf(DynamicStates);
        
        VkPipelineDynamicStateCreateInfo DynamicState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .dynamicStateCount = DynamicStateCount,
            .pDynamicStates = DynamicStates,
        };
        
        VkFormat Formats[] = 
        {
            Renderer->SurfaceFormat.format,
        };
        constexpr u32 FormatCount = CountOf(Formats);
        
        VkPipelineRenderingCreateInfoKHR DynamicRendering = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
            .pNext = nullptr,
            .viewMask = 0,
            .colorAttachmentCount = FormatCount,
            .pColorAttachmentFormats = Formats,
            .depthAttachmentFormat = VK_FORMAT_D32_SFLOAT,
            .stencilAttachmentFormat = VK_FORMAT_UNDEFINED,
        };
        
        VkGraphicsPipelineCreateInfo Info = 
        {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = &DynamicRendering,
            .flags = 0,
            .stageCount = ShaderStageCount,
            .pStages = ShaderStages,
            .pVertexInputState = &VertexInputState,
            .pInputAssemblyState = &InputAssemblyState,
            .pTessellationState = nullptr,
            .pViewportState = &ViewportState,
            .pRasterizationState = &RasterizationState,
            .pMultisampleState = &MultisampleState,
            .pDepthStencilState = &DepthStencilState,
            .pColorBlendState = &ColorBlendState,
            .pDynamicState = &DynamicState,
            .layout = Renderer->PipelineLayout,
            .renderPass = nullptr,
            .subpass = 0,
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = 0,
        };
        
        Result = vkCreateGraphicsPipelines(Renderer->Device, VK_NULL_HANDLE, 1, &Info, nullptr, &Renderer->Pipeline);
        if (Result != VK_SUCCESS)
        {
            return false;
        }

        vkDestroyShaderModule(Renderer->Device, VSModule, nullptr);
        vkDestroyShaderModule(Renderer->Device, FSModule, nullptr);
    }

    // ImGui pipeline
    {
        // Sampler
        {
            VkSamplerCreateInfo SamplerInfo = 
            {
                .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .magFilter = VK_FILTER_LINEAR,
                .minFilter = VK_FILTER_LINEAR,
                .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                .mipLodBias = 0.0f,
                .anisotropyEnable = VK_FALSE,
                .maxAnisotropy = 1.0f,
                .compareEnable = VK_FALSE,
                .compareOp = VK_COMPARE_OP_ALWAYS,
                .minLod = 0.0f,
                .maxLod = 0.0f,
                .borderColor = {},
                .unnormalizedCoordinates = VK_FALSE,
            };

            VkSampler Sampler;
            if (vkCreateSampler(Renderer->Device, &SamplerInfo, nullptr, &Sampler) == VK_SUCCESS)
            {
                Renderer->ImGuiSampler = Sampler;
            }
            else
            {
                return false;
            }
        }

        // Create descriptor set
        {


            VkDescriptorSetLayoutBinding Binding = 
            {
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                .pImmutableSamplers = &Renderer->ImGuiSampler,
            };

            VkDescriptorSetLayoutCreateInfo SetLayoutInfo = 
            {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .bindingCount = 1,
                .pBindings = &Binding,
            };
            VkDescriptorSetLayout SetLayout;
            if (vkCreateDescriptorSetLayout(Renderer->Device, &SetLayoutInfo, nullptr, &SetLayout) == VK_SUCCESS)
            {
                Renderer->ImGuiSetLayout = SetLayout;
            }
            else
            {
                return false;
            }

            VkDescriptorPoolSize PoolSize = 
            {
                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
            };

            VkDescriptorPoolCreateInfo PoolInfo = 
            {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .maxSets = 1,
                .poolSizeCount = 1,
                .pPoolSizes = &PoolSize,
            };
            VkDescriptorPool Pool;
            if (vkCreateDescriptorPool(Renderer->Device, &PoolInfo, nullptr, &Pool) == VK_SUCCESS)
            {
                VkDescriptorSetAllocateInfo AllocInfo = 
                {
                    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                    .pNext = nullptr,
                    .descriptorPool = Pool,
                    .descriptorSetCount = 1,
                    .pSetLayouts = &Renderer->ImGuiSetLayout,
                };
                VkDescriptorSet DescriptorSet;
                if (vkAllocateDescriptorSets(Renderer->Device, &AllocInfo, &DescriptorSet) == VK_SUCCESS)
                {
                    Renderer->ImGuiDescriptorPool = Pool;
                    Renderer->ImGuiDescriptorSet = DescriptorSet;
                }
                else
                {
                    return false;
                }
            }
            else
            {
                return false;
            }
        }

        // Create pipeline layout
        {
            VkPushConstantRange PushConstants = 
            {
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                .offset = 0,
                .size = sizeof(mat4),
            };
            VkPipelineLayoutCreateInfo Info = 
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .setLayoutCount = 1,
                .pSetLayouts = &Renderer->ImGuiSetLayout,
                .pushConstantRangeCount = 1,
                .pPushConstantRanges = &PushConstants,
            };
            
            Result = vkCreatePipelineLayout(Renderer->Device, &Info, nullptr, &Renderer->ImGuiPipelineLayout);
            if (Result != VK_SUCCESS)
            {
                return false;
            }
        }
        
        //  Create shaders
        VkShaderModule VSModule, FSModule;
        {
            CBuffer VSBin = LoadEntireFile("shader/imguishader.vs");
            CBuffer FSBin = LoadEntireFile("shader/imguishader.fs");
            
            assert((VSBin.Size > 0) && (FSBin.Size > 0));
            
            VkShaderModuleCreateInfo VSInfo = 
            {
                .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .codeSize = VSBin.Size,
                .pCode = (u32*)VSBin.Data,
            };
            VkShaderModuleCreateInfo FSInfo =
            {
                .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .codeSize = FSBin.Size,
                .pCode = (u32*)FSBin.Data,
            };
            
            Result = vkCreateShaderModule(Renderer->Device, &VSInfo, nullptr, &VSModule);
            if (Result != VK_SUCCESS)
            {
                return false;
            }
            Result = vkCreateShaderModule(Renderer->Device, &FSInfo, nullptr, &FSModule);
            if (Result != VK_SUCCESS)
            {
                return false;
            }
        }
        
        VkPipelineShaderStageCreateInfo ShaderStages[] = 
        {
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .module = VSModule,
                .pName = "main",
                .pSpecializationInfo = nullptr,
            },
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .module = FSModule,
                .pName = "main",
                .pSpecializationInfo = nullptr,
            },
        };
        constexpr u32 ShaderStageCount = CountOf(ShaderStages);
        
        VkVertexInputBindingDescription VertexBinding = 
        {
            .binding = 0,
            .stride = sizeof(ImDrawVert),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        };
        
        VkVertexInputAttributeDescription VertexAttribs[] = 
        {
            // Pos
            {
                .location = ATTRIB_POS,
                .binding = 0,
                .format = VK_FORMAT_R32G32_SFLOAT,
                .offset = offsetof(ImDrawVert, pos),
            },
            // UVW
            {
                .location = ATTRIB_TEXCOORD,
                .binding = 0,
                .format = VK_FORMAT_R32G32_SFLOAT,
                .offset = offsetof(ImDrawVert, uv),
            },
            // Color
            {
                .location = ATTRIB_COLOR,
                .binding = 0,
                .format = VK_FORMAT_R8G8B8A8_UNORM,
                .offset = offsetof(ImDrawVert, col),
            },
        };
        constexpr u32 VertexAttribCount = CountOf(VertexAttribs);
        
        VkPipelineVertexInputStateCreateInfo VertexInputState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .vertexBindingDescriptionCount = 1,
            .pVertexBindingDescriptions = &VertexBinding,
            .vertexAttributeDescriptionCount = VertexAttribCount,
            .pVertexAttributeDescriptions = VertexAttribs,
        };
        
        VkPipelineInputAssemblyStateCreateInfo InputAssemblyState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            .primitiveRestartEnable = VK_FALSE,
        };
        
        VkViewport Viewport = {};
        VkRect2D Scissor = {};
        
        VkPipelineViewportStateCreateInfo ViewportState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .viewportCount = 1,
            .pViewports = &Viewport,
            .scissorCount = 1,
            .pScissors = &Scissor,
        };
        
        VkPipelineRasterizationStateCreateInfo RasterizationState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .depthClampEnable = VK_FALSE,
            .rasterizerDiscardEnable = VK_FALSE,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_NONE,
            .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .depthBiasEnable = VK_FALSE,
            .depthBiasConstantFactor = 0.0f,
            .depthBiasClamp = 0.0f,
            .depthBiasSlopeFactor = 0.0f,
            .lineWidth = 1.0f,
        };
        
        VkPipelineMultisampleStateCreateInfo MultisampleState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
            .sampleShadingEnable = VK_FALSE,
            .minSampleShading = 1.0f,
            .pSampleMask = nullptr,
            .alphaToCoverageEnable = VK_FALSE,
            .alphaToOneEnable = VK_FALSE,
        };
        
        VkPipelineDepthStencilStateCreateInfo DepthStencilState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .depthTestEnable = VK_FALSE,
            .depthWriteEnable = VK_FALSE,
            .depthCompareOp = VK_COMPARE_OP_LESS,
            .depthBoundsTestEnable = VK_FALSE,
            .stencilTestEnable = VK_FALSE,
            .front = 
            { 
                .failOp = VK_STENCIL_OP_KEEP,
                .passOp = VK_STENCIL_OP_KEEP,
                .depthFailOp = VK_STENCIL_OP_KEEP,
                .compareOp = VK_COMPARE_OP_ALWAYS,
                .compareMask = 0,
                .writeMask = 0,
                .reference = 0,
            },
            .back = 
            {
                .failOp = VK_STENCIL_OP_KEEP,
                .passOp = VK_STENCIL_OP_KEEP,
                .depthFailOp = VK_STENCIL_OP_KEEP,
                .compareOp = VK_COMPARE_OP_ALWAYS,
                .compareMask = 0,
                .writeMask = 0,
                .reference = 0,
            },
            .minDepthBounds = 0.0f,
            .maxDepthBounds = 1.0f,
        };
        
        VkPipelineColorBlendAttachmentState Attachment = 
        {
            .blendEnable = VK_TRUE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .alphaBlendOp = VK_BLEND_OP_ADD,
            .colorWriteMask = 
                VK_COLOR_COMPONENT_R_BIT|
                VK_COLOR_COMPONENT_G_BIT|
                VK_COLOR_COMPONENT_B_BIT|
                VK_COLOR_COMPONENT_A_BIT,
        };
        
        VkPipelineColorBlendStateCreateInfo ColorBlendState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .logicOpEnable = VK_FALSE,
            .logicOp = VK_LOGIC_OP_CLEAR,
            .attachmentCount = 1,
            .pAttachments = &Attachment,
            .blendConstants = { 1.0f, 1.0f, 1.0f, 1.0f },
        };
        
        VkDynamicState DynamicStates[] = 
        {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };
        constexpr u32 DynamicStateCount = CountOf(DynamicStates);
        
        VkPipelineDynamicStateCreateInfo DynamicState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .dynamicStateCount = DynamicStateCount,
            .pDynamicStates = DynamicStates,
        };
        
        VkFormat Formats[] = 
        {
            Renderer->SurfaceFormat.format,
        };
        constexpr u32 FormatCount = CountOf(Formats);
        
        VkPipelineRenderingCreateInfoKHR DynamicRendering = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
            .pNext = nullptr,
            .viewMask = 0,
            .colorAttachmentCount = FormatCount,
            .pColorAttachmentFormats = Formats,
            .depthAttachmentFormat = VK_FORMAT_D32_SFLOAT,
            .stencilAttachmentFormat = VK_FORMAT_UNDEFINED,
        };
        
        VkGraphicsPipelineCreateInfo Info = 
        {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = &DynamicRendering,
            .flags = 0,
            .stageCount = ShaderStageCount,
            .pStages = ShaderStages,
            .pVertexInputState = &VertexInputState,
            .pInputAssemblyState = &InputAssemblyState,
            .pTessellationState = nullptr,
            .pViewportState = &ViewportState,
            .pRasterizationState = &RasterizationState,
            .pMultisampleState = &MultisampleState,
            .pDepthStencilState = &DepthStencilState,
            .pColorBlendState = &ColorBlendState,
            .pDynamicState = &DynamicState,
            .layout = Renderer->ImGuiPipelineLayout,
            .renderPass = nullptr,
            .subpass = 0,
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = 0,
        };
        
        Result = vkCreateGraphicsPipelines(Renderer->Device, VK_NULL_HANDLE, 1, &Info, nullptr, &Renderer->ImGuiPipeline);
        if (Result != VK_SUCCESS)
        {
            return false;
        }

        vkDestroyShaderModule(Renderer->Device, VSModule, nullptr);
        vkDestroyShaderModule(Renderer->Device, FSModule, nullptr);
    }

    // ImPipeline
    {
        // Create pipeline layout
        {
            VkPushConstantRange PushConstants = 
            {
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                .offset = 0,
                .size = sizeof(mat4),
            };
            VkPipelineLayoutCreateInfo Info = 
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .setLayoutCount = 0,
                .pSetLayouts = nullptr,
                .pushConstantRangeCount = 1,
                .pPushConstantRanges = &PushConstants,
            };
            
            Result = vkCreatePipelineLayout(Renderer->Device, &Info, nullptr, &Renderer->ImPipelineLayout);
            if (Result != VK_SUCCESS)
            {
                return false;
            }
        }
        
        //  Create shaders
        VkShaderModule VSModule, FSModule;
        {
            CBuffer VSBin = LoadEntireFile("shader/imshader.vs");
            CBuffer FSBin = LoadEntireFile("shader/imshader.fs");
            
            assert((VSBin.Size > 0) && (FSBin.Size > 0));
            
            VkShaderModuleCreateInfo VSInfo = 
            {
                .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .codeSize = VSBin.Size,
                .pCode = (u32*)VSBin.Data,
            };
            VkShaderModuleCreateInfo FSInfo =
            {
                .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .codeSize = FSBin.Size,
                .pCode = (u32*)FSBin.Data,
            };
            
            Result = vkCreateShaderModule(Renderer->Device, &VSInfo, nullptr, &VSModule);
            if (Result != VK_SUCCESS)
            {
                return false;
            }
            Result = vkCreateShaderModule(Renderer->Device, &FSInfo, nullptr, &FSModule);
            if (Result != VK_SUCCESS)
            {
                return false;
            }
        }
        
        VkPipelineShaderStageCreateInfo ShaderStages[] = 
        {
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .module = VSModule,
                .pName = "main",
                .pSpecializationInfo = nullptr,
            },
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .module = FSModule,
                .pName = "main",
                .pSpecializationInfo = nullptr,
            },
        };
        constexpr u32 ShaderStageCount = CountOf(ShaderStages);
        
        VkVertexInputBindingDescription VertexBinding = 
        {
            .binding = 0,
            .stride = sizeof(vertex),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        };
        
        VkVertexInputAttributeDescription VertexAttribs[] = 
        {
            // Pos
            {
                .location = ATTRIB_POS,
                .binding = 0,
                .format = VK_FORMAT_R32G32B32_SFLOAT,
                .offset = offsetof(vertex, P),
            },
#if 0
            // UVW
            {
                .location = ATTRIB_TEXCOORD,
                .binding = 0,
                .format = VK_FORMAT_R32G32B32_SFLOAT,
                .offset = offsetof(vertex, UVW),
            },
#endif
            // Color
            {
                .location = ATTRIB_COLOR,
                .binding = 0,
                .format = VK_FORMAT_R8G8B8A8_UNORM,
                .offset = offsetof(vertex, Color),
            },
        };
        constexpr u32 VertexAttribCount = CountOf(VertexAttribs);
        
        VkPipelineVertexInputStateCreateInfo VertexInputState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .vertexBindingDescriptionCount = 1,
            .pVertexBindingDescriptions = &VertexBinding,
            .vertexAttributeDescriptionCount = VertexAttribCount,
            .pVertexAttributeDescriptions = VertexAttribs,
        };
        
        VkPipelineInputAssemblyStateCreateInfo InputAssemblyState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            .primitiveRestartEnable = VK_FALSE,
        };
        
        VkViewport Viewport = {};
        VkRect2D Scissor = {};
        
        VkPipelineViewportStateCreateInfo ViewportState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .viewportCount = 1,
            .pViewports = &Viewport,
            .scissorCount = 1,
            .pScissors = &Scissor,
        };
        
        VkPipelineRasterizationStateCreateInfo RasterizationState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .depthClampEnable = VK_FALSE,
            .rasterizerDiscardEnable = VK_FALSE,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_NONE,
            .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .depthBiasEnable = VK_TRUE,
            .depthBiasConstantFactor = 0.0f,
            .depthBiasClamp = 0.0f,
            .depthBiasSlopeFactor = 0.0f,
            .lineWidth = 1.0f,
        };
        
        VkPipelineMultisampleStateCreateInfo MultisampleState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
            .sampleShadingEnable = VK_FALSE,
            .minSampleShading = 1.0f,
            .pSampleMask = nullptr,
            .alphaToCoverageEnable = VK_FALSE,
            .alphaToOneEnable = VK_FALSE,
        };
        
        VkPipelineDepthStencilStateCreateInfo DepthStencilState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .depthTestEnable = VK_TRUE,
            .depthWriteEnable = VK_FALSE,
            .depthCompareOp = VK_COMPARE_OP_LESS,
            .depthBoundsTestEnable = VK_FALSE,
            .stencilTestEnable = VK_FALSE,
            .front = 
            { 
                .failOp = VK_STENCIL_OP_KEEP,
                .passOp = VK_STENCIL_OP_KEEP,
                .depthFailOp = VK_STENCIL_OP_KEEP,
                .compareOp = VK_COMPARE_OP_ALWAYS,
                .compareMask = 0,
                .writeMask = 0,
                .reference = 0,
            },
            .back = 
            {
                .failOp = VK_STENCIL_OP_KEEP,
                .passOp = VK_STENCIL_OP_KEEP,
                .depthFailOp = VK_STENCIL_OP_KEEP,
                .compareOp = VK_COMPARE_OP_ALWAYS,
                .compareMask = 0,
                .writeMask = 0,
                .reference = 0,
            },
            .minDepthBounds = 0.0f,
            .maxDepthBounds = 1.0f,
        };
        
        VkPipelineColorBlendAttachmentState Attachment = 
        {
            .blendEnable = VK_FALSE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
            .alphaBlendOp = VK_BLEND_OP_ADD,
            .colorWriteMask = 
                VK_COLOR_COMPONENT_R_BIT|
                VK_COLOR_COMPONENT_G_BIT|
                VK_COLOR_COMPONENT_B_BIT|
                VK_COLOR_COMPONENT_A_BIT,
        };
        
        VkPipelineColorBlendStateCreateInfo ColorBlendState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .logicOpEnable = VK_FALSE,
            .logicOp = VK_LOGIC_OP_CLEAR,
            .attachmentCount = 1,
            .pAttachments = &Attachment,
            .blendConstants = { 1.0f, 1.0f, 1.0f, 1.0f },
        };
        
        VkDynamicState DynamicStates[] = 
        {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
            VK_DYNAMIC_STATE_DEPTH_BIAS,
            VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY_EXT,
        };
        constexpr u32 DynamicStateCount = CountOf(DynamicStates);
        
        VkPipelineDynamicStateCreateInfo DynamicState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .dynamicStateCount = DynamicStateCount,
            .pDynamicStates = DynamicStates,
        };
        
        VkFormat Formats[] = 
        {
            Renderer->SurfaceFormat.format,
        };
        constexpr u32 FormatCount = CountOf(Formats);
        
        VkPipelineRenderingCreateInfoKHR DynamicRendering = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
            .pNext = nullptr,
            .viewMask = 0,
            .colorAttachmentCount = FormatCount,
            .pColorAttachmentFormats = Formats,
            .depthAttachmentFormat = VK_FORMAT_D32_SFLOAT,
            .stencilAttachmentFormat = VK_FORMAT_UNDEFINED,
        };
        
        VkGraphicsPipelineCreateInfo Info = 
        {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = &DynamicRendering,
            .flags = 0,
            .stageCount = ShaderStageCount,
            .pStages = ShaderStages,
            .pVertexInputState = &VertexInputState,
            .pInputAssemblyState = &InputAssemblyState,
            .pTessellationState = nullptr,
            .pViewportState = &ViewportState,
            .pRasterizationState = &RasterizationState,
            .pMultisampleState = &MultisampleState,
            .pDepthStencilState = &DepthStencilState,
            .pColorBlendState = &ColorBlendState,
            .pDynamicState = &DynamicState,
            .layout = Renderer->ImPipelineLayout,
            .renderPass = nullptr,
            .subpass = 0,
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = 0,
        };
        
        Result = vkCreateGraphicsPipelines(Renderer->Device, VK_NULL_HANDLE, 1, &Info, nullptr, &Renderer->ImPipeline);
        if (Result != VK_SUCCESS)
        {
            return false;
        }

        vkDestroyShaderModule(Renderer->Device, VSModule, nullptr);
        vkDestroyShaderModule(Renderer->Device, FSModule, nullptr);
    }

    return true;
}

bool Renderer_CreateImGuiTexture(vulkan_renderer* Renderer, u32 Width, u32 Height, const u8* Data)
{
    assert(Renderer);
    bool Result = false;

    VkImageCreateInfo ImageInfo = 
    {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8_UNORM,
        .extent = { Width, Height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VkImage Image;
    if (vkCreateImage(Renderer->Device, &ImageInfo, nullptr, &Image) == VK_SUCCESS)
    {
        VkMemoryRequirements MemoryRequirements = {};
        vkGetImageMemoryRequirements(Renderer->Device, Image, &MemoryRequirements);

        u32 MemoryTypes = Renderer->DeviceLocalMemoryTypes & MemoryRequirements.memoryTypeBits;
        u32 MemoryType = 0;
        if (BitScanForward(&MemoryType, MemoryTypes) != 0)
        {
            VkMemoryAllocateInfo AllocInfo = 
            {
                .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                .pNext = nullptr,
                .allocationSize = MemoryRequirements.size,
                .memoryTypeIndex = MemoryType,
            };
            VkDeviceMemory Memory;
            if (vkAllocateMemory(Renderer->Device, &AllocInfo, nullptr, &Memory) == VK_SUCCESS)
            {
                if (vkBindImageMemory(Renderer->Device, Image, Memory, 0) == VK_SUCCESS)
                {
                    VkImageViewCreateInfo ViewInfo = 
                    {
                        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                        .pNext = nullptr,
                        .flags = 0,
                        .image = Image,
                        .viewType = VK_IMAGE_VIEW_TYPE_2D,
                        .format = VK_FORMAT_R8_UNORM,
                        .components = 
                        {
                            .r = VK_COMPONENT_SWIZZLE_ONE,
                            .g = VK_COMPONENT_SWIZZLE_ONE,
                            .b = VK_COMPONENT_SWIZZLE_ONE,
                            .a = VK_COMPONENT_SWIZZLE_R,
                        },
                        .subresourceRange = 
                        {
                            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                            .baseMipLevel = 0,
                            .levelCount = 1,
                            .baseArrayLayer = 0,
                            .layerCount = 1,
                        },
                    };

                    if (StagingHeap_CopyImage(
                            &Renderer->StagingHeap, 
                            Renderer->TransferQueue, 
                            Renderer->TransferCmdBuffer, 
                            Image,
                            Width, Height, 1, 1, VK_FORMAT_R8_UNORM,
                            Data))
                        {
                            VkImageView ImageView;
                            if (vkCreateImageView(Renderer->Device, &ViewInfo, nullptr, &ImageView) == VK_SUCCESS)
                            {
                                VkDescriptorImageInfo DescriptorImageInfo = 
                                {
                                    .sampler = VK_NULL_HANDLE,
                                    .imageView = ImageView,
                                    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                };
                                VkWriteDescriptorSet DescriptorWrite = 
                                {
                                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                    .pNext = nullptr,
                                    .dstSet = Renderer->ImGuiDescriptorSet,
                                    .dstBinding = 0,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                    .pImageInfo = &DescriptorImageInfo,
                                    .pBufferInfo = nullptr,
                                    .pTexelBufferView = nullptr,
                                };
                                vkUpdateDescriptorSets(Renderer->Device, 1, &DescriptorWrite, 0, nullptr);

                                Renderer->ImGuiTex = Image;
                                Renderer->ImGuiTexMemory = Memory;
                                Renderer->ImGuiTexView = ImageView;
                                Result = true;
                            }
                            else
                            {
                                vkFreeMemory(Renderer->Device, Memory, nullptr);
                                vkDestroyImage(Renderer->Device, Image, nullptr);
                            }
                        }
                        else
                        {
                            vkFreeMemory(Renderer->Device, Memory, nullptr);
                            vkDestroyImage(Renderer->Device, Image, nullptr);
                        }
                }
                else
                {
                    vkFreeMemory(Renderer->Device, Memory, nullptr);
                    vkDestroyImage(Renderer->Device, Image, nullptr);
                }
            }
            else
            {
                vkDestroyImage(Renderer->Device, Image, nullptr);
            }
        }
        else
        {
            vkDestroyImage(Renderer->Device, Image, nullptr);
        }
    }

    return Result;
}

renderer_frame_params* Renderer_NewFrame(vulkan_renderer* Renderer)
{
    TIMED_FUNCTION();

    u64 FrameIndex = Renderer->NextFrameIndex;
    Renderer->NextFrameIndex = (Renderer->NextFrameIndex + 1) % 2;

    renderer_frame_params* Frame = Renderer->FrameParams + FrameIndex;
    Frame->FrameIndex = FrameIndex;
    Frame->Renderer = Renderer;
    Frame->SwapchainImageIndex = INVALID_INDEX_U32;
    Frame->VertexStack.At = 0;

    {
        TIMED_BLOCK("WaitForPreviousFrames");
        
        vkWaitForFences(Renderer->Device, 1, &Frame->RenderFinishedFence, VK_TRUE, UINT64_MAX);
        vkResetFences(Renderer->Device, 1, &Frame->RenderFinishedFence);
    }

    VkResult Result = vkAcquireNextImageKHR(
        Renderer->Device, Renderer->Swapchain, 
        0, 
        Frame->ImageAcquiredSemaphore,
        nullptr, 
        &Frame->SwapchainImageIndex);
    if (Result != VK_SUCCESS)
    {
        assert(!"Can't acquire swapchain image");
        return nullptr;
    }

    Frame->SwapchainImage = Renderer->SwapchainImages[Frame->SwapchainImageIndex];
    Frame->SwapchainImageView = Renderer->SwapchainImageViews[Frame->SwapchainImageIndex];

    vkResetCommandPool(Renderer->Device, Frame->CmdPool, VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);
    return Frame;
}

void Renderer_SubmitFrame(vulkan_renderer* Renderer, renderer_frame_params* Frame)
{
    TIMED_FUNCTION();

    VkPipelineStageFlags WaitStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    VkSubmitInfo SubmitInfo = 
    {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &Frame->ImageAcquiredSemaphore,
        .pWaitDstStageMask = &WaitStageMask,
        .commandBufferCount = 1,
        .pCommandBuffers = &Frame->CmdBuffer,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &Frame->RenderFinishedSemaphore,
    };
    vkQueueSubmit(Renderer->GraphicsQueue, 1, &SubmitInfo, Frame->RenderFinishedFence);

    VkPresentInfoKHR PresentInfo = 
    {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext = nullptr,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &Frame->RenderFinishedSemaphore,
        .swapchainCount = 1,
        .pSwapchains = &Frame->Renderer->Swapchain,
        .pImageIndices = &Frame->SwapchainImageIndex,
        .pResults = nullptr,
    };

    vkQueuePresentKHR(Renderer->GraphicsQueue, &PresentInfo);
}

void Renderer_BeginRendering(renderer_frame_params* Frame)
{
    TIMED_FUNCTION();

    VkCommandBufferBeginInfo BeginInfo = 
    {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr,
    };
    vkBeginCommandBuffer(Frame->CmdBuffer, &BeginInfo);

    VkImageMemoryBarrier BeginBarriers[] = 
    {
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = Frame->SwapchainImage,
            .subresourceRange = 
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        },
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = Frame->DepthBuffer,
            .subresourceRange = 
            {
                .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        },
    };
    constexpr u32 BeginBarrierCount = CountOf(BeginBarriers);
    
    vkCmdPipelineBarrier(Frame->CmdBuffer,
        VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
        0,
        0, nullptr,
        0, nullptr,
        BeginBarrierCount, BeginBarriers);
    
    VkRenderingAttachmentInfoKHR ColorAttachment = 
    {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
        .pNext = nullptr,
        .imageView = Frame->SwapchainImageView,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .resolveMode = VK_RESOLVE_MODE_NONE,
        .resolveImageView = VK_NULL_HANDLE,
        .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue =  { .color = { 0.0f, 1.0f, 1.0f, 0.0f, }, },
    };
    VkRenderingAttachmentInfoKHR DepthAttachment = 
    {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
        .pNext = nullptr,
        .imageView = Frame->DepthBufferView,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .resolveMode = VK_RESOLVE_MODE_NONE,
        .resolveImageView = VK_NULL_HANDLE,
        .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue =  { .depthStencil = { 1.0f, 0 }, },
    };
    VkRenderingAttachmentInfoKHR StencilAttachment = 
    {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
        .pNext = nullptr,
        .imageView = VK_NULL_HANDLE,
        .imageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .resolveMode = VK_RESOLVE_MODE_NONE,
        .resolveImageView = VK_NULL_HANDLE,
        .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .clearValue = { .depthStencil = { 1.0f, 0 }, },
    };
    
    VkRenderingInfoKHR RenderingInfo = 
    {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR,
        .pNext = nullptr,
        .flags = 0,
        .renderArea = { { 0, 0 }, Frame->Renderer->SwapchainSize },
        .layerCount = 1,
        .viewMask = 0,
        .colorAttachmentCount = 1,
        .pColorAttachments = &ColorAttachment,
        .pDepthAttachment = &DepthAttachment,
        .pStencilAttachment = &StencilAttachment,
    };

    vkCmdBeginRenderingKHR(Frame->CmdBuffer, &RenderingInfo);

    VkViewport Viewport = 
    {
        .x = 0.0f,
        .y = 0.0f,
        .width = (f32)Frame->Renderer->SwapchainSize.width,
        .height = (f32)Frame->Renderer->SwapchainSize.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    
    VkRect2D Scissor = 
    {
        .offset = { 0, 0 },
        .extent = Frame->Renderer->SwapchainSize,
    };
    
    vkCmdSetViewport(Frame->CmdBuffer, 0, 1, &Viewport);
    vkCmdSetScissor(Frame->CmdBuffer, 0, 1, &Scissor);

}
void Renderer_EndRendering(renderer_frame_params* Frame)
{
    TIMED_FUNCTION();

    vkCmdEndRenderingKHR(Frame->CmdBuffer);

    VkImageMemoryBarrier EndBarriers[] = 
    {
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = 0,
            .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = Frame->SwapchainImage,
            .subresourceRange = 
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        },
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = Frame->DepthBuffer,
            .subresourceRange = 
            {
                .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        },
    };
    constexpr u32 EndBarrierCount = CountOf(EndBarriers);

    vkCmdPipelineBarrier(Frame->CmdBuffer, 
        VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
        0,
        0, nullptr,
        0, nullptr,
        EndBarrierCount, EndBarriers);

    vkEndCommandBuffer(Frame->CmdBuffer);
}

void Renderer_RenderChunks(renderer_frame_params* Frame, u32 Count, const chunk* Chunks)
{
    TIMED_FUNCTION();

    vkCmdBindPipeline(Frame->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Frame->Renderer->Pipeline);
    vkCmdBindDescriptorSets(Frame->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Frame->Renderer->PipelineLayout, 
        0, 1, &Frame->Renderer->DescriptorSet, 0, nullptr);

    VkDeviceSize Offset = 0;
    vkCmdBindVertexBuffers(Frame->CmdBuffer, 0, 1, &Frame->Renderer->VB.Buffer, &Offset);

    mat4 VP = Frame->ProjectionTransform * Frame->ViewTransform;

    for (u32 i = 0; i < Count; i++)
    {
        const chunk* Chunk = Chunks + i;
        if (!(Chunk->Flags & CHUNK_STATE_UPLOADED_BIT))
        {
            continue;
        }
        if (Chunk->AllocationIndex == INVALID_INDEX_U32)
        {
            continue;
        }

        vulkan_vertex_buffer_allocation Allocation = Frame->Renderer->VB.Allocations[Chunk->AllocationIndex];
        if (Allocation.BlockIndex == INVALID_INDEX_U32)
        {
            continue;
        }
        mat4 WorldTransform = Mat4(
            1.0f, 0.0f, 0.0f, (f32)Chunk->P.x * CHUNK_DIM_X,
            0.0f, 1.0f, 0.0f, (f32)Chunk->P.y * CHUNK_DIM_Y,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f);
        mat4 Transform = VP * WorldTransform;
        
        vkCmdPushConstants(
            Frame->CmdBuffer,
            Frame->Renderer->PipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT, 0,
            sizeof(mat4), &Transform);
        
        vulkan_vertex_buffer_block Block = Frame->Renderer->VB.Blocks[Allocation.BlockIndex];
        vkCmdDraw(Frame->CmdBuffer, Block.VertexCount, 1, Block.VertexOffset, 0);
    }
}

u64 Frame_PushToStack(renderer_frame_params* Frame, u64 Alignment, const void* Data, u64 Size)
{
    assert(Frame);
    u64 Result = INVALID_INDEX_U64;

    u64 Offset = Frame->VertexStack.At;
    if (Alignment != 0)
    {
        Offset = AlignTo(Offset, Alignment);
    }

    u64 End = Offset + Size;
    if (End <= Frame->VertexStack.Size)
    {
        u8* Dest = (u8*)Frame->VertexStack.Mapping + Offset;
        memcpy(Dest, Data, Size);
        Frame->VertexStack.At = End;

        Result = Offset;
    }

    return Result;
}

void Renderer_BeginImmediate(renderer_frame_params* Frame)
{
    TIMED_FUNCTION();

    // NOTE(boti): Supposedly we don't need to set the viewport/scissor here when all our pipelines have that as dynamic
    // TODO(boti): ^Verify
    vkCmdBindPipeline(Frame->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Frame->Renderer->ImPipeline);
}

void Renderer_ImmediateBoxOutline(renderer_frame_params* Frame, aabb Box, u32 Color)
{
    TIMED_FUNCTION();

    static constexpr u32 VertexCount = 2*8 + 8;
    vertex VertexData[VertexCount] = 
    {
        // Bottom
        { { Box.Min.x, Box.Min.y, Box.Min.z }, { }, Color },
        { { Box.Max.x, Box.Min.y, Box.Min.z }, { }, Color },
        { { Box.Max.x, Box.Min.y, Box.Min.z }, { }, Color },
        { { Box.Max.x, Box.Max.y, Box.Min.z }, { }, Color },
        { { Box.Max.x, Box.Max.y, Box.Min.z }, { }, Color },
        { { Box.Min.x, Box.Max.y, Box.Min.z }, { }, Color },
        { { Box.Min.x, Box.Max.y, Box.Min.z }, { }, Color },
        { { Box.Min.x, Box.Min.y, Box.Min.z }, { }, Color },

        // Top
        { { Box.Min.x, Box.Min.y, Box.Max.z }, { }, Color },
        { { Box.Max.x, Box.Min.y, Box.Max.z }, { }, Color },
        { { Box.Max.x, Box.Min.y, Box.Max.z }, { }, Color },
        { { Box.Max.x, Box.Max.y, Box.Max.z }, { }, Color },
        { { Box.Max.x, Box.Max.y, Box.Max.z }, { }, Color },
        { { Box.Min.x, Box.Max.y, Box.Max.z }, { }, Color },
        { { Box.Min.x, Box.Max.y, Box.Max.z }, { }, Color },
        { { Box.Min.x, Box.Min.y, Box.Max.z }, { }, Color },

        // Sides
        { { Box.Min.x, Box.Min.y, Box.Min.z }, { }, Color },
        { { Box.Min.x, Box.Min.y, Box.Max.z }, { }, Color },
        { { Box.Max.x, Box.Min.y, Box.Min.z }, { }, Color },
        { { Box.Max.x, Box.Min.y, Box.Max.z }, { }, Color },
        { { Box.Max.x, Box.Max.y, Box.Min.z }, { }, Color },
        { { Box.Max.x, Box.Max.y, Box.Max.z }, { }, Color },
        { { Box.Min.x, Box.Max.y, Box.Min.z }, { }, Color },
        { { Box.Min.x, Box.Max.y, Box.Max.z }, { }, Color },
    };

    u64 Offset = Frame_PushToStack(Frame, 16, VertexData, sizeof(VertexData));
    if (Offset != INVALID_INDEX_U64)
    {
#if 0
        assert((Offset % sizeof(vertex)) == 0);
        u32 VertexOffset = SafeU64ToU32(Offset / sizeof(vertex));
#else
        vkCmdBindVertexBuffers(Frame->CmdBuffer, 0, 1, &Frame->VertexStack.Buffer, &Offset);
#endif

        mat4 Transform = Frame->ProjectionTransform * Frame->ViewTransform;
        vkCmdSetPrimitiveTopologyEXT(Frame->CmdBuffer, VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
        vkCmdPushConstants(Frame->CmdBuffer, Frame->Renderer->ImPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mat4), &Transform);
        vkCmdSetDepthBias(Frame->CmdBuffer, -0.1f, 0.0f, 0.0f);
        vkCmdDraw(Frame->CmdBuffer, VertexCount, 1, 0, 0);
    }
    else
    {
        assert(!"Renderer_ImmediateBoxOutline failed");
    }
}

void Renderer_ImmediateRect2D(renderer_frame_params* Frame, vec2 p0, vec2 p1, u32 Color)
{
    TIMED_FUNCTION();

    vertex VertexData[] = 
    {
        { { p1.x, p0.y, 0.0f }, {}, Color }, 
        { { p1.x, p1.y, 0.0f }, {}, Color },
        { { p0.x, p0.y, 0.0f }, {}, Color },
        { { p0.x, p1.y, 0.0f }, {}, Color },
    };
    u32 VertexCount = CountOf(VertexData);

    u64 Offset = Frame_PushToStack(Frame, 16, VertexData, sizeof(VertexData));
    if (Offset != INVALID_INDEX_U64)
    {
        vkCmdBindVertexBuffers(Frame->CmdBuffer, 0, 1, &Frame->VertexStack.Buffer, &Offset);
        mat4 Transform = Mat4(
            2.0f / Frame->Renderer->SwapchainSize.width, 0.0f, 0.0f, -1.0f,
            0.0f, 2.0f / Frame->Renderer->SwapchainSize.height, 0.0f, -1.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f);
        vkCmdPushConstants(Frame->CmdBuffer, Frame->Renderer->ImPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mat4), &Transform);
        vkCmdSetPrimitiveTopologyEXT(Frame->CmdBuffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
        vkCmdSetDepthBias(Frame->CmdBuffer, 0.0f, 0.0f, 0.0f);
        vkCmdDraw(Frame->CmdBuffer, VertexCount, 1, 0, 0);
    }
    else
    {
        assert(!"Renderer_ImmediateRect2D failed");
    }
}

void Renderer_ImmediateRectOutline2D(renderer_frame_params* Frame, outline_type Type, f32 OutlineSize, vec2 p0, vec2 p1, u32 Color)
{
    TIMED_FUNCTION();

    vec2 P0, P1;
    if (Type == outline_type::Outer)
    {
        P0 = p0;
        P1 = p1;
    }
    else if (Type == outline_type::Inner)
    {
        P0 = p0 + vec2{ OutlineSize, OutlineSize };
        P1 = p1 + vec2{ -OutlineSize, -OutlineSize };
    }
    else
    {
        P0 = {};
        P1 = {};
        assert(!"Invalid code path");
    }

    // Top
    Renderer_ImmediateRect2D(Frame,
        vec2{ P0.x - OutlineSize, P0.y - OutlineSize },
        vec2{ P1.x + OutlineSize, P0.y               },
        Color);
    // Left
    Renderer_ImmediateRect2D(Frame, 
        vec2{ P0.x - OutlineSize, P0.y - OutlineSize },
        vec2{ P0.x              , P1.y + OutlineSize },
        Color);
    // Right
    Renderer_ImmediateRect2D(Frame, 
        vec2{ P1.x              , P0.y - OutlineSize },
        vec2{ P1.x + OutlineSize, P1.y + OutlineSize },
        Color);
    // Bottom
    Renderer_ImmediateRect2D(Frame, 
        vec2{ P0.x - OutlineSize, P1.y               },
        vec2{ P1.x + OutlineSize, P1.y + OutlineSize },
        Color);
}

void Renderer_RenderImGui(renderer_frame_params* Frame)
{
    TIMED_FUNCTION();

    ImGui::Render();

    ImDrawData* DrawData = ImGui::GetDrawData();
    if (DrawData && (DrawData->TotalVtxCount > 0) && (DrawData->TotalIdxCount > 0))
    {
        u64 VertexDataOffset = AlignTo(Frame->VertexStack.At, sizeof(ImDrawVert));
        u64 VertexDataSize = ((u64)DrawData->TotalVtxCount * sizeof(ImDrawVert));
        u64 VertexDataEnd = VertexDataOffset + VertexDataSize;

        u64 IndexDataOffset = AlignTo(VertexDataEnd, sizeof(ImDrawIdx));
        u64 IndexDataSize = (u64)DrawData->TotalIdxCount * sizeof(ImDrawIdx);
        u64 IndexDataEnd = IndexDataOffset + IndexDataSize;

        u64 ImGuiDataSize = VertexDataSize + IndexDataSize;
        u64 ImGuiDataEnd = IndexDataEnd;

        if (ImGuiDataEnd <= Frame->VertexStack.Size)
        {
            Frame->VertexStack.At = ImGuiDataEnd;

            vkCmdBindPipeline(Frame->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Frame->Renderer->ImGuiPipeline);
            vkCmdBindDescriptorSets(
                Frame->CmdBuffer, 
                VK_PIPELINE_BIND_POINT_GRAPHICS, 
                Frame->Renderer->ImGuiPipelineLayout,
                0, 1, &Frame->Renderer->ImGuiDescriptorSet, 0, nullptr);

            mat4 Transform = Mat4(
                2.0f / Frame->Renderer->SwapchainSize.width, 0.0f, 0.0f, -1.0f,
                0.0f, 2.0f / Frame->Renderer->SwapchainSize.height, 0.0f, -1.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f);
            vkCmdPushConstants(
                Frame->CmdBuffer, 
                Frame->Renderer->ImGuiPipelineLayout, 
                VK_SHADER_STAGE_VERTEX_BIT,
                0, sizeof(mat4), &Transform);

            vkCmdBindVertexBuffers(Frame->CmdBuffer, 0, 1, &Frame->VertexStack.Buffer, &VertexDataOffset);
            VkIndexType IndexType = (sizeof(ImDrawIdx) == 2) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
            vkCmdBindIndexBuffer(Frame->CmdBuffer, Frame->VertexStack.Buffer, IndexDataOffset, IndexType);

            ImDrawVert* VertexDataAt = (ImDrawVert*)((u8*)Frame->VertexStack.Mapping + VertexDataOffset);
            ImDrawIdx* IndexDataAt = (ImDrawIdx*)((u8*)Frame->VertexStack.Mapping + IndexDataOffset);

            u32 VertexOffset = 0;
            u32 IndexOffset = 0;
            for (int CmdListIndex = 0; CmdListIndex < DrawData->CmdListsCount; CmdListIndex++)
            {
                ImDrawList* CmdList = DrawData->CmdLists[CmdListIndex];

                memcpy(VertexDataAt, CmdList->VtxBuffer.Data, (u64)CmdList->VtxBuffer.Size * sizeof(ImDrawVert));
                VertexDataAt += CmdList->VtxBuffer.Size;

                memcpy(IndexDataAt, CmdList->IdxBuffer.Data, (u64)CmdList->IdxBuffer.Size * sizeof(ImDrawIdx));
                IndexDataAt += CmdList->IdxBuffer.Size;

                for (int CmdBufferIndex = 0; CmdBufferIndex < CmdList->CmdBuffer.Size; CmdBufferIndex++)
                {
                    ImDrawCmd* Command = &CmdList->CmdBuffer[CmdBufferIndex];
                    assert((u64)Command->TextureId == Frame->Renderer->ImGuiTextureID);

                    VkRect2D Scissor = 
                    {
                        .offset = { (s32)Command->ClipRect.x, (s32)Command->ClipRect.y },
                        .extent = 
                        { 
                            .width = (u32)Command->ClipRect.z,
                            .height = (u32)Command->ClipRect.w,
                        },
                    };
                    vkCmdSetScissor(Frame->CmdBuffer, 0, 1, &Scissor);
                    vkCmdDrawIndexed(
                        Frame->CmdBuffer, Command->ElemCount, 1, 
                        Command->IdxOffset + IndexOffset, 
                        Command->VtxOffset + VertexOffset, 
                        0);
                }

                VertexOffset += CmdList->VtxBuffer.Size;
                IndexOffset += CmdList->IdxBuffer.Size;
            }

            // Reset the scissor in case someone wants to render after us
            VkRect2D Scissor = 
            {
                .offset = { 0, 0 },
                .extent = Frame->Renderer->SwapchainSize,
            };
            vkCmdSetScissor(Frame->CmdBuffer, 0, 1, &Scissor);
        }
        else
        {
            DebugPrint("WARNING: not enough memory for ImGui\n");
        }
    }
}