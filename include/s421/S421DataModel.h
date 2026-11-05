#pragma once

/**
 * @file S421DataModel.h
 * @brief S-421 核心数据模型（8 个纯数据类）。
 *
 * 数据对象层次：
 * @verbatim
   Dataset（XML 根）
     └── Route（航线容器：id + 版本号 + 格式版本）
           ├── RouteInfo（元数据）
           ├── RouteWaypoint[]（航路点：坐标 + 转向半径）
           │     └── RouteWaypointLeg（航段：几何 + 安全 + 航速）
           ├── RouteActionPoint[]（动作点）
           └── RouteSchedule[]（manual / calculated / recommended 三类元素）
   @endverbatim
 *
 * 设计：纯数据容器，展示由 S421Formatter 负责。
 * 字段类型：`T`=必填 / `optional<T>`=可选 / `vector<T>`=多个 / `unique_ptr<T>`=独占子对象。
 */

#include "S421Types.h"
#include <string>
#include <vector>
#include "S421Optional.h"
#include <memory>

namespace s421 {

class RouteInfo;
class RouteWaypoint;
class RouteWaypointLeg;
class RouteActionPoint;
class RouteSchedule;
class RouteScheduleElement;

/**
 * @class RouteInfo
 * @brief 航线元数据（S-421 中字段最多的类，30+ 属性）。
 *
 * 对应 XSD: `complexType name="RouteInfo"`。
 * 对应 FC: `InformationType "Route Info"`。
 */
class RouteInfo {
public:
    // ---- 必填字段 ----
    std::string name;                                   ///< 航线名称
    RouteInfoStatus status = RouteInfoStatus::Initial;  ///< 航线状态（默认=初始）

    // ---- 可选字段：时间信息（UTC ISO 8601 格式，以 Z 结尾）----
    s421::optional<std::string> author;          ///< 创建者
    s421::optional<std::string> editionTime;     ///< 最后编辑时间（如 `"2024-01-01T12:00:00Z"`）
    s421::optional<std::string> description;     ///< 航线描述
    s421::optional<std::string> validityStart;   ///< 有效期开始
    s421::optional<std::string> validityEnd;     ///< 有效期结束

    // ---- 可选字段：港口信息（UN/LOCODE = 5 位字母代码）----
    // UN/LOCODE 是联合国制定的港口代码，如 "CNSHA" = 上海，"FJLEU" = 勒武卡
    s421::optional<std::string> departurePortID1;    ///< 出发港代码 1
    s421::optional<std::string> departurePortID2;    ///< 出发港代码 2（备用）
    s421::optional<std::string> departurePortCall;   ///< 出发港呼号
    s421::optional<std::string> arrivalPortID1;      ///< 到达港代码 1
    s421::optional<std::string> arrivalPortID2;      ///< 到达港代码 2（备用）
    s421::optional<std::string> arrivalPortCall;     ///< 到达港呼号

    // ---- 可选字段：航线链接（串联多段航线时使用）----
    s421::optional<std::string> referencePrevRoute;  ///< 上一段航线的引用
    s421::optional<std::string> referenceNextRoute;  ///< 下一段航线的引用

    // ---- 可选字段：船舶信息 ----
    s421::optional<TypeOfShip> typeOfShip;       ///< 船舶类型（2.0：业务船型文本枚举，取代旧 vesselType）
    s421::optional<std::string> vesselName;      ///< 船名（如 `"BALTIC BRIGHT"`）
    s421::optional<std::string> vesselMMSI;      ///< MMSI 码（9 位数字，船舶唯一标识）
    s421::optional<std::string> vesselCallsign;  ///< 呼号
    s421::optional<std::string> vesselIMO;       ///< IMO 编号（7 位数字，船舶终身编号）
    s421::optional<std::string> vesselVoyage;    ///< 航次号
    s421::optional<bool> masterApproval;         ///< 船长是否已批准该计划航线（2.0 新增）

    // ---- 可选字段：船舶尺寸（单位：米，精度 0.01）----
    s421::optional<double> vesselHeight;         ///< 船舶水面以上高度
    s421::optional<double> vesselLength;         ///< 船长
    s421::optional<double> vesselBeam;           ///< 船宽（型宽）
    s421::optional<double> draftMax;             ///< 最大吃水（最重要的尺寸参数之一）
    s421::optional<double> airDraftMax;          ///< 最大空气吃水（水面到最高点）
    s421::optional<double> beamMax;              ///< 最大宽度
    s421::optional<double> lengthMax;            ///< 最大长度

