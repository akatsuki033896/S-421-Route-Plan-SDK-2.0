#pragma once

/**
 * @file S421Types.h
 * @brief S-421 枚举类型和基础数据结构（Edition 2.0）。
 *
 * Edition 2.0 关键变化：
 *   1. 枚举在 XML 中不再是整数编码，而是可读文本（如 "Planned" 而非 "2"）。
 *   2. `RouteInfoStatus::Acknowledged` 改名为 `Checked`。
 *   3. `ActionType` 删除 `Reserved1-5`，新增 `CriticalPoint` / `Others`。
 *   4. 删除 `VesselType`（旧 AIS 数字编码），新增 `TypeOfShip`（业务船型文本）。
 *
 * 因此每个枚举都配一对函数：
 *   - `toString()`   —— 枚举 → 文本（序列化写 XML 时用）
 *   - `fromString()` —— 文本 → 枚举（解析读 XML 时用，大小写/空格不敏感）
 */

#include <string>
#include <cstdint>
#include <cctype>

namespace s421 {

/**
 * @namespace s421::detail
 * @brief 实现细节，外部不应直接依赖。
 */
namespace detail {

/**
 * @brief 文本归一化：trim + 转小写 + 去除全部空白。
 *
 * 外部输入可能带前后空白、大小写不一，甚至带空格（如 `"Used for Monitoring"`）。
 * 直接用 `==` 比较很容易失败，归一化成"无空白小写串"再比较，抹平表面差异。
 *
 * @param s 外部输入文本。
 * @return 归一化后的字符串，例：`"Used for Monitoring"` → `"usedformonitoring"`。
 */
inline std::string normalize(const std::string& s) {
    std::string out;
    out.reserve(s.size());  ///< 预分配容量，避免循环里反复重新分配内存
    for (unsigned char c : s) {  ///< unsigned char：避免负值导致 isspace 未定义行为
        if (std::isspace(c)) continue;
        out.push_back(static_cast<char>(std::tolower(c)));
    }
    return out;
}

} // namespace detail

/**
 * @enum RouteInfoStatus
 * @brief 航线状态枚举（XSD: `routeInfoStatusType`）。
 *
 * 船岸航线交换的核心状态机，表示航线在生命周期中的当前阶段。
 * Edition 2.0 里旧 `Acknowledged(4)` 改名为 `Checked`。
 *
 * 流程：`Initial` → `Planned` → `Recommended` → `Checked`
 *     → `Used for Monitoring` → `Terminated`
 */
enum class RouteInfoStatus : int {
    Initial           = 1,   ///< 初始状态（刚创建，未编辑）
    Planned           = 2,   ///< 已计划（船端完成编辑，准备发送）
    Recommended       = 3,   ///< 已推荐（第三方服务返回优化建议）
    Checked           = 4,   ///< 已校验（第三方校验通过；2.0 由 Acknowledged 改名）
    UsedForMonitoring = 5,   ///< 用于监控（船端正在沿航线航行）
    Terminated        = 6,   ///< 已终止（航行结束或手动终止）
    Errors            = 7,   ///< 错误（数据无效或解析失败）
    Incomplete        = 8,   ///< 不完整（航线数据缺失）
    RouteIssues       = 9,   ///< 航线问题（安全/合规性问题）
    ServiceStarted    = 10,  ///< 服务开始（岸基开始监控此航线）
    ServiceEnded      = 11   ///< 服务结束（岸基停止监控）
};

/**
 * @brief 航线状态 → 文本（与 XSD 枚举值一致，序列化写 XML 用）。
 * @param s 状态枚举值。
 * @return 对应文本；未知值返回 `"Unknown"`。
 */
inline const char* toString(RouteInfoStatus s) {
    switch (s) {
        case RouteInfoStatus::Initial:           return "Initial";
        case RouteInfoStatus::Planned:           return "Planned";
        case RouteInfoStatus::Recommended:       return "Recommended";
        case RouteInfoStatus::Checked:           return "Checked";
        case RouteInfoStatus::UsedForMonitoring: return "Used for Monitoring";
        case RouteInfoStatus::Terminated:        return "Terminated";
        case RouteInfoStatus::Errors:            return "Errors";
        case RouteInfoStatus::Incomplete:        return "Incomplete";
        case RouteInfoStatus::RouteIssues:       return "Route issues";
        case RouteInfoStatus::ServiceStarted:    return "Service started";
        case RouteInfoStatus::ServiceEnded:      return "Service ended";
    }
    return "Unknown";
}

/**
 * @brief 航线状态 → 人类可读描述（带编号，展示层用）。
 * @param s 状态枚举值。
 * @return 形如 `"Planned(2)"` 的描述；未知值返回 `"Unknown"`。
 */
inline const char* toDescription(RouteInfoStatus s) {
    switch (s) {
        case RouteInfoStatus::Initial:           return "Initial(1)";
        case RouteInfoStatus::Planned:           return "Planned(2)";
        case RouteInfoStatus::Recommended:       return "Recommended(3)";
        case RouteInfoStatus::Checked:           return "Checked(4)";
        case RouteInfoStatus::UsedForMonitoring: return "Monitoring(5)";
        case RouteInfoStatus::Terminated:        return "Terminated(6)";
        case RouteInfoStatus::Errors:            return "Errors(7)";
        case RouteInfoStatus::Incomplete:        return "Incomplete(8)";
        case RouteInfoStatus::RouteIssues:       return "RouteIssues(9)";
        case RouteInfoStatus::ServiceStarted:    return "ServiceStarted(10)";
        case RouteInfoStatus::ServiceEnded:      return "ServiceEnded(11)";
    }
    return "Unknown";
}

/**
 * @brief 文本 → 航线状态（解析读 XML 用，大小写/空白不敏感，精确匹配）。
 *
 * 为什么用 `bool` + out 参数而不直接返回枚举：
 * 解析可能失败（文本不认识）。用 `bool` 表示成败、用 out 带出结果，
 * 比返回特殊值（如 `-1`）更安全——枚举类型里根本存不了非法值。
 *
 * @param s   外部文本（如 `"Planned"`）。
 * @param out 输出参数，成功时写入结果。
 * @return `true` = 成功（@p out 已赋值）；`false` = 文本不匹配任何已知状态。
 */
inline bool fromString(const std::string& s, RouteInfoStatus& out) {
    std::string n = detail::normalize(s);
    if (n == "initial")            { out = RouteInfoStatus::Initial;           return true; }
    if (n == "planned")            { out = RouteInfoStatus::Planned;           return true; }
    if (n == "recommended")        { out = RouteInfoStatus::Recommended;       return true; }
    if (n == "checked")            { out = RouteInfoStatus::Checked;           return true; }
    if (n == "usedformonitoring")  { out = RouteInfoStatus::UsedForMonitoring; return true; }
    if (n == "terminated")         { out = RouteInfoStatus::Terminated;        return true; }
    if (n == "errors")             { out = RouteInfoStatus::Errors;            return true; }
    if (n == "incomplete")         { out = RouteInfoStatus::Incomplete;        return true; }
    if (n == "routeissues")        { out = RouteInfoStatus::RouteIssues;       return true; }
    if (n == "servicestarted")     { out = RouteInfoStatus::ServiceStarted;    return true; }
    if (n == "serviceended")       { out = RouteInfoStatus::ServiceEnded;      return true; }
    return false;
}

/**
 * @enum LegGeometryType
 * @brief 航段几何类型（XSD: `routeWaypointLegGeometryTypeType`）。
 *
 *   1. `Loxodrome`（恒向线/Rhumb line）：地图上的直线，航向不变但距离略长。
 *   2. `Orthodrome`（大圆航线/Great circle）：球面最短路径，但航向持续变化。
 *
 * @note XSD 枚举值是长串（`"loxodrome (= rhumb line)"`），但测试示例用短串（`"loxodrome"`）。
 *       `fromString` 用前缀匹配兼容两者，`toString` 输出短串。
 */
enum class LegGeometryType : int {
    Loxodrome  = 1,   ///< 恒向线（Rhumb line）
    Orthodrome = 2    ///< 大圆航线（Great circle）
};

/**
 * @brief 航段几何类型 → 短串（与测试示例一致，便于 roundtrip）。
 * @param t 几何类型。
 * @return `"loxodrome"` / `"orthodrome"`；未知值返回 `"Unknown"`。
 */
inline const char* toString(LegGeometryType t) {
    switch (t) {
        case LegGeometryType::Loxodrome:  return "loxodrome";
        case LegGeometryType::Orthodrome: return "orthodrome";
    }
    return "Unknown";
}

/**
 * @brief 文本 → 航段几何类型（前缀匹配）。
 *
 * 为什么用前缀匹配：XSD 里这俩枚举值是长串（`"loxodrome (= rhumb line)"`），
 * 但实际测试文件用短串（`"loxodrome"`），前缀匹配两种写法都能命中，鲁棒性更好。
 *
 * @param s   外部文本。
 * @param out 输出参数，成功时写入结果。
 * @return `true` = 成功；`false` = 文本不匹配。
 *
 * @note `rfind(sub, 0) == 0` 等价于"检查字符串是否以 sub 开头"。
 */
inline bool fromString(const std::string& s, LegGeometryType& out) {
    std::string n = detail::normalize(s);
    if (n.rfind("loxodrome", 0) == 0)  { out = LegGeometryType::Loxodrome;  return true; }
    if (n.rfind("orthodrome", 0) == 0) { out = LegGeometryType::Orthodrome; return true; }
    return false;
}

/**
 * @enum ActionType
 * @brief 动作点类型（XSD: `routeActionPointRequiredActionType`）。
 *
 * 动作点（`RouteActionPoint`）表示航线上特定位置需要执行的操作。
 * Edition 2.0 删除旧 `Reserved1-5`，新增 `CriticalPoint` / `Others`。
 */
enum class ActionType : int {
    UserDefined   = 1,   ///< 用户自定义动作
    Report        = 2,   ///< 需要向 VTS 报告
    UKCM          = 3,   ///< UKC 管理（Under Keel Clearance Management，富余水深管理）
    Note          = 4,   ///< 注意事项提示
    CriticalPoint = 5,   ///< 关键点
    Reserved1     = 6,   ///< 6-9保留供未来使用
    Reserved2     = 7,
    Reserved3     = 8,
    Reserved4     = 9
};

/**
 * @brief 动作类型 → 2.0 文本。
 * @param a 动作类型。
 * @return 对应文本；未知值返回 `"Unknown"`。
 */
inline const char* toString(ActionType a) {
    switch (a) {
        case ActionType::UserDefined:   return "UserDefined";
        case ActionType::Report:        return "Report";
        case ActionType::UKCM:          return "UKCM";
        case ActionType::Note:          return "Note";
        case ActionType::CriticalPoint: return "Critical point";
        case ActionType::Others:        return "Others";
    }
    return "Unknown";
}

/**
 * @brief 文本 → 动作类型（大小写/空白不敏感，精确匹配）。
 * @param s   外部文本。
 * @param out 输出参数，成功时写入结果。
 * @return `true` = 成功；`false` = 文本不匹配。
 */
inline bool fromString(const std::string& s, ActionType& out) {
    std::string n = detail::normalize(s);
    if (n == "userdefined")   { out = ActionType::UserDefined;   return true; }
    if (n == "report")        { out = ActionType::Report;        return true; }
    if (n == "ukcm")          { out = ActionType::UKCM;          return true; }
    if (n == "note")          { out = ActionType::Note;          return true; }
    if (n == "criticalpoint") { out = ActionType::CriticalPoint; return true; }
    if (n == "others")        { out = ActionType::Others;        return true; }
    return false;
}

/**
 * @enum TypeOfShip
 * @brief 船舶类型枚举（XSD: `typeOfShipType`）。
 *
 * Edition 2.0 用 `typeOfShip` 取代旧版 `routeInfoVesselType`（AIS 数字编码）。
 * 新枚举使用更通用的业务船型文本，更贴近实际船舶分类语义。
 * 完整列表来自 `S-421_schema_20260604.xsd` 中的 `typeOfShipType` 定义。
 */
enum class TypeOfShip : int {
    GeneralCargoVessel   = 1,    ///< 杂货船
    ContainerVessel      = 2,    ///< 集装箱船
    Tanker               = 3,    ///< 油轮
    SailingVessel        = 4,    ///< 帆船
    FishingVessel        = 5,    ///< 渔船
    SpecialPurposeVessel = 6,    ///< 特种用途船
    ManOfWar             = 7,    ///< 军舰
    Submarine            = 8,    ///< 潜艇
    HighSpeedCraft       = 9,    ///< 高速船
    BulkCarrier          = 10,   ///< 散货船
    Seaplane             = 11,   ///< 水上飞机
    Tugboat              = 12,   ///< 拖船
    PassengerVessel      = 13,   ///< 客船
    Ferry                = 14,   ///< 渡轮
    Boat                 = 15    ///< 小艇
};

/**
 * @brief 船型 → 2.0 文本（带空格）。
 * @param t 船型。
 * @return 对应文本；未知值返回 `"Unknown"`。
 */
inline const char* toString(TypeOfShip t) {
    switch (t) {
        case TypeOfShip::GeneralCargoVessel:   return "General Cargo Vessel";
        case TypeOfShip::ContainerVessel:      return "Container Vessel";
        case TypeOfShip::Tanker:               return "Tanker";
        case TypeOfShip::SailingVessel:        return "Sailing Vessel";
        case TypeOfShip::FishingVessel:        return "Fishing Vessel";
        case TypeOfShip::SpecialPurposeVessel: return "Special Purpose Vessel";
        case TypeOfShip::ManOfWar:             return "Man of War";
        case TypeOfShip::Submarine:            return "Submarine";
        case TypeOfShip::HighSpeedCraft:       return "High Speed Craft";
        case TypeOfShip::BulkCarrier:          return "Bulk Carrier";
        case TypeOfShip::Seaplane:             return "Seaplane";
        case TypeOfShip::Tugboat:              return "Tugboat";
        case TypeOfShip::PassengerVessel:      return "Passenger Vessel";
        case TypeOfShip::Ferry:                return "Ferry";
        case TypeOfShip::Boat:                 return "Boat";
    }
    return "Unknown";
}

/**
 * @brief 文本 → 船型（大小写/空白不敏感，精确匹配）。
 * @param s   外部文本。
 * @param out 输出参数，成功时写入结果。
 * @return `true` = 成功；`false` = 文本不匹配。
 */
inline bool fromString(const std::string& s, TypeOfShip& out) {
    std::string n = detail::normalize(s);
    if (n == "generalcargovessel")   { out = TypeOfShip::GeneralCargoVessel;   return true; }
    if (n == "containervessel")      { out = TypeOfShip::ContainerVessel;      return true; }
    if (n == "tanker")               { out = TypeOfShip::Tanker;               return true; }
    if (n == "sailingvessel")        { out = TypeOfShip::SailingVessel;        return true; }
    if (n == "fishingvessel")        { out = TypeOfShip::FishingVessel;        return true; }
    if (n == "specialpurposevessel") { out = TypeOfShip::SpecialPurposeVessel; return true; }
    if (n == "manofwar")             { out = TypeOfShip::ManOfWar;             return true; }
    if (n == "submarine")            { out = TypeOfShip::Submarine;            return true; }
    if (n == "highspeedcraft")       { out = TypeOfShip::HighSpeedCraft;       return true; }
    if (n == "bulkcarrier")          { out = TypeOfShip::BulkCarrier;          return true; }
    if (n == "seaplane")             { out = TypeOfShip::Seaplane;             return true; }
    if (n == "tugboat")              { out = TypeOfShip::Tugboat;              return true; }
    if (n == "passengervessel")      { out = TypeOfShip::PassengerVessel;      return true; }
    if (n == "ferry")                { out = TypeOfShip::Ferry;                return true; }
    if (n == "boat")                 { out = TypeOfShip::Boat;                 return true; }
    return false;
}

/**
 * @struct Position
 * @brief WGS84 坐标（纬度 + 经度）。
 *
 * GML 标准中坐标顺序为 (纬度, 经度)，即 `(lat, lon)`。
 * - 纬度范围：-90（南极）到 +90（北极），赤道为 0。
 * - 经度范围：-180 到 +180，本初子午线为 0，东经为正。
 */
struct Position {
    double lat = 0.0;   ///< 纬度（Latitude），范围：-90 ~ +90
    double lon = 0.0;   ///< 经度（Longitude），范围：-180 ~ +180

    /// 默认构造：原点 (0, 0)。
    Position() = default;

    /**
     * @brief 以给定纬度、经度构造。
     * @param lat_ 纬度。
     * @param lon_ 经度。
     */
    Position(double lat_, double lon_) : lat(lat_), lon(lon_) {}
};

/**
 * @struct RouteExtension
 * @brief 厂商扩展信息（XSD: `routeExtensionsType`）。
 *
 * S-421 标准允许设备厂商在标准数据之外添加自定义信息。
 * 这些扩展数据不影响标准的解析和校验流程。
 */
struct RouteExtension {
    std::string manufacturerId;  ///< 厂商标识（如 `"ACME"`）
    std::string name;            ///< 扩展名称
    std::string version;         ///< 扩展版本号
    std::string note;            ///< 扩展备注/内容
};

} // namespace s421
