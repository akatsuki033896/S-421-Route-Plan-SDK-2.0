cmake_minimum_required(VERSION 3.15)
project(s421_sdk VERSION 2.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")  # 静态库
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")  # 动态库
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")  # 可执行

option(BUILD_SHARED_LIBS  "build dll" OFF) # OFF: build lib, ON: build dll
option(S421_BUILD_TOOL    "build s421_tool" ON)
option(S421_BUILD_EXAMPLES "build s421_demo" ON)
option(S421_INSTALL       "generate install target(lib and headers)" ON)

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
add_library(s421::s421 ALIAS s421)

# 公共头：调用方链接后自动拿到 include/ 路径
target_include_directories(s421
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/pugixml   # pugixml.cpp 内部 #include "pugixml.hpp"
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib      # zlib.h
        ${CMAKE_CURRENT_BINARY_DIR}                        # 生成的 zconf.h
)

# 内嵌zlib和pugi::xml
target_compile_definitions(s421 PRIVATE pugi=s421_pugi Z_PREFIX)

# 编译期日志级别
target_compile_definitions(s421 PUBLIC S421_LOG_LEVEL=${S421_LOG_LEVEL})

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

# S421-demo
if(S421_BUILD_EXAMPLES)
    add_executable(s421_demo examples/demo.cpp)
    target_link_libraries(s421_demo PRIVATE s421)
    if(MSVC)
        target_compile_options(s421_demo PRIVATE /utf-8)
    endif()
endif()

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
