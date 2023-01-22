#include "Renderer.hpp"

#include <Platform.hpp>
#include <Profiler.hpp>

#include <imgui/imgui.h>

#include "RenderDevice.cpp"
#include "RTHeap.cpp"
#include "StagingHeap.cpp"
#include "VertexBuffer.cpp"

static bool Renderer_InitializeFrameParams(renderer* Renderer);

bool Renderer_ResizeRenderTargets(renderer* Renderer)
{
    assert(Renderer);
    assert(Renderer->RenderDevice.Device);

    bool Result = false;

    vkDeviceWaitIdle(Renderer->RenderDevice.Device);

    VkSurfaceCapabilitiesKHR SurfaceCaps = {};
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(Renderer->RenderDevice.PhysicalDevice, Renderer->Surface, &SurfaceCaps) == VK_SUCCESS)
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
        if (vkCreateSwapchainKHR(Renderer->RenderDevice.Device, &SwapchainInfo, nullptr, &Renderer->Swapchain) == VK_SUCCESS)
        {
            // NOTE: this is safe because we waited for device idle
            vkDestroySwapchainKHR(Renderer->RenderDevice.Device, OldSwapchain, nullptr);
            Renderer->SwapchainSize = CurrentExtent;

            vkGetSwapchainImagesKHR(
                Renderer->RenderDevice.Device, 
                Renderer->Swapchain, 
                &Renderer->SwapchainImageCount, 
                nullptr);
            assert(Renderer->SwapchainImageCount <= 16);

            vkGetSwapchainImagesKHR(
                Renderer->RenderDevice.Device, 
                Renderer->Swapchain, 
                &Renderer->SwapchainImageCount,
                Renderer->SwapchainImages);

            bool ImageViewCreationFailed = false;
            for (u32 i = 0; i < Renderer->SwapchainImageCount; i++)
            {
                if (Renderer->SwapchainImageViews[i]) 
                {
                    vkDestroyImageView(Renderer->RenderDevice.Device, Renderer->SwapchainImageViews[i], nullptr);
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

                if (vkCreateImageView(Renderer->RenderDevice.Device, &ImageViewInfo, nullptr, &Renderer->SwapchainImageViews[i]) == VK_SUCCESS)
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
                
                u32 RenderTargetSuitableMemoryTypes = Renderer->RenderDevice.MemoryTypes.DeviceLocal;
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
                    if (vkCreateImage(Renderer->RenderDevice.Device, &Info, nullptr, &Renderer->DepthBuffers[i]) == VK_SUCCESS)
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
                    vkGetImageMemoryRequirements(Renderer->RenderDevice.Device, Renderer->DepthBuffers[i], &RTMemoryRequirements[RTMemoryRequirementCount++]);
                }

                if (!ImageCreationFailed)
                {
                    if ((Renderer->RTHeap.Heap != VK_NULL_HANDLE) ||
                        (RTHeap_Create(&Renderer->RTHeap, 64*1024*1024,
                                       Renderer->RenderDevice.MemoryTypes.DeviceLocal,
                                       RTMemoryRequirementCount, RTMemoryRequirements,
                                       Renderer->RenderDevice.Device)))
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
                                vkDestroyImageView(Renderer->RenderDevice.Device, Renderer->DepthBufferViews[i], nullptr);
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
                            if (vkCreateImageView(Renderer->RenderDevice.Device, &ViewInfo, nullptr, &Renderer->DepthBufferViews[i]) == VK_SUCCESS)
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

bool Renderer_Initialize(renderer* Renderer, memory_arena* Arena)
{
    if (!CreateRenderDevice(&Renderer->RenderDevice))
    {
        return false;
    }

    Renderer->Surface = CreateVulkanSurface(Renderer->RenderDevice.Instance);
    if (Renderer->Surface == VK_NULL_HANDLE)
    {
        return false;
    }

    VkResult Result = VK_SUCCESS;
    {
        VkSurfaceCapabilitiesKHR SurfaceCaps;
        Result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(Renderer->RenderDevice.PhysicalDevice, Renderer->Surface, &SurfaceCaps);
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
            vkGetPhysicalDeviceSurfaceFormatsKHR(Renderer->RenderDevice.PhysicalDevice, Renderer->Surface, &SurfaceFormatCount, nullptr);
            if (SurfaceFormatCount > 64)
            {
                return false;
            }
            vkGetPhysicalDeviceSurfaceFormatsKHR(Renderer->RenderDevice.PhysicalDevice, Renderer->Surface, &SurfaceFormatCount, SurfaceFormats);
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
            .queueFamilyIndex = Renderer->RenderDevice.TransferFamilyIndex,
        };

        Result = vkCreateCommandPool(Renderer->RenderDevice.Device, &CmdPoolInfo, nullptr, &Renderer->TransferCmdPool);
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
        Result = vkAllocateCommandBuffers(Renderer->RenderDevice.Device, &AllocInfo, &Renderer->TransferCmdBuffer);
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
                .queueFamilyIndex = Renderer->RenderDevice.GraphicsFamilyIndex,
            };
        
            Result = vkCreateCommandPool(Renderer->RenderDevice.Device, &Info, nullptr, &Renderer->FrameParams[i].CmdPool);
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
            Result = vkAllocateCommandBuffers(Renderer->RenderDevice.Device, &Info, &Renderer->FrameParams[i].CmdBuffer);
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

            Result = vkCreateFence(Renderer->RenderDevice.Device, &Info, nullptr, &Renderer->FrameParams[i].RenderFinishedFence);
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

            Result = vkCreateSemaphore(Renderer->RenderDevice.Device, &Info, nullptr, &Renderer->FrameParams[i].ImageAcquiredSemaphore);
            if (Result != VK_SUCCESS)
            {
                return false;
            }
            Result = vkCreateSemaphore(Renderer->RenderDevice.Device, &Info, nullptr, &Renderer->FrameParams[i].RenderFinishedSemaphore);
        }
    }

    
    if (!StagingHeap_Create(&Renderer->StagingHeap, 256*1024*1024, Renderer))
    {
        return false;
    }

    // Vertex buffer
    if (!VB_Create(&Renderer->VB, Renderer->RenderDevice.MemoryTypes.DeviceLocal, 1024*1024*1024, Renderer->RenderDevice.Device, Arena))
    {
        return false;
    }

#if 1
    if (!Renderer_InitializeFrameParams(Renderer))
    {
        return false;
    }
#else
    // Create per frame vertex stack
    for (u32 i = 0; i < Renderer->SwapchainImageCount; i++)
    {
        renderer_frame_params* Frame = Renderer->FrameParams + i;

        VkBufferCreateInfo BufferInfo = 
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .size = Frame->VertexStack.VertexStackSize,
            .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
        };
        VkBuffer Buffer;
        if (vkCreateBuffer(Renderer->RenderDevice.Device, &BufferInfo, nullptr, &Buffer) == VK_SUCCESS)
        {
            VkMemoryRequirements MemoryRequirements = {};
            vkGetBufferMemoryRequirements(Renderer->RenderDevice.Device, Buffer, &MemoryRequirements);

            assert(MemoryRequirements.size == Frame->VertexStack.VertexStackSize);

            u32 MemoryTypes = MemoryRequirements.memoryTypeBits & Renderer->RenderDevice.MemoryTypes.HostVisibleCoherent;
            u32 MemoryType = 0;
            if (BitScanForward(&MemoryType, MemoryTypes) != 0)
            {
                VkMemoryAllocateInfo AllocInfo = 
                {
                    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                    .pNext = nullptr,
                    .allocationSize = Frame->VertexStack.VertexStackSize,
                    .memoryTypeIndex = MemoryType,
                };

                VkDeviceMemory Memory;
                if (vkAllocateMemory(Renderer->RenderDevice.Device, &AllocInfo, nullptr, &Memory) == VK_SUCCESS)
                {
                    if (vkBindBufferMemory(Renderer->RenderDevice.Device, Buffer, Memory, 0) == VK_SUCCESS)
                    {
                        // NOTE(boti): persistent mapping
                        void* Mapping = nullptr;
                        if (vkMapMemory(Renderer->RenderDevice.Device, Memory, 0, VK_WHOLE_SIZE, 0, &Mapping) == VK_SUCCESS)
                        {
                            Frame->VertexStack.Memory = Memory;
                            Frame->VertexStack.Buffer = Buffer;
                            Frame->VertexStack.Size = Frame->VertexStack.VertexStackSize;
                            Frame->VertexStack.At = 0;
                            Frame->VertexStack.Mapping = Mapping;
                        }
                        else
                        {
                            vkFreeMemory(Renderer->RenderDevice.Device, Memory, nullptr);
                            vkDestroyBuffer(Renderer->RenderDevice.Device, Buffer, nullptr);
                            return false;
                        }
                    }
                    else
                    {
                        vkFreeMemory(Renderer->RenderDevice.Device, Memory, nullptr);
                        vkDestroyBuffer(Renderer->RenderDevice.Device, Buffer, nullptr);
                        return false;
                    }
                }
                else
                {
                    vkDestroyBuffer(Renderer->RenderDevice.Device, Buffer, nullptr);
                    return false;
                }
            }
            else
            {
                vkDestroyBuffer(Renderer->RenderDevice.Device, Buffer, nullptr);
                return false;
            }
        }
        else
        {
            return false;
        }
    }

    // Create per frame (indirect) command buffer
    for (u32 i = 0; i < Renderer->SwapchainImageCount; i++)
    {
        renderer_frame_params* Frame = Renderer->FrameParams + i;

        VkBufferCreateInfo BufferInfo = 
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .size = Frame->DrawCommands.MemorySize,
            .usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
        };

        VkBuffer Buffer = VK_NULL_HANDLE;
        if (vkCreateBuffer(Renderer->RenderDevice.Device, &BufferInfo, nullptr, &Buffer) == VK_SUCCESS)
        {
            VkMemoryRequirements MemoryRequirements = {};
            vkGetBufferMemoryRequirements(Renderer->RenderDevice.Device, Buffer, &MemoryRequirements);

            assert(MemoryRequirements.size == Frame->DrawCommands.MemorySize);

            u32 MemoryTypes = MemoryRequirements.memoryTypeBits & Renderer->RenderDevice.MemoryTypes.HostVisibleCoherent;
            u32 MemoryType = 0;
            if (BitScanForward(&MemoryType, MemoryTypes) != 0)
            {
                VkMemoryAllocateInfo AllocInfo = 
                {
                    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                    .pNext = nullptr,
                    .allocationSize = Frame->DrawCommands.MemorySize,
                    .memoryTypeIndex = MemoryType,
                };

                VkDeviceMemory Memory = VK_NULL_HANDLE;
                if (vkAllocateMemory(Renderer->RenderDevice.Device, &AllocInfo, nullptr, &Memory) == VK_SUCCESS)
                {
                    if (vkBindBufferMemory(Renderer->RenderDevice.Device, Buffer, Memory, 0) == VK_SUCCESS)
                    {
                        void* Mapping = nullptr;
                        if (vkMapMemory(Renderer->RenderDevice.Device, Memory, 0, Frame->DrawCommands.MemorySize, 0, &Mapping) == VK_SUCCESS)
                        {
                            Frame->DrawCommands.Memory = Memory;
                            Frame->DrawCommands.Buffer = Buffer;

                            Frame->DrawCommands.DrawIndex = 0;
                            Frame->DrawCommands.Commands = (VkDrawIndirectCommand*)Mapping;
                        }
                        else
                        {
                            vkFreeMemory(Renderer->RenderDevice.Device, Memory, nullptr);
                            vkDestroyBuffer(Renderer->RenderDevice.Device, Buffer, nullptr);
                            return false;
                        }
                    }
                    else
                    {
                        vkFreeMemory(Renderer->RenderDevice.Device, Memory, nullptr);
                        vkDestroyBuffer(Renderer->RenderDevice.Device, Buffer, nullptr);
                        return false;
                    }
                }
                else
                {
                    vkDestroyBuffer(Renderer->RenderDevice.Device, Buffer, nullptr);
                    return false;
                }
            }
            else
            {
                vkDestroyBuffer(Renderer->RenderDevice.Device, Buffer, nullptr);
                return false;
            }
        }
        else
        {
            return false;
        }
    }

    // Create per frame instance data
    for (u32 i = 0; i < Renderer->SwapchainImageCount; i++)
    {
        renderer_frame_params* Frame = Renderer->FrameParams + i;

        
        VkBufferCreateInfo BufferInfo = 
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .size = Frame->ChunkPositions.MemorySize,
            .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
        };

        VkBuffer Buffer = VK_NULL_HANDLE;
        if (vkCreateBuffer(Renderer->RenderDevice.Device, &BufferInfo, nullptr, &Buffer) == VK_SUCCESS)
        {
            VkMemoryRequirements MemoryRequirements = {};
            vkGetBufferMemoryRequirements(Renderer->RenderDevice.Device, Buffer, &MemoryRequirements);

            assert(MemoryRequirements.size == Frame->ChunkPositions.MemorySize);

            u32 MemoryTypes = MemoryRequirements.memoryTypeBits & Renderer->RenderDevice.MemoryTypes.HostVisibleCoherent;
            u32 MemoryType = 0;
            if (BitScanForward(&MemoryType, MemoryTypes) != 0)
            {
                VkMemoryAllocateInfo AllocInfo = 
                {
                    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                    .pNext = nullptr,
                    .allocationSize = Frame->ChunkPositions.MemorySize,
                    .memoryTypeIndex = MemoryType,
                };

                VkDeviceMemory Memory = VK_NULL_HANDLE;
                if (vkAllocateMemory(Renderer->RenderDevice.Device, &AllocInfo, nullptr, &Memory) == VK_SUCCESS)
                {
                    if (vkBindBufferMemory(Renderer->RenderDevice.Device, Buffer, Memory, 0) == VK_SUCCESS)
                    {
                        void* Mapping = nullptr;
                        if (vkMapMemory(Renderer->RenderDevice.Device, Memory, 0, Frame->ChunkPositions.MemorySize, 0, &Mapping) == VK_SUCCESS)
                        {
                            Frame->ChunkPositions.Memory = Memory;
                            Frame->ChunkPositions.Buffer = Buffer;

                            Frame->ChunkPositions.ChunkAt = 0;
                            Frame->ChunkPositions.Mapping = (vec2*)Mapping;
                        }
                        else
                        {
                            vkFreeMemory(Renderer->RenderDevice.Device, Memory, nullptr);
                            vkDestroyBuffer(Renderer->RenderDevice.Device, Buffer, nullptr);
                            return false;
                        }
                    }
                    else
                    {
                        vkFreeMemory(Renderer->RenderDevice.Device, Memory, nullptr);
                        vkDestroyBuffer(Renderer->RenderDevice.Device, Buffer, nullptr);
                        return false;
                    }
                }
                else
                {
                    vkDestroyBuffer(Renderer->RenderDevice.Device, Buffer, nullptr);
                    return false;
                }
            }
            else
            {
                vkDestroyBuffer(Renderer->RenderDevice.Device, Buffer, nullptr);
                return false;
            }
        }
        else
        {
            return false;
        }
    }
