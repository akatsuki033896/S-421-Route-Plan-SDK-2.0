#====================================================================
#  sdk.mk - s421-sdk 的 Win32 库构建（Debug /MDd /IDL2 /不含 zlib）
#
#  供「Debug + 自带 zlib（zlibstat.lib）」的 MSVC 调用方集成：
#    - Debug /MDd /IDL2（匹配调用方 Debug 工程）
#    - 不含 zlib（由调用方 zlib 提供）→ 不定义 Z_PREFIX，用标准 inflate
#    - pugixml 内嵌（s421_pugi 隔离）
#    - EXCLUDE_FILES main.cpp（库不含 main）
#  只产 s421.lib（不编 tool/demo）。
#
#  前置：VS2022 环境（x64 或 x86）：
#    call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
#
#  用法（命令行参数化 arch 目录，x64/x86 各一份，互不覆盖）：
#    make -f sdk.mk OUTPUTDIR=output/sdk_x64 OBJDIR=obj/sdk_x64 BUILDDIR=build/sdk_x64
#    make -f sdk.mk OUTPUTDIR=output/sdk_x86 OBJDIR=obj/sdk_x86 BUILDDIR=build/sdk_x86
#====================================================================

ifndef MAKEROOT
MAKEROOT := E:/makecore/
endif

MAKECORE = mk_core.vs
DEBUG    := TRUE      # mk_core.vs 据此加 /Od /Zi /MDd

OUTPUTDIR  := output/sdk
OBJDIR     := obj/sdk
OUTPUTFILE := s421.lib
BUILDDIR   := build/sdk

.DEFAULT_GOAL := all

SDKROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)

# mk_core.vs 的 recipe 用 cmd 语法
SHELL := cmd.exe
.SHELLFLAGS := /c

# 源码：s421 + pugixml（不含 zlib —— 外部提供，不进 s421.lib）
SOURCEDIR := $(SDKROOT)/src/ $(SDKROOT)/third_party/pugixml/

# 头：s421 + pugixml + zlib.h(编译 include 用) + 生成的 zconf.h
INCDIR := $(SDKROOT)/include/ $(SDKROOT)/third_party/pugixml/ $(SDKROOT)/third_party/zlib/ $(BUILDDIR)/

# C++17；IDL2（Debug STL 调试级别）；pugixml 隔离。不定义 Z_PREFIX（用标准 inflate 匹配调用方 zlib）。
# 注：/MDd 由 mk_core.vs 的 DEBUG 分支自动加
CFLAGS += /std:c++17 /D_ITERATOR_DEBUG_LEVEL=2 /Dpugi=s421_pugi /utf-8 /EHsc /D_CRT_SECURE_NO_DEPRECATE /D_CRT_NONSTDC_NO_DEPRECATE

TARGET_LIB := TRUE
EXCLUDE_FILES := main.cpp   # 排除 s421_tool 的 main()，不进库

# ==================== 引入 makecore 核心 ====================
-include $(MAKEROOT)$(MAKECORE)

# ==================== zconf.h（s421 编译 include zlib.h 需要；预置 copy）====================
$(BUILDDIR)/zconf.h: $(SDKROOT)/build-win32/zconf.h
	@-if not exist $(call Cmd_GetWin32Path,$(BUILDDIR)) mkdir $(call Cmd_GetWin32Path,$(BUILDDIR))
	@copy /Y $(call Cmd_GetWin32Path,$<) $(call Cmd_GetWin32Path,$@) >nul

all: $(BUILDDIR)/zconf.h
$(TARGET): $(BUILDDIR)/zconf.h
