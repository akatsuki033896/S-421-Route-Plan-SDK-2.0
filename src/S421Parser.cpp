// S421Parser.cpp - S-421 XML 解析器的完整实现
//
// 解析策略（两阶段法）—— 解析器核心设计：
//   第一阶段 - 建索引：遍历 <members> 下所有对象，按 gml:id 收集进 map
//              （Edition 2.0 用单一 <members> 包装所有对象；旧版是 member/imember 分装）
//   第二阶段 - 按引用解析：从 Route 出发，跟着 xlink:href 去索引表里查目标节点，再解析它
//
// S-421 XML 是"扁平 + 引用"结构（类似数据库外键）：Route 不直接包含 RouteInfo，
// 而是写 <routeInfo xlink:href="#RTE.INFO"/> "指过去"找它。所以必须先扫一遍把所有
// 对象按 ID 存好（Phase 1），再按引用逐个取出解析（Phase 2）。
// 另外 href 值和 gml:id 还可能不完全匹配，需要多策略查找（见 resolveById）

#include "s421/S421Parser.h"
#include "s421/MiniUnzip.h"
#include "s421/S421Fs.h"
#include <pugixml.hpp>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstring>

namespace s421 {

// ============================================================
//  构造 / 析构
// ============================================================
S421Parser::S421Parser() = default;
S421Parser::~S421Parser() = default;

// ============================================================
//  parseFile - 解析 .s421 文件（扩展名不拘，.gml 等历史名同样可解析）
// ============================================================
std::unique_ptr<Dataset> S421Parser::parseFile(const std::string& filePath) {
    lastError_.clear();

    pugi::xml_document doc;
    // parse_ws_pcdata: 保留空白文本节点
    pugi::xml_parse_result result = doc.load_file(filePath.c_str(),
        pugi::parse_default | pugi::parse_ws_pcdata);

    if (!result) {
        lastError_ = "XML 解析失败: " + std::string(result.description())
                   + " (位置: " + std::to_string(result.offset) + ")";
        return nullptr;
    }

    return buildFromDoc(doc);
}

// ============================================================
//  parseString - 解析 XML 字符串
// ============================================================
std::unique_ptr<Dataset> S421Parser::parseString(const std::string& xmlContent) {
    lastError_.clear();
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_string(xmlContent.c_str());
    if (!result) {
        lastError_ = "XML 字符串解析失败: " + std::string(result.description());
        return nullptr;
    }
    return buildFromDoc(doc);
}

// ============================================================
//  parseExchangeSet - 解析 S-100 兼容交换集
//  规格书 §3.1.1 第三种格式
//
//  交换集目录结构：
//    CATALOG.001.XML  — 交换目录（指向 .s421 文件）
//    xxx.s421         — 航线数据文件
//    MD_xxx.xml       — ISO 19115 元数据（跳过）
//    support/         — 辅助目录（跳过）
// ============================================================
std::unique_ptr<Dataset> S421Parser::parseExchangeSet(const std::string& exchangeSetDir) {
    lastError_.clear();

    // 构建目录路径（确保以 / 结尾）
    std::string dir = exchangeSetDir;
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') {
        dir += '/';
    }

    // 策略 1：解析 CATALOG 文件找 .s421 文件名
    // CATALOG 中包含 <S100XC:fileName>xxx.s421</S100XC:fileName>
    std::vector<std::string> catalogCandidates = {
        dir + "CATALOG.001.XML",
        dir + "CATALOG.XML",
        dir + "catalog.xml",
        dir + "catalog.001.xml"
    };

    for (const auto& catPath : catalogCandidates) {
        pugi::xml_document catDoc;
        pugi::xml_parse_result catResult = catDoc.load_file(catPath.c_str());
        if (!catResult) continue;

        // 在 CATALOG 中搜索 fileName 元素（可能带 S100XC: 前缀）
        std::string s421FileName;
        auto fileNameNode = catDoc.root().find_node([](pugi::xml_node node) {
            std::string name = node.name();
            return (name == "S100XC:fileName" || name == "fileName")
                   && !node.text().empty();
        });

        if (!fileNameNode.empty()) {
            s421FileName = fileNameNode.text().get();
        }

        if (!s421FileName.empty()) {
            // 尝试多种路径组合
            std::vector<std::string> tryPaths = {
                dir + s421FileName,             // 目录 + 文件名
                dir + "data/" + s421FileName,   // 可能的 data 子目录
                s421FileName                     // 可能是绝对路径
            };
            for (const auto& s421Path : tryPaths) {
                auto dataset = parseFile(s421Path);
                if (dataset) return dataset;
            }
        }
    }

    // 策略 2：直接搜索目录中的 .s421 文件（兜底）
    // 目录不存在 / 无权限访问时 forEachRegularFile 静默返回（等价原 fs catch）。
    std::unique_ptr<Dataset> found;
    s421::fs::forEachRegularFile(dir, [&](const std::string& path) {
        if (path.find(".s421") != std::string::npos
            && path.find(".xml") == std::string::npos
            && path.find(".s421p") != std::string::npos) {
            auto dataset = parseFile(path);
            if (dataset) {
                found = std::move(dataset);  // 回调中拿到结果，跳出后返回
            }
        }
    });
    if (found) return found;

