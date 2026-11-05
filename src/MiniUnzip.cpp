// MiniUnzip.cpp - 最小 ZIP 读取器实现
//
// 【ZIP 文件格式速览（只看我们用到的部分）】
//   一个 ZIP 文件长这样（顺序）：
//     [Local File Header + 文件数据] × N    ← 每个文件一份，在最前面
//     [Central Directory 条目]      × N    ← 每个文件的"目录卡"
//     [End of Central Directory (EOCD)]    ← 整个 ZIP 的结尾标记
//   读取策略：先从文件末尾找到 EOCD（它告诉我们中央目录在哪），
//             再读中央目录拿到每个文件的元信息，最后按需去本地头后面取数据。

#include "s421/MiniUnzip.h"
#include <zlib.h>          // DEFLATE 解压
#include <fstream>
#include <cstring>

namespace s421 {

// ZIP 格式里的"签名"（魔数），用来标记各类记录的起始
static const uint32_t SIG_EOCD    = 0x06054b50;   // End of Central Directory
static const uint32_t SIG_CENTRAL = 0x02014b50;   // Central Directory 条目
static const uint32_t SIG_LOCAL   = 0x04034b50;   // Local File Header

// ---- 小端序整数读取（ZIP 用小端）----
// 把 offset 处的 2/4 个字节按"低位在前"拼成整数
uint16_t MiniUnzip::readU16(size_t offset) const {
    return static_cast<uint16_t>(data_[offset])
         | (static_cast<uint16_t>(data_[offset + 1]) << 8);
}
uint32_t MiniUnzip::readU32(size_t offset) const {
    return static_cast<uint32_t>(data_[offset])
         | (static_cast<uint32_t>(data_[offset + 1]) << 8)
         | (static_cast<uint32_t>(data_[offset + 2]) << 16)
         | (static_cast<uint32_t>(data_[offset + 3]) << 24);
}

// ---- open：读文件 + 找中央目录 + 解析 ----
bool MiniUnzip::open(const std::string& zipPath) {
    entries_.clear();
    error_.clear();

    // 1. 整个文件读进内存（.s421p 很小，几十 KB，全读最简单）
    std::ifstream f(zipPath, std::ios::binary);
    if (!f) {
        error_ = "无法打开文件: " + zipPath;
        return false;
    }
    data_.assign((std::istreambuf_iterator<char>(f)),
                  std::istreambuf_iterator<char>());

    if (data_.size() < 22) {   // EOCD 最少 22 字节
        error_ = "文件太小，不像 ZIP: " + zipPath;
        return false;
    }

    // 2. 找中央目录
    auto cdOffset = findCentralDir();
    if (!cdOffset) {
        error_ = "未找到 ZIP End of Central Directory（可能不是 ZIP）: " + zipPath;
        return false;
    }

    // 3. 解析中央目录
    if (!parseCentralDir(*cdOffset)) {
        return false;   // parseCentralDir 已设置 error_
    }

    if (entries_.empty()) {
        error_ = "ZIP 内无条目: " + zipPath;
        return false;
    }
    return true;
}

// ---- findCentralDir：从文件末尾向前找 EOCD 签名 ----
// EOCD 记录固定 22 字节，但后面可能跟最多 65535 字节的"注释"，
// 所以从文件末尾往前扫最多 65557 字节，找第一个匹配签名 0x06054b50 的位置。
s421::optional<size_t> MiniUnzip::findCentralDir() {
    if (data_.size() < 22) return s421::nullopt;

    size_t maxScan = data_.size();
    if (maxScan > 65557) maxScan = 65557;

    for (size_t back = 0; back + 22 <= data_.size() && back <= maxScan; ++back) {
        size_t pos = data_.size() - 22 - back;
        if (readU32(pos) == SIG_EOCD) {
            // EOCD 里 pos+16 处存的是"中央目录起始偏移"
            return readU32(pos + 16);
        }
    }
    return s421::nullopt;
}

// ---- parseCentralDir：遍历中央目录条目 ----
bool MiniUnzip::parseCentralDir(size_t cdOffset) {
    size_t pos = cdOffset;
    while (pos + 46 <= data_.size()) {
        if (readU32(pos) != SIG_CENTRAL) break;   // 不是中央目录条目了，结束

        Entry e;
        e.method      = readU16(pos + 10);   // 压缩方法
        e.compSize    = readU32(pos + 20);   // 压缩后大小
        e.uncompSize  = readU32(pos + 24);   // 原始大小
        uint16_t nameLen  = readU16(pos + 28);
        uint16_t extraLen = readU16(pos + 30);
        uint16_t cmtLen   = readU16(pos + 32);
        e.localOffset = readU32(pos + 42);   // 对应本地头的偏移

        // 文件名紧跟在 46 字节固定头之后
        e.name.assign(reinterpret_cast<const char*>(data_.data() + pos + 46), nameLen);
        entries_.push_back(std::move(e));

        // 下一条目：跳过 固定46 + 名 + 额外 + 注释
        pos += 46 + nameLen + extraLen + cmtLen;
    }
    return true;
}

// ---- extractEntry：取一个条目的解压后内容 ----
s421::optional<std::string> MiniUnzip::extractEntry(const Entry& e) {
    // 1. 定位本地文件头
    // 【注意】本地头的字段偏移和中央目录【不同】！
    //   本地头：filename length 在 +26，extra field length 在 +28（比中央目录靠前 2 字节）
    if (e.localOffset + 30 > data_.size()) {
        error_ = "本地头偏移越界";
        return s421::nullopt;
    }
    if (readU32(e.localOffset) != SIG_LOCAL) {
        error_ = "本地头签名不匹配";
        return s421::nullopt;
    }
    uint16_t nameLen  = readU16(e.localOffset + 26);
    uint16_t extraLen = readU16(e.localOffset + 28);

    // 2. 数据起点 = 本地头(30) + 文件名 + 额外字段
    size_t dataStart = e.localOffset + 30 + nameLen + extraLen;
    if (dataStart + e.compSize > data_.size()) {
        error_ = "压缩数据越界";
        return s421::nullopt;
    }
    const uint8_t* compData = data_.data() + dataStart;

    // 3. 按压缩方法处理
    if (e.method == 0) {
        // STORE：没压缩，直接拷字节
        return std::string(reinterpret_cast<const char*>(compData), e.compSize);
    }

    if (e.method == 8) {
        // DEFLATE：用 zlib 解压
        // 【关键】ZIP 存的是"原始 DEFLATE"（RFC 1951，无 zlib 头/校验），
        //   而 uncompress() 只认"zlib 格式"（RFC 1950，带 2 字节头 + Adler32 尾）。
        //   所以必须用 inflate 流式 API，且 windowBits 传负值（-MAX_WBITS）表示"原始 DEFLATE"。
        z_stream zs;
        std::memset(&zs, 0, sizeof(zs));
        zs.next_in   = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(compData));
        zs.avail_in  = e.compSize;

        std::string out(e.uncompSize, '\0');
        // C++14 的 std::string::data() 只返回 const char*（C++17 才有非 const 重载），
        // 用 &out[0] 拿到可写缓冲地址。out 已预分配 uncompSize 字节，写入安全。
        zs.next_out  = reinterpret_cast<Bytef*>(&out[0]);
        zs.avail_out = static_cast<uInt>(e.uncompSize);

        // windowBits 负值 = 原始 DEFLATE（跳过 zlib 头部解析）
        if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) {
            error_ = "zlib inflateInit2 失败";
            return s421::nullopt;
        }
        int ret = inflate(&zs, Z_FINISH);   // 一次性解完（输出缓冲已预分配够大）
        uLong produced = zs.total_out;
        inflateEnd(&zs);                    // 释放 zlib 内部状态
        if (ret != Z_STREAM_END) {
            error_ = std::string("zlib 解压失败，错误码: ") + std::to_string(ret);
            return s421::nullopt;
        }
        out.resize(produced);   // 按实际产出长度截断
        return out;
    }

    error_ = "不支持的压缩方法: " + std::to_string(e.method);
    return s421::nullopt;
}

// ---- readFirstEntry：取第一个条目 ----
s421::optional<std::pair<std::string, std::string>> MiniUnzip::readFirstEntry() {
    if (entries_.empty()) return s421::nullopt;
    auto content = extractEntry(entries_.front());
    if (!content) return s421::nullopt;
    return std::make_pair(entries_.front().name, std::move(*content));
}

} // namespace s421
