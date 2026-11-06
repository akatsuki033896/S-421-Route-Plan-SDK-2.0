#pragma once

/**
 * @file S421Serializer.h
 * @brief S-421 数据序列化器（Dataset → XML）。
 *
 * 与 S421Parser 互为镜像：
 *   - Parser：`.s421` XML 文件 → Dataset 对象
 *   - Serializer：Dataset 对象 → `.s421` XML 文件
 */

#include "S421DataModel.h"
#include <string>
#include <map>

/// 前向声明 pugixml 类型
namespace pugi {
    class xml_document;
    class xml_node;
}

namespace s421 {

/**
 * @class S421Serializer
 * @brief 将 Dataset 序列化为 S-421 XML。
 *
 * 核心流程：
 *   1. 构建 IdMap（为所有对象分配 `gml:id`）
 *   2. 按标准顺序输出 `member`/`imember`
 *   3. optional 字段只在有值时输出
 *
 * 设计原则：
 *   - 不修改 Dataset 对象（`const` 引用传入）
 *   - `gml:id` 动态生成，从对象的 `id` 字段推导
 *   - 与 Parser 的命名保持一致
 */
class S421Serializer {
public:
    S421Serializer();
    ~S421Serializer();

    // ---- 公共 API ----

    /**
     * @brief 序列化 Dataset 到 `.s421` XML 文件。
     * @param dataset  要序列化的数据集（只读）。
     * @param filePath 输出文件路径。
     * @return `true` = 成功；`false` = 失败（调用 getLastError() 获取原因）。
     */
    bool serializeToFile(const Dataset& dataset, const std::string& filePath);

    /**
     * @brief 序列化 Dataset 到 XML 字符串。
     * @param dataset 要序列化的数据集（只读）。
     * @return XML 字符串；失败时返回空字符串（检查 getLastError()）。
     */
    std::string serializeToString(const Dataset& dataset);

    /**
     * @brief 序列化为 `.s421p` 压缩容器（ZIP 包含 `.s421`）。
     *
     * 规格书 §3.1.2：强制输出格式。
     *
     * @param dataset  数据集。
     * @param filePath `.s421p` 输出路径。
     * @param compress `true` = ZIP 压缩；`false` = 纯 XML 兼容（默认 `true`）。
     * @return `true` = 成功；`false` = 失败（调用 getLastError() 获取原因）。
     */
    bool serializeToCompressed(const Dataset& dataset,
                               const std::string& filePath,
                               bool compress = true);

    /**
     * @brief 序列化为 S-100 兼容交换集（目录结构）。
     *
     * 规格书 §3.1.1 第三种格式。生成文件：
     * @verbatim
       outputDir/CATALOG.001.XML  — 交换目录
       outputDir/MD_xxx.s421.xml  — ISO 19115 元数据
       outputDir/xxx.s421         — 航线数据
       outputDir/support/         — 辅助目录
       @endverbatim
     *
     * @param dataset        数据集。
     * @param outputDir      输出目录。
     * @param producingAgency 生产机构名称（默认 `"Ship"`）。
     * @param agencyContact  机构联系方式（默认空）。
     * @return `true` = 成功；`false` = 失败（调用 getLastError() 获取原因）。
     */
    bool serializeToExchangeSet(const Dataset& dataset,
                                const std::string& outputDir,
                                const std::string& producingAgency = "Ship",
                                const std::string& agencyContact = "");

    /**
     * @brief 获取最后一次错误信息。
     * @return 最近一次操作的错误描述；无错误时为空字符串。
     */
    const std::string& getLastError() const { return lastError_; }

    // ---- 配置选项 ----

    /**
     * @brief 设置坐标输出精度（小数位数）。
     * @param digits 小数位数（默认 6）。
     */
    void setCoordinatePrecision(int digits);

private:
    std::string lastError_;          ///< 最近一次操作的错误信息
    int coordPrecision_ = 6;         ///< 坐标输出小数位数

    /**
     * @struct IdMap
     * @brief 为所有对象分配 `gml:id`。
     *
     * S-421 XML 中每个对象都需要 `gml:id` 属性，
     * 容器通过 `xlink:href="#ID"` 引用它们。
     * IdMap 在序列化时动态生成，不改数据模型。
     */
    struct IdMap {
        // 固定 ID（容器/单例对象）
        std::string routeId;             ///< `"RTE"`
        std::string infoId;              ///< `"RTE.INFO"`
        std::string wptsContainerId;     ///< `"RTE.WPTS"`
        std::string aptsContainerId;     ///< `"RTE.APTS"`
        std::string schedsContainerId;   ///< `"RTE.SCHEDS"`

