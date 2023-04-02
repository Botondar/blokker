#include "RenderDevice.hpp"

#include <Platform.hpp>

static PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT_ = nullptr;
#define vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT_

static VkBool32 VKAPI_PTR VulkanDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT Severity,
    VkDebugUtilsMessageTypeFlagsEXT Type,
    const VkDebugUtilsMessengerCallbackDataEXT* CallbackData,
    void* UserData)
{
    Platform.DebugPrint("%s\n", CallbackData->pMessage);
    return VK_FALSE;
}

struct vulkan_layer_extensions
{
    u32 Count;
    VkExtensionProperties Extensions;
};

static bool RenderDevice_CreateInstance(
    render_device* Device,
    u32 RequiredVersionMajor, u32 RequiredVersionMinor,
    const char* Layers[], u32 LayerCount,
    const char* Extensions[], u32 ExtensionCount)
{
    bool Result = false;

    assert(Device);

    u32 InstanceVersion = 0;
    if (vkEnumerateInstanceVersion(&InstanceVersion) == VK_SUCCESS)
    {
        const u32 VersionMajor = VK_API_VERSION_MAJOR(InstanceVersion);
        const u32 VersionMinor = VK_API_VERSION_MINOR(InstanceVersion);

        bool IsInvalidVersion = 
            (VersionMajor < RequiredVersionMajor) || 
            ((VersionMajor == RequiredVersionMajor) && (VersionMinor < RequiredVersionMinor));
        if (!IsInvalidVersion)
        {
            // TODO(boti): enumerate and check instance extensions and layers

            VkApplicationInfo AppInfo = 
            {
                .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                .pNext = nullptr,
                .pApplicationName = "blokker",
                .applicationVersion = 1,
                .pEngineName = "blokker",
                .engineVersion = 1,
                .apiVersion = InstanceVersion,
            };

            VkInstanceCreateInfo InstanceInfo = 
            {
                .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .pApplicationInfo = &AppInfo,
                .enabledLayerCount = LayerCount,
                .ppEnabledLayerNames = Layers,
                .enabledExtensionCount = ExtensionCount,
                .ppEnabledExtensionNames = Extensions,
            };

            if (vkCreateInstance(&InstanceInfo, nullptr, &Device->Instance) == VK_SUCCESS)
            {
                Result = true;
            }
        }
        else
        {
            // TODO: logging
        }
    }

    return Result;
}

static bool RenderDevice_EnableDebugging(render_device* Device)
{
    bool Result = false;

    assert(Device);

    vkCreateDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(Device->Instance, "vkCreateDebugUtilsMessengerEXT");
    if (vkCreateDebugUtilsMessengerEXT)
    {
        VkDebugUtilsMessengerCreateInfoEXT Info = 
        {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .pNext = nullptr,
            .flags = 0,
            .messageSeverity = 
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType = 
                VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = &VulkanDebugCallback,
            .pUserData = nullptr,
        };

        if (vkCreateDebugUtilsMessengerEXT(Device->Instance, &Info, nullptr, &Device->DebugMessenger) == VK_SUCCESS)
        {
            Result = true;
        }
    }

    return Result;
}

