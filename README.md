pbkit_sdl_gpu
====

Basic implementation of [sdl-gpu](https://github.com/grimfang4/sdl-gpu) for the original Microsoft Xbox.
Uses pbkit from the [nxdk](https://github.com/XboxDev/nxdk).

## Usage - Makefile

In the top level `Makefile`:
1. Set the `SDL_GPU_DIR` variable to the location of the sdl-gpu submodule. 
2. Include the pbkit-sdl-gpu `Makefile.inc`

```makefile
override SDL_GPU_DIR := third_party/sdl-gpu
include third_party/pbkit-sdl-gpu/Makefile.inc
```

In your source files:
1. Include `pbkit_sdl_gpu.h`
2. Initialize the library by calling `PBKitSDLGPUInit`

```c
#ifdef FC_USE_SDL_GPU
#include "third_party/pbkit-sdl-gpu/pbkit_sdl_gpu.h"
#endif

#ifdef FC_USE_SDL_GPU
  PBKitSDLGPUInit();
#endif

```
