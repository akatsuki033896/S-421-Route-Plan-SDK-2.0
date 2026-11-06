# S-421 Route Plan SDK 2.0

## Generate Project

```sh
cmake -B build -G "Visual Studio 17 2022"
```

## BUILD

### Build `s421.lib` with zlib

generate VS project and build in Visual Studio.

### Build `s421.lib` without zlib (MSVC)

**CRT/ABI should be same as Visual Studio version.**

**JadeECS: x86, Debug, VS2022**

- output(release): `build-win32/win32`
- output(debug): `build-win32/win32d`

In CMD:

```sh
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x86 # build x86
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 # build x64

cd build-win32
make -f sdk.mk OUTPUTDIR=output/sdk_x86 OBJDIR=obj/sdk_x86 BUILDDIR=build/sdk_x86 # release(/O2 /MD /EHsc)
make debug # debug(/Od /Zi /MDd)
dumpbin /headers output\win32\s421.lib | findstr machine # output: 14C machine (x86)
```

### Build `libs421.a` (ARM RK3128)

Compiler: Linaro GCC 4.9.4 / `gcc-armhf`

```sh
make -f armv7-a.mk MAKEROOT=/your/makecore # makecore path
bash build-rk3128.sh              # Release (-O2，strip --strip-debug)
bash build-rk3128.sh debug        # Debug(-g -O0)
bash build-rk3128.sh info         
bash build-rk3128.sh clean-all
arm-linux-gnueabihf-readelf -h build-rk3128/output/armv7-a/libs421.a | grep -E 'Machine|Flags' # Machine: ARM, Flags: 0x5000402, Version5 EABI, hard-float ABI
```

## Quick Start

```cpp
#include <s421/S421.h>

s421::S421Parser parser;
auto dataset = parser.parseFile("route.s421");
if (dataset) {
    s421::S421Validator validator;
    auto result = validator.validate(*dataset);   // ValidationResult
}
```

### 

copy `.h` and `s421.lib` to your project directories

### CMake

```cmake
add_subdirectory(s421-prod-2.0)
target_link_libraries(your_project_name PRIVATE s421)
```