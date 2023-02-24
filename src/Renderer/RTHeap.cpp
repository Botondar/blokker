#include "RTHeap.hpp"

bool RTHeap_Create(
    render_target_heap* Heap,
    u64 Size, u32 MemoryTypeBase,
    VkDevice Device)
{
    bool Result = false;
    
    VkFormat RequiredFormats[] = 
    {
        VK_FORMAT_D32_SFLOAT,
    };
    VkImageUsageFlags RequiredUsages[] = 
    {
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
    };
    static_assert(CountOf(RequiredFormats) == CountOf(RequiredUsages));

    u32 SuitableMemoryTypes = MemoryTypeBase;
    for (u32 i = 0; i < CountOf(RequiredFormats); i++)
    {
        VkImageCreateInfo Info = 
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = RequiredFormats[i],
            .extent = { 1280, 720, 1 },
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = RequiredUsages[i],
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };

        VkImage Image = VK_NULL_HANDLE;
        if (vkCreateImage(Device, &Info, nullptr, &Image) == VK_SUCCESS)
        {
            VkMemoryRequirements MemoryRequirements = {};
            vkGetImageMemoryRequirements(Device, Image, &MemoryRequirements);
            SuitableMemoryTypes &= MemoryRequirements.memoryTypeBits;
            vkDestroyImage(Device, Image, nullptr);
        }
        else
        {
            FatalError("Failed to create RT heap dummy image");
        }
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
    render_target_heap* Heap,
    VkImage Image)
{
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
