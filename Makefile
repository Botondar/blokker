# Flags
LANG = -std:c++20 -arch:AVX2 -MT -EHsc
WARNINGS = -W4 -WX -wd4201 -wd4100 -wd4189 -wd4200 -wd4505
DEFINES = -DDEVELOPER=1 -DPLATFORM_WIN32=1 -DWIN32_LEAN_AND_MEAN=1 -DNOMINMAX=1
FP_ENV = -fp:strict -fp:except-
OPTIMIZATION = -Zi -O2 -Oi
MISC = -GT -Isrc/ -I$(VULKAN_SDK)/Include/
LIBS = kernel32.lib user32.lib vulkan-1.lib

SHADER_OPT = --target-env=vulkan1.2 -std=450core -I "src/shader/" -O

SHADERS = "shader/shader.vs" "shader/shader.fs" "shader/imshader.vs" "shader/imshader.fs" "shader/imguishader.vs" "shader/imguishader.fs"

all: "build" "build/win32_platform.obj" "build/game.obj" "build/imgui.lib" "build/blokker.exe" $(SHADERS)

clean:
	@del /Q "build\*.*"
	@del /Q "shader\*.*"

"build":
	@mkdir build

"build/win32_platform.obj": "src/Win32_Main.cpp" "src/Platform.hpp"
	@cl -nologo $(LANG) $(WARNINGS) $(DEFINES) $(FP_ENV) $(OPTIMIZATION) $(MISC) -c "src/Win32_Main.cpp" -Fo:$@ -Fd:"build/"
"build/game.obj": "src/*.cpp" "src/*.hpp" "src/Renderer/*.cpp" "src/Renderer/*.hpp"
	@cl -nologo $(LANG) $(WARNINGS) $(DEFINES) $(FP_ENV) $(OPTIMIZATION) $(MISC) -c "src/Game.cpp" -Fo:$@ -Fd:"build/"
"build/imgui.lib": "src/imgui/*.cpp" "src/imgui/*.h"
	@cl -nologo $(LANG) -W4 -WX -Zi -O2 -Oi -c -Fe:"build/imgui.lib" -Fo:"build/imgui.obj" -Fd:"build/" "src/imgui/build.cpp"
	@lib -nologo -OUT:$@ "build/imgui.obj"
"build/blokker.exe": "build/win32_platform.obj" "build/game.obj" "build/imgui.lib"
	@link -nologo -LIBPATH:$(VULKAN_SDK)/Lib/ -LIBPATH:lib/ -OUT:$@ $** $(LIBS)
	
# The shaders might benefit from an actual build-system...
"shader/shader.vs": "src/shader/shader.glsl"
	@echo $@
	@glslc $(SHADER_OPT) -fshader-stage=vert -o $@ -DVERTEX_SHADER=1 $**
"shader/shader.fs": "src/shader/shader.glsl"
	@echo $@
	@glslc $(SHADER_OPT) -fshader-stage=frag -o $@ -DFRAGMENT_SHADER=1 $**
	
"shader/imshader.vs": "src/shader/imshader.glsl"
	@echo $@
	@glslc $(SHADER_OPT) -fshader-stage=vert -o $@ -DVERTEX_SHADER=1 $**
"shader/imshader.fs": "src/shader/imshader.glsl"
	@echo $@
	@glslc $(SHADER_OPT) -fshader-stage=frag -o $@ -DFRAGMENT_SHADER=1 $**
	
"shader/imguishader.vs": "src/shader/imguishader.glsl"
	@echo $@
	@glslc $(SHADER_OPT) -fshader-stage=vert -o $@ -DVERTEX_SHADER=1 $**
"shader/imguishader.fs": "src/shader/imguishader.glsl"
	@echo $@
	@glslc $(SHADER_OPT) -fshader-stage=frag -o $@ -DFRAGMENT_SHADER=1 $**