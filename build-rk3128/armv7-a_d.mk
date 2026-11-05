#====================================================================
#  armv7-a_d.mk - s421 RK3128 Debug 构建（带 -g，不 strip）
#
#  对齐 build-linux/armv7-a_d.mk：Debug 产物保留调试符号，便于在板子上 gdb。
#
#  用法：
#    make -f armv7-a_d.mk
#====================================================================

# makecore 根目录（含 mk_core.gcc / mk_core.mk / gcc-armhf/）。默认 E:/makecore。
ifndef MAKEROOT
MAKEROOT := E:/makecore
endif

# make 核心：mk_core.gcc 提供 ARM GCC 工具链 + 编译/打包规则
MAKECORE = mk_core.gcc
ARMGCC   = TRUE
# Debug 版只 ranlib 生成符号索引（GCC 4.9 的 ar 不自动建），不 strip 保留调试符号
AFTER_BUILD = $(TOOL_CHAIN_PREFIX)ranlib $(OUTPUTDIR)/$(OUTPUTFILE)

# ---------------- 变体配置（先于 common.mk 设置）----------------
DEBUG        := TRUE       
OPTIMIZATION := 0

# 输出目录 / 产物名
OBJDIR    := obj/armv7-a_d
OUTPUTDIR := output/armv7-a_d
OUTPUTFILE := libs421.a
BUILDDIR  := build/armv7-a_d

# ==================== 包含业务配置 + makecore 核心 ====================
# 显式默认目标 = all（mk_core.mk 定义 all，这里确保不被 zconf.h 等规则抢占）
.DEFAULT_GOAL := all
-include common.mk

# ---------------- 额外目标：ARM 可执行（debug 版，不 strip）----------------
EXTRA_TARGETS := $(OUTPUTDIR)/s421_tool $(OUTPUTDIR)/s421_demo
all: $(EXTRA_TARGETS)

TOOL_EXE_LDFLAGS := -static-libstdc++ -lpthread

$(OUTPUTDIR)/s421_tool: $(SDKROOT)/src/main.cpp $(TARGET) | $(OUTPUTDIR)
	@echo "==> linking s421_tool (ARM executable, debug)"
	@$(CPP) $(CPPFLAGS) $(CFLAGS) $< -L$(OUTPUTDIR) -ls421 $(TOOL_EXE_LDFLAGS) -o $@
	@echo "    $(notdir $@): $$(wc -c < $@) bytes"

$(OUTPUTDIR)/s421_demo: $(SDKROOT)/examples/demo.cpp $(TARGET) | $(OUTPUTDIR)
	@echo "==> linking s421_demo (ARM executable, debug)"
	@$(CPP) $(CPPFLAGS) $(CFLAGS) $< -L$(OUTPUTDIR) -ls421 $(TOOL_EXE_LDFLAGS) -o $@
	@echo "    $(notdir $@): $$(wc -c < $@) bytes"
