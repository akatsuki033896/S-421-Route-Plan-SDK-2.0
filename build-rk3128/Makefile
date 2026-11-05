#====================================================================
#  Makefile - s421 RK3128 构建入口
#
#  参考 build-linux/Makefile：默认走 Release（armv7-a.mk）。
#
#  用法：
#    make                # 默认 Release（armv7-a）
#    make debug          # Debug（armv7-a_d）
#    make info           # 查看配置
#    make clean          # 清理 release 中间产物
#    make clean-debug    # 清理 debug 中间产物
#    make clean-all      # 清理所有
#====================================================================

# 默认目标：Release
DEFAULT_MAKEFILE := armv7-a.mk

# 透传 make 的 goal 与命令行变量给子 makefile
.DEFAULT_GOAL := default

%::
	@$(MAKE) --no-print-directory -f $(DEFAULT_MAKEFILE) $@

default:
	@$(MAKE) --no-print-directory -f $(DEFAULT_MAKEFILE) all

debug:
	@$(MAKE) --no-print-directory -f armv7-a_d.mk all

info:
	@$(MAKE) --no-print-directory -f $(DEFAULT_MAKEFILE) info

clean:
	@$(MAKE) --no-print-directory -f $(DEFAULT_MAKEFILE) clean

clean-debug:
	@$(MAKE) --no-print-directory -f armv7-a_d.mk clean

clean-all: clean clean-debug
	@rm -rf build/