    // ---- 厂商扩展 ----
    std::vector<RouteExtension> extensions;      ///< 厂商扩展列表
};

/**
 * @class RouteWaypoint
 * @brief 航路点（航线上的一个位置点）。
 *
 * 对应 XSD: `complexType name="RouteWaypoint"`。
 */
class RouteWaypoint {
public:
    // ---- 必填字段 ----
    Position geometry;           ///< WGS84 坐标（纬度 + 经度）
    int id = 0;                  ///< 航路点唯一标识（正整数）
    double turnRadius = 0.0;     ///< 转向半径（海里，范围 0.00-5.00）

    // ---- 可选字段 ----
    s421::optional<std::string> name;            ///< 航路点名称（如 `"WP1"`、`"转弯点A"`）
    s421::optional<std::string> externalRefID;   ///< 外部引用 ID
    s421::optional<bool> fixed;                  ///< 是否为固定航路点；`true`=不可被 VTS 移动，`false`=可以被优化
    s421::optional<std::string> targetWaypointRef; ///< 调度元素引用（2.0 新增 targetWaypoint 的 xlink:href）

    // ---- 关联的航段 ----
    /// 从此航路点到下一个航路点之间的航段信息。
    /// 最后一个航路点通常没有 leg（因为没有"下一个"航路点）。
    std::unique_ptr<RouteWaypointLeg> leg;

    // ---- 厂商扩展 ----
    std::vector<RouteExtension> extensions;      ///< 厂商扩展列表
};

/**
 * @class RouteWaypointLeg
 * @brief 航段（两个相邻航路点之间的连接段）。
 *
 * 对应 XSD: `complexType name="RouteWaypointLeg"`。
 */
class RouteWaypointLeg {
public:
    // ---- 必填字段 ----
    LegGeometryType geometryType = LegGeometryType::Loxodrome;  ///< 几何类型（默认恒向线）

    // ---- 可选字段：横偏距限制（XTD = Cross Track Distance）----
    // 用于定义航段的"安全走廊"宽度，船不应偏离此范围
    s421::optional<int> starboardXTDL;   ///< 右舷横偏距限制（单位：米）
    s421::optional<int> portXTDL;        ///< 左舷横偏距限制（单位：米）
    s421::optional<int> starboardCL;     ///< 右舷通道限制
    s421::optional<int> portCL;          ///< 左舷通道限制

    // ---- 可选字段：安全参数 ----
    s421::optional<double> safetyContour;   ///< 安全等深线（水深低于此值有搁浅风险）
    s421::optional<double> safetyDepth;     ///< 安全水深（最小安全水深）
    s421::optional<double> staticUKC;       ///< 静态 UKC（Under Keel Clearance，富余水深）
    s421::optional<double> dynamicUKC;      ///< 动态 UKC（考虑波浪、潮汐等动态因素）
    s421::optional<double> safetyMargin;    ///< 安全裕度

    // ---- 可选字段：航速限制（单位：节，1 节 ≈ 1.852 km/h）----
    s421::optional<double> sogMin;    ///< 最小对地航速（Speed Over Ground）
    s421::optional<double> sogMax;    ///< 最大对地航速
    s421::optional<double> stwMin;    ///< 最小对水航速（Speed Through Water）
    s421::optional<double> stwMax;    ///< 最大对水航速

    // ---- 可选字段：吃水参数（单位：米）----
    s421::optional<double> draft;         ///< 当前吃水
    s421::optional<double> draftForward;  ///< 首吃水
    s421::optional<double> draftAft;      ///< 尾吃水
    s421::optional<double> draftMax;      ///< 最大吃水
    s421::optional<double> airDraftMax;   ///< 最大空气吃水
    s421::optional<double> beamMax;       ///< 最大船宽
    s421::optional<double> lengthMax;     ///< 最大船长

    // ---- 可选字段：备注 ----
    s421::optional<std::string> note;     ///< 备注
    s421::optional<std::string> issue;    ///< 问题说明（VTS 检查后发现的安全问题）

