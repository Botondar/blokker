#pragma once

#include <Common.hpp>
#include <vulkan/vulkan.h>

typedef void* native_handle;

struct game_memory
{
    u64 MemorySize;
    void* Memory;

    struct game_state* Game;
};

void DebugPrint_(const char* Format, ...);

#if DEVELOPER
#define DebugPrint(...) DebugPrint_(__VA_ARGS__)
#else
#define DebugPrint(...)
#endif

void PlatformLog_(const char* Function, int Line, const char* Format, ...);
#define LogMsg(fmt, ...) PlatformLog_(__FUNCTION__, __LINE__, fmt, __VA_ARGS__)

// TODO: runtime variable?
constexpr u64 PLATFORM_PAGE_SIZE = 4096;

void* Platform_VirtualAlloc(void* Pointer, u64 Size);
bool Platform_VirtualFree(void* Pointer, u64 Size);

CBuffer LoadEntireFile(const char* Path);
bool WriteEntireFile(const char* Path, u64 Size, const void* Data);

VkSurfaceKHR CreateVulkanSurface(VkInstance vkInstance);

bool ToggleCursor();

s64 GetPerformanceCounter();
f32 GetElapsedTime(s64 Start, s64 End);
f32 GetTimeFromCounter(s64 Counter);