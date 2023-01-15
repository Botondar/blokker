#pragma once

#include <Common.hpp>
#include <Math.hpp>
#include <vulkan/vulkan.h>
#include <Memory.hpp>

struct game_memory
{
    u64 MemorySize;
    void* Memory;

    struct game_state* Game;
};

enum mouse_button : u32
{
    MOUSE_LEFT = 0,
    MOUSE_RIGHT = 1,
    MOUSE_MIDDLE = 2,
    MOUSE_EXTRA0 = 3,
    MOUSE_EXTRA1 = 4,
    MOUSE_ButtonCount,
};

struct game_io
{
    f32 DeltaTime;

    // ShouldQuit is both whether a quit request happened in the platform layer
    // _and_ something the game code can set to quit the app
    bool ShouldQuit;

    // Platform events the game might need to react to
    bool NeedRendererResize; // NOTE(boti): The game code should set this to false when it processes the resize
    bool IsMinimized;

    // Peripherals
    bool IsCursorEnabled;
    bool MouseButtons[MOUSE_ButtonCount];
    vec2 MouseP;
    vec2 MouseDelta;
    f32 WheelDelta;

    bool EscapePressed;
    bool BacktickPressed;
    bool MPressed;

    bool Forward;
    bool Back;
    bool Right;
    bool Left;

    bool Space;
    bool LeftShift;
    bool LeftControl;
    bool LeftAlt;
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

buffer LoadEntireFile(const char* Path, memory_arena* Arena);

VkSurfaceKHR CreateVulkanSurface(VkInstance vkInstance);

bool ToggleCursor();

s64 GetPerformanceCounter();
f32 GetElapsedTime(s64 Start, s64 End);
f32 GetTimeFromCounter(s64 Counter);