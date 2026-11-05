#pragma once

/**
 * @file S421Formatter.h
 * @brief 可选的调试/诊断输出模块（人读格式打印 Dataset）。
 */

#include "S421DataModel.h"
#include <ostream>

namespace s421 {

/**
 * @struct FormatResult
 * @brief 格式化结果的摘要信息。
 */
struct FormatResult {
    int waypointCount = 0;       ///< 航路点总数
    int legCount = 0;            ///< 航段总数
    int actionPointCount = 0;    ///< 动作点总数
    int scheduleCount = 0;       ///< 调度总数
};

/**
 * @brief 将 Dataset 格式化输出到流。
 *
 * @param ds      要显示的数据集。
 * @param os      输出流（通常是 `std::cout`）。
 * @param verbose `true` = 显示详细信息（每个航点坐标、航段参数等）；
 *                `false` = 仅显示摘要（航点数、航段数等计数）。
 * @return FormatResult 摘要结构。
 */
FormatResult formatDataset(const Dataset& ds, std::ostream& os,
                           bool verbose = false);

/**
 * @brief 生成航线的一行摘要（适合日志记录）。
 * @param route 航线数据。
 * @return 形如 `"route=RTE123 edition=1 waypoints=5 status=Planned"` 的字符串。
 */
std::string summarizeRoute(const Route& route);

} // namespace s421
