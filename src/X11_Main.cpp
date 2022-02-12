// TODO(mark): Error handling. Like... Everywhere...
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <fcntl.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_xlib.h>

#include "Math.hpp"
#include "Platform.hpp"

#include "Common.hpp"
#include "Game.hpp"
#include "Profiler.hpp"
#include "Renderer.hpp"

#define AllEventMask 0x0fff

static const char* Win32_ClassName = "wndclass_blokker";
static const char* Win32_WindowTitle = "Blokker";

struct x11_state
{

    Display* Display;
    Window Window;
    // TODO(mark): PrevWindowPlacement
    // TODO(mark): DWORD WindowStyle
    // TODO(mark):  DWORD WINDOWPLACEMENT
    // TODO(mark): BOOL IsMinimized;
    // TODO(mark): BOOL IsFullscreen;

    s64 PerformanceFrequency = 1000 * 1000 * 1000;

    bool HasDebugger;

    vec2i MouseLDownP;
    bool IsCursorDisabled;
};
static x11_state X11State;

static bool SetClipCursorToWindow(bool Clip)
{
    if (Clip) {
        XGrabPointer(X11State.Display, X11State.Window, true, 0, GrabModeAsync, GrabModeAsync, X11State.Window, None, CurrentTime);
    } else {
        XUngrabPointer(X11State.Display, CurrentTime);
    }
    return Clip;
}

int ShowCursor(bool show)
{
    if (show) {
        XDefineCursor(X11State.Display, X11State.Window, XC_arrow);
    } else {
        XUndefineCursor(X11State.Display, X11State.Window);
    }
    return 0;
}

bool ToggleCursor()
{
    X11State.IsCursorDisabled = !X11State.IsCursorDisabled;
    SetClipCursorToWindow(X11State.IsCursorDisabled);
    ShowCursor(!X11State.IsCursorDisabled);
    return !X11State.IsCursorDisabled;
}

// NOTE(mark): s64 GetPerformanceCounter() -> Is this needed?

// NOTE(mark): https://github.com/Botondar/objload/blob/1cc9170e78549e0824389ee2a26d09bb23e9c69f/src/Linux_Timing.cpp#L15
s64 GetPerformanceCounter()
{
    timespec Time = {};
    clock_gettime(CLOCK_MONOTONIC_RAW, &Time);

    s64 Result = (s64)((u64)Time.tv_sec * 1000000000ull + (u64)Time.tv_nsec);
    return Result;
}

f32 GetElapsedTime(s64 Start, s64 End)
{
    f32 Result = (End - Start) / (f32)X11State.PerformanceFrequency;
    return Result;
}

f32 GetTimeFromCounter(s64 Counter)
{
    f32 Result = Counter / (f32)X11State.PerformanceFrequency;
    return Result;
}

static bool x11_ToggleFullscreen()
{
    // TODO(mark)
    return 0;
}

VkSurfaceKHR CreateVulkanSurface(VkInstance vkInstance)
{
    VkSurfaceKHR Surface = VK_NULL_HANDLE;

    VkXlibSurfaceCreateInfoKHR Info = {
        .sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
        .pNext = nullptr,
        .flags = 0,
        .dpy = X11State.Display,
        .window = X11State.Window,
    };

    VkResult Result = vkCreateXlibSurfaceKHR(vkInstance, &Info, nullptr, &Surface);
    if (Result != VK_SUCCESS) {
        Surface = VK_NULL_HANDLE;
    }

    return Surface;
}

void DebugPrint_(const char* Format, ...)
{
    constexpr size_t BufferSize = 768;
    char Buff[BufferSize];

    va_list ArgList;
    va_start(ArgList, Format);

    vsnprintf(Buff, BufferSize, Format, ArgList);

    // TODO(mark): If state has HasDebugger enabled do the stuff OutputDebugStringA does on Windows

    fputs(Buff, stdout);
    va_end(ArgList);
}

CBuffer LoadEntireFile(const char* Path)
{
    CBuffer Buffer = {};

    FILE* File = fopen(Path, "rb");
    if (File) {
        // TODO(boti): this is _NOT_ standard compliant for binary files
        fseek(File, 0, SEEK_END);
        Buffer.Size = (u64)ftell(File);
        fseek(File, 0, SEEK_SET);

        Buffer.Data = new u8[Buffer.Size];
        size_t SizeRead = fread(Buffer.Data, 1, Buffer.Size, File);
        assert(SizeRead == Buffer.Size);

        fclose(File);
    }

    return Buffer;
}

bool WriteEntireFile(const char* Path, u64 Size, const void* Data)
{
    // TODO(mark)
    return false;
}

// TODO(mark): static LRESULT CALLBACK MainWindowProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam) !!

// TODO(mark): static bool win32_ProcessInput(game_input* Input) !!

static vulkan_renderer Renderer;
static game_state GameState;

// extern Window XCreateWindow(
//     Display*		/* display */,
//     Window		/* parent */,
//     int			/* x */,
//     int			/* y */,
//     unsigned int	/* width */,
//     unsigned int	/* height */,
//     unsigned int	/* border_width */,
//     int			/* depth */,
//     unsigned int	/* class */,
//     Visual*		/* visual */,
//     unsigned long	/* valuemask */,
//     XSetWindowAttributes*	/* attributes */
// );

