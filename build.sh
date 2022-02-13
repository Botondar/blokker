#!/bin/bash
BuildPath="./build"
mkdir -p $BuildPath
mkdir -p shader
mkdir -p $BuildPath/obj

Warnings=""
CommonOptions="-Isrc/ -std=c++20 -march=x86-64-v3 -Oi -O2 -g -lX11 -lXfixes -lvulkan"
CommonDefines="-DDEVELOPER=1 -DPLATFORM_LINUX=1"
CompilerOptions="$CommonOptions $CommonDefines -o build/build"
ClangArgs="$CompilerOptions $Warnings"
Files="src/X11_Main.cpp src/Game.cpp"

clang++ $ClangArgs $Files

mkdir -p shader

ShaderCompilerOptions="--target-env=vulkan1.2 -std=450core -I ./src/shader/ -O"

glslc $ShaderCompilerOptions -fshader-stage=vert -o shader/shader.vs -DVERTEX_SHADER=1 src/shader/shader.glsl
glslc $ShaderCompilerOptions -fshader-stage=frag -o shader/shader.fs -DFRAGMENT_SHADER=1 src/shader/shader.glsl

glslc $ShaderCompilerOptions -fshader-stage=vert -o shader/imshader.vs -DVERTEX_SHADER=1 src/shader/imshader.glsl
glslc $ShaderCompilerOptions -fshader-stage=frag -o shader/imshader.fs -DFRAGMENT_SHADER=1 src/shader/imshader.glsl
