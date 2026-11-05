#!/usr/bin/env bash
# build-rk3128.sh - s421 SDK RK3128 (ARM Linux) 一键构建脚本
#
# 目标平台：RK3128（4× Cortex-A7 / ARMv7-A / NEON / VFPv4 / hard-float / glibc Linux）
# 工具链  ：arm-linux-gnueabihf-（由 makecore 的 mk_core.gcc 自动选定，
#          MAKECORE=mk_core.gcc + ARMGCC=TRUE → $(MAKEROOT)gcc-armhf/，Linaro GCC 4.9.4）
#
# 用法：
#   bash build-rk3128.sh              # 默认 Release（-O2，strip --strip-debug）
#   bash build-rk3128.sh debug        # Debug（-g -O0，保留符号）
#   bash build-rk3128.sh clean        # 清理 Release 中间产物
#   bash build-rk3128.sh clean-debug  # 清理 Debug 中间产物
#   bash build-rk3128.sh clean-all    # 清理全部
#   bash build-rk3128.sh info         # 查看 mk_core 选定的工具链 / 路径配置
#
# 自定义 makecore 路径（含 mk_core.gcc + gcc-armhf/）：
#   bash build-rk3128.sh MAKEROOT=/your/makecore
#
# 产物（build-rk3128/output/armv7-a/ 或 armv7-a_d/）：
#   libs421.a   - 静态库（自包含，zlib 已编入）
#   s421_tool   - 命令行工具（ARM 可执行，可在板子上跑）
#   s421_demo   - 调用示例（ARM 可执行）
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MK_DIR="$ROOT/build-rk3128"

# 第一个非赋值参数作为子命令
CMD="release"
for arg in "$@"; do
    case "$arg" in
        *=*) ;;                # 赋值参数（如 MAKEROOT=...），跳过
        *) CMD="$arg"; break;; # 第一个非赋值参数 = 子命令
    esac
done

# 透传所有赋值参数给 make（MAKEROOT=... 等）
MAKE_VARS=("$@")
# 过滤掉子命令本身，只保留赋值
FILTERED=()
for v in "${MAKE_VARS[@]}"; do
    case "$v" in
        release|debug|clean|clean-debug|clean-all|info) ;;  # 子命令，不透传
        *=*) FILTERED+=("$v") ;;
    esac
done

cd "$MK_DIR"

case "$CMD" in
    release|"")
        echo "==> 构建 RK3128 Release（armv7-a）"
        make -f armv7-a.mk "${FILTERED[@]}"
        ;;
    debug)
        echo "==> 构建 RK3128 Debug（armv7-a_d）"
        make -f armv7-a_d.mk "${FILTERED[@]}"
        ;;
    clean)
        echo "==> 清理 Release"
        make -f armv7-a.mk clean "${FILTERED[@]}"
        ;;
    clean-debug)
        echo "==> 清理 Debug"
        make -f armv7-a_d.mk clean "${FILTERED[@]}"
        ;;
    clean-all)
        echo "==> 清理全部（Release + Debug）"
        make -f armv7-a.mk clean "${FILTERED[@]}" || true
        make -f armv7-a_d.mk clean "${FILTERED[@]}" || true
        rm -rf "$MK_DIR/build"
        ;;
    info)
        make -f armv7-a.mk info "${FILTERED[@]}"
        ;;
    *)
        echo "未知子命令: $CMD" >&2
        echo "用法: bash build-rk3128.sh [release|debug|clean|clean-debug|clean-all|info] [VAR=value ...]" >&2
        exit 1
        ;;
esac

echo ""
echo "✅ 完成。"
if [[ "$CMD" == "release" || "$CMD" == "debug" || "$CMD" == "" ]]; then
    SUB=$([[ "$CMD" == "debug" ]] && echo "armv7-a_d" || echo "armv7-a")
    OUT="$MK_DIR/output/$SUB"
    echo "产物目录: $OUT"
    [[ -f "$OUT/libs421.a" ]] && echo "  libs421.a  ($(wc -c < "$OUT/libs421.a") 字节)"
    [[ -f "$OUT/s421_tool" ]] && echo "  s421_tool  ($(wc -c < "$OUT/s421_tool") 字节)"
    [[ -f "$OUT/s421_demo" ]] && echo "  s421_demo  ($(wc -c < "$OUT/s421_demo") 字节)"
fi
