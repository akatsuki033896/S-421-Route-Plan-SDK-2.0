// MiniZip.cpp - 最小 ZIP 文件生成器实现
//
// ZIP 文件结构（简化版，STORE 模式无压缩）：
//   [Local File Header 1] [File Data 1]
//   [Local File Header 2] [File Data 2]
//   ...
//   [Central Directory Entry 1]
//   [Central Directory Entry 2]
//   ...
//   [End of Central Directory Record]
//
// 参考：https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT

#include "s421/MiniZip.h"
#include <fstream>
#include <algorithm>

namespace s421 {

// ============================================================
//  CRC32 查找表
// ============================================================
static uint32_t crc32Table[256];
static bool crc32TableInit = false;

static void initCRC32Table() {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320u;
            } else {
                crc >>= 1;
            }
        }
        crc32Table[i] = crc;
    }
    crc32TableInit = true;
}

uint32_t MiniZip::computeCRC32(const uint8_t* data, size_t length) {
    if (!crc32TableInit) initCRC32Table();
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; i++) {
        crc = (crc >> 8) ^ crc32Table[(crc ^ data[i]) & 0xFF];
    }
    return crc ^ 0xFFFFFFFF;
}

// ============================================================
//  小端序写入
// ============================================================
void MiniZip::writeU16(std::ostream& os, uint16_t v) {
    os.put(v & 0xFF);
    os.put((v >> 8) & 0xFF);
}

void MiniZip::writeU32(std::ostream& os, uint32_t v) {
    os.put(v & 0xFF);
    os.put((v >> 8) & 0xFF);
    os.put((v >> 16) & 0xFF);
    os.put((v >> 24) & 0xFF);
}

uint16_t MiniZip::dosTime() {
    std::time_t now = std::time(nullptr);
    std::tm* t = std::localtime(&now);
    return static_cast<uint16_t>((t->tm_hour << 11) | (t->tm_min << 5) | (t->tm_sec / 2));
}

uint16_t MiniZip::dosDate() {
    std::time_t now = std::time(nullptr);
    std::tm* t = std::localtime(&now);
    return static_cast<uint16_t>(((t->tm_year - 80) << 9) | ((t->tm_mon + 1) << 5) | t->tm_mday);
}

// ============================================================
//  addFile
// ============================================================
void MiniZip::addFile(const std::string& fileName, const std::string& data) {
    Entry entry;
    entry.fileName = fileName;
    entry.data.assign(data.begin(), data.end());
    entry.crc32 = computeCRC32(entry.data.data(), entry.data.size());
    entries_.push_back(std::move(entry));
}

bool MiniZip::addFileFromDisk(const std::string& fileName, const std::string& diskPath) {
    std::ifstream f(diskPath, std::ios::binary);
    if (!f) {
        error_ = "无法读取文件: " + diskPath;
        return false;
    }
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    addFile(fileName, content);
    return true;
}

// ============================================================
//  writeTo - 生成 ZIP 文件
// ============================================================
bool MiniZip::writeTo(const std::string& zipPath) {
    if (entries_.empty()) {
        error_ = "ZIP 中没有文件";
        return false;
    }

    std::ofstream out(zipPath, std::ios::binary);
    if (!out) {
        error_ = "无法创建 ZIP 文件: " + zipPath;
        return false;
    }

    uint16_t dosT = dosTime();
    uint16_t dosD = dosDate();

    // 记录每个 Local File Header 的偏移量
    std::vector<uint32_t> offsets;
    offsets.reserve(entries_.size());

    // === 写入 Local File Headers + Data ===
    for (const auto& entry : entries_) {
        offsets.push_back(static_cast<uint32_t>(out.tellp()));

        // Local File Header (30 bytes + name + data)
        writeU32(out, 0x04034B50);     // 签名 'PK\x03\x04'
        writeU16(out, 20);              // 版本（2.0）
        writeU16(out, 0);               // 标志（无加密等）
        writeU16(out, 0);               // 压缩方法（0=STORE，不压缩）
        writeU16(out, dosT);            // 修改时间
        writeU16(out, dosD);            // 修改日期
        writeU32(out, entry.crc32);     // CRC-32
        writeU32(out, static_cast<uint32_t>(entry.data.size()));  // 压缩大小
        writeU32(out, static_cast<uint32_t>(entry.data.size()));  // 原始大小
        writeU16(out, static_cast<uint16_t>(entry.fileName.size())); // 文件名长度
        writeU16(out, 0);               // 额外字段长度

        // 文件名
        out.write(entry.fileName.c_str(), entry.fileName.size());

        // 文件数据
        out.write(reinterpret_cast<const char*>(entry.data.data()), entry.data.size());
    }

    // === 写入 Central Directory ===
    uint32_t cdStart = static_cast<uint32_t>(out.tellp());

    for (size_t i = 0; i < entries_.size(); i++) {
        const auto& entry = entries_[i];

        writeU32(out, 0x02014B50);     // Central Directory 签名
        writeU16(out, 20);              // 创建版本
        writeU16(out, 20);              // 解压所需版本
        writeU16(out, 0);               // 标志
        writeU16(out, 0);               // 压缩方法
        writeU16(out, dosT);            // 修改时间
        writeU16(out, dosD);            // 修改日期
        writeU32(out, entry.crc32);
        writeU32(out, static_cast<uint32_t>(entry.data.size()));
        writeU32(out, static_cast<uint32_t>(entry.data.size()));
        writeU16(out, static_cast<uint16_t>(entry.fileName.size()));
        writeU16(out, 0);               // 额外字段长度（修复：中央目录 entry 此字段原本漏写，后续字段整体前移 2 字节，local-header offset 落在 +40 而非标准 +42，读端 readU32(pos+42) 解析越界）
        writeU16(out, 0);               // 文件注释长度
        writeU16(out, 0);               // 磁盘号起始
        writeU16(out, 0);               // 内部文件属性
        writeU32(out, 0);               // 外部文件属性
        writeU32(out, offsets[i]);       // Local Header 偏移量

        out.write(entry.fileName.c_str(), entry.fileName.size());
    }

    uint32_t cdSize = static_cast<uint32_t>(out.tellp()) - cdStart;

    // === End of Central Directory Record ===
    writeU32(out, 0x06054B50);     // EOCD 签名
    writeU16(out, 0);               // 磁盘号
    writeU16(out, 0);               // Central Directory 起始磁盘号
    writeU16(out, static_cast<uint16_t>(entries_.size())); // 本磁盘记录数
    writeU16(out, static_cast<uint16_t>(entries_.size())); // 总记录数
    writeU32(out, cdSize);          // Central Directory 大小
    writeU32(out, cdStart);         // Central Directory 偏移量
    writeU16(out, 0);               // 注释长度

    out.close();
    return true;
}

} // namespace s421
