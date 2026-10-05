// Copyright AR Game Systems 2026. All rights reserved.

#include "NameTBD.h"

#include "WidgetAnimation/WidgetAnimEvaluator.h"
#include "WidgetAnimation/Evaluators/WidgetAnimCountUp.h"
#include "WidgetAnimation/Evaluators/WidgetAnimTextReveal.h"
#include "WidgetAnimation/Evaluators/WidgetAnimTextTracks.h"
#include "WidgetAnimation/Evaluators/WidgetAnimBlueprintType.h"
#include "WidgetAnimation/Evaluators/WidgetAnimCues.h"
#include "WidgetAnimation/Evaluators/WidgetAnimBlueprintCue.h"
#include "WidgetAnimation/WidgetAnimCue.h"
#include "WidgetAnimation/WidgetAnimationComponent.h"

#define LOCTEXT_NAMESPACE "FNameTBDModule"

void FNameTBDModule::StartupModule()
{
	// The first-party registered track types. A game or licensee module registers its own the same way, from its
	// own StartupModule - nothing here needs to know about them.
	// The text tracks, listed under Text in the Add Track menu - see WidgetAnimTextTracks.h.
	FWidgetAnimEvaluatorRegistry& Registry = FWidgetAnimEvaluatorRegistry::Get();
	Registry.Register(MakeShared<FWidgetAnimTextRevealEvaluator>());
	Registry.Register(MakeShared<FWidgetAnimCountUpEvaluator>());
	Registry.Register(MakeShared<FWidgetAnimScrambleEvaluator>());
	Registry.Register(MakeShared<FWidgetAnimTextColorEvaluator>());
	Registry.Register(MakeShared<FWidgetAnimShimmerEvaluator>());
	Registry.Register(MakeShared<FWidgetAnimWaveEvaluator>());
	Registry.Register(MakeShared<FWidgetAnimShakeEvaluator>());
	Registry.Register(MakeShared<FWidgetAnimLetterSpacingEvaluator>());

	// The bridge every Blueprint track type runs through - see UWidgetAnimBlueprintType. Blueprint types themselves
	// need no registering: the Widget Animations panel finds them as assets.
	Registry.Register(MakeShared<FWidgetAnimBlueprintEvaluator>());

	// The cues, listed under Cues - see WidgetAnimCue.h - and the bridge every Blueprint cue runs through.
	Registry.Register(MakeShared<FWidgetAnimEventCueEvaluator>());
	Registry.Register(MakeShared<FWidgetAnimSoundCueEvaluator>());
	Registry.Register(MakeShared<FWidgetAnimPlayCueEvaluator>());
	Registry.Register(MakeShared<FWidgetAnimBlueprintCueEvaluator>());
}

void FNameTBDModule::ShutdownModule()
{
	FWidgetAnimEvaluatorRegistry& Registry = FWidgetAnimEvaluatorRegistry::Get();
	Registry.Unregister(FWidgetAnimTextRevealParams::StaticStruct());
	Registry.Unregister(FWidgetAnimCountUpParams::StaticStruct());
	Registry.Unregister(FWidgetAnimScrambleParams::StaticStruct());
	Registry.Unregister(FWidgetAnimTextColorParams::StaticStruct());
	Registry.Unregister(FWidgetAnimShimmerParams::StaticStruct());
	Registry.Unregister(FWidgetAnimWaveParams::StaticStruct());
	Registry.Unregister(FWidgetAnimShakeParams::StaticStruct());
	Registry.Unregister(FWidgetAnimLetterSpacingParams::StaticStruct());
	Registry.Unregister(FWidgetAnimBlueprintTypeParams::StaticStruct());
	Registry.Unregister(FWidgetAnimEventCueParams::StaticStruct());
	Registry.Unregister(FWidgetAnimSoundCueParams::StaticStruct());
	Registry.Unregister(FWidgetAnimPlayCueParams::StaticStruct());
	Registry.Unregister(FWidgetAnimBlueprintCueParams::StaticStruct());

	// Cues passed on the last frame have nothing left to fire into.
	WidgetAnimCues::DiscardPending();
	UWidgetAnimationComponent::ShutdownRuntimeClock();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FNameTBDModule, NameTBD)