#====================================================================
#  armv7-a-extzlib.mk - s421 ARM 构建（不含 zlib，外部提供）
#
#  与 armv7-a.mk 的差异：
#    - 不内嵌 zlib：libs421.a 不含 zlib 的 .o（zlib 由调用方提供）
#      → 不定义 Z_PREFIX，s421 用标准 inflate，匹配调用方的 zlib
#    - pugixml 仍内嵌（s421_pugi 隔离）
#  只产 libs421.a（不编 tool/demo）。
#
#  用法（Git Bash，工具链由 mk_core.gcc 自动选定）：
#    cd build-rk3128 && make -f armv7-a-extzlib.mk
#====================================================================

ifndef MAKEROOT
MAKEROOT := E:/makecore
endif

MAKECORE = mk_core.gcc
ARMGCC   = TRUE

# Release
DEBUG        := FALSE
OPTIMIZATION := 2

OBJDIR     := obj/armv7-a-extzlib
OUTPUTDIR  := output/armv7-a-extzlib
OUTPUTFILE := libs421.a
BUILDDIR   := build/armv7-a-extzlib

.DEFAULT_GOAL := all

# ---------------- 项目路径 ----------------
SDKROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)

# 源码：s421 + pugixml（不含 zlib —— 外部提供，不进 libs421.a）
SOURCEDIR := $(SDKROOT)/src/ $(SDKROOT)/third_party/pugixml/

# 头：s421 + pugixml + zlib.h(编译 include 用) + 生成的 zconf.h
INCDIR := $(SDKROOT)/include/ $(SDKROOT)/third_party/pugixml/ $(SDKROOT)/third_party/zlib/ $(BUILDDIR)/

C_PREDEF += LINUX _LINUX LINUX_ARMV7A

# ---------------- RK3128 CPU / 浮点 ----------------
RK_ARCH_FLAGS := -march=armv7-a -mtune=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard -mthumb

# ---------------- 第三方符号隔离（仅 pugixml；不定义 Z_PREFIX）----------------
S421_VENDOR_RENAME := -Dpugi=s421_pugi
APPEND_CFLAGS += $(RK_ARCH_FLAGS) -fPIC -fvisibility=hidden -funwind-tables $(S421_VENDOR_RENAME)

# ---------------- C++ 标准 ----------------
CPPFLAGS += -std=c++1y

# ---------------- 输出 ----------------
TARGET_LIB := TRUE
EXCLUDE_FILES := main.cpp   # 排除 s421_tool 的 main()，不进库（库不应含 main）
TARGET     := $(OUTPUTDIR)/$(OUTPUTFILE)

# ==================== 引入 makecore 核心 ====================
-include $(MAKEROOT)$(MAKECORE)

# ==================== zconf.h 生成（放在 include mk_core 之后）====================
$(BUILDDIR)/zconf.h: $(SDKROOT)/third_party/zlib/zconf.h.cmakein
	@mkdir -p $(BUILDDIR)
	@echo "==> generating zconf.h (zlib config header)"
	@sed -e 's/^#cmakedefine Z_PREFIX$$/\/* #undef Z_PREFIX *\//g' \
	     -e 's/^#cmakedefine Z_HAVE_UNISTD_H$$/#define Z_HAVE_UNISTD_H/g' \
	     $< > $@

$(TARGET): $(BUILDDIR)/zconf.h
$(OBJECTS): $(BUILDDIR)/zconf.h
