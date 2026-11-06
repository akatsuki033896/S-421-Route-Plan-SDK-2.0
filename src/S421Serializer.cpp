// S421Serializer.cpp - S-421 数据序列化器实现
//
// 三种输出格式：
//   serializeToFile()        → .s421 XML 文件
//   serializeToCompressed()  → .s421p ZIP 压缩包（ZIP 内含一个 .s421）
//   serializeToExchangeSet() → S-100 兼容交换集（目录结构 + CATALOG + 元数据）

#include "s421/S421Serializer.h"
#include "s421/MiniZip.h"
#include "s421/S421Fs.h"
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

    // 航点 ID
    for (const auto& wp : dataset.route->waypoints) {
        if (wp) {
            ids.waypointIds[wp->id] = "RTE.WPT." + std::to_string(wp->id);
        }
    }

    // 航段 ID（顺序编号，跳过没有 leg 的航点）
    int legSeq = 1;
    for (const auto& wp : dataset.route->waypoints) {
        if (wp && wp->leg) {
            ids.legIds[legSeq] = "RTE.WPT.LEG." + std::to_string(legSeq);
            legSeq++;
        }
    }

    // 动作点 ID
    for (const auto& ap : dataset.route->actionPoints) {
        if (ap) {
            ids.actionPointIds[ap->id] = "RTE.APT." + std::to_string(ap->id);
        }
    }

    // 调度 ID
    for (const auto& sched : dataset.route->schedules) {
        if (sched) {
            int sid = sched->id;
            ids.scheduleIds[sid] = "RTE.SCHED." + std::to_string(sid);
            ids.schedManualIds[sid] = "RTE.SCHED." + std::to_string(sid) + ".MAN";
            ids.schedCalcIds[sid]   = "RTE.SCHED." + std::to_string(sid) + ".CAL";
            ids.schedRecIds[sid]    = "RTE.SCHED." + std::to_string(sid) + ".REC";

            // 调度元素 ID
            int elemSeq = 1;
            for (const auto& e : sched->manualElements) {
                std::string key = "MANUAL." + std::to_string(sid) + "." + std::to_string(elemSeq);
                ids.elementIds[key] = "RTE.SCHED." + std::to_string(sid) + ".MAN.ELEMENT." + std::to_string(elemSeq);
                elemSeq++;
            }
            elemSeq = 1;
            for (const auto& e : sched->calculatedElements) {
                std::string key = "CALC." + std::to_string(sid) + "." + std::to_string(elemSeq);
                ids.elementIds[key] = "RTE.SCHED." + std::to_string(sid) + ".CAL.ELEMENT." + std::to_string(elemSeq);
                elemSeq++;
            }
            elemSeq = 1;
            for (const auto& e : sched->recommendedElements) {
                std::string key = "REC." + std::to_string(sid) + "." + std::to_string(elemSeq);
                ids.elementIds[key] = "RTE.SCHED." + std::to_string(sid) + ".REC.ELEMENT." + std::to_string(elemSeq);
                elemSeq++;
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
            dataset.route->info ? dataset.route->info->name.c_str() : "S-421 Route");
        dii.append_child("S100:datasetReferenceDate").text().set("2026-06-04");
        dii.append_child("S100:datasetLanguage").text().set("ENG");
        dii.append_child("S100:datasetTopicCategory").text().set("transportation");
        dii.append_child("S100:datasetPurpose").text().set("base");
        dii.append_child("S100:updateNumber").text().set("0");
    }

    // 3. 所有业务对象统一放在 <members> 下（2.0 取代旧版 member/imember 分装）
    pugi::xml_node members = root.append_child("members");

    // 3a. RouteInfo
    if (dataset.route->info) {
        writeRouteInfo(members, *dataset.route->info, ids);
    }

    // 3b. Schedules（容器 + Schedule + Manual/Calc/Rec 容器 + Element）
    if (!dataset.route->schedules.empty()) {
        writeSchedulesContainer(members, *dataset.route, ids);

        for (const auto& sched : dataset.route->schedules) {
            if (!sched) continue;
            int sid = sched->id;
            std::string schedGmlId = "RTE.SCHED." + std::to_string(sid);

            // RouteSchedule 自身
            writeSchedule(members, *sched, ids);

            // Manual 容器 + 元素
            if (!sched->manualElements.empty()) {
                std::string manId = ids.schedManualIds.at(sid);
                pugi::xml_node manNode = members.append_child("RouteScheduleManual");
                manNode.append_attribute("gml:id") = manId.c_str();

                writeRef(manNode, "routeScheduleCollection", schedGmlId,
                         "http://www.iho.int/gml/roles/routeScheduleCollection");

                int elemSeq = 1;
                for (const auto& elem : sched->manualElements) {
                    std::string key = "MANUAL." + std::to_string(sid) + "." + std::to_string(elemSeq);
                    std::string elemId = ids.elementIds.at(key);
                    writeRef(manNode, "routeScheduleElement", elemId,
                             "http://www.iho.int/gml/roles/routeScheduleElement");
                    elemSeq++;
                }

                elemSeq = 1;
                for (const auto& elem : sched->manualElements) {
                    std::string key = "MANUAL." + std::to_string(sid) + "." + std::to_string(elemSeq);
                    std::string elemId = ids.elementIds.at(key);
                    writeScheduleElement(members, elem, elemId, manId,
                                         "routeScheduleElementManualCollection", ids);
                    elemSeq++;
                }
            }

            // Calculated 容器 + 元素
            if (!sched->calculatedElements.empty()) {
                std::string calId = ids.schedCalcIds.at(sid);
                pugi::xml_node calNode = members.append_child("RouteScheduleCalculated");
                calNode.append_attribute("gml:id") = calId.c_str();

                writeRef(calNode, "routeScheduleCollection", schedGmlId,
                         "http://www.iho.int/gml/roles/routeScheduleCollection");

                int elemSeq = 1;
                for (const auto& elem : sched->calculatedElements) {
                    std::string key = "CALC." + std::to_string(sid) + "." + std::to_string(elemSeq);
                    std::string elemId = ids.elementIds.at(key);
                    writeRef(calNode, "routeScheduleElement", elemId,
                             "http://www.iho.int/gml/roles/routeScheduleElement");
                    elemSeq++;
                }

                elemSeq = 1;
                for (const auto& elem : sched->calculatedElements) {
                    std::string key = "CALC." + std::to_string(sid) + "." + std::to_string(elemSeq);
                    std::string elemId = ids.elementIds.at(key);
                    writeScheduleElement(members, elem, elemId, calId,
                                         "routeScheduleElementCalculatedCollection", ids);
                    elemSeq++;
                }
            }

            // Recommended 容器 + 元素
            if (!sched->recommendedElements.empty()) {
                std::string recId = ids.schedRecIds.at(sid);
                pugi::xml_node recNode = members.append_child("RouteScheduleRecommended");
                recNode.append_attribute("gml:id") = recId.c_str();

                writeRef(recNode, "routeScheduleCollection", schedGmlId,
                         "http://www.iho.int/gml/roles/routeSchedule");

                int elemSeq = 1;
                for (const auto& elem : sched->recommendedElements) {
                    std::string key = "REC." + std::to_string(sid) + "." + std::to_string(elemSeq);
                    std::string elemId = ids.elementIds.at(key);
                    writeRef(recNode, "routeScheduleElement", elemId,
                             "http://www.iho.int/gml/roles/routeScheduleCollection");
                    elemSeq++;
                }

                elemSeq = 1;
                for (const auto& elem : sched->recommendedElements) {
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
    if (!dataset.route->waypoints.empty()) {
        writeWaypointsContainer(members, *dataset.route, ids);

        int runningLegSeq = 1;
        for (size_t i = 0; i < dataset.route->waypoints.size(); i++) {
            const auto& wp = dataset.route->waypoints[i];
            if (!wp) continue;

            const RouteWaypoint* nextWp = nullptr;
            if (i + 1 < dataset.route->waypoints.size()) {
                nextWp = dataset.route->waypoints[i + 1].get();
            }

            writeWaypoint(members, *wp, nextWp, runningLegSeq, ids);
            if (wp->leg) runningLegSeq++;
        }

        int legSeq = 1;
        for (size_t i = 0; i < dataset.route->waypoints.size(); i++) {
            const auto& wp = dataset.route->waypoints[i];
            if (!wp || !wp->leg) continue;

            const GM_Point startPos = wp->geometry;
            // 终点 = 下一个航点的坐标。末尾航点没有"下一个"时保持空
            s421::optional<GM_Point> endPos;
            if (i + 1 < dataset.route->waypoints.size() && dataset.route->waypoints[i + 1]) {
                endPos = dataset.route->waypoints[i + 1]->geometry;
            }

            writeLeg(members, *wp->leg, startPos, endPos, legSeq, ids);
            legSeq++;
        }
    }

    // 3e. ActionPoints 容器 + 逐个 ActionPoint
    if (!dataset.route->actionPoints.empty()) {
        writeActionPointsContainer(members, *dataset.route, ids);
        for (const auto& ap : dataset.route->actionPoints) {
            if (!ap) continue;
            writeActionPoint(members, *ap, ids);
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

// 航线
void S421Serializer::writeRoute(pugi::xml_node& parent, const Route& route, const IdMap& ids) {
    pugi::xml_node routeNode = parent.append_child("Route");
    routeNode.append_attribute("gml:id") = ids.routeId.c_str();

    // 必填字段
    routeNode.append_child("routeFormatVersion").text().set(route.formatVersion.c_str());
    routeNode.append_child("routeID").text().set(route.id.c_str());
    routeNode.append_child("routeEditionNo").text().set(route.editionNo);

    // 以下顺序严格遵循 XSD RouteType 的 xs:sequence：
    //   FormatVersion → ID → EditionNo → Extensions → routeInfo
    //   → routeSchedules → routeWaypoints → routeActionPoints

    // Route 级别的扩展（XSD 顺序：紧跟 routeEditionNo 之后）
    if (!route.extensions.empty()) {
        int seq = 1;
        for (const auto& ext : route.extensions) {
            std::string extId = ids.routeId + ".EXT." + std::to_string(seq);
            writeExtensions(routeNode, "routeExtensions", {ext}, extId);
            seq++;
        }
    }

    // 引用（指向容器对象）
    writeRef(routeNode, "routeInfo", ids.infoId,
             "http://www.iho.int/gml/roles/routeInfo");

    // routeSchedules 必须写在 routeWaypoints 之前
    if (!route.schedules.empty()) {
        writeRef(routeNode, "routeSchedules", ids.schedsContainerId,
                 "http://www.iho.int/gml/roles/routeSchedules");
    }
    // 注意：必须与 writeWaypointsContainer（buildDocument 中受 waypoints.empty() 保护）的条件保持一致。否则空数据集会产出一条引用 '#RTE.WPTS' 的悬空引用
    if (!route.waypoints.empty()) {
        writeRef(routeNode, "routeWaypoints", ids.wptsContainerId,
                 "http://www.iho.int/gml/roles/routeWaypoints");
    }

    if (!route.actionPoints.empty()) {
        writeRef(routeNode, "routeActionPoints", ids.aptsContainerId,
                 "http://www.iho.int/gml/roles/routeActionPoints");
    }
}

// 航线信息
void S421Serializer::writeRouteInfo(pugi::xml_node& parent, const RouteInfo& info,
                                     const IdMap& ids) {
    pugi::xml_node infoNode = parent.append_child("RouteInfo");
    infoNode.append_attribute("gml:id") = ids.infoId.c_str();

    // 以下顺序严格遵循 XSD RouteInfoType 的 xs:sequence：
    //   Name → Author → EditionTime → Description → Status → MasterApproval
    //   → ValidityStart/End → 港口 → Reference* → typeOfShip → 船舶 → 尺寸 → Extensions

    infoNode.append_child("routeInfoName").text().set(info.name.c_str());

    // 可选字段
    writeOptText(infoNode, "routeInfoAuthor", info.author);
    writeOptText(infoNode, "routeInfoEditionTime", info.editionTime);
    writeOptText(infoNode, "routeInfoDescription", info.description);

    // 2.0：status 输出文本（如 "Planned"）——XSD 顺序在 Description 之后
    infoNode.append_child("routeInfoStatus").text().set(toString(info.status));

    writeOptBool(infoNode, "routeInfoMasterApproval", info.masterApproval);

    writeOptText(infoNode, "routeInfoValidityStart", info.validityStart);
    writeOptText(infoNode, "routeInfoValidityEnd", info.validityEnd);

    // 港口
    writeOptText(infoNode, "routeInfoDeparturePortID1", info.departurePortID1);
    writeOptText(infoNode, "routeInfoDeparturePortID2", info.departurePortID2);
    writeOptText(infoNode, "routeInfoDeparturePortCall", info.departurePortCall);
    writeOptText(infoNode, "routeInfoArrivalPortID1", info.arrivalPortID1);
    writeOptText(infoNode, "routeInfoArrivalPortID2", info.arrivalPortID2);
    writeOptText(infoNode, "routeInfoArrivalPortCall", info.arrivalPortCall);

    // 引用
    writeOptText(infoNode, "routeInfoReferencePrevRoute", info.referencePrevRoute);
    writeOptText(infoNode, "routeInfoReferenceNextRoute", info.referenceNextRoute);

    // 船舶（2.0：typeOfShip 文本枚举，取代旧 routeInfoVesselType）
    if (info.typeOfShip.has_value()) {
        infoNode.append_child("typeOfShip").text().set(toString(*info.typeOfShip));
    }
    writeOptText(infoNode, "routeInfoVesselName", info.vesselName);
    writeOptText(infoNode, "routeInfoVesselMMSI", info.vesselMMSI);
    writeOptText(infoNode, "routeInfoVesselCallsign", info.vesselCallsign);
    writeOptText(infoNode, "routeInfoVesselIMO", info.vesselIMO);
    writeOptText(infoNode, "routeInfoVesselVoyage", info.vesselVoyage);

    // 船舶尺寸
    writeOptDouble(infoNode, "routeInfoVesselHeight", info.vesselHeight);
    writeOptDouble(infoNode, "routeInfoVesselLength", info.vesselLength);
    writeOptDouble(infoNode, "routeInfoVesselBeam", info.vesselBeam);
    writeOptDouble(infoNode, "routeInfoDraftMax", info.draftMax);
    writeOptDouble(infoNode, "routeInfoAirDraftMax", info.airDraftMax);
    writeOptDouble(infoNode, "routeInfoBeamMax", info.beamMax);
    writeOptDouble(infoNode, "routeInfoLengthMax", info.lengthMax);

    // 扩展
    if (!info.extensions.empty()) {
        pugi::xml_node extWrapper = infoNode.append_child("routeInfoExtensions");
        int seq = 1;
        for (const auto& ext : info.extensions) {
            std::string extId = ids.infoId + ".EXT." + std::to_string(seq);
            writeExtensions(extWrapper, "routeExtensions", {ext}, extId);
            seq++;
        }
    }
}

void S421Serializer::writeWaypointsContainer(pugi::xml_node& parent,
                                              const Route& route, const IdMap& ids) {
    pugi::xml_node container = parent.append_child("RouteWaypoints");
    container.append_attribute("gml:id") = ids.wptsContainerId.c_str();

    // 反向引用到 Route
    writeRef(container, "routeWaypointsCollection", ids.routeId,
             "http://www.iho.int/gml/roles/routeWaypointsCollection");

    // 引用每个航点
    for (const auto& wp : route.waypoints) {
        if (!wp) continue;
        auto it = ids.waypointIds.find(wp->id);
        if (it != ids.waypointIds.end()) {
            writeRef(container, "routeWaypoint", it->second,
                     "http://www.iho.int/gml/roles/consistsOfRouteWaypoint");
        }
    }

    // 容器扩展
    if (!route.waypoints.empty() && !route.extensions.empty()) {
        // Waypoints 容器没有单独的扩展列表，跳过
    }
}

void S421Serializer::writeWaypoint(pugi::xml_node& parent, const RouteWaypoint& wp,
                                    const RouteWaypoint* nextWp, int wpIndex,
                                    const IdMap& ids) {
    auto it = ids.waypointIds.find(wp.id);
    std::string wpGmlId = (it != ids.waypointIds.end()) ? it->second : "RTE.WPT." + std::to_string(wp.id);

    pugi::xml_node wpNode = parent.append_child("RouteWaypoint");
    wpNode.append_attribute("gml:id") = wpGmlId.c_str();

    // 以下子元素顺序严格遵循 XSD RouteWaypointType 的 xs:sequence：
    //   ID → Name → ExternalReferenceID → Fixed → TurnRadius → Extensions
    //   → targetWaypoint → Collection → Leg → geometry（geometry 必须最后）

    // 必填字段
    wpNode.append_child("routeWaypointID").text().set(wp.id);

    // 可选字段
    writeOptText(wpNode, "routeWaypointName", wp.name);
    writeOptText(wpNode, "routeWaypointExternalReferenceID", wp.externalRefID);
    writeOptBool(wpNode, "routeWaypointFixed", wp.fixed);

    wpNode.append_child("routeWaypointTurnRadius").text().set(
        std::to_string(wp.turnRadius).c_str());

    // 扩展（XSD 顺序：TurnRadius 之后、targetWaypoint 之前）
    if (!wp.extensions.empty()) {
        pugi::xml_node extWrapper = wpNode.append_child("routeWaypointExtensions");
        int seq = 1;
        for (const auto& ext : wp.extensions) {
            std::string extId = wpGmlId + ".EXT." + std::to_string(seq);
            writeExtensions(extWrapper, "routeExtensions", {ext}, extId);
            seq++;
        }
    }

    // targetWaypoint 引用（2.0 新增：航点 → 调度元素）
    if (wp.targetWaypointRef.has_value()) {
        pugi::xml_node tw = wpNode.append_child("targetWaypoint");
        tw.append_attribute("xlink:href") = wp.targetWaypointRef.value().c_str();
        tw.append_attribute("xlink:title") = "routeWaypointScheduleElement";
    }

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
    // 注意：geometry 是 sequence 的最后一个元素，必须最后写。

    writeOptDouble(legNode, "routeWaypointLegAirDraftMax", leg.airDraftMax);
    writeOptDouble(legNode, "routeWaypointLegBeamMax", leg.beamMax);
    writeOptDouble(legNode, "routeWaypointLegDraft", leg.draft);
    writeOptDouble(legNode, "routeWaypointLegDraftAft", leg.draftAft);
    writeOptDouble(legNode, "routeWaypointLegDraftForward", leg.draftForward);
    writeOptDouble(legNode, "routeWaypointLegDraftMax", leg.draftMax);
    writeOptDouble(legNode, "routeWaypointLegDynamicUKC", leg.dynamicUKC);

    // 必填：几何类型（2.0：文本 loxodrome/orthodrome）
    legNode.append_child("routeWaypointLegGeometryType").text().set(toString(leg.geometryType));

    writeOptText(legNode, "routeWaypointLegIssue", leg.issue);
    writeOptDouble(legNode, "routeWaypointLegLengthMax", leg.lengthMax);
    writeOptText(legNode, "routeWaypointLegNote", leg.note);
    writeOptInt(legNode, "routeWaypointLegPortCL", leg.portCL);
    writeOptInt(legNode, "routeWaypointLegPortXTDL", leg.portXTDL);
    writeOptDouble(legNode, "routeWaypointLegSafetyContour", leg.safetyContour);
    writeOptDouble(legNode, "routeWaypointLegSafetyDepth", leg.safetyDepth);
    writeOptDouble(legNode, "routeWaypointLegSafetyMargin", leg.safetyMargin);
    writeOptDouble(legNode, "routeWaypointLegSOGMax", leg.sogMax);
    writeOptDouble(legNode, "routeWaypointLegSOGMin", leg.sogMin);
    writeOptInt(legNode, "routeWaypointLegStarboardCL", leg.starboardCL);
    writeOptInt(legNode, "routeWaypointLegStarboardXTDL", leg.starboardXTDL);
    writeOptDouble(legNode, "routeWaypointLegStaticUKC", leg.staticUKC);
    writeOptDouble(legNode, "routeWaypointLegSTWMax", leg.stwMax);
    writeOptDouble(legNode, "routeWaypointLegSTWMin", leg.stwMin);

    // 扩展（XSD 顺序：extensions 在 collection 之前）
    if (!leg.extensions.empty()) {
        pugi::xml_node extWrapper = legNode.append_child("routeWaypointLegExtensions");
        int seq = 1;
        for (const auto& ext : leg.extensions) {
            std::string extId = legGmlId + ".EXT." + std::to_string(seq);
            writeExtensions(extWrapper, "routeExtensions", {ext}, extId);
            seq++;
        }
    }

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

    // 引用每个动作点
    for (const auto& ap : route.actionPoints) {
        if (!ap) continue;
        auto it = ids.actionPointIds.find(ap->id);
        if (it != ids.actionPointIds.end()) {
            writeRef(container, "routeActionPoint", it->second,
                     "http://www.iho.int/gml/roles/routeActionPoint");
        }
    }
}

void S421Serializer::writeActionPoint(pugi::xml_node& parent,
                                       const RouteActionPoint& ap, const IdMap& ids) {
    auto it = ids.actionPointIds.find(ap.id);
    std::string apGmlId = (it != ids.actionPointIds.end()) ? it->second : "RTE.APT." + std::to_string(ap.id);

    pugi::xml_node apNode = parent.append_child("RouteActionPoint");
    apNode.append_attribute("gml:id") = apGmlId.c_str();

    // 以下顺序严格遵循 XSD RouteActionPointType 的 xs:sequence：
    //   ID → Name → ExternalReference → Radius → TimetoAct
    //   → RequiredAction → RequiredActionDescription → Extensions
    //   → Collection → geometry（geometry 必须最后）

    // 必填
    apNode.append_child("routeActionPointID").text().set(ap.id);

    // 可选
    writeOptText(apNode, "routeActionPointName", ap.name);
    writeOptText(apNode, "routeActionPointExternalReference", ap.externalRef);
    writeOptDouble(apNode, "routeActionPointRadius", ap.radius);
    // 注意：schema 中该元素名是小写 t 的 TimetoAct
    writeOptDouble(apNode, "routeActionPointTimetoAct", ap.timeToAct);

    // 2.0：requiredAction 输出文本（如 "Report"）
    apNode.append_child("routeActionPointRequiredAction").text().set(toString(ap.requiredAction));
    apNode.append_child("routeActionPointRequiredActionDescription").text().set(
        ap.actionDescription.c_str());

    // 扩展
    if (!ap.extensions.empty()) {
        pugi::xml_node extWrapper = apNode.append_child("routeActionPointExtensions");
        int seq = 1;
        for (const auto& ext : ap.extensions) {
            std::string extId = apGmlId + ".EXT." + std::to_string(seq);
            writeExtensions(extWrapper, "routeExtensions", {ext}, extId);
            seq++;
        }
    }

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

    // 引用每个调度
    for (const auto& sched : route.schedules) {
        if (!sched) continue;
        auto it = ids.scheduleIds.find(sched->id);
        if (it != ids.scheduleIds.end()) {
            writeRef(container, "routeSchedule", it->second,
                     "http://www.iho.int/gml/roles/routeSchedule");
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

    schedNode.append_child("routeScheduleID").text().set(sid);
    writeOptText(schedNode, "routeScheduleName", sched.name);

    // 反向引用
    writeRef(schedNode, "routeScheduleCollection", ids.schedsContainerId,
             "http://www.iho.int/gml/roles/routeScheduleCollection");

    // 引用 Manual/Calculated/Recommended
    auto manIt = ids.schedManualIds.find(sid);
    if (manIt != ids.schedManualIds.end() && !sched.manualElements.empty()) {
        writeRef(schedNode, "routeScheduleManual", manIt->second,
                 "http://www.iho.int/gml/roles/routeScheduleManual");
    }
    auto calIt = ids.schedCalcIds.find(sid);
    if (calIt != ids.schedCalcIds.end() && !sched.calculatedElements.empty()) {
        writeRef(schedNode, "routeScheduleCalculated", calIt->second,
                 "http://www.iho.int/gml/roles/routeScheduleCalculated");
    }
    auto recIt = ids.schedRecIds.find(sid);
    if (recIt != ids.schedRecIds.end() && !sched.recommendedElements.empty()) {
        writeRef(schedNode, "routeScheduleRecommended", recIt->second,
                 "http://www.iho.int/gml/roles/routeScheduleRecommended");
    }

    // 写入 Manual 容器 + 元素
    if (!sched.manualElements.empty()) {
        std::string manId = ids.schedManualIds.at(sid);
    }

    // Manual/Calc/Rec 容器和元素在 buildDocument 中通过单独的 imember 写入
}

void S421Serializer::writeScheduleElement(pugi::xml_node& parent,
                                           const RouteScheduleElement& elem,
                                           const std::string& elemId,
                                           const std::string& collectionId,
                                           const char* collectionNodeName,
                                           const IdMap& ids) {
    pugi::xml_node elemNode = parent.append_child("RouteScheduleElement");
    elemNode.append_attribute("gml:id") = elemId.c_str();

    elemNode.append_child("routeWaypointID").text().set(elem.waypointId);

    writeOptDouble(elemNode, "routeScheduleElementPlanSOG", elem.planSOG);
    writeOptText(elemNode, "routeScheduleElementETD", elem.etd);
    writeOptText(elemNode, "routeScheduleElementETA", elem.eta);
    writeOptInt(elemNode, "routeScheduleElementETDWindowBefore", elem.etdWindowBefore);
    writeOptInt(elemNode, "routeScheduleElementETDWindowAfter", elem.etdWindowAfter);
    writeOptInt(elemNode, "routeScheduleElementETAWindowBefore", elem.etaWindowBefore);
    writeOptInt(elemNode, "routeScheduleElementETAWindowAfter", elem.etaWindowAfter);
    writeOptText(elemNode, "routeScheduleElementNote", elem.note);

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

// 厂商扩展
void S421Serializer::writeExtensions(pugi::xml_node& parent, const char* wrapperName,
                                      const std::vector<RouteExtension>& extensions,
                                      const std::string& baseId) {
    int seq = 1;
    for (const auto& ext : extensions) {
        std::string extId = baseId.empty()
            ? std::string("EXT.") + std::to_string(seq)
            : baseId;

        pugi::xml_node extNode = parent.append_child(wrapperName);
        extNode.append_attribute("gml:id") = extId.c_str();
        extNode.append_attribute("routeExtensionsManufacturerId") = ext.manufacturerId.c_str();

        if (!ext.name.empty()) {
            extNode.append_attribute("routeExtensionsName") = ext.name.c_str();
        }
        if (!ext.version.empty()) {
            extNode.append_attribute("routeExtensionsVersion") = ext.version.c_str();
        }

        // Note（可以有多个，用换行分割后再合并简化处理）
        if (!ext.note.empty()) {
            extNode.append_child("routeExtensionsNote").text().set(ext.note.c_str());
        }
        seq++;
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
        {"Route",                "routeInfo",                          "routeInfoComGM_Point"},
        {"Route",                "routeSchedules",                     "routeSchedulesComGM_Point"},
        {"Route",                "routeWaypoints",                     "routeWaypointsComGM_Point"},
        {"Route",                "routeActionPoints",                  "routeActionPointsComGM_Point"},
        // ---- RouteWaypoints 容器 ----
        {"RouteWaypoints",       "routeWaypointsCollection",           "routeWaypointsComGM_Point"},
        {"RouteWaypoints",       "routeWaypoint",                      "routeWaypointComGM_Point"},
        // ---- RouteWaypoint 自身 ----
        {"RouteWaypoint",        "routeWaypointCollection",            "routeWaypointComGM_Point"},
        {"RouteWaypoint",        "routeWaypointLeg",                   "routeWaypointLegComGM_Point"},
        {"RouteWaypoint",        "targetWaypoint",                     "routeWaypointScheduleElement"},
        // ---- RouteWaypointLeg ----
        {"RouteWaypointLeg",     "routeWaypointLegCollection",         "routeWaypointLegComGM_Point"},
        // ---- RouteSchedules 容器 / RouteSchedule ----
        {"RouteSchedules",       "routeSchedule",                      "routeScheduleComGM_Point"},
        {"RouteSchedule",        "routeScheduleCollection",            "routeScheduleComGM_Point"},
        {"RouteSchedule",        "routeScheduleManual",                "routeScheduleManualComGM_Point"},
        {"RouteSchedule",        "routeScheduleCalculated",            "routeScheduleCalculatedComGM_Point"},
        {"RouteSchedule",        "routeScheduleRecommended",           "routeScheduleRecommendedComGM_Point"},
        // ---- Manual / Calculated / Recommended 容器 ----
        {"RouteScheduleManual",       "routeScheduleCollection",              "routeScheduleManualComGM_Point"},
        {"RouteScheduleManual",       "routeScheduleElement",                 "routeScheduleElementManualComGM_Point"},
        {"RouteScheduleCalculated",   "routeScheduleCollection",              "routeScheduleCalculatedComGM_Point"},
        {"RouteScheduleCalculated",   "routeScheduleElement",                 "routeScheduleElementCalculatedComGM_Point"},
        {"RouteScheduleRecommended",  "routeScheduleCollection",              "routeScheduleRecommendedComGM_Point"},
        {"RouteScheduleRecommended",  "routeScheduleElement",                 "routeScheduleElementRecommendedComGM_Point"},
        // ---- ScheduleElement 回指各自的集合 ----
        {"RouteScheduleElement", "routeScheduleElementManualCollection",      "routeScheduleElementManualComGM_Point"},
        {"RouteScheduleElement", "routeScheduleElementCalculatedCollection",  "routeScheduleElementCalculatedComGM_Point"},
        {"RouteScheduleElement", "routeScheduleElementRecommendedCollection", "routeScheduleElementRecommendedComGM_Point"},
        // ---- RouteActionPoints 容器 / RouteActionPoint ----
        {"RouteActionPoints",    "routeActionPointsCollection",        "routeActionPointsComGM_Point"},
        {"RouteActionPoints",    "routeActionPoint",                   "routeActionPointComGM_Point"},
        {"RouteActionPoint",     "routeActionPointCollection",         "routeActionPointComGM_Point"},
    };

    for (const auto& e : kTable) {
        if (std::strcmp(e.parent, parentName) == 0 && std::strcmp(e.child, childName) == 0) {
            return e.title;
        }
    }
    return nullptr;   // 未登记的引用组合：不写 xlink:title（宁可缺失也不写错）
}

// writeRef - 写一个引用元素
//   <routeInfo xlink:href="#RTE.INFO" xlink:title="routeInfoComGM_Point"/>
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

    // 航线描述（MD 和 CATALOG 共用）
    std::string description = "S-421 Route Plan";
    if (dataset.route->info) {
        description = dataset.route->info->name;
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
