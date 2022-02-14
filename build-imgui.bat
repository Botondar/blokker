@echo off

IF NOT EXIST lib mkdir lib
cd lib
del /q *
cd ..

set Warnings=-W4 -WX
set CommonOptions=-c -MT -Zi -EHsc -fp:fast -GT -std:c++20 -arch:AVX2 -Oi -O2
set CompilerOptions=%Warnings% %CommonOptions%

cl -nologo %CompilerOptions% "src/imgui/build.cpp" /Folib/imgui.obj /Fdlib/imgui.pdb
lib -nologo /OUT:lib/imgui.lib lib/imgui.obj
