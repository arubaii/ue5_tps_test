// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimCues.h"

#include "Blueprint/UserWidget.h"
#include "Components/Widget.h"
#include "NameTBD/Sound/UISoundTypes.h"
#include "NameTBD/WidgetAnimation/WidgetAnimationComponent.h"
#include "Sound/SoundBase.h"

#define LOCTEXT_NAMESPACE "WidgetAnimCues"

namespace WidgetAnimCueTargets
{
	/**
	 * Outward from Widget - the widget itself first - to the first thing that plays animations by Anim ID: a Widget
	 * Animation Component on a widget, or an animation host (a button, a card, an animatable widget). The same search
	 * Set Widget Animation Value makes. One of the two outputs is set when it returns true.
	 */
	static bool FindPlayer(UWidget* Widget, UWidgetAnimationComponent*& OutComponent, IWidgetAnimHost*& OutHost)
	{
		OutComponent = nullptr;
		OutHost = nullptr;

		static constexpr int32 MaxSearchDepth = 64;
		UObject* Search = Widget;
		for (int32 Depth = 0; Search && Depth < MaxSearchDepth; ++Depth, Search = Search->GetOuter())
		{
			if (const UWidget* SearchWidget = Cast<UWidget>(Search))
			{
				if (UWidgetAnimationComponent* Component = UWidgetAnimationComponent::FindWidgetAnimationComponent(SearchWidget))
				{
					OutComponent = Component;
					return true;
				}
			}
			if (IWidgetAnimHost* Host = Cast<IWidgetAnimHost>(Search))
			{
				OutHost = Host;
				return true;
			}
		}
		return false;
	}
}

namespace WidgetAnimCueDispatchers
{
	/**
	 * Fills a function's parameters by type, whatever they are called: a Widget Animation Cue Info gets Info, a name the
	 * event's name, a number the point's value; any other is left at its default. Params is set up with InitializeStruct.
	 */
	static void FillParams(const UFunction* Function, uint8* Params, FName EventName, const FWidgetAnimCueInfo& Info)
	{
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			if (const FStructProperty* StructProperty = CastField<FStructProperty>(*It))
			{
				if (StructProperty->Struct == FWidgetAnimCueInfo::StaticStruct())
				{
					StructProperty->CopyCompleteValue(StructProperty->ContainerPtrToValuePtr<void>(Params), &Info);
				}
			}
			else if (const FNameProperty* NameProperty = CastField<FNameProperty>(*It))
			{
				NameProperty->SetPropertyValue_InContainer(Params, EventName);
			}
			else if (const FDoubleProperty* DoubleProperty = CastField<FDoubleProperty>(*It))
			{
				DoubleProperty->SetPropertyValue_InContainer(Params, Info.Value);
			}
			else if (const FFloatProperty* FloatProperty = CastField<FFloatProperty>(*It))
			{
				FloatProperty->SetPropertyValue_InContainer(Params, Info.Value);
			}
		}
	}

	/** Calls Owner's event function of this name - an Event track's Custom Event - if it has one. True if it did. */
	static bool CallEventFunction(UObject* Owner, FName FunctionName, FName EventName, const FWidgetAnimCueInfo& Info)
	{
		UFunction* Function = Owner->FindFunction(FunctionName);
		if (!Function)
		{
			return false;
		}

		uint8* Params = static_cast<uint8*>(FMemory_Alloca_Aligned(FMath::Max<int32>(Function->ParmsSize, 1), Function->GetMinAlignment()));
		Function->InitializeStruct(Params);
		FillParams(Function, Params, EventName, Info);
		Owner->ProcessEvent(Function, Params);
		Function->DestroyStruct(Params);
		return true;
	}

	/** Broadcasts Owner's Event Dispatcher of this name, when it declares one, with its parameters filled by type. */
	static void Broadcast(UObject* Owner, FName Dispatcher, FName EventName, const FWidgetAnimCueInfo& Info)
	{
		const FMulticastDelegateProperty* Property = FindFProperty<FMulticastDelegateProperty>(Owner->GetClass(), Dispatcher);
		const FMulticastScriptDelegate* Delegate = Property ? Property->GetMulticastDelegate(Property->ContainerPtrToValuePtr<void>(Owner)) : nullptr;
		if (!Delegate || !Delegate->IsBound())
		{
			return;
		}

		UFunction* Signature = Property->SignatureFunction;
		if (!Signature || Signature->ParmsSize == 0)
		{
			Delegate->ProcessMulticastDelegate<UObject>(nullptr);
			return;
		}

		uint8* Params = static_cast<uint8*>(FMemory_Alloca_Aligned(Signature->ParmsSize, Signature->GetMinAlignment()));
		Signature->InitializeStruct(Params);
		FillParams(Signature, Params, EventName, Info);
		Delegate->ProcessMulticastDelegate<UObject>(Params);
		Signature->DestroyStruct(Params);
	}
}

