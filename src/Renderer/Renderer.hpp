#pragma once

#define ENABLE_VK_SHADER_OBJECT 1

#include <Common.hpp>
#include <Intrinsics.hpp>
#include <Math.hpp>
#include <Memory.hpp>
#include <Shapes.hpp>
#include <imgui/imgui.h>

#include <Renderer/RenderAPI.hpp>

#include <Platform.hpp>

#include <vulkan/vulkan.h>
#define Vulkan_FunctionSignature(name, ret, ...) typedef ret (VKAPI_CALL *PFN_##name)(__VA_ARGS__)
#define Vulkan_CommandSignature(name, ...) Vulkan_FunctionSignature(name, void, VkCommandBuffer, __VA_ARGS__)
#define Vulkan_DeclareFunctionPointer(name) PFN_##name name

#ifndef VK_EXT_shader_object
#define VK_EXT_shader_object 1
#define VK_EXT_SHADER_OBJECT_EXTENSION_NAME "VK_EXT_shader_object"

// VkObjectType
#define VK_OBJECT_TYPE_SHADER_EXT ((VkObjectType)1000482000)
// VkResult
#define VK_ERROR_INCOMPATIBLE_SHADER_BINARY_EXT ((VkResult)1000482000)
// VkStructureType
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT ((VkStructureType)1000482000)
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_PROPERTIES_EXT ((VkStructureType)1000482001)
#define VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT ((VkStructureType)1000482002)
#define VK_STRUCTURE_TYPE_SHADER_REQUIRED_SUBGROUP_SIZE_CREATE_INFO_EXT ((VkStructreType)VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO)
#define VK_STRUCTURE_TYPE_VERTEX_INPUT_ATTRIBUTE_DESCRIPTION_2_EXT ((VkStructureType)1000352002)
#define VK_STRUCTURE_TYPE_VERTEX_INPUT_BINDING_DESCRIPTION_2_EXT ((VkStructureType)1000352001)

VK_DEFINE_HANDLE(VkShaderEXT);

typedef enum VkShaderCodeTypeEXT {
    VK_SHADER_CODE_TYPE_BINARY_EXT = 0,
    VK_SHADER_CODE_TYPE_SPIRV_EXT = 1,
} VkShaderCodeTypeEXT;

typedef enum VkShaderCreateFlagBitsEXT {
    VK_SHADER_CREATE_LINK_STAGE_BIT_EXT = 0x00000001,
    VK_SHADER_CREATE_ALLOW_VARYING_SUBGROUP_SIZE_BIT_EXT = 0x00000002,
    VK_SHADER_CREATE_REQUIRE_FULL_SUBGROUPS_BIT_EXT = 0x00000004,
    VK_SHADER_CREATE_NO_TASK_SHADER_BIT_EXT = 0x00000008,
    VK_SHADER_CREATE_DISPATCH_BASE_BIT_EXT = 0x00000010,
    VK_SHADER_CREATE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_EXT = 0x00000020,
    VK_SHADER_CREATE_FRAGMENT_DENSITY_MAP_ATTACHMENT_BIT_EXT = 0x00000040,
} VkShaderCreateFlagBitsEXT;
typedef VkFlags VkShaderCreateFlagsEXT;

// Extends VkPhysicalDeviceFeatures2, VkDeviceCreateInfo
typedef struct VkPhysicalDeviceShaderObjectFeaturesEXT {
    VkStructureType    sType;
    void*              pNext;
    VkBool32           shaderObject;
} VkPhysicalDeviceShaderObjectFeaturesEXT;

// Extends VkPhysicalDeviceProperties2
typedef struct VkPhysicalDeviceShaderObjectPropertiesEXT {
    VkStructureType    sType;
    void*              pNext;
    uint8_t            shaderBinaryUUID[VK_UUID_SIZE];
    uint32_t           shaderBinaryVersion;
} VkPhysicalDeviceShaderObjectPropertiesEXT;

