#pragma once

/**
 * @file S421Fs.h
 * @brief 跨平台文件系统薄封装（替代 C++17 `std::filesystem`）。
 *
 * 背景：s421 早期用 `std::filesystem`（C++17）。为兼容只带 C++14 libstdc++ 的
 *       交叉编译器（如 RK3128 的 Linaro GCC 5.5），改用 POSIX / Win32 API
 *       手写两个最小封装。s421 全代码库只用到两件事：
 *         1. `mkdir -p`（创建输出目录树）
 *         2. 遍历目录中的普通文件（交换集目录兜底搜索）
 *       因此本头只暴露这两个能力，不追求等价 `std::filesystem`。
 *
 * 平台分支：
 *   - MSVC（`_WIN32` 且非 `__CYGWIN__`/MinGW）→ 用 `<windows.h>` 的
 *     `FindFirstFile` / `CreateDirectoryA` / `_mkdir`
 *   - 其余（Linux / ARM / MinGW / Cygwin）→ `<dirent.h>` + `<sys/stat.h>` 的
 *     `opendir`/`readdir` / `mkdir`
 *
 * 路径分隔符统一接受 `'/'`（Win32 的 `CreateDirectoryA`/`mkdir` 也接受 `'/'`），
 * 本头不做分隔符替换，调用方用 `'/'` 拼接即可。
 */

#include <string>
#include <functional>
#include <cerrno>       ///< errno / EEXIST

#if defined(_WIN32) && !defined(__CYGWIN__) && !defined(__MINGW32__) && !defined(__MINGW64__)
    // MSVC 原生 Win32
    #include <direct.h>     ///< _mkdir
    #include <windows.h>    ///< FindFirstFileA / CreateDirectoryA
#else
    // POSIX（Linux / ARM / MinGW / Cygwin）
    #include <sys/stat.h>   ///< mkdir / stat / S_ISREG
    #include <sys/types.h>
    #include <dirent.h>     ///< opendir / readdir / closedir
#endif

namespace s421 {
/**
 * @namespace s421::fs
 * @brief 跨平台文件系统薄封装。
 */
namespace fs {

/**
 * @brief 路径拼接：`a + "/" + b`（仅简单字符串拼接，不去重冗余斜杠）。
 *
 * 等价于原 `std::filesystem` 的 `operator/`。
 *
 * @param a 左侧路径；为空时直接返回 @p b。
 * @param b 右侧路径；为空时直接返回 @p a。
 * @return 拼接后的路径，例：`join("/out", "support")` → `"/out/support"`。
 */
inline std::string join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    // a 已以分隔符结尾则不重复加
    char last = a[a.size() - 1];
    if (last == '/' || last == '\\') return a + b;
    return a + "/" + b;
}

/**
 * @brief 递归创建目录（等价 `mkdir -p` / `fs::create_directories`）。
 *
 * 成功或目录已存在返回 `true`；失败返回 `false` 并（若 @p err 非空）写入原因。
 * 实现按 `'/'` 逐段创建，每段目标已存在（不论是否目录）即视为该段成功。
 *
 * @param path 目标目录路径（`'\'` 会被统一为 `'/'`）。
 * @param err  可选输出参数，失败时写入错误原因。
 * @return `true` = 成功或已存在；`false` = 创建失败。
 */
inline bool createDirectories(const std::string& path, std::string* err = nullptr) {
    if (path.empty()) return true;

    std::string p = path;
    // 统一分隔符为 '/'，便于分段
    for (std::size_t i = 0; i < p.size(); ++i)
        if (p[i] == '\\') p[i] = '/';

    // 逐段创建
    std::size_t pos = 0;
    while (pos < p.size()) {
        std::size_t next = p.find('/', pos);
        std::size_t segEnd = (next == std::string::npos) ? p.size() : next;
        if (segEnd == 0) { pos = 1; continue; }  // 开头的 '/'（绝对路径）

        std::string sub = p.substr(0, segEnd);

#if defined(_WIN32) && !defined(__CYGWIN__) && !defined(__MINGW32__) && !defined(__MINGW64__)
        // Win32 原生（MSVC）。_mkdir 已存在返回 EEXIST，视为成功。
        if (_mkdir(sub.c_str()) == 0) { /* 新建成功 */ }
        else if (errno == EEXIST) { /* 已存在，继续 */ }
        else {
            if (err) *err = std::string("createDirectories failed: ") + sub
                             + " (errno=" + std::to_string(errno) + ")";
            return false;
        }
#else
        // POSIX（Linux/ARM/MinGW/Cygwin）。权限 0777 受 umask 修正。
        if (mkdir(sub.c_str(), 0777) == 0) { /* 新建成功 */ }
        else if (errno == EEXIST) { /* 已存在，继续 */ }
        else {
            if (err) *err = std::string("createDirectories failed: ") + sub
                             + " (errno=" + std::to_string(errno) + ")";
            return false;
        }
#endif
        if (next == std::string::npos) break;
        pos = next + 1;
    }
    return true;
}

/**
 * @brief 遍历目录中的普通文件（非递归）。
 *
 * 对每个普通文件调用 `cb(完整路径)`。目录不存在 / 无权限访问时静默
 * （等价原 `fs::directory_iterator` 抛 `filesystem_error` 被 catch 忽略）。
 *
 * @param dir 待遍历目录。
 * @param cb  回调，参数为 `"dir/<name>"` 拼接后的完整路径。
 */
inline void forEachRegularFile(const std::string& dir,
                               const std::function<void(const std::string&)>& cb) {
    if (dir.empty()) return;

#if defined(_WIN32) && !defined(__CYGWIN__) && !defined(__MINGW32__) && !defined(__MINGW64__)
    // Win32：FindFirstFileA 遍历。掩码用 "dir/*"
    WIN32_FIND_DATAA fd;
    std::string pattern = dir + "/*";
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;  // 目录不存在/无权限，静默
    do {
        const char* name = fd.cFileName;
        if (name[0] == '.' && (name[1] == '\0' ||
                               (name[1] == '.' && name[2] == '\0'))) continue;  // . / ..
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;  // 只取文件
        cb(join(dir, name));
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    // POSIX：opendir/readdir。普通文件判断用 stat 的 S_ISREG。
    DIR* d = opendir(dir.c_str());
    if (!d) return;  // 目录不存在/无权限，静默
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        const char* name = e->d_name;
        if (name[0] == '.' && (name[1] == '\0' ||
                               (name[1] == '.' && name[2] == '\0'))) continue;  // . / ..
        std::string full = join(dir, name);
        struct stat st;
        if (stat(full.c_str(), &st) == 0 && S_ISREG(st.st_mode)) cb(full);
    }
    closedir(d);
#endif
}

} // namespace fs
} // namespace s421
