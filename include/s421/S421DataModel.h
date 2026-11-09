#pragma once

/**
 * @file S421DataModel.h
 * @brief S-421 核心数据模型（14 个纯数据类）。
 *
 * 数据对象层次（对应 Route plan application schema，Figure 1）：
 * @verbatim
   Dataset（XML 根）
     └── Route（航线容器：id + 版本号 + 格式版本）
           ├── RouteInfo（元数据）                        —— 1
           ├── RouteWaypoints（航点集合）                  —— 0..1
           │     └── RouteWaypoint[]（航点）               —— 2..*
           │           └── RouteWaypointLeg（航段）         —— 0..1
           ├── RouteActionPoints（动作点集合）              —— 0..1
           │     └── RouteActionPoint[]（动作点）           —— 1..*
           └── RouteSchedules（调度集合）                   —— 0..1
                 └── RouteSchedule[]（调度）               —— 0..*
                       ├── RouteScheduleManual             —— 0..1
                       ├── RouteScheduleCalculated         —— 0..1
                       └── RouteScheduleRecommended        —— 0..1
                             （三者共享 RouteScheduleElement，各 0..*）
                                 └─(waypointId 引用)→ RouteWaypoint
   @endverbatim
 *
 * 所有权约定（映射 UML composition 组合）：
 *   - 多重性 1    → 按值成员（必存在）
 *   - 多重性 0..1 → std::unique_ptr（nullptr = 不存在）
 *   - 多重性 0..* → std::vector 按值元素（独占所有权）
 */

#include "S421Optional.h"
#include "S421Types.h"
#include <memory>
#include <string>
#include <vector>

namespace s421
{

// 类定义顺序 = 依赖顺序（被持有者在前），故无需前向声明。

/**
 * @class RouteInfo
 * @brief 航线基础信息
 *
 * 对应 XSD: `complexType name="RouteInfo"`。
 * 对应 FC: `InformationType "Route Info"`。
 */
class RouteInfo
{
public:
    std::string name;                                  ///< 航线名称
    std::string author;                                ///< 创建者
    std::string editionTime;                           ///< 最后编辑时间（如 `"2024-01-01T12:00:00Z"`）
    std::string description;                           ///< 航线描述
    RouteInfoStatus status = RouteInfoStatus::Initial; ///< 航线状态（默认=初始）
    s421::optional<bool> masterApproval;               ///< 船长是否已批准该计划航线
    std::string validityStart;                         ///< 有效期开始
    std::string validityEnd;                           ///< 有效期结束

    // UN/LOCODE 是联合国制定的港口代码，如 "CNSHA" = 上海，"FJLEU" = 勒武卡
    s421::optional<std::string> departurePortID1;  ///< 出发港代码 1
    s421::optional<std::string> departurePortID2;  ///< 出发港代码 2（备用）
    s421::optional<std::string> departurePortCall; ///< 出发港呼号
    s421::optional<std::string> arrivalPortID1;    ///< 到达港代码 1
    s421::optional<std::string> arrivalPortID2;    ///< 到达港代码 2（备用）
    s421::optional<std::string> arrivalPortCall;   ///< 到达港呼号

    s421::optional<std::string> referencePrevRoute; ///< 上一段航线的引用
    s421::optional<std::string> referenceNextRoute; ///< 下一段航线的引用

    s421::optional<VesselType> vesselType;      ///< 船舶类型
    s421::optional<std::string> vesselName;     ///< 船名（如 `"BALTIC BRIGHT"`）
    std::string vesselMMSI;                     ///< MMSI 码（9 位数字，船舶唯一标识）
    s421::optional<std::string> vesselCallsign; ///< 呼号
    s421::optional<std::string> vesselIMO;      ///< IMO 编号（7 位数字，船舶终身编号）
    s421::optional<std::string> vesselVoyage;   ///< 航次号

    s421::optional<double> vesselHeight; ///< 船舶水面以上高度
    s421::optional<double> vesselLength; ///< 船长
    s421::optional<double> vesselBeam;   ///< 船宽（型宽）
    s421::optional<double> draftMax;     ///< 最大吃水（最重要的尺寸参数之一）
    s421::optional<double> airDraftMax;  ///< 最大空气吃水（水面到最高点）
    s421::optional<double> beamMax;      ///< 最大宽度
    s421::optional<double> lengthMax;    ///< 最大长度

    s421::optional<RouteExtensions> extensions; ///< 厂商扩展列表
};

/**
 * @class RouteWaypointLeg
 * @brief 航段（两个相邻航路点之间的连接段）。
 *
 * 对应 XSD: `complexType name="RouteWaypointLeg"`。
 */
class RouteWaypointLeg
{
public:
    // 横偏距限制（XTD = Cross Track Distance
    // 用于定义航段的"安全走廊"宽度，船不应偏离此范围
    s421::optional<int> starboardXTDL; ///< 右舷横偏距限制（单位：米）
    s421::optional<int> portXTDL;      ///< 左舷横偏距限制（单位：米）
    s421::optional<int> starboardCL;   ///< 右舷通道限制
    s421::optional<int> portCL;        ///< 左舷通道限制

