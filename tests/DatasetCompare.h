#pragma once

/**
 * @file DatasetCompare.h
 * @brief 测试专用：两个 Dataset 的深度逐字段比较（roundtrip 回环测试基建）。
 *
 * 用途：roundtrip 测试的核心是"比较两次解析得到的模型是否语义一致"：
 *   模型 A --serialize--> XML --parse--> 模型 B，要求 equal(A, B)。
 *
 * 设计约定（与序列化器的"按值可选字段"约定镜像，见 S421Serializer.cpp 顶部注释）：
 *   1. 浮点比较带容差 `eps`（默认 1e-6）：坐标序列化为 6 位小数、一般 double
 *      为 1 位小数，往返必然有精度损失，不能做位级比较。
 *   2. optional 字段要求两侧"有值性"一致且值相等（double 走容差）。
 *   3. RouteExtensions 的 name/version/content 按"空串 = 未设置"归一后比较——
 *      解析端 parseExtension 对缺失属性会写入空串 optional，归一才能与
 *      "从未设置"的模型侧对齐。
 *
 * @note 比较器是全保真的：连当前序列化器尚不回环的字段
 *       （RouteSchedule / RouteScheduleManual|Calculated|Recommended /
 *       RouteScheduleElement 的 extensions）也在比较范围内——一旦测试样本设置了
 *       这些字段，差异会立刻暴露，这是有意为之（测试基建应当暴露保真缺口）。
 */

#include <s421/S421DataModel.h>

#include <string>

namespace s421 {
namespace test {

/**
 * @struct DatasetDiff
 * @brief 比较结果：是否一致 + 首个差异的描述。
 */
struct DatasetDiff {
    bool equal = true;              ///< `true` = 两数据集语义一致
    std::string firstDifference;    ///< 首个差异的"路径: [A值] vs [B值]"；一致时为空

    /// @brief 隐式转 bool，方便 `if (!diff)` 写法。
    explicit operator bool() const { return equal; }
};

/**
 * @brief 深度比较两个 Dataset（逐类、逐字段）。
 *
 * @param a   比较基准（通常是回环前的模型）。
 * @param b   比较对象（通常是回环后重新解析的模型）。
 * @param eps 浮点容差；默认 1e-6 覆盖坐标 6 位小数的序列化精度损失。
 * @return DatasetDiff；发现首个差异即停（equal=false + 路径描述）。
 */
DatasetDiff compareDatasets(const Dataset& a, const Dataset& b, double eps = 1e-6);

} // namespace test
} // namespace s421