// =====================================================================================================
// Event
// =====================================================================================================

FText FWidgetAnimEventCueEvaluator::GetDisplayName() const
{
	return LOCTEXT("EventName", "Event");
}

FText FWidgetAnimEventCueEvaluator::GetDescription() const
{
	return LOCTEXT("EventDescription", "Tells the game a moment has come, by name: heard by the playing component's On Animation Event, and by the nearest User Widget around it with the Widget Animation Event Listener interface.");
}

FText FWidgetAnimEventCueEvaluator::GetTrackSummary(const FInstancedStruct& Params) const
{
	const FWidgetAnimEventCueParams* EventParams = GetParams(Params);
	return EventParams && !EventParams->EventName.IsNone()
		? FText::FromName(EventParams->EventName)
		: LOCTEXT("EventNoName", "(no name)");
}

double FWidgetAnimEventCueEvaluator::GetPointValue(const FInstancedStruct& Params) const
{
	const FWidgetAnimEventCueParams* EventParams = GetParams(Params);
	return EventParams ? EventParams->Value : 0.0;
}

void FWidgetAnimEventCueEvaluator::Fire(const FWidgetAnimCueContext& Context) const
{
	const FWidgetAnimEventCueParams* EventParams = Context.Params ? GetParams(*Context.Params) : nullptr;
	UWidget* Host = Context.Info.Host.Get();
	if (!EventParams || !Host || Context.Info.bDesignTime)
	{
		return;
	}

	const FName EventName = EventParams->EventName;

	// Its event - the Custom Event On <Animation> Event Track <Name> Fired, named by the animation the track is in - on the
	// nearest User Widget around the widget playing it that has one: the Widget Blueprint the animation's component was set
	// up in, usually. Then any Event Dispatcher of its name (On + its name) declared by a User Widget around it, for a
	// widget holding this one to listen through.
	const FName FunctionName = WidgetAnimCues::GetEventFunctionName(Context.Info.Animation.Get(), EventName);
	const FName Dispatcher = WidgetAnimCues::GetEventDispatcherName(EventName);
	bool bCalledNode = FunctionName.IsNone();
	static constexpr int32 MaxOwnerDepth = 64;
	UObject* Owner = Host;
	for (int32 Depth = 0; Owner && Depth < MaxOwnerDepth; ++Depth, Owner = Owner->GetOuter())
	{
		if (!Owner->IsA<UUserWidget>())
		{
			continue;
		}
		if (!bCalledNode)
		{
			bCalledNode = WidgetAnimCueDispatchers::CallEventFunction(Owner, FunctionName, EventName, Context.Info);
		}
		if (!Dispatcher.IsNone())
		{
			WidgetAnimCueDispatchers::Broadcast(Owner, Dispatcher, EventName, Context.Info);
		}
	}

	// The component playing it: the widget playing it is the component's own widget.
	if (UWidgetAnimationComponent* Component = UWidgetAnimationComponent::FindWidgetAnimationComponent(Host))
	{
		Component->OnAnimationEvent.Broadcast(EventName, Context.Info);
	}

	// The nearest listener around the widget playing it - the User Widget whose designer the animation was set up in,
	// usually. Only the nearest: an event is its business, not every widget's around it.
	static constexpr int32 MaxSearchDepth = 64;
	UObject* Search = Host;
	for (int32 Depth = 0; Search && Depth < MaxSearchDepth; ++Depth, Search = Search->GetOuter())
	{
		if (Search->GetClass()->ImplementsInterface(UWidgetAnimEventListener::StaticClass()))
		{
			IWidgetAnimEventListener::Execute_OnWidgetAnimationEvent(Search, EventName, Context.Info);
			break;
		}
	}

	WidgetAnimCues::OnEvent().Broadcast(EventName, Context.Info);
}

