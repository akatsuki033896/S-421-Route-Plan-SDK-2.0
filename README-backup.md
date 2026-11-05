# S-421 SDK

S-421（IEC 63173-1）船载航线数据的**解析 / 序列化 / 校验**库，C++14，跨平台（Windows / Linux / ARM Linux）。

> 注：源码用 C++14（非 C++17），以便在仅支持 C++14 libstdc++ 的交叉编译器（如 RK3128 的 Linaro GCC 5.5）上编译。原 C++17 的 `std::optional` / `std::filesystem` 已分别替换为自包含的 `s421::optional`（[include/s421/S421Optional.h](include/s421/S421Optional.h)，C++17 下自动透传标准库）与 `s421::fs`（[include/s421/S421Fs.h](include/s421/S421Fs.h)），API 语义不变。

默认构建为**静态库**：

- Windows → `s421.lib`
- Linux   → `libs421.a`

## 功能

- **解析** [S421Parser](include/s421/S421Parser.h)：`.s421` / `.s421p`（ZIP 压缩）/ S-100 交换集 → `Dataset`
- **序列化** [S421Serializer](include/s421/S421Serializer.h)：`Dataset` → `.s421` / `.s421p` / 交换集
- **校验** [S421Validator](include/s421/S421Validator.h)：格式 / 属性 / 逻辑 / 错误反馈 四层校验
- **格式化** [S421Formatter](include/s421/S421Formatter.h)：人读摘要，便于调试

## 依赖

**零外部依赖**——zlib 已内嵌（`third_party/zlib`，1.3.1，zlib license）随项目一起编译；pugixml 同样内嵌（`third_party/pugixml`）。

拿到本包即可直接编译，**无需安装 zlib / vcpkg / 任何系统库**。

### 第三方符号隔离（重要）

zlib / pugixml 以源码编入 `s421.lib` / `libs421.a`，但它们的符号**不会以原名导出**，而是编译期重命名为 s421 私有名：

| 第三方 | 原符号 | 重命名后 | 机制 |
|---|---|---|---|
| pugixml | `pugi::xml_document::xml_document()` | `s421_pugi::xml_document::xml_document()` | `-Dpugi=s421_pugi`（命名空间宏替换）|
| zlib | `inflate` / `deflate` / `crc32` … | `z_inflate` / `z_deflate` / `z_crc32` … | `-DZ_PREFIX`（zlib 官方前缀机制）|

这样调用方即使再链接自己的 zlib / pugixml（任意版本），也**不会与库内的符号冲突**（`multiple definition`）。库内 s421 源码通过同样的宏引用 `s421_pugi::` / `z_inflate`，跨文件调用正常。

> s421 公开 API（`parseFile` / `validate` 等）不涉及 `pugi::`（仅 private 方法用，调用方不引用），故对调用方完全透明——调用方**无需**也定义这些宏，照常 `#include <s421/S421.h>` 即可。
>
> 此机制在 CMake（`target_compile_definitions(s421 PRIVATE ...)`）与 ARM Makefile（`APPEND_CFLAGS`）两套构建中均已内置，开箱即用。

为何不用 `-fvisibility=hidden`？该选项对**动态库**有效（HIDDEN 符号不导出），但对**静态库**无效——HIDDEN 符号仍是 GLOBAL binding，链接进可执行时仍会与调用方同名符号 `multiple definition`。符号重命名是静态库隔离的唯一可靠手段。

## 构建

```bash
# 配置
cmake -B build -DCMAKE_BUILD_TYPE=Release
# （Windows MSVC 用：cmake -B build -G "Visual Studio 17 2022"）

# 编译
cmake --build build --config Release
```

产物：

| 文件 | 说明 |
|---|---|
| `build/lib/Release/s421.lib`（Win MSVC）或 `libs421.a`（Linux/MinGW） | 静态库 |
| `build/bin/Release/s421_demo.exe`（Win）或 `s421_demo`（Linux） | 最简调用示例 |

## 交叉编译：RK3128（ARM Linux）

RK3128 = 4× Cortex-A7（ARMv7-A + NEON + VFPv4 + hard-float + glibc Linux）。
SDK 提供**独立的 Makefile 构建**（`build-rk3128/`，不依赖 CMake），产物为 ARM 静态库 + 可执行。

### 工具链（由 makecore 自动选定）

构建体系**对齐 `build-linux/`**：通过 `-include $(MAKEROOT)$(MAKECORE)`（`MAKECORE = mk_core.gcc` + `ARMGCC=TRUE`）让 [makecore](../makecore) 的 `mk_core.gcc` 自动选定工具链——它会从 `$(MAKEROOT)gcc-armhf/bin/` 取 `arm-linux-gnueabihf-`（hard-float）。**不硬编码任何 gcc 路径**。

- `MAKEROOT` 默认 `E:/makecore`，指向 makecore 根（含 `mk_core.gcc` / `mk_core.mk` / `gcc-armhf/`）。
- makecore 选定的是 `gcc-armhf`（Linaro GCC 4.9.4，首个完整支持 C++14 的 GCC，源码已 C++14 化可直接编译）。
- 自定义 makecore 路径：

```bash
make -f armv7-a.mk MAKEROOT=/your/makecore
```

用 `make info` 可查看实际选定的工具链版本（确认 `TOOL_CHAIN_PREFIX` 与 gcc 版本）。

### 一键构建

```bash
bash build-rk3128.sh              # 默认 Release（-O2，strip --strip-debug）
bash build-rk3128.sh debug        # Debug（-g -O0，保留符号，便于 gdb）
bash build-rk3128.sh info         # 查看 mk_core 选定的工具链 / 路径配置
bash build-rk3128.sh clean-all    # 清理全部
```

