// Copyright AR Game Systems 2026. All rights reserved.

#include "NameTBD.h"

#include "WidgetAnimation/WidgetAnimEvaluator.h"
#include "WidgetAnimation/Evaluators/WidgetAnimCountUp.h"
#include "WidgetAnimation/Evaluators/WidgetAnimTextReveal.h"
#include "WidgetAnimation/Evaluators/WidgetAnimTextTracks.h"
#include "WidgetAnimation/Evaluators/WidgetAnimBlueprintType.h"
#include "WidgetAnimation/Evaluators/WidgetAnimCues.h"
#include "WidgetAnimation/Evaluators/WidgetAnimBlueprintCue.h"
#include "WidgetAnimation/Evaluators/WidgetAnimShapeTrack.h"
#include "WidgetAnimation/Evaluators/WidgetAnimDesignTrack.h"
#include "WidgetAnimation/Evaluators/WidgetAnimInteractionTracks.h"
#include "WidgetAnimation/WidgetAnimCue.h"
#include "WidgetAnimation/WidgetAnimInput.h"
#include "WidgetAnimation/WidgetAnimationComponent.h"
#include "WidgetAnimation/WidgetAnimStateMachine.h"
#include "Widgets/FlowList/FlowList.h"
#include "Diegetic/DiegeticScreenComponent.h"
#include "Widgets/Constraints/WidgetConstraintComponent.h"

#define LOCTEXT_NAMESPACE "FNameTBDModule"

namespace NameTBDModuleLocal
{
	/** The Flow List's answer to "is this widget selected?" for state machines - see WidgetAnimMachineWidgetStates. */
	int32 FlowListSelectedProvider = INDEX_NONE;
}

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

	// What plays a Shape track's keys onto its shape - not a type of its own in the Add Track menu, where the Shape track
	// is built in; registered so the settings it captures can be put back by type. See WidgetAnimShapeTrack.h.
	Registry.Register(MakeShared<FWidgetAnimShapeKeysEvaluator>());

	// And a Design track's keys onto its widget, the same way. See WidgetAnimDesignTrack.h.
	Registry.Register(MakeShared<FWidgetAnimDesignKeysEvaluator>());

	// The interaction tracks, listed under Interaction: a widget's Constraints and Pointer Effects animated. See
	// WidgetAnimInteractionTracks.h.
	Registry.Register(MakeShared<FWidgetAnimConstraintEvaluator>());
	Registry.Register(MakeShared<FWidgetAnimPointerEffectsEvaluator>());

	// A widget in a Flow List's selected item reads Selected in every state machine - see UFlowList::IsInSelectedItem.
	NameTBDModuleLocal::FlowListSelectedProvider = WidgetAnimMachineWidgetStates::RegisterSelectedProvider([](UWidget* Widget)
	{
		return UFlowList::IsInSelectedItem(Widget);
	});

	// A widget a Diegetic Screen draws into the world reads that screen's pointer in its Widget Animation and state machine
	// inputs, not the desktop's - see WidgetAnimInput::FWorldPointer.
	WidgetAnimInput::SetWorldPointerResolver([](const UWidget* Widget, WidgetAnimInput::FWorldPointer& OutPointer)
	{
		const UDiegeticScreenComponent* Screen = UDiegeticScreenComponent::FindScreenForWidget(Widget);
		if (!Screen)
		{
			return false;
		}
		Screen->GetWorldPointer(OutPointer);
		return true;
	});
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
	Registry.Unregister(FWidgetAnimShapeKeysParams::StaticStruct());
	Registry.Unregister(FWidgetAnimDesignKeysParams::StaticStruct());
	Registry.Unregister(FWidgetAnimConstraintParams::StaticStruct());
	Registry.Unregister(FWidgetAnimPointerEffectsParams::StaticStruct());

	WidgetAnimMachineWidgetStates::UnregisterSelectedProvider(NameTBDModuleLocal::FlowListSelectedProvider);
	NameTBDModuleLocal::FlowListSelectedProvider = INDEX_NONE;

	// Cues passed on the last frame have nothing left to fire into.
	WidgetAnimCues::DiscardPending();
	UWidgetAnimationComponent::ShutdownRuntimeClock();

	// The input counter the state machines' built-in pulses read goes with the code it runs.
	WidgetAnimInput::StopCountingEvents();
	WidgetAnimInput::SetWorldPointerResolver(nullptr);

	// Constraints stop being solved with the code that solves them.
	UWidgetConstraintComponent::ShutdownSolver();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FNameTBDModule, NameTBD)