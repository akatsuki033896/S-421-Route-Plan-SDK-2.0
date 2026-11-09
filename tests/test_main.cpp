// test_main.cpp - s421 roundtrip 回环测试
//
// 目的：把"解析 ↔ 序列化"的一致性固化成可重复执行的测试，防止模型/schema
//       再度演进时读写两侧悄悄漂移（读 → 写 → 再读 → 逐字段比对）。
//
// 四个用例：
//   1. modelRoundtrip     样本模型 → serializeToString → parseString → 比对（核心回环）
//   2. zipRoundtrip       样本模型 → .s421p ZIP 容器 → parseCompressedFile → 比对
//   3. serializeDeterministic  同一模型序列化两次，字节级一致（写侧无随机性）
//   4. sampleFiles        命令行传入的外部样本文件 → parse → serialize → parse → 比对
//                         （没有传参时跳过；官方样本不进仓库，见 tests/data/README.md）
//
// 运行（不依赖任何测试框架，零外部依赖）：
//   ./build/bin/s421_tests                     # 用例 1-3
//   ./build/bin/s421_tests sample1.gml sample2.s421p   # 附加用例 4
//
// 已知保真限制（样本取值对策）：
//   - double 序列化精度：坐标 6 位小数、其余 1 位小数（样本取值对齐此精度，
//     使回环零损失；任意取值会因精度截断产生差异——这是真实限制，不是测试误差）。
// 历史缺口（schedule 级 extensions 不回环）已由 FieldSpec 表驱动修复，
// 样本已设置这些 extensions 用例覆盖（见 makeSampleDataset）。

#include "DatasetCompare.h"

#include <s421/S421.h>

#include <cstdio>
#include <iostream>
#include <memory>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

// ============================================================
//  极简测试骨架（零依赖）
// ============================================================
int g_run = 0;
int g_failedTests = 0;

/// 断言：失败计入当前用例并打印现场
void check(bool cond, const std::string& what) {
    if (!cond) {
        std::cout << "    [FAIL] " << what << "\n";
    }
}

void runTest(const char* name, bool (*fn)()) {
    g_run++;
    std::cout << "[RUN ] " << name << "\n";
    const bool ok = fn();
    if (ok) {
        std::cout << "[PASS] " << name << "\n";
    } else {
        g_failedTests++;
        std::cout << "[FAIL] " << name << "\n";
    }
}

