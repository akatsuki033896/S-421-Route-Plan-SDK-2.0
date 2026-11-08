#pragma once
//
// S421FieldTable.h —— 由 tools/fieldspecGenerate.py 生成，勿手改。
// 事实源: dataset/S-421_schema_20260604.xsd
// 重新生成: python3 tools/fieldspecGenerate.py
// 同步校验: python3 tools/fieldspecGenerate.py --check
//
// 每张表的行顺序 = XSD xs:sequence 声明顺序（序列化按表序输出即满足 schema 顺序）。
// 结构性子元素（gml:ReferenceType 引用 / S100 几何 / 容器组合）不进字段表，
// 由 Parser/Serializer 的对象图代码处理。

#include "S421FieldSpec.h"

namespace s421 {
namespace fieldspec {

// ---- RouteInfoType -> s421::RouteInfo ----
// 结构性: 注: <routeInfoExtensions> 在 XSD 中直接引用 routeExtensionsType（其余包装元素均为各自 *ExtensionsType）——XSD 瑕疵，运行时保持嵌套形状
static const FieldSpec kRouteInfoFields[] = {
    field("routeInfoName", FieldType::String, true, &s421::RouteInfo::name),
    field("routeInfoAuthor", FieldType::String, false, &s421::RouteInfo::author),
    field("routeInfoEditionTime", FieldType::String, false, &s421::RouteInfo::editionTime),
    field("routeInfoDescription", FieldType::String, false, &s421::RouteInfo::description),
    field("routeInfoStatus", FieldType::EnumStatus, true, &s421::RouteInfo::status),
    field("routeInfoMasterApproval", FieldType::OptBool, false, &s421::RouteInfo::masterApproval),
    field("routeInfoValidityStart", FieldType::String, false, &s421::RouteInfo::validityStart),
    field("routeInfoValidityEnd", FieldType::String, false, &s421::RouteInfo::validityEnd),
    field("routeInfoDeparturePortID1", FieldType::OptString, false, &s421::RouteInfo::departurePortID1),
    field("routeInfoDeparturePortID2", FieldType::OptString, false, &s421::RouteInfo::departurePortID2),
    field("routeInfoDeparturePortCall", FieldType::OptString, false, &s421::RouteInfo::departurePortCall),
    field("routeInfoArrivalPortID1", FieldType::OptString, false, &s421::RouteInfo::arrivalPortID1),
    field("routeInfoArrivalPortID2", FieldType::OptString, false, &s421::RouteInfo::arrivalPortID2),
    field("routeInfoArrivalPortCall", FieldType::OptString, false, &s421::RouteInfo::arrivalPortCall),
    field("routeInfoReferencePrevRoute", FieldType::OptString, false, &s421::RouteInfo::referencePrevRoute),
    field("routeInfoReferenceNextRoute", FieldType::OptString, false, &s421::RouteInfo::referenceNextRoute),
    field("typeOfShip", FieldType::OptEnumShip, false, &s421::RouteInfo::vesselType),
    field("routeInfoVesselName", FieldType::OptString, false, &s421::RouteInfo::vesselName),
    field("routeInfoVesselMMSI", FieldType::String, false, &s421::RouteInfo::vesselMMSI),
    field("routeInfoVesselCallsign", FieldType::OptString, false, &s421::RouteInfo::vesselCallsign),
    field("routeInfoVesselIMO", FieldType::OptString, false, &s421::RouteInfo::vesselIMO),
    field("routeInfoVesselVoyage", FieldType::OptString, false, &s421::RouteInfo::vesselVoyage),
    field("routeInfoVesselHeight", FieldType::OptDouble, false, &s421::RouteInfo::vesselHeight, 1),
    field("routeInfoVesselLength", FieldType::OptDouble, false, &s421::RouteInfo::vesselLength, 1),
    field("routeInfoVesselBeam", FieldType::OptDouble, false, &s421::RouteInfo::vesselBeam, 1),
    field("routeInfoDraftMax", FieldType::OptDouble, false, &s421::RouteInfo::draftMax, 1),
    field("routeInfoAirDraftMax", FieldType::OptDouble, false, &s421::RouteInfo::airDraftMax, 1),
    field("routeInfoBeamMax", FieldType::OptDouble, false, &s421::RouteInfo::beamMax, 1),
    field("routeInfoLengthMax", FieldType::OptDouble, false, &s421::RouteInfo::lengthMax, 1),
    field("routeInfoExtensions", FieldType::Extensions, false, &s421::RouteInfo::extensions, 1, "routeInfoExtensions"),
};
static const size_t kRouteInfoFieldsCount =
    sizeof(kRouteInfoFields) / sizeof(kRouteInfoFields[0]);

// ---- RouteScheduleType -> s421::RouteSchedule ----
// 结构性: routeScheduleCollection (gml:ReferenceType)
// 结构性: routeScheduleManual (gml:ReferenceType)
// 结构性: routeScheduleCalculated (gml:ReferenceType)
// 结构性: routeScheduleRecommended (gml:ReferenceType)
static const FieldSpec kRouteScheduleFields[] = {
    field("routeScheduleID", FieldType::Int, true, &s421::RouteSchedule::id),
    field("routeScheduleName", FieldType::OptString, false, &s421::RouteSchedule::name),
    field("routeScheduleExtensions", FieldType::Extensions, false, &s421::RouteSchedule::extensions, 1, "routeScheduleExtensions"),
};
static const size_t kRouteScheduleFieldsCount =
    sizeof(kRouteScheduleFields) / sizeof(kRouteScheduleFields[0]);

// ---- RouteScheduleManualType -> s421::RouteScheduleManual ----
// 结构性: routeScheduleCollection (gml:ReferenceType)
// 结构性: routeScheduleElement (gml:ReferenceType)
static const FieldSpec kRouteScheduleManualFields[] = {
    field("routeScheduleManualExtensions", FieldType::Extensions, false, &s421::RouteScheduleManual::extensions, 1, "routeScheduleManualExtensions"),
};
static const size_t kRouteScheduleManualFieldsCount =
    sizeof(kRouteScheduleManualFields) / sizeof(kRouteScheduleManualFields[0]);

// ---- RouteScheduleCalculatedType -> s421::RouteScheduleCalculated ----
// 结构性: routeScheduleCollection (gml:ReferenceType)
// 结构性: routeScheduleElement (gml:ReferenceType)
static const FieldSpec kRouteScheduleCalculatedFields[] = {
    field("routeScheduleCalculatedExtensions", FieldType::Extensions, false, &s421::RouteScheduleCalculated::extensions, 1, "routeScheduleCalculatedExtensions"),
};
static const size_t kRouteScheduleCalculatedFieldsCount =
    sizeof(kRouteScheduleCalculatedFields) / sizeof(kRouteScheduleCalculatedFields[0]);

// ---- RouteScheduleRecommendedType -> s421::RouteScheduleRecommended ----
// 结构性: routeScheduleCollection (gml:ReferenceType)
// 结构性: routeScheduleElement (gml:ReferenceType)
static const FieldSpec kRouteScheduleRecommendedFields[] = {
    field("routeScheduleRecommendedExtensions", FieldType::Extensions, false, &s421::RouteScheduleRecommended::extensions, 1, "routeScheduleRecommendedExtensions"),
};
static const size_t kRouteScheduleRecommendedFieldsCount =
    sizeof(kRouteScheduleRecommendedFields) / sizeof(kRouteScheduleRecommendedFields[0]);

// ---- RouteScheduleElementType -> s421::RouteScheduleElement ----
// 结构性: routeScheduleElementManualCollection (gml:ReferenceType)
// 结构性: routeScheduleElementCalculatedCollection (gml:ReferenceType)
// 结构性: routeScheduleElementRecommendedCollection (gml:ReferenceType)
static const FieldSpec kRouteScheduleElementFields[] = {
    field("routeScheduleElementExtensions", FieldType::Extensions, false, &s421::RouteScheduleElement::extensions, 1, "routeScheduleElementExtensions"),
    field("routeWaypointID", FieldType::Int, true, &s421::RouteScheduleElement::waypointId),
    field("routeScheduleElementPlanSOG", FieldType::Double, false, &s421::RouteScheduleElement::planSOG, 1),
    field("routeScheduleElementETD", FieldType::String, false, &s421::RouteScheduleElement::etd),
    field("routeScheduleElementETA", FieldType::String, false, &s421::RouteScheduleElement::eta),
    field("routeScheduleElementETDWindowBefore", FieldType::OptInt, false, &s421::RouteScheduleElement::etdWindowBefore),
    field("routeScheduleElementETDWindowAfter", FieldType::OptInt, false, &s421::RouteScheduleElement::etdWindowAfter),
    field("routeScheduleElementETAWindowBefore", FieldType::OptInt, false, &s421::RouteScheduleElement::etaWindowBefore),
    field("routeScheduleElementETAWindowAfter", FieldType::OptInt, false, &s421::RouteScheduleElement::etaWindowAfter),
    field("routeScheduleElementNote", FieldType::OptString, false, &s421::RouteScheduleElement::note),
};
static const size_t kRouteScheduleElementFieldsCount =
    sizeof(kRouteScheduleElementFields) / sizeof(kRouteScheduleElementFields[0]);

// ---- RouteType -> s421::Route ----
// 结构性: routeInfo (gml:ReferenceType)
// 结构性: routeSchedules (gml:ReferenceType)
// 结构性: routeWaypoints (gml:ReferenceType)
// 结构性: routeActionPoints (gml:ReferenceType)
static const FieldSpec kRouteFields[] = {
    field("routeFormatVersion", FieldType::String, true, &s421::Route::formatVersion),
    field("routeID", FieldType::String, true, &s421::Route::id),
    field("routeEditionNo", FieldType::Int, true, &s421::Route::editionNo),
    field("routeExtensions", FieldType::Extensions, false, &s421::Route::extensions),
};
static const size_t kRouteFieldsCount =
    sizeof(kRouteFields) / sizeof(kRouteFields[0]);

// ---- RouteWaypointType -> s421::RouteWaypoint ----
// 结构性: targetWaypoint (gml:ReferenceType)
// 结构性: routeWaypointCollection (gml:ReferenceType)
// 结构性: routeWaypointLeg (gml:ReferenceType)
// 结构性: geometry (S100 几何属性)
static const FieldSpec kRouteWaypointFields[] = {
    field("routeWaypointID", FieldType::Int, false, &s421::RouteWaypoint::id),
    field("routeWaypointName", FieldType::String, false, &s421::RouteWaypoint::name),
    field("routeWaypointExternalReferenceID", FieldType::OptString, false, &s421::RouteWaypoint::externalRefID),
    field("routeWaypointFixed", FieldType::Bool, false, &s421::RouteWaypoint::fixed),
    field("routeWaypointTurnRadius", FieldType::Double, true, &s421::RouteWaypoint::turnRadius, 6),
    field("routeWaypointExtensions", FieldType::Extensions, false, &s421::RouteWaypoint::extensions, 1, "routeWaypointExtensions"),
};
static const size_t kRouteWaypointFieldsCount =
    sizeof(kRouteWaypointFields) / sizeof(kRouteWaypointFields[0]);

// ---- RouteWaypointLegType -> s421::RouteWaypointLeg ----
// 结构性: routeWaypointLegCollection (gml:ReferenceType)
// 结构性: geometry (S100 几何属性)
static const FieldSpec kRouteWaypointLegFields[] = {
    field("routeWaypointLegAirDraftMax", FieldType::OptDouble, false, &s421::RouteWaypointLeg::airDraftMax, 1),
    field("routeWaypointLegBeamMax", FieldType::OptDouble, false, &s421::RouteWaypointLeg::beamMax, 1),
    field("routeWaypointLegDraft", FieldType::OptDouble, false, &s421::RouteWaypointLeg::draft, 1),
    field("routeWaypointLegDraftAft", FieldType::OptDouble, false, &s421::RouteWaypointLeg::draftAft, 1),
    field("routeWaypointLegDraftForward", FieldType::OptDouble, false, &s421::RouteWaypointLeg::draftForward, 1),
    field("routeWaypointLegDraftMax", FieldType::OptDouble, false, &s421::RouteWaypointLeg::draftMax, 1),
    field("routeWaypointLegDynamicUKC", FieldType::OptDouble, false, &s421::RouteWaypointLeg::dynamicUKC, 1),
    field("routeWaypointLegGeometryType", FieldType::EnumGeometry, true, &s421::RouteWaypointLeg::geometryType),
    field("routeWaypointLegIssue", FieldType::OptString, false, &s421::RouteWaypointLeg::issue),
    field("routeWaypointLegLengthMax", FieldType::OptDouble, false, &s421::RouteWaypointLeg::lengthMax, 1),
    field("routeWaypointLegNote", FieldType::OptString, false, &s421::RouteWaypointLeg::note),
    field("routeWaypointLegPortCL", FieldType::OptInt, false, &s421::RouteWaypointLeg::portCL),
    field("routeWaypointLegPortXTDL", FieldType::OptInt, false, &s421::RouteWaypointLeg::portXTDL),
    field("routeWaypointLegSafetyContour", FieldType::OptDouble, false, &s421::RouteWaypointLeg::safetyContour, 1),
    field("routeWaypointLegSafetyDepth", FieldType::OptDouble, false, &s421::RouteWaypointLeg::safetyDepth, 1),
    field("routeWaypointLegSafetyMargin", FieldType::OptDouble, false, &s421::RouteWaypointLeg::safetyMargin, 1),
    field("routeWaypointLegSOGMax", FieldType::OptDouble, false, &s421::RouteWaypointLeg::sogMax, 1),
    field("routeWaypointLegSOGMin", FieldType::OptDouble, false, &s421::RouteWaypointLeg::sogMin, 1),
    field("routeWaypointLegStarboardCL", FieldType::OptInt, false, &s421::RouteWaypointLeg::starboardCL),
    field("routeWaypointLegStarboardXTDL", FieldType::OptInt, false, &s421::RouteWaypointLeg::starboardXTDL),
    field("routeWaypointLegStaticUKC", FieldType::OptDouble, false, &s421::RouteWaypointLeg::staticUKC, 1),
    field("routeWaypointLegSTWMax", FieldType::OptDouble, false, &s421::RouteWaypointLeg::stwMax, 1),
    field("routeWaypointLegSTWMin", FieldType::OptDouble, false, &s421::RouteWaypointLeg::stwMin, 1),
    field("routeWaypointLegExtensions", FieldType::Extensions, false, &s421::RouteWaypointLeg::extensions, 1, "routeWaypointLegExtensions"),
};
static const size_t kRouteWaypointLegFieldsCount =
    sizeof(kRouteWaypointLegFields) / sizeof(kRouteWaypointLegFields[0]);

// ---- RouteActionPointType -> s421::RouteActionPoint ----
// 结构性: routeActionPointCollection (gml:ReferenceType)
// 结构性: geometry (S100 几何属性)
static const FieldSpec kRouteActionPointFields[] = {
    field("routeActionPointID", FieldType::Int, false, &s421::RouteActionPoint::id),
    field("routeActionPointName", FieldType::OptString, false, &s421::RouteActionPoint::name),
    field("routeActionPointExternalReference", FieldType::OptString, false, &s421::RouteActionPoint::externalRef),
    field("routeActionPointRadius", FieldType::Double, false, &s421::RouteActionPoint::radius, 1),
    field("routeActionPointTimetoAct", FieldType::Double, false, &s421::RouteActionPoint::timeToAct, 1),
    field("routeActionPointRequiredAction", FieldType::EnumAction, true, &s421::RouteActionPoint::requiredAction),
    field("routeActionPointRequiredActionDescription", FieldType::String, true, &s421::RouteActionPoint::actionDescription),
    field("routeActionPointExtensions", FieldType::Extensions, false, &s421::RouteActionPoint::extensions, 1, "routeActionPointExtensions"),
};
static const size_t kRouteActionPointFieldsCount =
    sizeof(kRouteActionPointFields) / sizeof(kRouteActionPointFields[0]);

} // namespace fieldspec
} // namespace s421
