@echo off

IF NOT EXIST shader mkdir shader
cd shader
del /q *
cd ..

set ShaderCompilerOptions=--target-env=vulkan1.2 -std=450core -I "src/shader/" -O 

glslc %ShaderCompilerOptions% -fshader-stage=vert -o shader/shader.vs -DVERTEX_SHADER=1 "src/shader/shader.glsl"
glslc %ShaderCompilerOptions% -fshader-stage=frag -o shader/shader.fs -DFRAGMENT_SHADER=1 "src/shader/shader.glsl"

glslc %ShaderCompilerOptions% -fshader-stage=vert -o shader/imshader.vs -DVERTEX_SHADER=1 "src/shader/imshader.glsl"
glslc %ShaderCompilerOptions% -fshader-stage=frag -o shader/imshader.fs -DFRAGMENT_SHADER=1 "src/shader/imshader.glsl"

glslc %ShaderCompilerOptions% -fshader-stage=vert -o shader/imguishader.vs -DVERTEX_SHADER=1 "src/shader/imguishader.glsl"
glslc %ShaderCompilerOptions% -fshader-stage=frag -o shader/imguishader.fs -DFRAGMENT_SHADER=1 "src/shader/imguishader.glsl"