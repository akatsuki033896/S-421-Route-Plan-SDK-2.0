// S421Formatter.cpp - 数据 → 人类可读屏幕输出的格式化器
//
// verbose=false → 一行摘要（航点数/航段数/动作点数/调度数）
// verbose=true  → 详细信息（每个航点坐标、航段参数）

#include "s421/S421Formatter.h"
#include "s421/S421Logger.h"
#include <iomanip>
#include <sstream>

namespace s421 {

// ============================================================
//  内部辅助函数
// ============================================================

// pad - 生成指定数量的空格（用于缩进对齐）
static std::string pad(int indent) {
    return std::string(indent, ' ');
}

// fmtOpt - 格式化输出一个可选字段（有值时才输出）
template<typename T>
static void fmtOpt(std::ostream& os, const char* label,
                   const s421::optional<T>& val, int indent) {
    if (val.has_value()) {
        os << pad(indent) << label << ": " << val.value() << "\n";
    }
}

// ============================================================
//  formatDataset - 主入口函数
//  把 Dataset 以可读格式输出到 os；verbose=true 详细，false 一行摘要。
//  返回 FormatResult（航点数/航段数统计），供调用方日志使用，避免重复计数。
// ============================================================
FormatResult formatDataset(const Dataset& ds, std::ostream& os, bool verbose) {
    FormatResult result;

    os << "Dataset gml:id=" << ds.gmlId << "\n";

    // 防御性：解析出问题可能没 Route
    if (!ds.route) {
        os << "  (无航线数据)\n";
        return result;
    }

    const auto& r = *ds.route;

    if (verbose) {
        // ---- 详细模式：输出所有信息 ----

        // 航线基本信息
        os << pad(2) << "航线: id=" << r.id
           << " 版本=" << r.formatVersion
           << " 版本号=" << r.editionNo << "\n";

        // 航线元数据
        if (r.info) {
            os << pad(4) << "元数据: 名称=" << r.info->name
               << " 状态=" << toString(r.info->status);
            if (r.info->author.has_value())
                os << " 创建者=" << r.info->author.value();
            if (r.info->vesselMMSI.has_value())
                os << " MMSI=" << r.info->vesselMMSI.value();
            if (r.info->typeOfShip.has_value())
                os << " 船型=" << toString(*r.info->typeOfShip);
            if (r.info->masterApproval.has_value())
                os << " 船长批准=" << (r.info->masterApproval.value() ? "是" : "否");
            os << "\n";
        }

        // 航路点列表（紧凑格式）
        for (const auto& wp : r.waypoints) {
            if (!wp) continue;
            os << pad(4) << "航路点#" << wp->id
               << " (" << std::fixed << std::setprecision(6)
               << wp->geometry.lat << ", " << wp->geometry.lon << ")";
            if (wp->name.has_value())
                os << " 名称=" << wp->name.value();
            os << " 转向半径=" << std::fixed << std::setprecision(2) << wp->turnRadius << "海里";
            if (wp->fixed.has_value())
                os << (wp->fixed.value() ? " 固定" : " 可调");
            os << "\n";

            // 关联的航段信息
            if (wp->leg && verbose) {
                const auto& leg = *wp->leg;
                os << pad(6) << "航段: " << toString(leg.geometryType);
                if (leg.sogMax.has_value())
                    os << " 最大航速=" << leg.sogMax.value() << "节";
                if (leg.safetyDepth.has_value())
                    os << " 安全水深=" << leg.safetyDepth.value() << "米";
                os << "\n";
            }
        }

        // 动作点列表
        for (const auto& ap : r.actionPoints) {
            if (!ap) continue;
            os << pad(4) << "动作点#" << ap->id
               << " 动作=" << toString(ap->requiredAction)
               << " 描述=" << ap->actionDescription << "\n";
        }

        // 调度列表
        for (const auto& sched : r.schedules) {
            if (!sched) continue;
            os << pad(4) << "调度#" << sched->id;
            int total = sched->manualElements.size()
                      + sched->calculatedElements.size()
                      + sched->recommendedElements.size();
            os << " 元素数=" << total << "\n";
        }
    }

    // ---- 统计计数（无论是否 verbose 都要计算）----
    result.waypointCount = static_cast<int>(r.waypoints.size());
    result.actionPointCount = static_cast<int>(r.actionPoints.size());
    result.scheduleCount = static_cast<int>(r.schedules.size());
    for (const auto& wp : r.waypoints) {
        if (wp && wp->leg) result.legCount++;
    }

    // ---- 简洁模式：只输出一行摘要 ----
    if (!verbose) {
        os << pad(2) << "航路点=" << result.waypointCount
           << " 航段=" << result.legCount
           << " 动作点=" << result.actionPointCount
           << " 调度=" << result.scheduleCount << "\n";
    }

    return result;
}

// ============================================================
//  summarizeRoute - 生成航线的一行摘要（用于日志）
// ============================================================
std::string summarizeRoute(const Route& route) {
    std::ostringstream oss;
    oss << "航线=" << route.id
        << " 版本号=" << route.editionNo
        << " 航路点数=" << route.waypoints.size();
    if (route.info) {
        oss << " 状态=" << toString(route.info->status);
    }
    return oss.str();
}

} // namespace s421
