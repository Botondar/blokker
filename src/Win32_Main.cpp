#include <Windows.h>
#include <windowsx.h>
#include <io.h>
#include <fcntl.h>
#include <hidusage.h>

#include <cstdlib>
#include <cstdio>
#include <cassert>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>

#include <Platform.hpp>

#include <Common.hpp>
#include <Renderer/Renderer.hpp>
#include <Game.hpp>
#include <Profiler.hpp>

static const char* Win32_ClassName = "wndclass_blokker";
static const char* Win32_WindowTitle = "Blokker";

struct win32_state
{
    HINSTANCE Instance;
    HWND Window;

    WINDOWPLACEMENT PrevWindowPlacement = { sizeof(WINDOWPLACEMENT) };
    DWORD WindowStyle;
    BOOL WasWindowResized;
    BOOL IsMinimized;
    BOOL IsFullscreen;
    
    s64 PerformanceFrequency;

    BOOL HasDebugger;
    bool IsCursorDisabled;
};
static win32_state Win32State;

static bool SetClipCursorToWindow(bool Clip)
{
    if (Clip)
    {
        RECT Rect;
        GetClientRect(Win32State.Window, &Rect);
        POINT P0 = { Rect.left, Rect.top };
        POINT P1 = { Rect.right, Rect.bottom };
        ClientToScreen(Win32State.Window, &P0);
        ClientToScreen(Win32State.Window, &P1);
        Rect = 
        { 
            .left = P0.x, .top = P0.y, 
            .right = P1.x, .bottom = P1.y,
        };
        ClipCursor(&Rect);
    }
    else
    {
        ClipCursor(nullptr);
    }
    return Clip;
}

bool ToggleCursor()
{
    Win32State.IsCursorDisabled = !Win32State.IsCursorDisabled;
    SetClipCursorToWindow(Win32State.IsCursorDisabled);
    ShowCursor(!Win32State.IsCursorDisabled);
    return !Win32State.IsCursorDisabled;
}

s64 GetPerformanceCounter()
{
    s64 Result;
    QueryPerformanceCounter((LARGE_INTEGER*)&Result);
    return Result;
}

f32 GetElapsedTime(s64 Start, s64 End)
{
    f32 Result = (End - Start) / (f32)Win32State.PerformanceFrequency;
    return Result;
}

f32 GetTimeFromCounter(s64 Counter)
{
    f32 Result = Counter / (f32)Win32State.PerformanceFrequency;
    return Result;
}

