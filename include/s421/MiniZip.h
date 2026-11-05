/**
 * @file MiniZip.h
 * @brief 最小 ZIP 文件生成器（纯 C++17，无外部依赖）。
 *
 * 仅实现"创建 ZIP 包"这一个功能，不需要解压。
 * ZIP 结构：Local File Header + 文件数据 + Central Directory + End Record。
 */

#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <cstdint>
#include <ctime>

namespace s421 {

/**
 * @class MiniZip
 * @brief 最小 ZIP 文件写入器。
 */
class MiniZip {
public:
    /**
     * @brief 添加一个文件到 ZIP 中。
     * @param fileName ZIP 内的文件名（路径）。
     * @param data     文件内容。
     */
    void addFile(const std::string& fileName, const std::string& data);

    /**
     * @brief 添加一个文件（从磁盘路径读取）。
     * @param fileName ZIP 内的文件名（路径）。
     * @param diskPath 磁盘上的源文件路径。
     * @return `true` = 读取并添加成功；`false` = 失败（看 getError）。
     */
    bool addFileFromDisk(const std::string& fileName, const std::string& diskPath);

    /**
     * @brief 写入 ZIP 文件。
     * @param zipPath 输出的 ZIP 文件路径。
     * @return `true` = 成功；`false` = 失败（看 getError）。
     */
    bool writeTo(const std::string& zipPath);

    /**
     * @brief 获取错误信息。
     * @return 最近一次操作的错误描述。
     */
    const std::string& getError() const { return error_; }

private:
    std::string error_;   ///< 最近一次操作的错误信息

    /**
     * @struct Entry
     * @brief ZIP 内部：一个文件的条目。
     */
    struct Entry {
        std::string fileName;       ///< 文件名
        std::vector<uint8_t> data;  ///< 文件数据（未压缩，STORE 模式）
        uint32_t crc32;             ///< CRC32 校验和
    };

    std::vector<Entry> entries_;    ///< 已添加的文件条目

    /**
     * @brief 计算 CRC32。
     * @param data   数据缓冲区。
     * @param length 数据字节数。
     * @return 计算得到的 CRC32 值。
     */
    static uint32_t computeCRC32(const uint8_t* data, size_t length);

    /**
     * @brief 写入 16 位小端序整数。
     * @param os 输出流。
     * @param v  待写入的值。
     */
    static void writeU16(std::ostream& os, uint16_t v);

    /**
     * @brief 写入 32 位小端序整数。
     * @param os 输出流。
     * @param v  待写入的值。
     */
    static void writeU32(std::ostream& os, uint32_t v);

    /**
     * @brief 获取当前 DOS 时间字段。
     * @return 打包后的 DOS 时间值。
     */
    static uint16_t dosTime();

    /**
     * @brief 获取当前 DOS 日期字段。
     * @return 打包后的 DOS 日期值。
     */
    static uint16_t dosDate();
};

} // namespace s421
