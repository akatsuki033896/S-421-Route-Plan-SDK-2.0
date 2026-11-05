#!/usr/bin/env bash
# pack_sdk.sh - 把第三方需要的文件从源码（唯一真相）抽取到独立的 sdk/ 包
#
# 源码始终以项目根目录为准；本脚本只做"抽取 + 复制"，保证 sdk/ 与源码一致。
# 修改源码后重新跑一次本脚本，即可刷新 sdk/。
#
# 用法：  bash pack_sdk.sh
# 产物：  sdk/   （自包含，可直接整包交付给第三方）
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK="$ROOT/sdk"

echo "==> 清理旧包: $SDK"
rm -rf "$SDK"

echo "==> 创建目录结构"
mkdir -p "$SDK/include/s421" \
         "$SDK/src" \
         "$SDK/third_party" \
         "$SDK/examples"

echo "==> 复制公共头文件（第三方 #include 用）"
cp "$ROOT"/include/s421/*.h "$SDK/include/s421/"

echo "==> 复制库源码（命令行工具 main.cpp 是内部调试用，不进 SDK）"
for f in "$ROOT"/src/*.cpp; do
    [ "$(basename "$f")" = "main.cpp" ] && continue
    cp "$f" "$SDK/src/"
done

echo "==> 复制第三方依赖（pugixml + 内嵌 zlib，均源码集成）"
cp -r "$ROOT/third_party/pugixml" "$SDK/third_party/"
cp -r "$ROOT/third_party/zlib"    "$SDK/third_party/"

echo "==> 复制示例"
cp "$ROOT/examples/demo.cpp" "$SDK/examples/"

echo "==> 复制构建脚本与使用说明"
cp "$ROOT/CMakeLists.txt" "$SDK/"
cp "$ROOT/README.md"      "$SDK/"

echo ""
echo "✅ SDK 打包完成 → $SDK"
echo ""
echo "交付给第三方后，他们只需："
echo "    cmake -B build -DCMAKE_BUILD_TYPE=Release"
echo "    cmake --build build"
echo "即可得到静态库 + 命令行工具 + 示例。"
