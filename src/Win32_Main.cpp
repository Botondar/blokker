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

#include <Renderer/Renderer.hpp>
#include <Profiler.hpp>

static const char* Win32_ClassName = "wndclass_blokker";
static const char* Win32_WindowTitle = "Blokker";
static const char* Win32_GameDLLPath = "build/game.dll";
static const char* Win32_GameDLLTempPath = "build/game-temp.dll";

struct platform_work_queue
{
    static constexpr u32 MaxWorkCount = 512;

    HANDLE Semaphore;

    // TODO(boti): u64 Completion
    volatile u32 Completion;
    volatile u32 CompletionGoal;
    volatile u32 ReadIndex;
    volatile u32 WriteIndex;
    work_function WorkQueue[MaxWorkCount];
};

struct win32_state
{
    HINSTANCE Instance;
    HWND Window;

    HMODULE GameDLL;
    update_and_render_func* Game_UpdateAndRender;

    WINDOWPLACEMENT PrevWindowPlacement = { sizeof(WINDOWPLACEMENT) };
    DWORD WindowStyle;
    BOOL WasWindowResized;
    BOOL IsMinimized;
    BOOL IsFullscreen;
    
    s64 PerformanceFrequency;

    BOOL HasDebugger;
    bool IsCursorDisabled;

    HANDLE WorkerSemaphore;
    platform_work_queue HighPriorityQueue;
    platform_work_queue LowPriorityQueue;
};
static win32_state Win32State;

static void WinDebugPrint(const char* Format, ...);

