#pragma once

/**
 * @file S421FieldSpec.h
 * @brief 字段映射表框架（库内私有，不出公共 API 头）。
 *
 * 设计对应 GMLAS 的核心理念：**schema 驱动一切，读写共享同一份映射**。
 *   - 表数据（每类一张 FieldSpec 数组，元素顺序 = XSD xs:sequence 顺序）
 *     由 tools/fieldspecGenerate.py 从 dataset/S-421_schema_20260604.xsd 生成，
 *     产物为 S421FieldTable.h（勿手改）。
 *   - 本头只提供通用机制：FieldSpec 描述、成员指针类型擦除访问、
 *     readFields / writeFields 两个编解码函数。Parser 与 Serializer 共用。
 *
 * 与既有序列化约定的对应关系（见旧 writeOpt 系列 / writeTextIfNonEmpty 系列）：
 *   - 按值 std::string：required(XSD minOccurs>=1) 恒写；否则空串=未设置不写
 *   - 按值 double/int ：required 恒写；否则 0=未设置不写
 *   - 按值 bool       ：required 恒写；否则 false=未设置不写
 *   - optional<T>     ：has_value 才写
 *   - 枚举（按值）     ：恒写 toString 文本；解析 fromString 失败保持默认（现行宽容行为）
 *   - Extensions      ：wrapper 元素（或 Route 级直接子元素）+ 内层 routeExtensions
 *
 * 不进字段表的结构性内容（几何、gml:ReferenceType 引用、容器组合关系）
 * 仍由 Parser/Serializer 的手写代码处理——它们不是"字段"，而是对象图结构。
 */

#include <s421/S421DataModel.h>
#include <pugixml.hpp>

#include <cstddef>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>
#include <type_traits>

namespace s421 {
namespace fieldspec {

/**
 * @enum FieldType
 * @brief 字段类别：决定成员指针的实型与读/写分派。
 */
enum class FieldType {
    String,       ///< 按值 std::string
    OptString,    ///< optional<std::string>
    Double,       ///< 按值 double
    OptDouble,    ///< optional<double>
    Int,          ///< 按值 int
    OptInt,       ///< optional<int>
    Bool,         ///< 按值 bool
    OptBool,      ///< optional<bool>
    EnumStatus,   ///< RouteInfoStatus（按值，XSD routeInfoStatusType）
    EnumGeometry, ///< LegGeometryType（按值，XSD routeWaypointLegGeometryTypeType）
    EnumAction,   ///< ActionType（按值，XSD routeActionPointRequiredActionType）
    OptEnumShip,  ///< optional<TypeOfShip>（XSD typeOfShipType）
    Extensions,   ///< optional<RouteExtensions>
};

/**
 * @struct FieldSpec
 * @brief 一个"XML 简单字段 ↔ 模型成员"的映射行。
 */
struct FieldSpec {
    const char* xmlName;   ///< XSD/实例元素名（生成器已对照 XSD 校验）
    FieldType   type;      ///< 字段类别
    bool        required;  ///< XSD minOccurs >= 1
    int         decimals;  ///< Double/OptDouble 输出小数位（1 为默认；turnRadius=6）

    /**
     * 成员地址访问器（类型擦除）。
     * @param obj  对象地址。
     * @param spec 本行地址——访问器从这里取回成员指针本体。
     * @return 成员地址（按行内 type 分派后转型读写）。
     */
    void* (*member)(void* obj, const void* spec);

    /**
     * 成员指针的原始存储（T C::* 平凡可拷贝，按字节 memcpy 存取）。
     * 单继承模型类的成员指针在常见平台 ≤ 16 字节；超限时 field() 静态断言。
     */
    unsigned char memberBuf[16];