static bool win32_ToggleFullscreen()
{
    DWORD WindowStyle = GetWindowLongA(Win32State.Window, GWL_STYLE);
    bool IsFullscreen = !((WindowStyle & WS_OVERLAPPEDWINDOW) != 0);

    if (!IsFullscreen)
    {
        GetWindowPlacement(Win32State.Window, &Win32State.PrevWindowPlacement);

        MONITORINFO MonitorInfo = { sizeof(MONITORINFO) };
        GetMonitorInfoA(MonitorFromWindow(Win32State.Window, MONITOR_DEFAULTTOPRIMARY), &MonitorInfo);

        SetWindowLongA(Win32State.Window, GWL_STYLE, WindowStyle & ~WS_OVERLAPPEDWINDOW);
        SetWindowPos(Win32State.Window, HWND_TOP,
            MonitorInfo.rcMonitor.left, MonitorInfo.rcMonitor.top,
            MonitorInfo.rcMonitor.right - MonitorInfo.rcMonitor.left,
            MonitorInfo.rcMonitor.bottom - MonitorInfo.rcMonitor.top,
            SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
    else
    {
        SetWindowLongA(Win32State.Window, GWL_STYLE, WindowStyle | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(Win32State.Window, &Win32State.PrevWindowPlacement);
        SetWindowPos(Win32State.Window, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }

    // Refresh clip rect
    SetClipCursorToWindow(Win32State.IsCursorDisabled);

    Win32State.WindowStyle = GetWindowLongA(Win32State.Window, GWL_STYLE);
    Win32State.IsFullscreen = !IsFullscreen;
    return Win32State.IsFullscreen;
}

VkSurfaceKHR CreateVulkanSurface(VkInstance vkInstance)
{
    VkSurfaceKHR Surface = VK_NULL_HANDLE;
    
    VkWin32SurfaceCreateInfoKHR Info = 
    {
        .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
        .pNext = nullptr,
        .flags = 0,
        .hinstance = Win32State.Instance,
        .hwnd = Win32State.Window,
    };
    
    VkResult Result = vkCreateWin32SurfaceKHR(vkInstance, &Info, nullptr, &Surface);
    if (Result != VK_SUCCESS)
    {
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

    if (Win32State.HasDebugger)
    {
        OutputDebugStringA(Buff);
    }
    else 
    {
        fputs(Buff, stdout);
    }
    va_end(ArgList);
}

CBuffer LoadEntireFile(const char* Path)
{
    CBuffer Result;

    HANDLE File = CreateFileA(Path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (File != INVALID_HANDLE_VALUE)
    {
        LARGE_INTEGER FileSize;
        if (GetFileSizeEx(File, &FileSize))
        {
            Result.Size = (u64)FileSize.QuadPart;
            if (Result.Size <= 0xFFFFFFFF)
            {
                Result.Data = new u8[Result.Size];

                DWORD BytesRead;
                BOOL ReadResult = ReadFile(File, Result.Data, (u32)Result.Size, &BytesRead, nullptr);
                if (!ReadResult || (BytesRead != Result.Size))
                {
                    delete[] Result.Data;
                    Result.Data = nullptr;
                    Result.Size = 0;
                }
            }
            else 
            {
                // TODO
            }
        }
        CloseHandle(File);
    }

    return Result;
}

bool WriteEntireFile(const char* Path, u64 Size, const void* Data)
{
    bool Result = false;

    HANDLE File = CreateFileA(Path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (File != INVALID_HANDLE_VALUE)
    {
        assert(Size <= 0xFFFFFFFF);

        DWORD BytesWritten;
        if (WriteFile(File, Data, (DWORD)Size, &BytesWritten, nullptr))
        {
            Result = (BytesWritten == Size);
        }

        CloseHandle(File);
    }

    return Result;
}

static LRESULT CALLBACK MainWindowProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
    LRESULT Result = 0;
    switch (Message)
    {
        case WM_DESTROY:
        {
            PostQuitMessage(0);
        } break;
        case WM_SIZE:
        {
            if (Window == Win32State.Window)
            {
                Win32State.WasWindowResized = TRUE;
                if (WParam == SIZE_MINIMIZED)
                {
                    Win32State.IsMinimized = TRUE;
                }
                else 
                {
                    Win32State.IsMinimized = FALSE;
                }
            }
        } break;
        case WM_KILLFOCUS:
        {
            if (Window == Win32State.Window)
            {
                if (Win32State.IsCursorDisabled)
                {
                    ToggleCursor();
                }
            }
        } break;
        case WM_ACTIVATE:
        {
            if ((Window == Win32State.Window) && (WParam == WA_INACTIVE))
            {
                if (Win32State.IsCursorDisabled)
                {
                    ToggleCursor();
                }
            }
        } break;
        case WM_ERASEBKGND:
        {
            Result = 1;
        } break;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_MOUSEMOVE:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
        case WM_SYSCHAR:
        {
            /* Ignored message */
        } break;
        case WM_KEYDOWN:
        case WM_KEYUP:
        {
            bool IsDown = !(LParam & (1 << 31));
            if (IsDown && (WParam == VK_F11))
            {
                win32_ToggleFullscreen();
            }
        } break;
        default:
        {
            Result = DefWindowProcA(Window, Message, WParam, LParam);
        } break;
    }
    return Result;
}

static bool win32_ProcessInput(game_input* Input)
{
    // Clear non-sticky input
    Input->MouseDelta = {};
    Input->EscapePressed = false;
    Input->BacktickPressed = false;

    // NOTE(boti): Don't use multiple loops to remove only a certain range of messages, 
    //             See: https://docs.microsoft.com/en-us/troubleshoot/windows/win32/application-using-message-filters-unresponsive-win10
    MSG Message = {};
    while (PeekMessageA(&Message, nullptr, 0, 0, PM_REMOVE))
    {
        switch (Message.message)
        {
            case WM_INPUT:
            {
                if (Message.wParam == RIM_INPUT)
                {
                    HRAWINPUT Handle = (HRAWINPUT)Message.lParam;

                    RAWINPUT RawInput = {};

                    UINT Size = sizeof(RawInput);
                    if (GetRawInputData(Handle, RID_INPUT, &RawInput, &Size, sizeof(RawInput.header)) <= sizeof(RawInput))
                    {
                        if (RawInput.header.dwType == RIM_TYPEMOUSE)
                        {
                            const RAWMOUSE* Mouse = &RawInput.data.mouse;
                            vec2 Delta = { (f32)Mouse->lLastX, (f32)Mouse->lLastY };
                            Input->MouseDelta += Delta;
                        }
                    }
                    else
                    {
                        // TODO
                    }

                    DefWindowProcA(Message.hwnd, Message.message, Message.wParam, Message.lParam);
                }
            } break; 
            case WM_SYSKEYDOWN:
            case WM_SYSKEYUP:
            case WM_KEYDOWN:
            case WM_KEYUP:
            {
                bool IsDown = (Message.lParam & (1 << 31)) == 0;
                bool WasDown = (Message.lParam & (1 << 30)) != 0;
                switch (Message.wParam)
                {
                    case 'W': Input->Forward = IsDown; break;
                    case 'S': Input->Back = IsDown; break;
                    case 'D': Input->Right = IsDown; break;
                    case 'A': Input->Left = IsDown; break;
                    case VK_SPACE: Input->Space = IsDown; break;
                    case VK_SHIFT:
                    case VK_LSHIFT: Input->LeftShift = IsDown; break;
                    case VK_CONTROL:
                    case VK_LCONTROL: Input->LeftControl = IsDown; break;
                    case VK_MENU:
                    case VK_LMENU: Input->LeftAlt = IsDown; break;
                    case VK_ESCAPE:
                    {
                        if (IsDown && !WasDown)
                        {
                            Input->EscapePressed = true;
                        }
                    } break;
                    case VK_OEM_3:
                    {
                        if (IsDown && !WasDown)
                        {
                            Input->BacktickPressed = true;
                        }
                    } break;
                }
            } break;
            case WM_LBUTTONDOWN:
            {
                Input->MouseButtons[MOUSE_LEFT] = true;
                SetCapture(Win32State.Window);
            } break;
            case WM_LBUTTONUP:
            {
                Input->MouseButtons[MOUSE_LEFT] = false;
                ReleaseCapture();
            } break;
            case WM_RBUTTONDOWN:
            {
                Input->MouseButtons[MOUSE_RIGHT] = true;
                SetCapture(Win32State.Window);
            } break;
            case WM_RBUTTONUP:
            {
                Input->MouseButtons[MOUSE_RIGHT] = false;
                ReleaseCapture();
            } break;
            case WM_MBUTTONDOWN:
            {
                Input->MouseButtons[MOUSE_MIDDLE] = true;
                SetCapture(Win32State.Window);
            } break;
            case WM_MBUTTONUP:
            {
                Input->MouseButtons[MOUSE_MIDDLE] = false;
                ReleaseCapture();
            } break;
            case WM_MOUSEMOVE:
            {
                vec2i P = { GET_X_LPARAM(Message.lParam), GET_Y_LPARAM(Message.lParam) };
                Input->MouseP = (vec2)P;
            } break;

            case WM_QUIT: return true;
            default:
            {
                TranslateMessage(&Message);
                DispatchMessageA(&Message);
            } break;
        }

        if (WM_KEYFIRST <= Message.message && Message.message <= WM_KEYLAST)
        {
            TranslateMessage(&Message);
            DispatchMessageA(&Message);
        }
    }
    return false;
}

// Put these into global memory so we don't blow out the stack
static renderer Renderer;
static game_state GameState;

int CALLBACK WinMain(HINSTANCE Instance, HINSTANCE PrevInstance, LPSTR CommandLine, int CommandShow)
{
    Win32State.Instance = Instance;
    Win32State.HasDebugger = IsDebuggerPresent();
    QueryPerformanceFrequency((LARGE_INTEGER*)&Win32State.PerformanceFrequency);
    
#if DEVELOPER
    // Init debug cmd prompt
    if (!Win32State.HasDebugger)
    {
        AllocConsole();
        AttachConsole(GetCurrentProcessId());

        HANDLE StdOutHandle = GetStdHandle(STD_OUTPUT_HANDLE);
        
        int ConsoleID = _open_osfhandle((intptr_t)StdOutHandle, _O_TEXT);
        FILE* StdOut = _fdopen(ConsoleID, "wb");
        
        freopen_s(&StdOut, "CONOUT$", "wb", stdout);
    }
#endif
    
    WNDCLASSA WindowClass = 
    {
        .style = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc = &MainWindowProc,
        .hInstance = Instance,
        .hCursor = LoadCursorA(nullptr, IDC_ARROW),
        .lpszClassName = Win32_ClassName,
    };
    
    if (!RegisterClassA(&WindowClass)) 
    {
        return -1;
    }
    
    Win32State.WindowStyle = WS_OVERLAPPEDWINDOW;
    Win32State.Window = CreateWindowA(Win32_ClassName, Win32_WindowTitle,
                                      Win32State.WindowStyle,
                                      CW_USEDEFAULT, CW_USEDEFAULT,
                                      CW_USEDEFAULT, CW_USEDEFAULT,
                                      nullptr, nullptr, Win32State.Instance, nullptr);
    
    if (!Win32State.Window) 
    {
        return -1;
    }
    
    ShowWindow(Win32State.Window, SW_SHOW);
    
    // Init raw input
    {
        RAWINPUTDEVICE Devices[] = 
        {
            // Mouse
            {
                .usUsagePage = HID_USAGE_PAGE_GENERIC,
                .usUsage = HID_USAGE_GENERIC_MOUSE,
                .dwFlags = RIDEV_INPUTSINK,
                .hwndTarget = Win32State.Window,
            },
        };
        constexpr u32 DeviceCount = CountOf(Devices);

        BOOL Result = RegisterRawInputDevices(Devices, DeviceCount, sizeof(RAWINPUTDEVICE));
        if (!Result)
        {
            return -1;
        }
    }

    GameState.Renderer = &Renderer;

    // Allocate memory
    {
        u64 ChunkHeadersSize = (u64)world::MaxChunkCount * sizeof(chunk);
        GameState.World.Chunks = (chunk*)VirtualAlloc(nullptr, ChunkHeadersSize, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        if (!GameState.World.Chunks)
        {
            DWORD ErrorCode = GetLastError();
            DebugPrint("Memory allocation failed: %x\n", ErrorCode);
            return -1;
        }

        // TODO: large pages
        u64 ChunkDataSize = (u64)world::MaxChunkCount * sizeof(chunk_data);
        GameState.World.ChunkData = (chunk_data*)VirtualAlloc(nullptr, ChunkDataSize, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        if (!GameState.World.ChunkData)
        {
            DWORD ErrorCode = GetLastError();
            DebugPrint("Memory allocation failed: %x\n", ErrorCode);
            return -1;
        }
    }

    if(!Game_Initialize(&GameState))
    {
        return -1;
    }

    game_input Input = {};

    f32 DeltaTime = 0.0f;
    f32 Time = 0.0f;
    u64 FrameCount = 0;
    bool IsRunning = true;
    while (IsRunning) 
    {
        //DebugPrint("%llu. Frame started\n", FrameCount);
        Time += DeltaTime;

        s64 StartTime;
        QueryPerformanceCounter((LARGE_INTEGER*)&StartTime);

#if DEVELOPER && 0
        if (IsHungAppWindow(Win32State.Window))
        {
            assert(!"App hung");
        }
#endif
        // Show frame time in window title
        {
            constexpr size_t BuffSize = 256;
            char Buff[BuffSize];

            snprintf(Buff, BuffSize, "%s [%.2fms | %.1fFPS]",
                     Win32_WindowTitle,
                     1000.0f * DeltaTime,
                     1.0f / DeltaTime);

            SetWindowTextA(Win32State.Window, Buff);
        }

        if (win32_ProcessInput(&Input))
        {
            IsRunning = false;
            break;
        }

        if (Win32State.WasWindowResized)
        {
            GameState.NeedRendererResize = true;
            Win32State.WasWindowResized = FALSE;
        }
        Input.IsCursorEnabled = !Win32State.IsCursorDisabled;
        GameState.IsMinimized = Win32State.IsMinimized;

        GameState.FrameIndex = FrameCount;
        Game_UpdateAndRender(&GameState, &Input, DeltaTime);

        // Since there's no rendering when we're minimzed we don't want to be burning the CPU
        if (Win32State.IsMinimized)
        {
            Sleep(20);
        }

        GlobalProfiler.Reset();

        s64 EndTime;
        QueryPerformanceCounter((LARGE_INTEGER*)&EndTime);

        DeltaTime = (EndTime - StartTime) / (f32)Win32State.PerformanceFrequency;
        //DebugPrint("%llu. Frame ended\n", FrameCount);
        FrameCount++;
    }
    
    if (Win32State.IsCursorDisabled)
    {
        ClipCursor(nullptr);
        ShowCursor(TRUE);
    }

    return 0;
}