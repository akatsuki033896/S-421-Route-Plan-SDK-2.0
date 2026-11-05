#====================================================================
#  Makefile - s421 WIN32 构建入口
#
#  对齐 build-linux / build-rk3128 的 Makefile 入口风格：默认走 Release（win32.mk）。
#
#  前置：需在 MSVC 环境运行（cl.exe 在 PATH）。
#    - Developer Command Prompt for VS 2022，或
#    - 先 call vcvarsall.bat x64
#
#  用法：
#    make                # 默认 Release（win32）
#    make debug          # Debug（win32d）
#    make clean          # 清理 Release
#    make clean-debug    # 清理 Debug
#    make clean-all      # 清理全部
#====================================================================

DEFAULT_MAKEFILE := win32.mk
.DEFAULT_GOAL := default

%::
	@$(MAKE) --no-print-directory -f $(DEFAULT_MAKEFILE) $@

default:
	@$(MAKE) --no-print-directory -f $(DEFAULT_MAKEFILE) all

debug:
	@$(MAKE) --no-print-directory -f win32d.mk all

clean:
	@$(MAKE) --no-print-directory -f $(DEFAULT_MAKEFILE) clean

clean-debug:
	@$(MAKE) --no-print-directory -f win32d.mk clean

clean-all: clean clean-debug
	@-if exist build rd /s /q build
