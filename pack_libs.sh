#!/usr/bin/env bash
# pack_libs.sh - 打包「预编静态库 + 头文件 + 工具」为 s421-sdk/ 包
#
# Win32 库 = Debug /MDd /IDL2 /不含 zlib（供 Debug + 自带 zlib 的 MSVC 工程集成）；
# ARM  库 = Release 自含 zlib。
# 重编后重跑即刷新。
#
# 用法：  bash pack_libs.sh
# 产物：  s421-sdk/
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK="$ROOT/s421-sdk"

echo "==> 清理旧包: $SDK"
rm -rf "$SDK"

echo "==> 创建目录结构"
mkdir -p "$SDK/include/s421" \
         "$SDK/lib/win32_x64" "$SDK/lib/win32_x86" "$SDK/lib/arm" \
         "$SDK/bin/win32_x64" "$SDK/bin/win32_x86" "$SDK/bin/arm" \
         "$SDK/examples" "$SDK/tests/data"

echo "==> 复制头文件"
cp "$ROOT"/include/s421/*.h "$SDK/include/s421/"

echo "==> 复制 Win32 x64 库（Debug/MDd/不含zlib，来自 sdk_x64）+ 工具（Release 自含）"
if [ -f "$ROOT/build-win32/output/sdk_x64/s421.lib" ]; then
    cp "$ROOT/build-win32/output/sdk_x64/s421.lib" "$SDK/lib/win32_x64/"
    [ -f "$ROOT/build-win32/output/win32/s421_tool.exe" ] && cp "$ROOT/build-win32/output/win32/s421_tool.exe" "$SDK/bin/win32_x64/"
    [ -f "$ROOT/build-win32/output/win32/s421_demo.exe" ] && cp "$ROOT/build-win32/output/win32/s421_demo.exe" "$SDK/bin/win32_x64/"
    echo "    ✓ Win32 x64: s421.lib (Debug/不含zlib) + tool/demo (Release 自含)"
else
    echo "    ⚠ Win32 x64 库未构建（output/sdk_x64/s421.lib 不存在）。先：vcvarsall x64 再 make -f sdk.mk OUTPUTDIR=output/sdk_x64 OBJDIR=obj/sdk_x64 BUILDDIR=build/sdk_x64"
fi

echo "==> 复制 Win32 x86 库（Debug/MDd/不含zlib，来自 sdk_x86）+ 工具（Release 自含）"
if [ -f "$ROOT/build-win32/output/sdk_x86/s421.lib" ]; then
    cp "$ROOT/build-win32/output/sdk_x86/s421.lib" "$SDK/lib/win32_x86/"
    [ -f "$ROOT/build-win32/output/win32x86/s421_tool.exe" ] && cp "$ROOT/build-win32/output/win32x86/s421_tool.exe" "$SDK/bin/win32_x86/"
    [ -f "$ROOT/build-win32/output/win32x86/s421_demo.exe" ] && cp "$ROOT/build-win32/output/win32x86/s421_demo.exe" "$SDK/bin/win32_x86/"
    echo "    ✓ Win32 x86: s421.lib (Debug/不含zlib) + tool/demo (Release 自含)"
else
    echo "    ⚠ Win32 x86 库未构建（output/sdk_x86/s421.lib 不存在）。先：vcvarsall x86 再 make -f sdk.mk OUTPUTDIR=output/sdk_x86 OBJDIR=obj/sdk_x86 BUILDDIR=build/sdk_x86"
fi

echo "==> 复制 ARM 库（不含 zlib，来自 armv7-a-extzlib）+ 工具（自含）"
if [ -f "$ROOT/build-rk3128/output/armv7-a-extzlib/libs421.a" ]; then
    cp "$ROOT/build-rk3128/output/armv7-a-extzlib/libs421.a" "$SDK/lib/arm/"
    [ -f "$ROOT/build-rk3128/output/armv7-a/s421_tool" ] && cp "$ROOT/build-rk3128/output/armv7-a/s421_tool" "$SDK/bin/arm/"
    [ -f "$ROOT/build-rk3128/output/armv7-a/s421_demo" ] && cp "$ROOT/build-rk3128/output/armv7-a/s421_demo" "$SDK/bin/arm/"
    chmod +x "$SDK/bin/arm/"* 2>/dev/null || true
    echo "    ✓ ARM: libs421.a (不含zlib) + tool/demo (自含)"
else
    echo "    ⚠ ARM 库未构建（output/armv7-a-extzlib/libs421.a 不存在）。先：make -C build-rk3128 -f armv7-a-extzlib.mk"
fi

echo "==> 复制示例源码 + 样本数据"
cp "$ROOT/examples/demo.cpp" "$SDK/examples/"
for f in "$ROOT"/tests/data/RTE-TEST-GMIN_20260604.s421 \
         "$ROOT"/tests/data/RTE-TEST-GBASIC_20260604.s421 \
         "$ROOT"/tests/data/RTE-TEST-GFULL_20260604.s421 \
         "$ROOT"/tests/data/GMIN.s421p \
         "$ROOT"/tests/data/GBASIC.s421p \
         "$ROOT"/tests/data/GFULL.s421p; do
    [ -f "$f" ] && cp "$f" "$SDK/tests/data/" || echo "    ⚠ 缺样本: $(basename "$f")"
done
echo "    ✓ examples/demo.cpp + tests/data/"

echo "==> 生成 README.md"
cat > "$SDK/README.md" <<'EOF'
# S-421 SDK（预编库）

## 库配置（重要，先看）
- **Win32（lib/win32_x64、lib/win32_x86）**：`Debug /MDd /IDL2 /不含 zlib`。
  供 **Debug + 自带 zlib** 的 MSVC 工程集成：链本库 **+ 你自己的 zlib**（如 zlibstat.lib）。
- **ARM（lib/arm/libs421.a）**：`Release /不含 zlib`。供自带 zlib 的 ARM Linux 工程集成：链本库 + 你自己的 zlib（`-lz` 或 libz.a）。

## 目录
- `include/s421/`            — 头文件（`#include <s421/S421.h>`）
- `lib/win32_x64/s421.lib`   — Windows MSVC 64位（Debug /MDd /不含 zlib）
- `lib/win32_x86/s421.lib`   — Windows MSVC 32位（Debug /MDd /不含 zlib）
- `lib/arm/libs421.a`        — ARM Linux（Release /不含 zlib）
- `bin/`                     — 命令行工具 s421_tool + s421_demo（Release 自含，独立运行，不链入工程）
- `examples/demo.cpp`        — 调用示例源码（解析→校验→序列化）
- `tests/data/`              — 样本（GMIN/GBASIC/GFULL，.s421 / .s421p）

## 集成（Windows / MSVC，Debug）
前置：VS2022 Debug 环境（`/MDd /IDL2`）。链库时 **s421.lib 要在你的 zlib 之前**：
命令行（64位 Debug）：
    cl /nologo /EHsc /std:c++17 /utf-8 /MDd /D_ITERATOR_DEBUG_LEVEL=2 \
       /I include /DS421_STATIC_DEFINE /Dpugi=s421_pugi \
       app.cpp /link lib\win32_x64\s421.lib zlibstat.lib
CMake：
    target_include_directories(app PRIVATE include)
    target_link_libraries(app PRIVATE lib/win32_x64/s421.lib zlibstat.lib)
    target_compile_definitions(app PRIVATE S421_STATIC_DEFINE pugi=s421_pugi _ITERATOR_DEBUG_LEVEL=2)
    # Debug 配置须 /MDd：set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDebugDLL")

## 集成（ARM Linux）
    g++ -std=c++14 -I include -DS421_STATIC_DEFINE -Dpugi=s421_pugi \
        app.cpp lib/arm/libs421.a -lz -lpthread -o app
    # -lz = 调用方的 zlib（提供 inflate 等；s421 库不含 zlib）

## 注意
- **Win32 库是 Debug /MDd /IDL2**：调用方必须 Debug 配置，否则报 LNK2038 RuntimeLibrary / _ITERATOR_DEBUG_LEVEL 不匹配。
- **Win32 库不含 zlib**：必须额外链你自己的 zlib（zlibstat.lib 等），s421.lib 放在 zlib 前。
- **S421_STATIC_DEFINE 必须加**：静态库标记，不加则 S421_API 被当 dllimport。
- **pugi=s421_pugi 必须加**：库内 pugixml namespace 已重命名隔离，编译须一致。
- **位数匹配**：64位链 win32_x64，32位链 win32_x86。
- **ARM 库不含 zlib**：必须额外链 zlib（`-lz` 或 libz.a），否则 inflate 等符号未解析。
- ARM 工具拷到板子后：`chmod +x s421_tool s421_demo`。
EOF

echo ""
echo "✅ 打包完成 → $SDK"
echo ""
echo "内容清单："
( cd "$SDK" && find . -type f | sort | sed 's|^\./|  |' )
