project('blokker', 'cpp')

include_dirs = [
  include_directories('src')
]

src_cpp = [
  'src/X11_Main.cpp',
  'src/Game.cpp'
]

src_shaders = [
  'src/shader/shader.glsl',
  'src/shader/imshader.glsl'
]
shader_dir = meson.source_root() + '/shader'

dependencies =  [
  dependency('x11'),
  dependency('xfixes'),
  dependency('vulkan')
]

#NOTE(mark) Generate the project with 'CC=clang CXX=clang++ meson build'
compiler_options = [
  '-std=c++20',
  '-march=x86-64-v3',
  '-Oi',
  '-O2',
  '-g'
]

shader_compiler_options = [
  '--target-env=vulkan1.2',
  '-std=450core',
  '-I' + meson.source_root() + '/src/shader',
  '-O'
]

defines = [
  '-DDEVELOPER=1',
  '-DPLATFORM_LINUX=1'
]

glslc = find_program('glslc')
foreach s : src_shaders
  custom_target(
    command : [glslc, shader_compiler_options, '-fshader-stage=vert', '-o', shader_dir + '/' + '@OUTPUT@', '-DVERTEX_SHADER=1', '@INPUT@'],
    input : s,
    output : '@BASENAME@.vs',
    install: true,
    install_dir: ''
  )
endforeach
foreach s : src_shaders
  custom_target(
    command : [glslc, shader_compiler_options, '-fshader-stage=frag', '-o', shader_dir + '/' + '@OUTPUT@', '-DFRAGMENT_SHADER=1', '@INPUT@'],
    input : s,
    output : '@BASENAME@.fs',
    install: true,
    install_dir: ''
  )
endforeach

executable('blokker',
           sources: [src_cpp],
           include_directories: [include_dirs],
           dependencies: dependencies,
           cpp_args: [compiler_options, defines]
          )