// =====================================================================================================
// Sound
// =====================================================================================================

FText FWidgetAnimSoundCueEvaluator::GetDisplayName() const
{
	return LOCTEXT("SoundName", "Sound");
}

FText FWidgetAnimSoundCueEvaluator::GetDescription() const
{
	return LOCTEXT("SoundDescription", "Plays a sound in 2D, as a UI sound. Staggered over a text's letters, one per letter. Heard in the editor's previews too.");
}

FText FWidgetAnimSoundCueEvaluator::GetValueLabel() const
{
	return LOCTEXT("SoundValueLabel", "Volume");
}

FText FWidgetAnimSoundCueEvaluator::GetTrackSummary(const FInstancedStruct& Params) const
{
	const FWidgetAnimSoundCueParams* SoundParams = GetParams(Params);
	return SoundParams && SoundParams->Sound
		? FText::FromString(SoundParams->Sound->GetName())
		: LOCTEXT("SoundNone", "(no sound)");
}

double FWidgetAnimSoundCueEvaluator::GetPointValue(const FInstancedStruct& Params) const
{
	const FWidgetAnimSoundCueParams* SoundParams = GetParams(Params);
	return SoundParams ? SoundParams->Volume : 1.0;
}

void FWidgetAnimSoundCueEvaluator::Fire(const FWidgetAnimCueContext& Context) const
{
	const FWidgetAnimSoundCueParams* SoundParams = Context.Params ? GetParams(*Context.Params) : nullptr;
	if (!SoundParams || !SoundParams->Sound)
	{
		return;
	}

	const float Volume = FMath::Max(Context.Info.Value, 0.f);
	const float Jitter = SoundParams->PitchVariation > 0.f ? FMath::FRandRange(-SoundParams->PitchVariation, SoundParams->PitchVariation) : 0.f;
	const float Pitch = FMath::Max(SoundParams->Pitch + Jitter, 0.01f);

	// Fading: up from silence as it starts, and down to silence finishing as the track's run does - as the sound ends,
	// with Length From Sound. Where the run's end isn't known (a value-driven track), or this is its very end (Fire At
	// End), the sound's own end is used, and a looping sound with neither never fades out.
	WidgetAnimCues::FSoundFade Fade;
	Fade.FadeIn = FMath::Max(SoundParams->FadeIn, 0.f);
	Fade.Curve = static_cast<uint8>(SoundParams->FadeCurve);
	if (SoundParams->FadeOut > 0.f)
	{
		float End = Context.Info.SecondsToTrackEnd;
		if (End <= KINDA_SMALL_NUMBER)
		{
			const float SoundSeconds = SoundParams->Sound->GetDuration();
			End = SoundSeconds > 0.f && SoundSeconds < INDEFINITELY_LOOPING_DURATION ? SoundSeconds / Pitch : -1.f;
		}
		if (End > 0.f)
		{
			Fade.FadeOut = FMath::Min(SoundParams->FadeOut, End);
			Fade.FadeOutAt = End - Fade.FadeOut;
		}
	}

	// An editor preview has no game world to play in; the editor module plays it, if it has been told to.
	if (Context.Info.bDesignTime)
	{
		WidgetAnimCues::PlayPreviewSound(SoundParams->Sound, Volume, Pitch, Fade);
		return;
	}

	if (Fade.HasFade())
	{
		FUISoundPlayer::PlayFaded(Context.GetWorldContext(), SoundParams->Sound, Volume, Pitch, Fade.FadeIn, Fade.FadeOutAt, Fade.FadeOut, Fade.Curve);
		return;
	}

	FUISoundPlayer::PlayOneShot(Context.GetWorldContext(), SoundParams->Sound, Volume, Pitch);
}

float FWidgetAnimSoundCueEvaluator::GetSoundLength(const FWidgetAnimSoundCueParams& Params)
{
	if (!Params.Sound)
	{
		return -1.f;
	}

	const float Seconds = Params.Sound->GetDuration();
	if (Seconds <= 0.f || Seconds >= INDEFINITELY_LOOPING_DURATION)
	{
		return -1.f;
	}
	return Seconds / FMath::Max(Params.Pitch, 0.01f);
}

