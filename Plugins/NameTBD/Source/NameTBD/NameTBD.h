// Copyright AR Game Systems 2026. All rights reserved.

#pragma once

#include "Modules/ModuleManager.h"

class FNameTBDModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};