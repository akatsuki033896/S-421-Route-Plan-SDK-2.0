// main.cpp - S-421 SDK 命令行工具入口
//
// 退出码：0 成功 / 1 参数错误 / 2 解析错误 / 3 校验错误

#include "s421/S421Parser.h"
#include "s421/S421Validator.h"
#include "s421/S421Serializer.h"
#include "s421/S421Formatter.h"
#include "s421/S421Logger.h"

#include <iostream>
#include <string>
#include <cstdlib>

// Windows: 控制台切 UTF-8，避免中文乱码
#ifdef _WIN32
#include <windows.h>
#endif

// ============================================================
//  命令行辅助函数
// ============================================================

// 打印使用说明（参数错误或 --help 时调用）
static void printUsage(const char* prog) {
    std::cerr
        << "S-421 SDK 工具\n"
        << "\n"
        << "用法:\n"
        << "  " << prog << " parse <file> [--verbose] [--output <out.s421>]\n"
        << "  " << prog << " validate <file>\n"
        << "  " << prog << " info\n"
        << "\n"
        << "命令:\n"
        << "  parse     解析 S-421 文件（.s421, .s421p, .gml）\n"
        << "  validate  解析并执行完整校验\n"
        << "  info      显示 SDK 版本信息\n"
        << "\n"
        << "选项:\n"
        << "  --verbose         显示详细输出\n"
        << "  --output <path>   将解析结果序列化到文件\n"
        << "  --log-level <lvl> 设置日志级别: error|warn|info|debug\n";
}

// 显示 SDK 版本信息（info 命令）
static void printInfo() {
    // 实际编译用的 C++ 标准（MSVC 看 _MSVC_LANG，其他编译器看 __cplusplus）
    const char* cppStd = "C++?";
#if defined(_MSVC_LANG)
    #if _MSVC_LANG >= 201703L
        cppStd = "C++17";
    #elif _MSVC_LANG >= 201402L
        cppStd = "C++14";
    #elif _MSVC_LANG >= 201103L
        cppStd = "C++11";
    #endif
#elif defined(__cplusplus)
    #if __cplusplus >= 201703L
        cppStd = "C++17";
    #elif __cplusplus >= 201402L
        cppStd = "C++14";
    #elif __cplusplus >= 201103L
        cppStd = "C++11";
    #endif
#endif
    std::cout << "S-421 船载航线 SDK\n"
              << "  版本:       2.0.0\n"
              << "  标准:       IEC 63173-1 / S-421 Ed 2.0\n"
              << "  C++ 标准:   " << cppStd << "\n"
              << "  XML 解析器: pugixml 1.14\n";
}

// 设置日志级别（error/warn/info/debug），非法值返回 false
static bool setLogLevel(const std::string& level) {
    if (level == "error") {
        s421::Logger::instance().setLevel(s421::LogLevel::Error);
    } else if (level == "warn") {
        s421::Logger::instance().setLevel(s421::LogLevel::Warn);
    } else if (level == "info") {
        s421::Logger::instance().setLevel(s421::LogLevel::Info);
    } else if (level == "debug") {
        s421::Logger::instance().setLevel(s421::LogLevel::Debug);
    } else {
        std::cerr << "未知日志级别: " << level << "\n";
        return false;
    }
    return true;
}

// ============================================================
//  命令处理函数
// ============================================================