// ============================================================
//  样本数据集：字段全覆盖（取值对齐序列化精度，保证回环零损失）
// ============================================================
std::unique_ptr<s421::Dataset> makeSampleDataset() {
    auto ds = std::make_unique<s421::Dataset>();
    ds->gmlId = "S421.TEST-001";

    auto route = std::make_unique<s421::Route>();
    route->formatVersion = "2.0";
    route->id = "RTE-TEST-001";
    route->editionNo = 1;
    route->extensions = s421::RouteExtensions{"ACME",
                                              std::string("NavSuite"),
                                              std::string("1.0"),
                                              std::string("vendor extension note")};

    // ---- RouteInfo（2.0 模型：按值成员）----
    s421::RouteInfo& info = route->info;
    info.name = "TEST ROUTE";
    info.author = "Captain A";
    info.editionTime = "2024-01-15T08:30:00Z";
    info.description = "sample route for roundtrip test";
    info.status = s421::RouteInfoStatus::Planned;
    info.masterApproval = true;
    info.validityStart = "2024-01-01T00:00:00Z";
    info.validityEnd = "2024-02-01T00:00:00Z";
    info.departurePortID1 = "CNSHA";
    info.arrivalPortID1 = "SGSIN";
    info.referencePrevRoute = "PREV-ROUTE-REF";
    info.vesselType = s421::TypeOfShip::Tanker;
    info.vesselName = "BALTIC BRIGHT";
    info.vesselMMSI = "123456789";
    info.vesselCallsign = "AB1234";
    info.vesselIMO = "1234567";
    info.vesselVoyage = "V001";
    info.vesselHeight = 20.0;
    info.vesselLength = 180.0;
    info.vesselBeam = 32.0;
    info.draftMax = 9.0;
    info.airDraftMax = 21.0;
    info.beamMax = 32.2;
    info.lengthMax = 185.0;
    info.extensions = s421::RouteExtensions{"ACME",
                                            std::string("InfoExt"),
                                            std::string("2.0"),
                                            std::string("info extension note")};

    // ---- Waypoints：3 个航点，前 2 个带航段 ----
    auto wpts = std::make_unique<s421::RouteWaypoints>();

    s421::RouteWaypoint wp1;
    wp1.id = 1;
    wp1.name = "WP ALPHA";
    wp1.externalRefID = "https://example.com/wp1";
    wp1.fixed = true;
    wp1.geometry = s421::GM_Point(59.1, 24.1);
    wp1.turnRadius = 3.0;
    {
        auto leg = std::make_unique<s421::RouteWaypointLeg>();
        leg->starboardXTDL = 500;
        leg->portXTDL = 500;
        leg->starboardCL = 1000;
        leg->portCL = 1000;
        leg->safetyContour = 12.5;
        leg->safetyDepth = 15.5;
        leg->geometryType = s421::LegGeometryType::Loxodrome;
        leg->sogMin = 5.5;
        leg->sogMax = 12.3;
        leg->stwMin = 5.0;
        leg->stwMax = 12.0;
        leg->draft = 8.5;
        leg->draftForward = 8.2;
        leg->draftAft = 8.8;
        leg->draftMax = 9.0;
        leg->airDraftMax = 20.5;
        leg->beamMax = 32.0;
        leg->lengthMax = 180.5;
        leg->staticUKC = 1.5;
        leg->dynamicUKC = 1.2;
        leg->safetyMargin = 0.5;
        leg->note = std::string("crossing TSS");
        leg->extensions = s421::RouteExtensions{"ACME",
                                                std::string("LegExt"),
                                                std::string("1.1"),
                                                std::string("leg extension note")};
        wp1.leg = std::move(leg);
    }
    wpts->waypoints.push_back(std::move(wp1));

    s421::RouteWaypoint wp2;
    wp2.id = 2;
    wp2.name = "WP BRAVO";
    wp2.fixed = false;   // false 视为未设置，序列化时省略（往返后仍为 false）
    wp2.geometry = s421::GM_Point(59.5, 25.0);
    wp2.turnRadius = 1.0;
    {
        auto leg = std::make_unique<s421::RouteWaypointLeg>();
        leg->geometryType = s421::LegGeometryType::Orthodrome;
        leg->sogMax = 10.0;
        leg->safetyDepth = 14.0;
        leg->issue = std::string("shallow patch ahead");
        wp2.leg = std::move(leg);
    }
    wpts->waypoints.push_back(std::move(wp2));

    s421::RouteWaypoint wp3;   // 末航点：无航段
    wp3.id = 3;
    wp3.geometry = s421::GM_Point(59.9, 25.9);
    wp3.turnRadius = 0.0;
    wpts->waypoints.push_back(std::move(wp3));

    route->waypoints = std::move(wpts);

    // ---- ActionPoints ----
    auto apts = std::make_unique<s421::RouteActionPoints>();
    s421::RouteActionPoint ap1;
    ap1.id = 1;
    ap1.name = std::string("AP Report");
    ap1.externalRef = "https://example.com/ap1";
    ap1.geometry = s421::GM_Point(59.3, 24.8);
    ap1.distance = 2.5;
    ap1.timeToAct = 60.0;
    ap1.requiredAction = s421::ActionType::Report;
    ap1.actionDescription = "Contact VTS on approach";
    ap1.extensions = s421::RouteExtensions{"ACME",
                                           std::string("ApExt"),
                                           std::string("1.0"),
                                           std::string("ap extension note")};
    apts->actionPoints.push_back(std::move(ap1));
    route->actionPoints = std::move(apts);

    // ---- Schedules（三类子调度各至少一个元素）----
    // schedule / 子调度 / element 三级的 extensions 已由 FieldSpec 表驱动
    // 解析+写出（XSD 生成表），样本全部设置以锁定该能力。
    auto scheds = std::make_unique<s421::RouteSchedules>();
    s421::RouteSchedule sched1;
    sched1.id = 1;
    sched1.name = std::string("MAIN");
    sched1.extensions = s421::RouteExtensions{"ACME",
                                              std::string("SchedExt"),
                                              std::string("1.0"),
                                              std::string("schedule extension note")};

    {
        auto manual = std::make_unique<s421::RouteScheduleManual>();
        manual->extensions = s421::RouteExtensions{"ACME",
                                                   std::string("ManExt"),
                                                   std::string("1.0"),
                                                   std::string("manual extension note")};
        s421::RouteScheduleElement e1;
        e1.waypointId = 1;
        e1.planSOG = 12.3;
        e1.etd = "2024-01-01T08:00:00Z";
        e1.eta = "2024-01-01T10:00:00Z";
        e1.etdWindowBefore = 10;
        e1.etdWindowAfter = 15;
        e1.etaWindowBefore = 10;
        e1.etaWindowAfter = 15;
        e1.note = std::string("departure");
        e1.extensions = s421::RouteExtensions{"ACME",
                                              std::string("ElemExt"),
                                              std::string("1.0"),
                                              std::string("element extension note")};
        manual->elements.push_back(std::move(e1));

        s421::RouteScheduleElement e2;
        e2.waypointId = 2;
        e2.planSOG = 10.5;
        e2.etd = "2024-01-01T12:00:00Z";
        e2.eta = "2024-01-01T14:00:00Z";
        manual->elements.push_back(e2);
        sched1.manual = std::move(manual);
    }
    {
        auto calc = std::make_unique<s421::RouteScheduleCalculated>();
        calc->extensions = s421::RouteExtensions{"ACME",
                                                 std::string("CalcExt"),
                                                 std::string("1.0"),
                                                 std::string("calculated extension note")};
        s421::RouteScheduleElement e1;
        e1.waypointId = 1;
        e1.planSOG = 11.0;
        e1.etd = "2024-01-01T08:05:00Z";
        e1.eta = "2024-01-01T09:55:00Z";
        calc->elements.push_back(e1);
        sched1.calculated = std::move(calc);
    }
    {
        auto rec = std::make_unique<s421::RouteScheduleRecommended>();
        rec->extensions = s421::RouteExtensions{"ACME",
                                                std::string("RecExt"),
                                                std::string("1.0"),
                                                std::string("recommended extension note")};
        s421::RouteScheduleElement e1;
        e1.waypointId = 2;
        e1.planSOG = 9.5;
        e1.etd = "2024-01-01T12:10:00Z";
        e1.eta = "2024-01-01T14:05:00Z";
        rec->elements.push_back(e1);
        sched1.recommended = std::move(rec);
    }
    scheds->schedules.push_back(std::move(sched1));
    route->schedules = std::move(scheds);

    // ---- boundedBy ----
    route->lowerCorner = s421::GM_Point(59.0, 24.0);
    route->upperCorner = s421::GM_Point(60.0, 26.0);

    ds->route = std::move(route);
    return ds;
}