    lastError_ = "交换集目录中未找到有效的 .s421 文件: " + dir;
    return nullptr;
}

// ============================================================
//  parseCompressedFile - 解析 .s421p 文件
//  规格书: .s421p 是 ZIP 压缩包，内含一个 .s421 文件
//  但实际示例中有些 .s421p 就是纯 XML（兼容处理）
// ============================================================
std::unique_ptr<Dataset> S421Parser::parseCompressedFile(const std::string& filePath) {
    lastError_.clear();

    // 双兜底策略：先试 ZIP（.s421p 标准），失败再退回当纯 XML（兼容非标准历史数据）
    // ——早期示例里有些 .s421p 其实是纯 XML（没压缩）

    // ---- 策略 1：当 ZIP 解压 ----
    MiniUnzip unzip;
    if (unzip.open(filePath)) {
        auto entry = unzip.readFirstEntry();   // {文件名, 解压后内容}
        if (entry) {
            auto dataset = parseString(entry->second);
            if (dataset) {
                return dataset;
            }
            // 解压成功但 XML 无效：继续走策略 2 兜底
        }
    }
    std::string zipError = unzip.getError();   // 记下 ZIP 阶段错误，便于最终诊断

    // ---- 策略 2：当纯 XML 解析 ----
    auto dataset = parseFile(filePath);
    if (dataset) {
        return dataset;
    }

    lastError_ = ".s421p 文件既非有效 ZIP 也非有效 XML"
               + (zipError.empty() ? std::string() : "（ZIP 原因: " + zipError + "）")
               + "（XML 原因: " + lastError_ + "）";
    return nullptr;
}

// ============================================================
//  辅助方法：安全获取"可选"子节点的值
//  字段缺失时返回 s421::nullopt，避免崩溃；调用方用 .has_value() 判断
// ============================================================
s421::optional<std::string> S421Parser::getOptText(pugi::xml_node parent, const char* childName) {
    pugi::xml_node child = parent.child(childName);
    if (child.empty()) return s421::nullopt;
    const char* val = child.text().get();
    if (!val || val[0] == '\0') return s421::nullopt;  // 有节点但文本为空也算空
    return std::string(val);
}

s421::optional<double> S421Parser::getOptDouble(pugi::xml_node parent, const char* childName) {
    pugi::xml_node child = parent.child(childName);
    if (child.empty()) return s421::nullopt;
    return child.text().as_double();
}

s421::optional<int> S421Parser::getOptInt(pugi::xml_node parent, const char* childName) {
    pugi::xml_node child = parent.child(childName);
    if (child.empty()) return s421::nullopt;
    return child.text().as_int();
}

s421::optional<bool> S421Parser::getOptBool(pugi::xml_node parent, const char* childName) {
    pugi::xml_node child = parent.child(childName);
    if (child.empty()) return s421::nullopt;
    std::string val = child.text().get();
    // 兼容 "true"/"false" 和 "1"/"0" 两种写法（实际数据中混用）
    if (val == "true" || val == "1") return true;
    if (val == "false" || val == "0") return false;
    return s421::nullopt;
}

// ============================================================
//  resolveRef - 通过 xlink:href 在整篇文档里查找目标节点
//
//  XML 引用长这样: <routeInfo xlink:href="#RTE.INFO" />
//  '#RTE.INFO' 里的 # 是"本地引用"标记，真正的 ID 是 RTE.INFO。
//
//  这是 Phase 1 索引法的"兜底"——优先用 resolveById 查 map（O(1)），
//  查不到时才用这个全文档递归搜索（慢，但能兜住各种异常 ID 拼写）。
// ============================================================
pugi::xml_node S421Parser::resolveRef(pugi::xml_node docNode, const char* href) {
    if (!href || href[0] != '#') return pugi::xml_node();   // 不是 # 开头就不是本地引用

    const char* id = href + 1;   // 跳过 '#'，拿到真正的 gml:id

    return docNode.root().find_node([id](pugi::xml_node node) {
        return strcmp(node.attribute("gml:id").value(), id) == 0;
    });
}

// ============================================================
//  parseGmlPos - 解析坐标字符串 "纬度 经度"
//  GML 格式: "59.892863 25.822235"（纬度在前，经度在后，空格分隔）
// ============================================================
GM_Point S421Parser::parseGmlPos(const std::string& posStr) {
    GM_Point pos;
    std::istringstream iss(posStr);
    iss >> pos.lat >> pos.lon;
    return pos;
}

