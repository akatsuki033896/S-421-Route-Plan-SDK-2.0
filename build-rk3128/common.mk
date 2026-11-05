#====================================================================
#  common.mk - s421 RK3128 交叉编译业务配置
#
#  对齐 build-linux 体系：工具链与编译/打包规则由 makecore 的 mk_core.gcc
#  （通过 -include $(MAKEROOT)$(MAKECORE)）提供；本文件只负责 s421 专属的
#  业务变量：源码目录、头文件、CPU 浮点 flags、C++ 标准、zconf.h 生成。
#
#  工具链选定：上层（armv7-a.mk 等）设 MAKECORE=mk_core.gcc + ARMGCC=TRUE，
#  mk_core.gcc 据此把 $(MAKEROOT)gcc-armhf/bin 加进 PATH，并设
#  TOOL_CHAIN_PREFIX=arm-linux-gnueabihf-（hard-float）。不硬编码任何 gcc 路径。
#
#  变量契约（mk_core.mk 期望本文件先设好）：
#    SOURCEDIR / INCDIR / C_PREDEF / LIBS / LIBDIR
#    OBJDIR / OUTPUTDIR / OUTPUTFILE / TARGET_LIB
#    OPTIMIZATION / DEBUG / APPEND_CFLAGS / APPEND_LDFLAGS / AFTER_BUILD
#====================================================================

# ---------------- 项目路径（相对本目录）----------------
# SDKROOT 指向 s421 项目根（build-rk3128 的上一级）
SDKROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)

# 源码目录：s421 库源 + 第三方（zlib / pugixml，均源码内嵌）
SOURCEDIR := $(SDKROOT)/src/ $(SDKROOT)/third_party/zlib/ $(SDKROOT)/third_party/pugixml/

# 头文件搜索目录（mk_core.mk 会自动追加 $(MAKEROOT)include）
INCDIR := $(SDKROOT)/include/ $(SDKROOT)/third_party/pugixml/ $(SDKROOT)/third_party/zlib/ $(BUILDDIR)/

# C/C++ 预定义宏（对齐 build-linux 的 LINUX _LINUX LINUX_ARMV7A 体系）
C_PREDEF += LINUX _LINUX LINUX_ARMV7A

# ---------------- RK3128 CPU / 浮点 ----------------
# RK3128 = 4× Cortex-A7（ARMv7-A）+ NEON + VFPv4，hard-float ABI，glibc Linux。
# 通过 APPEND_CFLAGS 注入（mk_core.mk 会把 APPEND_CFLAGS 追加进 CFLAGS）。
# 注：ARCH 变量由 mk_core.gcc 的 ifdef ARCH 分支处理 -march，但浮点/fpu/ABI
#     需显式给出，故统一放 APPEND_CFLAGS。
RK_ARCH_FLAGS := -march=armv7-a -mtune=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard -mthumb

# ---------------- 第三方符号隔离（关键）----------------
# zlib + pugixml 以源码编入 libs421.a。若它们的符号（inflate/deflate/pugi::*）保持
# 原名导出，应用层一旦再链接自己的 zlib/pugixml（另一版本）就会 multiple definition 冲突。
# 静态库下 -fvisibility=hidden 无效（HIDDEN 仍是 GLOBAL binding，链接仍冲突），
# objcopy --localize 又会断开库内 s421 对 zlib 的跨 .o 引用。
# 故用符号重命名：编译时给所有源文件注入宏，把第三方符号改成 s421 私有名：
#   -Dpugi=s421_pugi  : pugixml 的 namespace pugi → s421_pugi（符号 _ZN4pugi→_ZN9s421_pugi）
#   -DZ_PREFIX        : zlib 的 inflate→z_inflate / deflate→z_deflate / crc32→z_crc32 ...
# s421 公开 API（parseFile 等）不涉及 pugi::（仅 private 方法用，应用层不引用），故对调用方透明。
S421_VENDOR_RENAME := -Dpugi=s421_pugi -DZ_PREFIX
APPEND_CFLAGS += $(RK_ARCH_FLAGS) -fPIC -fvisibility=hidden -funwind-tables $(S421_VENDOR_RENAME)

