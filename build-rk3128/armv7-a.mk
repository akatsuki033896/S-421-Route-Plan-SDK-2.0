#====================================================================
#  armv7-a.mk - s421 RK3128 Release 构建（ARMv7-A + hard-float）
#
#  对齐 build-linux/armv7-a.mk：
#    MAKECORE = mk_core.gcc + ARMGCC=TRUE → makecore 自动选定
#    $(MAKEROOT)gcc-armhf/bin 的 arm-linux-gnueabihf- 工具链。
#    Release 产物在打包后做 strip 去调试符号。
#
#  用法：
#    make -f armv7-a.mk                    # 构建 release 版 libs421.a
#    make -f armv7-a.mk MAKEROOT=/your/makecore   # 自定义 makecore 路径
#    make -f armv7-a.mk clean
#====================================================================

# makecore 根目录（含 mk_core.gcc / mk_core.mk / gcc-armhf/）。默认 E:/makecore。
ifndef MAKEROOT
MAKEROOT := E:/makecore
endif

# make 核心：mk_core.gcc 提供 ARM GCC 工具链 + 编译/打包规则
MAKECORE = mk_core.gcc
ARMGCC   = TRUE

# 打包后：先 ranlib 生成符号索引（GCC 4.9 的 ar 不自动建索引，链接器需要），
# 再 strip --strip-debug 去调试信息（保留符号表，否则链接器报 no index）。
# 注意：不能用裸 strip（=--strip-all），会删掉符号表导致库无法链接。
# 用 mk_core 选定的 $(TOOL_CHAIN_PREFIX) 前缀工具。
AFTER_BUILD = $(TOOL_CHAIN_PREFIX)ranlib $(OUTPUTDIR)/$(OUTPUTFILE) && $(TOOL_CHAIN_PREFIX)strip --strip-debug $(OUTPUTDIR)/$(OUTPUTFILE)

# ---------------- 变体配置（先于 common.mk 设置）----------------
DEBUG        := FALSE      
OPTIMIZATION := 2          

# 输出目录 / 产物名
OBJDIR    := obj/armv7-a
OUTPUTDIR := output/armv7-a
OUTPUTFILE := libs421.a
# BUILDDIR 放生成的 zconf.h（行内勿加注释，否则尾随空格污染变量）
BUILDDIR  := build/armv7-a

# ==================== 包含业务配置 + makecore 核心 ====================
# 显式默认目标 = all（mk_core.mk 定义 all，这里确保不被 zconf.h 等规则抢占）
.DEFAULT_GOAL := all
-include common.mk

# ---------------- 额外目标：ARM 可执行（s421_tool / s421_demo）----------------
# mk_core 的 all 默认只产 libs421.a（TARGET_LIB=TRUE）；这里追加可执行目标。
EXTRA_TARGETS := $(OUTPUTDIR)/s421_tool $(OUTPUTDIR)/s421_demo
all: $(EXTRA_TARGETS)

# 工具用 mk_core 已定义的 $(CXX=CPP) / $(STRIP)
TOOL_EXE_LDFLAGS := -static-libstdc++ -lpthread

$(OUTPUTDIR)/s421_tool: $(SDKROOT)/src/main.cpp $(TARGET) | $(OUTPUTDIR)
	@echo "==> linking s421_tool (ARM executable)"
	@$(CPP) $(CPPFLAGS) $(CFLAGS) $< -L$(OUTPUTDIR) -ls421 $(TOOL_EXE_LDFLAGS) -o $@
	@$(STRIP) $@
	@echo "    $(notdir $@): $$(wc -c < $@) bytes"

$(OUTPUTDIR)/s421_demo: $(SDKROOT)/examples/demo.cpp $(TARGET) | $(OUTPUTDIR)
	@echo "==> linking s421_demo (ARM executable)"
	@$(CPP) $(CPPFLAGS) $(CFLAGS) $< -L$(OUTPUTDIR) -ls421 $(TOOL_EXE_LDFLAGS) -o $@
	@$(STRIP) $@
	@echo "    $(notdir $@): $$(wc -c < $@) bytes"