// ============================================================
//  parsePointGeometry - 解析 Point 几何
//  XML 结构:
//    <geometry>
//      <S100:pointProperty>
//        <S100:Point>
//          <gml:pos>59.892863 25.822235</gml:pos>
//        </S100:Point>
//      </S100:pointProperty>
//    </geometry>
// ============================================================
GM_Point S421Parser::parsePointGeometry(pugi::xml_node geometryNode) {
    // 尝试多种可能的节点名: "gml:pos", "pos"
    pugi::xml_node posNode = geometryNode.first_element_by_path("gml:pos");
    if (posNode.empty()) posNode = geometryNode.first_element_by_path("pos");

    // 直接路径找不到时用 find_node 递归搜索（pugixml 会保留命名空间前缀）
    if (posNode.empty()) {
        posNode = geometryNode.find_node([](pugi::xml_node node) {
            std::string name = node.name();
            auto colon = name.rfind(':');
            if (colon != std::string::npos) name = name.substr(colon + 1);
            return name == "pos" && !node.text().empty();
        });
    }

    if (!posNode.empty()) {
        return parseGmlPos(posNode.text().get());
    }
    return GM_Point{0.0, 0.0};
}

// ============================================================
//  parseExtension - 解析厂商扩展
//  XML 结构:
//    <routeExtensions gml:id="..." routeExtensionsManufacturerId="ACME"
//                     routeExtensionsName="..." routeExtensionsVersion="1.0">
//      <routeExtensionsNote>内容文本</routeExtensionsNote>
//    </routeExtensions>
// ============================================================
RouteExtensions S421Parser::parseExtension(pugi::xml_node extNode) {
    RouteExtensions ext;
    ext.manufacturerId = extNode.attribute("routeExtensionsManufacturerId").value();
    ext.name = extNode.attribute("routeExtensionsName").value();
    ext.version = extNode.attribute("routeExtensionsVersion").value();

    // 收集所有 note（可以有多个）
    std::string content;
    for (pugi::xml_node note : extNode.children("routeExtensionsNote")) {
        if (!content.empty()) content += "\n";
        content += note.text().get();
    }
    if (!content.empty()) ext.content = content;
    return ext;
}

// ============================================================
//  buildFromDoc - 从 pugixml 文档构建 Dataset（解析主入口）
// ============================================================

// 提取 XML 节点名中的本地部分（去掉命名空间前缀）
// "S421:RouteWaypoint" → "RouteWaypoint"
static std::string localName(const std::string& qualifiedName) {
    auto colon = qualifiedName.rfind(':');
    return (colon != std::string::npos) ? qualifiedName.substr(colon + 1) : qualifiedName;
}

// 多策略 ID 查找
// S-421 标准中 xlink:href 和 gml:id 的命名不一定精确匹配
// 例如：href="#RTE.WPTS.WPT.1" → gml:id="RTE.WPT.1"
//       href="#RTE.WPTS.WP.1"   → gml:id="RTE.WPT.1"
// 策略：1. 精确匹配 → 2. 末段数字匹配 → 3. resolveRef 全文档搜索
template<typename Map>
pugi::xml_node resolveById(const Map& index, pugi::xml_node docNode, const char* href) {
    if (!href || href[0] != '#') return pugi::xml_node();
    std::string targetId(href + 1);

    // 策略 1: 精确匹配
    auto it = index.find(targetId);
    if (it != index.end()) return it->second;

    // 策略 2: 末段数字匹配
    // 提取 href 中的最后一段数字编号（如 ".1"、".10"），
    // 在索引表中找以相同数字后缀结尾的 key
    // 注意：只有当索引表元素较少（<100）时才不会误匹配
    auto lastDot = targetId.rfind('.');
    if (lastDot != std::string::npos && lastDot > 0) {
        std::string numSuffix = targetId.substr(lastDot); // ".1" or ".10"
        // 确认最后一段确实是数字
        bool isNumeric = true;
        for (size_t i = 1; i < numSuffix.size(); i++) {
            if (!std::isdigit(static_cast<unsigned char>(numSuffix[i]))) {
                isNumeric = false;
                break;
            }
        }
        if (isNumeric) {
            // 在索引表中找以相同数字后缀结尾的唯一匹配
            pugi::xml_node match;
            int matchCount = 0;
            for (auto& kv : index) {
                const std::string& key = kv.first;
                const pugi::xml_node& node = kv.second;
                if (key.size() > numSuffix.size() &&
                    key.compare(key.size() - numSuffix.size(), numSuffix.size(), numSuffix) == 0) {
                    // 检查数字前面是 '.'（避免部分数字匹配如 ".1" 匹配到 ".21"）
                    auto keyDot = key.rfind('.');
                    if (keyDot != std::string::npos &&
                        key.substr(keyDot) == numSuffix) {
                        match = node;
                        matchCount++;
                    }
                }
            }
            if (matchCount == 1) return match;
        }
    }

    // 策略 3: 由调用方走 resolveRef 全文档搜索兜底
    return pugi::xml_node();
}

