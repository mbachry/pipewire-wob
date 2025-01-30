project('pipewire-wob', 'c')

wireplumber_dep = dependency('wireplumber-0.5')
cc = meson.get_compiler('c')
libm = cc.find_library('m')

executable('pipewire-wob', 'pipemon.c', dependencies : [wireplumber_dep, libm])