// parse 命令：解析文件 → 显示结果 → (可选)序列化输出
static int cmdParse(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "错误: 缺少文件参数\n";
        return 1;
    }

    std::string filePath = argv[2];
    bool verbose = false;
    std::string outputPath;          // 序列化输出路径（可选）

    for (int i = 3; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--verbose" || arg == "-v") {
            verbose = true;
        } else if (arg == "--output" || arg == "-o") {
            if (i + 1 >= argc) {
                std::cerr << "错误: --output 需要一个文件路径参数\n";
                return 1;
            }
            outputPath = argv[++i];  // ++i 消费并跳过路径参数
        } else if (arg == "--log-level") {
            if (i + 1 >= argc) {
                std::cerr << "错误: --log-level 需要一个级别值\n";
                return 1;
            }
            if (!setLogLevel(argv[++i])) return 1;
        } else {
            std::cerr << "未知选项: " << arg << "\n";
            return 1;
        }
    }

    // 解析：.s421p 走压缩解析（先 ZIP 解压再 XML 解析），其他直接 XML 解析
    s421::S421Parser parser;
    std::unique_ptr<s421::Dataset> dataset;
    if (filePath.find(".s421p") != std::string::npos) {
        dataset = parser.parseCompressedFile(filePath);
    } else {
        dataset = parser.parseFile(filePath);
    }

    if (!dataset) {
        std::cerr << "解析错误: " << parser.getLastError() << "\n";
        return 2;
    }

    // 格式化输出到控制台（verbose 时显示每个航点坐标和航段参数）
    s421::FormatResult summary = s421::formatDataset(*dataset, std::cout, verbose);

    S421_LOG_INFO("已解析 %s: %d 个航点, %d 个航段",
                  filePath.c_str(), summary.waypointCount, summary.legCount);

    // 可选：序列化输出（用于验证 parse→serialize 往返一致性）
    if (!outputPath.empty()) {
        s421::S421Serializer serializer;
        if (!serializer.serializeToFile(*dataset, outputPath)) {
            std::cerr << "序列化错误: " << serializer.getLastError() << "\n";
            return 2;
        }
        std::cout << "输出已写入: " << outputPath << "\n";
    }

    return 0;
}

// validate 命令：解析文件 → 运行四层校验 → 显示结果
static int cmdValidate(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "错误: 缺少文件参数\n";
        return 1;
    }

    std::string filePath = argv[2];

    // 解析可选的 --log-level
    for (int i = 3; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--log-level") {
            if (i + 1 >= argc) { std::cerr << "错误: --log-level 需要一个级别值\n"; return 1; }
            if (!setLogLevel(argv[++i])) return 1;
        }
    }

    // 第一步：解析文件（同样支持 .s421p 压缩包）
    s421::S421Parser parser;
    std::unique_ptr<s421::Dataset> dataset;
    if (filePath.find(".s421p") != std::string::npos) {
        dataset = parser.parseCompressedFile(filePath);
    } else {
        dataset = parser.parseFile(filePath);
    }
    if (!dataset) {
        std::cerr << "解析错误: " << parser.getLastError() << "\n";
        return 2;
    }

    // 第二步：运行四层校验（格式 / 属性 / 逻辑 / 错误反馈）
    s421::S421Validator validator;
    s421::ValidationResult result = validator.validate(*dataset);

    // 第三步：输出校验结果（ok = 无错误，警告不阻断）
    if (result.ok()) {
        std::cout << "校验通过\n";
        s421::formatDataset(*dataset, std::cout, false);
        return 0;
    }

    std::cerr << "校验失败（" << result.errorCount() << " 个错误, "
              << result.warningCount() << " 个警告）:\n";
    for (const auto& err : result.errors) {
        std::cerr << "  " << err.toString() << "\n";
    }
    for (const auto& warn : result.warnings) {
        std::cerr << "  " << warn.toString() << "\n";
    }
    return 3;
}

// ============================================================
//  main - 程序入口：根据命令分发到对应处理函数
// ============================================================
int main(int argc, char* argv[]) {
    // Windows: 控制台输出设为 UTF-8，否则中文乱码
#ifdef _WIN32
    SetConsoleOutputCP(65001);
#endif

    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    std::string command = argv[1];

    if (command == "parse") {
        return cmdParse(argc, argv);
    } else if (command == "validate") {
        return cmdValidate(argc, argv);
    } else if (command == "info") {
        printInfo();
        return 0;
    } else if (command == "--help" || command == "-h") {
        printUsage(argv[0]);
        return 0;
    } else {
        std::cerr << "未知命令: " << command << "\n";
        printUsage(argv[0]);
        return 1;
    }
}
