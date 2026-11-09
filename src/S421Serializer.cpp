// S421Serializer.cpp - S-421 数据序列化器实现
//
// 三种输出格式：
//   serializeToFile()        → .s421 XML 文件
//   serializeToCompressed()  → .s421p ZIP 压缩包（ZIP 内含一个 .s421）
//   serializeToExchangeSet() → S-100 兼容交换集（目录结构 + CATALOG + 元数据）

#include "s421/S421Serializer.h"
#include "s421/MiniZip.h"
#include "s421/S421Fs.h"
#include "S421FieldSpec.h"
#include "S421FieldTable.h"
#include <pugixml.hpp>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <cstring>

namespace s421 {

// WGS84 坐标参照系完整 URI
static const char* const kSrsNameWgs84 =
    "http://www.opengis.net/def/crs/EPSG/0/4326";

S421Serializer::S421Serializer() = default;
S421Serializer::~S421Serializer() = default;

// ============================================================
//  字段输出策略说明（已上移到 FieldSpec 表驱动）
//
//  全部"简单字段 + extensions"的写出由 S421FieldTable.h 的表驱动
//  （fieldspec::writeFields），表内容（元素名/类别/required/顺序）由 XSD
//  生成并交叉校验——Parser 与 Serializer 共用同一份映射，改名/增删字段
//  只需改 XSD + 重新生成。输出约定：
//    required（XSD minOccurs>=1）恒写；
//    否则按值成员空串/0/false 视为未设置不写；optional 有值才写。
//  本文件保留的代码只负责对象图结构：几何、xlink 引用、容器与成员顺序。
// ============================================================

void S421Serializer::setCoordinatePrecision(int digits) {
    coordPrecision_ = digits;
}

// public API

bool S421Serializer::serializeToFile(const Dataset& dataset, const std::string& filePath) {
    lastError_.clear();
    pugi::xml_document doc;

    if (!buildDocument(doc, dataset)) {
        return false;
    }

    if (!doc.save_file(filePath.c_str(), "    ", pugi::format_indent)) {
        lastError_ = "无法写入文件: " + filePath;
        return false;
    }
    return true;
}

std::string S421Serializer::serializeToString(const Dataset& dataset) {
    lastError_.clear();
    pugi::xml_document doc;

    if (!buildDocument(doc, dataset)) {
        return "";
    }

    std::ostringstream oss;
    doc.save(oss, "    ", pugi::format_indent);
    return oss.str();
}

// 从 Dataset 构建所有对象的 gml:id 映射
S421Serializer::IdMap S421Serializer::buildIdMap(const Dataset& dataset) const {
    IdMap ids;

    // 固定 ID
    ids.routeId         = "RTE";
    ids.infoId          = "RTE.INFO";
    ids.wptsContainerId = "RTE.WPTS";
    ids.aptsContainerId = "RTE.APTS";
    ids.schedsContainerId = "RTE.SCHEDS";

    if (!dataset.route) return ids;
    const Route& route = *dataset.route;

    // 航点 ID（2.0 模型：route.waypoints 为 unique_ptr<RouteWaypoints>，内含值向量）
    if (route.waypoints) {
        for (const auto& wp : route.waypoints->waypoints) {
            ids.waypointIds[wp.id] = "RTE.WPT." + std::to_string(wp.id);
        }
    }

    // 航段 ID（顺序编号，跳过没有 leg 的航点）
    int legSeq = 1;
    if (route.waypoints) {
        for (const auto& wp : route.waypoints->waypoints) {
            if (wp.leg) {
                ids.legIds[legSeq] = "RTE.WPT.LEG." + std::to_string(legSeq);
                legSeq++;
            }
        }
    }

    // 动作点 ID
    if (route.actionPoints) {
        for (const auto& ap : route.actionPoints->actionPoints) {
            ids.actionPointIds[ap.id] = "RTE.APT." + std::to_string(ap.id);
        }
    }

    // 调度 ID
    if (route.schedules) {
        for (const auto& sched : route.schedules->schedules) {
            int sid = sched.id;
            ids.scheduleIds[sid] = "RTE.SCHED." + std::to_string(sid);
            ids.schedManualIds[sid] = "RTE.SCHED." + std::to_string(sid) + ".MAN";
            ids.schedCalcIds[sid]   = "RTE.SCHED." + std::to_string(sid) + ".CAL";
            ids.schedRecIds[sid]    = "RTE.SCHED." + std::to_string(sid) + ".REC";

            // 调度元素 ID（2.0 模型：manual/calculated/recommended 三个 unique_ptr 子对象各含 elements）
            int elemSeq = 1;
            if (sched.manual) {
                for (const auto& e : sched.manual->elements) {
                    std::string key = "MANUAL." + std::to_string(sid) + "." + std::to_string(elemSeq);
                    ids.elementIds[key] = "RTE.SCHED." + std::to_string(sid) + ".MAN.ELEMENT." + std::to_string(elemSeq);
                    elemSeq++;
                }
            }
            elemSeq = 1;
            if (sched.calculated) {
                for (const auto& e : sched.calculated->elements) {
                    std::string key = "CALC." + std::to_string(sid) + "." + std::to_string(elemSeq);
                    ids.elementIds[key] = "RTE.SCHED." + std::to_string(sid) + ".CAL.ELEMENT." + std::to_string(elemSeq);
                    elemSeq++;
                }
            }
            elemSeq = 1;
            if (sched.recommended) {
                for (const auto& e : sched.recommended->elements) {
                    std::string key = "REC." + std::to_string(sid) + "." + std::to_string(elemSeq);
                    ids.elementIds[key] = "RTE.SCHED." + std::to_string(sid) + ".REC.ELEMENT." + std::to_string(elemSeq);
                    elemSeq++;
                }
            }
        }
    }

    return ids;
}

// 构建xml
bool S421Serializer::buildDocument(pugi::xml_document& doc, const Dataset& dataset) {
    if (!dataset.route) {
        lastError_ = "Dataset 没有 Route 数据";
        return false;
    }

    IdMap ids = buildIdMap(dataset);

    // XML 声明
    pugi::xml_node decl = doc.prepend_child(pugi::node_declaration);
    decl.append_attribute("version") = "1.0";
    decl.append_attribute("encoding") = "UTF-8";

    // 根元素（2.0 用默认命名空间，无 S421: 前缀）
    pugi::xml_node root = doc.append_child("Dataset");
    writeDatasetRoot(root, dataset, ids);

    // 1. boundedBy（空间范围）
    writeBoundedBy(root, *dataset.route);

    // 2. DatasetIdentificationInformation（2.0 新增的 S100 文件级元数据块）
    {
        pugi::xml_node dii = root.append_child("S100:DatasetIdentificationInformation");
        dii.append_child("S100:encodingSpecification").text().set("S-100 Part 10b");
        dii.append_child("S100:encodingSpecificationEdition").text().set("1.0");
        dii.append_child("S100:productIdentifier").text().set("S-421");
        dii.append_child("S100:productEdition").text().set("2.0");
        dii.append_child("S100:applicationProfile").text().set("S-421 GML");
        dii.append_child("S100:datasetFileIdentifier").text().set(dataset.gmlId.c_str());
        dii.append_child("S100:datasetTitle").text().set(
            dataset.route->info.name.empty() ? "S-421 Route" : dataset.route->info.name.c_str());
        dii.append_child("S100:datasetReferenceDate").text().set("2026-06-04");
        dii.append_child("S100:datasetLanguage").text().set("ENG");
        dii.append_child("S100:datasetTopicCategory").text().set("transportation");
        dii.append_child("S100:datasetPurpose").text().set("base");
        dii.append_child("S100:updateNumber").text().set("0");
    }

    // 3. 所有业务对象统一放在 <members> 下（2.0 取代旧版 member/imember 分装）
    pugi::xml_node members = root.append_child("members");

    // 3a. RouteInfo（2.0 模型：Route::info 为按值成员，恒存在）
    writeRouteInfo(members, dataset.route->info, ids);

    // 3b. Schedules（容器 + Schedule + Manual/Calc/Rec 容器 + Element）
    // 2.0 模型：route.schedules 为 unique_ptr<RouteSchedules>，内含值向量；
    //           每个 RouteSchedule 通过 manual/calculated/recommended 三个 unique_ptr 持有子调度
    if (dataset.route->schedules && !dataset.route->schedules->schedules.empty()) {
        writeSchedulesContainer(members, *dataset.route, ids);

        for (const auto& sched : dataset.route->schedules->schedules) {
            int sid = sched.id;
            std::string schedGmlId = "RTE.SCHED." + std::to_string(sid);

            // RouteSchedule 自身
            writeSchedule(members, sched, ids);

            // Manual 容器 + 元素
            if (sched.manual && !sched.manual->elements.empty()) {
                std::string manId = ids.schedManualIds.at(sid);
                pugi::xml_node manNode = members.append_child("RouteScheduleManual");
                manNode.append_attribute("gml:id") = manId.c_str();

                // extensions 走表（XSD 顺序：extensions 在 collection 引用之前）
                fieldspec::writeFields(manNode, sched.manual.get(),
                                       fieldspec::kRouteScheduleManualFields,
                                       fieldspec::kRouteScheduleManualFieldsCount,
                                       manId);

                writeRef(manNode, "routeScheduleCollection", schedGmlId,
                         "http://www.iho.int/gml/roles/routeScheduleCollection");

                int elemSeq = 1;
                for (const auto& elem : sched.manual->elements) {
                    std::string key = "MANUAL." + std::to_string(sid) + "." + std::to_string(elemSeq);
                    std::string elemId = ids.elementIds.at(key);
                    writeRef(manNode, "routeScheduleElementManual", elemId,
                             "http://www.iho.int/gml/roles/routeScheduleElement");
                    elemSeq++;
                }

                elemSeq = 1;
                for (const auto& elem : sched.manual->elements) {
                    std::string key = "MANUAL." + std::to_string(sid) + "." + std::to_string(elemSeq);
                    std::string elemId = ids.elementIds.at(key);
                    writeScheduleElement(members, elem, elemId, manId,
                                         "routeScheduleElementManualCollection", ids);
                    elemSeq++;
                }
            }

            // Calculated 容器 + 元素
            if (sched.calculated && !sched.calculated->elements.empty()) {
                std::string calId = ids.schedCalcIds.at(sid);
                pugi::xml_node calNode = members.append_child("RouteScheduleCalculated");
                calNode.append_attribute("gml:id") = calId.c_str();

                // extensions 走表（XSD 顺序：extensions 在 collection 引用之前）
                fieldspec::writeFields(calNode, sched.calculated.get(),
                                       fieldspec::kRouteScheduleCalculatedFields,
                                       fieldspec::kRouteScheduleCalculatedFieldsCount,
                                       calId);

                writeRef(calNode, "routeScheduleCollection", schedGmlId,
                         "http://www.iho.int/gml/roles/routeScheduleCollection");

                int elemSeq = 1;
                for (const auto& elem : sched.calculated->elements) {
                    std::string key = "CALC." + std::to_string(sid) + "." + std::to_string(elemSeq);
                    std::string elemId = ids.elementIds.at(key);
                    writeRef(calNode, "routeScheduleElementCalculated", elemId,
                             "http://www.iho.int/gml/roles/routeScheduleElement");
                    elemSeq++;
                }

                elemSeq = 1;
                for (const auto& elem : sched.calculated->elements) {
                    std::string key = "CALC." + std::to_string(sid) + "." + std::to_string(elemSeq);
                    std::string elemId = ids.elementIds.at(key);
                    writeScheduleElement(members, elem, elemId, calId,
                                         "routeScheduleElementCalculatedCollection", ids);
                    elemSeq++;
                }
            }

            // Recommended 容器 + 元素
            if (sched.recommended && !sched.recommended->elements.empty()) {
                std::string recId = ids.schedRecIds.at(sid);
                pugi::xml_node recNode = members.append_child("RouteScheduleRecommended");
                recNode.append_attribute("gml:id") = recId.c_str();

                // extensions 走表（XSD 顺序：extensions 在 collection 引用之前）
                fieldspec::writeFields(recNode, sched.recommended.get(),
                                       fieldspec::kRouteScheduleRecommendedFields,
                                       fieldspec::kRouteScheduleRecommendedFieldsCount,
                                       recId);

                writeRef(recNode, "routeScheduleCollection", schedGmlId,
                         "http://www.iho.int/gml/roles/routeSchedule");

                int elemSeq = 1;
                for (const auto& elem : sched.recommended->elements) {
                    std::string key = "REC." + std::to_string(sid) + "." + std::to_string(elemSeq);
                    std::string elemId = ids.elementIds.at(key);
                    writeRef(recNode, "routeScheduleElementRecommended", elemId,
                             "http://www.iho.int/gml/roles/routeScheduleElement");
                    elemSeq++;
                }

                elemSeq = 1;
                for (const auto& elem : sched.recommended->elements) {
                    std::string key = "REC." + std::to_string(sid) + "." + std::to_string(elemSeq);
                    std::string elemId = ids.elementIds.at(key);
                    writeScheduleElement(members, elem, elemId, recId,
                                         "routeScheduleElementRecommendedCollection", ids);
                    elemSeq++;
                }
            }
        }
    }

    // 3c. Route
    writeRoute(members, *dataset.route, ids);

    // 3d. RouteWaypoints 容器 + 逐个 Waypoint + Leg
    if (dataset.route->waypoints && !dataset.route->waypoints->waypoints.empty()) {
        const std::vector<RouteWaypoint>& wps = dataset.route->waypoints->waypoints;
        writeWaypointsContainer(members, *dataset.route, ids);

        int runningLegSeq = 1;
        for (size_t i = 0; i < wps.size(); i++) {
            const RouteWaypoint& wp = wps[i];

            const RouteWaypoint* nextWp = nullptr;
            if (i + 1 < wps.size()) {
                nextWp = &wps[i + 1];
            }

            writeWaypoint(members, wp, nextWp, runningLegSeq, ids);
            if (wp.leg) runningLegSeq++;
        }

        int legSeq = 1;
        for (size_t i = 0; i < wps.size(); i++) {
            const RouteWaypoint& wp = wps[i];
            if (!wp.leg) continue;

            const GM_Point startPos = wp.geometry;
            // 终点 = 下一个航点的坐标。末尾航点没有"下一个"时保持空
            s421::optional<GM_Point> endPos;
            if (i + 1 < wps.size()) {
                endPos = wps[i + 1].geometry;
            }

            writeLeg(members, *wp.leg, startPos, endPos, legSeq, ids);
            legSeq++;
        }
    }

    // 3e. ActionPoints 容器 + 逐个 ActionPoint
    if (dataset.route->actionPoints && !dataset.route->actionPoints->actionPoints.empty()) {
        writeActionPointsContainer(members, *dataset.route, ids);
        for (const auto& ap : dataset.route->actionPoints->actionPoints) {
            writeActionPoint(members, ap, ids);
        }
    }

    return true;
}

void S421Serializer::writeDatasetRoot(pugi::xml_node& root,
                                       const Dataset& dataset, const IdMap& ids) {
    // gml:id
    root.append_attribute("gml:id") = dataset.gmlId.c_str();

    // 命名空间声明（Edition 2.0）
    root.append_attribute("xmlns") = "http://www.iec.ch/S421/2.0";
    root.append_attribute("xmlns:S421") = "http://www.iec.ch/S421/2.0";
    root.append_attribute("xmlns:xlink") = "http://www.w3.org/1999/xlink";
    root.append_attribute("xmlns:s100_profile") = "http://www.iho.int/S-100/profile/s100_gmlProfile";
    root.append_attribute("xmlns:S100") = "http://www.iho.int/s100gml/5.0";
    root.append_attribute("xmlns:gml") = "http://www.opengis.net/gml/3.2";
    root.append_attribute("xmlns:xsi") = "http://www.w3.org/2001/XMLSchema-instance";
    root.append_attribute("xsi:schemaLocation") =
        "http://www.iec.ch/S421/2.0 S-421_schema_20260604.xsd";
}

// 空间范围
void S421Serializer::writeBoundedBy(pugi::xml_node& parent, const Route& route) {
    if (!route.lowerCorner.has_value() || !route.upperCorner.has_value()) return;

    pugi::xml_node boundedBy = parent.append_child("gml:boundedBy");
    pugi::xml_node envelope = boundedBy.append_child("gml:Envelope");
    envelope.append_attribute("srsName") = kSrsNameWgs84;
    envelope.append_attribute("srsDimension") = "2";

    pugi::xml_node lc = envelope.append_child("gml:lowerCorner");
    lc.text().set(formatPos(route.lowerCorner.value()).c_str());

    pugi::xml_node uc = envelope.append_child("gml:upperCorner");
    uc.text().set(formatPos(route.upperCorner.value()).c_str());
}

// 航线 —— 简单字段 + extensions 走表；引用是对象图结构，手写
void S421Serializer::writeRoute(pugi::xml_node& parent, const Route& route, const IdMap& ids) {
    pugi::xml_node routeNode = parent.append_child("Route");
    routeNode.append_attribute("gml:id") = ids.routeId.c_str();

    // 表序 = XSD RouteType xs:sequence：
    //   FormatVersion → ID → EditionNo → Extensions（表内输出到此为止）
    fieldspec::writeFields(routeNode, &route,
                           fieldspec::kRouteFields, fieldspec::kRouteFieldsCount,
                           ids.routeId);

    // 引用（指向容器对象）
    writeRef(routeNode, "routeInfo", ids.infoId,
             "http://www.iho.int/gml/roles/routeInfo");

    // routeSchedules 必须写在 routeWaypoints 之前
    if (route.schedules && !route.schedules->schedules.empty()) {
        writeRef(routeNode, "routeSchedules", ids.schedsContainerId,
                 "http://www.iho.int/gml/roles/routeSchedules");
    }
    // 注意：必须与 writeWaypointsContainer（buildDocument 中受 waypoints 空保护）的条件保持一致。否则空数据集会产出一条引用 '#RTE.WPTS' 的悬空引用
    if (route.waypoints && !route.waypoints->waypoints.empty()) {
        writeRef(routeNode, "routeWaypoints", ids.wptsContainerId,
                 "http://www.iho.int/gml/roles/routeWaypoints");
    }

    if (route.actionPoints && !route.actionPoints->actionPoints.empty()) {
        writeRef(routeNode, "routeActionPoints", ids.aptsContainerId,
                 "http://www.iho.int/gml/roles/routeActionPoints");
    }
}

// 航线信息 —— 全部为简单字段 + extensions，整体走表（约 30 个字段一处声明）
void S421Serializer::writeRouteInfo(pugi::xml_node& parent, const RouteInfo& info,
                                     const IdMap& ids) {
    pugi::xml_node infoNode = parent.append_child("RouteInfo");
    infoNode.append_attribute("gml:id") = ids.infoId.c_str();

    fieldspec::writeFields(infoNode, &info,
                           fieldspec::kRouteInfoFields, fieldspec::kRouteInfoFieldsCount,
                           ids.infoId);
}

void S421Serializer::writeWaypointsContainer(pugi::xml_node& parent,
                                              const Route& route, const IdMap& ids) {
    pugi::xml_node container = parent.append_child("RouteWaypoints");
    container.append_attribute("gml:id") = ids.wptsContainerId.c_str();

    // 反向引用到 Route
    writeRef(container, "routeWaypointsCollection", ids.routeId,
             "http://www.iho.int/gml/roles/routeWaypointsCollection");

    // 引用每个航点（2.0 模型：route.waypoints 为 unique_ptr<RouteWaypoints>）
    if (route.waypoints) {
        for (const auto& wp : route.waypoints->waypoints) {
            auto it = ids.waypointIds.find(wp.id);
            if (it != ids.waypointIds.end()) {
                writeRef(container, "routeWaypoint", it->second,
                         "http://www.iho.int/gml/roles/consistsOfRouteWaypoint");
            }
        }
    }
}

void S421Serializer::writeWaypoint(pugi::xml_node& parent, const RouteWaypoint& wp,
                                    const RouteWaypoint* nextWp, int wpIndex,
                                    const IdMap& ids) {
    auto it = ids.waypointIds.find(wp.id);
    std::string wpGmlId = (it != ids.waypointIds.end()) ? it->second : "RTE.WPT." + std::to_string(wp.id);

    pugi::xml_node wpNode = parent.append_child("RouteWaypoint");
    wpNode.append_attribute("gml:id") = wpGmlId.c_str();

    // 简单字段 + extensions 走表（表序 = XSD RouteWaypointType xs:sequence，
    // 输出到 Extensions 为止；targetWaypoint 在 2.0 模型中已不存在——
    // 航点与调度元素的关联由 RouteScheduleElement::waypointId 表达）
    fieldspec::writeFields(wpNode, &wp,
                           fieldspec::kRouteWaypointFields, fieldspec::kRouteWaypointFieldsCount,
                           wpGmlId);

    // 反向引用到容器
    writeRef(wpNode, "routeWaypointCollection", ids.wptsContainerId,
             "http://www.iho.int/gml/roles/routeWaypointCollection");

    // 引用关联的 Leg（wpIndex 现在是当前 leg 序号）
    if (wp.leg) {
        writeRef(wpNode, "routeWaypointLeg",
                 "RTE.WPT.LEG." + std::to_string(wpIndex),
                 "http://www.iho.int/gml/roles/routeWaypointLeg");
    }

    // 几何（Point）——XSD sequence 的最后一个元素
    writePointGeometry(wpNode, wp.geometry, wpGmlId + ".PT");
}

// ============================================================
//  writeLeg
// ============================================================
void S421Serializer::writeLeg(pugi::xml_node& parent, const RouteWaypointLeg& leg,
                               const GM_Point& startPos,
                               const s421::optional<GM_Point>& endPos,
                               int legIndex, const IdMap& ids) {
    auto it = ids.legIds.find(legIndex);
    std::string legGmlId = (it != ids.legIds.end()) ? it->second : "RTE.WPT.LEG." + std::to_string(legIndex);

    pugi::xml_node legNode = parent.append_child("RouteWaypointLeg");
    legNode.append_attribute("gml:id") = legGmlId.c_str();

    // 本函数内所有子元素的输出顺序严格遵循 XSD 的 xs:sequence
    // （RouteWaypointLegType）：sequence 元素顺序不可乱，否则 schema 校验失败。
    // 简单字段 + extensions 全部走表（表序 = XSD 序）；geometry 是 sequence
    // 的最后一个元素，由本函数末尾手写。

    fieldspec::writeFields(legNode, &leg,
                           fieldspec::kRouteWaypointLegFields, fieldspec::kRouteWaypointLegFieldsCount,
                           legGmlId);

    // 反向引用：指向关联的 Waypoint
    // Leg 的 waypointLegCollection 指向下一个航点（Leg 的终点）
    std::string targetWpId = "RTE.WPT." + std::to_string(legIndex + 1);
    writeRef(legNode, "routeWaypointLegCollection", targetWpId,
             "http://www.iho.int/gml/roles/routeWaypointLegCollection");

    // 几何（Point→Curve）——XSD sequence 的最后一个元素。
    if (endPos.has_value()) {
        writeCurveGeometry(legNode, startPos, endPos.value(), legGmlId + ".CV");
    }
}

void S421Serializer::writeActionPointsContainer(pugi::xml_node& parent,
                                                  const Route& route, const IdMap& ids) {
    pugi::xml_node container = parent.append_child("RouteActionPoints");
    container.append_attribute("gml:id") = ids.aptsContainerId.c_str();

    // 反向引用到 Route
    writeRef(container, "routeActionPointsCollection", ids.routeId,
             "http://www.iho.int/gml/roles/routeActionPointsCollection");

    // 引用每个动作点（2.0 模型：route.actionPoints 为 unique_ptr<RouteActionPoints>）
    if (route.actionPoints) {
        for (const auto& ap : route.actionPoints->actionPoints) {
            auto it = ids.actionPointIds.find(ap.id);
            if (it != ids.actionPointIds.end()) {
                writeRef(container, "routeActionPoint", it->second,
                         "http://www.iho.int/gml/roles/routeActionPoint");
            }
        }
    }
}

void S421Serializer::writeActionPoint(pugi::xml_node& parent,
                                       const RouteActionPoint& ap, const IdMap& ids) {
    auto it = ids.actionPointIds.find(ap.id);
    std::string apGmlId = (it != ids.actionPointIds.end()) ? it->second : "RTE.APT." + std::to_string(ap.id);

    pugi::xml_node apNode = parent.append_child("RouteActionPoint");
    apNode.append_attribute("gml:id") = apGmlId.c_str();

    // 简单字段 + extensions 走表（表序 = XSD RouteActionPointType xs:sequence，
    // 输出到 Extensions 为止；Collection 引用与 geometry 由下方手写）
    fieldspec::writeFields(apNode, &ap,
                           fieldspec::kRouteActionPointFields, fieldspec::kRouteActionPointFieldsCount,
                           apGmlId);

    // 反向引用
    writeRef(apNode, "routeActionPointCollection", ids.aptsContainerId,
             "http://www.iho.int/gml/roles/routeActionPointCollection");

    // 几何（Point）——XSD sequence 的最后一个元素
    if (ap.geometry.lat != 0.0 || ap.geometry.lon != 0.0) {
        writePointGeometry(apNode, ap.geometry, apGmlId + ".PT");
    }
}

void S421Serializer::writeSchedulesContainer(pugi::xml_node& parent,
                                              const Route& route, const IdMap& ids) {
    pugi::xml_node container = parent.append_child("RouteSchedules");
    container.append_attribute("gml:id") = ids.schedsContainerId.c_str();

    // 引用每个调度（2.0 模型：route.schedules 为 unique_ptr<RouteSchedules>）
    if (route.schedules) {
        for (const auto& sched : route.schedules->schedules) {
            auto it = ids.scheduleIds.find(sched.id);
            if (it != ids.scheduleIds.end()) {
                writeRef(container, "routeSchedule", it->second,
                         "http://www.iho.int/gml/roles/routeSchedule");
            }
        }
    }
}

void S421Serializer::writeSchedule(pugi::xml_node& parent, const RouteSchedule& sched,
                                    const IdMap& ids) {
    int sid = sched.id;
    auto it = ids.scheduleIds.find(sid);
    std::string schedGmlId = (it != ids.scheduleIds.end()) ? it->second : "RTE.SCHED." + std::to_string(sid);

    pugi::xml_node schedNode = parent.append_child("RouteSchedule");
    schedNode.append_attribute("gml:id") = schedGmlId.c_str();

    // id / name / extensions 走表（表序 = XSD RouteScheduleType xs:sequence，
    // 输出到 Extensions 为止；Collection 与 Manual/Calc/Rec 引用由下方手写）
    fieldspec::writeFields(schedNode, &sched,
                           fieldspec::kRouteScheduleFields, fieldspec::kRouteScheduleFieldsCount,
                           schedGmlId);

    // 反向引用
    writeRef(schedNode, "routeScheduleCollection", ids.schedsContainerId,
             "http://www.iho.int/gml/roles/routeScheduleCollection");

    // 引用 Manual/Calculated/Recommended
    // 2.0 模型：sched.manual/calculated/recommended 为 unique_ptr，内含 elements 向量
    auto manIt = ids.schedManualIds.find(sid);
    if (manIt != ids.schedManualIds.end()
        && sched.manual && !sched.manual->elements.empty()) {
        writeRef(schedNode, "routeScheduleManual", manIt->second,
                 "http://www.iho.int/gml/roles/routeScheduleManual");
    }
    auto calIt = ids.schedCalcIds.find(sid);
    if (calIt != ids.schedCalcIds.end()
        && sched.calculated && !sched.calculated->elements.empty()) {
        writeRef(schedNode, "routeScheduleCalculated", calIt->second,
                 "http://www.iho.int/gml/roles/routeScheduleCalculated");
    }
    auto recIt = ids.schedRecIds.find(sid);
    if (recIt != ids.schedRecIds.end()
        && sched.recommended && !sched.recommended->elements.empty()) {
        writeRef(schedNode, "routeScheduleRecommended", recIt->second,
                 "http://www.iho.int/gml/roles/routeScheduleRecommended");
    }

    // Manual/Calc/Rec 容器和元素在 buildDocument 中通过单独的 member 写入
}

void S421Serializer::writeScheduleElement(pugi::xml_node& parent,
                                           const RouteScheduleElement& elem,
                                           const std::string& elemId,
                                           const std::string& collectionId,
                                           const char* collectionNodeName,
                                           const IdMap& ids) {
    pugi::xml_node elemNode = parent.append_child("RouteScheduleElement");
    elemNode.append_attribute("gml:id") = elemId.c_str();

    // 简单字段 + extensions 走表（表序 = XSD RouteScheduleElementType xs:sequence，
    // 输出到 Note 为止；所属容器回指由下方手写）
    fieldspec::writeFields(elemNode, &elem,
                           fieldspec::kRouteScheduleElementFields, fieldspec::kRouteScheduleElementFieldsCount,
                           elemId);

    // 反向引用到所属容器
    writeRef(elemNode, collectionNodeName, collectionId,
             "http://www.iho.int/gml/roles/routeScheduleElementCollection");
}

void S421Serializer::writePointGeometry(pugi::xml_node& parent, const GM_Point& pos,
                                         const std::string& pointGmlId) {
    pugi::xml_node geom = parent.append_child("geometry");
    pugi::xml_node prop = geom.append_child("S100:pointProperty");
    pugi::xml_node point = prop.append_child("S100:Point");
    point.append_attribute("gml:id") = pointGmlId.c_str();
    point.append_attribute("srsName") = kSrsNameWgs84;
    point.append_attribute("srsDimension") = "2";
    point.append_child("gml:pos").text().set(formatPos(pos).c_str());
}

void S421Serializer::writeCurveGeometry(pugi::xml_node& parent,
                                         const GM_Point& start, const GM_Point& end,
                                         const std::string& curveGmlId) {
    pugi::xml_node geom = parent.append_child("geometry");
    pugi::xml_node prop = geom.append_child("S100:curveProperty");
    pugi::xml_node curve = prop.append_child("S100:Curve");
    curve.append_attribute("gml:id") = curveGmlId.c_str();

    pugi::xml_node segments = curve.append_child("gml:segments");
    pugi::xml_node ls = segments.append_child("gml:LineStringSegment");

    // posList: "lat1 lon1 lat2 lon2"
    std::string posList = formatPos(start) + " " + formatPos(end);
    ls.append_child("gml:posList").text().set(posList.c_str());
}

// 厂商扩展 —— 委托给 fieldspec 共享实现（与解析端 parseExtension 同源）
// 说明：字段写入已全部由 writeFields 表驱动（Extensions 行），本方法保留
// 以维持头文件声明的可用性，仅供表外特殊场景调用。
void S421Serializer::writeExtensions(pugi::xml_node& parent, const char* wrapperName,
                                      const std::vector<RouteExtensions>& extensions,
                                      const std::string& baseId) {
    for (const auto& ext : extensions) {
        fieldspec::writeRouteExtensionsNode(parent, wrapperName, ext,
            baseId.empty() ? std::string("EXT") : baseId);
    }
}

std::string S421Serializer::formatPos(const GM_Point& pos) const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(coordPrecision_)
        << pos.lat << " " << pos.lon;
    return oss.str();
}

