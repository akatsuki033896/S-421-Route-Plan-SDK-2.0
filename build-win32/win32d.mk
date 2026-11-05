#====================================================================
#  win32d.mk - s421 WIN32 (MSVC) Debug 构建（带调试信息）
#
#  用法：
#    make -f win32d.mk          # 构建 debug 版 s421.lib（/Od /Zi /MDd）
#====================================================================

# makecore 根目录。默认 E:/makecore/（带尾斜杠）。
ifndef MAKEROOT
MAKEROOT := E:/makecore/
endif

MAKECORE = mk_core.vs

# ---------------- 变体配置 ----------------
DEBUG     := TRUE      # mk_core.vs 据此加 /Od /Zi /MDd /DEBUG

OUTPUTDIR := output/win32d
OBJDIR    := obj/win32d
OUTPUTFILE := s421.lib
BUILDDIR  := build/win32d

# ==================== 包含业务配置 + makecore 核心 ====================
.DEFAULT_GOAL := all
-include common.mk
