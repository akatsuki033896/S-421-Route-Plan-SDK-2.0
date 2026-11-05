/**
 * @file MiniUnzip.h
 * @brief 最小 ZIP 文件读取器（纯 C++17 + zlib 解压）。
 *
 * MiniZip 的镜像：从 ZIP 取文件。S-421 的 `.s421p` 是 ZIP 压缩包
 * （内含一个 `.s421`），解析器需要先解压拿到 XML 再解析。
 *
 * 支持范围：
 *   - ZIP 结构解析（End of Central Directory / Central Directory / Local File Header）
 *   - STORE（method=0，无压缩）：直接拷贝字节
 *   - DEFLATE（method=8，常见压缩）：用 zlib 解压
 *
 * @note ZIP 用小端序存数字。x86/ARM 普通机器也是小端，但代码用
 *       显式"拼字节"读法确保跨平台一致。
 */

#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "S421Optional.h"

namespace s421 {

/**
 * @class MiniUnzip
 * @brief 最小 ZIP 读取器。
 */
class MiniUnzip {
public:
    /**
     * @brief 打开 ZIP 文件并解析其目录结构。
     * @param zipPath ZIP 文件路径。
     * @return `true` = 是合法 ZIP 且解析成功；`false` = 不是 ZIP 或损坏（看 getError）。
     */
    bool open(const std::string& zipPath);

    /**
     * @brief 读取 ZIP 内第一个条目。
     * @return `{文件名, 解压后内容}`；失败（无条目或解压失败）返回 `nullopt`。
     */
    s421::optional<std::pair<std::string, std::string>> readFirstEntry();

    /**
     * @brief 获取错误信息。
     * @return 最近一次操作的错误描述。
     */
    const std::string& getError() const { return error_; }

private:
    /**
     * @struct Entry
     * @brief ZIP 内一个条目的目录信息（来自 Central Directory）。
     */
    struct Entry {
        std::string name;        ///< 文件名
        uint16_t method;         ///< 压缩方法：0=STORE, 8=DEFLATE
        uint32_t compSize;       ///< 压缩后大小
        uint32_t uncompSize;     ///< 原始大小
        uint32_t localOffset;    ///< 本地文件头在 ZIP 中的偏移
    };

    std::string error_;             ///< 最近一次操作的错误信息
    std::vector<uint8_t> data_;     ///< 整个 ZIP 文件读进内存（小文件，够用）
    std::vector<Entry> entries_;    ///< 解析出的条目列表

    /**
     * @brief 从 data_ 的指定偏移处读 16 位小端整数。
     * @param offset 字节偏移。
     * @return 读取到的值。
     */
    uint16_t readU16(size_t offset) const;

    /**
     * @brief 从 data_ 的指定偏移处读 32 位小端整数。
     * @param offset 字节偏移。
     * @return 读取到的值。
     */
    uint32_t readU32(size_t offset) const;

    /**
     * @brief 解析 End of Central Directory。
     * @return 中央目录的起始偏移；失败返回 `nullopt`。
     */
    s421::optional<size_t> findCentralDir();

    /**
     * @brief 解析中央目录，填充 entries_。
     * @param cdOffset 中央目录起始偏移。
     * @return `true` = 解析成功。
     */
    bool parseCentralDir(size_t cdOffset);

    /**
     * @brief 解压一个条目的数据（按 method 选择 STORE 直拷或 DEFLATE 用 zlib）。
     * @param e 目标条目。
     * @return 解压后的内容；失败返回 `nullopt`。
     */
    s421::optional<std::string> extractEntry(const Entry& e);
};

} // namespace s421