        /// 按对象 id 索引的 ID 映射（key = 对象 id，value = `gml:id` 字符串）
        std::map<int, std::string> waypointIds;    ///< key=wp.id, `"RTE.WPT.{id}"`
        std::map<int, std::string> legIds;         ///< key=顺序号, `"RTE.WPT.LEG.{n}"`
        std::map<int, std::string> actionPointIds; ///< key=ap.id, `"RTE.APT.{id}"`
        std::map<int, std::string> scheduleIds;    ///< key=sched.id, `"RTE.SCHED.{id}"`

        /// Schedule 子类别 ID（从 schedule id 推导），value 形如 `"RTE.SCHED.{id}.MAN"`
        std::map<int, std::string> schedManualIds;  ///< 人工调度容器 ID
        std::map<int, std::string> schedCalcIds;    ///< 计算调度容器 ID
        std::map<int, std::string> schedRecIds;     ///< 推荐调度容器 ID

        /// Schedule Element ID
        /// key=`"MANUAL.1.1"`（类别.调度id.顺序号），value=`"RTE.SCHED.1.MAN.ELEMENT.1"`
        std::map<std::string, std::string> elementIds;
    };

    /**
     * @brief 从 Dataset 构建 ID 映射。
     * @param dataset 数据集。
     * @return 构建好的 IdMap。
     */
    IdMap buildIdMap(const Dataset& dataset) const;

    // ---- XML 文档构建 ----

    /**
     * @brief 构建完整的 XML 文档。
     * @param doc     输出参数，构建结果写入此文档。
     * @param dataset 数据集。
     * @return `true` = 成功；`false` = 失败（调用 getLastError() 获取原因）。
     */
    bool buildDocument(pugi::xml_document& doc, const Dataset& dataset);

    // ---- 元素写入方法 ----

    /**
     * @brief 写入根元素 `Dataset`（含命名空间声明）。
     * @param root    根节点。
     * @param dataset 数据集。
     * @param ids     ID 映射。
     */
    void writeDatasetRoot(pugi::xml_node& root, const Dataset& dataset, const IdMap& ids);

    /**
     * @brief 写入 `gml:boundedBy` 空间范围。
     * @param parent 父节点。
     * @param route  航线数据。
     */
    void writeBoundedBy(pugi::xml_node& parent, const Route& route);

    /**
     * @brief 写入 `Route` 元素。
     * @param parent 父节点（`members`）。
     * @param route  航线数据。
     * @param ids    ID 映射。
     */
    void writeRoute(pugi::xml_node& parent, const Route& route, const IdMap& ids);

    /**
     * @brief 写入 `RouteInfo` 元素。
     * @param parent 父节点（`members`）。
     * @param info   航线元数据。
     * @param ids    ID 映射。
     */
    void writeRouteInfo(pugi::xml_node& parent, const RouteInfo& info, const IdMap& ids);

    /**
     * @brief 写入 `RouteWaypoints` 容器。
     * @param parent 父节点（`members`）。
     * @param route  航线数据。
     * @param ids    ID 映射。
     */
    void writeWaypointsContainer(pugi::xml_node& parent, const Route& route, const IdMap& ids);

    /**
     * @brief 写入单个 `RouteWaypoint`。
     * @param parent  父节点（`members`）。
     * @param wp      航路点。
     * @param nextWp  下一个航路点（用于推导航段，可为 `nullptr`）。
     * @param wpIndex 当前航段序号。
     * @param ids     ID 映射。
     */
    void writeWaypoint(pugi::xml_node& parent, const RouteWaypoint& wp,
                       const RouteWaypoint* nextWp, int wpIndex, const IdMap& ids);

    /**
     * @brief 写入单个 `RouteWaypointLeg`。
     * @param parent    父节点（`members`）。
     * @param leg       航段数据。
     * @param startPos  航段起点坐标。
     * @param endPos    航段终点坐标；为空表示该 Leg 无后继航点（无法推导终点），此时不输出 geometry。
     * @param legIndex  航段序号。
     * @param ids       ID 映射。
     */
    void writeLeg(pugi::xml_node& parent, const RouteWaypointLeg& leg,
                  const GM_Point& startPos, const s421::optional<GM_Point>& endPos,
                  int legIndex, const IdMap& ids);

    /**
     * @brief 写入 `RouteActionPoints` 容器。
     * @param parent 父节点（`members`）。
     * @param route  航线数据。
     * @param ids    ID 映射。
     */
    void writeActionPointsContainer(pugi::xml_node& parent, const Route& route, const IdMap& ids);

