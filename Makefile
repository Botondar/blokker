# Timestamp for pdb
!if [echo %DATE%_%TIME% > datetime.tmp] == 0
DATETIME = \
!include datetime.tmp
DATETIME = $(DATETIME:.=)
DATETIME = $(DATETIME::=)
DATETIME = $(DATETIME:-=)
DATETIME = $(DATETIME: =0)
!if [del datetime.tmp]
!endif
!endif

# Cpp
LANG = -std:c++20 -arch:AVX2 -MT -EHsc
WARNINGS = -W4 -WX -wd4201 -wd4100 -wd4189 -wd4200 -wd4505
DEFINES = -DDEVELOPER=1 -DPLATFORM_WIN32=1 -DWIN32_LEAN_AND_MEAN=1 -DNOMINMAX=1
FP_ENV = -fp:strict -fp:except-
OPTIMIZATION = -Zi -O2 -Oi
MISC = -GT -Isrc/ -I$(VULKAN_SDK)/Include/
LIBS = kernel32.lib user32.lib ole32.lib vulkan-1.lib

COMMON = $(LANG) $(DEFINES) $(WARNINGS) $(FP_ENV) $(OPTIMIZATION)

# GLSL
SHADER_OPT = --target-env=vulkan1.2 -std=450core -I "src/shader/" -O

SHADERS = "shader/shader.vs" "shader/shader.fs" "shader/imshader.vs" "shader/imshader.fs" "shader/imguishader.vs" "shader/imguishader.fs"
ALL_SOURCES = "src/*.cpp" "src/*.hpp" "src/Renderer/*.cpp" "src/Renderer/*.hpp"

all: "build" "build/blokker.exe" "build/game.dll" "build/imgui.lib" $(SHADERS)

clean:
	@del /Q "build\*.*"
	@del /Q "shader\*.*"

"build":
	@mkdir build

"build/imgui.lib": "src/imgui/*.cpp" "src/imgui/*.h"
	@cl -nologo $(LANG) -W4 -WX -Zi -O2 -Oi -c -Fo:"build/imgui.obj" -Fd:"build/" "src/imgui/build.cpp"
    @lib -nologo -OUT:$@ "build/imgui.obj"
"build/blokker.exe": "src/Win32_Main.cpp" "build/imgui.lib"
	@cl -nologo $(COMMON) $(MISC) $** -Fo:"build/" -Fd:"build/" $(LIBS) "build\imgui.lib" -link -LIBPATH:$(VULKAN_SDK)/Lib/ -OUT:"build\blokker.exe"

# NOTE(boti): we need unique .pdb names for hot-reloading, but we don't want them to keep piling up so they deleted here explicitly
"build/game.dll": $(ALL_SOURCES) "build/imgui.lib"
    @del /Q "build\game*.pdb"
    @cl -nologo $(COMMON) $(MISC) "src/Game.cpp" -Fo:"build/" -Fd:"build/" -link -DLL -PDB:"build/game$(DATETIME).pdb" -EXPORT:Game_UpdateAndRender -OUT:"build/game.dll" -LIBPATH:$(VULKAN_SDK)/Lib/ vulkan-1.lib "build\imgui.lib"
	
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