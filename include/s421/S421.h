#pragma once

/**
 * @file S421.h
 * @brief S-421 SDK 聚合头。
 *
 * 调用方只需 `#include <s421/S421.h>` 即可拿到全部公共 API，
 * 不必逐个 include 各模块头。
 */

#include "S421Export.h"

#include "S421Types.h"
#include "S421DataModel.h"

#include "S421Parser.h"
#include "S421Serializer.h"
#include "S421Validator.h"
#include "S421Formatter.h"

#include "S421Logger.h"