bool FWidgetAnimSoundCueEvaluator::NeedsPlayPreparation(const FInstancedStruct& Params) const
{
	return HasDerivedLength(Params);
}

bool FWidgetAnimSoundCueEvaluator::HasDerivedLength(const FInstancedStruct& Params) const
{
	const FWidgetAnimSoundCueParams* SoundParams = GetParams(Params);
	return SoundParams && SoundParams->bLengthFromSound && GetSoundLength(*SoundParams) > 0.f;
}

float FWidgetAnimSoundCueEvaluator::PrepareForPlay(UObject* Target, FInstancedStruct& Params, float AuthoredRun) const
{
	// Nothing about the widget matters: the sound alone says how long it is.
	const FWidgetAnimSoundCueParams* SoundParams = GetParams(Params);
	const float Seconds = SoundParams && SoundParams->bLengthFromSound ? GetSoundLength(*SoundParams) : -1.f;
	return Seconds > 0.f ? Seconds : -1.f;
}

FText FWidgetAnimSoundCueEvaluator::GetDerivedLengthLabel(const FInstancedStruct& Params) const
{
	return LOCTEXT("SoundDerivedLength", "sound's length");
}

// =====================================================================================================
// Play Animation
// =====================================================================================================

FText FWidgetAnimPlayCueEvaluator::GetDisplayName() const
{
	return LOCTEXT("PlayName", "Play Animation");
}

FText FWidgetAnimPlayCueEvaluator::GetDescription() const
{
	return LOCTEXT("PlayDescription", "Plays other animations by Anim ID - on the widget playing this one, or on the widget this track is aimed at.");
}

FText FWidgetAnimPlayCueEvaluator::GetValueLabel() const
{
	return LOCTEXT("PlayValueLabel", "Speed");
}

FText FWidgetAnimPlayCueEvaluator::GetTrackSummary(const FInstancedStruct& Params) const
{
	const FWidgetAnimPlayCueParams* PlayParams = GetParams(Params);
	if (!PlayParams || PlayParams->AnimIds.Num() == 0)
	{
		return LOCTEXT("PlayAll", "Play all");
	}

	TArray<FString> Names;
	for (const FName AnimId : PlayParams->AnimIds)
	{
		Names.Add(AnimId.ToString());
	}
	return FText::Format(LOCTEXT("PlaySome", "Play {0}"), FText::FromString(FString::Join(Names, TEXT(", "))));
}

double FWidgetAnimPlayCueEvaluator::GetPointValue(const FInstancedStruct& Params) const
{
	const FWidgetAnimPlayCueParams* PlayParams = GetParams(Params);
	return PlayParams ? PlayParams->PlaybackSpeed : 1.0;
}

void FWidgetAnimPlayCueEvaluator::Fire(const FWidgetAnimCueContext& Context) const
{
	const FWidgetAnimPlayCueParams* PlayParams = Context.Params ? GetParams(*Context.Params) : nullptr;
	if (!PlayParams || Context.Info.bDesignTime)
	{
		return;
	}

	UWidget* Where = PlayParams->PlayOn == EWidgetAnimPlayCueOn::TrackWidget && Context.Info.Target ? Context.Info.Target.Get() : Context.Info.Host.Get();

	UWidgetAnimationComponent* Component = nullptr;
	IWidgetAnimHost* Host = nullptr;
	if (!WidgetAnimCueTargets::FindPlayer(Where, Component, Host))
	{
		return;
	}

	const bool bReverse = PlayParams->PlayDirection == EWidgetAnimPlayDirection::Reverse;
	const float Speed = FMath::Max(Context.Info.Value, 0.01f);
	if (Component)
	{
		Component->PlayWidgetAnimationsById(PlayParams->AnimIds, bReverse, Speed, PlayParams->bHoldFinalPose, PlayParams->bRestartIfPlaying);
	}
	else
	{
		Host->PlayWidgetAnimationsById(PlayParams->AnimIds, bReverse, Speed, PlayParams->bHoldFinalPose, PlayParams->bRestartIfPlaying);
	}
}

#undef LOCTEXT_NAMESPACE