std::unique_ptr<Dataset> S421Parser::buildFromDoc(pugi::xml_document& doc) {
    auto dataset = std::make_unique<Dataset>();

    // ========== 查找 Dataset 根元素 ==========
    pugi::xml_node datasetNode;
    for (pugi::xml_node child : doc.children()) {
        std::string name = localName(child.name());
        // 容忍根元素大小写差异：测试文件 GFULL 用 "DataSet"，其余用 "Dataset"
        if (name == "Dataset" || name == "DataSet") {
            datasetNode = child;
            break;
        }
    }

    if (datasetNode.empty()) {
        lastError_ = "找不到 Dataset 根元素";
        return nullptr;
    }

    dataset->gmlId = datasetNode.attribute("gml:id").value();

    // ========== 解析空间范围 boundedBy ==========
    // 【注意】parseFile 开启了 parse_ws_pcdata（保留空白文本节点），
    //   因此 <gml:boundedBy> 与 <gml:Envelope> 之间的换行缩进会变成一个
    //   空白文本子节点。用 first_child() 会取到这个文本节点而不是 Envelope，
    //   导致后面查 lowerCorner/upperCorner 全部落空、boundedBy 整块丢失。
    //   这里改为按子节点名遍历（文本节点 localName 为空，会被自然跳过），
    //   并兼容带/不带 gml: 前缀两种写法。
    pugi::xml_node envNode;
    for (pugi::xml_node child : datasetNode.children()) {
        if (localName(child.name()) == "boundedBy") {
            for (pugi::xml_node sub : child.children()) {
                if (localName(sub.name()) == "Envelope") {
                    envNode = sub;
                    break;
                }
            }
            break;
        }
    }

    // ========== Phase 1: 建索引 — 收集所有对象节点 ==========
    // 用 unordered_map 做 gml:id → xml_node 索引（O(1) 查找），每类对象一张表
    std::unordered_map<std::string, pugi::xml_node> waypointNodes;   // RouteWaypoint
    std::unordered_map<std::string, pugi::xml_node> legNodes;        // RouteWaypointLeg
    std::unordered_map<std::string, pugi::xml_node> actionPointNodes;// RouteActionPoint
    std::unordered_map<std::string, pugi::xml_node> scheduleNodes;   // RouteSchedule
    std::unordered_map<std::string, pugi::xml_node> schedElementNodes; // RouteScheduleElement
    std::unordered_map<std::string, pugi::xml_node> schedManualNodes; // RouteScheduleManual
    std::unordered_map<std::string, pugi::xml_node> schedCalcNodes;   // RouteScheduleCalculated
    std::unordered_map<std::string, pugi::xml_node> schedRecNodes;    // RouteScheduleRecommended

    // 单例对象（全局只有一个）不用进 map，直接存节点即可
    pugi::xml_node routeNode;
    pugi::xml_node infoNode;
    pugi::xml_node wptsContainer;  // RouteWaypoints 容器
    pugi::xml_node aptsContainer;  // RouteActionPoints 容器
    pugi::xml_node schedsContainer;// RouteSchedules 容器

    // 按节点名字把它塞进对应的 map / 变量里
    auto indexChild = [&](pugi::xml_node child) {
        std::string lname = localName(child.name());
        std::string gmlId = child.attribute("gml:id").value();

        if (lname == "Route") {
            routeNode = child;
        } else if (lname == "RouteInfo") {
            infoNode = child;
        } else if (lname == "RouteWaypoints") {
            wptsContainer = child;
        } else if (lname == "RouteWaypoint") {
            if (!gmlId.empty()) waypointNodes[gmlId] = child;
        } else if (lname == "RouteWaypointLeg") {
            if (!gmlId.empty()) legNodes[gmlId] = child;
        } else if (lname == "RouteActionPoints") {
            aptsContainer = child;
        } else if (lname == "RouteActionPoint") {
            if (!gmlId.empty()) actionPointNodes[gmlId] = child;
        } else if (lname == "RouteSchedules") {
            schedsContainer = child;
        } else if (lname == "RouteSchedule") {
            if (!gmlId.empty()) scheduleNodes[gmlId] = child;
        } else if (lname == "RouteScheduleManual") {
            if (!gmlId.empty()) schedManualNodes[gmlId] = child;
        } else if (lname == "RouteScheduleCalculated") {
            if (!gmlId.empty()) schedCalcNodes[gmlId] = child;
        } else if (lname == "RouteScheduleRecommended") {
            if (!gmlId.empty()) schedRecNodes[gmlId] = child;
        } else if (lname == "RouteScheduleElement") {
            if (!gmlId.empty()) schedElementNodes[gmlId] = child;
        }
    };

    // 遍历 <members> 容器下的所有对象，建索引
    // （Edition 2.0 用单一 <members> 包装取代旧版 <member>/<imember>）
    for (pugi::xml_node child : datasetNode.child("members").children()) {
        indexChild(child);
    }

    // ========== 解析 Route ==========
    if (routeNode.empty()) {
        lastError_ = "找不到 Route 元素";
        return nullptr;
    }

    dataset->route = parseRoute(routeNode);

    // ========== 解析 RouteInfo ==========
    // 如果没在索引中找到 RouteInfo，尝试通过 Route 的 href 引用查找
    if (infoNode.empty()) {
        pugi::xml_node infoRef = routeNode.child("routeInfo");
        if (!infoRef.empty()) {
            const char* href = infoRef.attribute("xlink:href").value();
            infoNode = resolveRef(datasetNode, href);
        }
    }

    if (!infoNode.empty() && dataset->route) {
        dataset->route->info = parseRouteInfo(infoNode);
    }

    // ========== 解析空间范围 ==========
    if (!envNode.empty() && dataset->route) {
        // 同样按 localName 匹配，避免受前缀写法 / 空白文本节点影响
        pugi::xml_node lc, uc;
        for (pugi::xml_node sub : envNode.children()) {
            std::string ln = localName(sub.name());
            if (ln == "lowerCorner") lc = sub;
            else if (ln == "upperCorner") uc = sub;
        }
        // 空文本保护：GMIN 等 minimal 文件角点可能为空（<gml:lowerCorner/>），
        // 此时不应把 {0,0} 当作合法边界
        const char* lcText = lc.text().get();
        const char* ucText = uc.text().get();
        if (lcText && lcText[0] && ucText && ucText[0]) {
            dataset->route->lowerCorner = parseGmlPos(lcText);
            dataset->route->upperCorner = parseGmlPos(ucText);
        }
    }

    // ========== Phase 2: 按引用解析各对象 ==========
    if (!dataset->route) return dataset;

    // ---------- 解析 Waypoints ----------
    // 从 RouteWaypoints 容器中的 routeWaypoint xlink:href 引用查找
    // 同时，每个 Waypoint 可能通过自身的 routeWaypointLeg href 关联一个 Leg
    if (!wptsContainer.empty()) {
        dataset->route->waypoints = std::make_unique<RouteWaypoints>();
        for (pugi::xml_node wpRef : wptsContainer.children("routeWaypoint")) {
            const char* href = wpRef.attribute("xlink:href").value();
            pugi::xml_node wpNode = resolveById(waypointNodes, datasetNode, href);

            if (wpNode.empty()) {
                // 最终兜底：resolveRef 全文档搜索
                wpNode = resolveRef(datasetNode, href);
            }

            if (!wpNode.empty()) {
                RouteWaypoint wp = parseWaypoint(wpNode);

                // Waypoint 自身可能有 routeWaypointLeg href，关联 Leg
                pugi::xml_node legRef = wpNode.child("routeWaypointLeg");
                if (!legRef.empty()) {
                    const char* legHref = legRef.attribute("xlink:href").value();
                    pugi::xml_node legNode = resolveById(legNodes, datasetNode, legHref);
                    if (legNode.empty()) {
                        legNode = resolveRef(datasetNode, legHref);
                    }
                    if (!legNode.empty()) {
                        wp.leg = std::make_unique<RouteWaypointLeg>(parseLeg(legNode));
                    }
                }

                dataset->route->waypoints->waypoints.push_back(std::move(wp));
            }
        }
    }

    // ---------- 解析 ActionPoints ----------
    if (!aptsContainer.empty()) {
        dataset->route->actionPoints = std::make_unique<RouteActionPoints>();
        for (pugi::xml_node apRef : aptsContainer.children("routeActionPoint")) {
            const char* href = apRef.attribute("xlink:href").value();
            pugi::xml_node apNode = resolveById(actionPointNodes, datasetNode, href);
            if (apNode.empty()) {
                apNode = resolveRef(datasetNode, href);
            }
            if (!apNode.empty()) {
                dataset->route->actionPoints->actionPoints.push_back(parseActionPoint(apNode));
            }
        }
    }

    // ---------- 解析 Schedules ----------
    if (!schedsContainer.empty()) {
        dataset->route->schedules = std::make_unique<RouteSchedules>();
        for (pugi::xml_node schedRef : schedsContainer.children("routeSchedule")) {
            const char* href = schedRef.attribute("xlink:href").value();
            if (!href || href[0] != '#') continue;
            std::string targetId(href + 1);

            auto it = scheduleNodes.find(targetId);
            if (it != scheduleNodes.end()) {
                RouteSchedule sched;
                pugi::xml_node schedNode = it->second;

                sched.id = schedNode.child("routeScheduleID").text().as_int();
                sched.name = getOptText(schedNode, "routeScheduleName");

                // 查找 Manual/Calculated/Recommended 引用
                pugi::xml_node manualRef = schedNode.child("routeScheduleManual");
                pugi::xml_node calcRef = schedNode.child("routeScheduleCalculated");
                pugi::xml_node recRef = schedNode.child("routeScheduleRecommended");

                // 解析 Manual 元素
                if (!manualRef.empty()) {
                    const char* mHref = manualRef.attribute("xlink:href").value();
                    if (mHref && mHref[0] == '#') {
                        auto mIt = schedManualNodes.find(mHref + 1);
                        if (mIt != schedManualNodes.end()) {
                            if (!sched.manual) sched.manual = std::make_unique<RouteScheduleManual>();
                            for (pugi::xml_node elemRef : mIt->second.children("routeScheduleElement")) {
                                const char* eHref = elemRef.attribute("xlink:href").value();
                                if (eHref && eHref[0] == '#') {
                                    auto eIt = schedElementNodes.find(eHref + 1);
                                    if (eIt != schedElementNodes.end()) {
                                        sched.manual->elements.push_back(parseScheduleElement(eIt->second));
                                    }
                                }
                            }
                        }
                    }
                }

                // 解析 Calculated 元素
                if (!calcRef.empty()) {
                    const char* cHref = calcRef.attribute("xlink:href").value();
                    if (cHref && cHref[0] == '#') {
                        auto cIt = schedCalcNodes.find(cHref + 1);
                        if (cIt != schedCalcNodes.end()) {
                            if (!sched.calculated) sched.calculated = std::make_unique<RouteScheduleCalculated>();
                            for (pugi::xml_node elemRef : cIt->second.children("routeScheduleElement")) {
                                const char* eHref = elemRef.attribute("xlink:href").value();
                                if (eHref && eHref[0] == '#') {
                                    auto eIt = schedElementNodes.find(eHref + 1);
                                    if (eIt != schedElementNodes.end()) {
                                        sched.calculated->elements.push_back(parseScheduleElement(eIt->second));
                                    }
                                }
                            }
                        }
                    }
                }

                // 解析 Recommended 元素
                if (!recRef.empty()) {
                    const char* rHref = recRef.attribute("xlink:href").value();
                    if (rHref && rHref[0] == '#') {
                        auto rIt = schedRecNodes.find(rHref + 1);
                        if (rIt != schedRecNodes.end()) {
                            if (!sched.recommended) sched.recommended = std::make_unique<RouteScheduleRecommended>();
                            for (pugi::xml_node elemRef : rIt->second.children("routeScheduleElement")) {
                                const char* eHref = elemRef.attribute("xlink:href").value();
                                if (eHref && eHref[0] == '#') {
                                    auto eIt = schedElementNodes.find(eHref + 1);
                                    if (eIt != schedElementNodes.end()) {
                                        sched.recommended->elements.push_back(parseScheduleElement(eIt->second));
                                    }
                                }
                            }
                        }
                    }
                }

                dataset->route->schedules->schedules.push_back(std::move(sched));
            } else {
                // 兜底：用旧的 parseSchedule 方法
                pugi::xml_node schedNode = resolveRef(datasetNode, href);
                if (!schedNode.empty()) {
                    dataset->route->schedules->schedules.push_back(parseSchedule(schedNode));
                }
            }
        }
    }

    return dataset;
}