    // 安全参数
    s421::optional<double> safetyContour; ///< 安全等深线（水深低于此值有搁浅风险）
    s421::optional<double> safetyDepth;   ///< 安全水深（最小安全水深）

    LegGeometryType geometryType = LegGeometryType::Loxodrome; ///< 几何类型（默认恒向线）

    // 航速限制（单位：节，1 节 ≈ 1.852 km/h)
    s421::optional<double> sogMin; ///< 最小对地航速（Speed Over Ground）
    s421::optional<double> sogMax; ///< 最大对地航速
    s421::optional<double> stwMin; ///< 最小对水航速（Speed Through Water）
    s421::optional<double> stwMax; ///< 最大对水航速

    // 吃水参数（单位：米）
    s421::optional<double> draft;        ///< 当前吃水
    s421::optional<double> draftForward; ///< 首吃水
    s421::optional<double> draftAft;     ///< 尾吃水
    s421::optional<double> draftMax;     ///< 最大吃水
    s421::optional<double> airDraftMax;  ///< 最大空气吃水
    s421::optional<double> beamMax;      ///< 最大船宽
    s421::optional<double> lengthMax;    ///< 最大船长

    // UKC
    s421::optional<double> staticUKC;    ///< 静态 UKC（Under Keel Clearance，富余水深）
    s421::optional<double> dynamicUKC;   ///< 动态 UKC（考虑波浪、潮汐等动态因素）
    s421::optional<double> safetyMargin; ///< 安全裕度

    // ---- 可选字段：备注 ----
    s421::optional<std::string> note;  ///< 备注
    s421::optional<std::string> issue; ///< 问题说明（VTS 检查后发现的安全问题）

    // ---- 厂商扩展 ----
    s421::optional<RouteExtensions> extensions; ///< 厂商扩展列表
};

/**
 * @class RouteScheduleElement
 * @brief 调度元素（一个航路点的时间安排）。
 *
 * 对应 XSD: `complexType name="RouteScheduleElement"`。
 * 三个子调度（Manual / Calculated / Recommended）共享本类（各 0..*）。
 */
class RouteScheduleElement
{
public:
    int waypointId = 0; ///< 关联的航路点 ID（targetWaypoint 引用 → RouteWaypoint::id）

    double planSOG = 0.0;                      ///< 计划对地航速（节）
    std::string etd;                     ///< 预计离开时间（Estimated Time of Departure）
    std::string eta;                     ///< 预计到达时间（Estimated Time of Arrival）
    s421::optional<int> etdWindowBefore; ///< ETD 时间窗口（提前量，分钟）
    s421::optional<int> etdWindowAfter;  ///< ETD 时间窗口（延后量，分钟）
    s421::optional<int> etaWindowBefore; ///< ETA 时间窗口（提前量，分钟）
    s421::optional<int> etaWindowAfter;  ///< ETA 时间窗口（延后量，分钟）
    s421::optional<std::string> note;    ///< 备注

    s421::optional<RouteExtensions> extensions; ///< 厂商扩展列表
};

/**
 * @class RouteWaypoint
 * @brief 航路点（航线上的一个位置点）。
 *
 * 对应 XSD: `complexType name="RouteWaypoint"`。
 */
class RouteWaypoint
{
public:
    int id = 0;                                 ///< 航路点唯一标识
    std::string name;                           ///< 航路点名称
    s421::optional<std::string> externalRefID;  ///< 外部引用 ID（可选）
    bool fixed = false;                         ///< 是否为固定航路点，false可编辑，true固定，默认都可编辑
    GM_Point geometry;                          ///< WGS84 坐标（纬度 + 经度）
    double turnRadius = 0.0;                    ///< 转向半径（海里，范围 0.00-5.00）
    s421::optional<RouteExtensions> extensions; ///< 厂商扩展列表

