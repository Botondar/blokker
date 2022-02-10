#pragma once

#include <Common.hpp>
#include <vulkan/vulkan.h>

typedef void* native_handle;

void DebugPrint_(const char* Format, ...);

#if DEVELOPER
#define DebugPrint(...) DebugPrint_(__VA_ARGS__)
#else
#define DebugPrint(...)
#endif

CBuffer LoadEntireFile(const char* Path);
bool WriteEntireFile(const char* Path, u64 Size, const void* Data);

VkSurfaceKHR CreateVulkanSurface(VkInstance vkInstance);

bool ToggleCursor();

s64 GetPerformanceCounter();
f32 GetElapsedTime(s64 Start, s64 End);
f32 GetTimeFromCounter(s64 Counter);