#pragma once

#include <vulkan/vulkan.h>
#include <Common.hpp>

struct vulkan_physical_device_desc
{
    static constexpr u32 MaxQueueFamilyCount = 8;

    u32 QueueFamilyCount;
    VkQueueFamilyProperties QueueFamilyProps[MaxQueueFamilyCount];
    VkPhysicalDeviceProperties Props;
    VkPhysicalDeviceMemoryProperties MemoryProps;
};

struct render_device
{
    VkInstance Instance;
    VkDebugUtilsMessengerEXT DebugMessenger;

    VkPhysicalDevice PhysicalDevice;
    vulkan_physical_device_desc DeviceDesc;

    // Memory type bitmasks
    struct 
    {
        u32 DeviceLocal;
        u32 HostVisible;
        u32 HostVisibleCoherent;
        u32 DeviceLocalAndHostVisible;
    } MemoryTypes;

    u64 NonCoherentAtomSize;
    VkDevice Device;

    // Queue and queue families
    u32 GraphicsFamilyIndex;
    u32 TransferFamilyIndex;
    VkQueue GraphicsQueue;
    VkQueue TransferQueue;
};

bool CreateRenderDevice(render_device* RenderDevice);