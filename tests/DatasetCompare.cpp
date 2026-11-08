// DatasetCompare.cpp - Dataset 深度逐字段比较的实现
//
// 组织方式：
//   - Ctx   ：比较上下文（容差 + 首个差异），done() 后各比较函数全部短路
//   - cmp*  ：标量 / optional / 容器 的小比较器，拼接"路径"前缀
//   - 每个模型类一个 compare 函数，字段顺序与 S421DataModel.h 声明顺序一致，
//     便于 diff 路径直接对照头文件定位。

#include "DatasetCompare.h"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <vector>

namespace s421 {
namespace test {

namespace {

// ============================================================
//  比较上下文
// ============================================================
struct Ctx {
    double eps = 1e-6;
    DatasetDiff result;

    /// 发现首个差异后全部短路
    bool done() const { return !result.equal; }

    void diff(const std::string& path, const std::string& va, const std::string& vb) {
        result.equal = false;
        result.firstDifference = path + ": [" + va + "] vs [" + vb + "]";
    }
};

// ============================================================
//  值格式化（仅供差异报告展示）
// ============================================================
std::string str(bool v)               { return v ? "true" : "false"; }
std::string str(int v)                { return std::to_string(v); }
std::string str(const std::string& v) { return "\"" + v + "\""; }

std::string str(double v) {
    std::ostringstream oss;
    oss << std::setprecision(9) << v;
    return oss.str();
}

std::string str(const GM_Point& p) {
    std::ostringstream oss;
    oss << std::setprecision(9) << "(" << p.lat << ", " << p.lon << ")";
    return oss.str();
}

std::string str(RouteInfoStatus v) { return toString(v); }
std::string str(LegGeometryType v) { return toString(v); }
std::string str(ActionType v)      { return toString(v); }
std::string str(TypeOfShip v)      { return toString(v); }

// ============================================================
//  标量比较
// ============================================================
bool eqDouble(double a, double b, double eps) { return std::fabs(a - b) <= eps; }
bool eqPoint(const GM_Point& a, const GM_Point& b, double eps) {
    return eqDouble(a.lat, b.lat, eps) && eqDouble(a.lon, b.lon, eps);
}

void cmpStr(Ctx& c, const std::string& p, const std::string& a, const std::string& b) {
    if (!c.done() && a != b) c.diff(p, str(a), str(b));
}

void cmpInt(Ctx& c, const std::string& p, int a, int b) {
    if (!c.done() && a != b) c.diff(p, str(a), str(b));
}

void cmpBool(Ctx& c, const std::string& p, bool a, bool b) {
    if (!c.done() && a != b) c.diff(p, str(a), str(b));
}

void cmpDouble(Ctx& c, const std::string& p, double a, double b) {
    if (!c.done() && !eqDouble(a, b, c.eps)) c.diff(p, str(a), str(b));
}

void cmpPoint(Ctx& c, const std::string& p, const GM_Point& a, const GM_Point& b) {
    if (!c.done() && !eqPoint(a, b, c.eps)) c.diff(p, str(a), str(b));
}

template <typename T>
void cmpEnum(Ctx& c, const std::string& p, T a, T b) {
    if (!c.done() && a != b) c.diff(p, str(a), str(b));
}

// ============================================================
//  optional 比较
// ============================================================

/// 有值性必须一致；值相等比较走 T 的 operator==（string/bool/int/枚举）
template <typename T>
void cmpOpt(Ctx& c, const std::string& p, const optional<T>& a, const optional<T>& b) {
    if (c.done()) return;
    if (a.has_value() != b.has_value()) {
        c.diff(p + ".presence",
               a.has_value() ? "set" : "unset",
               b.has_value() ? "set" : "unset");
        return;
    }
    if (a.has_value() && !(*a == *b)) c.diff(p, str(*a), str(*b));
}

/// optional<double>：值比较带容差
void cmpOptDouble(Ctx& c, const std::string& p,
                  const optional<double>& a, const optional<double>& b) {
    if (c.done()) return;
    if (a.has_value() != b.has_value()) {
        c.diff(p + ".presence",
               a.has_value() ? "set" : "unset",
               b.has_value() ? "set" : "unset");
        return;
    }
    if (a.has_value() && !eqDouble(*a, *b, c.eps)) c.diff(p, str(*a), str(*b));
}

/// optional<GM_Point>：值比较带容差
void cmpOptPoint(Ctx& c, const std::string& p,
                 const optional<GM_Point>& a, const optional<GM_Point>& b) {
    if (c.done()) return;
    if (a.has_value() != b.has_value()) {
        c.diff(p + ".presence",
               a.has_value() ? "set" : "unset",
               b.has_value() ? "set" : "unset");
        return;
    }
    if (a.has_value() && !eqPoint(*a, *b, c.eps)) c.diff(p, str(*a), str(*b));
}

/// "空串 = 未设置" 归一：对齐解析端 parseExtension 对缺失属性写入空串的行为
optional<std::string> normOptStr(const optional<std::string>& v) {
    if (v.has_value() && v->empty()) return nullopt;
    return v;
}

// ============================================================
//  RouteExtensions（厂商扩展）
// ============================================================
void cmpExtensions(Ctx& c, const std::string& p,
                   const optional<RouteExtensions>& a,
                   const optional<RouteExtensions>& b) {
    if (c.done()) return;
    if (a.has_value() != b.has_value()) {
        c.diff(p + ".presence",
               a.has_value() ? "set" : "unset",
               b.has_value() ? "set" : "unset");
        return;
    }
    if (!a.has_value()) return;

    cmpStr(c, p + ".manufacturerId", a->manufacturerId, b->manufacturerId);
    // name/version/content：按"空串 = 未设置"归一后比较（见文件头注释第 3 条）
    cmpOpt(c, p + ".name",    normOptStr(a->name),    normOptStr(b->name));
    cmpOpt(c, p + ".version", normOptStr(a->version), normOptStr(b->version));
    cmpOpt(c, p + ".content", normOptStr(a->content), normOptStr(b->content));
}

// ============================================================
//  RouteInfo（航线元数据）
// ============================================================
void cmpRouteInfo(Ctx& c, const RouteInfo& a, const RouteInfo& b) {
    const std::string p = "route.info";
    cmpStr(c, p + ".name", a.name, b.name);
    cmpStr(c, p + ".author", a.author, b.author);
    cmpStr(c, p + ".editionTime", a.editionTime, b.editionTime);
    cmpStr(c, p + ".description", a.description, b.description);
    cmpEnum(c, p + ".status", a.status, b.status);
    cmpOpt(c, p + ".masterApproval", a.masterApproval, b.masterApproval);
    cmpStr(c, p + ".validityStart", a.validityStart, b.validityStart);
    cmpStr(c, p + ".validityEnd", a.validityEnd, b.validityEnd);

    cmpOpt(c, p + ".departurePortID1", a.departurePortID1, b.departurePortID1);
    cmpOpt(c, p + ".departurePortID2", a.departurePortID2, b.departurePortID2);
    cmpOpt(c, p + ".departurePortCall", a.departurePortCall, b.departurePortCall);
    cmpOpt(c, p + ".arrivalPortID1", a.arrivalPortID1, b.arrivalPortID1);
    cmpOpt(c, p + ".arrivalPortID2", a.arrivalPortID2, b.arrivalPortID2);
    cmpOpt(c, p + ".arrivalPortCall", a.arrivalPortCall, b.arrivalPortCall);

    cmpOpt(c, p + ".referencePrevRoute", a.referencePrevRoute, b.referencePrevRoute);
    cmpOpt(c, p + ".referenceNextRoute", a.referenceNextRoute, b.referenceNextRoute);

    cmpOpt(c, p + ".vesselType", a.vesselType, b.vesselType);
    cmpOpt(c, p + ".vesselName", a.vesselName, b.vesselName);
    cmpStr(c, p + ".vesselMMSI", a.vesselMMSI, b.vesselMMSI);
    cmpOpt(c, p + ".vesselCallsign", a.vesselCallsign, b.vesselCallsign);
    cmpOpt(c, p + ".vesselIMO", a.vesselIMO, b.vesselIMO);
    cmpOpt(c, p + ".vesselVoyage", a.vesselVoyage, b.vesselVoyage);

    cmpOptDouble(c, p + ".vesselHeight", a.vesselHeight, b.vesselHeight);
    cmpOptDouble(c, p + ".vesselLength", a.vesselLength, b.vesselLength);
    cmpOptDouble(c, p + ".vesselBeam", a.vesselBeam, b.vesselBeam);
    cmpOptDouble(c, p + ".draftMax", a.draftMax, b.draftMax);
    cmpOptDouble(c, p + ".airDraftMax", a.airDraftMax, b.airDraftMax);
    cmpOptDouble(c, p + ".beamMax", a.beamMax, b.beamMax);
    cmpOptDouble(c, p + ".lengthMax", a.lengthMax, b.lengthMax);

    cmpExtensions(c, p + ".extensions", a.extensions, b.extensions);
}

// ============================================================
//  RouteWaypointLeg（航段）
// ============================================================
void cmpLeg(Ctx& c, const std::string& p,
            const RouteWaypointLeg& a, const RouteWaypointLeg& b) {
    cmpOpt(c, p + ".starboardXTDL", a.starboardXTDL, b.starboardXTDL);
    cmpOpt(c, p + ".portXTDL", a.portXTDL, b.portXTDL);
    cmpOpt(c, p + ".starboardCL", a.starboardCL, b.starboardCL);
    cmpOpt(c, p + ".portCL", a.portCL, b.portCL);

    cmpOptDouble(c, p + ".safetyContour", a.safetyContour, b.safetyContour);
    cmpOptDouble(c, p + ".safetyDepth", a.safetyDepth, b.safetyDepth);

    cmpEnum(c, p + ".geometryType", a.geometryType, b.geometryType);

    cmpOptDouble(c, p + ".sogMin", a.sogMin, b.sogMin);
    cmpOptDouble(c, p + ".sogMax", a.sogMax, b.sogMax);
    cmpOptDouble(c, p + ".stwMin", a.stwMin, b.stwMin);
    cmpOptDouble(c, p + ".stwMax", a.stwMax, b.stwMax);

    cmpOptDouble(c, p + ".draft", a.draft, b.draft);
    cmpOptDouble(c, p + ".draftForward", a.draftForward, b.draftForward);
    cmpOptDouble(c, p + ".draftAft", a.draftAft, b.draftAft);
    cmpOptDouble(c, p + ".draftMax", a.draftMax, b.draftMax);
    cmpOptDouble(c, p + ".airDraftMax", a.airDraftMax, b.airDraftMax);
    cmpOptDouble(c, p + ".beamMax", a.beamMax, b.beamMax);
    cmpOptDouble(c, p + ".lengthMax", a.lengthMax, b.lengthMax);

    cmpOptDouble(c, p + ".staticUKC", a.staticUKC, b.staticUKC);
    cmpOptDouble(c, p + ".dynamicUKC", a.dynamicUKC, b.dynamicUKC);
    cmpOptDouble(c, p + ".safetyMargin", a.safetyMargin, b.safetyMargin);

    cmpOpt(c, p + ".note", a.note, b.note);
    cmpOpt(c, p + ".issue", a.issue, b.issue);

    cmpExtensions(c, p + ".extensions", a.extensions, b.extensions);
}

// ============================================================
//  RouteWaypoint（航路点）
// ============================================================
void cmpWaypoint(Ctx& c, const std::string& p,
                 const RouteWaypoint& a, const RouteWaypoint& b) {
    cmpInt(c, p + ".id", a.id, b.id);
    cmpStr(c, p + ".name", a.name, b.name);
    cmpOpt(c, p + ".externalRefID", a.externalRefID, b.externalRefID);
    cmpBool(c, p + ".fixed", a.fixed, b.fixed);
    cmpPoint(c, p + ".geometry", a.geometry, b.geometry);
    cmpDouble(c, p + ".turnRadius", a.turnRadius, b.turnRadius);
    cmpExtensions(c, p + ".extensions", a.extensions, b.extensions);

    // leg（0..1 组合）
    if (c.done()) return;
    if ((bool)a.leg != (bool)b.leg) {
        c.diff(p + ".leg.presence",
               a.leg ? "set" : "unset",
               b.leg ? "set" : "unset");
        return;
    }
    if (a.leg) cmpLeg(c, p + ".leg", *a.leg, *b.leg);
}

// ============================================================
//  RouteActionPoint（动作点）
// ============================================================
void cmpActionPoint(Ctx& c, const std::string& p,
                    const RouteActionPoint& a, const RouteActionPoint& b) {
    cmpInt(c, p + ".id", a.id, b.id);
    cmpOpt(c, p + ".name", a.name, b.name);
    cmpOpt(c, p + ".externalRef", a.externalRef, b.externalRef);
    cmpPoint(c, p + ".geometry", a.geometry, b.geometry);
    cmpDouble(c, p + ".radius", a.radius, b.radius);
    cmpDouble(c, p + ".timeToAct", a.timeToAct, b.timeToAct);
    cmpEnum(c, p + ".requiredAction", a.requiredAction, b.requiredAction);
    cmpStr(c, p + ".actionDescription", a.actionDescription, b.actionDescription);
    cmpExtensions(c, p + ".extensions", a.extensions, b.extensions);
}

// ============================================================
//  RouteScheduleElement（调度元素）
// ============================================================
void cmpScheduleElement(Ctx& c, const std::string& p,
                        const RouteScheduleElement& a, const RouteScheduleElement& b) {
    cmpInt(c, p + ".waypointId", a.waypointId, b.waypointId);
    cmpDouble(c, p + ".planSOG", a.planSOG, b.planSOG);
    cmpStr(c, p + ".etd", a.etd, b.etd);
    cmpStr(c, p + ".eta", a.eta, b.eta);
    cmpOpt(c, p + ".etdWindowBefore", a.etdWindowBefore, b.etdWindowBefore);
    cmpOpt(c, p + ".etdWindowAfter", a.etdWindowAfter, b.etdWindowAfter);
    cmpOpt(c, p + ".etaWindowBefore", a.etaWindowBefore, b.etaWindowBefore);
    cmpOpt(c, p + ".etaWindowAfter", a.etaWindowAfter, b.etaWindowAfter);
    cmpOpt(c, p + ".note", a.note, b.note);
    // extensions 已由 FieldSpec 表驱动解析+写出（原保真缺口已修复），纳入比较
    cmpExtensions(c, p + ".extensions", a.extensions, b.extensions);
}

// ============================================================
//  子调度容器（Manual / Calculated / Recommended 同构，模板共用）
// ============================================================
template <typename SubT>
void cmpScheduleSub(Ctx& c, const std::string& p,
                    const std::unique_ptr<SubT>& a,
                    const std::unique_ptr<SubT>& b) {
    if (c.done()) return;
    if ((bool)a != (bool)b) {
        c.diff(p + ".presence", a ? "set" : "unset", b ? "set" : "unset");
        return;
    }
    if (!a) return;

    const size_t na = a->elements.size(), nb = b->elements.size();
    if (na != nb) {
        c.diff(p + ".elements.size", std::to_string(na), std::to_string(nb));
        return;
    }
    for (size_t i = 0; i < na && !c.done(); i++) {
        cmpScheduleElement(c, p + ".elements[" + std::to_string(i) + "]",
                           a->elements[i], b->elements[i]);
    }
    cmpExtensions(c, p + ".extensions", a->extensions, b->extensions);
}

// ============================================================
//  RouteSchedule（调度）
// ============================================================
void cmpSchedule(Ctx& c, const std::string& p,
                 const RouteSchedule& a, const RouteSchedule& b) {
    cmpInt(c, p + ".id", a.id, b.id);
    cmpOpt(c, p + ".name", a.name, b.name);
    // extensions 已由 FieldSpec 表驱动解析+写出（原保真缺口已修复），纳入比较
    cmpExtensions(c, p + ".extensions", a.extensions, b.extensions);

    cmpScheduleSub(c, p + ".manual", a.manual, b.manual);
    cmpScheduleSub(c, p + ".calculated", a.calculated, b.calculated);
    cmpScheduleSub(c, p + ".recommended", a.recommended, b.recommended);
}

// ============================================================
//  Route（航线）
// ============================================================
void cmpRoute(Ctx& c, const std::string& p, const Route& a, const Route& b) {
    cmpStr(c, p + ".formatVersion", a.formatVersion, b.formatVersion);
    cmpStr(c, p + ".id", a.id, b.id);
    cmpInt(c, p + ".editionNo", a.editionNo, b.editionNo);

    cmpRouteInfo(c, a.info, b.info);

    // waypoints（0..1 容器 + 值向量）
    if (!c.done() && (bool)a.waypoints != (bool)b.waypoints) {
        c.diff(p + ".waypoints.presence",
               a.waypoints ? "set" : "unset",
               b.waypoints ? "set" : "unset");
    }
    if (!c.done() && a.waypoints) {
        const std::vector<RouteWaypoint>& wa = a.waypoints->waypoints;
        const std::vector<RouteWaypoint>& wb = b.waypoints->waypoints;
        if (wa.size() != wb.size()) {
            c.diff(p + ".waypoints.size", std::to_string(wa.size()), std::to_string(wb.size()));
        } else {
            for (size_t i = 0; i < wa.size() && !c.done(); i++) {
                cmpWaypoint(c, p + ".waypoints[" + std::to_string(i) + "]", wa[i], wb[i]);
            }
        }
    }

    // actionPoints
    if (!c.done() && (bool)a.actionPoints != (bool)b.actionPoints) {
        c.diff(p + ".actionPoints.presence",
               a.actionPoints ? "set" : "unset",
               b.actionPoints ? "set" : "unset");
    }
    if (!c.done() && a.actionPoints) {
        const std::vector<RouteActionPoint>& aa = a.actionPoints->actionPoints;
        const std::vector<RouteActionPoint>& ab = b.actionPoints->actionPoints;
        if (aa.size() != ab.size()) {
            c.diff(p + ".actionPoints.size", std::to_string(aa.size()), std::to_string(ab.size()));
        } else {
            for (size_t i = 0; i < aa.size() && !c.done(); i++) {
                cmpActionPoint(c, p + ".actionPoints[" + std::to_string(i) + "]", aa[i], ab[i]);
            }
        }
    }

    // schedules
    if (!c.done() && (bool)a.schedules != (bool)b.schedules) {
        c.diff(p + ".schedules.presence",
               a.schedules ? "set" : "unset",
               b.schedules ? "set" : "unset");
    }
    if (!c.done() && a.schedules) {
        const std::vector<RouteSchedule>& sa = a.schedules->schedules;
        const std::vector<RouteSchedule>& sb = b.schedules->schedules;
        if (sa.size() != sb.size()) {
            c.diff(p + ".schedules.size", std::to_string(sa.size()), std::to_string(sb.size()));
        } else {
            for (size_t i = 0; i < sa.size() && !c.done(); i++) {
                cmpSchedule(c, p + ".schedules[" + std::to_string(i) + "]", sa[i], sb[i]);
            }
        }
    }

    cmpExtensions(c, p + ".extensions", a.extensions, b.extensions);
    cmpOptPoint(c, p + ".lowerCorner", a.lowerCorner, b.lowerCorner);
    cmpOptPoint(c, p + ".upperCorner", a.upperCorner, b.upperCorner);
}

} // anonymous namespace（内部辅助到此为止）

// ============================================================
//  公共入口
// ============================================================
DatasetDiff compareDatasets(const Dataset& a, const Dataset& b, double eps) {
    Ctx c;
    c.eps = eps;

    cmpStr(c, "dataset.gmlId", a.gmlId, b.gmlId);

    if (!c.done() && (bool)a.route != (bool)b.route) {
        c.diff("dataset.route.presence",
               a.route ? "set" : "unset",
               b.route ? "set" : "unset");
    }
    if (!c.done() && a.route) {
        cmpRoute(c, "route", *a.route, *b.route);
    }

    return c.result;
}

} // namespace test
} // namespace s421
