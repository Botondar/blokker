#pragma once

#include <Common.hpp>
#include <Math.hpp>
#include <vulkan/vulkan.h>
#include <Memory.hpp>

typedef function<void(memory_arena*)> work_function;

// Work queue is an opaque type to the game
struct platform_work_queue;

typedef void (add_work_func)(platform_work_queue* Queue, work_function Function);
typedef void (wait_for_all_work_func)(platform_work_queue* Queue);
typedef void (debug_print_func)(const char* Format, ...);
typedef void (log_msg_func)(const char* Function, int Line, const char* Format, ...);
typedef buffer (load_entire_file_func)(const char* Path, memory_arena* Arena);
typedef VkSurfaceKHR (create_vulkan_surface_func)(VkInstance VulkanInstance);
typedef bool (toggle_cursor_func)();
typedef s64 (get_performance_counter_func)();
typedef f32 (get_elapsed_time_func)(s64 Start, s64 End);
typedef f32 (get_time_from_counter_func)(s64 Counter);

struct platform_api
{
    add_work_func* AddWork;
    wait_for_all_work_func* WaitForAllWork;
    debug_print_func* DebugPrint;
    log_msg_func* LogMsg;
    load_entire_file_func* LoadEntireFile;
    create_vulkan_surface_func* CreateVulkanSurface;
    toggle_cursor_func* ToggleCursor;
    get_performance_counter_func* GetPerformanceCounter;
    get_elapsed_time_func* GetElapsedTime;
    get_time_from_counter_func* GetTimeFromCounter;

    platform_work_queue* HighPriorityQueue;
    platform_work_queue* LowPriorityQueue;
};

struct game_memory
{
    u64 MemorySize;
    void* Memory;

    platform_api Platform;

    struct game_state* Game;
};

//
// Input
//
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
    u32 FrameIndex;
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

// TODO: runtime variable?
constexpr u64 PLATFORM_PAGE_SIZE = 4096;

// Implemented by the game
typedef void (update_and_render_func)(game_memory* Memory, game_io* IO);