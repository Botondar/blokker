@echo off

IF NOT EXIST build mkdir build
cd build
del /q *
cd ..

set CommonOptions=-std=c++20 -g -Oi -O3
set CommonDefines=-DDEVELOPER=1 -DPLATFORM_WIN32=1 -DWIN32_LEAN_AND_MEAN -DNOMINMAX
set Warnings=-Werror -Wno-multichar
set IncludeDirs=-Isrc/ -I%VULKAN_SDK%/Include/
set LinkerOptions=-L%VULKAN_SDK%/Lib/ -lvulkan-1 -lkernel32 -luser32 
set Options=%CommonOptions% %CommonDefines% %Warnings% %IncludeDirs% %LinkerOptions%

clang %Options% "src/Win32_Main.cpp" "src/Game.cpp" -obuild/blokker.exe 
