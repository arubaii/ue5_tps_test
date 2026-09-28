// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * The Widget Data Binder's log category, shared by the binder and its event proxies so "Log LogWidgetDataBinder
 * Verbose" turns both on. Declared once here and defined once, in WidgetDataBinderComponent.cpp: a static category
 * in each .cpp is two definitions of one type as soon as a unity build puts the two files together.
 */
DECLARE_LOG_CATEGORY_EXTERN(LogWidgetDataBinder, Log, All);