    const char* extWrapper; ///< Extensions 行专用：包装元素名；nullptr = 直接子元素
};

/**
 * @brief 构造一行 FieldSpec（供生成的表使用）。
 *
 * @tparam C 模型类（RouteInfo 等）。
 * @tparam T 成员类型（含 optional<...>）。
 * @param xmlName    XSD 元素名。
 * @param t          字段类别。
 * @param required   XSD minOccurs>=1。
 * @param member     成员指针。
 * @param decimals   double 输出小数位（默认 1）。
 * @param extWrapper Extensions 包装元素名（默认 nullptr = 直接子元素）。
 */
template <class C, typename T>
inline FieldSpec field(const char* xmlName, FieldType t, bool required, T C::*member,
                       int decimals = 1, const char* extWrapper = nullptr) {
    static_assert(std::is_trivially_copyable<T C::*>::value,
                  "member pointer must be trivially copyable");
    static_assert(sizeof(T C::*) <= sizeof(FieldSpec::memberBuf),
                  "member pointer larger than FieldSpec::memberBuf");

    FieldSpec f;
    f.xmlName = xmlName;
    f.type = t;
    f.required = required;
    f.decimals = decimals;
    f.extWrapper = extWrapper;

    std::memcpy(f.memberBuf, &member, sizeof(member));
    // 捕获less lambda：只依赖参数（obj / spec），可转换为函数指针；
    // 成员指针本体从 spec 的 memberBuf 取回。
    f.member = [](void* obj, const void* spec) -> void* {
        T C::* m;
        std::memcpy(&m, static_cast<const FieldSpec*>(spec)->memberBuf, sizeof(m));
        return &(static_cast<C*>(obj)->*m);
    };
    return f;
}

// ============================================================
//  Extensions 节点的共享解析 / 写出
// ============================================================

/**
 * @brief 解析一个 `<routeExtensions .../>` 节点。
 *
 * 与旧 S421Parser::parseExtension 行为一致，仅一处归一增强：
 * 属性缺失时 name/version 置为 nullopt（旧实现会写入空串 optional）。
 */
inline RouteExtensions parseRouteExtensionsNode(pugi::xml_node extNode) {
    RouteExtensions ext;
    ext.manufacturerId = extNode.attribute("routeExtensionsManufacturerId").value();

    const char* name = extNode.attribute("routeExtensionsName").value();
    if (name && name[0]) ext.name = std::string(name);

    const char* version = extNode.attribute("routeExtensionsVersion").value();
    if (version && version[0]) ext.version = std::string(version);

    // note 可有多个，换行合并进 content（与旧实现一致）
    std::string content;
    for (pugi::xml_node note : extNode.children("routeExtensionsNote")) {
        if (!content.empty()) content += "\n";
        content += note.text().get();
    }
    if (!content.empty()) ext.content = content;
    return ext;
}

/**
 * @brief 写出一个扩展块：wrapper（可选）+ 内层 `<routeExtensions gml:id=...>`。
 * @param wrapperName 包装元素名；nullptr 表示 extNode 直接挂在 parent 上（Route 级）。
 * @param extId       内层 routeExtensions 的 gml:id（调用方按 "XXX.EXT.1" 约定生成）。
 */
inline void writeRouteExtensionsNode(pugi::xml_node parent, const char* wrapperName,
                                     const RouteExtensions& ext, const std::string& extId) {
    pugi::xml_node holder = wrapperName ? parent.append_child(wrapperName) : parent;
    pugi::xml_node extNode = holder.append_child("routeExtensions");
    extNode.append_attribute("gml:id") = extId.c_str();
    extNode.append_attribute("routeExtensionsManufacturerId") = ext.manufacturerId.c_str();
    if (ext.name.has_value()) {
        extNode.append_attribute("routeExtensionsName") = ext.name.value().c_str();
    }
    if (ext.version.has_value()) {
        extNode.append_attribute("routeExtensionsVersion") = ext.version.value().c_str();
    }
    if (ext.content.has_value()) {
        extNode.append_child("routeExtensionsNote").text().set(ext.content.value().c_str());
    }
}

// ============================================================
//  读侧小工具（镜像旧 getOpt* 语义）
// ============================================================
inline void readOptText(pugi::xml_node node, const char* name, optional<std::string>& out) {
    pugi::xml_node c = node.child(name);
    if (c.empty()) return;
    const char* v = c.text().get();
    if (!v || v[0] == '\0') return;
    out = std::string(v);
}

inline void readOptInt(pugi::xml_node node, const char* name, optional<int>& out) {
    pugi::xml_node c = node.child(name);
    if (!c.empty()) out = c.text().as_int();
}

inline void readOptDouble(pugi::xml_node node, const char* name, optional<double>& out) {
    pugi::xml_node c = node.child(name);
    if (!c.empty()) out = c.text().as_double();
}

inline void readOptBool(pugi::xml_node node, const char* name, optional<bool>& out) {
    pugi::xml_node c = node.child(name);
    if (c.empty()) return;
    std::string v = c.text().get();
    if (v == "true" || v == "1") out = true;
    else if (v == "false" || v == "0") out = false;
}

template <typename E>
inline void readEnum(pugi::xml_node node, const char* name, E& out) {
    E v;
    // fromString 失败时保持模型默认值——现行解析器的宽容行为（严格化属后续校验任务）
    if (fromString(std::string(node.child(name).text().get()), v)) out = v;
}

template <typename E>
inline void readOptEnum(pugi::xml_node node, const char* name, optional<E>& out) {
    E v;
    if (fromString(std::string(node.child(name).text().get()), v)) out = v;
}

// ============================================================
//  写侧小工具（镜像旧 writeOpt* / writeXxxIfYyy 语义）
// ============================================================
inline void writeText(pugi::xml_node parent, const char* name, const std::string& v) {
    parent.append_child(name).text().set(v.c_str());
}

inline std::string formatDouble(double v, int decimals) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(decimals) << v;
    return oss.str();
}

inline void writeDouble(pugi::xml_node parent, const char* name, double v, int decimals) {
    writeText(parent, name, formatDouble(v, decimals));
}

// ============================================================
//  Extensions 字段读取（wrapper 型 / Route 级直挂型）
// ============================================================
inline void readExtensionsField(pugi::xml_node node, const FieldSpec& f,
                                optional<RouteExtensions>& out) {
    if (f.extWrapper == nullptr) {
        // Route 级：routeExtensions 为直接子元素
        for (pugi::xml_node e : node.children(f.xmlName)) {
            out = parseRouteExtensionsNode(e);
        }
    } else {
        // 包装型：wrapper > routeExtensions
        for (pugi::xml_node w : node.children(f.extWrapper)) {
            for (pugi::xml_node e : w.children("routeExtensions")) {
                out = parseRouteExtensionsNode(e);
            }
        }
    }
}

// ============================================================
//  核心 codec：按表读 / 按表写
// ============================================================

/**
 * @brief 按表读取：把 node 下的简单字段读入 obj（obj 由调用方默认构造）。
 *
 * 元素缺失时字段保持模型默认值；枚举文本非法时同样保持默认（宽容模式）。
 */
inline void readFields(pugi::xml_node node, void* obj,
                       const FieldSpec* specs, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        const FieldSpec& f = specs[i];
        void* m = f.member(obj, &f);

        switch (f.type) {
        case FieldType::String:
            *static_cast<std::string*>(m) = node.child(f.xmlName).text().get();
            break;
        case FieldType::OptString:
            readOptText(node, f.xmlName, *static_cast<optional<std::string>*>(m));
            break;
        case FieldType::Double:
            *static_cast<double*>(m) = node.child(f.xmlName).text().as_double();
            break;
        case FieldType::OptDouble:
            readOptDouble(node, f.xmlName, *static_cast<optional<double>*>(m));
            break;
        case FieldType::Int:
            *static_cast<int*>(m) = node.child(f.xmlName).text().as_int();
            break;
        case FieldType::OptInt:
            readOptInt(node, f.xmlName, *static_cast<optional<int>*>(m));
            break;
        case FieldType::Bool: {
            pugi::xml_node c = node.child(f.xmlName);
            if (!c.empty()) {
                std::string v = c.text().get();
                if (v == "true" || v == "1") *static_cast<bool*>(m) = true;
                else if (v == "false" || v == "0") *static_cast<bool*>(m) = false;
            }
            break;
        }
        case FieldType::OptBool:
            readOptBool(node, f.xmlName, *static_cast<optional<bool>*>(m));
            break;
        case FieldType::EnumStatus:
            readEnum(node, f.xmlName, *static_cast<RouteInfoStatus*>(m));
            break;
        case FieldType::EnumGeometry:
            readEnum(node, f.xmlName, *static_cast<LegGeometryType*>(m));
            break;
        case FieldType::EnumAction:
            readEnum(node, f.xmlName, *static_cast<ActionType*>(m));
            break;
        case FieldType::OptEnumShip:
            readOptEnum(node, f.xmlName, *static_cast<optional<TypeOfShip>*>(m));
            break;
        case FieldType::Extensions:
            readExtensionsField(node, f, *static_cast<optional<RouteExtensions>*>(m));
            break;
        }
    }
}

