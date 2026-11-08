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

### Tests

roundtrip 回环测试（解析 → 序列化 → 再解析 → 逐字段比对），随默认构建生成 `s421_tests`：

```sh
./build/bin/s421_tests                                # 内置样本：模型/XML/ZIP 回环 + 序列化确定性
./build/bin/s421_tests path/to/sample.gml [...]       # 附加外部样本文件回归（见 tests/data/README.md）
```

### Schema Codegen

字段映射表 `src/S421FieldTable.h` 由 XSD 生成（Parser/Serializer 读写共用，勿手改）：

```sh
python3 tools/fieldspecGenerate.py            # 从 dataset/S-421_schema_20260604.xsd 重新生成
python3 tools/fieldspecGenerate.py --check    # 校验生成物与 XSD 是否同步（CI 用，漂移则退出码 1）
cmake --build build --target s421_codegen_check   # 同上，CMake 目标形式
```

schema 升级（如 2.0 → 2.1）流程：替换 XSD → 跑生成器（漂移处报错并逐条处理）→ 跑 roundtrip 测试。