#endif

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
            if (vkCreateSampler(Renderer->RenderDevice.Device, &SamplerInfo, nullptr, &Sampler) == VK_SUCCESS)
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
                {
                    .binding = 2,
                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .descriptorCount = 1,
                    .stageFlags = VK_SHADER_STAGE_ALL,
                    .pImmutableSamplers = nullptr,
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
            if (vkCreateDescriptorSetLayout(Renderer->RenderDevice.Device, &CreateInfo, nullptr, &Layout) == VK_SUCCESS)
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
                { .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1, },
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
            if (vkCreateDescriptorPool(Renderer->RenderDevice.Device, &PoolInfo, nullptr, &Pool) == VK_SUCCESS)
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
                if (vkAllocateDescriptorSets(Renderer->RenderDevice.Device, &AllocInfo, &DescriptorSet) == VK_SUCCESS)
                {
                    Renderer->DescriptorPool = Pool;
                    Renderer->DescriptorSet = DescriptorSet;
                }
                else
                {
                    vkDestroyDescriptorPool(Renderer->RenderDevice.Device, Pool, nullptr);
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
        u64 ArenaSave = Arena->Used;
        u32* PixelBuffer = PushArray<u32>(Arena, TexWidth*TexHeight*TexMaxArrayCount);
        u32* PixelBufferAt = PixelBuffer;
        struct image
        {
            u32 Width;
            u32 Height;
            VkFormat Format;
            u32* Pixels;
        };

        auto LoadBMP = [&PixelBufferAt, &Arena](const char* Path, image* Image) -> bool
        {
            bool Result = false;

            u64 ArenaSave = Arena->Used;
            buffer Buffer = LoadEntireFile(Path, Arena);
            if (Buffer.Data)
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
                Arena->Used = ArenaSave; // Free the allocation for the loaded file
            }

            return Result;
        };

        static const char* TexturePaths[] = 
        {
            "texture/ground_side.bmp",
            "texture/ground_top.bmp",
            "texture/ground_bottom.bmp",
            "texture/stone_side.bmp",
            "texture/coal_side.bmp",
            "texture/iron_side.bmp",
            "texture/trunk_side.bmp",
            "texture/trunk_top.bmp",
            "texture/leaves_side.bmp",
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
                .flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT,
                .imageType = VK_IMAGE_TYPE_2D,
                .format = VK_FORMAT_R8G8B8A8_UNORM,
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
            if (vkCreateImage(Renderer->RenderDevice.Device, &CreateInfo, nullptr, &Image) == VK_SUCCESS)
            {
                VkMemoryRequirements MemoryRequirements = {};
                vkGetImageMemoryRequirements(Renderer->RenderDevice.Device, Image, &MemoryRequirements);

                u32 MemoryType = 0;
                if (BitScanForward(&MemoryType, MemoryRequirements.memoryTypeBits & Renderer->RenderDevice.MemoryTypes.DeviceLocal))
                {
                    VkMemoryAllocateInfo AllocInfo = 
                    {
                        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                        .pNext = nullptr,
                        .allocationSize = MemoryRequirements.size,
                        .memoryTypeIndex = MemoryType,
                    };

                    VkDeviceMemory Memory;
                    if (vkAllocateMemory(Renderer->RenderDevice.Device, &AllocInfo, nullptr, &Memory) == VK_SUCCESS)
                    {
                        if (vkBindImageMemory(Renderer->RenderDevice.Device, Image, Memory, 0) == VK_SUCCESS)
                        {
                            if (StagingHeap_CopyImage(&Renderer->StagingHeap, 
                                Renderer->RenderDevice.TransferQueue,
                                Renderer->TransferCmdBuffer,
                                Image, 
                                TexWidth, TexHeight, TexMipCount, TextureCount,
                                VK_FORMAT_R8G8B8A8_UNORM,
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
                                if (vkCreateImageView(Renderer->RenderDevice.Device, &ViewInfo, nullptr, &ImageView) == VK_SUCCESS)
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
                                    vkUpdateDescriptorSets(Renderer->RenderDevice.Device, 1, &DescriptorWrite, 0, nullptr);

                                    Renderer->Tex = Image;
                                    Renderer->TexView = ImageView;
                                    Renderer->TexMemory = Memory;
                                }
                                else
                                {
                                    vkFreeMemory(Renderer->RenderDevice.Device, Memory, nullptr);
                                    vkDestroyImage(Renderer->RenderDevice.Device, Image, nullptr);
                                    return false;
                                }

                            }
                            else
                            {
                                vkFreeMemory(Renderer->RenderDevice.Device, Memory, nullptr);
                                vkDestroyImage(Renderer->RenderDevice.Device, Image, nullptr);
                                return false;
                            }
                        }
                        else
                        {
                            vkFreeMemory(Renderer->RenderDevice.Device, Memory, nullptr);
                            vkDestroyImage(Renderer->RenderDevice.Device, Image, nullptr);
                            return false;
                        }
                    }
                    else
                    {
                        vkDestroyImage(Renderer->RenderDevice.Device, Image, nullptr);
                        return false;
                    }
                }
                else
                {
                    vkDestroyImage(Renderer->RenderDevice.Device, Image, nullptr);
                    return false;
                }
            }
            else
            {
                return false;
            }
        }

        Arena->Used = ArenaSave;
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
            
            Result = vkCreatePipelineLayout(Renderer->RenderDevice.Device, &Info, nullptr, &Renderer->PipelineLayout);
            if (Result != VK_SUCCESS)
            {
                return false;
            }
        }
        
        //  Create shaders
        VkShaderModule VSModule, FSModule;
        {
            u64 ArenaSave = Arena->Used;
            buffer VSBin = LoadEntireFile("shader/shader.vs", Arena);
            buffer FSBin = LoadEntireFile("shader/shader.fs", Arena);
            
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
            
            Result = vkCreateShaderModule(Renderer->RenderDevice.Device, &VSInfo, nullptr, &VSModule);
            if (Result != VK_SUCCESS)
            {
                return false;
            }
            Result = vkCreateShaderModule(Renderer->RenderDevice.Device, &FSInfo, nullptr, &FSModule);
            if (Result != VK_SUCCESS)
            {
                return false;
            }
            Arena->Used = ArenaSave;
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
        
        VkVertexInputBindingDescription VertexBindings[]
        {
            // Vertices
            {
                .binding = 0,
                .stride = sizeof(terrain_vertex),
                .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
            },

            // (instanced) chunk positions
            {
                .binding = 1,
                .stride = sizeof(vec2),
                .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE,
            },
        };
        constexpr u32 VertexBindingCount = CountOf(VertexBindings);

        VkVertexInputAttributeDescription VertexAttribs[] = 
        {
            // Pos
            {
                .location = ATTRIB_POS,
                .binding = 0,
                .format = VK_FORMAT_R32_UINT,
                .offset = offsetof(terrain_vertex, P),
            },
            // UVW
            {
                .location = ATTRIB_TEXCOORD,
                .binding = 0,
                .format = VK_FORMAT_R32_UINT,
                .offset = offsetof(terrain_vertex, TexCoord),
            },
            // ChunkP
            {
                .location = ATTRIB_CHUNK_P,
                .binding = 1,
                .format = VK_FORMAT_R32G32_SFLOAT,
                .offset = 0,
            },

        };
        constexpr u32 VertexAttribCount = CountOf(VertexAttribs);
        
        VkPipelineVertexInputStateCreateInfo VertexInputState = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .vertexBindingDescriptionCount = VertexBindingCount,
            .pVertexBindingDescriptions = VertexBindings,
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
        
        VkPipelineRenderingCreateInfo DynamicRendering = 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
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
        
        Result = vkCreateGraphicsPipelines(Renderer->RenderDevice.Device, VK_NULL_HANDLE, 1, &Info, nullptr, &Renderer->Pipeline);
        if (Result != VK_SUCCESS)
        {
            return false;
        }

        vkDestroyShaderModule(Renderer->RenderDevice.Device, VSModule, nullptr);
        vkDestroyShaderModule(Renderer->RenderDevice.Device, FSModule, nullptr);
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
            if (vkCreateSampler(Renderer->RenderDevice.Device, &SamplerInfo, nullptr, &Sampler) == VK_SUCCESS)
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
            if (vkCreateDescriptorSetLayout(Renderer->RenderDevice.Device, &SetLayoutInfo, nullptr, &SetLayout) == VK_SUCCESS)
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
            if (vkCreateDescriptorPool(Renderer->RenderDevice.Device, &PoolInfo, nullptr, &Pool) == VK_SUCCESS)
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
                if (vkAllocateDescriptorSets(Renderer->RenderDevice.Device, &AllocInfo, &DescriptorSet) == VK_SUCCESS)
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
            
            Result = vkCreatePipelineLayout(Renderer->RenderDevice.Device, &Info, nullptr, &Renderer->ImGuiPipelineLayout);
            if (Result != VK_SUCCESS)
            {
                return false;
            }
        }
        
        //  Create shaders
        VkShaderModule VSModule, FSModule;
        {
            u64 ArenaSave = Arena->Used;
            buffer VSBin = LoadEntireFile("shader/imguishader.vs", Arena);
            buffer FSBin = LoadEntireFile("shader/imguishader.fs", Arena);
            
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
            
            Result = vkCreateShaderModule(Renderer->RenderDevice.Device, &VSInfo, nullptr, &VSModule);
            if (Result != VK_SUCCESS)
            {
                return false;
            }
            Result = vkCreateShaderModule(Renderer->RenderDevice.Device, &FSInfo, nullptr, &FSModule);
            if (Result != VK_SUCCESS)
            {
                return false;
            }
            Arena->Used = ArenaSave;
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
        
        Result = vkCreateGraphicsPipelines(Renderer->RenderDevice.Device, VK_NULL_HANDLE, 1, &Info, nullptr, &Renderer->ImGuiPipeline);
        if (Result != VK_SUCCESS)
        {
            return false;
        }

        vkDestroyShaderModule(Renderer->RenderDevice.Device, VSModule, nullptr);
        vkDestroyShaderModule(Renderer->RenderDevice.Device, FSModule, nullptr);
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
            
            Result = vkCreatePipelineLayout(Renderer->RenderDevice.Device, &Info, nullptr, &Renderer->ImPipelineLayout);
            if (Result != VK_SUCCESS)
            {
                return false;
            }
        }
        
        //  Create shaders
        VkShaderModule VSModule, FSModule;
        {
            u64 ArenaSave = Arena->Used;
            buffer VSBin = LoadEntireFile("shader/imshader.vs", Arena);
            buffer FSBin = LoadEntireFile("shader/imshader.fs", Arena);
            
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
            
            Result = vkCreateShaderModule(Renderer->RenderDevice.Device, &VSInfo, nullptr, &VSModule);
            if (Result != VK_SUCCESS)
            {
                return false;
            }
            Result = vkCreateShaderModule(Renderer->RenderDevice.Device, &FSInfo, nullptr, &FSModule);
            if (Result != VK_SUCCESS)
            {
                return false;
            }
            Arena->Used = ArenaSave;
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
        
        Result = vkCreateGraphicsPipelines(Renderer->RenderDevice.Device, VK_NULL_HANDLE, 1, &Info, nullptr, &Renderer->ImPipeline);
        if (Result != VK_SUCCESS)
        {
            return false;
        }

        vkDestroyShaderModule(Renderer->RenderDevice.Device, VSModule, nullptr);
        vkDestroyShaderModule(Renderer->RenderDevice.Device, FSModule, nullptr);
    }

    return true;
}