// xlinkTitleFor - 由「父元素名 + 引用子元素名」推导 xlink:title 角色名
static const char* xlinkTitleFor(const char* parentName, const char* childName) {
    struct Entry { const char* parent; const char* child; const char* title; };
    static const Entry kTable[] = {
        // ---- Route 顶层引用 ----
        {"Route",                "routeInfo",                          "routeInfoComposition"},
        {"Route",                "routeSchedules",                     "routeSchedulesComposition"},
        {"Route",                "routeWaypoints",                     "routeWaypointsComposition"},
        {"Route",                "routeActionPoints",                  "routeActionPointsComposition"},
        // ---- RouteWaypoints 容器 ----
        {"RouteWaypoints",       "routeWaypointsCollection",           "routeWaypointsComposition"},
        {"RouteWaypoints",       "routeWaypoint",                      "routeWaypointComposition"},
        // ---- RouteWaypoint 自身 ----
        {"RouteWaypoint",        "routeWaypointCollection",            "routeWaypointComposition"},
        {"RouteWaypoint",        "routeWaypointLeg",                   "routeWaypointLegComposition"},
        {"RouteWaypoint",        "targetWaypoint",                     "routeWaypointScheduleElement"},
        // ---- RouteWaypointLeg ----
        {"RouteWaypointLeg",     "routeWaypointLegCollection",         "routeWaypointLegComposition"},
        // ---- RouteSchedules 容器 / RouteSchedule ----
        {"RouteSchedules",       "routeSchedule",                      "routeScheduleComposition"},
        {"RouteSchedule",        "routeScheduleCollection",            "routeScheduleComposition"},
        {"RouteSchedule",        "routeScheduleManual",                "routeScheduleManualComposition"},
        {"RouteSchedule",        "routeScheduleCalculated",            "routeScheduleCalculatedComposition"},
        {"RouteSchedule",        "routeScheduleRecommended",           "routeScheduleRecommendedComposition"},
        // ---- Manual / Calculated / Recommended 容器 ----
        {"RouteScheduleManual",       "routeScheduleCollection",              "routeScheduleManualComposition"},
        {"RouteScheduleManual",       "routeScheduleElementManual",           "routeScheduleElementManualComposition"},
        {"RouteScheduleCalculated",   "routeScheduleCollection",              "routeScheduleCalculatedComposition"},
        {"RouteScheduleCalculated",   "routeScheduleElementCalculated",       "routeScheduleElementCalculatedComposition"},
        {"RouteScheduleRecommended",  "routeScheduleCollection",              "routeScheduleRecommendedComposition"},
        {"RouteScheduleRecommended",  "routeScheduleElementRecommended",      "routeScheduleElementRecommendedComposition"},
        // ---- ScheduleElement 回指各自的集合 ----
        {"RouteScheduleElement", "routeScheduleElementManualCollection",      "routeScheduleElementManualComposition"},
        {"RouteScheduleElement", "routeScheduleElementCalculatedCollection",  "routeScheduleElementCalculatedComposition"},
        {"RouteScheduleElement", "routeScheduleElementRecommendedCollection", "routeScheduleElementRecommendedComposition"},
        // ---- RouteActionPoints 容器 / RouteActionPoint ----
        {"RouteActionPoints",    "routeActionPointsCollection",        "routeActionPointsComposition"},
        {"RouteActionPoints",    "routeActionPoint",                   "routeActionPointComposition"},
        {"RouteActionPoint",     "routeActionPointCollection",         "routeActionPointComposition"},
    };

    for (const auto& e : kTable) {
        if (std::strcmp(e.parent, parentName) == 0 && std::strcmp(e.child, childName) == 0) {
            return e.title;
        }
    }
    return nullptr;   // 未登记的引用组合：不写 xlink:title（宁可缺失也不写错）
}