// ============================================================
//  parseRoute
// ============================================================
std::unique_ptr<Route> S421Parser::parseRoute(pugi::xml_node node) {
    auto route = std::make_unique<Route>();

    route->formatVersion = node.child("routeFormatVersion").text().get();
    route->id = node.child("routeID").text().get();
    route->editionNo = node.child("routeEditionNo").text().as_int();

    // 解析 Route 级别的 Extensions
    for (pugi::xml_node ext : node.children("routeExtensions")) {
        route->extensions = parseExtension(ext);
    }

    return route;
}

// ============================================================
//  parseRouteInfo
// ============================================================
RouteInfo S421Parser::parseRouteInfo(pugi::xml_node node) {
    RouteInfo info;

    // 必填
    info.name = node.child("routeInfoName").text().get();
    // 2.0：status 存的是文本（如 "Planned"），用 fromString 解析
    {
        std::string statusText = node.child("routeInfoStatus").text().get();
        RouteInfoStatus st;
        info.status = fromString(statusText, st) ? st : RouteInfoStatus::Initial;
    }

    info.author = node.child("routeInfoAuthor").text().get();
    info.editionTime = node.child("routeInfoEditionTime").text().get();
    info.description = node.child("routeInfoDescription").text().get();
    info.validityStart = node.child("routeInfoValidityStart").text().get();
    info.validityEnd = node.child("routeInfoValidityEnd").text().get();

    // 港口
    info.departurePortID1 = getOptText(node, "routeInfoDeparturePortID1");
    info.departurePortID2 = getOptText(node, "routeInfoDeparturePortID2");
    info.departurePortCall = getOptText(node, "routeInfoDeparturePortCall");
    info.arrivalPortID1 = getOptText(node, "routeInfoArrivalPortID1");
    info.arrivalPortID2 = getOptText(node, "routeInfoArrivalPortID2");
    info.arrivalPortCall = getOptText(node, "routeInfoArrivalPortCall");

    // 引用
    info.referencePrevRoute = getOptText(node, "routeInfoReferencePrevRoute");
    info.referenceNextRoute = getOptText(node, "routeInfoReferenceNextRoute");

    // 船舶
    // 2.0：routeInfoVesselType(数字) 删除，改为 typeOfShip(文本枚举)
    if (auto ts = getOptText(node, "typeOfShip")) {
        TypeOfShip t;
        if (fromString(*ts, t)) info.vesselType = t;
    }
    info.masterApproval = getOptBool(node, "routeInfoMasterApproval");
    info.vesselName = getOptText(node, "routeInfoVesselName");
    info.vesselMMSI = node.child("routeInfoVesselMMSI").text().get();
    info.vesselCallsign = getOptText(node, "routeInfoVesselCallsign");
    info.vesselIMO = getOptText(node, "routeInfoVesselIMO");
    info.vesselVoyage = getOptText(node, "routeInfoVesselVoyage");

    // 尺寸
    info.vesselHeight = getOptDouble(node, "routeInfoVesselHeight");
    info.vesselLength = getOptDouble(node, "routeInfoVesselLength");
    info.vesselBeam = getOptDouble(node, "routeInfoVesselBeam");
    info.draftMax = getOptDouble(node, "routeInfoDraftMax");
    info.airDraftMax = getOptDouble(node, "routeInfoAirDraftMax");
    info.beamMax = getOptDouble(node, "routeInfoBeamMax");
    info.lengthMax = getOptDouble(node, "routeInfoLengthMax");

    // Extensions
    for (pugi::xml_node ext : node.children("routeInfoExtensions")) {
        for (pugi::xml_node re : ext.children("routeExtensions")) {
            info.extensions = parseExtension(re);
        }
    }

    return info;
}

