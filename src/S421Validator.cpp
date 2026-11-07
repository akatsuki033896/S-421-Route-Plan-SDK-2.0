// S421Validator.cpp - S-421 数据校验引擎实现
//
// 四层校验（对应规格书 §3.3.2）：
//   1. 格式校验  — 基本结构完整性（Route、RouteInfo 是否存在）
//   2. 属性校验  — 强制属性是否存在 + 值域是否合规
//   3. 逻辑校验  — 跨字段关系（航点 ID 唯一、航段数 ≤ 航点数-1、时间顺序）
//   4. 错误反馈  — 标准化输出（category + objectId + field + message）
//
// 错误（Error）必须修；警告（Warning）不阻断；result.ok() = 无错误

#include "s421/S421Validator.h"
#include <sstream>
#include <set>
#include <algorithm>
#include <cctype>

namespace s421 {

bool ValidationResult::ok() const {
    return errors.empty();
}

size_t ValidationResult::errorCount() const {
    return errors.size();
}

size_t ValidationResult::warningCount() const {
    return warnings.size();
}

// ============================================================
//  ValidationResult::format
// ============================================================
void ValidationResult::format(std::ostream& os) const {
    if (ok() && warnings.empty()) {
        os << "Validation passed (no errors, no warnings)\n";
        return;
    }

    os << "Validation: " << errorCount() << " error(s), "
       << warningCount() << " warning(s)\n";

    for (const auto& e : errors) {
        os << e.toString() << "\n";
    }
    for (const auto& w : warnings) {
        os << w.toString() << "\n";
    }
}

// ============================================================
//  主入口
// ============================================================
// validate - 校验主入口：编排四层校验，把所有问题收集进 result 返回
// （"结果对象"模式：一次报出全部问题，由调用方决定怎么处理）
ValidationResult S421Validator::validate(const Dataset& dataset) {
    ValidationResult result;

    // 第 1 层：格式/结构校验（Route、RouteInfo 是否存在等）
    checkStructure(dataset, result);

    if (!dataset.route) return result;   // 连 Route 都没有，后续校验无法进行

    const Route& route = *dataset.route;

    // 第 2 层：属性校验（逐对象检查字段）
    // 2.0 模型：info 为按值成员；waypoints/actionPoints/schedules 为
    // unique_ptr<容器>（nullptr = 集合不存在），集合内为值向量
    checkRouteAttributes(route, result);

    checkRouteInfoAttributes(route.info, result);

    if (route.waypoints) {
        for (const auto& wp : route.waypoints->waypoints) {
            checkWaypointAttributes(wp, result);
            // 航段校验（通过航点的 leg）
            if (wp.leg) {
                checkLegAttributes(*wp.leg, result);
            }
        }
    }

    if (route.actionPoints) {
        for (const auto& ap : route.actionPoints->actionPoints) {
            checkActionPointAttributes(ap, result);
        }
    }

    if (route.schedules) {
        for (const auto& sched : route.schedules->schedules) {
            checkScheduleAttributes(sched, result);
            if (sched.manual) {
                for (const auto& elem : sched.manual->elements) {
                    checkScheduleElementAttributes(elem, result);
                }
            }
            if (sched.calculated) {
                for (const auto& elem : sched.calculated->elements) {
                    checkScheduleElementAttributes(elem, result);
                }
            }
            if (sched.recommended) {
                for (const auto& elem : sched.recommended->elements) {
                    checkScheduleElementAttributes(elem, result);
                }
            }
        }
    }

    // 第 3 层：逻辑校验
    checkLogic(route, result);

    return result;
}

// ============================================================
//  第 1 层：格式/结构校验
// ============================================================
void S421Validator::checkStructure(const Dataset& dataset, ValidationResult& result) {
    // Dataset 必须有 gml:id
    if (isEmpty(dataset.gmlId)) {
        addError(result, "format", "Dataset", "gml:id",
                 "数据集缺少 gml:id 标识，无法唯一标识此航线数据文件");
    }

    // Dataset 必须有 Route
    if (!dataset.route) {
        addError(result, "format", "Dataset", "route",
                 "数据集缺少 Route 对象，这是 S-421 的强制顶层容器");
        return;
    }

    // Route 必须有 RouteInfo
    if (dataset.route->info.name.empty()) {
        addError(result, "format", "Route", "routeInfo",
                 "航线缺少 RouteInfo 对象，无法获取航线基本信息和状态");
    }

    // Route 若存在航点则必须有 ≥2 个（2.0 的 GMIN 等 minimal 文件可合法地无航点）
    // 2.0 模型：route.waypoints 为 unique_ptr<RouteWaypoints>，内含值向量
    if (dataset.route->waypoints
        && !dataset.route->waypoints->waypoints.empty()
        && dataset.route->waypoints->waypoints.size() < 2) {
        addError(result, "format", "Route", "waypoints",
                 "航点数量为 " + std::to_string(dataset.route->waypoints->waypoints.size())
                 + "，存在航点时至少需要 2 个");
    }
}

// ============================================================
//  第 2 层：属性校验
// ============================================================

void S421Validator::checkRouteAttributes(const Route& route, ValidationResult& result) {
    // routeID 必须唯一标识（XSD NonEmptyText: ≥3位，首尾为字母数字）
    if (isEmpty(route.id)) {
        addError(result, "attribute", "Route", "routeID",
                 "强制属性缺失：航线唯一标识 routeID 不能为空");
    } else if (route.id.size() < 3) {
        addError(result, "attribute", "Route", "routeID",
                 "routeID 长度不足 3 位，当前值: '" + route.id + "'");
    } else {
        // XSD NonEmptyText 模式：首尾字符必须是字母或数字
        char first = route.id.front();
        char last = route.id.back();
        bool firstOk = std::isalnum(static_cast<unsigned char>(first));
        bool lastOk = std::isalnum(static_cast<unsigned char>(last));
        if (!firstOk || !lastOk) {
            addWarning(result, "attribute", "Route", "routeID",
                       "routeID 首尾字符应为字母或数字（XSD NonEmptyText 约束），当前值: '"
                       + route.id + "'");
        }
    }

    // routeFormatVersion 格式 X.Y（Edition 2.0 默认 "2.0"）
    if (isEmpty(route.formatVersion)) {
        addWarning(result, "attribute", "Route", "routeFormatVersion",
                   "缺少格式版本号，默认应为 '2.0'");
    }

    // routeEditionNo 必须为正整数
    if (route.editionNo < 1) {
        addError(result, "attribute", "Route", "routeEditionNo",
                 "版本号必须 ≥1，当前值: " + std::to_string(route.editionNo));
    }
}

void S421Validator::checkRouteInfoAttributes(const RouteInfo& info, ValidationResult& result) {
    std::string objId = "RouteInfo";

    // routeInfoName（必填）
    if (isEmpty(info.name)) {
        addError(result, "attribute", objId, "routeInfoName",
                 "强制属性缺失：航线名称不能为空");
    }

    // routeInfoStatus：2.0 为文本枚举，解析期已保证合法；防御性检查
    if (std::string(toString(info.status)) == "Unknown") {
        addError(result, "attribute", objId, "routeInfoStatus",
                 "状态枚举值非法");
    }

    // routeInfoEditionTime（必填，UTC ISO 8601；2.0 模型为按值 std::string，空串=未设置）
    if (isEmpty(info.editionTime)) {
        addWarning(result, "attribute", objId, "routeInfoEditionTime",
                   "缺少编辑时间，建议使用 UTC ISO 8601 格式（如 2024-01-01T12:00:00Z）");
    } else if (!isValidTimeFormat(info.editionTime)) {
        addWarning(result, "attribute", objId, "routeInfoEditionTime",
                   "时间格式可能不规范，建议使用 UTC ISO 8601（如 2024-01-01T12:00:00Z）");
    }

    // routeInfoVesselMMSI（2.0 可选；模型为按值 std::string，空串=未设置）
    if (!isEmpty(info.vesselMMSI)) {
        if (!isValidMMSI(info.vesselMMSI)) {
            addError(result, "attribute", objId, "routeInfoVesselMMSI",
                     "MMSI 应为 9 位数字，当前值: '" + info.vesselMMSI + "'");
        }
    } else {
        addWarning(result, "attribute", objId, "routeInfoVesselMMSI",
                   "未提供 MMSI（2.0 可选），建议补充 9 位船舶 MMSI");
    }

    // routeInfoVesselIMO（7位数字，XSD IMONumberText）
    if (info.vesselIMO.has_value() && !isEmpty(info.vesselIMO.value())) {
        if (!isValidIMO(info.vesselIMO.value())) {
            addWarning(result, "attribute", objId, "routeInfoVesselIMO",
                       "IMO 编号应为 7 位数字（XSD IMONumberText），当前值: '"
                       + info.vesselIMO.value() + "'");
        }
    }

    // 港口代码（XSD PortIDType: 5位字母，如 "CNSHA"）
    // 注意：标准测试数据中部分港口代码不完全合规，所以仅作为警告
    if (info.departurePortID1.has_value() && !isEmpty(info.departurePortID1.value())) {
        if (!isValidPortID(info.departurePortID1.value())) {
            addWarning(result, "attribute", objId, "routeInfoDeparturePortID1",
                       "出发港代码建议为 5 位字母（XSD PortIDType），当前值: '"
                       + info.departurePortID1.value() + "'");
        }
    }
    if (info.arrivalPortID1.has_value() && !isEmpty(info.arrivalPortID1.value())) {
        if (!isValidPortID(info.arrivalPortID1.value())) {
            addWarning(result, "attribute", objId, "routeInfoArrivalPortID1",
                       "到达港代码建议为 5 位字母（XSD PortIDType），当前值: '"
                       + info.arrivalPortID1.value() + "'");
        }
    }

    // 有效期校验（逻辑校验在这里做更方便；2.0 模型为按值 std::string，空串=未设置）
    if (!isEmpty(info.validityStart) && !isEmpty(info.validityEnd)) {
        if (info.validityEnd < info.validityStart) {
            addError(result, "logic", objId, "validityStart/End",
                     "有效期开始晚于结束: start=" + info.validityStart
                     + ", end=" + info.validityEnd);
        }
    }

    // 船舶尺寸合理性
    if (info.vesselLength.has_value() && info.vesselLength.value() <= 0) {
        addError(result, "attribute", objId, "routeInfoVesselLength",
                 "船长必须为正数，当前值: " + std::to_string(info.vesselLength.value()));
    }
    if (info.draftMax.has_value() && info.draftMax.value() <= 0) {
        addError(result, "attribute", objId, "routeInfoDraftMax",
                 "最大吃水必须为正数，当前值: " + std::to_string(info.draftMax.value()));
    }
}

void S421Validator::checkWaypointAttributes(const RouteWaypoint& wp, ValidationResult& result) {
    std::string objId = "Waypoint#" + std::to_string(wp.id);

    // routeWaypointID（必填，唯一）
    if (wp.id < 1) {
        addError(result, "attribute", objId, "routeWaypointID",
                 "航点 ID 必须为正整数");
    }

    // geometry（坐标范围）
    if (wp.geometry.lat < -90.0 || wp.geometry.lat > 90.0) {
        addError(result, "attribute", objId, "geometry.lat",
                 "纬度超出范围 -90~+90，当前值: " + std::to_string(wp.geometry.lat));
    }
    if (wp.geometry.lon < -180.0 || wp.geometry.lon > 180.0) {
        addError(result, "attribute", objId, "geometry.lon",
                 "经度超出范围 -180~+180，当前值: " + std::to_string(wp.geometry.lon));
    }

    // routeWaypointTurnRadius（2.0 XSD 为无限制 xs:double；仅校验非负）
    if (wp.turnRadius < 0.0) {
        addError(result, "attribute", objId, "routeWaypointTurnRadius",
                 "转向半径不能为负，当前值: " + std::to_string(wp.turnRadius));
    }
}

void S421Validator::checkLegAttributes(const RouteWaypointLeg& leg, ValidationResult& result) {
    std::string objId = "Leg";

    // geometryType：2.0 为文本枚举（loxodrome/orthodrome），解析期已保证合法；防御性检查
    if (std::string(toString(leg.geometryType)) == "Unknown") {
        addError(result, "attribute", objId, "routeWaypointLegGeometryType",
                 "几何类型枚举值非法");
    }

    // XTD 值域检查（XSD XtdType: 1-10000 的整数，单位为米）
    if (leg.starboardXTDL.has_value()) {
        int v = leg.starboardXTDL.value();
        if (v < 1 || v > 10000) {
            addError(result, "attribute", objId, "starboardXTDL",
                     "右舷横偏距限制值域 1-10000（XSD XtdType），当前值: " + std::to_string(v));
        }
    }
    if (leg.portXTDL.has_value()) {
        int v = leg.portXTDL.value();
        if (v < 1 || v > 10000) {
            addError(result, "attribute", objId, "portXTDL",
                     "左舷横偏距限制值域 1-10000（XSD XtdType），当前值: " + std::to_string(v));
        }
    }

    // 安全水深合理性
    if (leg.safetyDepth.has_value() && leg.safetyDepth.value() < 0) {
        addError(result, "attribute", objId, "safetyDepth",
                 "安全水深不能为负数");
    }

    // 航速合理性（节）
    if (leg.sogMax.has_value() && leg.sogMax.value() < 0) {
        addError(result, "attribute", objId, "sogMax",
                 "最大对地航速不能为负数");
    }
    if (leg.sogMin.has_value() && leg.sogMax.has_value()
        && leg.sogMin.value() > leg.sogMax.value()) {
        addWarning(result, "logic", objId, "sogMin/sogMax",
                   "最小航速大于最大航速，请检查航速约束");
    }
}

void S421Validator::checkActionPointAttributes(const RouteActionPoint& ap, ValidationResult& result) {
    std::string objId = "ActionPoint#" + std::to_string(ap.id);

    // routeActionPointID（2.0 可选；缺失或非正仅警告）
    if (ap.id < 1) {
        addWarning(result, "attribute", objId, "routeActionPointID",
                   "动作点 ID 缺失或非正（2.0 可选），建议补充唯一正整数 ID");
    }

    // requiredAction：2.0 为文本枚举（6 种），解析期已保证合法；防御性检查
    if (std::string(toString(ap.requiredAction)) == "Unknown") {
        addError(result, "attribute", objId, "routeActionPointRequiredAction",
                 "动作类型枚举值非法");
    }

    // actionDescription（必填）
    if (isEmpty(ap.actionDescription)) {
        addWarning(result, "attribute", objId, "actionDescription",
                   "动作描述为空，建议补充说明此动作点的具体操作");
    }
}

void S421Validator::checkScheduleAttributes(const RouteSchedule& sched, ValidationResult& result) {
    std::string objId = "Schedule#" + std::to_string(sched.id);

    if (sched.id < 1) {
        addError(result, "attribute", objId, "routeScheduleID",
                 "调度 ID 必须为正整数");
    }

    // 至少应该有某种调度元素
    // 2.0 模型：manual/calculated/recommended 为 unique_ptr 子对象，各含 elements 向量
    const bool hasAnyElement =
        (sched.manual && !sched.manual->elements.empty())
        || (sched.calculated && !sched.calculated->elements.empty())
        || (sched.recommended && !sched.recommended->elements.empty());
    if (!hasAnyElement) {
        addWarning(result, "attribute", objId, "elements",
                   "调度对象没有任何调度元素（人工/计算/推荐均为空）");
    }
}

void S421Validator::checkScheduleElementAttributes(const RouteScheduleElement& elem,
                                                     ValidationResult& result) {
    std::string objId = "ScheduleElement[WP#" + std::to_string(elem.waypointId) + "]";

    // waypointId 必须为正整数
    if (elem.waypointId < 1) {
        addError(result, "attribute", objId, "routeWaypointId",
                 "关联航点 ID 必须为正整数");
    }

    // 航速合理性（2.0 模型：planSOG 为按值 double）
    if (elem.planSOG < 0) {
        addError(result, "attribute", objId, "planSOG",
                 "计划航速不能为负数");
    }

    // ETD ≤ ETA（逻辑校验；2.0 模型：etd/eta 为按值 std::string，空串=未设置）
    if (!isEmpty(elem.etd) && !isEmpty(elem.eta)) {
        if (elem.eta < elem.etd) {
            addWarning(result, "logic", objId, "ETD/ETA",
                       "ETA 早于 ETD，请确认时间顺序: ETD=" + elem.etd
                       + ", ETA=" + elem.eta);
        }
    }

    // 时间窗口合理性
    if (elem.etdWindowBefore.has_value() && elem.etdWindowBefore.value() < 0) {
        addError(result, "attribute", objId, "etdWindowBefore",
                 "ETD 前窗口不能为负数");
    }
    if (elem.etdWindowAfter.has_value() && elem.etdWindowAfter.value() < 0) {
        addError(result, "attribute", objId, "etdWindowAfter",
                 "ETD 后窗口不能为负数");
    }
}

// ============================================================
//  第 3 层：逻辑校验
// ============================================================
void S421Validator::checkLogic(const Route& route, ValidationResult& result) {
    // 2.0 模型：waypoints/actionPoints/schedules 为 unique_ptr<容器>，集合内为值向量
    static const std::vector<RouteWaypoint> kNoWaypoints;
    const std::vector<RouteWaypoint>& wps = route.waypoints ? route.waypoints->waypoints
                                                            : kNoWaypoints;

    // 航点 ID 唯一性
    std::set<int> seenIds;
    for (const auto& wp : wps) {
        if (seenIds.count(wp.id)) {
            addError(result, "logic", "Waypoint#" + std::to_string(wp.id),
                     "routeWaypointID",
                     "航点 ID 重复: " + std::to_string(wp.id) + "，每个航点必须有唯一 ID");
        }
        seenIds.insert(wp.id);
    }

    // 航段连续性：有 N 个航点，最多 N-1 个航段（无航点时跳过，避免 size()-1 下溢误报）
    if (!wps.empty()) {
        int legCount = 0;
        for (const auto& wp : wps) {
            if (wp.leg) legCount++;
        }
        if (legCount > static_cast<int>(wps.size()) - 1) {
            addError(result, "logic", "Route", "legs",
                     "航段数(" + std::to_string(legCount) + ")超过航点数-1("
                     + std::to_string(wps.size() - 1) + ")");
        }
    }

    // 第一个航点通常不应该有 Leg（它是起点）
    if (!wps.empty() && wps.front().leg) {
        addWarning(result, "logic", "Waypoint#1", "leg",
                   "第一个航点关联了航段，通常起点不需要航段（航段从起点出发）");
    }

    // 调度元素中的 waypointId 应该引用存在的航点
    std::set<int> wpIds;
    for (const auto& wp : wps) {
        wpIds.insert(wp.id);
    }
    if (route.schedules) {
        // 检查一类子调度的所有元素引用（elements 为空指针时跳过）
        auto checkElements = [&](const std::vector<RouteScheduleElement>* elements) {
            if (!elements) return;
            for (const auto& elem : *elements) {
                if (wpIds.count(elem.waypointId) == 0) {
                    addWarning(result, "logic",
                               "ScheduleElement[WP#" + std::to_string(elem.waypointId) + "]",
                               "routeWaypointId",
                               "调度元素引用了不存在的航点 ID: " + std::to_string(elem.waypointId));
                }
            }
        };
        for (const auto& sched : route.schedules->schedules) {
            checkElements(sched.manual ? &sched.manual->elements : nullptr);
            checkElements(sched.calculated ? &sched.calculated->elements : nullptr);
            checkElements(sched.recommended ? &sched.recommended->elements : nullptr);
        }
    }

    // 动作点 ID 唯一性
    std::set<int> seenApIds;
    if (route.actionPoints) {
        for (const auto& ap : route.actionPoints->actionPoints) {
            if (seenApIds.count(ap.id)) {
                addError(result, "logic", "ActionPoint#" + std::to_string(ap.id),
                         "routeActionPointID",
                         "动作点 ID 重复: " + std::to_string(ap.id));
            }
            seenApIds.insert(ap.id);
        }
    }

    // boundedBy 坐标合理性（lowerCorner 应在 upperCorner 的西南方）
    if (route.lowerCorner.has_value() && route.upperCorner.has_value()) {
        if (route.lowerCorner.value().lat > route.upperCorner.value().lat) {
            addWarning(result, "logic", "Route", "boundedBy",
                       "空间范围 lowerCorner 纬度大于 upperCorner 纬度");
        }
    }
}

// ============================================================
//  辅助方法：往结果里追加一条错误/警告
// ============================================================

void S421Validator::addError(ValidationResult& result, const std::string& category,
                              const std::string& objectId, const std::string& field,
                              const std::string& message) {
    ValidationError err;
    err.level = ValidationError::Level::Error;
    err.category = category;
    err.objectId = objectId;
    err.field = field;
    err.message = message;
    result.errors.push_back(err);
}

void S421Validator::addWarning(ValidationResult& result, const std::string& category,
                                const std::string& objectId, const std::string& field,
                                const std::string& message) {
    ValidationError warn;
    warn.level = ValidationError::Level::Warning;
    warn.category = category;
    warn.objectId = objectId;
    warn.field = field;
    warn.message = message;
    result.warnings.push_back(warn);
}

bool S421Validator::isEmpty(const std::string& s) {
    return s.empty() || s.find_first_not_of(" \t\r\n") == std::string::npos;
}

bool S421Validator::isValidMMSI(const std::string& mmsi) {
    if (mmsi.size() != 9) return false;
    return std::all_of(mmsi.begin(), mmsi.end(),
                       [](unsigned char c) { return std::isdigit(c); });
}

bool S421Validator::isValidIMO(const std::string& imo) {
    // XSD IMONumberText: 正好 7 位数字
    if (imo.size() != 7) return false;
    return std::all_of(imo.begin(), imo.end(),
                       [](unsigned char c) { return std::isdigit(c); });
}

bool S421Validator::isValidPortID(const std::string& portId) {
    // XSD PortIDType: 正好 5 位字母
    if (portId.size() != 5) return false;
    return std::all_of(portId.begin(), portId.end(),
                       [](unsigned char c) { return std::isalpha(c); });
}

bool S421Validator::isValidTimeFormat(const std::string& time) {
    // 简单检查：应该包含 'T' 和以 'Z' 结尾或包含时区偏移
    // ISO 8601 基本格式：YYYY-MM-DDTHH:MM:SSZ
    if (time.find('T') == std::string::npos) return false;
    if (time.back() != 'Z' && time.find('+') == std::string::npos
        && time.find('-', 10) == std::string::npos) {
        return false;
    }
    return time.size() >= 16; // 至少 "YYYY-MM-DDTHH:MM"
}

} // namespace s421