    /**
     * @brief 写入单个 `RouteActionPoint`。
     * @param parent 父节点（`members`）。
     * @param ap     动作点。
     * @param ids    ID 映射。
     */
    void writeActionPoint(pugi::xml_node& parent, const RouteActionPoint& ap, const IdMap& ids);

    /**
     * @brief 写入 `RouteSchedules` 容器。
     * @param parent 父节点（`members`）。
     * @param route  航线数据。
     * @param ids    ID 映射。
     */
    void writeSchedulesContainer(pugi::xml_node& parent, const Route& route, const IdMap& ids);

    /**
     * @brief 写入单个 `RouteSchedule`。
     * @param parent 父节点（`members`）。
     * @param sched  调度数据。
     * @param ids    ID 映射。
     */
    void writeSchedule(pugi::xml_node& parent, const RouteSchedule& sched, const IdMap& ids);

    /**
     * @brief 写入单个 `RouteScheduleElement`。
     * @param parent             父节点（`members`）。
     * @param elem               调度元素。
     * @param elemId             该元素的 `gml:id`。
     * @param collectionId       所属集合容器的 `gml:id`。
     * @param collectionNodeName 回指集合的子元素名（如 `"routeScheduleElementManualCollection"`）。
     * @param ids                ID 映射。
     */
    void writeScheduleElement(pugi::xml_node& parent, const RouteScheduleElement& elem,
                              const std::string& elemId, const std::string& collectionId,
                              const char* collectionNodeName, const IdMap& ids);

    // ---- 几何写入 ----

    /**
     * @brief 写入点几何（`S100:pointProperty`）。
     * @param parent     父节点。
     * @param pos        坐标。
     * @param pointGmlId 生成的点对象 `gml:id`。
     */
    void writePointGeometry(pugi::xml_node& parent, const GM_Point& pos,
                            const std::string& pointGmlId);

    /**
     * @brief 写入曲线几何（`S100:curveProperty`，两点一线段）。
     * @param parent      父节点。
     * @param start       起点坐标。
     * @param end         终点坐标。
     * @param curveGmlId  生成的曲线对象 `gml:id`。
     */
    void writeCurveGeometry(pugi::xml_node& parent, const GM_Point& start,
                            const GM_Point& end, const std::string& curveGmlId);

    // ---- 厂商扩展写入 ----

    /**
     * @brief 写入厂商扩展块。
     * @param parent      父节点。
     * @param wrapperName 包装元素名（如 `"routeExtensions"`）。
     * @param extensions  扩展列表。
     * @param baseId      生成 `gml:id` 所用的基础 ID。
     */
    void writeExtensions(pugi::xml_node& parent, const char* wrapperName,
                         const std::vector<RouteExtensions>& extensions,
                         const std::string& baseId);

    // ---- 辅助方法 ----

    /**
     * @brief 格式化坐标为 `"lat lon"` 字符串。
     * @param pos 坐标。
     * @return 形如 `"22.075835 118.594834"` 的字符串（精度由 setCoordinatePrecision 决定）。
     */
    std::string formatPos(const GM_Point& pos) const;

    /**
     * @brief 写入 xlink 引用元素。
     *
     * 输出形如 `<routeInfo xlink:href="#RTE.INFO" xlink:title="routeInfoComGM_Point"/>`。
     * 其中 `xlink:title` 由「父元素 + 子元素」查表（`xlinkTitleFor`）推导。
     *
     * @param parent    父节点。
     * @param childName 引用子元素名。
     * @param targetId  目标对象的 `gml:id`。
     * @param roleUri   角色 URI；已不再写出，保留占位以免改动调用点。
     */
    void writeRef(pugi::xml_node& parent, const char* childName,
                  const std::string& targetId, const char* roleUri);

    /**
     * @brief 写入可选子元素（只在有值时写入）。
     * @param parent 父节点。
     * @param name   子元素名。
     * @param value  值；为空时完全不输出该元素。
     */
    void writeOptText(pugi::xml_node& parent, const char* name,
                      const s421::optional<std::string>& value);

    /// @copydoc writeOptText(pugi::xml_node&, const char*, const s421::optional<std::string>&)
    void writeOptDouble(pugi::xml_node& parent, const char* name,
                        const s421::optional<double>& value);

    /// @copydoc writeOptText(pugi::xml_node&, const char*, const s421::optional<std::string>&)
    void writeOptInt(pugi::xml_node& parent, const char* name,
                     const s421::optional<int>& value);

    /// @copydoc writeOptText(pugi::xml_node&, const char*, const s421::optional<std::string>&)
    void writeOptBool(pugi::xml_node& parent, const char* name,
                      const s421::optional<bool>& value);
};

} // namespace s421