// ============================================================
//  parseWaypoint
// ============================================================
RouteWaypoint S421Parser::parseWaypoint(pugi::xml_node node) {
    RouteWaypoint wp;

    // 几何坐标
    pugi::xml_node geomNode = node.child("geometry");
    if (!geomNode.empty()) {
        wp.geometry = parsePointGeometry(geomNode);
    }

    // 必填属性
    wp.id = node.child("routeWaypointID").text().as_int();
    wp.turnRadius = node.child("routeWaypointTurnRadius").text().as_double();

    // 可选属性
    wp.name = node.child("routeWaypointName").text().get();
    wp.externalRefID = getOptText(node, "routeWaypointExternalReferenceID");
    if (auto f = getOptBool(node, "routeWaypointFixed")) wp.fixed = *f;

    // Extensions
    for (pugi::xml_node ext : node.children("routeWaypointExtensions")) {
        for (pugi::xml_node re : ext.children("routeExtensions")) {
            wp.extensions = parseExtension(re);
        }
    }

    return wp;
}

// ============================================================
//  parseLeg
// ============================================================
RouteWaypointLeg S421Parser::parseLeg(pugi::xml_node node) {
    RouteWaypointLeg leg;

    // 必填：几何类型（2.0：文本 loxodrome/orthodrome，用前缀匹配 fromString）
    {
        std::string geoText = node.child("routeWaypointLegGeometryType").text().get();
        LegGeometryType gt;
        if (fromString(geoText, gt)) leg.geometryType = gt;
    }

    // 横偏距
    leg.starboardXTDL = getOptInt(node, "routeWaypointLegStarboardXTDL");
    leg.portXTDL = getOptInt(node, "routeWaypointLegPortXTDL");
    leg.starboardCL = getOptInt(node, "routeWaypointLegStarboardCL");
    leg.portCL = getOptInt(node, "routeWaypointLegPortCL");

    // 安全参数
    leg.safetyContour = getOptDouble(node, "routeWaypointLegSafetyContour");
    leg.safetyDepth = getOptDouble(node, "routeWaypointLegSafetyDepth");
    leg.staticUKC = getOptDouble(node, "routeWaypointLegStaticUKC");
    leg.dynamicUKC = getOptDouble(node, "routeWaypointLegDynamicUKC");
    leg.safetyMargin = getOptDouble(node, "routeWaypointLegSafetyMargin");

    // 航速
    leg.sogMin = getOptDouble(node, "routeWaypointLegSOGMin");
    leg.sogMax = getOptDouble(node, "routeWaypointLegSOGMax");
    leg.stwMin = getOptDouble(node, "routeWaypointLegSTWMin");
    leg.stwMax = getOptDouble(node, "routeWaypointLegSTWMax");

    // 吃水
    leg.draft = getOptDouble(node, "routeWaypointLegDraft");
    leg.draftForward = getOptDouble(node, "routeWaypointLegDraftForward");
    leg.draftAft = getOptDouble(node, "routeWaypointLegDraftAft");
    leg.draftMax = getOptDouble(node, "routeWaypointLegDraftMax");
    leg.airDraftMax = getOptDouble(node, "routeWaypointLegAirDraftMax");
    leg.beamMax = getOptDouble(node, "routeWaypointLegBeamMax");
    leg.lengthMax = getOptDouble(node, "routeWaypointLegLengthMax");

    // 备注/问题
    leg.note = getOptText(node, "routeWaypointLegNote");
    leg.issue = getOptText(node, "routeWaypointLegIssue");

    // Extensions
    for (pugi::xml_node ext : node.children("routeWaypointLegExtensions")) {
        for (pugi::xml_node re : ext.children("routeExtensions")) {
            leg.extensions = parseExtension(re);
        }
    }

    return leg;
}

