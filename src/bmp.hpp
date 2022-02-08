#pragma once
#include <Common.hpp>

constexpr u16 BMP_FILE_TAG = 'MB';

enum bmp_compression_type : u32
{
    BMP_COMPRESSION_NONE = 0,
};

#pragma pack(push, 1)

struct bmp_file_header
{
    u16 Tag;
    u32 FileSize;
    u16 Reserved1;
    u16 Reserved2;
    u32 Offset;
};

struct bmp_info_header
{
    u32 HeaderSize;
    s32 Width;
    s32 Height;
    u16 Planes;
    u16 BitCount;
    u32 Compression;
    u32 ImageSize;
    s32 PixelsPerMeterX;
    s32 PixelsPerMeterY;
    u32 ClrUsed;
    u32 ClrImportant;
};

struct bmp_file
{
    bmp_file_header File;
    bmp_info_header Info;
    u8 Data[];
};

#pragma pack(pop)