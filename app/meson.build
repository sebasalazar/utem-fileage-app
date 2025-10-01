project(
  'paralelo',
  'cpp',
  default_options: [
    'cpp_std=c++17',
    'buildtype=debug',        # -g y sin -O2; usa 'release' para optimizar
    'warning_level=3'         # equivalente aprox. a -Wall -Wextra -Wpedantic
  ]
)

cpp = meson.get_compiler('cpp')

# Flags adicionales (opcionales)
add_project_arguments(
  '-march=native',
  language: 'cpp'
)

# Dependencias
openmp = dependency('openmp', required: true)
tbb    = dependency('tbb',   required: true)
boost  = dependency('boost', modules: ['thread', 'system', 'atomic'], required: true)

# Librerías “planas” (cuando no hay pkg-config)
libm     = cpp.find_library('m', required: false)       # en Linux normalmente está
libatomic= cpp.find_library('atomic', required: false)  # útil en algunas libstdc++

# Fuentes compartidas
edad_src = files('Edad.cpp', 'edad.hpp')

# Ejecutables
paralelo = executable(
  'paralelo',
  ['main.cpp'] + edad_src,
  dependencies: [openmp, tbb, boost],
  link_with: [],
  link_args: [],
  install: true           # permite "meson install"
)

simple = executable(
  'simple',
  ['simple.cpp'] + edad_src,
  dependencies: [],
  link_with: [],
  link_args: [],
  install: true
)

# Enlazar libm/libatomic si existen
foreach exe : [paralelo, simple]
  if libm.found()
    exe.add_dependency(libm)
  endif
  if libatomic.found()
    exe.add_dependency(libatomic)
  endif
endforeach