bool Renderer_CreateImGuiTexture(renderer* Renderer, u32 Width, u32 Height, const u8* Data)
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
    if (vkCreateImage(Renderer->RenderDevice.Device, &ImageInfo, nullptr, &Image) == VK_SUCCESS)
    {
        VkMemoryRequirements MemoryRequirements = {};
        vkGetImageMemoryRequirements(Renderer->RenderDevice.Device, Image, &MemoryRequirements);

        u32 MemoryTypes = Renderer->RenderDevice.MemoryTypes.DeviceLocal & MemoryRequirements.memoryTypeBits;
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
            if (vkAllocateMemory(Renderer->RenderDevice.Device, &AllocInfo, nullptr, &Memory) == VK_SUCCESS)
            {
                if (vkBindImageMemory(Renderer->RenderDevice.Device, Image, Memory, 0) == VK_SUCCESS)
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
                            Renderer->RenderDevice.TransferQueue, 
                            Renderer->TransferCmdBuffer, 
                            Image,
                            Width, Height, 1, 1, VK_FORMAT_R8_UNORM,
                            Data))
                        {
                            VkImageView ImageView;
                            if (vkCreateImageView(Renderer->RenderDevice.Device, &ViewInfo, nullptr, &ImageView) == VK_SUCCESS)
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
                                vkUpdateDescriptorSets(Renderer->RenderDevice.Device, 1, &DescriptorWrite, 0, nullptr);

                                Renderer->ImGuiTex = Image;
                                Renderer->ImGuiTexMemory = Memory;
                                Renderer->ImGuiTexView = ImageView;
                                Result = true;
                            }
                            else
                            {
                                vkFreeMemory(Renderer->RenderDevice.Device, Memory, nullptr);
                                vkDestroyImage(Renderer->RenderDevice.Device, Image, nullptr);
                            }
                        }
                        else
                        {
                            vkFreeMemory(Renderer->RenderDevice.Device, Memory, nullptr);
                            vkDestroyImage(Renderer->RenderDevice.Device, Image, nullptr);
                        }
                }
                else
                {
                    vkFreeMemory(Renderer->RenderDevice.Device, Memory, nullptr);
                    vkDestroyImage(Renderer->RenderDevice.Device, Image, nullptr);
                }
            }
            else
            {
                vkDestroyImage(Renderer->RenderDevice.Device, Image, nullptr);
            }
        }
        else
        {
            vkDestroyImage(Renderer->RenderDevice.Device, Image, nullptr);
        }
    }

    return Result;
}