/**
 * @brief 按表写：把 obj 的字段按 XSD 顺序写入 parent。
 * @param idBase 扩展块 gml:id 前缀（生成 "idBase.EXT.1"），与旧 IdMap 约定一致。
 */
inline void writeFields(pugi::xml_node parent, const void* obj,
                        const FieldSpec* specs, size_t count,
                        const std::string& idBase) {
    for (size_t i = 0; i < count; ++i) {
        const FieldSpec& f = specs[i];
        void* m = f.member(const_cast<void*>(obj), &f);

        switch (f.type) {
        case FieldType::String: {
            const std::string& v = *static_cast<const std::string*>(m);
            if (f.required || !v.empty()) writeText(parent, f.xmlName, v);
            break;
        }
        case FieldType::OptString: {
            const optional<std::string>& v = *static_cast<const optional<std::string>*>(m);
            if (v.has_value()) writeText(parent, f.xmlName, v.value());
            break;
        }
        case FieldType::Double: {
            const double v = *static_cast<const double*>(m);
            if (f.required || v != 0.0) writeDouble(parent, f.xmlName, v, f.decimals);
            break;
        }
        case FieldType::OptDouble: {
            const optional<double>& v = *static_cast<const optional<double>*>(m);
            if (v.has_value()) writeDouble(parent, f.xmlName, v.value(), f.decimals);
            break;
        }
        case FieldType::Int: {
            const int v = *static_cast<const int*>(m);
            if (f.required || v != 0) writeText(parent, f.xmlName, std::to_string(v));
            break;
        }
        case FieldType::OptInt: {
            const optional<int>& v = *static_cast<const optional<int>*>(m);
            if (v.has_value()) writeText(parent, f.xmlName, std::to_string(v.value()));
            break;
        }
        case FieldType::Bool: {
            const bool v = *static_cast<const bool*>(m);
            if (f.required || v) writeText(parent, f.xmlName, v ? "true" : "false");
            break;
        }
        case FieldType::OptBool: {
            const optional<bool>& v = *static_cast<const optional<bool>*>(m);
            if (v.has_value()) writeText(parent, f.xmlName, v.value() ? "true" : "false");
            break;
        }
        case FieldType::EnumStatus:
            writeText(parent, f.xmlName, toString(*static_cast<const RouteInfoStatus*>(m)));
            break;
        case FieldType::EnumGeometry:
            writeText(parent, f.xmlName, toString(*static_cast<const LegGeometryType*>(m)));
            break;
        case FieldType::EnumAction:
            writeText(parent, f.xmlName, toString(*static_cast<const ActionType*>(m)));
            break;
        case FieldType::OptEnumShip: {
            const optional<TypeOfShip>& v = *static_cast<const optional<TypeOfShip>*>(m);
            if (v.has_value()) writeText(parent, f.xmlName, toString(v.value()));
            break;
        }
        case FieldType::Extensions: {
            const optional<RouteExtensions>& v = *static_cast<const optional<RouteExtensions>*>(m);
            if (v.has_value()) {
                writeRouteExtensionsNode(parent, f.extWrapper, v.value(), idBase + ".EXT.1");
            }
            break;
        }
        }
    }
}

} // namespace fieldspec
} // namespace s421
