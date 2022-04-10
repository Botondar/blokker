#include "StagingHeap.hpp"

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
        case VK_FORMAT_R8G8B8A8_UNORM:
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