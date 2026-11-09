#pragma once

/**
 * @file S421Parser.h
 * @brief S-421 XML 文件解析器。
 */

#include "S421DataModel.h"
#include <string>
#include <memory>
#include <unordered_map>

/// 前向声明 pugixml 类型，避免在头文件中包含 pugixml.hpp
namespace pugi {
    class xml_document;
    class xml_node;
}

namespace s421 {

/**
 * @class S421Parser
 * @brief 解析 S-421 XML 文件。
 *
 * 支持三种输入格式：
 *   1. `.s421`  —— 直接解析 XML
 *   2. `.s421p` —— 先尝试 ZIP 解压，再解析 XML；失败则直接当 XML 解析
 *   3. 交换集   —— 先解析 `CATALOG.001.XML`，再定位 `.s421` 文件
 */
class S421Parser {
public:
    S421Parser();
    ~S421Parser();

    /**
     * @brief 解析 .s421 / .gml 文件。
     * @param filePath 文件路径。
     * @return 解析成功返回 Dataset 对象（调用方获得所有权），失败返回 `nullptr`。
     */
    std::unique_ptr<Dataset> parseFile(const std::string& filePath);

    /**
     * @brief 解析 .s421p 压缩容器文件。
     * @param filePath 文件路径。
     * @return 解析成功返回 Dataset 对象（调用方获得所有权），失败返回 `nullptr`。
     */
    std::unique_ptr<Dataset> parseCompressedFile(const std::string& filePath);

    /**
     * @brief 解析 S-100 兼容交换集（目录结构）。
     *
     * 规格书 §3.1.1 第三种格式：读取 `CATALOG.001.XML` 定位 `.s421` 文件。
     *
     * @param exchangeSetDir 交换集目录路径（包含 `CATALOG.001.XML`）。
     * @return 解析成功返回 Dataset 对象（调用方获得所有权），失败返回 `nullptr`。
     */
    std::unique_ptr<Dataset> parseExchangeSet(const std::string& exchangeSetDir);

    /**
     * @brief 解析 XML 字符串（用于测试或网络接收）。
     * @param xmlContent XML 文本内容。
     * @return 解析成功返回 Dataset 对象（调用方获得所有权），失败返回 `nullptr`。
     */
    std::unique_ptr<Dataset> parseString(const std::string& xmlContent);

    /**
     * @brief 获取最后一次错误信息。
     * @return 最近一次操作的错误描述；无错误时为空字符串。
     */
    const std::string& getLastError() const { return lastError_; }

private:
    std::string lastError_;   ///< 最近一次操作的错误信息

    // ---- 内部解析方法 ----

    /**
     * @brief 从 pugixml 文档对象构建 Dataset。
     * @param doc 已加载的 XML 文档。
     * @return 构建成功返回 Dataset；失败返回 `nullptr`。
     */
    std::unique_ptr<Dataset> buildFromDoc(pugi::xml_document& doc);

    /**
     * @brief 解析 Route 元素。
     * @param routeNode `Route` 节点。
     * @return 解析结果。
     */
    std::unique_ptr<Route> parseRoute(pugi::xml_node routeNode);

    /**
     * @brief 解析 RouteInfo 元素。
     * @param infoNode `RouteInfo` 节点。
     * @return 解析结果。
     */
    RouteInfo parseRouteInfo(pugi::xml_node infoNode);

    /**
     * @brief 解析 RouteWaypoint 元素。
     * @param wpNode `RouteWaypoint` 节点。
     * @return 解析结果。
     */
    RouteWaypoint parseWaypoint(pugi::xml_node wpNode);

    /**
     * @brief 解析 RouteWaypointLeg 元素。
     * @param legNode `RouteWaypointLeg` 节点。
     * @return 解析结果。
     */
    RouteWaypointLeg parseLeg(pugi::xml_node legNode);

    /**
     * @brief 解析 RouteActionPoint 元素。
     * @param apNode `RouteActionPoint` 节点。
     * @return 解析结果。
     */
    RouteActionPoint parseActionPoint(pugi::xml_node apNode);

    /**
     * @brief 解析 RouteSchedule 及其子元素。
     * @param schedNode `RouteSchedule` 节点。
     * @return 解析结果。
     */
    RouteSchedule parseSchedule(pugi::xml_node schedNode);

    /**
     * @brief 解析 RouteScheduleElement 元素。
     * @param elemNode `RouteScheduleElement` 节点。
     * @return 解析结果。
     */
    RouteScheduleElement parseScheduleElement(pugi::xml_node elemNode);

    /**
     * @brief 解析 RouteExtensions 元素。
     * @param extNode `routeExtensions` 节点。
     * @return 解析结果。
     */
    RouteExtensions parseExtension(pugi::xml_node extNode);

    // ---- 辅助方法 ----

    /**
     * @brief 解析几何坐标（Point → GM_Point）。
     * @param geometryNode `geometry` 节点。
     * @return 解析出的坐标；无法解析时返回原点。
     */
    GM_Point parsePointGeometry(pugi::xml_node geometryNode);

    /**
     * @brief 解析 `gml:pos` 字符串 `"lat lon"` → GM_Point。
     * @param posStr 坐标文本，格式为 `"<纬度> <经度>"`。
     * @return 解析出的坐标。
     */
    GM_Point parseGmlPos(const std::string& posStr);

    /**
     * @brief 安全获取子节点文本（不存在则返回 optional 的空值）。
     * @param parent    父节点。
     * @param childName 子元素名。
     * @return 子节点文本；节点缺失或文本为空时返回空 optional。
     */
    s421::optional<std::string> getOptText(pugi::xml_node parent, const char* childName);

    /**
     * @brief 安全获取子节点数值（double）。
     * @param parent    父节点。
     * @param childName 子元素名。
     * @return 子节点数值；节点缺失时返回空 optional。
     */
    s421::optional<double> getOptDouble(pugi::xml_node parent, const char* childName);

    /**
     * @brief 安全获取子节点数值（int）。
     * @param parent    父节点。
     * @param childName 子元素名。
     * @return 子节点数值；节点缺失时返回空 optional。
     */
    s421::optional<int> getOptInt(pugi::xml_node parent, const char* childName);

    /**
     * @brief 安全获取子节点布尔值（兼容 `"true"`/`"false"` 与 `"1"`/`"0"`）。
     * @param parent    父节点。
     * @param childName 子元素名。
     * @return 子节点布尔值；节点缺失或文本无法识别时返回空 optional。
     */
    s421::optional<bool> getOptBool(pugi::xml_node parent, const char* childName);

    /**
     * @brief 通过 `xlink:href="#GML.ID"` 在文档中查找对应节点。
     * @param docNode 文档根节点的引用。
     * @param href    `xlink:href` 的值，如 `"#RTE.INFO"`。
     * @return 找到的节点（找不到则返回空节点）。
     */
    pugi::xml_node resolveRef(pugi::xml_node docNode, const char* href);
};

} // namespace s421