static DWORD __stdcall WinWorkerThread(void* Param)
{
    HANDLE Semaphore = Win32State.WorkerSemaphore;
    platform_work_queue* HighPriorityQueue = &Win32State.HighPriorityQueue;
    platform_work_queue* LowPriorityQueue = &Win32State.LowPriorityQueue;

    memory_arena Arena = {};
    Arena.Size = MiB(32);
    Arena.Base = (u8*)VirtualAlloc(nullptr, Arena.Size, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
    if (!Arena.Base)
    {
        WinDebugPrint("Failed to allocate thread memory");
        ExitProcess(1);
    }

    for (;;)
    {
        // NOTE(boti): The control flow is ugly here, we only want to move onto the low priority queue
        //             when we know the high priority queue is empty, and we only want to sleep
        //             when both queues are empty.
        //             This means that we want to restart the loop each time there is/was work in whichever queue.
        u32 ReadIndex = HighPriorityQueue->ReadIndex;
        if (ReadIndex < HighPriorityQueue->WriteIndex)
        {
            function Work = HighPriorityQueue->WorkQueue[ReadIndex % HighPriorityQueue->MaxWorkCount];
            if (AtomicCompareExchange(&HighPriorityQueue->ReadIndex, ReadIndex + 1, ReadIndex) == ReadIndex)
            {
                Work.Invoke(&Arena);
                AtomicIncrement(&HighPriorityQueue->Completion);
                Arena.Used = 0;
            }
            continue;
        }
        ReadIndex = LowPriorityQueue->ReadIndex;
        if (ReadIndex < LowPriorityQueue->WriteIndex)
        {
            function Work = LowPriorityQueue->WorkQueue[ReadIndex % LowPriorityQueue->MaxWorkCount];
            if (AtomicCompareExchange(&LowPriorityQueue->ReadIndex, ReadIndex + 1, ReadIndex) == ReadIndex)
            {
                Work.Invoke(&Arena);
                AtomicIncrement(&LowPriorityQueue->Completion);
                Arena.Used = 0;
            }

            continue;
        }

        WaitForSingleObject(Semaphore, INFINITE);
    }
    //return(0);
}

static void WinAddWork(platform_work_queue* Queue, work_function Work)
{
    u32 WriteIndex = Queue->WriteIndex;
    while (WriteIndex - Queue->ReadIndex >= Queue->MaxWorkCount)
    {
        SpinWait;
    }

    Queue->WorkQueue[WriteIndex % Queue->MaxWorkCount] = Work;
    u32 PrevIndex = AtomicCompareExchange(&Queue->WriteIndex, WriteIndex + 1, WriteIndex);
    assert(PrevIndex == WriteIndex);

    AtomicIncrement(&Queue->CompletionGoal);
    ReleaseSemaphore(Win32State.WorkerSemaphore, 1, nullptr);
}

static void WinWaitForAllWork(platform_work_queue* Queue)
{
    while (Queue->Completion != Queue->CompletionGoal)
    {
        SpinWait;
    }
}

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

static bool WinToggleCursor()
{
    Win32State.IsCursorDisabled = !Win32State.IsCursorDisabled;
    SetClipCursorToWindow(Win32State.IsCursorDisabled);
    ShowCursor(!Win32State.IsCursorDisabled);
    return !Win32State.IsCursorDisabled;
}

static s64 WinGetPerformanceCounter()
{
    s64 Result;
    QueryPerformanceCounter((LARGE_INTEGER*)&Result);
    return Result;
}

static f32 WinGetElapsedTime(s64 Start, s64 End)
{
    f32 Result = (End - Start) / (f32)Win32State.PerformanceFrequency;
    return Result;
}

static f32 WinGetTimeFromCounter(s64 Counter)
{
    f32 Result = Counter / (f32)Win32State.PerformanceFrequency;
    return Result;
}

static void* WinImGuiAlloc(size_t Size, void* User)
{
    void* Result = HeapAlloc(GetProcessHeap(), 0, Size);
    return(Result);
}

static void WinImGuiFree(void* Address, void* User)
{
    HeapFree(GetProcessHeap(), 0, Address);
}

static bool WinToggleFullscreen()
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

static VkSurfaceKHR WinCreateVulkanSurface(VkInstance vkInstance)
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

static void WinDebugPrint(const char* Format, ...)
{
    constexpr size_t BufferSize = 768;
    char Buff[BufferSize];

    va_list ArgList;
    va_start(ArgList, Format);

    vsnprintf(Buff, BufferSize, Format, ArgList);

    va_end(ArgList);

    if (Win32State.HasDebugger)
    {
        OutputDebugStringA(Buff);
    }
    else 
    {
        fputs(Buff, stdout);
    }
}

static void WinLogMsg(const char* Function, int Line, const char* Format, ...)
{
    constexpr size_t BufferSize = 768;
    char Message[BufferSize];
    char Output[BufferSize];

    va_list ArgList;
    va_start(ArgList, Format);

    vsnprintf(Message, BufferSize, Format, ArgList);

    va_end(ArgList);

    snprintf(Output, BufferSize, "%s:%d: %s\n", Function, Line, Message);

    if (Win32State.HasDebugger)
    {
        OutputDebugStringA(Output);
    }
    else
    {
        fputs(Output, stdout);
    }
}

static buffer WinLoadEntireFile(const char* Path, memory_arena* Arena)
{
    buffer Buffer = {};

    HANDLE File = CreateFileA(Path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (File != INVALID_HANDLE_VALUE)
    {
        LARGE_INTEGER FileSize;
        if (GetFileSizeEx(File, &FileSize))
        {
            assert(FileSize.QuadPart <= 0xFFFFFFFFll);
            
            u64 Save = Arena->Used;
            u64 Size = (u64)FileSize.QuadPart;
            void* Data = PushSize(Arena, Size, 64);
            if (Data)
            {
                DWORD BytesRead;
                if (ReadFile(File, Data, (u32)Size, &BytesRead, nullptr))
                {
                    Buffer.Size = Size;
                    Buffer.Data = (u8*)Data;
                }
                else
                {
                    Arena->Used = Save;
                }
            }
        }
        CloseHandle(File);
    }

    return(Buffer);
}

static LRESULT CALLBACK WinMainWindowProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
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
                    WinToggleCursor();
                }
            }
        } break;
        case WM_ACTIVATE:
        {
            if ((Window == Win32State.Window) && (WParam == WA_INACTIVE))
            {
                if (Win32State.IsCursorDisabled)
                {
                    WinToggleCursor();
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
                WinToggleFullscreen();
            }
        } break;
        default:
        {
            Result = DefWindowProcA(Window, Message, WParam, LParam);
        } break;
    }
    return Result;
}

