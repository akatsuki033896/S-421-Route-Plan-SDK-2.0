#====================================================================
#  win32.mk - s421 WIN32 (MSVC) Release 构建
#
#  对齐 build-linux / build-rk3128 风格：
#    MAKECORE = mk_core.vs -> makecore 用 cl.exe / link.exe (MSVC) 编译打包。
#
#  前置条件：需在 MSVC 环境下运行 make（cl.exe 在 PATH）。两种方式：
#    1) "Developer Command Prompt for VS 2022" 里 cd 到本目录，make -f win32.mk
#    2) 普通 cmd 里先 call vcvarsall，再 make：
#         call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
#         make -f win32.mk
#
#  用法：
#    make -f win32.mk            # 构建 release 版 s421.lib（x64）
#    make -f win32.mk clean
#====================================================================

# makecore 根目录（含 mk_core.vs / mk_core.mk）。默认 E:/makecore/（带尾斜杠）。
ifndef MAKEROOT
MAKEROOT := E:/makecore/
endif

# make 核心：mk_core.vs 提供 MSVC 工具链 + 编译/打包规则
MAKECORE = mk_core.vs

# ---------------- 变体配置（先于 common.mk 设置）----------------
# DEBUG 不定义 -> Release（mk_core.vs 默认 /O2 /MD）

# 输出目录 / 产物名
OUTPUTDIR := output/win32
OBJDIR    := obj/win32
OUTPUTFILE := s421.lib
# BUILDDIR 放生成的 zconf.h（行内勿加注释，否则尾随空格污染变量）
BUILDDIR  := build/win32

# ==================== 包含业务配置 + makecore 核心 ====================
.DEFAULT_GOAL := all
-include common.mk

# ---------------- 额外目标：WIN32 可执行（s421_tool.exe / s421_demo.exe）----------------
# mk_core.vs 的 all 默认只产 s421.lib（TARGET_LIB=TRUE）；这里追加可执行目标。
# cl 不带 /c -> 一次完成「编译 + 链接」；/Fe 指定 exe；/link 指定 s421.lib。
# main.cpp/demo.cpp 看到的 s421 头含 pugi:: 声明，须与库一致 /Dpugi=s421_pugi。
# ARG_INCDIR 由 mk_core.vs 生成（含 include/、pugixml/、zlib/、BUILDDIR），复用之。
EXE_CFLAGS := /EHsc /std:c++14 /MD /Dpugi=s421_pugi /utf-8 /D_CRT_SECURE_NO_DEPRECATE /D_CRT_NONSTDC_NO_DEPRECATE
EXTRA_TARGETS := $(OUTPUTDIR)/s421_tool.exe $(OUTPUTDIR)/s421_demo.exe
all: $(EXTRA_TARGETS)

$(OUTPUTDIR)/s421_tool.exe: $(SDKROOT)/src/main.cpp $(TARGET) | $(OUTPUTDIR)
	@echo [link] s421_tool.exe
	@cl $(EXE_CFLAGS) $(ARG_INCDIR) $(call Cmd_GetWin32Path,$<) /Fe$(call Cmd_GetWin32Path,$@) /link $(call Cmd_GetWin32Path,$(TARGET))
	@echo     $(notdir $@) built

$(OUTPUTDIR)/s421_demo.exe: $(SDKROOT)/examples/demo.cpp $(TARGET) | $(OUTPUTDIR)
	@echo [link] s421_demo.exe
	@cl $(EXE_CFLAGS) $(ARG_INCDIR) $(call Cmd_GetWin32Path,$<) /Fe$(call Cmd_GetWin32Path,$@) /link $(call Cmd_GetWin32Path,$(TARGET))
	@echo     $(notdir $@) built