// writeRef - 写一个引用元素
//   <routeInfo xlink:href="#RTE.INFO" xlink:title="routeInfoComposition"/>
//
// 【xlink:href 的 # 规则】
//   本项目所有 gml:id 都以 'R' 开头（RTE、RTE.INFO、RTE.WPT.1…）。
//   约定：引用这类对象时前面加 '#'（表示"本地引用"，标准写法）。
//   所以这里判断 targetId[0]=='R' 就加 #。else 分支理论上走不到，保留作兜底。
//
// 【为何只写 title、不写 arcrole】
//   官方样例（GMIN/GBASIC/GFULL_20260604）中所有引用一律只写 xlink:title、
//   不写 xlink:arcrole；接收端按 title 识别角色，带 arcrole 的文档会被拒绝。
//   故此处只输出 xlink:title（roleUri 参数已不再使用，保留占位以免改动 23 个调用点）。
void S421Serializer::writeRef(pugi::xml_node& parent, const char* childName,
                               const std::string& targetId, const char* roleUri) {
    (void)roleUri;   // 已不再写出：接收端只认 xlink:title

    pugi::xml_node ref = parent.append_child(childName);

    if (targetId[0] == 'R') {
        // 对象 ID 以 R 开头 → 加 # 表示本地引用
        ref.append_attribute("xlink:href") = ("#" + targetId).c_str();
    } else {
        ref.append_attribute("xlink:href") = targetId.c_str();
    }

    // 角色名：以「父元素 + 子元素」为键查表
    const char* title = xlinkTitleFor(parent.name(), childName);
    if (title != nullptr) {
        ref.append_attribute("xlink:title") = title;
    }
}