// ============================================================
//  parseActionPoint
// ============================================================
RouteActionPoint S421Parser::parseActionPoint(pugi::xml_node node) {
    RouteActionPoint ap;

    ap.id = node.child("routeActionPointID").text().as_int();
    ap.name = getOptText(node, "routeActionPointName");
    ap.externalRef = getOptText(node, "routeActionPointExternalReference");
    
    pugi::xml_node geomNode = node.child("geometry");
    if (!geomNode.empty()) {
        ap.geometry = parsePointGeometry(geomNode);
    }

    ap.distance = node.child("routeActionPointDistance").text().as_double();

    {
        pugi::xml_node tta = node.child("routeActionPointTimetoAct");
        if (tta.empty())
        {
            tta = node.child("routeActionPointTimeToAct");
        }
        ap.timeToAct = tta.text().as_double();
    }

    // 2.0：requiredAction 存的是文本（如 "Report"），用 fromString 解析
    {
        std::string actText = node.child("routeActionPointRequiredAction").text().get();
        ActionType at;
        if (fromString(actText, at)) ap.requiredAction = at;
    }

    ap.actionDescription = node.child("routeActionPointRequiredActionDescription").text().get();

    // Extensions
    for (pugi::xml_node ext : node.children("routeActionPointExtensions")) {
        for (pugi::xml_node re : ext.children("routeExtensions")) {
            ap.extensions = parseExtension(re);
        }
    }
    return ap;
}

