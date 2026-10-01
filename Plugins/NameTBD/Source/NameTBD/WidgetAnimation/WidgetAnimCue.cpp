// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimCue.h"

#include "Components/Widget.h"
#include "Containers/Ticker.h"
#include "WidgetAnimLibrary.h"

#define LOCTEXT_NAMESPACE "WidgetAnimCue"

// =====================================================================================================
// FWidgetAnimCueContext / FWidgetAnimCueEvaluatorBase
// =====================================================================================================

UObject* FWidgetAnimCueContext::GetWorldContext() const
{
	if (UWidget* TargetWidget = Info.Target.Get())
	{
		return TargetWidget;
	}
	return Info.Host.Get();
}

FText FWidgetAnimCueEvaluatorBase::GetCategory() const
{
	return LOCTEXT("CueCategory", "Cues");
}

// =====================================================================================================
// Firing, a frame later
// =====================================================================================================

namespace WidgetAnimCues
{
	namespace
	{
		/** One fire waiting for the next frame, with everything it needs kept - its settings copied, its widgets weak. */
		struct FQueuedCue
		{
			FInstancedStruct Params;
			TWeakObjectPtr<UWidget> Target;
			TWeakObjectPtr<UWidget> Host;
			TWeakObjectPtr<UObject> Animation;
			FName AnimId;
			int32 Point = 0;
			int32 PartIndex = INDEX_NONE;
			float Value = 0.f;
			bool bBackward = false;
			bool bInverted = false;
			bool bDesignTime = false;
			float SecondsToEnd = -1.f;
		};

		TArray<FQueuedCue>& GetPending()
		{
			static TArray<FQueuedCue> Pending;
			return Pending;
		}

		FTSTicker::FDelegateHandle& GetTickerHandle()
		{
			static FTSTicker::FDelegateHandle Handle;
			return Handle;
		}

		FPreviewSoundPlayer& GetPreviewSoundPlayer()
		{
			static FPreviewSoundPlayer Player;
			return Player;
		}

		bool Flush(float /*DeltaTime*/)
		{
			// Taken first, and the ticker forgotten: whatever these cues start may pass cues of its own, which wait for
			// the frame after, as every cue does.
			TArray<FQueuedCue> Cues = MoveTemp(GetPending());
			GetPending().Reset();
			GetTickerHandle().Reset();

			for (const FQueuedCue& Cue : Cues)
			{
				// Found again rather than kept from a frame ago: a module unloaded in between takes its types with it.
				const FWidgetAnimEvaluator* Evaluator = FWidgetAnimEvaluatorRegistry::Get().Find(Cue.Params);
				UWidget* Target = Cue.Target.Get();
				UWidget* Host = Cue.Host.Get();
				if (!Evaluator || !Evaluator->IsCue() || (!Target && !Host))
				{
					continue;
				}

				FWidgetAnimCueContext Context;
				Context.Params = &Cue.Params;
				Context.Target = Target ? Evaluator->ResolveTarget(Target, Cue.Params) : nullptr;
				Context.Info.Target = Target;
				Context.Info.Host = Host;
				Context.Info.Animation = Cue.Animation.Get();
				Context.Info.AnimId = Cue.AnimId;
				Context.Info.Point = Cue.Point;
				Context.Info.Value = Cue.Value;
				Context.Info.PartIndex = Cue.PartIndex;
				Context.Info.bPlayingBackward = Cue.bBackward;
				Context.Info.bInverted = Cue.bInverted;
				Context.Info.bDesignTime = Cue.bDesignTime;
				Context.Info.SecondsToTrackEnd = Cue.SecondsToEnd;

				Evaluator->Fire(Context);
			}

			// One-shot: added again by the next Queue.
			return false;
		}
	}

