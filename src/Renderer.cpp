#include "Renderer.hpp"

#include <cassert>
#include <Platform.hpp>

PFN_vkCmdBeginRenderingKHR vkCmdBeginRenderingKHR_ = nullptr;
PFN_vkCmdEndRenderingKHR   vkCmdEndRenderingKHR_   = nullptr;

static PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT_ = nullptr;
#define vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT_

static VkBool32 VKAPI_PTR VulkanDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT Severity,
    VkDebugUtilsMessageTypeFlagsEXT Type,
    const VkDebugUtilsMessengerCallbackDataEXT* CallbackData,
    void* UserData)
{
    DebugPrint("%s\n", CallbackData->pMessage);
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
    *Heap = {};
    
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

    if (Format == VK_FORMAT_R8G8B8A8_SRGB)
    {
#if 1
        u64 SrcSize = 0;
        {
            u64 CurrentWidth = (u64)Width;
            u64 CurrentHeight = (u64)Height;

            for (u32 i = 0; i < MipCount; i++)
            {
                SrcSize += CurrentWidth*CurrentHeight * 4;
            }

            SrcSize *= 4;
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

                    Offset += CurrentWidth*CurrentHeight * 4;
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

                            Block->VertexCount = VertexCount;
                            Block->Flags |= VBBLOCK_ALLOCATED_BIT;

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
        VB->MemoryUsage -= (u64)Block->VertexCount * sizeof(vertex);

        VB->FreeAllocationIndices[VB->FreeAllocationWrite++] = AllocationIndex;
        VB->FreeAllocationWrite %= vulkan_vertex_buffer::MaxAllocationCount;
        VB->FreeAllocationCount++;
    }
}

void VB_Defragment(vulkan_vertex_buffer* VB)
{
    assert(VB);

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
        "VK_LAYER_KHRONOS_synchronization2",
        "VK_LAYER_KHRONOS_validation",
    };
    static const char* RequiredInstanceExtensions[] = 
    {
        "VK_KHR_surface",
        "VK_KHR_win32_surface",
        "VK_KHR_get_physical_device_properties2",
        "VK_EXT_debug_report",
        "VK_EXT_debug_utils",
        "VK_EXT_validation_features",
    };
    static const char* RequiredDeviceLayers[] = 
    {
        "VK_LAYER_KHRONOS_synchronization2",
        "VK_LAYER_KHRONOS_validation",
    };
    static const char* RequiredDeviceExtensions[] = 
    {
        "VK_KHR_swapchain",
        "VK_KHR_synchronization2",
        "VK_KHR_dynamic_rendering",
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
            // TODO: device selection logic
            Renderer->PhysicalDevice = PhysicalDevices[0];
            Renderer->DeviceDesc = PhysicalDeviceDescs[0];

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
                else if (!IsDeviceLocal && IsHostVisible)
                {
                    Renderer->HostVisibleMemoryTypes |= (1 << i);
                }
                else if (!IsDeviceLocal && IsHostVisible && IsHostCoherent)
                {
                    Renderer->HostVisibleCoherentMemoryTypes |= (1 << i);
                }
                else if (IsDeviceLocal && IsHostVisible)
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

        VkPhysicalDeviceDynamicRenderingFeaturesKHR DynamicRenderingFeature = 
        {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR,
            .pNext = nullptr,
            .dynamicRendering = VK_FALSE,
        };

        VkPhysicalDeviceFeatures2 DeviceFeatures = 
        {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .pNext = &DynamicRenderingFeature,
        };

        vkGetPhysicalDeviceFeatures2(Renderer->PhysicalDevice, &DeviceFeatures);
        if (DynamicRenderingFeature.dynamicRendering == VK_FALSE)
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
            .pNext = &DynamicRenderingFeature,
            .flags = 0,
            .queueCreateInfoCount = QueueCreateInfoCount,
            .pQueueCreateInfos = QueueCreateInfos,
            .enabledLayerCount = RequiredDeviceLayerCount,
            .ppEnabledLayerNames = RequiredDeviceLayers,
            .enabledExtensionCount = RequiredDeviceExtensionCount,
            .ppEnabledExtensionNames = RequiredDeviceExtensions,
            .pEnabledFeatures = &DeviceFeatures.features,
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
    return true;
}