static u32 ChooseDevice(const vulkan_physical_device_desc* Descs, u32 Count)
{
    u32 Result = INVALID_INDEX_U32;

    for (u32 i = 0; i < Count; i++)
    {
        // TODO(boti): better device selection logic
        if (Descs[i].Props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        {
            Result = i;
            break;
        }
    }

    return Result;
}

static bool RenderDevice_ChooseAndCreateDevice(
    render_device* Device,
    const char* Layers[], u32 LayerCount,
    const char* Extensions[], u32 ExtensionCount)
{
    bool Result = false;

    assert(Device);

    constexpr u32 MaxPhysicalDeviceCount = 8;
    u32 PhysicalDeviceCount = MaxPhysicalDeviceCount;
    VkPhysicalDevice PhysicalDevices[MaxPhysicalDeviceCount];
    vulkan_physical_device_desc PhysicalDeviceDescs[MaxPhysicalDeviceCount];

    vkEnumeratePhysicalDevices(Device->Instance, &PhysicalDeviceCount, PhysicalDevices);

    // TODO(boti): for now, we ignore all devices with index > 8
    if (PhysicalDeviceCount > MaxPhysicalDeviceCount)
    {
        Platform.DebugPrint("WARNING: Too many devices\n");
        assert(false);
        PhysicalDeviceCount = MaxPhysicalDeviceCount;
    }

    // Enumerate props
    for (u32 i = 0; i < PhysicalDeviceCount; i++)
    {
        vulkan_physical_device_desc* Desc = PhysicalDeviceDescs + i;
        vkGetPhysicalDeviceProperties(PhysicalDevices[i], &Desc->Props);
        vkGetPhysicalDeviceMemoryProperties(PhysicalDevices[i], &Desc->MemoryProps);

        Desc->QueueFamilyCount = Desc->MaxQueueFamilyCount;
        vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevices[i], &Desc->QueueFamilyCount, Desc->QueueFamilyProps);
        if (Desc->QueueFamilyCount > Desc->MaxQueueFamilyCount)
        {
            Platform.DebugPrint("WARNING: Too many queue families\n");
            assert(false);
            Desc->QueueFamilyCount = Desc->MaxQueueFamilyCount;
        }
    }

    u32 DeviceIndex = ChooseDevice(PhysicalDeviceDescs, PhysicalDeviceCount);
    if (DeviceIndex != INVALID_INDEX_U32)
    {
        Device->PhysicalDevice = PhysicalDevices[DeviceIndex];
        Device->DeviceDesc = PhysicalDeviceDescs[DeviceIndex];
        Device->NonCoherentAtomSize = Device->DeviceDesc.Props.limits.nonCoherentAtomSize;

        // Enumerate the memory types and create bitmasks for all the configurations we need
        for (u32 i = 0; i < Device->DeviceDesc.MemoryProps.memoryTypeCount; i++)
        {
            const VkMemoryType* MemoryType = Device->DeviceDesc.MemoryProps.memoryTypes + i;

            bool IsDeviceLocal = (MemoryType->propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;
            bool IsHostVisible = (MemoryType->propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
            bool IsHostCoherent = (MemoryType->propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
            if (IsDeviceLocal && !IsHostVisible)
            {
                Device->MemoryTypes.DeviceLocal |= (1 << i);
            }
            if (!IsDeviceLocal && IsHostVisible)
            {
                Device->MemoryTypes.HostVisible |= (1 << i);
            }
            if (!IsDeviceLocal && IsHostVisible && IsHostCoherent)
            {
                Device->MemoryTypes.HostVisibleCoherent |= (1 << i);
            }
            if (IsDeviceLocal && IsHostVisible)
            {
                Device->MemoryTypes.DeviceLocalAndHostVisible |= (1 << i);
            }
        }

        // TODO: at least a portion of this should be in the ChooseDevice function

        // Enumerate dynamic features
        
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

#if ENABLE_VK_SHADER_OBJECT
        VkPhysicalDeviceShaderObjectFeaturesEXT ShaderObjectFeatures = 
        {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT,
            .pNext = nullptr,
            .shaderObject = VK_FALSE,
        };
        DynamicRenderingFeatures.pNext = &ShaderObjectFeatures;
#endif

        vkGetPhysicalDeviceFeatures2(Device->PhysicalDevice, &DeviceFeatures);
        if ((DynamicRenderingFeatures.dynamicRendering) &&
            (DynamicStateFeatures.extendedDynamicState) &&
            (DynamicStateFeatures2.extendedDynamicState2) 
#if ENABLE_VK_SHADER_OBJECT
            && (ShaderObjectFeatures.shaderObject)
#endif
        )
        {
            // Enumerate queues and queue families
            Device->GraphicsFamilyIndex = INVALID_INDEX_U32;
            Device->TransferFamilyIndex = INVALID_INDEX_U32;
            for (u32 i = 0; i < Device->DeviceDesc.QueueFamilyCount; i++)
            {
                VkQueueFlags Flags = Device->DeviceDesc.QueueFamilyProps[i].queueFlags;
            
                if ((Flags & VK_QUEUE_TRANSFER_BIT) &&
                    (Flags & VK_QUEUE_COMPUTE_BIT) &&
                    (Flags & VK_QUEUE_SPARSE_BINDING_BIT))
                {
                    Device->GraphicsFamilyIndex = i;
                }

                if ((Flags & VK_QUEUE_TRANSFER_BIT) &&
                    !(Flags & VK_QUEUE_COMPUTE_BIT) &&
                    !(Flags & VK_QUEUE_COMPUTE_BIT))
                {
                    Device->TransferFamilyIndex = i;
                }
            }

            if ((Device->GraphicsFamilyIndex != INVALID_INDEX_U32) &&
                (Device->TransferFamilyIndex != INVALID_INDEX_U32))
            {
                const f32 MaxPriority = 1.0f;
                VkDeviceQueueCreateInfo QueueCreateInfos[] = 
                {
                    {
                        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                        .pNext = nullptr,
                        .flags = 0,
                        .queueFamilyIndex = Device->GraphicsFamilyIndex,
                        .queueCount = 1,
                        .pQueuePriorities = &MaxPriority,
                    },
                    {
                        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                        .pNext = nullptr,
                        .flags = 0,
                        .queueFamilyIndex = Device->TransferFamilyIndex,
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
                    .enabledLayerCount = LayerCount,
                    .ppEnabledLayerNames = Layers,
                    .enabledExtensionCount = ExtensionCount,
                    .ppEnabledExtensionNames = Extensions,
                    .pEnabledFeatures = nullptr,
                };

                if (vkCreateDevice(Device->PhysicalDevice, &DeviceCreateInfo, nullptr, &Device->Device) == VK_SUCCESS)
                {
                    vkGetDeviceQueue(Device->Device, Device->GraphicsFamilyIndex, 0, &Device->GraphicsQueue);
                    vkGetDeviceQueue(Device->Device, Device->TransferFamilyIndex, 0, &Device->TransferQueue);

#if ENABLE_VK_SHADER_OBJECT
                    #define Vulkan_LoadFunction(dev, name) name = (PFN_##name)vkGetDeviceProcAddr(dev, #name);
                    Vulkan_LoadFunction(Device->Device, vkCreateShadersEXT);
                    Vulkan_LoadFunction(Device->Device, vkDestroyShaderEXT);
                    Vulkan_LoadFunction(Device->Device, vkGetShaderBinaryDataEXT);
                    Vulkan_LoadFunction(Device->Device, vkCmdBindShadersEXT);
                    #undef Vulkan_LoadFunction

                    if (vkCreateShadersEXT && vkDestroyShaderEXT &&
                        vkGetShaderBinaryDataEXT && vkCmdBindShadersEXT)
                    {
                        Result = true;
                    }
#else
                    Result = true;
#endif
                }
            }
        }
    }
    else
    {
        // TODO: logging
    }

    return Result;
}

bool CreateRenderDevice(render_device* RenderDevice)
{
    bool Result = false;

    assert(RenderDevice);
    *RenderDevice = {};

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
#if ENABLE_VK_SHADER_OBJECT
        "VK_EXT_shader_object",
#endif
        //"VK_KHR_synchronization2",
    };

    constexpr u32 RequiredInstanceLayerCount     = CountOf(RequiredInstanceLayers);
    constexpr u32 RequiredInstanceExtensionCount = CountOf(RequiredInstanceExtensions);
    constexpr u32 RequiredDeviceLayerCount       = CountOf(RequiredDeviceLayers);
    constexpr u32 RequiredDeviceExtensionCount   = CountOf(RequiredDeviceExtensions);

    if (RenderDevice_CreateInstance(RenderDevice, 1, 3,
            RequiredInstanceLayers, RequiredInstanceLayerCount,
            RequiredInstanceExtensions, RequiredInstanceExtensionCount))
    {
        if (RenderDevice_EnableDebugging(RenderDevice))
        {
            if (RenderDevice_ChooseAndCreateDevice(RenderDevice, 
                    RequiredDeviceLayers, RequiredDeviceLayerCount,
                    RequiredDeviceExtensions, RequiredDeviceExtensionCount))
            {
                Result = true;
            }
        }
    }
    else
    {
        // TODO: logging
    }

    return Result;
}