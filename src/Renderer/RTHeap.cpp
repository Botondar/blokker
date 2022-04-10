#include "RTHeap.hpp"

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