# ---------------- C++ 标准 ----------------
# 源码已 C++14 化（s421::optional / s421::fs）。
# mk_core.gcc 用 Cmd_CppCompile 编 .cpp（命令里含 $(CPPFLAGS)），故 C++ 标准放
# CPPFLAGS（只对 C++ 生效，不影响 .c 的 -std=c99）。
# GCC 4.9 的 C++14 标志是 -std=c++1y（GCC 5+ 才有 -std=c++14）；用 c++1y 两者兼容。
CPPFLAGS += -std=c++1y

# 优化等级由上层（armv7-a.mk）设 OPTIMIZATION；mk_core.mk 默认 -O$(OPTIMIZATION)。

# ---------------- 输出 ----------------
# 上层已设 OUTPUTFILE / OBJDIR / OUTPUTDIR / BUILDDIR。
# TARGET_LIB：让 mk_core.mk 走 AR 打包（Cmd_MakeLib），而非链接可执行。
TARGET_LIB := TRUE
# 排除 s421_tool 的 main()，不进库（库不应含 main；上层 armv7-a*.mk 仍把 main.cpp
# 单独链成 s421_tool/s421_demo，不经过此 SOURCEDIR 收集）。须在 include mk_core 之前。
EXCLUDE_FILES := main.cpp
TARGET     := $(OUTPUTDIR)/$(OUTPUTFILE)

# ==================== 引入 makecore 核心 ====================
# mk_core.gcc：按 ARMGCC=TRUE 选定 arm-linux-gnueabihf- 工具链，
#              定义 CC/CPP/AR/STRIP 等 + 注入 -fsigned-char/-Wall；
#              末尾 -include mk_core.mk 提供完整编译/归档/链接规则。
-include $(MAKEROOT)$(MAKECORE)

# ==================== zconf.h 生成（放在 include mk_core 之后）====================
# 放在此处是为了不抢默认目标（mk_core.mk 的 all 是第一个目标规则）。
# zlib 唯一需要配置的头。从模板 zconf.h.cmakein 生成（等价 CMake configure_file）。
# ARM/Linux 有 unistd.h → 定义 Z_HAVE_UNISTD_H；Z_PREFIX 不需要（注释掉）。
$(BUILDDIR)/zconf.h: $(SDKROOT)/third_party/zlib/zconf.h.cmakein
	@mkdir -p $(BUILDDIR)
	@echo "==> generating zconf.h (zlib config header)"
	@sed -e 's/^#cmakedefine Z_PREFIX$$/\/\* #undef Z_PREFIX \*\//g' \
	     -e 's/^#cmakedefine Z_HAVE_UNISTD_H$$/#define Z_HAVE_UNISTD_H/g' \
	     $< > $@

# 让 TARGET（libs421.a）依赖 zconf.h（zlib 源码间接需要）
$(TARGET): $(BUILDDIR)/zconf.h

# 让每个 .o 也依赖 zconf.h：zlib 源码与 zlib.h 间接 #include "zconf.h"，
# 必须在编译 .o 之前生成好（OBJECTS 由 mk_core.mk 定义，故此行放在 include 之后）。
$(OBJECTS): $(BUILDDIR)/zconf.h

# ==================== info：查看 mk_core 选定的工具链与配置 ====================
# 注意：echo 不用双引号包裹值——MAKEROOT 在 Windows 上可能含反斜杠（D:\makecore\），
# 双引号内结尾 \ 会转义闭合引号导致 sh 报 EOF。用单引号（sh 单引号不转义）。
.PHONY: info
info:
	@echo 'MAKEROOT         = $(MAKEROOT)'
	@echo 'MAKECORE         = $(MAKECORE)  (ARMGCC=$(ARMGCC))'
	@echo 'TOOL_CHAIN_PREFIX= $(TOOL_CHAIN_PREFIX)'
	@echo 'C++ standard     = $(CPPFLAGS)'
	@echo 'ARCH flags       = $(RK_ARCH_FLAGS)'
	@echo 'SDKROOT          = $(SDKROOT)'
	@echo 'OBJDIR / OUTPUT  = $(OBJDIR) / $(OUTPUTDIR)/$(OUTPUTFILE)'
	@echo 'TARGET           = $(TARGET)'
	@echo '--- actual gcc version (selected by mk_core) ---'
	@-$(TOOL_CHAIN_PREFIX)gcc --version 2>/dev/null | head -1 || echo '(toolchain not in PATH)'
