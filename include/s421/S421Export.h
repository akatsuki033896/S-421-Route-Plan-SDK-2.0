#pragma once

/**
 * @file S421Export.h
 * @brief 符号导出 / 可见性控制。
 *
 * 配合 CMake 的构建模式：
 *   - 静态库（默认）   → CMake PUBLIC 定义 `S421_STATIC_DEFINE` → `S421_API` 为空
 *   - 动态库-编译方    → CMake PRIVATE 定义 `S421_EXPORTS`     → `S421_API` = dllexport
 *   - 动态库-使用方    → 未定义上述宏                          → `S421_API` = dllimport
 *
 * @note 当前所有公开类尚未逐个标注 `S421_API`（静态库下不需要）；本头仅把机制备好，
 *       将来切换为动态库时，给公开类/函数加上 `S421_API` 前缀即可，无需再回头搭机制。
 */

#if defined(S421_STATIC_DEFINE)
    /// 静态库：符号随调用方一起编译，无需任何导出标记
    #define S421_API
#else
    #if defined(_WIN32) || defined(__CYGWIN__)
        #if defined(S421_EXPORTS)
            #define S421_API __declspec(dllexport)
        #else
            #define S421_API __declspec(dllimport)
        #endif
    #else
        #define S421_API __attribute__((visibility("default")))
    #endif
#endif
