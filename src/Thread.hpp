#pragma once

#include <Platform.hpp>
#include <Memory.hpp>

// TODO(boti): This is part of the platform layer
//             Refactor !!!

struct thread_context
{
    bump_allocator BumpAllocator;
};

extern thread_context* Platform_GetThreadContext();