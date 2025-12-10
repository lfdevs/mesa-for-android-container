# Benchmark Results
## Adreno 660
### glmark2
```log
MESA-LOADER: failed to retrieve device information
MESA: error: kgsl_pipe_get_param:103: invalid param id: 13
=======================================================
    glmark2 2023.01
=======================================================
    OpenGL Information
    GL_VENDOR:      freedreno
    GL_RENDERER:    FD660
    GL_VERSION:     4.6 (Compatibility Profile) Mesa 25.3.0-devel (git-6fb40f7c28)
    Surface Config: buf=32 r=8 g=8 b=8 a=8 depth=24 stencil=0 samples=0
    Surface Size:   800x600 windowed
=======================================================
[build] use-vbo=false: FPS: 872 FrameTime: 1.148 ms
[build] use-vbo=true: FPS: 924 FrameTime: 1.083 ms
[texture] texture-filter=nearest: FPS: 925 FrameTime: 1.082 ms
[texture] texture-filter=linear: FPS: 899 FrameTime: 1.113 ms
[texture] texture-filter=mipmap: FPS: 930 FrameTime: 1.076 ms
[shading] shading=gouraud: FPS: 933 FrameTime: 1.072 ms
[shading] shading=blinn-phong-inf: FPS: 949 FrameTime: 1.054 ms
[shading] shading=phong: FPS: 950 FrameTime: 1.053 ms
[shading] shading=cel: FPS: 929 FrameTime: 1.077 ms
[bump] bump-render=high-poly: FPS: 898 FrameTime: 1.114 ms
[bump] bump-render=normals: FPS: 953 FrameTime: 1.050 ms
[bump] bump-render=height: FPS: 952 FrameTime: 1.051 ms
[effect2d] kernel=0,1,0;1,-4,1;0,1,0;: FPS: 948 FrameTime: 1.055 ms
[effect2d] kernel=1,1,1,1,1;1,1,1,1,1;1,1,1,1,1;: FPS: 948 FrameTime: 1.055 ms
[pulsar] light=false:quads=5:texture=false: FPS: 945 FrameTime: 1.059 ms
[desktop] blur-radius=5:effect=blur:passes=1:separable=true:windows=4: FPS: 946 FrameTime: 1.057 ms
[desktop] effect=shadow:windows=4: FPS: 951 FrameTime: 1.052 ms
[buffer] columns=200:interleave=false:update-dispersion=0.9:update-fraction=0.5:update-method=map: FPS: 923 FrameTime: 1.084 ms
[buffer] columns=200:interleave=false:update-dispersion=0.9:update-fraction=0.5:update-method=subdata: FPS: 754 FrameTime: 1.328 ms
[buffer] columns=200:interleave=true:update-dispersion=0.9:update-fraction=0.5:update-method=map: FPS: 801 FrameTime: 1.250 ms
[ideas] speed=duration: FPS: 728 FrameTime: 1.374 ms
[jellyfish] <default>: FPS: 796 FrameTime: 1.257 ms
[terrain] <default>: FPS: 297 FrameTime: 3.368 ms
[shadow] <default>: FPS: 757 FrameTime: 1.323 ms
[refract] <default>: FPS: 606 FrameTime: 1.652 ms
[conditionals] fragment-steps=0:vertex-steps=0: FPS: 780 FrameTime: 1.282 ms
[conditionals] fragment-steps=5:vertex-steps=0: FPS: 768 FrameTime: 1.303 ms
[conditionals] fragment-steps=0:vertex-steps=5: FPS: 805 FrameTime: 1.243 ms
[function] fragment-complexity=low:fragment-steps=5: FPS: 799 FrameTime: 1.253 ms
[function] fragment-complexity=medium:fragment-steps=5: FPS: 805 FrameTime: 1.242 ms
[loop] fragment-loop=false:fragment-steps=5:vertex-steps=5: FPS: 795 FrameTime: 1.258 ms
[loop] fragment-steps=5:fragment-uniform=false:vertex-steps=5: FPS: 786 FrameTime: 1.273 ms
[loop] fragment-steps=5:fragment-uniform=true:vertex-steps=5: FPS: 799 FrameTime: 1.252 ms
=======================================================
                                  glmark2 Score: 842
=======================================================
```
### glmark2-es2
```log
os_same_file_description couldn't determine if two DRM fds reference the same file description. (Function not implemented)
Let's just assume that file descriptors for the same file probablyshare the file description instead. This may cause problems whenthat isn't the case.
MESA-LOADER: failed to retrieve device information
MESA: error: kgsl_pipe_get_param:103: invalid param id: 13
=======================================================
    glmark2 2023.01
=======================================================
    OpenGL Information
    GL_VENDOR:      freedreno
    GL_RENDERER:    FD660
    GL_VERSION:     OpenGL ES 3.2 Mesa 25.3.0-devel (git-6fb40f7c28)
    Surface Config: buf=32 r=8 g=8 b=8 a=8 depth=24 stencil=0 samples=0
    Surface Size:   800x600 windowed
=======================================================
[build] use-vbo=false: FPS: 797 FrameTime: 1.255 ms
[build] use-vbo=true: FPS: 810 FrameTime: 1.236 ms
[texture] texture-filter=nearest: FPS: 807 FrameTime: 1.239 ms
[texture] texture-filter=linear: FPS: 807 FrameTime: 1.239 ms
[texture] texture-filter=mipmap: FPS: 805 FrameTime: 1.243 ms
[shading] shading=gouraud: FPS: 799 FrameTime: 1.252 ms
[shading] shading=blinn-phong-inf: FPS: 806 FrameTime: 1.241 ms
[shading] shading=phong: FPS: 806 FrameTime: 1.242 ms
[shading] shading=cel: FPS: 803 FrameTime: 1.246 ms
[bump] bump-render=high-poly: FPS: 752 FrameTime: 1.330 ms
[bump] bump-render=normals: FPS: 803 FrameTime: 1.246 ms
[bump] bump-render=height: FPS: 808 FrameTime: 1.238 ms
[effect2d] kernel=0,1,0;1,-4,1;0,1,0;: FPS: 801 FrameTime: 1.249 ms
[effect2d] kernel=1,1,1,1,1;1,1,1,1,1;1,1,1,1,1;: FPS: 808 FrameTime: 1.239 ms
[pulsar] light=false:quads=5:texture=false: FPS: 809 FrameTime: 1.237 ms
[desktop] blur-radius=5:effect=blur:passes=1:separable=true:windows=4: FPS: 793 FrameTime: 1.262 ms
[desktop] effect=shadow:windows=4: FPS: 781 FrameTime: 1.281 ms
[buffer] columns=200:interleave=false:update-dispersion=0.9:update-fraction=0.5:update-method=map: FPS: 703 FrameTime: 1.423 ms
[buffer] columns=200:interleave=false:update-dispersion=0.9:update-fraction=0.5:update-method=subdata: FPS: 741 FrameTime: 1.351 ms
[buffer] columns=200:interleave=true:update-dispersion=0.9:update-fraction=0.5:update-method=map: FPS: 790 FrameTime: 1.266 ms
[ideas] speed=duration: FPS: 800 FrameTime: 1.251 ms
[jellyfish] <default>: FPS: 808 FrameTime: 1.239 ms
[terrain] <default>: FPS: 288 FrameTime: 3.480 ms
[shadow] <default>: FPS: 780 FrameTime: 1.283 ms
[refract] <default>: FPS: 606 FrameTime: 1.651 ms
[conditionals] fragment-steps=0:vertex-steps=0: FPS: 784 FrameTime: 1.276 ms
[conditionals] fragment-steps=5:vertex-steps=0: FPS: 803 FrameTime: 1.247 ms
[conditionals] fragment-steps=0:vertex-steps=5: FPS: 810 FrameTime: 1.236 ms
[function] fragment-complexity=low:fragment-steps=5: FPS: 797 FrameTime: 1.255 ms
[function] fragment-complexity=medium:fragment-steps=5: FPS: 809 FrameTime: 1.236 ms
[loop] fragment-loop=false:fragment-steps=5:vertex-steps=5: FPS: 810 FrameTime: 1.236 ms
[loop] fragment-steps=5:fragment-uniform=false:vertex-steps=5: FPS: 809 FrameTime: 1.237 ms
[loop] fragment-steps=5:fragment-uniform=true:vertex-steps=5: FPS: 767 FrameTime: 1.304 ms
=======================================================
                                  glmark2 Score: 771
=======================================================
```
### vkmark
```log
os_same_file_description couldn't determine if two DRM fds reference the same file description. (Function not implemented)
Let's just assume that file descriptors for the same file probablyshare the file description instead. This may cause problems whenthat isn't the case.
=======================================================
    vkmark 2025.01
=======================================================
    Vendor ID:      0x5143
    Device ID:      0x6060001
    Device Name:    Turnip Adreno (TM) 660
    Driver Version: 104865891
    Device UUID:    9b8368a2ae967de07da9b591ed389b09
=======================================================
[vertex] device-local=true: FPS: 999 FrameTime: 1.001 ms
[vertex] device-local=false: FPS: 1188 FrameTime: 0.842 ms
[texture] anisotropy=0: FPS: 1106 FrameTime: 0.904 ms
[texture] anisotropy=16: FPS: 1258 FrameTime: 0.795 ms
[shading] shading=gouraud: FPS: 1148 FrameTime: 0.871 ms
[shading] shading=blinn-phong-inf: FPS: 1160 FrameTime: 0.862 ms
[shading] shading=phong: FPS: 1107 FrameTime: 0.903 ms
[shading] shading=cel: FPS: 1112 FrameTime: 0.899 ms
[effect2d] kernel=edge: FPS: 1234 FrameTime: 0.810 ms
[effect2d] kernel=blur: FPS: 745 FrameTime: 1.342 ms
[desktop] <default>: FPS: 1248 FrameTime: 0.801 ms
[cube] <default>: FPS: 1364 FrameTime: 0.733 ms
[clear] <default>: FPS: 1552 FrameTime: 0.644 ms
=======================================================
                                   vkmark Score: 1170
=======================================================
```
## Adreno 730
### glmark2
```log
MESA-LOADER: failed to retrieve device information
MESA: error: kgsl_pipe_get_param:103: invalid param id: 13
=======================================================
    glmark2 2023.01
=======================================================
    OpenGL Information
    GL_VENDOR:      freedreno
    GL_RENDERER:    FD725
    GL_VERSION:     4.6 (Compatibility Profile) Mesa 25.3.0-devel (git-6fb40f7c28)
    Surface Config: buf=32 r=8 g=8 b=8 a=8 depth=24 stencil=0 samples=0
    Surface Size:   800x600 windowed
=======================================================
[build] use-vbo=false: FPS: 3097 FrameTime: 0.323 ms
[build] use-vbo=true: FPS: 1799 FrameTime: 0.556 ms
[texture] texture-filter=nearest: FPS: 1158 FrameTime: 0.864 ms
[texture] texture-filter=linear: FPS: 1079 FrameTime: 0.927 ms
[texture] texture-filter=mipmap: FPS: 1024 FrameTime: 0.977 ms
[shading] shading=gouraud: FPS: 1316 FrameTime: 0.760 ms
[shading] shading=blinn-phong-inf: FPS: 1092 FrameTime: 0.916 ms
[shading] shading=phong: FPS: 1176 FrameTime: 0.851 ms
[shading] shading=cel: FPS: 1073 FrameTime: 0.932 ms
[bump] bump-render=high-poly: FPS: 1160 FrameTime: 0.863 ms
[bump] bump-render=normals: FPS: 1028 FrameTime: 0.973 ms
[bump] bump-render=height: FPS: 1004 FrameTime: 0.997 ms
[effect2d] kernel=0,1,0;1,-4,1;0,1,0;: FPS: 1118 FrameTime: 0.895 ms
[effect2d] kernel=1,1,1,1,1;1,1,1,1,1;1,1,1,1,1;: FPS: 1262 FrameTime: 0.792 ms
[pulsar] light=false:quads=5:texture=false: FPS: 1924 FrameTime: 0.520 ms
[desktop] blur-radius=5:effect=blur:passes=1:separable=true:windows=4: FPS: 606 FrameTime: 1.652 ms
[desktop] effect=shadow:windows=4: FPS: 982 FrameTime: 1.019 ms
[buffer] columns=200:interleave=false:update-dispersion=0.9:update-fraction=0.5:update-method=map: FPS: 261 FrameTime: 3.833 ms
[buffer] columns=200:interleave=false:update-dispersion=0.9:update-fraction=0.5:update-method=subdata: FPS: 1351 FrameTime: 0.740 ms
[buffer] columns=200:interleave=true:update-dispersion=0.9:update-fraction=0.5:update-method=map: FPS: 266 FrameTime: 3.763 ms
[ideas] speed=duration: FPS: 515 FrameTime: 1.942 ms
[jellyfish] <default>: FPS: 1317 FrameTime: 0.760 ms
[terrain] <default>: FPS: 166 FrameTime: 6.027 ms
[shadow] <default>: FPS: 1173 FrameTime: 0.853 ms
[refract] <default>: FPS: 689 FrameTime: 1.453 ms
[conditionals] fragment-steps=0:vertex-steps=0: FPS: 1396 FrameTime: 0.717 ms
[conditionals] fragment-steps=5:vertex-steps=0: FPS: 1968 FrameTime: 0.508 ms
[conditionals] fragment-steps=0:vertex-steps=5: FPS: 1051 FrameTime: 0.952 ms
[function] fragment-complexity=low:fragment-steps=5: FPS: 1576 FrameTime: 0.635 ms
[function] fragment-complexity=medium:fragment-steps=5: FPS: 1083 FrameTime: 0.924 ms
[loop] fragment-loop=false:fragment-steps=5:vertex-steps=5: FPS: 1077 FrameTime: 0.929 ms
[loop] fragment-steps=5:fragment-uniform=false:vertex-steps=5: FPS: 1760 FrameTime: 0.568 ms
[loop] fragment-steps=5:fragment-uniform=true:vertex-steps=5: FPS: 1071 FrameTime: 0.934 ms
=======================================================
                                  glmark2 Score: 1169
=======================================================
```
### glmark2-es2
```log
MESA-LOADER: failed to retrieve device information
MESA: error: kgsl_pipe_get_param:103: invalid param id: 13
=======================================================
    glmark2 2023.01
=======================================================
    OpenGL Information
    GL_VENDOR:      freedreno
    GL_RENDERER:    FD725
    GL_VERSION:     OpenGL ES 3.2 Mesa 25.3.0-devel (git-6fb40f7c28)
    Surface Config: buf=32 r=8 g=8 b=8 a=8 depth=24 stencil=0 samples=0
    Surface Size:   800x600 windowed
=======================================================
[build] use-vbo=false: FPS: 1020 FrameTime: 0.981 ms
[build] use-vbo=true: FPS: 1180 FrameTime: 0.848 ms
[texture] texture-filter=nearest: FPS: 1076 FrameTime: 0.930 ms
[texture] texture-filter=linear: FPS: 1174 FrameTime: 0.852 ms
[texture] texture-filter=mipmap: FPS: 961 FrameTime: 1.041 ms
[shading] shading=gouraud: FPS: 1435 FrameTime: 0.697 ms
[shading] shading=blinn-phong-inf: FPS: 1022 FrameTime: 0.979 ms
[shading] shading=phong: FPS: 1051 FrameTime: 0.952 ms
[shading] shading=cel: FPS: 1100 FrameTime: 0.909 ms
[bump] bump-render=high-poly: FPS: 1150 FrameTime: 0.870 ms
[bump] bump-render=normals: FPS: 934 FrameTime: 1.071 ms
[bump] bump-render=height: FPS: 1014 FrameTime: 0.986 ms
[effect2d] kernel=0,1,0;1,-4,1;0,1,0;: FPS: 1555 FrameTime: 0.643 ms
[effect2d] kernel=1,1,1,1,1;1,1,1,1,1;1,1,1,1,1;: FPS: 1300 FrameTime: 0.769 ms
[pulsar] light=false:quads=5:texture=false: FPS: 1164 FrameTime: 0.859 ms
[desktop] blur-radius=5:effect=blur:passes=1:separable=true:windows=4: FPS: 580 FrameTime: 1.725 ms
[desktop] effect=shadow:windows=4:
 FPS: 3615 FrameTime: 0.277 ms
[buffer] columns=200:interleave=false:update-dispersion=0.9:update-fraction=0.5:update-method=map: FPS: 353 FrameTime: 2.838 ms
[buffer] columns=200:interleave=false:update-dispersion=0.9:update-fraction=0.5:update-method=subdata: FPS: 1822 FrameTime: 0.549 ms
[buffer] columns=200:interleave=true:update-dispersion=0.9:update-fraction=0.5:update-method=map: FPS: 275 FrameTime: 3.638 ms
[ideas] speed=duration: FPS: 543 FrameTime: 1.843 ms
[jellyfish] <default>: FPS: 1369 FrameTime: 0.731 ms
[terrain] <default>: FPS: 165 FrameTime: 6.067 ms
[shadow] <default>: FPS: 1211 FrameTime: 0.826 ms
[refract] <default>: FPS: 692 FrameTime: 1.445 ms
[conditionals] fragment-steps=0:vertex-steps=0: FPS: 1802 FrameTime: 0.555 ms
[conditionals] fragment-steps=5:vertex-steps=0: FPS: 1015 FrameTime: 0.986 ms
[conditionals] fragment-steps=0:vertex-steps=5: FPS: 1106 FrameTime: 0.905 ms
[function] fragment-complexity=low:fragment-steps=5: FPS: 1042 FrameTime: 0.960 ms
[function] fragment-complexity=medium:fragment-steps=5: FPS: 1282 FrameTime: 0.780 ms
[loop] fragment-loop=false:fragment-steps=5:vertex-steps=5: FPS: 1652 FrameTime: 0.606 ms
[loop] fragment-steps=5:fragment-uniform=false:vertex-steps=5: FPS: 1010 FrameTime: 0.990 ms
[loop] fragment-steps=5:fragment-uniform=true:vertex-steps=5: FPS: 1091 FrameTime: 0.917 ms
=======================================================
                                  glmark2 Score: 1143
=======================================================
```
### vkmark
Not working.  
## Adreno 840
### glmark2
```log
MESA-LOADER: failed to retrieve device information
MESA: error: kgsl_pipe_get_param:103: invalid param id: 13
=======================================================
    glmark2 2023.01
=======================================================
    OpenGL Information
    GL_VENDOR:      freedreno
    GL_RENDERER:    Adreno (TM) 840
    GL_VERSION:     4.6 (Compatibility Profile) Mesa 26.0.0-devel (git-85997e912c)
    Surface Config: buf=32 r=8 g=8 b=8 a=8 depth=24 stencil=0 samples=0
    Surface Size:   800x600 windowed
=======================================================
[build] use-vbo=false: FPS: 3791 FrameTime: 0.264 ms
[build] use-vbo=true: FPS: 3583 FrameTime: 0.279 ms
[texture] texture-filter=nearest: FPS: 3417 FrameTime: 0.293 ms
[texture] texture-filter=linear: FPS: 3846 FrameTime: 0.260 ms
[texture] texture-filter=mipmap: FPS: 3049 FrameTime: 0.328 ms
[shading] shading=gouraud: FPS: 3529 FrameTime: 0.283 ms
[shading] shading=blinn-phong-inf: FPS: 3451 FrameTime: 0.290 ms
[shading] shading=phong: FPS: 4112 FrameTime: 0.243 ms
[shading] shading=cel: FPS: 4173 FrameTime: 0.240 ms
[bump] bump-render=high-poly: FPS: 4185 FrameTime: 0.239 ms
[bump] bump-render=normals: FPS: 4103 FrameTime: 0.244 ms
[bump] bump-render=height: FPS: 3354 FrameTime: 0.298 ms
[effect2d] kernel=0,1,0;1,-4,1;0,1,0;: FPS: 3291 FrameTime: 0.304 ms
[effect2d] kernel=1,1,1,1,1;1,1,1,1,1;1,1,1,1,1;: FPS: 3106 FrameTime: 0.322 ms
[pulsar] light=false:quads=5:texture=false: FPS: 3346 FrameTime: 0.299 ms
[desktop] blur-radius=5:effect=blur:passes=1:separable=true:windows=4: FPS: 1955 FrameTime: 0.512 ms
[desktop] effect=shadow:windows=4: FPS: 2444 FrameTime: 0.409 ms
[buffer] columns=200:interleave=false:update-dispersion=0.9:update-fraction=0.5:update-method=map: FPS: 547 FrameTime: 1.829 ms
[buffer] columns=200:interleave=false:update-dispersion=0.9:update-fraction=0.5:update-method=subdata: FPS: 1209 FrameTime: 0.828 ms
[buffer] columns=200:interleave=true:update-dispersion=0.9:update-fraction=0.5:update-method=map: FPS: 615 FrameTime: 1.627 ms
[ideas] speed=duration: FPS: 1593 FrameTime: 0.628 ms
[jellyfish] <default>: FPS: 3489 FrameTime: 0.287 ms
[terrain] <default>: FPS: 809 FrameTime: 1.237 ms
[shadow] <default>: FPS: 3145 FrameTime: 0.318 ms
[refract] <default>: FPS: 1569 FrameTime: 0.638 ms
[conditionals] fragment-steps=0:vertex-steps=0: FPS: 3256 FrameTime: 0.307 ms
[conditionals] fragment-steps=5:vertex-steps=0: FPS: 3241 FrameTime: 0.309 ms
[conditionals] fragment-steps=0:vertex-steps=5: FPS: 3028 FrameTime: 0.330 ms
[function] fragment-complexity=low:fragment-steps=5: FPS: 3169 FrameTime: 0.316 ms
[function] fragment-complexity=medium:fragment-steps=5: FPS: 3198 FrameTime: 0.313 ms
[loop] fragment-loop=false:fragment-steps=5:vertex-steps=5: FPS: 3184 FrameTime: 0.314 ms
[loop] fragment-steps=5:fragment-uniform=false:vertex-steps=5: FPS: 3167 FrameTime: 0.316 ms
[loop] fragment-steps=5:fragment-uniform=true:vertex-steps=5: FPS: 3347 FrameTime: 0.299 ms
=======================================================
                                  glmark2 Score: 2947
=======================================================
```
### glmark2-es2
```log
MESA-LOADER: failed to retrieve device information
MESA: error: kgsl_pipe_get_param:103: invalid param id: 13
=======================================================
    glmark2 2023.01
=======================================================
    OpenGL Information
    GL_VENDOR:      freedreno
    GL_RENDERER:    Adreno (TM) 840
    GL_VERSION:     OpenGL ES 3.2 Mesa 26.0.0-devel (git-85997e912c)
    Surface Config: buf=32 r=8 g=8 b=8 a=8 depth=24 stencil=0 samples=0
    Surface Size:   800x600 windowed
=======================================================
[build] use-vbo=false: FPS: 3657 FrameTime: 0.273 ms
[build] use-vbo=true: FPS: 3641 FrameTime: 0.275 ms
[texture] texture-filter=nearest: FPS: 3459 FrameTime: 0.289 ms
[texture] texture-filter=linear: FPS: 3523 FrameTime: 0.284 ms
[texture] texture-filter=mipmap: FPS: 3560 FrameTime: 0.281 ms
[shading] shading=gouraud: FPS: 4238 FrameTime: 0.236 ms
[shading] shading=blinn-phong-inf: FPS: 3938 FrameTime: 0.254 ms
[shading] shading=phong: FPS: 3308 FrameTime: 0.302 ms
[shading] shading=cel: FPS: 3271 FrameTime: 0.306 ms
[bump] bump-render=high-poly: FPS: 3254 FrameTime: 0.307 ms
[bump] bump-render=normals: FPS: 3356 FrameTime: 0.298 ms
[bump] bump-render=height: FPS: 3360 FrameTime: 0.298 ms
[effect2d] kernel=0,1,0;1,-4,1;0,1,0;: FPS: 3367 FrameTime: 0.297 ms
[effect2d] kernel=1,1,1,1,1;1,1,1,1,1;1,1,1,1,1;: FPS: 3305 FrameTime: 0.303 ms
[pulsar] light=false:quads=5:texture=false: FPS: 3304 FrameTime: 0.303 ms
[desktop] blur-radius=5:effect=blur:passes=1:separable=true:windows=4: FPS: 1748 FrameTime: 0.572 ms
[desktop] effect=shadow:windows=4: FPS: 2472 FrameTime: 0.405 ms
[buffer] columns=200:interleave=false:update-dispersion=0.9:update-fraction=0.5:update-method=map: FPS: 392 FrameTime: 2.553 ms
[buffer] columns=200:interleave=false:update-dispersion=0.9:update-fraction=0.5:update-method=subdata: FPS: 1273 FrameTime: 0.786 ms
[buffer] columns=200:interleave=true:update-dispersion=0.9:update-fraction=0.5:update-method=map: FPS: 583 FrameTime: 1.717 ms
[ideas] speed=duration: FPS: 1192 FrameTime: 0.839 ms
[jellyfish] <default>: FPS: 3169 FrameTime: 0.316 ms
[terrain] <default>: FPS: 788 FrameTime: 1.269 ms
[shadow] <default>: FPS: 3095 FrameTime: 0.323 ms
[refract] <default>: FPS: 1600 FrameTime: 0.625 ms
[conditionals] fragment-steps=0:vertex-steps=0: FPS: 3394 FrameTime: 0.295 ms
[conditionals] fragment-steps=5:vertex-steps=0: FPS: 3396 FrameTime: 0.294 ms
[conditionals] fragment-steps=0:vertex-steps=5: FPS: 3284 FrameTime: 0.305 ms
[function] fragment-complexity=low:fragment-steps=5: FPS: 3285 FrameTime: 0.304 ms
[function] fragment-complexity=medium:fragment-steps=5: FPS: 3317 FrameTime: 0.302 ms
[loop] fragment-loop=false:fragment-steps=5:vertex-steps=5: FPS: 3444 FrameTime: 0.290 ms
[loop] fragment-steps=5:fragment-uniform=false:vertex-steps=5: FPS: 3391 FrameTime: 0.295 ms
[loop] fragment-steps=5:fragment-uniform=true:vertex-steps=5: FPS: 3404 FrameTime: 0.294 ms
=======================================================
                                  glmark2 Score: 2901
=======================================================
```
### vkmark
Not working.  
```log
Error: filesystem error: directory iterator cannot open directory: Permission denied [/dev/dri]
```