// ---- writeOpt 系列：可选字段的输出（Parser 的 getOpt 的镜像）----
// 【模式】只在 value.has_value() 时才写元素；没有值就完全不输出这个标签。
//   这对应 S-421 的"可选字段"语义——缺失字段不该写成空标签 <x></x>。
void S421Serializer::writeOptText(pugi::xml_node& parent, const char* name,
                                   const s421::optional<std::string>& value) {
    if (value.has_value()) {
        parent.append_child(name).text().set(value.value().c_str());
    }
}

void S421Serializer::writeOptDouble(pugi::xml_node& parent, const char* name,
                                     const s421::optional<double>& value) {
    if (value.has_value()) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1) << value.value();
        parent.append_child(name).text().set(oss.str().c_str());
    }
}

void S421Serializer::writeOptInt(pugi::xml_node& parent, const char* name,
                                  const s421::optional<int>& value) {
    if (value.has_value()) {
        parent.append_child(name).text().set(std::to_string(value.value()).c_str());
    }
}

void S421Serializer::writeOptBool(pugi::xml_node& parent, const char* name,
                                   const s421::optional<bool>& value) {
    if (value.has_value()) {
        parent.append_child(name).text().set(value.value() ? "true" : "false");
    }
}

// ============================================================
//  serializeToCompressed - .s421p ZIP 压缩容器
//  规格书 §3.1.2：船端必须支持输出 .s421p（ZIP 内含 1 个 .s421）
// ============================================================
bool S421Serializer::serializeToCompressed(const Dataset& dataset,
                                             const std::string& filePath,
                                             bool compress) {
    lastError_.clear();

    if (!compress) {
        // 不压缩：直接当 XML 写（兼容模式，实际测试数据中有这种）
        return serializeToFile(dataset, filePath);
    }

    // Step 1: 序列化为 XML 字符串
    std::string xml = serializeToString(dataset);
    if (xml.empty()) {
        return false;
    }

    // Step 2: 确定 ZIP 内的文件名（去掉 .s421p 后缀，换成 .s421）
    std::string innerName = filePath;
    // 去掉路径，只留文件名
    auto slash = innerName.find_last_of("/\\");
    if (slash != std::string::npos) innerName = innerName.substr(slash + 1);
    // .s421p → .s421
    if (innerName.size() > 6 && innerName.substr(innerName.size() - 6) == ".s421p") {
        innerName = innerName.substr(0, innerName.size() - 1); // 去掉最后的 'p'
    } else {
        innerName += ".s421"; // 如果没有 .s421p 后缀，加 .s421
    }

    // Step 3: 创建 ZIP
    MiniZip zip;
    zip.addFile(innerName, xml);

    if (!zip.writeTo(filePath)) {
        lastError_ = "ZIP 写入失败: " + zip.getError();
        return false;
    }

    return true;
}

