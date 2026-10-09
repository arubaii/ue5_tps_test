// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * The Niagara Particles widget's log category, shared by its files. Declared once here and defined once, in
 * NameTBDNiagaraModule.cpp: a static category in each .cpp is two definitions of one type as soon as a unity build
 * puts the two files together.
 */
DECLARE_LOG_CATEGORY_EXTERN(LogNiagaraUI, Log, All);
