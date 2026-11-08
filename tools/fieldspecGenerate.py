#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
fieldspecGenerate.py - 从 S-421 XSD 生成/校验字段映射表（GMLAS 式 "schema 为事实源" 的构建期实现）。

用法（在仓库根目录执行）：
    python3 tools/fieldspecGenerate.py            # 重新生成 src/S421FieldTable.h
    python3 tools/fieldspecGenerate.py --check    # 校验已生成文件是否与 XSD 同步（CI 用，不同步则退出码 1）

做什么：
    1. 解析 dataset/S-421_schema_20260604.xsd；
    2. 把本仓库维护的「元素 ↔ 模型成员」映射（FIELD_MAP）与 XSD 逐元素交叉校验：
       - 映射里的元素在 XSD 中缺失            → 硬错误（改名/删除未同步）
       - XSD 简单字段不在映射里               → 硬错误（新增字段未映射，强迫显式决策）
       - XSD 类型与映射的类别不符             → 硬错误
       - XSD minOccurs 与映射的 required 不符 → 硬错误
       - gml:ReferenceType / geometry 等结构性元素 → 跳过并在生成头注释中登记
    3. 生成 src/S421FieldTable.h（含 per 类 FieldSpec 数组，顺序 = XSD sequence 顺序）。

设计意图：XSD 是唯一事实源。标准升级（如 2.0 → 2.1）时，改 XSD 后跑本脚本，
凡漂移处直接报错，错误在"生成期"暴露，而不是像 2.0 升级那样在四处手写代码间
静默失配、直到编译/联调才被发现。
"""

import sys
import xml.etree.ElementTree as ET

XSD_PATH = "dataset/S-421_schema_20260604.xsd"
OUT_PATH = "src/S421FieldTable.h"

XS_NS = "http://www.w3.org/2001/XMLSchema"
GML_NS = "http://www.opengis.net/gml/3.2"

# XSD simpleType / 内建类型 → 允许的 FieldType 标签
TYPE_ALLOWED = {
    "xs:string": {"String", "OptString"},
    "xs:double": {"Double", "Double6", "OptDouble"},
    "xs:integer": {"Int", "OptInt"},
    "xs:boolean": {"Bool", "OptBool"},
    # 别名：模型以字符串承载，输出原样文本（往返按文本比较）
    "xs:dateTime": {"String", "OptString"},
    "xs:date": {"String", "OptString"},
    "xs:anyURI": {"String", "OptString"},
    "routeInfoStatusType": {"EnumStatus"},
    "routeWaypointLegGeometryTypeType": {"EnumGeometry"},
    "routeActionPointRequiredActionType": {"EnumAction"},
    "typeOfShipType": {"OptEnumShip"},
    "gml:ReferenceType": set(),          # 结构性引用：跳过
}

# 映射标签 → 生成代码里的 FieldType 枚举名
TAG_TO_ENUM = {
    "String": "FieldType::String",
    "OptString": "FieldType::OptString",
    "Double": "FieldType::Double",
    "Double6": "FieldType::Double",
    "OptDouble": "FieldType::OptDouble",
    "Int": "FieldType::Int",
    "OptInt": "FieldType::OptInt",
    "Bool": "FieldType::Bool",
    "OptBool": "FieldType::OptBool",
    "EnumStatus": "FieldType::EnumStatus",
    "EnumGeometry": "FieldType::EnumGeometry",
    "EnumAction": "FieldType::EnumAction",
    "OptEnumShip": "FieldType::OptEnumShip",
    "ExtensionsWrapped": "FieldType::Extensions",
    "ExtensionsDirect": "FieldType::Extensions",
}
# Double6：6 位小数（航向半径 0.00-5.00 海里需要两位以上精度）
TAG_DECIMALS = {"Double": "1", "Double6": "6", "OptDouble": "1"}

# ---------------------------------------------------------------------------
#  映射表（人工维护的唯一手工输入；一切以 XSD 校验为准）
#  条目：(XSD 元素名, 模型成员名, 映射标签)
# ---------------------------------------------------------------------------
FIELD_MAP = {
    "RouteType": [
        ("routeFormatVersion", "formatVersion", "String"),
        ("routeID", "id", "String"),
        ("routeEditionNo", "editionNo", "Int"),
        ("routeExtensions", "extensions", "ExtensionsDirect"),
    ],
    "RouteInfoType": [
        ("routeInfoName", "name", "String"),
        ("routeInfoAuthor", "author", "String"),
        ("routeInfoEditionTime", "editionTime", "String"),
        ("routeInfoDescription", "description", "String"),
        ("routeInfoStatus", "status", "EnumStatus"),
        ("routeInfoMasterApproval", "masterApproval", "OptBool"),
        ("routeInfoValidityStart", "validityStart", "String"),
        ("routeInfoValidityEnd", "validityEnd", "String"),
        ("routeInfoDeparturePortID1", "departurePortID1", "OptString"),
        ("routeInfoDeparturePortID2", "departurePortID2", "OptString"),
        ("routeInfoDeparturePortCall", "departurePortCall", "OptString"),
        ("routeInfoArrivalPortID1", "arrivalPortID1", "OptString"),
        ("routeInfoArrivalPortID2", "arrivalPortID2", "OptString"),
        ("routeInfoArrivalPortCall", "arrivalPortCall", "OptString"),
        ("routeInfoReferencePrevRoute", "referencePrevRoute", "OptString"),
        ("routeInfoReferenceNextRoute", "referenceNextRoute", "OptString"),
        ("typeOfShip", "vesselType", "OptEnumShip"),
        ("routeInfoVesselName", "vesselName", "OptString"),
        ("routeInfoVesselMMSI", "vesselMMSI", "String"),
        ("routeInfoVesselCallsign", "vesselCallsign", "OptString"),
        ("routeInfoVesselIMO", "vesselIMO", "OptString"),
        ("routeInfoVesselVoyage", "vesselVoyage", "OptString"),
        ("routeInfoVesselHeight", "vesselHeight", "OptDouble"),
        ("routeInfoVesselLength", "vesselLength", "OptDouble"),
        ("routeInfoVesselBeam", "vesselBeam", "OptDouble"),
        ("routeInfoDraftMax", "draftMax", "OptDouble"),
        ("routeInfoAirDraftMax", "airDraftMax", "OptDouble"),
        ("routeInfoBeamMax", "beamMax", "OptDouble"),
        ("routeInfoLengthMax", "lengthMax", "OptDouble"),
        ("routeInfoExtensions", "extensions", "ExtensionsWrapped"),
    ],
    "RouteWaypointType": [
        ("routeWaypointID", "id", "Int"),
        ("routeWaypointName", "name", "String"),
        ("routeWaypointExternalReferenceID", "externalRefID", "OptString"),
        ("routeWaypointFixed", "fixed", "Bool"),
        ("routeWaypointTurnRadius", "turnRadius", "Double6"),
        ("routeWaypointExtensions", "extensions", "ExtensionsWrapped"),
    ],
    "RouteWaypointLegType": [
        ("routeWaypointLegAirDraftMax", "airDraftMax", "OptDouble"),
        ("routeWaypointLegBeamMax", "beamMax", "OptDouble"),
        ("routeWaypointLegDraft", "draft", "OptDouble"),
        ("routeWaypointLegDraftAft", "draftAft", "OptDouble"),
        ("routeWaypointLegDraftForward", "draftForward", "OptDouble"),
        ("routeWaypointLegDraftMax", "draftMax", "OptDouble"),
        ("routeWaypointLegDynamicUKC", "dynamicUKC", "OptDouble"),
        ("routeWaypointLegGeometryType", "geometryType", "EnumGeometry"),
        ("routeWaypointLegIssue", "issue", "OptString"),
        ("routeWaypointLegLengthMax", "lengthMax", "OptDouble"),
        ("routeWaypointLegNote", "note", "OptString"),
        ("routeWaypointLegPortCL", "portCL", "OptInt"),
        ("routeWaypointLegPortXTDL", "portXTDL", "OptInt"),
        ("routeWaypointLegSafetyContour", "safetyContour", "OptDouble"),
        ("routeWaypointLegSafetyDepth", "safetyDepth", "OptDouble"),
        ("routeWaypointLegSafetyMargin", "safetyMargin", "OptDouble"),
        ("routeWaypointLegSOGMax", "sogMax", "OptDouble"),
        ("routeWaypointLegSOGMin", "sogMin", "OptDouble"),
        ("routeWaypointLegStarboardCL", "starboardCL", "OptInt"),
        ("routeWaypointLegStarboardXTDL", "starboardXTDL", "OptInt"),
        ("routeWaypointLegStaticUKC", "staticUKC", "OptDouble"),
        ("routeWaypointLegSTWMax", "stwMax", "OptDouble"),
        ("routeWaypointLegSTWMin", "stwMin", "OptDouble"),
        ("routeWaypointLegExtensions", "extensions", "ExtensionsWrapped"),
    ],
    "RouteActionPointType": [
        ("routeActionPointID", "id", "Int"),
        ("routeActionPointName", "name", "OptString"),
        ("routeActionPointExternalReference", "externalRef", "OptString"),
        ("routeActionPointRadius", "radius", "Double"),
        ("routeActionPointTimetoAct", "timeToAct", "Double"),
        ("routeActionPointRequiredAction", "requiredAction", "EnumAction"),
        ("routeActionPointRequiredActionDescription", "actionDescription", "String"),
        ("routeActionPointExtensions", "extensions", "ExtensionsWrapped"),
    ],
    "RouteScheduleType": [
        ("routeScheduleID", "id", "Int"),
        ("routeScheduleName", "name", "OptString"),
        ("routeScheduleExtensions", "extensions", "ExtensionsWrapped"),
    ],
    "RouteScheduleElementType": [
        ("routeScheduleElementExtensions", "extensions", "ExtensionsWrapped"),
        ("routeWaypointID", "waypointId", "Int"),
        ("routeScheduleElementPlanSOG", "planSOG", "Double"),
        ("routeScheduleElementETD", "etd", "String"),
        ("routeScheduleElementETA", "eta", "String"),
        ("routeScheduleElementETDWindowBefore", "etdWindowBefore", "OptInt"),
        ("routeScheduleElementETDWindowAfter", "etdWindowAfter", "OptInt"),
        ("routeScheduleElementETAWindowBefore", "etaWindowBefore", "OptInt"),
        ("routeScheduleElementETAWindowAfter", "etaWindowAfter", "OptInt"),
        ("routeScheduleElementNote", "note", "OptString"),
    ],
    "RouteScheduleManualType": [
        ("routeScheduleManualExtensions", "extensions", "ExtensionsWrapped"),
    ],
    "RouteScheduleCalculatedType": [
        ("routeScheduleCalculatedExtensions", "extensions", "ExtensionsWrapped"),
    ],
    "RouteScheduleRecommendedType": [
        ("routeScheduleRecommendedExtensions", "extensions", "ExtensionsWrapped"),
    ],
}

# complexType → 生成代码里的类名 + 数组名
CT_MAP = {
    "RouteType": ("s421::Route", "kRouteFields"),
    "RouteInfoType": ("s421::RouteInfo", "kRouteInfoFields"),
    "RouteWaypointType": ("s421::RouteWaypoint", "kRouteWaypointFields"),
    "RouteWaypointLegType": ("s421::RouteWaypointLeg", "kRouteWaypointLegFields"),
    "RouteActionPointType": ("s421::RouteActionPoint", "kRouteActionPointFields"),
    "RouteScheduleType": ("s421::RouteSchedule", "kRouteScheduleFields"),
    "RouteScheduleElementType": ("s421::RouteScheduleElement", "kRouteScheduleElementFields"),
    "RouteScheduleManualType": ("s421::RouteScheduleManual", "kRouteScheduleManualFields"),
    "RouteScheduleCalculatedType": ("s421::RouteScheduleCalculated", "kRouteScheduleCalculatedFields"),
    "RouteScheduleRecommendedType": ("s421::RouteScheduleRecommended", "kRouteScheduleRecommendedFields"),
}


def is_extensions_type(xsd_type: str) -> bool:
    return xsd_type in ("routeExtensionsType",) or xsd_type.endswith("ExtensionsType")


def parse_xsd():
    """返回 {complexType名: [ (元素名, XSD类型, required) 按声明顺序 ]}"""
    tree = ET.parse(XSD_PATH)
    root = tree.getroot()
    result = {}
    for ct in root.findall(f"{{{XS_NS}}}complexType"):
        ct_name = ct.get("name")
        if ct_name not in CT_MAP:
            continue
        seq = ct.find(f"{{{XS_NS}}}complexContent/{{{XS_NS}}}extension/{{{XS_NS}}}sequence")
        if seq is None:
            seq = ct.find(f"{{{XS_NS}}}sequence")
        elements = []
        if seq is not None:
            for el in seq.findall(f"{{{XS_NS}}}element"):
                elements.append((el.get("name"), el.get("type"), el.get("minOccurs", "1")))
        result[ct_name] = elements
    return result


def validate(xsd):
    """交叉校验，返回 (错误列表, 结构性跳过登记)"""
    errors = []
    structural = {}  # complexType -> [结构性元素描述]
    for ct_name, mapped in FIELD_MAP.items():
        if ct_name not in xsd:
            errors.append(f"XSD 中找不到 complexType {ct_name}（改名或删除？）")
            continue

        mapped_by_name = {m[0]: m for m in mapped}
        seen = set()
        struct_list = []

        for el_name, el_type, min_occurs in xsd[ct_name]:
            # 结构性元素：引用 / 几何
            if el_type == f"{{{GML_NS}}}ReferenceType" or el_type == "gml:ReferenceType":
                struct_list.append(f"{el_name} (gml:ReferenceType)")
                continue
            if el_name == "geometry":
                struct_list.append(f"{el_name} (S100 几何属性)")
                continue
            if el_type in (None, ""):
                # 本地 anonymous 类型（本 schema 不用于简单字段）
                struct_list.append(f"{el_name} (anonymous type)")
                continue

            if el_name not in mapped_by_name:
                errors.append(
                    f"{ct_name}: XSD 元素 <{el_name}> (type={el_type}, minOccurs={min_occurs}) "
                    f"不在 FIELD_MAP 中 —— schema 新增字段需显式映射或登记为结构性")
                continue

            _, member, tag = mapped_by_name[el_name]
            seen.add(el_name)

            required = min_occurs != "0"

            # required 一致性（extensions 恒可选；标签不区分 required 语义）
            if tag.startswith("Extensions"):
                if required:
                    errors.append(f"{ct_name}: <{el_name}> XSD 要求 minOccurs>=1，extensions 应为可选")
            # 类型一致性
            allowed = TYPE_ALLOWED.get(el_type, set())
            if tag.startswith("Extensions"):
                if not is_extensions_type(el_type):
                    errors.append(f"{ct_name}: <{el_name}> 映射为 extensions，但 XSD 类型是 {el_type}")
                if (el_type == "routeExtensionsType"
                        and el_name.endswith("Extensions")
                        and el_name != "routeExtensions"):
                    # 包装型元素名却直接引用 routeExtensionsType（Route 级直挂型属正常）
                    struct_list.append(
                        f"注: <{el_name}> 在 XSD 中直接引用 routeExtensionsType"
                        f"（其余包装元素均为各自 *ExtensionsType）——XSD 瑕疵，运行时保持嵌套形状")
            elif tag not in allowed:
                errors.append(
                    f"{ct_name}: <{el_name}> XSD 类型 {el_type} 与映射标签 {tag} 不符"
                    f"（允许: {sorted(allowed) or '无'}）")

            # plain 字段 required 由类型标签的 Opt/非Opt 决定，与 minOccurs 对照
            plain_optional_tags = {"OptString", "OptDouble", "OptInt", "OptBool", "OptEnumShip"}
            member_is_opt = tag in plain_optional_tags
            if not tag.startswith("Extensions"):
                if required and member_is_opt:
                    errors.append(f"{ct_name}: <{el_name}> XSD minOccurs>=1 但映射为可选成员（Opt*）")
                # 非 required 的 plain 字段：模型按值或 optional 均可（写侧约定兜底），不报错

        for el_name in mapped_by_name:
            if el_name not in seen:
                errors.append(f"{ct_name}: 映射元素 <{el_name}> 在 XSD sequence 中不存在（改名/删除？）")

        structural[ct_name] = struct_list

    return errors, structural


def generate(xsd, structural):
    lines = []
    w = lines.append
    w("#pragma once")
    w("//")
    w("// S421FieldTable.h —— 由 tools/fieldspecGenerate.py 生成，勿手改。")
    w("// 事实源: dataset/S-421_schema_20260604.xsd")
    w("// 重新生成: python3 tools/fieldspecGenerate.py")
    w("// 同步校验: python3 tools/fieldspecGenerate.py --check")
    w("//")
    w("// 每张表的行顺序 = XSD xs:sequence 声明顺序（序列化按表序输出即满足 schema 顺序）。")
    w("// 结构性子元素（gml:ReferenceType 引用 / S100 几何 / 容器组合）不进字段表，")
    w("// 由 Parser/Serializer 的对象图代码处理。")
    w("")
    w('#include "S421FieldSpec.h"')
    w("")
    w("namespace s421 {")
    w("namespace fieldspec {")
    w("")
    for ct_name, elements in xsd.items():
        if ct_name not in FIELD_MAP:
            continue
        cpp_class, array_name = CT_MAP[ct_name]
        mapped = {m[0]: m for m in FIELD_MAP[ct_name]}
        w(f"// ---- {ct_name} -> {cpp_class} ----")
        for note in structural.get(ct_name, []):
            w(f"// 结构性: {note}")
        w(f"static const FieldSpec {array_name}[] = {{")
        for el_name, el_type, min_occurs in elements:
            if el_name not in mapped:
                continue  # 结构性
            _, member, tag = mapped[el_name]
            enum_tag = TAG_TO_ENUM[tag]
            required = "true" if (min_occurs != "0" and not tag.startswith("Extensions")) else "false"
            if tag == "ExtensionsWrapped":
                w(f'    field("{el_name}", {enum_tag}, false, &{cpp_class}::{member}, 1, "{el_name}"),')
            elif tag == "ExtensionsDirect":
                w(f'    field("{el_name}", {enum_tag}, false, &{cpp_class}::{member}),')
            elif tag in TAG_DECIMALS:
                w(f'    field("{el_name}", {enum_tag}, {required}, &{cpp_class}::{member}, {TAG_DECIMALS[tag]}),')
            else:
                w(f'    field("{el_name}", {enum_tag}, {required}, &{cpp_class}::{member}),')
        w("};")
        w(f"static const size_t {array_name}Count =")
        w(f"    sizeof({array_name}) / sizeof({array_name}[0]);")
        w("")
    w("} // namespace fieldspec")
    w("} // namespace s421")
    return "\n".join(lines) + "\n"


def main():
    check_only = "--check" in sys.argv
    xsd = parse_xsd()
    errors, structural = validate(xsd)

    if errors:
        print("XSD 与 FIELD_MAP 漂移，生成终止：", file=sys.stderr)
        for e in errors:
            print(f"  - {e}", file=sys.stderr)
        sys.exit(1)

    content = generate(xsd, structural)

    if check_only:
        try:
            with open(OUT_PATH, "r", encoding="utf-8") as f:
                current = f.read()
        except FileNotFoundError:
            print(f"--check 失败：{OUT_PATH} 不存在，请先运行生成", file=sys.stderr)
            sys.exit(1)
        if current == content:
            print(f"OK: {OUT_PATH} 与 XSD 同步")
        else:
            print(f"DRIFT: {OUT_PATH} 与 XSD 不同步，请重新运行 python3 tools/fieldspecGenerate.py",
                  file=sys.stderr)
            sys.exit(1)
    else:
        with open(OUT_PATH, "w", encoding="utf-8") as f:
            f.write(content)
        n_rows = sum(len(v) for v in FIELD_MAP.values())
        print(f"已生成 {OUT_PATH}（{len(FIELD_MAP)} 张表 / {n_rows} 行，全部经 XSD 交叉校验）")


if __name__ == "__main__":
    main()
