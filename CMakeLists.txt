cmake_minimum_required(VERSION 3.15)
project(s421_sdk VERSION 2.0.0 LANGUAGES C CXX)

# ============================================================
#  S-421 SDK 跨平台构建
#
#  产物：
#    - 静态库 s421   （Windows: s421.lib / Linux: libs421.a）
#    - 命令行工具 s421_tool  （可选，S421_BUILD_TOOL）
#    - 示例程序 s421_demo    （可选，S421_BUILD_EXAMPLES）
#
#  默认静态库；切动态库：cmake -DBUILD_SHARED_LIBS=ON
# ============================================================

# ---- C++ 标准 ----
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 位置无关代码：静态库将来也能被链进 .so/.dll
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ---- 输出目录归一化（便于打包分发）----
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")  # 静态库
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")  # 动态库
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")  # 可执行

# ---- 构建选项 ----
option(BUILD_SHARED_LIBS  "构建为动态库（默认 OFF=静态库；ON 则出 .dll/.so）" OFF)
option(S421_BUILD_TOOL    "构建 s421_tool 命令行工具" ON)
option(S421_BUILD_EXAMPLES "构建示例程序 s421_demo" ON)
option(S421_INSTALL       "生成 install 目标（库 + 头文件，便于分发）" ON)

# 编译期日志级别（见 include/s421/S421Logger.h 的 S421_LOG_LEVEL）
set(S421_LOG_LEVEL 4 CACHE STRING "编译期日志级别: 0=全关 .. 4=全开（默认运行时过滤）")

# ============================================================
#  依赖：zlib（源码内嵌，随项目一起编译）
#    源码在 third_party/zlib（1.3.1，zlib license）。
#    内嵌而非 find_package：
#      ① MSVC/MinGW/Linux 各编译器都用同一份 zlib，ABI 自洽；
#      ② 第三方无需安装 zlib 或 vcpkg，拿到 sdk/ 即可开箱编译；
#      ③ 规避个别机器上系统残缺 zlib.h（stub）对 #include <zlib.h> 的污染。
# ============================================================
# zlib：源码直接编进 s421（见下方 ZLIB_SOURCES），使 s421.lib / libs421.a 自包含。
# zlib 唯一需要配置的是 zconf.h——模板 zconf.h.cmakein 只有 Z_PREFIX / Z_HAVE_UNISTD_H
# 两个占位符，其余平台判断都由预处理器自己完成。照 zlib 官方 CMakeLists 复刻这一步。
include(CheckIncludeFile)
check_include_file(unistd.h Z_HAVE_UNISTD_H)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib/zconf.h.cmakein"
    "${CMAKE_CURRENT_BINARY_DIR}/zconf.h"
    @ONLY)

# zlib 全部源码（与 s421 一起编译）
set(ZLIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib)
set(ZLIB_SOURCES
    ${ZLIB_DIR}/adler32.c
    ${ZLIB_DIR}/compress.c
    ${ZLIB_DIR}/crc32.c
    ${ZLIB_DIR}/deflate.c
    ${ZLIB_DIR}/gzclose.c
    ${ZLIB_DIR}/gzlib.c
    ${ZLIB_DIR}/gzread.c
    ${ZLIB_DIR}/gzwrite.c
    ${ZLIB_DIR}/inflate.c
    ${ZLIB_DIR}/infback.c
    ${ZLIB_DIR}/inftrees.c
    ${ZLIB_DIR}/inffast.c
    ${ZLIB_DIR}/trees.c
    ${ZLIB_DIR}/uncompr.c
    ${ZLIB_DIR}/zutil.c
)

# ============================================================
#  库 target: s421
# ============================================================
add_library(s421
    src/S421Parser.cpp
    src/S421Serializer.cpp
    src/S421Validator.cpp
    src/S421Formatter.cpp
    src/MiniZip.cpp
    src/MiniUnzip.cpp
    third_party/pugixml/pugixml.cpp
    ${ZLIB_SOURCES}
)
add_library(s421::s421 ALIAS s421)   # 命名空间别名，方便集成方