int main(int argc, char* argv[])
{
    X11State.Display = XOpenDisplay(nullptr);
    // TODO(mark): Win32State.HasDebugger = IsDebuggerPresent(); ??

    XSetWindowAttributes setWinAttr = {
        .event_mask = AllEventMask
    };

    int defScreen = DefaultScreen(X11State.Display);
    X11State.Window = XCreateWindow(
        X11State.Display,
        RootWindow(X11State.Display, defScreen),
        100,
        100,
        1280,
        720,
        0,
        CopyFromParent,
        InputOutput,
        CopyFromParent,
        CWBorderPixel | CWColormap | CWEventMask,
        &setWinAttr); // TODO(mark): Kill constants
    // TODO(mark): wnd EH

    XMapWindow(X11State.Display, X11State.Window);

    XSelectInput(X11State.Display, X11State.Window, ExposureMask | AllEventMask | PointerMotionMask | ResizeRedirectMask); // NOTE(mark): What is ExposureMask?

    GameState.Renderer = &Renderer;
    {
        // TODO(mark): mmap instead of malloc
        u64 ChunkHeadersSize = (u64)game_state::MaxChunkCount * sizeof(chunk);
        GameState.Chunks = (chunk*)malloc(ChunkHeadersSize);
        // TODO(mark): EH
        // TODO(boti): large pages
        u64 ChunkDataSize
            = (u64)game_state::MaxChunkCount * sizeof(chunk_data);
        GameState.ChunkData = (chunk_data*)malloc(ChunkDataSize);
        // TODO(mark): EH
    }
    if (!Game_Initialize(&GameState))
    {
        return -1;
    }

    game_input Input = {};

    f32 DeltaTime = 0.0f;
    f32 Time = 0.0f;
    u64 FrameCount = 0;
    bool IsRunning = true;

    vec2i prevMousePos = { 0, 0 };

    while (IsRunning)
    {
        Time += DeltaTime;
        s64 StartTime = GetPerformanceCounter();

        // TODO(mark): Window Title. Maybe not here

        // TODO(mark): Show frame time in window title

        {
            constexpr size_t BuffSize = 256;
            char Buff[BuffSize];

            snprintf(Buff, BuffSize, "%s [%.2fms | %.1fFPS]",
                Win32_WindowTitle,
                1000.0f * DeltaTime,
                1.0f / DeltaTime);

            XStoreName(X11State.Display, X11State.Window, Buff);
        }

        Input.MouseDelta = {};
        Input.EscapePressed = false;

        {
            vec2i wndMousePos;
            vec2i rootMousePos;
            Window rootWnd, childWnd;
            u32 mask;
            Input.IsCursorEnabled = false;
            XQueryPointer(X11State.Display, X11State.Window, &rootWnd, &childWnd, &rootMousePos.x, &rootMousePos.y, &wndMousePos.x, &wndMousePos.y, &mask);

            vec2i deltaMouse = wndMousePos - prevMousePos;
            DebugPrint("x: %d | y: %d\n", deltaMouse.x, deltaMouse.y);
            Input.MouseDelta = (vec2)deltaMouse;
            prevMousePos = wndMousePos;
        }

        while (XPending(X11State.Display))
        {
            XEvent event;
            XNextEvent(X11State.Display, &event);
            // TODO(mark): Full screen
            // TODO(mark): Handle exit event
            switch (event.type)
            {
                case KeyPress:
                {
                    switch (XLookupKeysym(&event.xkey, 0))
                    {
                        case XK_Escape:
                        {
                            IsRunning = false;
                            break;
                        }
                        case XK_w:
                        {
                            Input.Forward = true;
                            break;
                        }
                        case XK_s:
                        {
                            Input.Back = true;
                            break;
                        }
                        case XK_d:
                        {
                            Input.Right = true;
                            break;
                        }
                        case XK_a:
                        {
                            Input.Left = true;
                            break;
                        }
                        case XK_space:
                        {
                            Input.Space = true;
                            break;
                        }

                        case XK_Shift_L:
                        {
                            Input.LeftShift = true;
                            break;
                        }
                    }
                    break;
                }

                case KeyRelease:
                {
                    switch (XLookupKeysym(&event.xkey, 0))
                    {
                        case XK_w:
                        {
                            Input.Forward = false;
                            break;
                        }
                        case XK_s:
                        {
                            Input.Back = false;
                            break;
                        }
                        case XK_d:
                        {
                            Input.Right = false;
                            break;
                        }
                        case XK_a:
                        {
                            Input.Left = false;
                            break;
                        }
                        case XK_space:
                        {
                            Input.Space = false;
                            break;
                        }

                        case XK_Shift_L:
                        {
                            Input.LeftShift = false;
                            break;
                        }
                    }
                    break;
                }

                case MotionNotify:
                {
                    // TODO(mark): Raw input + implement
                }

                case ResizeRequest:
                {
                    GameState.NeedRendererResize = true;
                    break;
                }

                default:
                {
                    break;
                }
                    // TODO(mark): Other stuff from Win32_Main.cppL257 (win32_ProcessInput)
            }
        }
        Input.IsCursorEnabled = false; // !X11State.IsCursorDisabled;
        Game_UpdateAndRender(&GameState, &Input, DeltaTime);
        GameState.NeedRendererResize = false;

        // GlobalProfiler.Print(12.0e-3f);
        GlobalProfiler.Reset();

        s64 EndTime;
        EndTime = GetPerformanceCounter();
        DeltaTime = GetElapsedTime(StartTime, EndTime);
        FrameCount++;
    }
    // TODO(mark): Show cursor
    return 0;
}