    // ---- 厂商扩展 ----
    std::vector<RouteExtension> extensions;  ///< 厂商扩展列表
};

/**
 * @class RouteActionPoint
 * @brief 动作点（航线上需要执行特定操作的地点）。
 *
 * 对应 XSD: `complexType name="RouteActionPoint"`。
 */
class RouteActionPoint {
public:
    int id = 0;                                           ///< 动作点唯一标识(可选)
    s421::optional<std::string> name;                     ///< 动作点名称(可选)
    s421::optional<std::string> externalRef;              ///< 外部引用(可选)
    Position geometry;                                    ///< 动作点的地理位置
    double distance;                                      ///< 执行动作距离（海里）
    double timeToAct;                                     ///< 执行时间（秒）
    ActionType requiredAction = ActionType::UserDefined;  ///< 需要执行的动作类型
    std::string actionDescription;                        ///< 动作描述（如 `"通过报告线时联系 VTS"`）
    std::vector<RouteExtension> extensions;               ///< 厂商指定扩展信息(可选)
};

/**
 * @class RouteScheduleElement
 * @brief 调度元素（一个航路点的时间安排）。
 *
 * 对应 XSD: `complexType name="RouteScheduleElement"`。
 */
class RouteScheduleElement {
public:
    int waypointId = 0;                          ///< 关联的航路点 ID

    s421::optional<double> planSOG;              ///< 计划对地航速（节）
    s421::optional<std::string> etd;             ///< 预计离开时间（Estimated Time of Departure）
    s421::optional<std::string> eta;             ///< 预计到达时间（Estimated Time of Arrival）
    s421::optional<int> etdWindowBefore;         ///< ETD 时间窗口（提前量，分钟）
    s421::optional<int> etdWindowAfter;          ///< ETD 时间窗口（延后量，分钟）
    s421::optional<int> etaWindowBefore;         ///< ETA 时间窗口（提前量，分钟）
    s421::optional<int> etaWindowAfter;          ///< ETA 时间窗口（延后量，分钟）
    s421::optional<std::string> note;            ///< 备注

    std::vector<RouteExtension> extensions;      ///< 厂商扩展列表
};

/**
 * @class RouteSchedule
 * @brief 调度容器（三类调度元素）。
 *
 * 对应 XSD: `complexType name="RouteSchedule"`。
 *   - @ref manualElements      —— 人工调度（船员制定）
 *   - @ref calculatedElements  —— 计算调度（软件优化）
 *   - @ref recommendedElements —— VTS 推荐调度（岸端优化）
 */
class RouteSchedule {
public:
    int id = 0;                                   ///< 调度唯一标识
    s421::optional<std::string> name;             ///< 调度名称

    std::vector<RouteScheduleElement> manualElements;       ///< 人工调度元素
    std::vector<RouteScheduleElement> calculatedElements;   ///< 计算调度元素
    std::vector<RouteScheduleElement> recommendedElements;  ///< 推荐调度元素
};

/**
 * @class Route
 * @brief 航线顶层容器。
 *
 * 对应 XSD: `complexType name="Route"`。
 */
class Route {
public:
    // ---- 必填字段 ----
    std::string formatVersion = "2.0";    ///< S-421 格式版本（Edition 2.0）
    std::string id;                       ///< 航线唯一标识（如 `"RTE123"`）
    int editionNo = 1;                    ///< 版本号（每次修改递增）

    // ---- 子对象（Route 通过 unique_ptr 独占所有权）----
    std::unique_ptr<RouteInfo> info;                              ///< 航线元数据
    std::vector<std::unique_ptr<RouteWaypoint>> waypoints;        ///< 航路点列表
    std::vector<std::unique_ptr<RouteActionPoint>> actionPoints;  ///< 动作点列表
    std::vector<std::unique_ptr<RouteSchedule>> schedules;        ///< 调度列表

    // ---- 厂商扩展 ----
    std::vector<RouteExtension> extensions;  ///< 厂商扩展列表

    // ---- 空间范围（GML boundedBy，可选）----
    // 记录航线所有航路点的边界范围（西南角 + 东北角）
    s421::optional<Position> lowerCorner;  ///< 边界西南角（最小纬度、最小经度）
    s421::optional<Position> upperCorner;  ///< 边界东北角（最大纬度、最大经度）
};

/**
 * @class Dataset
 * @brief S-421 XML 文档的根元素。
 *
 * 对应 XSD: `complexType name="DatasetType"`。
 */
class Dataset {
public:
    std::string gmlId;                       ///< GML 标识（如 `"S421.BALTIC"`）
    std::unique_ptr<Route> route;            ///< 航线数据（核心内容）
};

} // namespace s421
