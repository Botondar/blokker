#include "Profiler.hpp"

#include <Platform.hpp>
#include <Math.hpp>

#include <cassert>
#include <cstring>

profiler GlobalProfiler;

void profiler::Reset()
{
    StatsBufferAt = (StatsBufferAt + 1) % MaxStatBufferCount;
    frame_statistics* CurrentStats = StatsBuffer + StatsBufferAt;
    memset(CurrentStats, 0, sizeof(frame_statistics));
    CurrentFrameIndex++;
}

void profiler::Begin(const char* Name, const char* Extra)
{
    assert(Name);
    frame_statistics* Stats = StatsBuffer + StatsBufferAt;

    assert(Stats->EntryPoolAt < Stats->MaxEntryCount);

    assert(Stats->StackAt < Stats->MaxStackCount);
    if (Stats->StackAt == 0)
    {
        u32 Index = Stats->EntryPoolAt++;
        profiler_entry* Entry = &Stats->EntryPool[Index];
        Entry->Name = Name;
        Entry->Extra = Extra;
        Entry->CallCount++;

        Stats->EntryStack[Stats->StackAt++] = Index;
    }
    else
    {
        u32 CurrentEntryIndex = Stats->EntryStack[Stats->StackAt - 1];
        profiler_entry* CurrentEntry = Stats->EntryPool + CurrentEntryIndex;

        // Try and find if the entry being added is already a child of an existing entry
        u32 ChildIndex = INVALID_INDEX_U32;
        profiler_entry* ChildEntry = nullptr;
        for (u32 i = 0; i < CurrentEntry->ChildrenCount; i++)
        {
            u32 Index = CurrentEntry->Children[i];
            profiler_entry* Entry = Stats->EntryPool + Index;
            if (Entry->Name == Name && Entry->Extra == Extra)
            {
                ChildIndex = Index;
                ChildEntry = Entry;
                break;
            }
        }

        if (ChildIndex == INVALID_INDEX_U32)
        {
            u32 Index = Stats->EntryPoolAt++;
            profiler_entry* Entry = &Stats->EntryPool[Index];
            Entry->Name = Name;
            Entry->Extra = Extra;
            Entry->CallCount++;

            assert(CurrentEntry->ChildrenCount < CurrentEntry->MaxChildrenCount);
            CurrentEntry->Children[CurrentEntry->ChildrenCount++] = Index;

            Stats->EntryStack[Stats->StackAt++] = Index;
        }
        else
        {
            ChildEntry->CallCount++;
            Stats->EntryStack[Stats->StackAt++] = ChildIndex;
        }
    }
}

void profiler::End(const char* Name, const char* Extra, s64 Time)
{
    assert(Name);
    frame_statistics* Stats = StatsBuffer + StatsBufferAt;

    assert(Stats->StackAt > 0);
    
    u32 EntryIndex = Stats->EntryStack[--(Stats->StackAt)];
    assert(EntryIndex < Stats->MaxEntryCount);

    profiler_entry* Entry = &Stats->EntryPool[EntryIndex];
    // NOTE: pointer comparison!
    assert(Entry->Name == Name);
    assert(Entry->Extra == Extra);

    Entry->CounterSum += Time;
}

const frame_statistics* profiler::GetPrevFrameStats() const
{
    const frame_statistics* Stats = nullptr;
    if (CurrentFrameIndex)
    {
        Stats = StatsBuffer + ((StatsBufferAt - 1) % MaxStatBufferCount);
    }
    return Stats;
}

void profiler::PrintFrom(const frame_statistics* Stats, const profiler_entry* Entry, f32 ParentTime, char* Padding, u32& PaddingAt) const
{
    constexpr size_t PaddingMax = 64;

    f32 Time = GetTimeFromCounter(Entry->CounterSum);
    u64 ExtraLength = Entry->Extra ? strlen(Entry->Extra) : 0;
    DebugPrint("%.*s%s%.*s: || %.2fms (%.1f%%) || x%lli\n",
        PaddingAt, Padding, 
        Entry->Name, ExtraLength, Entry->Extra,
        1000.0f * Time, 100.0f * Time / ParentTime, Entry->CallCount);

    assert(PaddingAt < PaddingMax);
    Padding[PaddingAt++] = '-';
    for (u32 i = 0; i < Entry->ChildrenCount; i++)
    {
        const profiler_entry* Child = Stats->EntryPool + Entry->Children[i];
        PrintFrom(Stats, Child, Time, Padding, PaddingAt);
    }
    Padding[--PaddingAt] = '\0';
}

void profiler::Print(f32 MinTime) const
{
    if (CurrentFrameIndex)
    {
        const frame_statistics* Stats = GetPrevFrameStats();
        // If there have been entries allocated we know that the one at the 
        // beginning of the stack is the root
        if (Stats->EntryPoolAt)
        {
            constexpr size_t PaddingMax = 64;
            u32 PaddingAt = 0;
            char Padding[PaddingMax];
            memset(Padding, '-', PaddingMax);

            const profiler_entry* Root = Stats->EntryPool + Stats->EntryStack[0];
            f32 RootTime = GetTimeFromCounter(Root->CounterSum); // Special case for root so we get 100% time
            if (RootTime >= MinTime)
            {
                DebugPrint("======================\n");
                PrintFrom(Stats, Root, RootTime, Padding, PaddingAt);
            }
        }
        else
        {
            assert(!"Empty profiler print attempt");
        }
    }
}

timed_block::timed_block(const char* Name, const char* Extra) : Name(Name), Extra(Extra)
{
    StartCounter = GetPerformanceCounter();
    GlobalProfiler.Begin(Name, Extra);
}

timed_block::~timed_block()
{
    EndCounter = GetPerformanceCounter();
    GlobalProfiler.End(Name, Extra, EndCounter - StartCounter);
}