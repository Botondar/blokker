#include "Common.hpp"

CBuffer::CBuffer() : Size(0), Data(nullptr)
{
}

CBuffer::~CBuffer()
{
    delete[] Data;
}

CBuffer::CBuffer(CBuffer&& Other) :
    Size(Other.Size),
    Data(Other.Data)
{
    Other.Size = 0;
    Other.Data = nullptr;
}