    // 组合 0..1：nullptr = 该航点无航段信息
    std::unique_ptr<RouteWaypointLeg> leg;      ///< 航段信息（连接到下一个航路点的航段）
};

/**
 * @class RouteWaypoints
 * @brief 航点集合（RouteWaypoint 的组合容器）。
 *
 * 对应 FC: `FeatureType "RouteWaypoints"`。
 * 约束：至少 2 个航点（2..*），由运行时校验保证。
 */
class RouteWaypoints
{
public:
    std::vector<RouteWaypoint> waypoints; ///< 航路点列表
};

/**
 * @class RouteActionPoint
 * @brief 动作点（航线上需要执行特定操作的地点）。
 *
 * 对应 XSD: `complexType name="RouteActionPoint"`。
 */
class RouteActionPoint
{
public:
    int id = 0;                                          ///< 动作点唯一标识(可选)
    s421::optional<std::string> name;                    ///< 动作点名称(可选)
    s421::optional<std::string> externalRef;             ///< 外部引用(可选)
    GM_Point geometry;                                   ///< 动作点的地理位置
    double distance;                                     ///< 执行动作距离（海里）
    double timeToAct;                                    ///< 执行时间（秒，可选）
    ActionType requiredAction = ActionType::UserDefined; ///< 需要执行的动作类型
    std::string actionDescription;                       ///< 动作描述（如 `"通过报告线时联系 VTS"`）
    s421::optional<RouteExtensions> extensions;          ///< 厂商指定扩展信息(可选)
};

/**
 * @class RouteActionPoints
 * @brief 动作点集合（RouteActionPoint 的组合容器）。
 *
 * 对应 FC: `FeatureType "RouteActionPoints"`。
 * 约束：至少 1 个动作点（1..*），由运行时校验保证。
 */
class RouteActionPoints
{
public:
    std::vector<RouteActionPoint> actionPoints; ///< 动作点列表
};

/**
 * @class RouteScheduleManual
 * @brief 人工调度（船员制定）
 *
 * 对应 XSD: `complexType name="RouteScheduleManual"`。
 * 注：不含 RouteSchedule 成员——图 1 中 "1" 是归属端（每个子调度属于 1 个调度），
 *     组合方向是 RouteSchedule → 子调度，反向持有会造成循环包含。
 */
class RouteScheduleManual
{
public:
    std::vector<RouteScheduleElement> elements; ///< 调度元素信息
    s421::optional<RouteExtensions> extensions; ///< 厂商扩展列表
};

/**
 * @class RouteScheduleCalculated
 * @brief 计算调度（软件优化）
 *
 * 对应 XSD: `complexType name="RouteScheduleCalculated"`。
 */
class RouteScheduleCalculated
{
public:
    std::vector<RouteScheduleElement> elements; ///< 调度元素信息
    s421::optional<RouteExtensions> extensions; ///< 厂商扩展列表
};

/**
 * @class RouteScheduleRecommended
 * @brief 推荐调度（VTS 推荐）
 *
 * 对应 XSD: `complexType name="RouteScheduleRecommended"`。
 */
class RouteScheduleRecommended
{
public:
    std::vector<RouteScheduleElement> elements; ///< 调度元素信息
    s421::optional<RouteExtensions> extensions; ///< 厂商扩展列表
};

/**
 * @class RouteSchedule
 * @brief 调度（三类子调度，各最多一个）。
 *
 * 对应 XSD: `complexType name="RouteSchedule"`。
 *   - @ref manual       —— 人工调度（船员制定），0..1
 *   - @ref calculated   —— 计算调度（软件优化），0..1
 *   - @ref recommended  —— VTS 推荐调度（岸端优化），0..1
 */
class RouteSchedule
{
public:
    int id = 0;                                  ///< 调度唯一标识
    s421::optional<std::string> name;            ///< 调度名称
    s421::optional<RouteExtensions> extensions;  ///< 厂商扩展列表

    std::unique_ptr<RouteScheduleManual> manual;             ///< 人工调度
    std::unique_ptr<RouteScheduleCalculated> calculated;     ///< 计算调度
    std::unique_ptr<RouteScheduleRecommended> recommended;   ///< 推荐调度
};

/**
 * @class RouteSchedules
 * @brief 调度集合（RouteSchedule 的组合容器）。
 *
 * 对应 FC: `InformationType "RouteSchedules"`。
 */
class RouteSchedules
{
public:
    std::vector<RouteSchedule> schedules; ///< 调度列表（0..*）
};

/**
 * @class Route
 * @brief 航线
 *
 * 对应 XSD: `complexType name="Route"`。
 */
class Route
{
public:
    std::string formatVersion = "2.0"; ///< S-421 格式版本（Edition 2.0）
    std::string id;                    ///< 航线唯一标识
    int editionNo = 1;                 ///< 版本号（每次修改递增）

    RouteInfo info; ///< 航线元数据

    std::unique_ptr<RouteWaypoints> waypoints;       ///< 航点集合
    std::unique_ptr<RouteActionPoints> actionPoints; ///< 动作点集合
    std::unique_ptr<RouteSchedules> schedules;       ///< 调度集合

    s421::optional<RouteExtensions> extensions; ///< 厂商扩展列表

    s421::optional<GM_Point> lowerCorner; ///< 边界西南角（最小纬度、最小经度）
    s421::optional<GM_Point> upperCorner; ///< 边界东北角（最大纬度、最大经度）
};

/**
 * @class Dataset
 * @brief S-421 XML 文档的根元素。
 *
 * 对应 XSD: `complexType name="DatasetType"`。
 */
class Dataset
{
public:
    std::string gmlId;            ///< GML 标识（如 `"S421.BALTIC"`）
    std::unique_ptr<Route> route; ///< 航线数据（核心内容）
};

} // namespace s421
