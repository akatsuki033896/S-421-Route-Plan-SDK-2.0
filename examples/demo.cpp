// examples/demo.cpp - 最简调用示例
//
// 目的：证明 s421 静态库可作为"外部依赖"被独立编译链接并正常工作。
// 集成方可参考本文件了解最短调用路径：解析 → 校验 → 序列化（读 → 写闭环）。
//
// 运行（在项目根目录执行，以便定位 tests/data 下样本）：
//   ./build/bin/s421_demo                          # 默认解析内置样本
//   ./build/bin/s421_demo path/to/your.s421        # 解析指定文件

#include <s421/S421.h>

#include <iostream>
#include <memory>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(65001);  // 控制台 UTF-8，避免中文乱码
#endif

    // const std::string file = (argc >= 2) ? argv[1] : "../../S-421/RTE-TEST-GBASIC.gml";

    const std::string file = (argc >= 2) ? argv[1] : "../../test.gml";

    // 1) 解析（.s421p 走压缩解析，其余走直接 XML 解析）
    s421::S421Parser parser;
    std::unique_ptr<s421::Dataset> dataset =
        (file.find(".s421p") != std::string::npos)
            ? parser.parseCompressedFile(file)
            : parser.parseFile(file);

    if (!dataset) {
        std::cerr << "解析失败: " << parser.getLastError() << "\n";
        return 1;
    }

    // 2) 校验（四层：格式 / 属性 / 逻辑 / 反馈）
    s421::S421Validator validator;
    s421::ValidationResult result = validator.validate(*dataset);
    std::cout << "校验结果: " << (result.ok() ? "通过" : "未通过")
              << "（错误 " << result.errorCount()
              << " / 警告 " << result.warningCount() << "）\n";

    result.format(std::cout);  // 输出详细错误/警告列表

    // 3) 摘要
    s421::FormatResult summary = s421::formatDataset(*dataset, std::cout, false);
    std::cout << "计数: 航点=" << summary.waypointCount
              << " 航段=" << summary.legCount
              << " 动作点=" << summary.actionPointCount
              << " 调度=" << summary.scheduleCount << "\n";

    if (dataset->route) {
        std::cout << "航线: " << s421::summarizeRoute(*dataset->route) << "\n";
    }

    // 4) 序列化：Dataset → .s421 / .s421p（把数据写回 S-421 文件）
    s421::S421Serializer serializer;
    if (serializer.serializeToFile(*dataset, "demo_out.s421")) {
        std::cout << "已写出: demo_out.s421（XML 格式）\n";
    } else {
        std::cerr << "序列化失败: " << serializer.getLastError() << "\n";
    }
    if (serializer.serializeToCompressed(*dataset, "demo_out.s421p")) {
        std::cout << "已写出: demo_out.s421p（ZIP 压缩格式）\n";
    } else {
        std::cerr << "压缩序列化失败: " << serializer.getLastError() << "\n";
    }

    return 0;
}