typedef struct VkShaderCreateInfoEXT {
    VkStructureType                 sType;
    const void*                     pNext;
    VkShaderCreateFlagsEXT          flags;
    VkShaderStageFlagBits           stage;
    VkShaderStageFlags              nextStage;
    VkShaderCodeTypeEXT             codeType;
    size_t                          codeSize;
    const void*                     pCode;
    const char*                     pName;
    uint32_t                        setLayoutCount;
    const VkDescriptorSetLayout*    pSetLayouts;
    uint32_t                        pushConstantRangeCount;
    const VkPushConstantRange*      pPushConstantRanges;
    const VkSpecializationInfo*     pSpecializationInfo;
} VkShaderCreateInfoEXT;

Vulkan_FunctionSignature(vkCreateShadersEXT, VkResult, VkDevice, uint32_t, const VkShaderCreateInfoEXT*, const VkAllocationCallbacks*, VkShaderEXT*);
Vulkan_FunctionSignature(vkDestroyShaderEXT, VkResult, VkDevice, VkShaderEXT, const VkAllocationCallbacks*);
Vulkan_FunctionSignature(vkGetShaderBinaryDataEXT, VkResult, VkDevice, VkShaderEXT, size_t*, void*);
Vulkan_CommandSignature(vkCmdBindShadersEXT, uint32_t, const VkShaderStageFlagBits*, const VkShaderEXT*);
#endif

extern Vulkan_DeclareFunctionPointer(vkCreateShadersEXT);
extern Vulkan_DeclareFunctionPointer(vkDestroyShaderEXT);
extern Vulkan_DeclareFunctionPointer(vkGetShaderBinaryDataEXT);
extern Vulkan_DeclareFunctionPointer(vkCmdBindShadersEXT);

// NOTE(boti): Because vulkan_core.h defines function prototypes 
//             even for the functions they don't themselves export,
//             we have to do this nonsense...
#define vkCmdSetVertexInputEXT vkCmdSetVertexInputEXT_
#define vkCmdSetAlphaToCoverageEnableEXT vkCmdSetAlphaToCoverageEnableEXT_
#define vkCmdSetAlphaToOneEnableEXT vkCmdSetAlphaToOneEnableEXT_
#define vkCmdSetColorBlendAdvancedEXT vkCmdSetColorBlendAdvancedEXT_
#define vkCmdSetColorBlendEnableEXT vkCmdSetColorBlendEnableEXT_
#define vkCmdSetColorBlendEquationEXT vkCmdSetColorBlendEquationEXT_
#define vkCmdSetColorWriteMaskEXT vkCmdSetColorWriteMaskEXT_
#define vkCmdSetConservativeRasterizationModeEXT vkCmdSetConservativeRasterizationModeEXT_
#define vkCmdSetDepthClampEnableEXT vkCmdSetDepthClampEnableEXT_
#define vkCmdSetDepthClipEnableEXT vkCmdSetDepthClipEnableEXT_
#define vkCmdSetDepthClipNegativeOneToOneEXT vkCmdSetDepthClipNegativeOneToOneEXT_
#define vkCmdSetExtraPrimitiveOverestimationSizeEXT vkCmdSetExtraPrimitiveOverestimationSizeEXT_
#define vkCmdSetLineRasterizationModeEXT vkCmdSetLineRasterizationModeEXT_
#define vkCmdSetLineStippleEnableEXT vkCmdSetLineStippleEnableEXT_
#define vkCmdSetLogicOpEnableEXT vkCmdSetLogicOpEnableEXT_
#define vkCmdSetPolygonModeEXT vkCmdSetPolygonModeEXT_
#define vkCmdSetProvokingVertexModeEXT vkCmdSetProvokingVertexModeEXT_
#define vkCmdSetRasterizationSamplesEXT vkCmdSetRasterizationSamplesEXT_
#define vkCmdSetRasterizationStreamEXT vkCmdSetRasterizationStreamEXT_
#define vkCmdSetSampleLocationsEnableEXT vkCmdSetSampleLocationsEnableEXT_
#define vkCmdSetSampleMaskEXT vkCmdSetSampleMaskEXT_
#define vkCmdSetTessellationDomainOriginEXT vkCmdSetTessellationDomainOriginEXT_