// ============================================================
//  用例 1：核心回环  模型 → XML → 模型
// ============================================================
bool test_modelRoundtrip() {
    std::unique_ptr<s421::Dataset> ds = makeSampleDataset();

    s421::S421Serializer serializer;
    const std::string xml = serializer.serializeToString(*ds);
    check(!xml.empty(), "serializeToString 返回空串");
    if (xml.empty()) return false;

    s421::S421Parser parser;
    std::unique_ptr<s421::Dataset> ds2 = parser.parseString(xml);
    check(ds2 != nullptr,
          "parseString 失败: " + parser.getLastError());
    if (!ds2) {
        std::cout << "---- 序列化输出（诊断）----\n" << xml << "\n";
        return false;
    }

    const s421::test::DatasetDiff diff = s421::test::compareDatasets(*ds, *ds2);
    check(diff.equal, "回环比对不一致: " + diff.firstDifference);
    return diff.equal;
}

// ============================================================
//  用例 2：ZIP 容器回环  模型 → .s421p → 模型
// ============================================================
bool test_zipRoundtrip() {
    std::unique_ptr<s421::Dataset> ds = makeSampleDataset();

    const std::string tmpPath = "s421_roundtrip_tmp.s421p";
    s421::S421Serializer serializer;
    check(serializer.serializeToCompressed(*ds, tmpPath),
          "serializeToCompressed 失败: " + serializer.getLastError());

    bool ok = false;
    s421::S421Parser parser;
    std::unique_ptr<s421::Dataset> ds2 = parser.parseCompressedFile(tmpPath);
    check(ds2 != nullptr,
          "parseCompressedFile 失败: " + parser.getLastError());
    if (ds2) {
        const s421::test::DatasetDiff diff = s421::test::compareDatasets(*ds, *ds2);
        check(diff.equal, "ZIP 回环比对不一致: " + diff.firstDifference);
        ok = diff.equal;
    }

    std::remove(tmpPath.c_str());   // 清理临时文件
    return ok;
}

