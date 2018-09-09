project('chapter_exe', 'cpp', version: 'mod4+avs+vs', meson_version: '>=0.46', default_options: ['cpp_std=c++14'])

bin_name = meson.project_name()
sources = ['src/chapter_exe.cpp', 'src/mvec.cpp']

icu = dependency('icu-uc')
vapoursynth = dependency('vapoursynth')
vapoursynth_script = dependency('vapoursynth-script')

executable(bin_name, sources, dependencies: [icu, vapoursynth, vapoursynth_script])