### 产物（`build-rk3128/output/armv7-a/`，debug 版在 `armv7-a_d/`）

| 文件 | 说明 |
|---|---|
| `libs421.a` | ARM 静态库（自包含，zlib 已编入；链接时无需额外库） |
| `s421_tool` | 命令行工具（ARM ELF，可拷到板子上直接跑） |
| `s421_demo` | 调用示例（ARM ELF） |

验证产物架构（应为 `Machine: ARM`、`hard-float ABI`）：

```bash
arm-linux-gnueabihf-readelf -h build-rk3128/output/armv7-a/libs421.a | grep -E 'Machine|Flags'
# 预期: Machine: ARM, Flags: 0x5000402, Version5 EABI, hard-float ABI
```

### 直接用 make（等价于脚本）

```bash
cd build-rk3128
make                # 默认 Release
make debug          # Debug
make -f armv7-a.mk  # 显式指定
```

### 在 RK3128 板子上集成 `libs421.a`

把 `libs421.a` 与 `include/s421/` 拷到板子工程，在你的 Makefile 里：

```makefile
CXXFLAGS += -I/path/to/include -std=c++14
LDFLAGS  += -L/path/to/lib -ls421 -lpthread
```

> zlib 已静态链入 `libs421.a`，无需额外链接 zlib。

## WIN32（MSVC）Makefile 编译

除 CMake 外，WIN32 还提供**独立的 Makefile 构建**（`build-win32/`，对齐 `build-linux` / `build-rk3128` 风格），通过 makecore 的 `mk_core.vs` 调用 `cl.exe` / `link.exe`。VS2017 / 2019 / 2022 均可——项目为 C++14，`/std:c++14`、`/utf-8` 在 VS2017（15.7+ / 15.5+）起即支持。

### 前置：进入 MSVC 环境（让 `cl` 进 PATH）

```bat
:: 方式1（最简）：开始菜单开 "Developer Command Prompt for VS 2017"，cd 到项目

:: 方式2：普通 cmd 里先初始化 VS 环境（x64）。VS2017/2019/2022 任选其一路径：
call "C:\Program Files (x86)\Microsoft Visual Studio\2017\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
:: call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
```

> 编 32 位库把 `x64` 换成 `x86`。

### 构建

```bat
cd build-win32
make                :: 默认 Release（/O2 /MD /EHsc），产出库 + 工具 + 示例
make debug          :: Debug（/Od /Zi /MDd），产出 output\win32d\*
make clean          :: 清理 Release
make clean-all      :: 清理全部
```

也可显式指定：`make -f win32.mk`（Release）/ `make -f win32d.mk`（Debug）。

### 产物（`build-win32/output/win32/`，Debug 在 `win32d/`）

| 文件 | 说明 |
|---|---|
| `s421.lib` | WIN32 静态库（x64；自包含：zlib/pugixml 已内嵌且符号隔离；`/EHsc` 异常展开） |
| `s421_tool.exe` | 命令行工具（x64） |
| `s421_demo.exe` | 调用示例（x64） |

> 静态库的 CRT/ABI 须与**调用方所用 VS 版本**一致：用哪个 VS 链接，就用同一个 VS 编 `s421.lib`。

## 集成方式（给调用方）

### 方式一：源码集成 `add_subdirectory`

把本目录作为子目录放入你的工程：

```cmake
add_subdirectory(s421-prod-2.0)
target_link_libraries(你的程序 PRIVATE s421)   # 或 s421::s421
```

```cpp
#include <s421/S421.h>

s421::S421Parser parser;
auto dataset = parser.parseFile("route.s421");
if (dataset) {
    s421::S421Validator validator;
    auto result = validator.validate(*dataset);   // ValidationResult
}
```

> 链接 `s421` 后，头文件路径会自动传播给调用方，无需手动 `target_include_directories`。

### 方式二：预编库直接链接

```bash
# 把库与头文件安装到 ./dist
cmake --install build --prefix ./dist --config Release
```

得到 `dist/include/s421/*.h` + `dist/lib/s421.lib`（或 `libs421.a`），在你的工程里：

```cmake
target_include_directories(你的程序 PRIVATE <dist>/include)
target_link_directories(你的程序 PRIVATE <dist>/lib)
target_link_libraries(你的程序 PRIVATE s421)
# zlib 已静态链入 s421.lib / libs421.a，无需额外链接
```

## 构建选项

| 选项 | 默认 | 说明 |
|---|---|---|
| `BUILD_SHARED_LIBS` | `OFF` | `ON` 则构建动态库（`.dll`/`.so`） |
| `S421_BUILD_EXAMPLES` | `ON` | 构建 `s421_demo` 示例 |
| `S421_INSTALL` | `ON` | 生成 install 目标 |
| `S421_LOG_LEVEL` | `4` | 编译期日志级别 `0`–`4` |

### 切换为动态库

```bash
cmake -B build -DBUILD_SHARED_LIBS=ON
```

产出 `s421.dll`/`libs421.so` + 导入库。

> ⚠️ C++ 跨 DLL 传递 `std::string` / `std::unique_ptr` / `std::optional` 等类型时，调用方须与本库使用**相同的编译器版本与 C 运行时 (CRT)**，否则会出现符号缺失或堆崩溃。这也是默认推荐静态库的原因。

## 调用示例

最短调用路径见 `examples/demo.cpp`（解析 → 校验 → 摘要），可直接参考。