// ============================================================
//  parseSchedule - 解析完整的调度链
//  调度有三层结构：RouteSchedule → Manual/Calculated/Recommended → Element
// ============================================================
RouteSchedule S421Parser::parseSchedule(pugi::xml_node node) {
    RouteSchedule sched;

    sched.id = node.child("routeScheduleID").text().as_int();
    sched.name = getOptText(node, "routeScheduleName");

    // 注意: Manual/Calculated/Recommended 的引用和 Element 可能在文档的不同 imember 中
    // 这里需要通过 resolveRef 找到它们
    // 但由于 pugixml 已加载整个文档，我们直接遍历 imember 查找

    pugi::xml_node docRoot = node.root();

    // 遍历所有 imember，查找属于此 Schedule 的 Manual/Calculated/Recommended
    for (pugi::xml_node imember = node.parent().next_sibling("imember");
         !imember.empty();
         imember = imember.next_sibling("imember")) {

        for (pugi::xml_node child : imember.children()) {
            std::string name = child.name();

            if (name.find("RouteScheduleManual") != std::string::npos) {
                if (!sched.manual) sched.manual = std::make_unique<RouteScheduleManual>();
                for (pugi::xml_node elemRef : child.children("routeScheduleElement")) {
                    const char* href = elemRef.attribute("xlink:href").value();
                    pugi::xml_node elemNode = resolveRef(docRoot, href);
                    if (!elemNode.empty()) {
                        sched.manual->elements.push_back(parseScheduleElement(elemNode));
                    }
                }
            }
            else if (name.find("RouteScheduleCalculated") != std::string::npos) {
                if (!sched.calculated) sched.calculated = std::make_unique<RouteScheduleCalculated>();
                for (pugi::xml_node elemRef : child.children("routeScheduleElement")) {
                    const char* href = elemRef.attribute("xlink:href").value();
                    pugi::xml_node elemNode = resolveRef(docRoot, href);
                    if (!elemNode.empty()) {
                        sched.calculated->elements.push_back(parseScheduleElement(elemNode));
                    }
                }
            }
            else if (name.find("RouteScheduleRecommended") != std::string::npos) {
                if (!sched.recommended) sched.recommended = std::make_unique<RouteScheduleRecommended>();
                for (pugi::xml_node elemRef : child.children("routeScheduleElement")) {
                    const char* href = elemRef.attribute("xlink:href").value();
                    pugi::xml_node elemNode = resolveRef(docRoot, href);
                    if (!elemNode.empty()) {
                        sched.recommended->elements.push_back(parseScheduleElement(elemNode));
                    }
                }
            }
        }
    }

    return sched;
}

// ============================================================
//  parseScheduleElement
// ============================================================
RouteScheduleElement S421Parser::parseScheduleElement(pugi::xml_node node) {
    RouteScheduleElement elem;

    // routeWaypointID 大小写：2.0 用大写 ID，旧版小写 Id；先取大写，取不到回退小写
    pugi::xml_node wpIdNode = node.child("routeWaypointID");
    if (wpIdNode.empty()) wpIdNode = node.child("routeWaypointId");
    elem.waypointId = wpIdNode.text().as_int();
    elem.planSOG = node.child("routeScheduleElementPlanSOG").text().as_double();
    elem.etd = node.child("routeScheduleElementETD").text().get();
    elem.eta = node.child("routeScheduleElementETA").text().get();
    elem.etdWindowBefore = getOptInt(node, "routeScheduleElementETDWindowBefore");
    elem.etdWindowAfter = getOptInt(node, "routeScheduleElementETDWindowAfter");
    elem.etaWindowBefore = getOptInt(node, "routeScheduleElementETAWindowBefore");
    elem.etaWindowAfter = getOptInt(node, "routeScheduleElementETAWindowAfter");
    elem.note = getOptText(node, "routeScheduleElementNote");

    return elem;
}

} // namespace s421
