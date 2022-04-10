@echo off

IF NOT EXIST build mkdir build
cd build
del /q *
cd ..

set Warnings=-W4 -WX -wd4201 -wd4100 -wd4189 -wd4200 -wd4505
set CommonOptions=-MT -Zi -EHsc -fp:fast -GT -Isrc/ -I%VULKAN_SDK%/Include/ -std:c++20 -arch:AVX2 -Oi -O2
set CommonDefines=-DDEVELOPER=1 -DPLATFORM_WIN32 -DWIN32_LEAN_AND_MEAN -DNOMINMAX
set CompilerOptions=%CommonOptions% %CommonDefines% -c
set LinkerOptions=-LIBPATH:%VULKAN_SDK%/Lib/ -LIBPATH:lib/ kernel32.lib user32.lib gdi32.lib vulkan-1.lib imgui.lib /DEBUG

cl -nologo %CompilerOptions% %Warnings% "src/Win32_Main.cpp" /Fobuild/win32_platform.obj /Fdbuild/win32_platform.pdb
cl -nologo %CompilerOptions% %Warnings% "src/Game.cpp" /Fobuild/game.obj /Fdbuild/game.pdb
link -nologo %LinkerOptions% /OUT:build/blokker.exe build/win32_platform.obj build/game.obj