	void Queue(TConstArrayView<FWidgetAnimCueFire> Fires, UWidget* Host, FName AnimId, bool bPreview)
	{
		if (Fires.Num() == 0)
		{
			return;
		}

		TArray<FQueuedCue>& Pending = GetPending();
		for (const FWidgetAnimCueFire& Fire : Fires)
		{
			if (!Fire.Params)
			{
				continue;
			}

			const FWidgetAnimEvaluator* Evaluator = FWidgetAnimEvaluatorRegistry::Get().Find(*Fire.Params);
			UWidget* Target = Fire.TargetWidget ? Fire.TargetWidget : Host;
			if (!Evaluator || !Evaluator->IsCue() || !Target)
			{
				continue;
			}

			// An editor preview only fires what is meant to be heard there. Decided now, while the widget is certainly
			// still there to ask.
			const bool bDesignTime = bPreview || Target->IsDesignTime() || (Host && Host->IsDesignTime());
			if (bDesignTime && !Evaluator->FiresAtDesignTime(*Fire.Params))
			{
				continue;
			}

			FQueuedCue& Cue = Pending.AddDefaulted_GetRef();
			Cue.Params = *Fire.Params;
			Cue.Target = Target;
			Cue.Host = Host ? Host : Target;
			Cue.Animation = Fire.Animation;
			Cue.AnimId = AnimId;
			Cue.Point = Fire.Point;
			Cue.PartIndex = Fire.Part.IsSet() ? Fire.Part.Index : INDEX_NONE;
			Cue.Value = static_cast<float>(Fire.Value);
			Cue.bBackward = Fire.bBackward;
			Cue.bInverted = Fire.bInverted;
			Cue.bDesignTime = bDesignTime;
			Cue.SecondsToEnd = Fire.SecondsToEnd;
		}

		// The same deferral an <event> in a Text Reveal's text gets: the next tick of the core ticker, which runs in the
		// editor and the game alike.
		if (Pending.Num() > 0 && !GetTickerHandle().IsValid())
		{
			GetTickerHandle() = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Flush));
		}
	}

	void DiscardPending()
	{
		if (GetTickerHandle().IsValid())
		{
			FTSTicker::RemoveTicker(GetTickerHandle());
			GetTickerHandle().Reset();
		}
		GetPending().Empty();
		GetPreviewSoundPlayer() = nullptr;
	}

	namespace
	{
		/**
		 * Letters, digits and underscores only - what a Blueprint name can hold - each word capitalized, so "card landed"
		 * reads CardLanded. Empty when nothing is left.
		 */
		FString MakeIdentifier(FName EventName)
		{
			FString Name;
			if (EventName.IsNone())
			{
				return Name;
			}

			bool bWordStart = true;
			for (const TCHAR Char : EventName.ToString())
			{
				if (FChar::IsAlnum(Char) || Char == TEXT('_'))
				{
					Name.AppendChar(bWordStart ? FChar::ToUpper(Char) : Char);
					bWordStart = false;
				}
				else
				{
					bWordStart = true;
				}
			}
			return Name;
		}
	}

	FString GetEventAnimationName(const UObject* Animation)
	{
		if (!Animation)
		{
			return TEXT("Additive");
		}

		// The asset's name as the Content Browser shows it, less the prefix every Widget Animation shares, which would
		// only make every event longer.
		FString Name = Animation->GetName();
		if (Name.StartsWith(TEXT("WA_"), ESearchCase::IgnoreCase) && Name.Len() > 3)
		{
			Name.RightChopInline(3);
		}
		return Name;
	}

	FName GetEventFunctionName(const UObject* Animation, FName EventName)
	{
		// The event's own words, each capitalized, one space between - "card landed!" reads Card Landed - so the event
		// reads as a sentence in the graph, as a widget's own events do.
		TArray<FString> Words;
		FString Word;
		for (const TCHAR Char : EventName.IsNone() ? FString() : EventName.ToString())
		{
			if (FChar::IsAlnum(Char) || Char == TEXT('_'))
			{
				Word.AppendChar(Word.IsEmpty() ? FChar::ToUpper(Char) : Char);
			}
			else if (!Word.IsEmpty())
			{
				Words.Add(MoveTemp(Word));
				Word.Reset();
			}
		}
		if (!Word.IsEmpty())
		{
			Words.Add(MoveTemp(Word));
		}
		if (Words.Num() == 0)
		{
			return NAME_None;
		}
		// Named by its animation too, so the same event name in two animations - Landed in a card and in a menu - is two
		// events, each told apart in the graph.
		const FString FunctionName = FString::Printf(TEXT("On %s Event Track %s Fired"), *GetEventAnimationName(Animation), *FString::Join(Words, TEXT(" ")));
		return FName(*FunctionName);
	}

	FName GetEventDispatcherName(FName EventName)
	{
		const FString Name = MakeIdentifier(EventName);
		if (Name.IsEmpty())
		{
			return NAME_None;
		}

		// A name already starting On and a capital - OnReveal - is a dispatcher name as it stands.
		const bool bHasOn = Name.Len() > 2 && Name.StartsWith(TEXT("On"), ESearchCase::CaseSensitive) && FChar::IsUpper(Name[2]);
		const FString DispatcherName = bHasOn ? Name : TEXT("On") + Name;
		return FName(*DispatcherName);
	}

	FWidgetAnimEventNative& OnEvent()
	{
		static FWidgetAnimEventNative Delegate;
		return Delegate;
	}

	void SetPreviewSoundPlayer(FPreviewSoundPlayer Player)
	{
		GetPreviewSoundPlayer() = MoveTemp(Player);
	}

	bool PlayPreviewSound(USoundBase* Sound, float Volume, float Pitch, const FSoundFade& Fade)
	{
		const FPreviewSoundPlayer& Player = GetPreviewSoundPlayer();
		if (!Sound || !Player)
		{
			return false;
		}
		Player(Sound, Volume, Pitch, Fade);
		return true;
	}

	void GetFiringPoints(const FInstancedStruct& TypeParams, const FWidgetAnimValueKeys& Keys, TArray<TPair<int32, float>, TInlineAllocator<8>>& OutPoints)
	{
		OutPoints.Reset();
		if (!WidgetAnimIsRegisteredCue(TypeParams))
		{
			return;
		}

		// A cue type whose settings don't derive from the shared ones fires as a new cue does: at its start, forward.
		static const FWidgetAnimCueParams Defaults;
		const FWidgetAnimCueParams* CueParams = GetCueParams(TypeParams);
		const FWidgetAnimCueParams& Rules = CueParams ? *CueParams : Defaults;

		const int32 NumPoints = Keys.NumPoints();
		for (int32 Point = 0; Point < NumPoints; ++Point)
		{
			if (Rules.FiresAtPoint(Point, NumPoints))
			{
				OutPoints.Emplace(Point, Keys.GetPointTime(Point));
			}
		}
	}

	double GetPointValue(const FInstancedStruct& TypeParams, const FWidgetAnimValueKeys& Keys, int32 Point)
	{
		if (Point > 0 && Point <= Keys.Keys.Num())
		{
			return Keys.Keys[Point - 1].Value;
		}
		return WidgetAnimGetRegisteredEndpoint(TypeParams, /*bStart=*/Point <= 0);
	}
}

#undef LOCTEXT_NAMESPACE
