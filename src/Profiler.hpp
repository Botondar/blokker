#pragma once
#include <Common.hpp>

// ==== HELPERS ====
#define STRINGIFY(a) #a
#define CONCAT(a, b) a##b

#define LINE_STR_(line) STRINGIFY(line)
#define LINE_STR LINE_STR_(__LINE__)
#define TIMED_BLOCK_(line, ...) timed_block CONCAT(Timed_Block, line)(__FUNCTION__, ":" __VA_ARGS__ ":" LINE_STR)
// ==== END HELPERS====

#define TIMED_FUNCTION() timed_block Timed_Function(__FUNCTION__, nullptr)
#define TIMED_BLOCK(...) TIMED_BLOCK_(__LINE__, __VA_ARGS__)

struct profiler_entry
{
    const char* Name;
    const char* Extra;
    s64 CounterSum;
    s64 CallCount;

    static constexpr u32 MaxChildrenCount = 1024;
    u32 ChildrenCount;
    u32 Children[MaxChildrenCount];
};

struct frame_statistics 
{
    static constexpr u32 MaxStackCount = 512;
    u32 StackAt = 0;
    u32 EntryStack[MaxStackCount];

    static constexpr u32 MaxEntryCount = 4096;
    u32 EntryPoolAt = 0;
    profiler_entry EntryPool[MaxEntryCount];
};

struct profiler
{
    static constexpr u32 MaxStatBufferCount = 16;
    u64 CurrentFrameIndex = 0;
    u32 StatsBufferAt = 0;
    frame_statistics StatsBuffer[MaxStatBufferCount];

    void Reset();
    void Begin(const char* Name, const char* Extra);
    void End(const char* Name, const char* Extra, s64 Time);

    const frame_statistics* GetPrevFrameStats() const;

    void DoGUI();

    // NOTE: prints _previous_ frame
    void Print(f32 MinTime = 0.0f) const;

private:
    u32 Push();

    void PrintFrom(
        const frame_statistics* Stats, 
        const profiler_entry* Entry, 
        f32 ParentTime, 
        char* Padding, u32& PaddingAt) const;

    void DrawEntry(
        const frame_statistics* Stats, 
        const profiler_entry* Entry, 
        f32 ParentTime) const;
};

struct timed_block
{
    const char* Name;
    const char* Extra;
    s64 StartCounter;
    s64 EndCounter;

    timed_block(const char* Name, const char* Extra);
    ~timed_block();
};

extern profiler GlobalProfiler;