// ============================================================
//  用例 3：序列化确定性（同一模型两次序列化结果一致）
// ============================================================
bool test_serializeDeterministic() {
    std::unique_ptr<s421::Dataset> ds = makeSampleDataset();

    s421::S421Serializer serializer;
    const std::string xml1 = serializer.serializeToString(*ds);
    const std::string xml2 = serializer.serializeToString(*ds);
    check(!xml1.empty() && !xml2.empty(), "serializeToString 返回空串");
    check(xml1 == xml2, "两次序列化输出不一致（写侧存在随机性）");
    return !xml1.empty() && xml1 == xml2;
}

// ============================================================
//  用例 4：外部样本文件回环（argv 传入；无参数时跳过）
//  样本文件 → parse → serialize → parse → 比对
// ============================================================
int test_sampleFiles(int argc, char** argv) {
    int failedFiles = 0;
    s421::S421Parser parser;
    s421::S421Serializer serializer;

    for (int i = 1; i < argc; i++) {
        const std::string path = argv[i];
        std::cout << "[RUN ] sampleFile: " << path << "\n";

        std::unique_ptr<s421::Dataset> ds =
            (path.find(".s421p") != std::string::npos)
                ? parser.parseCompressedFile(path)
                : parser.parseFile(path);

        if (!ds) {
            std::cout << "[FAIL] sampleFile: 解析失败: " << parser.getLastError() << "\n";
            failedFiles++;
            continue;
        }

        const std::string xml = serializer.serializeToString(*ds);
        if (xml.empty()) {
            std::cout << "[FAIL] sampleFile: 序列化失败: "
                      << serializer.getLastError() << "\n";
            failedFiles++;
            continue;
        }

        std::unique_ptr<s421::Dataset> ds2 = parser.parseString(xml);
        if (!ds2) {
            std::cout << "[FAIL] sampleFile: 再解析失败: " << parser.getLastError() << "\n";
            failedFiles++;
            continue;
        }

        const s421::test::DatasetDiff diff = s421::test::compareDatasets(*ds, *ds2);
        if (diff.equal) {
            std::cout << "[PASS] sampleFile: " << path << "\n";
        } else {
            // 样本文件可能触发已知保真缺口（见文件头），差异路径打印出来供定位
            std::cout << "[FAIL] sampleFile: 回环不一致: " << diff.firstDifference << "\n";
            failedFiles++;
        }
    }
    return failedFiles;
}

} // anonymous namespace

// ============================================================
//  main
// ============================================================
int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(65001);  // 控制台 UTF-8，避免中文乱码
#endif

    std::cout << "s421 roundtrip tests\n";

    runTest("modelRoundtrip",        test_modelRoundtrip);
    runTest("zipRoundtrip",          test_zipRoundtrip);
    runTest("serializeDeterministic", test_serializeDeterministic);

    const int failedFiles = test_sampleFiles(argc, argv);

    const int failed = g_failedTests + failedFiles;
    std::cout << "\n" << (g_run + 0) << " test(s) run, "
              << failed << " failure(s)\n";
    return failed == 0 ? 0 : 1;
}
