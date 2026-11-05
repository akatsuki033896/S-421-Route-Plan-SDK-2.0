#====================================================================
#  common.mk - s421 WIN32 (MSVC) 编译业务配置
#
#  对齐 build-linux / build-rk3128 体系：工具链与编译/打包规则由 makecore 的
#  mk_core.vs（通过 -include $(MAKEROOT)$(MAKECORE)）提供；本文件只负责 s421 专属
#  业务变量：源码目录、头文件、C++ 标准、第三方符号隔离、zconf.h 生成。
#
#  工具链：mk_core.vs 调用 cl.exe / link.exe（MSVC）。需先在 VS 环境运行 make：
#    - 命令行：先 call vcvarsall.bat x64（或 x86）
#    - 或用 "Developer Command Prompt for VS 2022"
#
#  变量契约（mk_core.vs 期望本文件先设好）：
#    SOURCEDIR / INCDIR / C_PREDEF / OBJDIR / OUTPUTDIR / OUTPUTFILE / TARGET_LIB
#====================================================================

# ---------------- 项目路径（相对本目录）----------------
SDKROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)

# ---------------- Shell ----------------
# mk_core.vs 的 recipe 用 cmd 语法（@if not exist ... mkdir、rd /s/q），
# 必须让 make 用 cmd.exe 执行，而非 Git Bash 的 sh。需在 VS 环境下运行 make。
SHELL := cmd.exe
.SHELLFLAGS := /c

# 源码目录：s421 库源 + 第三方（zlib / pugixml，均源码内嵌）
SOURCEDIR := $(SDKROOT)/src/ $(SDKROOT)/third_party/zlib/ $(SDKROOT)/third_party/pugixml/

# 头文件搜索目录（mk_core.vs 会自动追加 $(MAKEROOT)include，并转 Win32 路径）
INCDIR := $(SDKROOT)/include/ $(SDKROOT)/third_party/pugixml/ $(SDKROOT)/third_party/zlib/ $(BUILDDIR)/

# ---------------- C++ 标准 + 第三方符号隔离 ----------------
# 源码已 C++14 化。MSVC 的 C++14 用 /std:c++14（VS2017+）。
# 第三方符号隔离（与 CMake / ARM Makefile 一致）：
#   pugi=s421_pugi : pugixml namespace pugi -> s421_pugi
#   Z_PREFIX       : zlib inflate->z_inflate ...
# mk_core.vs 用 /D 加预定义宏，故放 CFLAGS（CFLAGS 经 mk_core.vs 转成 cl 的 /D）。
CFLAGS += /std:c++14 /Dpugi=s421_pugi /DZ_PREFIX /utf-8 /EHsc  # /EHsc 启用 C++ 异常展开（项目用 throw；mk_core.vs 默认不带，否则抛异常时 terminate）

# 屏蔽 zlib 用旧 CRT 函数的弃用警告
CFLAGS += /D_CRT_SECURE_NO_DEPRECATE /D_CRT_NONSTDC_NO_DEPRECATE

# ---------------- 输出 ----------------
TARGET_LIB := TRUE   # 走 mk_core.vs 的 link /LIB 打包静态库
# 排除 s421_tool 的 main()，不进库（库不应含 main；上层 win32.mk/win32d.mk 仍把 main.cpp
# 单独链成 s421_tool.exe/s421_demo.exe，不经过此 SOURCEDIR 收集）。须在 include mk_core.vs 之前。
EXCLUDE_FILES := main.cpp

# ==================== 引入 makecore 核心 ====================
# mk_core.vs：MSVC 工具链（cl/link），定义 TARGET 规则、OBJDIR/OUTPUTDIR 创建等。
-include $(MAKEROOT)$(MAKECORE)

# ==================== zconf.h（放在 include mk_core.vs 之后）====================
# 放在此处：① 不抢默认目标；② 可用 mk_core.vs 定义的 Cmd_GetWin32Path。
# zlib 唯一需要配置的头。WIN32 版已预置在 build-win32/zconf.h
# （Z_PREFIX / Z_HAVE_UNISTD_H 都 /* #undef */：Z_PREFIX 通过编译命令行 /DZ_PREFIX 传；
#   Win32 无 unistd.h）。直接 copy，不依赖 sed——纯 Windows cmd 也能跑。
$(BUILDDIR)/zconf.h: $(SDKROOT)/build-win32/zconf.h
	@-if not exist $(call Cmd_GetWin32Path,$(BUILDDIR)) mkdir $(call Cmd_GetWin32Path,$(BUILDDIR))
	@echo [zconf] copying preset zconf.h (Win32)
	@copy /Y $(call Cmd_GetWin32Path,$<) $(call Cmd_GetWin32Path,$@) >nul

# 让 all 先生成 zconf.h（order-only：不因 zconf.h 变化重复构建库）
# mk_core.vs 的 all 依赖 $(TARGET)，TARGET 一次性 cl 编译所有源，编译前 zconf.h 必须就绪。
all: $(BUILDDIR)/zconf.h
$(TARGET): $(BUILDDIR)/zconf.h