static bool win32_ProcessInput(game_io* Input)
{
    // Clear non-sticky input
    Input->MouseDelta = {};
    Input->WheelDelta = 0.0f;
    Input->EscapePressed = false;
    Input->BacktickPressed = false;
    Input->MPressed = false;

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
                    case 'M':
                    {
                        if (IsDown && !WasDown)
                        {
                            Input->MPressed = true;
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
            case WM_MOUSEWHEEL:
            {
                short iWheelDelta = GET_WHEEL_DELTA_WPARAM(Message.wParam);
                Input->WheelDelta = (f32)iWheelDelta / (f32)WHEEL_DELTA;
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

    static constexpr u32 WorkerCount = 3;
    Win32State.WorkerSemaphore = CreateSemaphoreA(nullptr, 0, WorkerCount, nullptr);
    if (!Win32State.WorkerSemaphore || Win32State.WorkerSemaphore == INVALID_HANDLE_VALUE)
    {
        return -1;
    }
    for (u32 ThreadIndex = 0; ThreadIndex < WorkerCount; ThreadIndex++)
    {
        DWORD WorkerID;
        HANDLE WorkerHandle = CreateThread(nullptr, 0, &WinWorkerThread, nullptr, 0, &WorkerID);
    }

    WNDCLASSA WindowClass = 
    {
        .style = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc = &WinMainWindowProc,
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

    HANDLE GameDLLFile = CreateFileA(Win32_GameDLLPath, 0, FILE_SHARE_READ|FILE_SHARE_WRITE, nullptr, 
                                     OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    FILETIME GameDLLWriteTime = {};
    {
        BY_HANDLE_FILE_INFORMATION Info = {};
        if (GetFileInformationByHandle(GameDLLFile, &Info))
        {
            GameDLLWriteTime = Info.ftLastWriteTime;
        }
        else
        {
            Assert(!"Couldn't retrieve game DLL file info");
        }
    }

    CopyFile(Win32_GameDLLPath, Win32_GameDLLTempPath, FALSE);

    Win32State.GameDLL = LoadLibraryA(Win32_GameDLLTempPath);
    if (Win32State.GameDLL)
    {
        Win32State.Game_UpdateAndRender = (update_and_render_func*)GetProcAddress(Win32State.GameDLL, "Game_UpdateAndRender");
        if (!Win32State.Game_UpdateAndRender)
        {
            return -1;
        }
    }
    else
    {
        return -1;
    }

    game_memory Memory = {};
    {
        Memory.MemorySize = GiB(4);
        Memory.Memory = VirtualAlloc(nullptr, Memory.MemorySize, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);

        if (!Memory.Memory)
        {
            return -1;
        }

        Memory.Platform.AddWork = &WinAddWork;
        Memory.Platform.WaitForAllWork = &WinWaitForAllWork;
        Memory.Platform.DebugPrint = &WinDebugPrint;
        Memory.Platform.LogMsg = &WinLogMsg;
        Memory.Platform.LoadEntireFile = &WinLoadEntireFile;
        Memory.Platform.CreateVulkanSurface = &WinCreateVulkanSurface;
        Memory.Platform.ToggleCursor = &WinToggleCursor;
        Memory.Platform.GetPerformanceCounter = &WinGetPerformanceCounter;
        Memory.Platform.GetElapsedTime = &WinGetElapsedTime;
        Memory.Platform.GetTimeFromCounter = &WinGetTimeFromCounter;

        Memory.Platform.HighPriorityQueue = &Win32State.HighPriorityQueue;
        Memory.Platform.LowPriorityQueue = &Win32State.LowPriorityQueue;

        Memory.ImGuiAlloc = &WinImGuiAlloc;
        Memory.ImGuiFree = &WinImGuiFree;

        ImGui::SetAllocatorFunctions(Memory.ImGuiAlloc, Memory.ImGuiFree, nullptr);
        Memory.ImGuiCtx = ImGui::CreateContext();
    }

    game_io IO = {};

    f32 Time = 0.0f;
    u64 FrameCount = 0;
    bool IsRunning = true;
    while (IsRunning) 
    {
        // DLL reload
        {
            BY_HANDLE_FILE_INFORMATION Info = {};
            if (GetFileInformationByHandle(GameDLLFile, &Info))
            {
                u64 OldWriteTime = ((u64)GameDLLWriteTime.dwHighDateTime << 32) | GameDLLWriteTime.dwLowDateTime;
                u64 CurrentWriteTime = ((u64)Info.ftLastWriteTime.dwHighDateTime << 32) | Info.ftLastWriteTime.dwLowDateTime;
                if (CurrentWriteTime > OldWriteTime)
                {
                    WinWaitForAllWork(&Win32State.HighPriorityQueue);
                    WinWaitForAllWork(&Win32State.LowPriorityQueue);

                    GameDLLWriteTime = Info.ftLastWriteTime;

                    FreeLibrary(Win32State.GameDLL);
                    Win32State.GameDLL = nullptr;
                    Win32State.Game_UpdateAndRender = nullptr;

                    CopyFile(Win32_GameDLLPath, Win32_GameDLLTempPath, FALSE);

                    Win32State.GameDLL = LoadLibraryA(Win32_GameDLLTempPath);
                    if (Win32State.GameDLL)
                    {
                        Win32State.Game_UpdateAndRender = (update_and_render_func*)GetProcAddress(Win32State.GameDLL, "Game_UpdateAndRender");
                        if (!Win32State.Game_UpdateAndRender)
                        {
                            Assert(!"Couldn't load game dll function");
                            return -1;
                        }
                    }
                    else
                    {
                        Assert(!"Couldn't reload game DLL");
                        return -1;
                    }
                }
            }
            else
            {
                Assert(!"Couldn't retrieve game DLL file info");
            }
        }

        s64 StartTime;
        QueryPerformanceCounter((LARGE_INTEGER*)&StartTime);

        Time += IO.DeltaTime;

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
                     1000.0f * IO.DeltaTime,
                     1.0f / IO.DeltaTime);

            SetWindowTextA(Win32State.Window, Buff);
        }

        if (win32_ProcessInput(&IO))
        {
            IsRunning = false;
            break;
        }

        if (Win32State.WasWindowResized)
        {
            IO.NeedRendererResize = true;
            Win32State.WasWindowResized = FALSE;
        }
        IO.IsCursorEnabled = !Win32State.IsCursorDisabled;
        IO.IsMinimized = Win32State.IsMinimized;

        Win32State.Game_UpdateAndRender(&Memory, &IO);

        // Since there's no rendering when we're minimzed we don't want to be burning the CPU
        if (Win32State.IsMinimized)
        {
            Sleep(5);
        }

        //GlobalProfiler.Reset();

        s64 EndTime;
        QueryPerformanceCounter((LARGE_INTEGER*)&EndTime);

        IO.DeltaTime = (EndTime - StartTime) / (f32)Win32State.PerformanceFrequency;
        //DebugPrint("%llu. Frame ended\n", FrameCount);
        IO.FrameIndex++;
    }
    
    if (Win32State.IsCursorDisabled)
    {
        ClipCursor(nullptr);
        ShowCursor(TRUE);
    }

    WinWaitForAllWork(&Win32State.HighPriorityQueue);
    WinWaitForAllWork(&Win32State.LowPriorityQueue);
    FreeLibrary(Win32State.GameDLL);
    DeleteFile(Win32_GameDLLTempPath);
    return 0;
}