// ============================================================
//  serializeToExchangeSet - S-100 兼容交换集
//  生成目录结构：
//    outputDir/CATALOG.001.XML  — 交换目录
//    outputDir/MD_xxx.xml       — ISO 19115 元数据
//    outputDir/xxx.s421         — 航线数据
//    outputDir/support/         — 辅助目录（空）
// ============================================================
bool S421Serializer::serializeToExchangeSet(const Dataset& dataset,
                                              const std::string& outputDir,
                                              const std::string& producingAgency,
                                              const std::string& agencyContact) {
    lastError_.clear();

    if (!dataset.route) {
        lastError_ = "Dataset 没有 Route 数据";
        return false;
    }

    // Step 1: 创建输出目录（递归，等价 mkdir -p）
    std::string fsErr;
    if (!s421::fs::createDirectories(outputDir, &fsErr)) {
        lastError_ = std::string("创建目录失败: ") + fsErr;
        return false;
    }
    if (!s421::fs::createDirectories(s421::fs::join(outputDir, "support"), &fsErr)) {
        lastError_ = std::string("创建目录失败: ") + fsErr;
        return false;
    }

    // 航线描述（MD 和 CATALOG 共用；2.0 模型：Route::info 为按值成员）
    std::string description = "S-421 Route Plan";
    if (!dataset.route->info.name.empty()) {
        description = dataset.route->info.name;
    }

    // Step 2: 确定文件名
    // .s421 文件名从 route ID 推导
    std::string routeId = dataset.route->id;
    // 替换文件名中不合法的字符
    for (char& c : routeId) {
        if (c == ' ' || c == '/' || c == '\\' || c == ':') c = '_';
    }
    std::string s421FileName = routeId + ".s421";
    std::string s421FilePath = outputDir + "/" + s421FileName;

    // MD 文件名
    std::string mdFileName = "MD_" + routeId + ".s421.xml";
    std::string mdFilePath = outputDir + "/" + mdFileName;

    // Step 3: 写 .s421 文件
    if (!serializeToFile(dataset, s421FilePath)) {
        return false;
    }

    // Step 4: 写 MD 元数据文件（ISO 19115 简化版）
    {
        std::ofstream md(mdFilePath);
        if (!md) {
            lastError_ = "无法创建元数据文件: " + mdFilePath;
            return false;
        }

        // 获取当前日期
        std::time_t now = std::time(nullptr);
        char dateBuf[11];
        std::strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", std::gmtime(&now));
        std::string today(dateBuf);

        // 空间范围
        double westLon = -180.0, eastLon = 180.0;
        double southLat = -90.0, northLat = 90.0;
        if (dataset.route->lowerCorner.has_value()) {
            westLon = dataset.route->lowerCorner.value().lon;
            southLat = dataset.route->lowerCorner.value().lat;
        }
        if (dataset.route->upperCorner.has_value()) {
            eastLon = dataset.route->upperCorner.value().lon;
            northLat = dataset.route->upperCorner.value().lat;
        }

        md << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        md << "<mdb:MD_Metadata\n";
        md << "    xmlns:gco=\"http://standards.iso.org/iso/19115/-3/gco/1.0\"\n";
        md << "    xmlns:cit=\"http://standards.iso.org/iso/19115/-3/cit/2.0\"\n";
        md << "    xmlns:gml=\"http://www.opengis.net/gml/3.2\"\n";
        md << "    xmlns:lan=\"http://standards.iso.org/iso/19115/-3/lan/1.0\"\n";
        md << "    xmlns:mcc=\"http://standards.iso.org/iso/19115/-3/mcc/1.0\"\n";
        md << "    xmlns:mdb=\"http://standards.iso.org/iso/19115/-3/mdb/2.0\"\n";
        md << "    xmlns:mri=\"http://standards.iso.org/iso/19115/-3/mri/1.0\"\n";
        md << "    xmlns:gex=\"http://standards.iso.org/iso/19115/-3/gex/1.0\"\n";
        md << "    xmlns:mco=\"http://standards.iso.org/iso/19115/-3/mco/1.0\"\n";
        md << "    xmlns:mmi=\"http://standards.iso.org/iso/19115/-3/mmi/1.0\"\n";
        md << "    xmlns:mas=\"http://standards.iso.org/iso/19115/-3/mas/1.0\"\n";
        md << "    xmlns:mda=\"http://standards.iso.org/iso/19115/-3/mda/2.0\"\n";
        md << "    xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">\n\n";

        // 标识符
        md << "  <mdb:metadataIdentifier>\n";
        md << "    <mcc:MD_Identifier>\n";
        md << "      <mcc:code><gco:CharacterString>" << routeId
           << "</gco:CharacterString></mcc:code>\n";
        md << "    </mcc:MD_Identifier>\n";
        md << "  </mdb:metadataIdentifier>\n\n";

        // 语言
        md << "  <mdb:defaultLocale>\n";
        md << "    <lan:PT_Locale>\n";
        md << "      <lan:language><lan:LanguageCode codeListValue=\"eng\">English</lan:LanguageCode></lan:language>\n";
        md << "      <lan:characterEncoding><lan:MD_CharacterSetCode codeListValue=\"UTF-8\">UTF-8</lan:MD_CharacterSetCode></lan:characterEncoding>\n";
        md << "    </lan:PT_Locale>\n";
        md << "  </mdb:defaultLocale>\n\n";

        // 联系方式
        md << "  <mdb:contact>\n";
        md << "    <cit:CI_Responsibility>\n";
        md << "      <cit:role><cit:CI_RoleCode codeListValue=\"owner\">owner</cit:CI_RoleCode></cit:role>\n";
        md << "      <cit:party>\n";
        md << "        <cit:CI_Organisation>\n";
        md << "          <cit:name><gco:CharacterString>" << producingAgency
           << "</gco:CharacterString></cit:name>\n";
        if (!agencyContact.empty()) {
            md << "          <cit:contactInfo><cit:CI_Contact>\n";
            md << "            <cit:address><cit:CI_Address>\n";
            md << "              <cit:electronicMailAddress><gco:CharacterString>"
               << agencyContact << "</gco:CharacterString></cit:electronicMailAddress>\n";
            md << "            </cit:CI_Address></cit:contactInfo></cit:CI_Contact>\n";
        }
        md << "        </cit:CI_Organisation>\n";
        md << "      </cit:party>\n";
        md << "    </cit:CI_Responsibility>\n";
        md << "  </mdb:contact>\n\n";

        // 日期
        md << "  <mdb:dateInfo>\n";
        md << "    <cit:CI_Date>\n";
        md << "      <cit:date><gco:DateTime>" << today << "T00:00:00</gco:DateTime></cit:date>\n";
        md << "      <cit:dateType><cit:CI_DateTypeCode codeListValue=\"creation\"/></cit:dateType>\n";
        md << "    </cit:CI_Date>\n";
        md << "  </mdb:dateInfo>\n\n";

        // 数据集标识
        md << "  <mdb:identificationInfo>\n";
        md << "    <mri:MD_DataIdentification>\n";
        md << "      <mri:citation>\n";
        md << "        <cit:CI_Citation>\n";
        md << "          <cit:title><gco:CharacterString>" << description
           << "</gco:CharacterString></cit:title>\n";
        md << "        </cit:CI_Citation>\n";
        md << "      </mri:citation>\n";
        md << "      <mri:abstract><gco:CharacterString>S-421 route plan data</gco:CharacterString></mri:abstract>\n";
        md << "      <mri:topicCategory><mri:MD_TopicCategoryCode>transportation</mri:MD_TopicCategoryCode></mri:topicCategory>\n";

        // 空间范围
        md << "      <mri:extent>\n";
        md << "        <gex:EX_Extent>\n";
        md << "          <gex:geographicElement>\n";
        md << "            <gex:EX_GeographicBoundingBox>\n";
        md << "              <gex:westBoundLongitude><gco:Decimal>" << westLon
           << "</gco:Decimal></gex:westBoundLongitude>\n";
        md << "              <gex:eastBoundLongitude><gco:Decimal>" << eastLon
           << "</gco:Decimal></gex:eastBoundLongitude>\n";
        md << "              <gex:southBoundLatitude><gco:Decimal>" << southLat
           << "</gco:Decimal></gex:southBoundLatitude>\n";
        md << "              <gex:northBoundLatitude><gco:Decimal>" << northLat
           << "</gco:Decimal></gex:northBoundLatitude>\n";
        md << "            </gex:EX_GeographicBoundingBox>\n";
        md << "          </gex:geographicElement>\n";
        md << "        </gex:EX_Extent>\n";
        md << "      </mri:extent>\n";

        md << "      <mri:defaultLocale><lan:PT_Locale>\n";
        md << "        <lan:language><lan:LanguageCode codeListValue=\"eng\">English</lan:LanguageCode></lan:language>\n";
        md << "        <lan:characterEncoding><lan:MD_CharacterSetCode codeListValue=\"UTF-8\"/></lan:characterEncoding>\n";
        md << "      </lan:PT_Locale></mri:defaultLocale>\n";
        md << "    </mri:MD_DataIdentification>\n";
        md << "  </mdb:identificationInfo>\n\n";

        md << "</mdb:MD_Metadata>\n";
    }

    // Step 5: 写 CATALOG.001.XML（交换目录）
    {
        std::string catPath = outputDir + "/CATALOG.001.XML";
        std::ofstream cat(catPath);
        if (!cat) {
            lastError_ = "无法创建交换目录: " + catPath;
            return false;
        }

        std::time_t now = std::time(nullptr);
        char dateBuf[11];
        std::strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", std::gmtime(&now));
        std::string today(dateBuf);

        cat << "<S100XC:S100_ExchangeCatalogue\n";
        cat << "    xmlns:gml=\"http://www.opengis.net/gml/3.2\"\n";
        cat << "    xmlns:xlink=\"http://www.w3.org/1999/xlink\"\n";
        cat << "    xmlns:cit=\"http://standards.iso.org/iso/19115/-3/cit/2.0\"\n";
        cat << "    xmlns:gco=\"http://standards.iso.org/iso/19115/-3/gco/1.0\"\n";
        cat << "    xmlns:lan=\"http://standards.iso.org/iso/19115/-3/lan/1.0\"\n";
        cat << "    xmlns:S100XC=\"http://www.iho.int/s100/xc\"\n";
        cat << "    xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">\n\n";

        // 标识
        cat << "  <S100XC:identifier>\n";
        cat << "    <S100XC:identifier>CATALOG.001.XML</S100XC:identifier>\n";
        cat << "    <S100XC:editionNumber>1.0</S100XC:editionNumber>\n";
        cat << "    <S100XC:date>" << today << "</S100XC:date>\n";
        cat << "  </S100XC:identifier>\n\n";

        // 联系
        cat << "  <S100XC:contact>\n";
        cat << "    <S100XC:organization>" << producingAgency << "</S100XC:organization>\n";
        cat << "  </S100XC:contact>\n\n";

        // 产品规格
        cat << "  <S100XC:productSpecification>\n";
        cat << "    <S100XC:name>S-421</S100XC:name>\n";
        cat << "    <S100XC:version>1.0</S100XC:version>\n";
        cat << "    <S100XC:date>" << today << "</S100XC:date>\n";
        cat << "    <S100XC:number>421</S100XC:number>\n";
        cat << "  </S100XC:productSpecification>\n\n";

        cat << "  <S100XC:metadataLanguage>English</S100XC:metadataLanguage>\n";
        cat << "  <S100XC:exchangeCatalogueName>CATALOG.001.XML</S100XC:exchangeCatalogueName>\n";
        cat << "  <S100XC:exchangeCatalogueDescription>S-421 Route Plan Exchange Set</S100XC:exchangeCatalogueDescription>\n\n";

        // 数据集发现元数据
        cat << "  <S100XC:datasetDiscoveryMetadata>\n";
        cat << "    <S100XC:S100_DatasetDiscoveryMetadata>\n";
        cat << "      <S100XC:fileName>" << s421FileName << "</S100XC:fileName>\n";
        cat << "      <S100XC:filePath></S100XC:filePath>\n";
        cat << "      <S100XC:description>" << description << "</S100XC:description>\n";
        cat << "      <S100XC:editionNumber>1</S100XC:editionNumber>\n";
        cat << "      <S100XC:updateNumber>0</S100XC:updateNumber>\n";
        cat << "      <S100XC:updateApplicationDate>" << today << "</S100XC:updateApplicationDate>\n";
        cat << "      <S100XC:issueDate>" << today << "</S100XC:issueDate>\n";
        cat << "      <S100XC:productSpecification>\n";
        cat << "        <S100XC:name>S-421</S100XC:name>\n";
        cat << "        <S100XC:version>1.0</S100XC:version>\n";
        cat << "        <S100XC:date>" << today << "</S100XC:date>\n";
        cat << "        <S100XC:number>421</S100XC:number>\n";
        cat << "      </S100XC:productSpecification>\n";
        cat << "      <S100XC:producingAgency>\n";
        cat << "        <cit:CI_Responsibility>\n";
        cat << "          <cit:role><cit:CI_RoleCode codeListValue=\"owner\">owner</cit:CI_RoleCode></cit:role>\n";
        cat << "          <cit:party><cit:CI_Organisation>\n";
        cat << "            <cit:name><gco:CharacterString>" << producingAgency
           << "</gco:CharacterString></cit:name>\n";
        cat << "          </cit:CI_Organisation></cit:party>\n";
        cat << "        </cit:CI_Responsibility>\n";
        cat << "      </S100XC:producingAgency>\n";
        cat << "      <S100XC:horizontalDatumReference>EPSG</S100XC:horizontalDatumReference>\n";
        cat << "      <S100XC:horizontalDatumValue>4326</S100XC:horizontalDatumValue>\n";
        cat << "      <S100XC:dataType>GML</S100XC:dataType>\n";
        cat << "      <S100XC:dataTypeVersion>3.2</S100XC:dataTypeVersion>\n";
        cat << "      <S100XC:defaultLocale>\n";
        cat << "        <lan:PT_Locale>\n";
        cat << "          <lan:language><lan:LanguageCode codeListValue=\"eng\">English</lan:LanguageCode></lan:language>\n";
        cat << "          <lan:characterEncoding><lan:MD_CharacterSetCode codeListValue=\"UTF-8\">UTF-8</lan:MD_CharacterSetCode></lan:characterEncoding>\n";
        cat << "        </lan:PT_Locale>\n";
        cat << "      </S100XC:defaultLocale>\n";
        cat << "      <S100XC:metadataFileIdentifier>" << mdFileName << "</S100XC:metadataFileIdentifier>\n";
        cat << "      <S100XC:metadataDateStamp>" << today << "</S100XC:metadataDateStamp>\n";
        cat << "      <S100XC:metadataLanguage>English</S100XC:metadataLanguage>\n";
        cat << "    </S100XC:S100_DatasetDiscoveryMetadata>\n";
        cat << "  </S100XC:datasetDiscoveryMetadata>\n\n";

        cat << "</S100XC:S100_ExchangeCatalogue>\n";
    }

    return true;
}

} // namespace s421
