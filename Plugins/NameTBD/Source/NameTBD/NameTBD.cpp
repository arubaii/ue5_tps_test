// Copyright AR Game Systems 2026. All rights reserved.

#include "NameTBD.h"

#include "WidgetAnimation/WidgetAnimEvaluator.h"
#include "WidgetAnimation/Evaluators/WidgetAnimCountUp.h"
#include "WidgetAnimation/Evaluators/WidgetAnimBlueprintType.h"

#define LOCTEXT_NAMESPACE "FNameTBDModule"

void FNameTBDModule::StartupModule()
{
	// The first-party registered track types. A game or licensee module registers its own the same way, from its
	// own StartupModule - nothing here needs to know about them.
	FWidgetAnimEvaluatorRegistry::Get().Register(MakeShared<FWidgetAnimCountUpEvaluator>());

	// The bridge every Blueprint track type runs through - see UWidgetAnimBlueprintType. Blueprint types themselves
	// need no registering: the Widget Animations panel finds them as assets.
	FWidgetAnimEvaluatorRegistry::Get().Register(MakeShared<FWidgetAnimBlueprintEvaluator>());
}

void FNameTBDModule::ShutdownModule()
{
	FWidgetAnimEvaluatorRegistry::Get().Unregister(FWidgetAnimCountUpParams::StaticStruct());
	FWidgetAnimEvaluatorRegistry::Get().Unregister(FWidgetAnimBlueprintTypeParams::StaticStruct());
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FNameTBDModule, NameTBD)