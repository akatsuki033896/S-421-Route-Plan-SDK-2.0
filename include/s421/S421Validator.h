#pragma once

/**
 * @file S421Validator.h
 * @brief S-421 数据校验引擎。
 *
 * 对应船端技术规格书 §3.3.2 四层校验要求：
 *   1. 格式校验：XML 结构符合 S-421 Schema
 *   2. 属性校验：强制属性不缺失，值域合规
 *   3. 逻辑校验：时间顺序、航点数量、ID 唯一性
 *   4. 错误反馈：标准化提示（错误类型 + 影响对象 + 修复建议）
 */

#include "S421DataModel.h"
#include <string>
#include <vector>
#include <ostream>

namespace s421 {

/**
 * @struct ValidationError
 * @brief 校验结果 — 一条错误或警告。
 */
struct ValidationError {
    /**
     * @enum Level
     * @brief 错误级别。
     */
    enum class Level {
        Error,      ///< 错误：必须修复
        Warning     ///< 警告：建议修复
    };

    Level level;                ///< 错误级别
    std::string category;       ///< 校验类别：format/attribute/logic
    std::string objectId;       ///< 影响对象（如 `"RouteInfo"`、`"Waypoint#3"`）
    std::string field;          ///< 字段名（如 `"routeInfoVesselMMSI"`）
    std::string message;        ///< 错误描述 + 修复建议

    /**
     * @brief 返回可读的错误字符串。
     * @return 形如 `"❌ [错误] [attribute] RouteInfo.field: 描述"` 的字符串。
     */
    std::string toString() const {
        std::string prefix = (level == Level::Error) ? "❌ [错误]" : "⚠️ [警告]";
        return prefix + " [" + category + "] " + objectId + "." + field + ": " + message;
    }
};

/**
 * @struct ValidationResult
 * @brief 校验结果集合。
 */
struct ValidationResult {
    std::vector<ValidationError> errors;    ///< 所有错误
    std::vector<ValidationError> warnings;  ///< 所有警告

    /**
     * @brief 是否通过校验（无错误）。
     * @return `true` = 无错误（可能仍有警告）。
     */
    bool ok() const { return errors.empty(); }

    /**
     * @brief 总错误数。
     * @return 错误条目数量。
     */
    size_t errorCount() const { return errors.size(); }

    /**
     * @brief 总警告数。
     * @return 警告条目数量。
     */
    size_t warningCount() const { return warnings.size(); }

    /**
     * @brief 将所有结果格式化输出到流。
     * @param os 输出流。
     */
    void format(std::ostream& os) const;
};

/**
 * @class S421Validator
 * @brief S-421 数据校验器。
 *
 * 用法：
 * @code
 * S421Validator validator;
 * ValidationResult result = validator.validate(dataset);
 * if (!result.ok()) { ... 处理错误 ... }
 * @endcode
 */
class S421Validator {
public:
    S421Validator() = default;

    /**
     * @brief 校验 Dataset（主入口）。
     * @param dataset 待校验的数据集。
     * @return 包含所有错误和警告的 ValidationResult。
     */
    ValidationResult validate(const Dataset& dataset);

private:
    // ---- 第 1 层：格式校验 ----

    /**
     * @brief 检查 XML 文档结构是否基本完整。
     * @param dataset 数据集。
     * @param result  输出参数，追加校验结果。
     */
    void checkStructure(const Dataset& dataset, ValidationResult& result);

    // ---- 第 2 层：属性校验 ----
    // 检查强制属性是否存在、值域是否合规

    /// @brief 检查 Route 层必填属性。
    void checkRouteAttributes(const Route& route, ValidationResult& result);
    /// @brief 检查 RouteInfo 层必填属性。
    void checkRouteInfoAttributes(const RouteInfo& info, ValidationResult& result);
    /// @brief 检查 RouteWaypoint 层必填属性。
    void checkWaypointAttributes(const RouteWaypoint& wp, ValidationResult& result);
    /// @brief 检查 RouteWaypointLeg 层必填属性。
    void checkLegAttributes(const RouteWaypointLeg& leg, ValidationResult& result);
    /// @brief 检查 RouteActionPoint 层必填属性。
    void checkActionPointAttributes(const RouteActionPoint& ap, ValidationResult& result);
    /// @brief 检查 RouteSchedule 层必填属性。
    void checkScheduleAttributes(const RouteSchedule& sched, ValidationResult& result);
    /// @brief 检查 RouteScheduleElement 层必填属性。
    void checkScheduleElementAttributes(const RouteScheduleElement& elem, ValidationResult& result);

    // ---- 第 3 层：逻辑校验 ----

    /**
     * @brief 检查数据之间的逻辑关系。
     * @param route  航线数据。
     * @param result 输出参数，追加校验结果。
     */
    void checkLogic(const Route& route, ValidationResult& result);

    // ---- 辅助方法 ----

    /**
     * @brief 添加一条错误。
     * @param result   输出参数。
     * @param category 校验类别。
     * @param objectId 影响对象。
     * @param field    字段名。
     * @param message  错误描述。
     */
    void addError(ValidationResult& result, const std::string& category,
                  const std::string& objectId, const std::string& field,
                  const std::string& message);

    /**
     * @brief 添加一条警告。
     * @param result   输出参数。
     * @param category 校验类别。
     * @param objectId 影响对象。
     * @param field    字段名。
     * @param message  警告描述。
     */
    void addWarning(ValidationResult& result, const std::string& category,
                    const std::string& objectId, const std::string& field,
                    const std::string& message);

    /**
     * @brief 检查字符串是否为空。
     * @param s 待检查字符串。
     * @return `true` = 空字符串。
     */
    static bool isEmpty(const std::string& s);

    /**
     * @brief 检查 MMSI 是否合法（9 位数字，XSD `MMSINumberText`）。
     * @param mmsi 待检查的 MMSI 字符串。
     * @return `true` = 格式合法。
     */
    static bool isValidMMSI(const std::string& mmsi);

    /**
     * @brief 检查 IMO 是否合法（7 位数字，XSD `IMONumberText`）。
     * @param imo 待检查的 IMO 字符串。
     * @return `true` = 格式合法。
     */
    static bool isValidIMO(const std::string& imo);

    /**
     * @brief 检查港口代码是否合法（5 位字母，XSD `PortIDType`）。
     * @param portId 待检查的港口代码。
     * @return `true` = 格式合法。
     */
    static bool isValidPortID(const std::string& portId);

    /**
     * @brief 检查 ISO 8601 时间格式（简单检查）。
     * @param time 待检查的时间字符串。
     * @return `true` = 格式合法。
     */
    static bool isValidTimeFormat(const std::string& time);
};

} // namespace s421