// NOTE(boti): Extension commands inherited from VK_KHR_dynamic_state3 (and VK_EXT_vertex_input_dynamic_state)
extern Vulkan_DeclareFunctionPointer(vkCmdSetVertexInputEXT);
extern Vulkan_DeclareFunctionPointer(vkCmdSetAlphaToCoverageEnableEXT);
extern Vulkan_DeclareFunctionPointer(vkCmdSetAlphaToOneEnableEXT);
extern Vulkan_DeclareFunctionPointer(vkCmdSetColorBlendAdvancedEXT);
extern Vulkan_DeclareFunctionPointer(vkCmdSetColorBlendEnableEXT);
extern Vulkan_DeclareFunctionPointer(vkCmdSetColorBlendEquationEXT);
extern Vulkan_DeclareFunctionPointer(vkCmdSetColorWriteMaskEXT);
extern Vulkan_DeclareFunctionPointer(vkCmdSetConservativeRasterizationModeEXT);
extern Vulkan_DeclareFunctionPointer(vkCmdSetDepthClampEnableEXT);
extern Vulkan_DeclareFunctionPointer(vkCmdSetDepthClipEnableEXT);
extern Vulkan_DeclareFunctionPointer(vkCmdSetDepthClipNegativeOneToOneEXT);
extern Vulkan_DeclareFunctionPointer(vkCmdSetExtraPrimitiveOverestimationSizeEXT);
extern Vulkan_DeclareFunctionPointer(vkCmdSetLineRasterizationModeEXT);
extern Vulkan_DeclareFunctionPointer(vkCmdSetLineStippleEnableEXT);
extern Vulkan_DeclareFunctionPointer(vkCmdSetLogicOpEnableEXT);
extern Vulkan_DeclareFunctionPointer(vkCmdSetPolygonModeEXT);
extern Vulkan_DeclareFunctionPointer(vkCmdSetProvokingVertexModeEXT);
extern Vulkan_DeclareFunctionPointer(vkCmdSetRasterizationSamplesEXT);
extern Vulkan_DeclareFunctionPointer(vkCmdSetRasterizationStreamEXT);
extern Vulkan_DeclareFunctionPointer(vkCmdSetSampleLocationsEnableEXT);
extern Vulkan_DeclareFunctionPointer(vkCmdSetSampleMaskEXT);
extern Vulkan_DeclareFunctionPointer(vkCmdSetTessellationDomainOriginEXT);

// We probably don't care about the NV extensions (for now)
#if 0
extern Vulkan_DeclareFunctionPointer(vkCmdSetCoverageModulationModeNV);
extern Vulkan_DeclareFunctionPointer(vkCmdSetCoverageModulationTableEnableNV);
extern Vulkan_DeclareFunctionPointer(vkCmdSetCoverageModulationTableNV);
extern Vulkan_DeclareFunctionPointer(vkCmdSetCoverageReductionModeNV);
extern Vulkan_DeclareFunctionPointer(vkCmdSetCoverageToColorEnableNV);
extern Vulkan_DeclareFunctionPointer(vkCmdSetCoverageToColorLocationNV);
extern Vulkan_DeclareFunctionPointer(vkCmdSetViewportWScalingEnableNV);
extern Vulkan_DeclareFunctionPointer(vkCmdSetRepresentativeFragmentTestEnableNV);
extern Vulkan_DeclareFunctionPointer(vkCmdSetShadingRateImageEnableNV);
extern Vulkan_DeclareFunctionPointer(vkCmdSetViewportSwizzleNV);
#endif

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
#if ENABLE_VK_SHADER_OBJECT
    VkShaderEXT MainVS;
    VkShaderEXT MainFS;

    VkShaderEXT ImVS;
    VkShaderEXT ImFS;

    VkShaderEXT ImGuiVS;
    VkShaderEXT ImGuiFS;
#endif
//#else
    VkPipelineLayout PipelineLayout;
    VkPipeline Pipeline;

    VkPipelineLayout ImPipelineLayout;
    VkPipeline ImPipeline;

    VkPipelineLayout ImGuiPipelineLayout;
    VkPipeline ImGuiPipeline;
//#endif
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