static bool Renderer_InitializeFrameParams(renderer* Renderer)
{
    // Create per frame uniform params
    {
        // Memory size of the entire allocation (which contains uniform buffers for each frame in flight)
        constexpr u64 MemorySize = 64*1024;

        u64 Alignment = 0;
        u32 MemoryTypes = Renderer->RenderDevice.MemoryTypes.DeviceLocalAndHostVisible;
        constexpr u64 UniformParamSize = 8 * 1024;
        for (u32 i = 0; i < Renderer->SwapchainImageCount; i++)
        {
            VkBufferCreateInfo BufferInfo = 
            {
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .size = UniformParamSize,
                .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                .queueFamilyIndexCount = 0,
                .pQueueFamilyIndices = nullptr,
            };

            VkBuffer Buffer = VK_NULL_HANDLE;
            if (vkCreateBuffer(Renderer->RenderDevice.Device, &BufferInfo, nullptr, &Buffer) == VK_SUCCESS)
            {
                VkMemoryRequirements MemoryRequirements = {};
                vkGetBufferMemoryRequirements(Renderer->RenderDevice.Device, Buffer, &MemoryRequirements);

                MemoryTypes &= MemoryRequirements.memoryTypeBits;
                Alignment = Max(Alignment, MemoryRequirements.alignment);

                Renderer->FrameParams[i].FrameUniformBuffer.BufferSize = UniformParamSize;
                Renderer->FrameParams[i].FrameUniformBuffer.Buffer = Buffer;
            }
            else
            {
                return false;
            }
        }

        u32 MemoryTypeIndex = 0;
        if (BitScanForward(&MemoryTypeIndex, MemoryTypes))
        {
            VkMemoryAllocateInfo AllocInfo = 
            {
                .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                .pNext = nullptr,
                .allocationSize = MemorySize,
                .memoryTypeIndex = MemoryTypeIndex,
            };

            VkDeviceMemory Memory = VK_NULL_HANDLE;
            if (vkAllocateMemory(Renderer->RenderDevice.Device, &AllocInfo, nullptr, &Memory) == VK_SUCCESS)
            {
                void* MappingBase = nullptr;
                if (vkMapMemory(Renderer->RenderDevice.Device, Memory, 0, VK_WHOLE_SIZE, 0, &MappingBase) == VK_SUCCESS)
                {
                    for (u32 i = 0; i < Renderer->SwapchainImageCount; i++)
                    {
                        u64 Offset = i * UniformParamSize;
                        if (vkBindBufferMemory(Renderer->RenderDevice.Device, Renderer->FrameParams[i].FrameUniformBuffer.Buffer, Memory, Offset) == VK_SUCCESS)
                        {
                            Renderer->FrameParams[i].FrameUniformBuffer.Memory = Memory;

                            Renderer->FrameParams[i].FrameUniformBuffer.Mapping = PointerByteOffset(MappingBase, Offset);

                        }
                        else
                        {
                            return false;
                        }
                    }
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
        else
        {
            return false;
        }
    }

    for (u32 i = 0; i < Renderer->SwapchainImageCount; i++)
    {
        renderer_frame_params* Frame = Renderer->FrameParams + i;

        // Create per frame vertex stack
        {
            VkBufferCreateInfo BufferInfo = 
            {
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .size = Frame->VertexStack.VertexStackSize,
                .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                .queueFamilyIndexCount = 0,
                .pQueueFamilyIndices = nullptr,
            };
            VkBuffer Buffer;
            if (vkCreateBuffer(Renderer->RenderDevice.Device, &BufferInfo, nullptr, &Buffer) == VK_SUCCESS)
            {
                VkMemoryRequirements MemoryRequirements = {};
                vkGetBufferMemoryRequirements(Renderer->RenderDevice.Device, Buffer, &MemoryRequirements);

                assert(MemoryRequirements.size == Frame->VertexStack.VertexStackSize);

                u32 MemoryTypes = MemoryRequirements.memoryTypeBits & Renderer->RenderDevice.MemoryTypes.HostVisibleCoherent;
                u32 MemoryType = 0;
                if (BitScanForward(&MemoryType, MemoryTypes) != 0)
                {
                    VkMemoryAllocateInfo AllocInfo = 
                    {
                        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                        .pNext = nullptr,
                        .allocationSize = Frame->VertexStack.VertexStackSize,
                        .memoryTypeIndex = MemoryType,
                    };

                    VkDeviceMemory Memory;
                    if (vkAllocateMemory(Renderer->RenderDevice.Device, &AllocInfo, nullptr, &Memory) == VK_SUCCESS)
                    {
                        if (vkBindBufferMemory(Renderer->RenderDevice.Device, Buffer, Memory, 0) == VK_SUCCESS)
                        {
                            // NOTE(boti): persistent mapping
                            void* Mapping = nullptr;
                            if (vkMapMemory(Renderer->RenderDevice.Device, Memory, 0, VK_WHOLE_SIZE, 0, &Mapping) == VK_SUCCESS)
                            {
                                Frame->VertexStack.Memory = Memory;
                                Frame->VertexStack.Buffer = Buffer;
                                Frame->VertexStack.Size = Frame->VertexStack.VertexStackSize;
                                Frame->VertexStack.At = 0;
                                Frame->VertexStack.Mapping = Mapping;
                            }
                            else
                            {
                                vkFreeMemory(Renderer->RenderDevice.Device, Memory, nullptr);
                                vkDestroyBuffer(Renderer->RenderDevice.Device, Buffer, nullptr);
                                return false;
                            }
                        }
                        else
                        {
                            vkFreeMemory(Renderer->RenderDevice.Device, Memory, nullptr);
                            vkDestroyBuffer(Renderer->RenderDevice.Device, Buffer, nullptr);
                            return false;
                        }
                    }
                    else
                    {
                        vkDestroyBuffer(Renderer->RenderDevice.Device, Buffer, nullptr);
                        return false;
                    }
                }
                else
                {
                    vkDestroyBuffer(Renderer->RenderDevice.Device, Buffer, nullptr);
                    return false;
                }
            }
            else
            {
                return false;
            }
        }

        // Create per frame (indirect) command buffer
        {
            VkBufferCreateInfo BufferInfo = 
            {
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .size = Frame->DrawCommands.MemorySize,
                .usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                .queueFamilyIndexCount = 0,
                .pQueueFamilyIndices = nullptr,
            };

            VkBuffer Buffer = VK_NULL_HANDLE;
            if (vkCreateBuffer(Renderer->RenderDevice.Device, &BufferInfo, nullptr, &Buffer) == VK_SUCCESS)
            {
                VkMemoryRequirements MemoryRequirements = {};
                vkGetBufferMemoryRequirements(Renderer->RenderDevice.Device, Buffer, &MemoryRequirements);

                assert(MemoryRequirements.size == Frame->DrawCommands.MemorySize);

                u32 MemoryTypes = MemoryRequirements.memoryTypeBits & Renderer->RenderDevice.MemoryTypes.HostVisibleCoherent;
                u32 MemoryType = 0;
                if (BitScanForward(&MemoryType, MemoryTypes) != 0)
                {
                    VkMemoryAllocateInfo AllocInfo = 
                    {
                        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                        .pNext = nullptr,
                        .allocationSize = Frame->DrawCommands.MemorySize,
                        .memoryTypeIndex = MemoryType,
                    };

                    VkDeviceMemory Memory = VK_NULL_HANDLE;
                    if (vkAllocateMemory(Renderer->RenderDevice.Device, &AllocInfo, nullptr, &Memory) == VK_SUCCESS)
                    {
                        if (vkBindBufferMemory(Renderer->RenderDevice.Device, Buffer, Memory, 0) == VK_SUCCESS)
                        {
                            void* Mapping = nullptr;
                            if (vkMapMemory(Renderer->RenderDevice.Device, Memory, 0, Frame->DrawCommands.MemorySize, 0, &Mapping) == VK_SUCCESS)
                            {
                                Frame->DrawCommands.Memory = Memory;
                                Frame->DrawCommands.Buffer = Buffer;

                                Frame->DrawCommands.DrawIndex = 0;
                                Frame->DrawCommands.Commands = (VkDrawIndirectCommand*)Mapping;
                            }
                            else
                            {
                                vkFreeMemory(Renderer->RenderDevice.Device, Memory, nullptr);
                                vkDestroyBuffer(Renderer->RenderDevice.Device, Buffer, nullptr);
                                return false;
                            }
                        }
                        else
                        {
                            vkFreeMemory(Renderer->RenderDevice.Device, Memory, nullptr);
                            vkDestroyBuffer(Renderer->RenderDevice.Device, Buffer, nullptr);
                            return false;
                        }
                    }
                    else
                    {
                        vkDestroyBuffer(Renderer->RenderDevice.Device, Buffer, nullptr);
                        return false;
                    }
                }
                else
                {
                    vkDestroyBuffer(Renderer->RenderDevice.Device, Buffer, nullptr);
                    return false;
                }
            }
            else
            {
                return false;
            }
        }

        // Create per frame instance data
        {
            VkBufferCreateInfo BufferInfo = 
            {
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .size = Frame->ChunkPositions.MemorySize,
                .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                .queueFamilyIndexCount = 0,
                .pQueueFamilyIndices = nullptr,
            };

            VkBuffer Buffer = VK_NULL_HANDLE;
            if (vkCreateBuffer(Renderer->RenderDevice.Device, &BufferInfo, nullptr, &Buffer) == VK_SUCCESS)
            {
                VkMemoryRequirements MemoryRequirements = {};
                vkGetBufferMemoryRequirements(Renderer->RenderDevice.Device, Buffer, &MemoryRequirements);

                assert(MemoryRequirements.size == Frame->ChunkPositions.MemorySize);

                u32 MemoryTypes = MemoryRequirements.memoryTypeBits & Renderer->RenderDevice.MemoryTypes.HostVisibleCoherent;
                u32 MemoryType = 0;
                if (BitScanForward(&MemoryType, MemoryTypes) != 0)
                {
                    VkMemoryAllocateInfo AllocInfo = 
                    {
                        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                        .pNext = nullptr,
                        .allocationSize = Frame->ChunkPositions.MemorySize,
                        .memoryTypeIndex = MemoryType,
                    };

                    VkDeviceMemory Memory = VK_NULL_HANDLE;
                    if (vkAllocateMemory(Renderer->RenderDevice.Device, &AllocInfo, nullptr, &Memory) == VK_SUCCESS)
                    {
                        if (vkBindBufferMemory(Renderer->RenderDevice.Device, Buffer, Memory, 0) == VK_SUCCESS)
                        {
                            void* Mapping = nullptr;
                            if (vkMapMemory(Renderer->RenderDevice.Device, Memory, 0, Frame->ChunkPositions.MemorySize, 0, &Mapping) == VK_SUCCESS)
                            {
                                Frame->ChunkPositions.Memory = Memory;
                                Frame->ChunkPositions.Buffer = Buffer;

                                Frame->ChunkPositions.ChunkAt = 0;
                                Frame->ChunkPositions.Mapping = (vec2*)Mapping;
                            }
                            else
                            {
                                vkFreeMemory(Renderer->RenderDevice.Device, Memory, nullptr);
                                vkDestroyBuffer(Renderer->RenderDevice.Device, Buffer, nullptr);
                                return false;
                            }
                        }
                        else
                        {
                            vkFreeMemory(Renderer->RenderDevice.Device, Memory, nullptr);
                            vkDestroyBuffer(Renderer->RenderDevice.Device, Buffer, nullptr);
                            return false;
                        }
                    }
                    else
                    {
                        vkDestroyBuffer(Renderer->RenderDevice.Device, Buffer, nullptr);
                        return false;
                    }
                }
                else
                {
                    vkDestroyBuffer(Renderer->RenderDevice.Device, Buffer, nullptr);
                    return false;
                }
            }
            else
            {
                return false;
            }
        }
    }
    return true;
}

renderer_frame_params* Renderer_NewFrame(renderer* Renderer, u64 FrameIndex)
{
    TIMED_FUNCTION();

    u32 BufferIndex = Renderer->NextBufferIndex;
    Renderer->NextBufferIndex = (Renderer->NextBufferIndex + 1) % 2;

    renderer_frame_params* Frame = Renderer->FrameParams + BufferIndex;
    Frame->FrameIndex = FrameIndex;
    Frame->BufferIndex = BufferIndex;
    Frame->Renderer = Renderer;
    Frame->SwapchainImageIndex = INVALID_INDEX_U32;
    Frame->VertexStack.At = 0;
    Frame->DrawCommands.DrawIndex = 0;
    Frame->ChunkPositions.ChunkAt = 0;

    {
        TIMED_BLOCK("WaitForPreviousFrames");
        
        vkWaitForFences(Renderer->RenderDevice.Device, 1, &Frame->RenderFinishedFence, VK_TRUE, UINT64_MAX);
        vkResetFences(Renderer->RenderDevice.Device, 1, &Frame->RenderFinishedFence);
    }

    VkResult Result = vkAcquireNextImageKHR(
        Renderer->RenderDevice.Device, Renderer->Swapchain, 
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

    vkResetCommandPool(Renderer->RenderDevice.Device, Frame->CmdPool, VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);
    return Frame;
}

void Renderer_SubmitFrame(renderer* Renderer, renderer_frame_params* Frame)
{
    TIMED_FUNCTION();

    {
        TIMED_BLOCK("Submit");

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

        vkQueueSubmit(Renderer->RenderDevice.GraphicsQueue, 1, &SubmitInfo, Frame->RenderFinishedFence);
    }

    {
        TIMED_BLOCK("Present");

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

        vkQueuePresentKHR(Renderer->RenderDevice.GraphicsQueue, &PresentInfo);
    }
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

    vkCmdBeginRendering(Frame->CmdBuffer, &RenderingInfo);

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

    vkCmdEndRendering(Frame->CmdBuffer);

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

void Renderer_RenderChunks(renderer_frame_params* Frame, u32 Count, chunk_render_data* Chunks)
{
    TIMED_FUNCTION();

    frustum ViewFrustum = Frame->Camera.GetFrustum((f32)Frame->Renderer->SwapchainSize.width / Frame->Renderer->SwapchainSize.height);

    VkDeviceSize DrawBufferOffset = Frame->DrawCommands.DrawIndex * sizeof(VkDrawIndirectCommand);
    u32 DrawCount = 0;

    VkDrawIndirectCommand* FirstCommand = Frame->DrawCommands.Commands + Frame->DrawCommands.DrawIndex;
    for (u32 i = 0; i < Count; i++)
    {
        chunk_render_data* Chunk = Chunks + i;
        
        vec2 ChunkP = { (f32)Chunk->P.x, (f32)Chunk->P.y };
        vec3 ChunkP3 = { ChunkP.x, ChunkP.y, 0.0f };

        aabb ChunkBox = MakeAABB(ChunkP3, ChunkP3 + vec3{ CHUNK_DIM_XY, CHUNK_DIM_XY, CHUNK_DIM_Z });

        if (IntersectFrustumAABB(ViewFrustum, ChunkBox))
        {
            *Chunk->LastRenderedInFrameIndex = Frame->BufferIndex;

            u32 InstanceOffset = (u32)Frame->ChunkPositions.ChunkAt++;
            memcpy(Frame->ChunkPositions.Mapping + InstanceOffset, &ChunkP, sizeof(ChunkP));

            FirstCommand[Frame->DrawCommands.DrawIndex++] = 
            { 
                .vertexCount = Chunk->VertexBlock->VertexCount,
                .instanceCount = 1, 
                .firstVertex = Chunk->VertexBlock->VertexOffset, 
                .firstInstance = InstanceOffset, 
            };
            DrawCount++;
        }
    }

    vkCmdBindPipeline(Frame->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Frame->Renderer->Pipeline);
    vkCmdBindDescriptorSets(Frame->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Frame->Renderer->PipelineLayout, 
        0, 1, &Frame->Renderer->DescriptorSet, 0, nullptr);

    VkDeviceSize VertexBufferOffset = 0;
    vkCmdBindVertexBuffers(Frame->CmdBuffer, 0, 1, &Frame->Renderer->VB.Buffer, &VertexBufferOffset);
    vkCmdBindVertexBuffers(Frame->CmdBuffer, 1, 1, &Frame->ChunkPositions.Buffer, &VertexBufferOffset);
    
    mat4 VP = Frame->ProjectionTransform * Frame->ViewTransform;

    vkCmdPushConstants(
        Frame->CmdBuffer,
        Frame->Renderer->PipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT, 0,
        sizeof(mat4), &VP);
    vkCmdDrawIndirect(Frame->CmdBuffer, Frame->DrawCommands.Buffer, DrawBufferOffset, DrawCount, sizeof(VkDrawIndirectCommand));
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

    vkCmdSetPrimitiveTopology(Frame->CmdBuffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
}

void Renderer_ImmediateBox(renderer_frame_params* Frame, aabb Box, u32 Color)
{
    TIMED_FUNCTION();

    const vertex VertexData[] =
    {
        // EAST
        { { Box.Max.x, Box.Min.y, Box.Min.z, }, { }, Color },
        { { Box.Max.x, Box.Max.y, Box.Min.z, }, { }, Color },
        { { Box.Max.x, Box.Max.y, Box.Max.z, }, { }, Color },
        { { Box.Max.x, Box.Min.y, Box.Min.z, }, { }, Color },
        { { Box.Max.x, Box.Max.y, Box.Max.z, }, { }, Color },
        { { Box.Max.x, Box.Min.y, Box.Max.z, }, { }, Color },
                                        
        // WEST                         
        { { Box.Min.x, Box.Min.y, Box.Min.z, }, { }, Color },
        { { Box.Min.x, Box.Max.y, Box.Max.z, }, { }, Color },
        { { Box.Min.x, Box.Max.y, Box.Min.z, }, { }, Color },
        { { Box.Min.x, Box.Min.y, Box.Min.z, }, { }, Color },
        { { Box.Min.x, Box.Min.y, Box.Max.z, }, { }, Color },
        { { Box.Min.x, Box.Max.y, Box.Max.z, }, { }, Color },
                                        
        // NORTH                        
        { { Box.Min.x, Box.Max.y, Box.Min.z, }, { }, Color },
        { { Box.Max.x, Box.Max.y, Box.Max.z, }, { }, Color },
        { { Box.Max.x, Box.Max.y, Box.Min.z, }, { }, Color },
        { { Box.Min.x, Box.Max.y, Box.Min.z, }, { }, Color },
        { { Box.Min.x, Box.Max.y, Box.Max.z, }, { }, Color },
        { { Box.Max.x, Box.Max.y, Box.Max.z, }, { }, Color },
                                        
        // SOUTH                        
        { { Box.Min.x, Box.Min.y, Box.Min.z, }, { }, Color },
        { { Box.Max.x, Box.Min.y, Box.Min.z, }, { }, Color },
        { { Box.Max.x, Box.Min.y, Box.Max.z, }, { }, Color },
        { { Box.Min.x, Box.Min.y, Box.Min.z, }, { }, Color },
        { { Box.Max.x, Box.Min.y, Box.Max.z, }, { }, Color },
        { { Box.Min.x, Box.Min.y, Box.Max.z, }, { }, Color },
                                        
        // TOP                          
        { { Box.Min.x, Box.Min.y, Box.Max.z, }, { }, Color },
        { { Box.Max.x, Box.Min.y, Box.Max.z, }, { }, Color },
        { { Box.Max.x, Box.Max.y, Box.Max.z, }, { }, Color },
        { { Box.Min.x, Box.Min.y, Box.Max.z, }, { }, Color },
        { { Box.Max.x, Box.Max.y, Box.Max.z, }, { }, Color },
        { { Box.Min.x, Box.Max.y, Box.Max.z, }, { }, Color },
                                        
        // BOTTOM                       
        { { Box.Min.x, Box.Min.y, Box.Min.z, }, { }, Color },
        { { Box.Max.x, Box.Max.y, Box.Min.z, }, { }, Color },
        { { Box.Max.x, Box.Min.y, Box.Min.z, }, { }, Color },
        { { Box.Min.x, Box.Min.y, Box.Min.z, }, { }, Color },
        { { Box.Min.x, Box.Max.y, Box.Min.z, }, { }, Color },
        { { Box.Max.x, Box.Max.y, Box.Min.z, }, { }, Color },
    };
    constexpr u32 VertexCount = CountOf(VertexData);

    u64 Offset = Frame_PushToStack(Frame, 16, VertexData, sizeof(VertexData));
    if (Offset != INVALID_INDEX_U64)
    {
        vkCmdBindVertexBuffers(Frame->CmdBuffer, 0, 1, &Frame->VertexStack.Buffer, &Offset);

        mat4 Transform = Frame->ProjectionTransform * Frame->ViewTransform;
        vkCmdPushConstants(Frame->CmdBuffer, Frame->Renderer->ImPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mat4), &Transform);
        vkCmdDraw(Frame->CmdBuffer, VertexCount, 1, 0, 0);
    }
    else
    {
        assert(!"Renderer_ImmediateBox failed");
    }
}

void Renderer_ImmediateBoxOutline(renderer_frame_params* Frame, f32 OutlineSize, aabb Box, u32 Color)
{
    TIMED_FUNCTION();

    vkCmdSetDepthBias(Frame->CmdBuffer, 0.0f, 0.0f, 0.0f);

    const aabb Boxes[] = 
    {
        // Bottom
        MakeAABB({ Box.Min.x, Box.Min.y, Box.Min.z }, { Box.Max.x, Box.Min.y + OutlineSize, Box.Min.z + OutlineSize }),
        MakeAABB({ Box.Min.x, Box.Min.y, Box.Min.z }, { Box.Min.x + OutlineSize, Box.Max.y, Box.Min.z + OutlineSize }),
        MakeAABB({ Box.Max.x, Box.Min.y, Box.Min.z }, { Box.Max.x - OutlineSize, Box.Max.y, Box.Min.z + OutlineSize }),
        MakeAABB({ Box.Min.x, Box.Max.y, Box.Min.z }, { Box.Max.x - OutlineSize, Box.Max.y - OutlineSize, Box.Min.z + OutlineSize }),

        // Top
        MakeAABB({ Box.Min.x, Box.Min.y, Box.Max.z }, { Box.Max.x, Box.Min.y + OutlineSize, Box.Max.z - OutlineSize }),
        MakeAABB({ Box.Min.x, Box.Min.y, Box.Max.z }, { Box.Min.x + OutlineSize, Box.Max.y, Box.Max.z - OutlineSize }),
        MakeAABB({ Box.Max.x, Box.Min.y, Box.Max.z }, { Box.Max.x - OutlineSize, Box.Max.y, Box.Max.z - OutlineSize }),
        MakeAABB({ Box.Min.x, Box.Max.y, Box.Max.z }, { Box.Max.x - OutlineSize, Box.Max.y - OutlineSize, Box.Max.z - OutlineSize }),

        // Side
        MakeAABB({ Box.Min.x, Box.Min.y, Box.Min.z }, { Box.Min.x + OutlineSize, Box.Min.y + OutlineSize, Box.Max.z }),
        MakeAABB({ Box.Max.x, Box.Min.y, Box.Min.z }, { Box.Max.x - OutlineSize, Box.Min.y + OutlineSize, Box.Max.z }),
        MakeAABB({ Box.Min.x, Box.Max.y, Box.Min.z }, { Box.Min.x + OutlineSize, Box.Max.y - OutlineSize, Box.Max.z }),
        MakeAABB({ Box.Max.x, Box.Max.y, Box.Min.z }, { Box.Max.x - OutlineSize, Box.Max.y - OutlineSize, Box.Max.z }),
    };
    constexpr u32 BoxCount = CountOf(Boxes);

    vkCmdSetDepthBias(Frame->CmdBuffer, -1.0f, 0.0f, -1.0f);
    for (u32 i = 0; i < BoxCount; i++)
    {
        Renderer_ImmediateBox(Frame, Boxes[i], Color);
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
        vkCmdSetPrimitiveTopology(Frame->CmdBuffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
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