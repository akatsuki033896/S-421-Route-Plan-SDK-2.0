#====================================================================
#  jadeecs.mk - s421 定制构建（给 JadeECS：VS2022 / x86 / Debug /MDd / C++17 / 外部 zlib）
#
#  与 win32.mk 的差异：
#    - x86（非 x64）
#    - Debug /MDd /IDL2（非 Release /MD）
#    - C++17（非 C++14）
#    - 不内嵌 zlib：s421.lib 不含 zlib 的 .obj（zlib 由调用方 zlibstat.lib 提供）
#      → 不定义 Z_PREFIX，s421 用标准 inflate 等，匹配 zlibstat
#    - pugixml 仍内嵌（s421_pugi 隔离；调用方一般无 pugixml）
#  只产 s421.lib（不编 tool/demo——第三方只要静态库）。
#
#  前置：VS2022 x86 环境
#    call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x86
#====================================================================

ifndef MAKEROOT
MAKEROOT := E:/makecore/
endif

MAKECORE = mk_core.vs
DEBUG    := TRUE      # mk_core.vs 据此加 /Od /Zi /MDd

OUTPUTDIR  := output/jadeecs
OBJDIR     := obj/jadeecs
OUTPUTFILE := s421.lib
BUILDDIR   := build/jadeecs

.DEFAULT_GOAL := all

SDKROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)

# mk_core.vs 的 recipe 用 cmd 语法
SHELL := cmd.exe
.SHELLFLAGS := /c

# 源码：s421 + pugixml（不含 zlib —— 外部提供，不进 s421.lib）
SOURCEDIR := $(SDKROOT)/src/ $(SDKROOT)/third_party/pugixml/

# 头：s421 + pugixml + zlib.h(编译 include 用) + 生成的 zconf.h
INCDIR := $(SDKROOT)/include/ $(SDKROOT)/third_party/pugixml/ $(SDKROOT)/third_party/zlib/ $(BUILDDIR)/

# C++17；IDL2（Debug STL 调试级别）；pugixml 隔离。不定义 Z_PREFIX。
# 注：/MDd 由 mk_core.vs 的 DEBUG 分支自动加（RUNTIME_LIB=MDd）
CFLAGS += /std:c++17 /D_ITERATOR_DEBUG_LEVEL=2 /Dpugi=s421_pugi /utf-8 /EHsc /D_CRT_SECURE_NO_DEPRECATE /D_CRT_NONSTDC_NO_DEPRECATE

TARGET_LIB := TRUE
EXCLUDE_FILES := main.cpp   # 排除 s421_tool 的 main()，不进库（库不应含 main）

# ==================== 引入 makecore 核心 ====================
-include $(MAKEROOT)$(MAKECORE)

# ==================== zconf.h（s421 编译 include zlib.h 需要；预置 copy）====================
$(BUILDDIR)/zconf.h: $(SDKROOT)/build-win32/zconf.h
	@-if not exist $(call Cmd_GetWin32Path,$(BUILDDIR)) mkdir $(call Cmd_GetWin32Path,$(BUILDDIR))
	@copy /Y $(call Cmd_GetWin32Path,$<) $(call Cmd_GetWin32Path,$@) >nul

all: $(BUILDDIR)/zconf.h
$(TARGET): $(BUILDDIR)/zconf.h