# 公共头：调用方链接后自动拿到 include/ 路径
target_include_directories(s421
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/pugixml   # pugixml.cpp 内部 #include "pugixml.hpp"
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib      # zlib.h
        ${CMAKE_CURRENT_BINARY_DIR}                        # 生成的 zconf.h
)

# zlib 已作为源码编进 s421（见 ZLIB_SOURCES），不再链接 zlibstatic；
# s421.lib / libs421.a 因此自包含，调用方链接时无需额外 zlib。

# 第三方符号隔离（PRIVATE：只编进 s421，不传播给调用方）
# zlib + pugixml 以源码编入 s421。若保持原名导出（inflate/deflate/pugi::*），
# 调用方一旦再链接自己的 zlib/pugixml（另一版本）就会 multiple definition 冲突。
# 用符号重命名把第三方符号改成 s421 私有名：
#   pugi=s421_pugi  : pugixml 的 namespace pugi → s421_pugi
#   Z_PREFIX        : zlib 的 inflate→z_inflate / deflate→z_deflate ...
# PRIVATE 保证只对 s421（含其内嵌的 zlib/pugixml 源码）生效；调用方完全无感
# （s421 公开 API 不涉及 pugi::，仅 private 方法用，调用方不引用）。
target_compile_definitions(s421 PRIVATE pugi=s421_pugi Z_PREFIX)

# 编译期日志级别（PUBLIC：保证库与调用方一致）
target_compile_definitions(s421 PUBLIC S421_LOG_LEVEL=${S421_LOG_LEVEL})

# MSVC：源文件含 UTF-8 中文（/utf-8）；zlib 用旧 CRT 函数，屏蔽其弃用警告
if(MSVC)
    target_compile_options(s421 PRIVATE /utf-8)
    target_compile_definitions(s421 PRIVATE _CRT_SECURE_NO_DEPRECATE _CRT_NONSTDC_NO_DEPRECATE)
endif()

# 符号可见性控制（对应 include/s421/S421Export.h 中的 S421_API）
if(BUILD_SHARED_LIBS)
    # 编译库时：标记为"正在导出"
    target_compile_definitions(s421 PRIVATE S421_EXPORTS)
    # GCC/Clang：默认隐藏符号，仅显式标注的导出
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(s421 PRIVATE -fvisibility=hidden)
    endif()
else()
    # 静态库：让所有链接 s421 的调用方都拿到 S421_STATIC_DEFINE → S421_API 为空
    target_compile_definitions(s421 PUBLIC S421_STATIC_DEFINE)
endif()

# ============================================================
#  命令行工具 target: s421_tool（可选）
# ============================================================
if(S421_BUILD_TOOL AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
    add_executable(s421_tool src/main.cpp)
    target_link_libraries(s421_tool PRIVATE s421)
    # main.cpp include 的 s421 头里有 pugi:: 声明（private 方法），需与库一致重命名，
    # 否则头里看到的 pugi:: 与库内的 s421_pugi:: 符号不一致。
    target_compile_definitions(s421_tool PRIVATE pugi=s421_pugi)
    if(MSVC)
        target_compile_options(s421_tool PRIVATE /utf-8)
    endif()
endif()

# ============================================================
#  示例 target: s421_demo（可选）
# ============================================================
if(S421_BUILD_EXAMPLES)
    add_executable(s421_demo examples/demo.cpp)
    target_link_libraries(s421_demo PRIVATE s421)
    if(MSVC)
        target_compile_options(s421_demo PRIVATE /utf-8)
    endif()
endif()

# ============================================================
#  安装规则（库 + 头文件）
# ============================================================
if(S421_INSTALL)
    include(GNUInstallDirs)
    install(TARGETS s421
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()
