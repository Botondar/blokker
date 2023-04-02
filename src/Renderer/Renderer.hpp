#pragma once

#define BLOKKER_RENDERER_REWRITE 1

#include <Common.hpp>
#include <Intrinsics.hpp>
#include <Math.hpp>
#include <Memory.hpp>
#include <Shapes.hpp>
#include <imgui/imgui.h>

#include <Renderer/RenderAPI.hpp>

#include <Platform.hpp>

#include <vulkan/vulkan.h>
#include <Renderer/RenderDevice.hpp>
#include <Renderer/RTHeap.hpp>
#include <Renderer/StagingHeap.hpp>
#include <Renderer/VertexBuffer.hpp>

extern platform_api Platform;

enum attrib_location : u32
{
    ATTRIB_POS = 0,
    ATTRIB_TEXCOORD = 1,
    ATTRIB_COLOR = 2,
    ATTRIB_CHUNK_P = 3,
};

struct vulkan_render_frame : public render_frame
{
    VkCommandPool CmdPool;
    VkCommandBuffer PrimaryCmdBuffer;
    VkCommandBuffer TransferCmdBuffer;

    VkCommandBuffer SceneCmdBuffer;
    VkCommandBuffer ImmediateCmdBuffer;
    VkCommandBuffer ImGuiCmdBuffer;

    VkSemaphore ImageAcquiredSemaphore;
    VkSemaphore RenderFinishedSemaphore;
    VkSemaphore TransferFinishedSemaphore;
    VkFence RenderFinishedFence;

    VkImage DepthBuffer;
    VkImageView DepthBufferView;

    VkImage SwapchainImage;
    VkImageView SwapchainImageView;
    u32 SwapchainImageIndex;

    // Indirect draw commands
    VkDeviceMemory DrawMemory;
    VkBuffer DrawBuffer;
    void* DrawMapping;

    // Per-instance data for indirect draw commands
    VkDeviceMemory PositionMemory;
    VkBuffer PositionBuffer;
    void* PositionMapping;

    // Vertex buffer for immediate-mode rendering
    VkDeviceMemory VertexMemory;
    VkBuffer VertexBuffer;
    void* VertexMapping;
    u64 VertexSize;
    u64 VertexOffset;
};

struct renderer 
{
    render_device RenderDevice;

    VkSurfaceFormatKHR SurfaceFormat;
    VkSurfaceKHR Surface;
    VkSwapchainKHR Swapchain;
    VkSampleCountFlagBits Multisampling;
    u32 SwapchainImageCount;
    VkExtent2D SwapchainSize;
    static constexpr u32 MaxSwapchainImageCount = 16;
    VkImage SwapchainImages[MaxSwapchainImageCount];
    VkImageView SwapchainImageViews[MaxSwapchainImageCount];

    VkCommandPool TransferCmdPool;
    VkCommandBuffer TransferCmdBuffer;

    // Offscreen render buffers
    VkImage DepthBuffer;
    VkImageView DepthBufferView;
    render_target_heap RTHeap;

    VkFormat DepthFormat;
    VkFormat StencilFormat;

    vulkan_render_frame FrameParams[MaxSwapchainImageCount];

    staging_heap StagingHeap;

    vertex_buffer VB;

    VkPipelineLayout PipelineLayout;
    VkPipeline Pipeline;

    VkPipelineLayout ImPipelineLayout;
    VkPipeline ImPipeline;

    VkPipelineLayout ImGuiPipelineLayout;
    VkPipeline ImGuiPipeline;

    VkSampler Sampler;
    VkDescriptorSetLayout DescriptorSetLayout;
    VkDescriptorPool DescriptorPool;
    VkDescriptorSet DescriptorSet;

    VkSampler ImGuiSampler;
    VkDescriptorSetLayout ImGuiSetLayout;
    VkDescriptorPool ImGuiDescriptorPool;
    VkDescriptorSet ImGuiDescriptorSet;

    VkImage Tex;
    VkImageView TexView;
    VkDeviceMemory TexMemory;

    VkImage ImGuiTex;
    VkImageView ImGuiTexView;
    VkDeviceMemory ImGuiTexMemory;

    static constexpr u32 ImGuiTextureID = 1;

    u64 CurrentFrameIndex;
};