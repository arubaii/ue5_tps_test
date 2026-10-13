// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimLibrary.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/RetainerBox.h"
#include "Components/TextBlock.h"
#include "Materials/MaterialParameters.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Algo/StableSort.h"
#include "Framework/Application/SlateApplication.h"
#include "UObject/ObjectKey.h"
#include "HAL/IConsoleManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "NameTBD/Widgets/ShapeBox.h"
#include "WidgetAnimTransform.h"
#include "WidgetAnimSpring.h"
#include "Evaluators/WidgetAnimShapeTrack.h"
#include "Evaluators/WidgetAnimDesignTrack.h"
#include "Text/WidgetAnimTextHost.h"

#if WITH_EDITOR
#include "UObject/UnrealType.h"
#include "WidgetAnimExpander.h"
#include "WidgetAnimParts.h"
#include "WidgetAnimPreset.h"

#include <atomic>
#endif

DEFINE_STAT(STAT_WidgetAnimRuntimeClock);
DEFINE_STAT(STAT_WidgetAnimEvaluate);
DEFINE_STAT(STAT_WidgetAnimApply);
DEFINE_STAT(STAT_WidgetAnimComponents);
DEFINE_STAT(STAT_WidgetAnimComponentsResting);
DEFINE_STAT(STAT_WidgetAnimTracksEvaluated);

namespace WidgetAnimScratch
{
	/**
	 * Results reused frame after frame instead of built from nothing: emptied with their arrays kept at the size they grew
	 * to, so a player ticked every frame stops allocating once it has played a frame. Hundreds of animated widgets used to
	 * allocate a fresh result - and a fresh one per track - on every one of them, every frame.
	 *
	 * One per depth: applying a result can call out (a Blueprint type's Apply Value) to something that ticks another player
	 * before this one is done with its own, which takes the next one down. Game thread only, as everything that plays is.
	 */
	TArray<TUniquePtr<FWidgetAnimResult>>& GetPool()
	{
		static TArray<TUniquePtr<FWidgetAnimResult>> Pool;
		return Pool;
	}

	int32& GetDepth()
	{
		static int32 Depth = 0;
		return Depth;
	}

	/** An empty result for as long as it is in scope. Never kept, returned, or moved out of. */
	struct FScope
	{
		FWidgetAnimResult& Result;

		FScope()
			: Result(Acquire())
		{
		}

		~FScope()
		{
			// Emptied as it is handed back, so nothing it pointed at is held on to between uses.
			Result.ResetKeepingCapacity();
			--GetDepth();
		}

		FScope(const FScope&) = delete;
		FScope& operator=(const FScope&) = delete;

	private:
		static FWidgetAnimResult& Acquire()
		{
			TArray<TUniquePtr<FWidgetAnimResult>>& Pool = GetPool();
			const int32 Depth = GetDepth()++;
			while (Pool.Num() <= Depth)
			{
				Pool.Add(MakeUnique<FWidgetAnimResult>());
			}
			FWidgetAnimResult& Scratch = *Pool[Depth];
			Scratch.ResetKeepingCapacity();
			return Scratch;
		}
	};
}

// The easing maths moved to WidgetAnimEasing.h so the Transform family - and the spring and sequencing
// families after it - share one implementation instead of each growing a copy. These two stay as thin
// forwarders because plenty of call sites in this file use them, and both are declared in the header.

float UWidgetAnimLibrary::EaseOutBack(float Alpha, float Overshoot)
{
	return WidgetAnimEasing::EaseOutBack(Alpha, Overshoot);
}

float UWidgetAnimLibrary::EvaluateEasedAlpha(const FWidgetAnimSettings& Settings, float LinearAlpha)
{
	// A value-driven track goes where its value says, straight: its Easing (or spring) is how it travels to each new
	// value, in time, not a bend in where a value puts it. Its keys' own curves still shape it.
	if (Settings.IsValueDriven())
	{
		return FMath::Clamp(LinearAlpha, 0.f, 1.f);
	}

	if (Settings.IsSpringTimed())
	{
		return UWidgetAnimSpringLibrary::EvaluateDirectedProgress(Settings.Spring, LinearAlpha, /*bTowardStart=*/false);
	}

	return WidgetAnimEasing::Evaluate(Settings.Easing, Settings.CustomCurve, LinearAlpha);
}

float UWidgetAnimLibrary::EvaluateLayerAlpha(const FWidgetAnimSettings& Settings, float DirectedAlpha, bool bTowardStart)
{
	// Straight, on a value-driven track - see EvaluateEasedAlpha.
	if (Settings.IsValueDriven())
	{
		return FMath::Clamp(DirectedAlpha, 0.f, 1.f);
	}

	// Only a spring cares which way it is heading. A tween's curve is the same curve rewound, which is
	// exactly what running a tween backwards means; a spring rewound would not look like a spring at all.
	if (Settings.IsSpringTimed())
	{
		return UWidgetAnimSpringLibrary::EvaluateDirectedProgress(Settings.Spring, DirectedAlpha, bTowardStart);
	}

	return WidgetAnimEasing::Evaluate(Settings.Easing, Settings.CustomCurve, DirectedAlpha);
}

float UWidgetAnimLibrary::EvaluateTrackEase(const FWidgetAnimSettings& Settings, float Alpha, bool bTowardStart)
{
	// Scale and Slide's Overshoot is the tween's way of getting a pop, and replaces the Easing when set. A
	// Spring layer already overshoots by its own physics, so there Overshoot is hidden and ignored - two
	// different pops stacked on one value would be neither. So is a track with keys between its start and end:
	// every segment would pop past its own key, which is a wobble, not a pop - its keys and their curves are how
	// it is shaped instead. Nor a Hold, which doesn't travel at all - it lands on its end the moment it arrives.
	if (Settings.Timing == EWidgetAnimTiming::Tween && Settings.ValueKeys.Keys.Num() == 0 && Settings.Easing != EWidgetAnimEasing::Hold)
	{
		if (Settings.AnimType == EWidgetAnimType::Scale && Settings.Overshoot > 0.f)
		{
			return EaseOutBack(Alpha, Settings.Overshoot);
		}
		if (Settings.AnimType == EWidgetAnimType::Slide && Settings.SlideOvershoot > 0.f)
		{
			return EaseOutBack(Alpha, Settings.SlideOvershoot);
		}
	}

	return EvaluateLayerAlpha(Settings, Alpha, bTowardStart);
}

void UWidgetAnimLibrary::SampleValueKeys(const FWidgetAnimSettings& Settings, float DirectedAlpha, bool bTowardStart, int32& OutSegment, float& OutAlpha)
{
	const FWidgetAnimValueKeys& ValueKeys = Settings.ValueKeys;
	const int32 LastSegment = ValueKeys.Keys.Num();

	// Exactly the start and exactly the end at either end of the track, whatever the easing makes of them. A
	// Custom curve that stops at 0.98, or a spring that is only within its rest threshold when its run ends,
	// would otherwise leave a fade to 0 very slightly visible for good - which is what a fast scrub, or a
	// frame landing just so, used to show.
	if (DirectedAlpha <= 0.f)
	{
		OutSegment = 0;
		OutAlpha = 0.f;
		return;
	}
	if (DirectedAlpha >= 1.f)
	{
		OutSegment = LastSegment;
		OutAlpha = 1.f;
		return;
	}

	// Where each point sits, never behind the one before it, so keys out of order can't turn a segment inside out.
	TArray<float, TInlineAllocator<8>> Times;
	Times.SetNum(LastSegment + 2);
	Times[0] = 0.f;
	for (int32 Point = 1; Point <= LastSegment; ++Point)
	{
		Times[Point] = FMath::Clamp(ValueKeys.Keys[Point - 1].Time, Times[Point - 1], 1.f);
	}
	Times[LastSegment + 1] = 1.f;

	// The segment Position falls in, and how far across it. Before the start it is the first segment and past
	// the end the last, run on past their ends - that is an overshoot, carried on in the direction it was going.
	auto Locate = [&Times, LastSegment](float Position, int32& OutLocated, float& OutAcross)
	{
		int32 Segment = 0;
		while (Segment < LastSegment && Position >= Times[Segment + 1])
		{
			++Segment;
		}

		const float Length = Times[Segment + 1] - Times[Segment];
		OutLocated = Segment;
		OutAcross = Length > KINDA_SMALL_NUMBER ? (Position - Times[Segment]) / Length : (Position >= Times[Segment + 1] ? 1.f : 0.f);
	};

	// A spring springs across the whole run, keys and all: its progress finds the segment, and the keys are points
	// along the way it travels.
	if (Settings.IsSpringTimed())
	{
		Locate(EvaluateTrackEase(Settings, DirectedAlpha, bTowardStart), OutSegment, OutAlpha);
		return;
	}

	// A tween with no keys between its start and end: one segment, moving by the track's Easing (or overshoot) - or by the
	// handles it was given in the Graph Editor, which take the segment over as they do any keyed one.
	if (LastSegment == 0 && ValueKeys.UsesStartCurveAlone())
	{
		OutSegment = 0;
		OutAlpha = WidgetAnimEasing::EvaluateSegment(ValueKeys.StartCurve, DirectedAlpha, [](float Alpha) { return Alpha; });
		return;
	}
	if (LastSegment == 0)
	{
		OutSegment = 0;
		OutAlpha = EvaluateTrackEase(Settings, DirectedAlpha);
		return;
	}

	// Keyed: plain time finds the segment, so every key is reached exactly when the timeline shows it - and the
	// segment's own curve, or the track's Easing for one left at Track Easing, shapes the way across it. Bending
	// time across the whole run instead made a key drawn halfway along arrive a fifth of the way in on an Ease Out
	// track, which is not what a keyframe means anywhere else.
	float Across = 0.f;
	Locate(DirectedAlpha, OutSegment, Across);

	if (Across <= 0.f || Across >= 1.f)
	{
		OutAlpha = FMath::Clamp(Across, 0.f, 1.f);
		return;
	}

	OutAlpha = WidgetAnimEasing::EvaluateSegment(ValueKeys.GetSegmentCurve(OutSegment), Across,
		[&Settings](float Alpha) { return EvaluateTrackEase(Settings, Alpha); });
}

FVector2D UWidgetAnimLibrary::GetSlideDirectionOffset(EWidgetSlideDirection Direction)
{
	switch (Direction)
	{
	case EWidgetSlideDirection::Up:    return FVector2D(0.f, -1.f);
	case EWidgetSlideDirection::Down:  return FVector2D(0.f, 1.f);
	case EWidgetSlideDirection::Left:  return FVector2D(-1.f, 0.f);
	case EWidgetSlideDirection::Right: return FVector2D(1.f, 0.f);
	}

	return FVector2D::ZeroVector;
}

FWidgetAnimTargetedTransform& UWidgetAnimLibrary::FindOrAddTargetedTransform(FWidgetAnimResult& Result, UWidget* TargetWidget, const FWidgetAnimPartKey& Part)
{
	// A part of a widget is a slot of its own, apart from the widget's: a letter's fade composes with the other tracks
	// on that letter, never with the text block's own opacity.
	TArray<FWidgetAnimTargetedTransform>& Transforms = Result.TargetedTransforms;
	const int32 Num = Transforms.Num();

	// A few entries - nearly every result - are found fastest by walking them. Past that (a stagger over a list's children,
	// a text's letters) every new entry walked the whole array, so a result of N widgets cost N squared to build, every
	// frame; there the entries are looked up instead. See FWidgetAnimTransformLookup.
	static constexpr int32 LookupThreshold = 24;
	if (Num < LookupThreshold)
	{
		for (FWidgetAnimTargetedTransform& Existing : Transforms)
		{
			if (Existing.TargetWidget == TargetWidget && Existing.Part == Part)
			{
				return Existing;
			}
		}
	}
	else
	{
		FWidgetAnimTransformLookup& Lookup = Result.TransformLookup;
		if (Lookup.Indexed > Num)
		{
			Lookup.Reset();
		}
		for (int32 Index = Lookup.Indexed; Index < Num; ++Index)
		{
			// The first entry for a widget is the one a walk would find.
			const FWidgetAnimTransformKey EntryKey{ Transforms[Index].TargetWidget, Transforms[Index].Part };
			if (!Lookup.Index.Contains(EntryKey))
			{
				Lookup.Index.Add(EntryKey, Index);
			}
		}
		Lookup.Indexed = Num;

		const FWidgetAnimTransformKey Key{ TargetWidget, Part };
		if (const int32* Found = Lookup.Index.Find(Key))
		{
			if (Transforms.IsValidIndex(*Found) && Transforms[*Found].TargetWidget == TargetWidget && Transforms[*Found].Part == Part)
			{
				return Transforms[*Found];
			}

			// Out of step with the array - something changed an entry in place: walked this once, and indexed afresh next time.
			Lookup.Reset();
			for (FWidgetAnimTargetedTransform& Existing : Transforms)
			{
				if (Existing.TargetWidget == TargetWidget && Existing.Part == Part)
				{
					return Existing;
				}
			}
		}
	}

	FWidgetAnimTargetedTransform& NewEntry = Transforms.AddDefaulted_GetRef();
	NewEntry.TargetWidget = TargetWidget;
	NewEntry.Part = Part;

	// Kept in step: the new entry is the one the next lookup of this widget finds.
	FWidgetAnimTransformLookup& Lookup = Result.TransformLookup;
	if (Num >= LookupThreshold && Lookup.Indexed == Num)
	{
		Lookup.Index.Add(FWidgetAnimTransformKey{ TargetWidget, Part }, Num);
		Lookup.Indexed = Num + 1;
	}
	return NewEntry;
}

namespace
{
	/**
	 * The slot holding TargetBox's outline for this frame, created if this is the first layer to write it.
	 *
	 * Unlike the transform slot, whatever the caller assigns here REPLACES what was there: shapes do not
	 * add or multiply, so the last layer to write a box wins.
	 */
	FWidgetAnimShapeTarget& FindOrAddShapeTarget(FWidgetAnimResult& Result, UShapeBox* TargetBox)
	{
		for (FWidgetAnimShapeTarget& Existing : Result.ShapeTargets)
		{
			if (Existing.TargetBox == TargetBox)
			{
				return Existing;
			}
		}

		FWidgetAnimShapeTarget& NewEntry = Result.ShapeTargets.AddDefaulted_GetRef();
		NewEntry.TargetBox = TargetBox;
		return NewEntry;
	}

	/**
	 * The slot for a registered track's write to TargetWidget's Channel this frame, created if this is the first
	 * track to write it. Like a shape, whatever the caller puts here REPLACES what was there - see
	 * EWidgetAnimComposition::Authoritative. Keyed on the widget the track was played on, not the object its
	 * evaluator narrows that to, so a null target (the fallback widget) is matched the way transforms match it.
	 */
	FWidgetAnimAuthoritativeWrite* FindAuthoritativeWrite(FWidgetAnimResult& Result, const UWidget* TargetWidget, FName Channel)
	{
		return Result.AuthoritativeWrites.FindByPredicate([TargetWidget, Channel](const FWidgetAnimAuthoritativeWrite& Write)
		{
			return Write.TargetWidget == TargetWidget && Write.Channel == Channel;
		});
	}

	FWidgetAnimAuthoritativeWrite& FindOrAddAuthoritativeWrite(FWidgetAnimResult& Result, UWidget* TargetWidget, FName Channel)
	{
		if (FWidgetAnimAuthoritativeWrite* Existing = FindAuthoritativeWrite(Result, TargetWidget, Channel))
		{
			return *Existing;
		}

		FWidgetAnimAuthoritativeWrite& NewEntry = Result.AuthoritativeWrites.AddDefaulted_GetRef();
		NewEntry.TargetWidget = TargetWidget;
		NewEntry.Channel = Channel;
		return NewEntry;
	}

	/**
	 * Puts a registered track's channel back at its captured baseline this frame - unless some track has already
	 * written that channel, whose write stands; and one that writes it later still replaces this. The registered-type
	 * half of TouchLayerTargets. Whether a baseline was actually captured is only asked when the result is applied.
	 */
	void AddAuthoritativeBaselineWrite(FWidgetAnimResult& Result, UWidget* TargetWidget, FName Channel, const FWidgetAnimEvaluator* Evaluator, const FInstancedStruct* Params)
	{
		if (!Evaluator || FindAuthoritativeWrite(Result, TargetWidget, Channel))
		{
			return;
		}

		FWidgetAnimAuthoritativeWrite& Write = FindOrAddAuthoritativeWrite(Result, TargetWidget, Channel);
		Write.Evaluator = Evaluator;
		Write.Params = Params;
		Write.bIsBaseline = true;
	}

	/**
	 * What was last applied to each target's channel by a quantized write - see FWidgetAnimEvaluator::GetQuantizeStep -
	 * so a write that would show exactly what is already showing is skipped. That is what keeps a Blueprint type from
	 * running Blueprint on every frame, and a Count from formatting the same number sixty times a second.
	 *
	 * Keyed by the resolved target and channel. An entry is only trusted when the same evaluator and the same settings
	 * wrote it; anything else writing that channel - a baseline put back, an unquantized write - drops the entry, so
	 * the next quantized write always goes through.
	 */
	struct FLastAuthoritativeWrite
	{
		const FWidgetAnimEvaluator* Evaluator = nullptr;
		const void* ParamsIdentity = nullptr;
		int64 Step = 0;

		/** The exact value last handed to Apply. */
		double AppliedValue = 0.0;

		/** The exact value the last write carried, applied or not - how a value that has stopped moving is noticed. */
		double SeenValue = 0.0;
	};

	using FAuthoritativeWriteKey = TPair<FObjectKey, FName>;

	TMap<FAuthoritativeWriteKey, FLastAuthoritativeWrite>& GetLastAuthoritativeWrites()
	{
		static TMap<FAuthoritativeWriteKey, FLastAuthoritativeWrite> LastWrites;
		return LastWrites;
	}

	void ForgetAuthoritativeWrite(const UObject* Target, FName Channel)
	{
		GetLastAuthoritativeWrites().Remove(FAuthoritativeWriteKey(FObjectKey(Target), Channel));
	}

	/** Records what a registered track's target holds in its channel, if nothing has recorded it already. */
	void CaptureOneAuthoritative(FWidgetAnimBaselineSet& Baselines, const FWidgetAnimEvaluator& Evaluator, const FInstancedStruct& Params, UWidget* Widget)
	{
		UObject* Target = Evaluator.ResolveTarget(Widget, Params);
		if (!Target)
		{
			return;
		}

		// Once per target and channel, whichever track and type gets there first: a second capture would read back
		// what an animation had already written.
		const FName Channel = Evaluator.GetChannel(Params);
		if (Baselines.FindAuthoritativeBaseline(Target, Channel))
		{
			return;
		}

		FInstancedStruct Captured;
		if (!Evaluator.CaptureBaseline(Target, Params, Captured))
		{
			return;
		}

		FWidgetAnimAuthoritativeBaseline& Baseline = Baselines.AuthoritativeBaselines.AddDefaulted_GetRef();
		Baseline.Target = Target;
		Baseline.Channel = Channel;
		// Reflected as a plain object pointer; the struct is never written through it.
		Baseline.CapturedBy = const_cast<UScriptStruct*>(Evaluator.GetParamsStruct());
		Baseline.Captured = MoveTemp(Captured);
	}

	/**
	 * One live write onto its target, through its evaluator - with what the target held before it was animated, when this
	 * type captured that, for a type that reads it (a Shape track's Use Authored Shape).
	 */
	void ApplyWrite(const FWidgetAnimAuthoritativeWrite& Write, UObject* Target, const FInstancedStruct& Params, const FWidgetAnimBaselineSet* Baselines)
	{
		const FWidgetAnimAuthoritativeBaseline* Baseline = Baselines ? Baselines->FindAuthoritativeBaseline(Target, Write.Channel) : nullptr;
		const bool bOwnCapture = Baseline && Baseline->CapturedBy.Get() == Write.Evaluator->GetParamsStruct();
		Write.Evaluator->ApplyWrite(Target, Params, Write, bOwnCapture ? &Baseline->Captured : nullptr);
	}

	/**
	 * Every registered-type write in the result, onto its target. Live writes go through their evaluator; a baseline
	 * write puts back what was captured - by whichever type captured it - and does nothing when nothing was.
	 */
	void ApplyAuthoritativeWrites(const FWidgetAnimResult& Result, UWidget* FallbackWidget, const FWidgetAnimBaselineSet* Baselines)
	{
		for (const FWidgetAnimAuthoritativeWrite& Write : Result.AuthoritativeWrites)
		{
			if (!Write.Evaluator)
			{
				continue;
			}

			const FInstancedStruct& Params = Write.GetParams();
			UObject* Target = Write.Evaluator->ResolveTarget(Write.TargetWidget ? Write.TargetWidget : FallbackWidget, Params);
			if (!Target)
			{
				continue;
			}

			TMap<FAuthoritativeWriteKey, FLastAuthoritativeWrite>& LastWrites = GetLastAuthoritativeWrites();
			const FAuthoritativeWriteKey Key(FObjectKey(Target), Write.Channel);

			if (!Write.bIsBaseline)
			{
				const double QuantizeStep = Write.Evaluator->GetQuantizeStep(Params);
				if (QuantizeStep > 0.0)
				{
					// Rounded the way a shown number is, half away from zero, so the step changes exactly when the
					// text would.
					const int64 Step = static_cast<int64>(FMath::RoundHalfFromZero(Write.Value / QuantizeStep));
					FLastAuthoritativeWrite* Last = LastWrites.Find(Key);
					if (Last && Last->Evaluator == Write.Evaluator && Last->ParamsIdentity == &Params && Last->Step == Step)
					{
						// Same step as what is showing - but the step only says when to apply while the value is on
						// its way. Where it comes to rest must be shown exactly: a 0-to-1 fill with a step of 0.05
						// last applied at 0.975 would otherwise sit at 0.975 for good, because 1.0 rounds to the same
						// step. So the exact value still goes through, once, when it lands on the track's start or
						// end, or stops moving anywhere else - a Hold key, a paused scrub, a held end pose, a spring
						// settling. Everything in between is still skipped.
						const bool bAtEndpoint = Write.Value == Write.Evaluator->GetEndpointValue(Params, /*bStart=*/false)
							|| Write.Value == Write.Evaluator->GetEndpointValue(Params, /*bStart=*/true);
						const bool bStoppedMoving = Write.Value == Last->SeenValue;
						Last->SeenValue = Write.Value;

						if (Write.Value == Last->AppliedValue || !(bAtEndpoint || bStoppedMoving))
						{
							continue;
						}

						ApplyWrite(Write, Target, Params, Baselines);
						Last->AppliedValue = Write.Value;
						continue;
					}

					ApplyWrite(Write, Target, Params, Baselines);

					// Entries for targets that are gone are dropped once the cache is large - not the whole cache, which with
					// thousands of animated texts or Blueprint types emptied it every frame, and with it the point of it: every
					// one of them was applied, Blueprint and all, every frame.
					static int32 PruneAt = 4096;
					if (LastWrites.Num() > PruneAt)
					{
						for (auto It = LastWrites.CreateIterator(); It; ++It)
						{
							if (!It->Key.Key.ResolveObjectPtr())
							{
								It.RemoveCurrent();
							}
						}
						PruneAt = FMath::Max(4096, LastWrites.Num() * 2);
					}
					FLastAuthoritativeWrite& Entry = LastWrites.FindOrAdd(Key);
					Entry.Evaluator = Write.Evaluator;
					Entry.ParamsIdentity = &Params;
					Entry.Step = Step;
					Entry.AppliedValue = Write.Value;
					Entry.SeenValue = Write.Value;
				}
				else
				{
					ApplyWrite(Write, Target, Params, Baselines);
					LastWrites.Remove(Key);
				}
				continue;
			}

			// Whatever is put back here is not what any cached write showed.
			LastWrites.Remove(Key);

			const FWidgetAnimAuthoritativeBaseline* Baseline = Baselines ? Baselines->FindAuthoritativeBaseline(Target, Write.Channel) : nullptr;
			const FWidgetAnimEvaluator* Capturer = Baseline ? FWidgetAnimEvaluatorRegistry::Get().Find(Baseline->CapturedBy.Get()) : nullptr;
			if (Capturer)
			{
				Capturer->RestoreBaseline(Target, Baseline->Captured);
			}
		}
	}
}

namespace
{
	/**
	 * What tells a Shake's items apart: each widget by its name, each letter by its place - so a Shake staggered over a
	 * panel's children or a text's letters doesn't move every item as one.
	 */
	int32 GetShakeItemSeed(const FWidgetAnimSettings& Settings)
	{
		uint32 Seed = 0;
		if (Settings.Part.IsSet())
		{
			Seed = HashCombine(GetTypeHash(Settings.Part.Unit), GetTypeHash(Settings.Part.Index + 1));
		}
		if (Settings.TargetWidget)
		{
			Seed = HashCombine(Seed, GetTypeHash(Settings.TargetWidget->GetFName()));
		}
		return static_cast<int32>(Seed & 0x7fffffff);
	}
}

void UWidgetAnimLibrary::ApplyAnimationContribution(const FWidgetAnimSettings& Settings, float DirectedAlpha, FWidgetAnimResult& OutResult, const FWidgetAnimBaselineSet* Baselines, bool bTowardStart)
{
	// Every type that travels from a start value to an end value does it the same way: through its points -
	// start, any keys, end - one segment at a time. See FWidgetAnimValueKeys and SampleValueKeys, which is also
	// what makes the very start and the very end exact.
	int32 Segment = 0;
	float SegmentAlpha = 0.f;
	if (WidgetAnimTypeSupportsValueKeys(Settings.AnimType))
	{
		SampleValueKeys(Settings, DirectedAlpha, bTowardStart, Segment, SegmentAlpha);
	}

	auto LerpValue = [&Settings, &Segment, &SegmentAlpha]()
	{
		return FMath::Lerp(WidgetAnimKeyPoints::GetValue(Settings, Segment), WidgetAnimKeyPoints::GetValue(Settings, Segment + 1), SegmentAlpha);
	};

	switch (Settings.AnimType)
	{
	case EWidgetAnimType::Fade:
	{
		FWidgetAnimTargetedTransform& Transform = FindOrAddTargetedTransform(OutResult, Settings.TargetWidget, Settings.Part);

		// Held inside what opacity can mean. A spring (or a custom curve) overshooting a fade to 1 would
		// otherwise ask for 1.1, and one overshooting a fade to 0 for less than nothing; neither is a look,
		// only a glitch. The values themselves are left alone, so an opacity authored above 1 still reaches it.
		const float Opacity = LerpValue();
		Transform.Opacity *= FMath::Clamp(Opacity, 0.f, FMath::Max(1.f, WidgetAnimKeyPoints::GetMaxValue(Settings)));
		break;
	}

	case EWidgetAnimType::Scale:
	{
		// X and Y apart, each between its own points - the same segment, the same way, so they arrive together.
		float ScaleX = LerpValue();
		float ScaleY = FMath::Lerp(WidgetAnimKeyPoints::GetValueY(Settings, Segment), WidgetAnimKeyPoints::GetValueY(Settings, Segment + 1), SegmentAlpha);

		// A scale that overshoots past 0 on its way to (or back to) nothing turns the widget inside out - a
		// negative scale is a mirror image - so a spring's bounce near 0 flipped it for a frame. Held at 0 unless
		// the track itself asks for a negative scale somewhere, which is a mirror on purpose.
		if (WidgetAnimKeyPoints::GetMinValue(Settings) >= 0.f)
		{
			ScaleX = FMath::Max(ScaleX, 0.f);
			ScaleY = FMath::Max(ScaleY, 0.f);
		}

		FWidgetAnimTargetedTransform& Transform = FindOrAddTargetedTransform(OutResult, Settings.TargetWidget, Settings.Part);
		Transform.Scale *= FVector2D(ScaleX, ScaleY);
		break;
	}

	case EWidgetAnimType::Slide:
	{
		// A Slide's points are how far along its direction the widget is: its Distance at the start, 0 at the end.
		const FVector2D Offset = GetSlideDirectionOffset(Settings.SlideFromDirection) * LerpValue();
		FWidgetAnimTargetedTransform& Transform = FindOrAddTargetedTransform(OutResult, Settings.TargetWidget, Settings.Part);
		Transform.Translation += Offset;
		break;
	}

	case EWidgetAnimType::Rotate:
	{
		FWidgetAnimTargetedTransform& Transform = FindOrAddTargetedTransform(OutResult, Settings.TargetWidget, Settings.Part);
		Transform.Angle += LerpValue();
		break;
	}

	case EWidgetAnimType::Pulse:
	{
		// Exactly 1 at both ends: the sine only gets back to 0 to within float error, which left a pulsed widget
		// a hair off its size once it stopped.
		const float Phase = DirectedAlpha * 2.f * PI;
		const float ScaleMultiplier = (DirectedAlpha <= 0.f || DirectedAlpha >= 1.f) ? 1.f : 1.f + Settings.PulseScaleAmount * FMath::Sin(Phase);
		FWidgetAnimTargetedTransform& Transform = FindOrAddTargetedTransform(OutResult, Settings.TargetWidget, Settings.Part);
		Transform.Scale *= FVector2D(ScaleMultiplier, ScaleMultiplier);
		break;
	}

	case EWidgetAnimType::Shake:
	{
		// Time into its run rather than the eased alpha: the noise is paced by Shakes Per Second, and bending time would
		// only speed it up and slow it down. Strength follows its Falloff, and is exactly nothing at either end, so a
		// Shake lands where it started.
		const float Alpha = FMath::Clamp(DirectedAlpha, 0.f, 1.f);
		FVector2D Offset;
		float Angle = 0.f;
		Settings.Shake.Sample(Alpha * FMath::Max(Settings.Duration, KINDA_SMALL_NUMBER), Settings.Shake.GetEnvelope(Alpha), GetShakeItemSeed(Settings), Offset, Angle);
		FWidgetAnimTargetedTransform& Transform = FindOrAddTargetedTransform(OutResult, Settings.TargetWidget, Settings.Part);
		Transform.Translation += Offset;
		Transform.Angle += Angle;
		break;
	}

	case EWidgetAnimType::MaterialScalar:
	{
		if (!Settings.ScalarParameterName.IsNone())
		{
			FWidgetAnimScalarTarget& Target = OutResult.FindOrAddScalar(Settings.GetMaterialWidget(), Settings.MaterialSlot, Settings.ScalarParameterName);
			Target.Value = LerpValue();
			Target.bIsBaseline = false;
		}
		break;
	}

	case EWidgetAnimType::MaterialVector:
	{
		if (!Settings.VectorParameterName.IsNone())
		{
			const FLinearColor Value = FMath::Lerp(WidgetAnimKeyPoints::GetColor(Settings, Segment), WidgetAnimKeyPoints::GetColor(Settings, Segment + 1), SegmentAlpha);
			FWidgetAnimVectorTarget& Target = OutResult.FindOrAddVector(Settings.GetMaterialWidget(), Settings.MaterialSlot, Settings.VectorParameterName);
			Target.Value = Value;
			Target.bIsBaseline = false;
		}
		break;
	}

	case EWidgetAnimType::Transform:
	case EWidgetAnimType::ShapeBox:
	{
		// Keyed like every other keyed layer: plain time finds the segment, so each key is reached exactly when
		// the timeline shows it, and each segment is shaped by its key's Curve To Next Key - the layer's Easing,
		// for a key left at Track Easing (straight, on a value-driven track). The last key is the layer's length. The layer's Delay was already
		// consumed by the clock before DirectedAlpha reached us, and inversion is folded into DirectedAlpha, so a
		// reversed layer just walks this time backwards. Exactly the first and last keys' times at either end.
		const float Duration = Settings.Transform.GetDuration();
		auto EaseSegment = [&Settings](float Alpha) { return EvaluateEasedAlpha(Settings, Alpha); };
		const TFunctionRef<float(float)> TrackEase(EaseSegment);

		// Null unless a baseline captured this Shape Box's authored outline, which is exactly when a key's
		// Use Authored Shape has something to mean.
		const bool bShapeBox = Settings.AnimType == EWidgetAnimType::ShapeBox;
		const FShapeBoxShape* AuthoredShape = bShapeBox && Baselines ? Baselines->FindShapeBaseline(Settings.ShapeTargetBox) : nullptr;

		// A spring fitted to the keys: it runs the track's length, its progress walks the keys - each segment straight,
		// the spring being the motion, as a spring track's value keys are points along its travel - and its overshoot
		// carries on past the last key (or before the first, heading back) instead of holding there.
		const bool bSpring = Settings.IsSpringTimed();

		// Easing Across Whole Track: the Easing spans the run, as a spring does - eased time walks the keys, each stretch
		// straight in time, and an overshooting Easing carries on past the last key (or before the first).
		const bool bWholeTrack = !bSpring && Settings.Transform.EasingSpan == EWidgetKeyEasingSpan::WholeTrack;
		const float LocalTime = bSpring
			? UWidgetAnimSpringLibrary::EvaluateDirectedProgress(Settings.Spring, FMath::Clamp(DirectedAlpha, 0.f, 1.f), bTowardStart) * Duration
			: bWholeTrack
				? EvaluateEasedAlpha(Settings, FMath::Clamp(DirectedAlpha, 0.f, 1.f)) * Duration
				: FMath::Clamp(DirectedAlpha, 0.f, 1.f) * Duration;

		const FWidgetTransformAnimResult Contribution = UWidgetAnimTransformLibrary::Evaluate(Settings.Transform,
			bShapeBox ? EWidgetKeyframeContent::ShapeBox : EWidgetKeyframeContent::Transform, LocalTime, AuthoredShape, (bSpring || bWholeTrack) ? nullptr : &TrackEase, /*bExtrapolate=*/bSpring || bWholeTrack);

		// A Shape Box track moves the outline only, never the widget.
		if (!bShapeBox)
		{
			FWidgetAnimTargetedTransform& Transform = FindOrAddTargetedTransform(OutResult, Settings.TargetWidget, Settings.Part);
			Transform.Translation += Contribution.Translation;
			Transform.Scale       *= Contribution.Scale;
			Transform.Shear       += Contribution.Shear;
			Transform.Angle       += Contribution.Angle;
		}

		// Assigned, not accumulated - see FindOrAddShapeTarget. A null box is kept rather than dropped: it
		// means "no target named", and is resolved against the fallback widget when the result is applied,
		// exactly as a transform with no target is. Dropping it here is what made a Shape Box track do nothing on
		// every entry left pointing at its own widget.
		// A letter has no outline of its own: the shape belongs to the widget, and is left to its unstaggered tracks.
		if (Contribution.bHasShape && !Settings.Part.IsSet())
		{
			FWidgetAnimShapeTarget& ShapeTarget = FindOrAddShapeTarget(OutResult, Settings.ShapeTargetBox);
			ShapeTarget.Shape = Contribution.Shape;
			ShapeTarget.bIsBaseline = false;
		}
		break;
	}

	case EWidgetAnimType::Shape:
	case EWidgetAnimType::Design:
	{
		// Keyed and timed exactly as a Transform track is; what its keys hold - a shape's settings, or a widget's look - is
		// blended where it is applied, onto the widget, through the authoritative channel registered types write through:
		// the key it is leaving and how far toward the next it is. See WidgetAnimShapeTrack.h and WidgetAnimDesignTrack.h.
		// A letter has no shape or design of its own.
		const bool bDesign = Settings.AnimType == EWidgetAnimType::Design;
		const FWidgetAnimEvaluator* Evaluator = bDesign ? WidgetAnimDesignTrack::FindEvaluator() : WidgetAnimShapeTrack::FindEvaluator();
		const bool bHasKeys = bDesign
			? Settings.TypeParams.GetPtr<FWidgetAnimDesignKeysParams>() != nullptr
			: Settings.TypeParams.GetPtr<FWidgetAnimShapeKeysParams>() != nullptr;
		if (!Evaluator || Settings.Part.IsSet() || !bHasKeys)
		{
			break;
		}

		const float Duration = Settings.Transform.GetDuration();
		auto EaseSegment = [&Settings](float Alpha) { return EvaluateEasedAlpha(Settings, Alpha); };
		const TFunctionRef<float(float)> TrackEase(EaseSegment);
		const bool bSpring = Settings.IsSpringTimed();
		const bool bWholeTrack = !bSpring && Settings.Transform.EasingSpan == EWidgetKeyEasingSpan::WholeTrack;
		const float LocalTime = bSpring
			? UWidgetAnimSpringLibrary::EvaluateDirectedProgress(Settings.Spring, FMath::Clamp(DirectedAlpha, 0.f, 1.f), bTowardStart) * Duration
			: bWholeTrack
				? EvaluateEasedAlpha(Settings, FMath::Clamp(DirectedAlpha, 0.f, 1.f)) * Duration
				: FMath::Clamp(DirectedAlpha, 0.f, 1.f) * Duration;

		int32 KeySegment = 0;
		float KeyAlpha = 0.f;
		if (UWidgetAnimTransformLibrary::FindSegment(Settings.Transform, LocalTime, (bSpring || bWholeTrack) ? nullptr : &TrackEase, /*bExtrapolate=*/bSpring || bWholeTrack, KeySegment, KeyAlpha) == 0)
		{
			break;
		}

		FWidgetAnimAuthoritativeWrite& Write = FindOrAddAuthoritativeWrite(OutResult, Settings.TargetWidget, Evaluator->GetChannel(Settings.TypeParams));
		Write.Evaluator = Evaluator;
		Write.Params = &Settings.TypeParams;
		Write.Value = KeyAlpha;
		Write.KeySegment = KeySegment;
		Write.bHasColor = false;
		Write.bIsBaseline = false;
		break;
	}

	case EWidgetAnimType::Custom:
	{
		// A registered type: the shared timeline has already found the segment and how far across it the track is,
		// so all that is left is the value there and who writes it. An unregistered type - its module not loaded -
		// plays as nothing rather than failing.
		const FWidgetAnimEvaluator* Evaluator = Settings.FindEvaluator();

		// A cue has no pose: it fires as the clock passes its points, which TickAnimationsInto finds - see
		// WidgetAnimCue.h. At any one alpha - a scrub, a held pose - there is nothing to write.
		if (!Evaluator || Evaluator->IsCue())
		{
			break;
		}

		// At full precision, not through WidgetAnimKeyPoints' floats: a count to 20,000,000 must land on it exactly.
		// The keys between are floats like every track's keys.
		auto PointValue = [&Settings, Evaluator](int32 Point) -> double
		{
			const int32 NumKeys = Settings.ValueKeys.Keys.Num();
			if (Point > 0 && Point <= NumKeys)
			{
				return Settings.ValueKeys.Keys[Point - 1].Value;
			}
			return Evaluator->GetEndpointValue(Settings.TypeParams, /*bStart=*/Point <= 0);
		};

		// Exactly the point itself at either end of a segment, not a lerp that lands a rounding error off it: a track
		// at its very end must carry exactly its End Value, which is what lets a quantized write recognize it has
		// landed and show it (see ApplyAuthoritativeWrites). Only exactly 0 and 1: a spring's alpha running past
		// either end is an overshoot, and still lerps out beyond the point.
		const double RegisteredValue = SegmentAlpha == 0.f ? PointValue(Segment)
			: SegmentAlpha == 1.f ? PointValue(Segment + 1)
			: FMath::Lerp(PointValue(Segment), PointValue(Segment + 1), static_cast<double>(SegmentAlpha));

		switch (Evaluator->GetComposition())
		{
		case EWidgetAnimComposition::Authoritative:
		{
			// Assigned, not accumulated: a later track writing the same channel of the same target replaces this.
			// Points at the settings rather than copying them - see FWidgetAnimAuthoritativeWrite.
			FWidgetAnimAuthoritativeWrite& Write = FindOrAddAuthoritativeWrite(OutResult, Settings.TargetWidget, Evaluator->GetChannel(Settings.TypeParams));
			Write.Evaluator = Evaluator;
			Write.Params = &Settings.TypeParams;
			Write.Value = RegisteredValue;
			Write.KeySegment = INDEX_NONE;
			Write.bIsBaseline = false;

			// Keys holding colors: blended between the same two points, the same way, exact at either end.
			Write.bHasColor = Evaluator->GetKeyContent(Settings.TypeParams) != EWidgetAnimKeyContent::Value;
			if (Write.bHasColor)
			{
				const FLinearColor From = WidgetAnimKeyPoints::GetColor(Settings, Segment);
				const FLinearColor To = WidgetAnimKeyPoints::GetColor(Settings, Segment + 1);
				Write.Color = SegmentAlpha == 0.f ? From : SegmentAlpha == 1.f ? To : FMath::Lerp(From, To, SegmentAlpha);
			}
			break;
		}

		case EWidgetAnimComposition::Cue:
			break;
		}
		break;
	}
	}
}

bool UWidgetAnimLibrary::GetEffectiveInvert(const FWidgetAnimSettings& Settings, bool bIsOutAnimation)
{
	return Settings.bInvertAnimation != bIsOutAnimation;
}

void UWidgetAnimLibrary::TouchLayerTargets(const FWidgetAnimSettings& Settings, FWidgetAnimResult& OutResult, const FWidgetAnimBaselineSet& Baselines)
{
	// A material layer that hasn't started puts its parameter back at what the material held before it was animated - so
	// a scrub back before it starts shows the material as designed - unless another track writes it this frame.
	if (Settings.AnimType == EWidgetAnimType::MaterialScalar || Settings.AnimType == EWidgetAnimType::MaterialVector)
	{
		const bool bVector = Settings.AnimType == EWidgetAnimType::MaterialVector;
		const FName Parameter = bVector ? Settings.VectorParameterName : Settings.ScalarParameterName;
		UWidget* MaterialWidget = Settings.GetMaterialWidget();
		const FWidgetAnimMaterialBaseline* Baseline = Parameter.IsNone() ? nullptr : Baselines.FindMaterialBaseline(MaterialWidget, Settings.MaterialSlot, Parameter, bVector);
		if (!Baseline)
		{
			return;
		}
		if (bVector)
		{
			if (!OutResult.FindVector(MaterialWidget, Settings.MaterialSlot, Parameter))
			{
				FWidgetAnimVectorTarget& Target = OutResult.FindOrAddVector(MaterialWidget, Settings.MaterialSlot, Parameter);
				Target.Value = Baseline->Vector;
				Target.bIsBaseline = true;
			}
		}
		else
		{
			if (!OutResult.FindScalar(MaterialWidget, Settings.MaterialSlot, Parameter))
			{
				FWidgetAnimScalarTarget& Target = OutResult.FindOrAddScalar(MaterialWidget, Settings.MaterialSlot, Parameter);
				Target.Value = Baseline->Scalar;
				Target.bIsBaseline = true;
			}
		}
		return;
	}

	// A registered type doesn't touch the transform at all - it puts its own channel back at its baseline instead. A cue
	// has neither, and touches nothing.
	if (Settings.AnimType == EWidgetAnimType::Custom)
	{
		const FWidgetAnimEvaluator* Evaluator = Settings.FindEvaluator();
		if (Evaluator && !Evaluator->IsCue())
		{
			AddAuthoritativeBaselineWrite(OutResult, Settings.TargetWidget, Evaluator->GetChannel(Settings.TypeParams), Evaluator, &Settings.TypeParams);
		}
		return;
	}

	// A Shape or Design track the same way: its shape's settings, or its widget's look, back as they were authored, the
	// transform untouched.
	if (Settings.AnimType == EWidgetAnimType::Shape || Settings.AnimType == EWidgetAnimType::Design)
	{
		const FWidgetAnimEvaluator* Evaluator = Settings.AnimType == EWidgetAnimType::Design ? WidgetAnimDesignTrack::FindEvaluator() : WidgetAnimShapeTrack::FindEvaluator();
		if (Evaluator && !Settings.Part.IsSet())
		{
			AddAuthoritativeBaselineWrite(OutResult, Settings.TargetWidget, Evaluator->GetChannel(Settings.TypeParams), Evaluator, &Settings.TypeParams);
		}
		return;
	}

	// The identity: multiplies and adds nothing, so a layer that IS contributing to the same widget this frame
	// is untouched by it - but the widget is written, at its baseline, even when nothing else is. Over baselines that are
	// only put back, the identity would be written as it is - a widget authored at half opacity shown at full - so the
	// widget is noted instead, and written at its captured pose unless a track poses it. A part is never captured: at rest
	// it is where its widget lays it out, which is the identity either way.
	// A Shape Box track poses no transform, so it touches none - only its outline, below.
	if (Settings.AnimType != EWidgetAnimType::ShapeBox)
	{
		if (!Baselines.bComposeTransforms && !Settings.Part.IsSet())
		{
			OutResult.RestingWidgets.Add(Settings.TargetWidget.Get());
		}
		else
		{
			FindOrAddTargetedTransform(OutResult, Settings.TargetWidget, Settings.Part);
		}
	}

	// A shape is assigned rather than composed, so the authored outline only goes in where no other layer has
	// put one this frame - and a later layer that does still replaces it.
	if (Settings.AnimType == EWidgetAnimType::ShapeBox && !Settings.Part.IsSet())
	{
		const bool bAlreadyWritten = OutResult.ShapeTargets.ContainsByPredicate([&Settings](const FWidgetAnimShapeTarget& Target)
		{
			return Target.TargetBox == Settings.ShapeTargetBox;
		});

		if (!bAlreadyWritten)
		{
			if (const FShapeBoxShape* Authored = Baselines.FindShapeBaseline(Settings.ShapeTargetBox))
			{
				FWidgetAnimShapeTarget& ShapeTarget = FindOrAddShapeTarget(OutResult, Settings.ShapeTargetBox);
				ShapeTarget.Shape = *Authored;
				ShapeTarget.bIsBaseline = true;
			}
		}
	}
}

namespace WidgetAnimLoop
{
	/** Where one layer sits on its animation's timeline, in seconds of track time. Speed already applied. */
	struct FLayerSpan
	{
		float Start = 0.f;
		float Length = KINDA_SMALL_NUMBER;
	};

	/**
	 * How long a layer runs. A keyframed layer's is its curve's last key, a Spring layer's is its spring's
	 * settle time; for both, the Duration field is hidden and ignored.
	 *
	 * A spring's settle time is worked out from its parameters on every call rather than cached - a handful
	 * of logs and square roots, and only a critically damped spring searches - so there is no stored copy to
	 * go stale when a spring is edited mid-preview.
	 */
	static float GetEffectiveLayerDuration(const FWidgetAnimSettings& Settings)
	{
		if (WidgetAnimTypeIsKeyframed(Settings.AnimType))
		{
			return Settings.Transform.GetDuration();
		}

		if (Settings.IsSpringTimed())
		{
			return UWidgetAnimSpringLibrary::GetSettleTime(Settings.Spring);
		}

		return Settings.Duration;
	}

	/**
	 * How far through its span a layer is at LocalTime seconds into it, 0 to 1 - exactly 0 or 1 within a
	 * hair of either end. A layer's end is worked out as start plus length and read back as time minus start,
	 * and in floats that round trip lands a whisker short of 1 about as often as not - which is a pose a
	 * whisker short of the end, held for good once the clock stops there.
	 */
	static float GetLayerAlpha(float LocalTime, const FLayerSpan& Span)
	{
		const float Alpha = FMath::Clamp(LocalTime / Span.Length, 0.f, 1.f);
		constexpr float Snap = 1.e-5f;
		return Alpha >= 1.f - Snap ? 1.f : (Alpha <= Snap ? 0.f : Alpha);
	}

	static FLayerSpan GetLayerSpan(const FWidgetAnimSettings& Settings)
	{
		// Speed scales the layer's whole placement, delay included, so a stagger keeps its proportions - and a layer of an
		// inserted animation plays at that animation's own pace on top.
		const float Speed = Settings.GetPlaySpeed();

		FLayerSpan Span;
		Span.Start = FMath::Max(Settings.Delay, 0.f) / Speed;
		// A Transform layer's length comes from its curve, a Spring layer's from its spring, rather than Duration.
		Span.Length = FMath::Max(GetEffectiveLayerDuration(Settings), KINDA_SMALL_NUMBER) / Speed;
		return Span;
	}

	/**
	 * The same leader/group assignment TickAnimationsInto computes inline, factored out for the duration
	 * helpers below so the two can't drift apart. TickAnimationsInto keeps its own inline copy rather than
	 * calling this: it runs every frame, and its TInlineAllocator arrays are worth leaving untouched.
	 */
	static void ComputeGroupSpans(
		const TArray<FWidgetAnimSettings>& Animations,
		TArray<int32, TInlineAllocator<16>>& OutLeaderOf,
		TArray<FLayerSpan, TInlineAllocator<16>>& OutSpans,
		TArray<float, TInlineAllocator<16>>& OutGroupLead,
		TArray<float, TInlineAllocator<16>>& OutGroupEnd)
	{
		const int32 NumLayers = Animations.Num();
		OutLeaderOf.SetNum(NumLayers);
		OutSpans.SetNum(NumLayers);
		OutGroupLead.Init(TNumericLimits<float>::Max(), NumLayers);
		OutGroupEnd.Init(0.f, NumLayers);

		// Each group's leader remembered as it is met, as TickAnimationsInto does: looking back over every earlier layer for
		// it cost the square of the layer count - and the designer's readouts ask this several times a frame.
		TArray<TPair<int32, int32>, TInlineAllocator<8>> GroupLeaders;
		for (int32 Index = 0; Index < NumLayers; ++Index)
		{
			OutLeaderOf[Index] = Index;

			const int32 Group = Animations[Index].LoopGroup;
			if (Group != INDEX_NONE)
			{
				const TPair<int32, int32>* Known = GroupLeaders.FindByPredicate([Group](const TPair<int32, int32>& Pair) { return Pair.Key == Group; });
				if (Known)
				{
					OutLeaderOf[Index] = Known->Value;
				}
				else
				{
					GroupLeaders.Emplace(Group, Index);
				}
			}

			OutSpans[Index] = GetLayerSpan(Animations[Index]);

			const int32 Leader = OutLeaderOf[Index];
			OutGroupLead[Leader] = FMath::Min(OutGroupLead[Leader], OutSpans[Index].Start);
			OutGroupEnd[Leader] = FMath::Max(OutGroupEnd[Leader], OutSpans[Index].Start + OutSpans[Index].Length);
		}
	}

	/**
	 * Analytic version of what AdvanceTimeline walks toward frame by frame: how long a group's clock takes
	 * to come to rest, in the same track-timeline seconds AdvanceTimeline measures CycleTime in. -1 if it
	 * never rests on its own (IsEndlessLoop).
	 *
	 * The first leg always runs the group's whole 0..End span, lead-in included; every following leg is a
	 * shorter Lead..End trip, and with Ping Pong a full extra "play" after the first is a forward-back
	 * pair of those trips - mirrors ShouldStartAnotherLeg exactly.
	 */
	static float GetGroupDuration(const FWidgetAnimSettings& Settings, float Lead, float End)
	{
		if (!Settings.bLoop)
		{
			return End;
		}

		if (UWidgetAnimLibrary::IsEndlessLoop(Settings))
		{
			return -1.f;
		}

		const int32 ExtraPlays = FMath::Max(Settings.LoopCount - 1, 0);
		const float ReturnLegLength = End - Lead;

		return Settings.bPingPong
			? End + ExtraPlays * 2.f * ReturnLegLength
			: End + ExtraPlays * ReturnLegLength;
	}

	/**
	 * Cumulative seconds a leader's clock has actually played, across every completed leg and the one in
	 * progress - unlike Clock.CycleTime itself, which AdvanceTimeline resets to Lead (or bounces between
	 * Lead and End with Ping Pong) at the start of every leg after the first, so read alone it never grows
	 * past one leg's length no matter how many legs have played.
	 */
	static float GetGroupElapsed(const FWidgetAnimSettings& Settings, const FWidgetAnimState& Clock, float Lead, float End)
	{
		if (!Settings.bLoop)
		{
			return Clock.CycleTime;
		}

		if (Clock.bFinished)
		{
			return GetGroupDuration(Settings, Lead, End);
		}

		if (Clock.LoopsCompleted <= 0)
		{
			// Still in the first leg, which runs the whole 0..End span - CycleTime already is cumulative.
			return Clock.CycleTime;
		}

		// Every leg after the first is a Lead..End trip, whichever direction it runs, so each one adds
		// exactly (End - Lead) seconds once it completes.
		const float ReturnLegLength = End - Lead;
		const float PriorCumulative = End + (Clock.LoopsCompleted - 1) * ReturnLegLength;
		const float CurrentLegProgress = Clock.bPlayingForward ? (Clock.CycleTime - Lead) : (End - Clock.CycleTime);

		return PriorCumulative + FMath::Max(CurrentLegProgress, 0.f);
	}

	/**
	 * The leader whose GetGroupDuration is the largest - or, if any group is endless, the first such group
	 * found - matching how GetStackDuration reduces a whole stack down to one number. OutDuration carries
	 * that leader's own duration back out (-1 for endless). INDEX_NONE, with OutDuration left at 0, for an
	 * empty stack.
	 */
	static int32 FindDrivingLeader(
		const TArray<FWidgetAnimSettings>& Animations,
		const TArray<int32, TInlineAllocator<16>>& LeaderOf,
		const TArray<float, TInlineAllocator<16>>& GroupLead,
		const TArray<float, TInlineAllocator<16>>& GroupEnd,
		float& OutDuration)
	{
		OutDuration = 0.f;
		int32 Driving = INDEX_NONE;

		for (int32 Index = 0; Index < Animations.Num(); ++Index)
		{
			if (LeaderOf[Index] != Index)
			{
				continue;
			}

			const float GroupDuration = GetGroupDuration(Animations[Index], GroupLead[Index], GroupEnd[Index]);
			if (GroupDuration < 0.f)
			{
				OutDuration = -1.f;
				return Index;
			}

			if (Driving == INDEX_NONE || GroupDuration > OutDuration)
			{
				OutDuration = GroupDuration;
				Driving = Index;
			}
		}

		return Driving;
	}

	/**
	 * Whether the leg that just ended is followed by another.
	 *
	 * Loop Count is how many times the animation plays through. With Ping Pong it plays back between
	 * those plays, so N plays is 2N-1 legs - forward, back, forward, ... - and it always ends on a
	 * forward leg, on its end pose. Counting legs instead is what made an even count settle on the
	 * start pose.
	 */
	static bool ShouldStartAnotherLeg(const FWidgetAnimSettings& Settings, const FWidgetAnimState& Clock, bool bEndedForward)
	{
		if (!Settings.bLoop)
		{
			return false;
		}

		// A return leg is never the last one: whatever ends the loop, it ends on the end pose.
		if (!bEndedForward)
		{
			return true;
		}

		if (Clock.bSettleRequested)
		{
			return false;
		}

		if (Settings.LoopCount <= 0)
		{
			return true;
		}

		// LoopsCompleted counts legs, including the one that just ended.
		const int32 PlaysCompleted = Settings.bPingPong ? (Clock.LoopsCompleted + 1) / 2 : Clock.LoopsCompleted;
		return PlaysCompleted < Settings.LoopCount;
	}

	/**
	 * One stretch of timeline a clock crossed in one advance, going one way - what cues are fired from. A frame that
	 * wraps round a loop, or turns round a ping-pong, crosses several. See WidgetAnimCue.h for the rules these encode.
	 */
	struct FClockPiece
	{
		/** The loop group's first layer, whose clock this is. */
		int32 Leader = INDEX_NONE;

		float From = 0.f;
		float To = 0.f;
		bool bForward = true;

		/**
		 * A forward leg's first stretch: its very start is passed too, not only what lies after it - the animation's first
		 * frame, and every pass of a loop after it.
		 */
		bool bIncludeFrom = false;

		/** That start is where a ping-pong return just turned round. */
		bool bFromIsTurn = false;

		/** A forward leg ending where a ping-pong return begins. */
		bool bToIsTurn = false;

		/** The end of the timeline, where a forward leg ends and a return begins. */
		float LegEnd = 0.f;
	};

	using FClockPieces = TArray<FClockPiece, TInlineAllocator<8>>;

	/**
	 * Advances one animation's shared clock.
	 *
	 * The clock is a position on the animation's timeline, 0 .. End, where End is the latest point any
	 * of its layers finishes. The first play runs the whole timeline. Every later leg runs between Lead
	 * (the earliest any layer starts) and End: the stagger between layers repeats every loop, but a
	 * lead-in shared by all of them is only waited out once, as a single delayed layer always did.
	 *
	 * A ping-pong return leg runs the same span backwards, so the layers unwind in reverse order.
	 *
	 * OutPieces, when given, gets every stretch the clock crossed, in order - for firing cues. Leader names the clock.
	 */
	static void AdvanceTimeline(const FWidgetAnimSettings& Settings, FWidgetAnimState& Clock, float Lead, float End, float DeltaTime, FClockPieces* OutPieces = nullptr, int32 Leader = INDEX_NONE)
	{
		if (Clock.bFinished)
		{
			return;
		}

		Lead = FMath::Clamp(Lead, 0.f, End);
		float Remaining = FMath::Max(DeltaTime, 0.f);

		// The clock's very first advance starts a leg - its first frame passes the start of the timeline itself - and so
		// does every leg after a loop wraps or a ping-pong turns.
		bool bLegStart = !Clock.bCueClockStarted;
		bool bFromTurn = false;
		Clock.bCueClockStarted = true;

		auto Record = [OutPieces, Leader, End, &bLegStart, &bFromTurn](float From, float To, bool bForward, bool bToTurn)
		{
			if (OutPieces)
			{
				FClockPiece& Piece = OutPieces->AddDefaulted_GetRef();
				Piece.Leader = Leader;
				Piece.From = From;
				Piece.To = To;
				Piece.bForward = bForward;
				Piece.bIncludeFrom = bForward && bLegStart;
				Piece.bFromIsTurn = bForward && bFromTurn;
				Piece.bToIsTurn = bToTurn;
				Piece.LegEnd = End;
			}
			bLegStart = false;
			bFromTurn = false;
		};

		// Bounded so a long hitch on a very short loop can't spin here. Hitting the bound only drops time.
		for (int32 Guard = 0; Guard < 1024; ++Guard)
		{
			const float From = Clock.CycleTime;
			if (Clock.bPlayingForward)
			{
				Clock.CycleTime += Remaining;
				if (Clock.CycleTime < End)
				{
					Record(From, Clock.CycleTime, /*bForward=*/true, /*bToTurn=*/false);
					return;
				}
				Remaining = Clock.CycleTime - End;
				Clock.CycleTime = End;
			}
			else
			{
				Clock.CycleTime -= Remaining;
				if (Clock.CycleTime > Lead)
				{
					Record(From, Clock.CycleTime, /*bForward=*/false, /*bToTurn=*/false);
					return;
				}
				Remaining = Lead - Clock.CycleTime;
				Clock.CycleTime = Lead;
				Record(From, Lead, /*bForward=*/false, /*bToTurn=*/false);
			}

			const bool bEndedForward = Clock.bPlayingForward;
			++Clock.LoopsCompleted;

			const bool bAnotherLeg = ShouldStartAnotherLeg(Settings, Clock, bEndedForward);
			if (bEndedForward)
			{
				// Known only now: whether the end just reached is where a ping-pong turns round.
				Record(From, End, /*bForward=*/true, /*bToTurn=*/bAnotherLeg && Settings.bPingPong);
			}

			if (!bAnotherLeg)
			{
				// Only ever reached at the end of a forward leg, so this is the end pose.
				Clock.CycleTime = End;
				Clock.bPlayingForward = true;
				Clock.bFinished = true;
				Clock.bSettleRequested = false;
				return;
			}

			if (Settings.bPingPong)
			{
				Clock.bPlayingForward = !bEndedForward;
			}
			else
			{
				Clock.CycleTime = Lead;
			}

			// The leg starting now: a forward one after a return starts where the ping-pong turned round.
			bLegStart = true;
			bFromTurn = !bEndedForward;

			if (End - Lead <= KINDA_SMALL_NUMBER)
			{
				return;
			}
		}
	}

	/** The cue rules of a cue track's settings - its own, or a new cue's for a type whose settings don't carry them. */
	static const FWidgetAnimCueParams& GetCueRules(const FInstancedStruct& TypeParams)
	{
		static const FWidgetAnimCueParams Defaults;
		const FWidgetAnimCueParams* Rules = WidgetAnimCues::GetCueParams(TypeParams);
		return Rules ? *Rules : Defaults;
	}

	/** Adds one fire to Result, unless this point of this track has already fired this frame. */
	static void AddCueFire(FWidgetAnimResult& Result, const FWidgetAnimSettings& Settings, int32 LayerIndex, int32 Point, bool bBackward, bool bInverted, float Order, float SecondsToEnd = -1.f)
	{
		const bool bAlreadyFired = Result.CueFires.ContainsByPredicate([&Settings, Point](const FWidgetAnimCueFire& Fire)
		{
			return Fire.Params == &Settings.TypeParams && Fire.Point == Point;
		});
		if (bAlreadyFired)
		{
			return;
		}

		FWidgetAnimCueFire& Fire = Result.CueFires.AddDefaulted_GetRef();
		Fire.Params = &Settings.TypeParams;
		Fire.TargetWidget = Settings.TargetWidget;
		// The animation the cue is authored in - an animation inserted as a track's own - else the one it plays in.
		Fire.Animation = Settings.AuthoredIn ? Settings.AuthoredIn.Get() : Settings.ValueScope.Get();
		Fire.Part = Settings.Part;
		Fire.Point = Point;
		Fire.Value = WidgetAnimCues::GetPointValue(Settings.TypeParams, Settings.ValueKeys, Point);
		Fire.bBackward = bBackward;
		Fire.bInverted = bInverted;
		Fire.LayerIndex = LayerIndex;
		Fire.Order = Order;
		Fire.SecondsToEnd = SecondsToEnd;
	}

	/**
	 * The points of one cue layer its clock crossed this frame, into Result. Span is where the layer sits on the timeline;
	 * inverted, a point sits where its motion is, mirrored in the layer's span. See WidgetAnimCue.h for the rules.
	 */
	static void GatherClockCues(const FWidgetAnimSettings& Settings, int32 LayerIndex, const FLayerSpan& Span, bool bInvert, int32 Leader, const FClockPieces& Pieces, FWidgetAnimResult& Result)
	{
		const FWidgetAnimCueParams& Rules = GetCueRules(Settings.TypeParams);
		if (bInvert && !Rules.bFireWhenInverted)
		{
			return;
		}

		TArray<TPair<int32, float>, TInlineAllocator<8>> Points;
		WidgetAnimCues::GetFiringPoints(Settings.TypeParams, Settings.ValueKeys, Points);
		if (Points.Num() == 0)
		{
			return;
		}

		// Only for telling a point exactly at a leg's start or end from one a hair beside it: the timeline's times and a
		// point's are worked out by the same sums, so they match to within float rounding when they are the same place.
		constexpr float Epsilon = 1.e-5f;

		for (int32 PieceIndex = 0; PieceIndex < Pieces.Num(); ++PieceIndex)
		{
			const FClockPiece& Piece = Pieces[PieceIndex];
			if (Piece.Leader != Leader)
			{
				continue;
			}

			for (const TPair<int32, float>& Point : Points)
			{
				const float At = Span.Start + (bInvert ? 1.f - Point.Value : Point.Value) * Span.Length;

				bool bCrossed = false;
				bool bAtTurn = false;
				if (Piece.bForward)
				{
					// (From, To] - a leg's start included on its first stretch, its end matched to within rounding.
					const bool bAtFrom = FMath::Abs(At - Piece.From) <= Epsilon;
					const float Upper = Piece.To >= Piece.LegEnd ? Piece.To + Epsilon : Piece.To;
					if (Piece.bIncludeFrom && bAtFrom)
					{
						bCrossed = true;
						bAtTurn = Piece.bFromIsTurn;
					}
					else if (At > Piece.From && At <= Upper)
					{
						bCrossed = true;
						bAtTurn = Piece.bToIsTurn && FMath::Abs(At - Piece.To) <= Epsilon;
					}
				}
				else
				{
					// [To, From) the other way - but never a turnaround point at either end, which the forward legs on
					// either side of a return pass (once, whichever direction the cue fires on).
					bCrossed = At > Piece.To && At <= Piece.From && At < Piece.LegEnd - Epsilon;
				}

				if (!bCrossed)
				{
					continue;
				}

				const bool bAllowed = bAtTurn
					? (Rules.AllowsForward() || Rules.AllowsBackward())
					: (Piece.bForward ? Rules.AllowsForward() : Rules.AllowsBackward());
				if (!bAllowed)
				{
					continue;
				}

				// In the order the playhead met them: stretch by stretch, and along each the way it was going.
				const float Along = Piece.bForward ? At - Piece.From : Piece.From - At;

				// How much of the track the playhead has still to pass, the way it is going - what a sound's fade out is
				// timed by.
				const float SecondsToEnd = Piece.bForward ? Span.Start + Span.Length - At : At - Span.Start;
				AddCueFire(Result, Settings, LayerIndex, Point.Key, /*bBackward=*/!Piece.bForward && !bAtTurn, bInvert, PieceIndex * 100000.f + Along, FMath::Max(SecondsToEnd, 0.f));
			}
		}
	}

	/**
	 * The points of one value-driven cue layer its value crossed since it last looked, into Result - rising past one is
	 * forward, falling back past one backward. The value it starts at crosses nothing, and movements under a hundredth of
	 * the range are waited out, so a spring settling on a point doesn't chatter; its own ends are always reached exactly.
	 */
	static void GatherValueCues(const FWidgetAnimSettings& Settings, int32 LayerIndex, FWidgetAnimState& State, FWidgetAnimResult& Result)
	{
		const float Progress = FMath::Clamp(State.ValueState.Progress.X, 0.f, 1.f);
		const float Directed = Settings.bInvertAnimation ? 1.f - Progress : Progress;

		if (!State.bCueProgressSet)
		{
			State.CueProgress = Directed;
			State.bCueProgressSet = true;
			return;
		}

		const float From = State.CueProgress;
		constexpr float MinStep = 0.01f;
		const bool bReachedEnd = (Directed == 0.f || Directed == 1.f) && Directed != From;
		if (FMath::Abs(Directed - From) < MinStep && !bReachedEnd)
		{
			return;
		}
		State.CueProgress = Directed;

		const FWidgetAnimCueParams& Rules = GetCueRules(Settings.TypeParams);
		const bool bRising = Directed > From;
		if (bRising ? !Rules.AllowsForward() : !Rules.AllowsBackward())
		{
			return;
		}

		TArray<TPair<int32, float>, TInlineAllocator<8>> Points;
		WidgetAnimCues::GetFiringPoints(Settings.TypeParams, Settings.ValueKeys, Points);
		for (const TPair<int32, float>& Point : Points)
		{
			const float At = Point.Value;
			const bool bCrossed = bRising ? (At > From && At <= Directed) : (At >= Directed && At < From);
			if (bCrossed)
			{
				AddCueFire(Result, Settings, LayerIndex, Point.Key, /*bBackward=*/!bRising, Settings.bInvertAnimation, bRising ? At - From : From - At);
			}
		}
	}
}

namespace WidgetAnimDrives
{
	/**
	 * The first layer of Settings that Drive moves - where its state is kept. Value-driven layers follow their own values
	 * whatever the animation they are in does, so they are never moved by one.
	 */
	static int32 FindLeader(const TArray<FWidgetAnimSettings>& Settings, const FWidgetAnimProgressDrive* Drive)
	{
		for (int32 Index = 0; Index < Settings.Num(); ++Index)
		{
			if (Settings[Index].ProgressDrive.Get() == Drive && !Settings[Index].IsValueDriven())
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	/**
	 * Where on its timeline - in the clock's seconds, as CycleTime counts them - a progress clock puts its layers for a
	 * progress: across the placeholder of the animation track it plays in, or, for an animation's own progress, that many
	 * seconds into the animation as authored (100% is one second) at its Playback Speed. Leader is the drive's first layer.
	 */
	static float GetTime(const TArray<FWidgetAnimSettings>& Settings, int32 Leader, float Progress)
	{
		const FWidgetAnimSettings& Layer = Settings[Leader];
		const FWidgetAnimProgressDrive& Drive = *Layer.ProgressDrive;
		if (Drive.Anchor != INDEX_NONE)
		{
			for (const FWidgetAnimSettings& Candidate : Settings)
			{
				if (Candidate.PlacementAnchor == Drive.Anchor)
				{
					const WidgetAnimLoop::FLayerSpan Span = WidgetAnimLoop::GetLayerSpan(Candidate);
					return Span.Start + Progress * Span.Length;
				}
			}
		}
		return Progress / FMath::Max(Layer.PlaybackSpeed, 0.01f);
	}

	/** Whether Id names the progress clock Layer follows, in Track. */
	static bool Names(const FWidgetAnimValueId& Id, FName TrackAnimId, const FWidgetAnimSettings& Layer)
	{
		const FWidgetAnimProgressDrive* Drive = Layer.ProgressDrive.Get();
		return Drive && !Layer.IsValueDriven() && !Id.ValueName.IsNone() && Drive->ValueName == Id.ValueName
			&& (Id.IsAnywhere() || Layer.ValueScope.Get() == Id.Animation)
			&& (Id.AnimId.IsNone() || TrackAnimId == Id.AnimId);
	}

	/**
	 * How far along a drive still playing on its clock (one with a Duration, no progress set yet) has got: where the clock
	 * of the loop group its first layer is in has reached, as a progress. 0 before the clock has run.
	 */
	static float GetClockProgress(const TArray<FWidgetAnimSettings>& Settings, const TArray<FWidgetAnimState>& States, int32 Leader)
	{
		if (!States.IsValidIndex(Leader))
		{
			return 0.f;
		}

		// The loop group's clock lives on its first layer that plays on the clock - see TickAnimationsInto.
		int32 Clock = Leader;
		const int32 Group = Settings[Leader].LoopGroup;
		if (Group != INDEX_NONE)
		{
			for (int32 Index = 0; Index < Settings.Num() && Index < States.Num(); ++Index)
			{
				if (Settings[Index].LoopGroup == Group && !Settings[Index].IsValueDriven())
				{
					Clock = Index;
					break;
				}
			}
		}

		const FWidgetAnimState& ClockState = States[Clock];
		if (!ClockState.bCueClockStarted)
		{
			return 0.f;
		}
		const float Progress = ClockState.CycleTime * FMath::Max(Settings[Leader].PlaybackSpeed, 0.01f);
		return FMath::Clamp(Progress, 0.f, 1.f);
	}

	/** The progress a drive shows now: its own once it has taken over from the clock, else where the clock has got to. */
	static float GetProgress(const TArray<FWidgetAnimSettings>& Settings, const TArray<FWidgetAnimState>& States, int32 Leader, bool bTarget)
	{
		if (!States.IsValidIndex(Leader))
		{
			return 0.f;
		}
		const FWidgetAnimState& State = States[Leader];
		if (State.bDriveTaken)
		{
			return bTarget ? State.DriveState.Target.X : State.DriveState.Progress.X;
		}
		return GetClockProgress(Settings, States, Leader);
	}

	/**
	 * Hands a drive still on its clock over to its progress, from wherever the clock had got to, so setting a progress
	 * while an entry plays over its Progress Animation Duration carries on from there rather than jumping.
	 */
	static void TakeOver(const TArray<FWidgetAnimSettings>& Settings, TArray<FWidgetAnimState>& States, int32 Leader)
	{
		FWidgetAnimState& State = States[Leader];
		if (State.bDriveTaken)
		{
			return;
		}

		const float Progress = GetClockProgress(Settings, States, Leader);
		WidgetAnimValue::Snap(State.DriveState, FVector4f(Progress, Progress, Progress, Progress));
		State.DriveTime = GetTime(Settings, Leader, Progress);
		// A clock that had already run has fired what lies behind it; one that hadn't leaves the start for the drive's first frame.
		State.bDriveStarted = Progress > 0.f;
		State.bDriveTaken = true;
	}
}

bool UWidgetAnimLibrary::IsEndlessLoop(const FWidgetAnimSettings& Settings)
{
	return Settings.bLoop && Settings.LoopCount <= 0;
}

bool UWidgetAnimLibrary::HasRunningLayers(const TArray<FWidgetAnimSettings>& Animations, const TArray<FWidgetAnimState>& States)
{
	if (States.Num() != Animations.Num())
	{
		return false;
	}

	for (const FWidgetAnimState& State : States)
	{
		if (!State.bFinished)
		{
			return true;
		}
	}

	return false;
}

bool UWidgetAnimLibrary::BlocksSettle(const FWidgetAnimSettings& Settings, const FWidgetAnimState& State)
{
	if (State.bFinished)
	{
		return false;
	}

	if (IsEndlessLoop(Settings) && State.LoopsCompleted >= 1)
	{
		// It has played through its span at least once, so what's left is ongoing flourish, not arrival.
		return false;
	}

	return true;
}

bool UWidgetAnimLibrary::TickAnimationsInto(const TArray<FWidgetAnimSettings>& Animations, TArray<FWidgetAnimState>& States, float DeltaTime, bool bIsOutAnimation, FWidgetAnimResult& OutResult, bool* bOutAllComplete, const FWidgetAnimBaselineSet* Baselines, bool bDriveValueLayers)
{
	const int32 NumLayers = Animations.Num();

	if (States.Num() != NumLayers)
	{
		const int32 OldNum = States.Num();
		States.SetNum(NumLayers);
		for (int32 Index = OldNum; Index < NumLayers; ++Index)
		{
			States[Index].DelayRemaining = FMath::Max(Animations[Index].Delay, 0.f);
		}
	}

	// Layers loop as the animation they belong to, not one by one. Looped separately, layers of different
	// lengths drift apart after the first cycle and every delay is lost after the first play, so a
	// layered animation never looked like itself again. Each group's clock lives on its first layer.
	TArray<int32, TInlineAllocator<16>> LeaderOf;
	TArray<WidgetAnimLoop::FLayerSpan, TInlineAllocator<16>> Spans;
	TArray<float, TInlineAllocator<16>> GroupLead;
	TArray<float, TInlineAllocator<16>> GroupEnd;
	LeaderOf.SetNum(NumLayers);
	Spans.SetNum(NumLayers);
	GroupLead.Init(TNumericLimits<float>::Max(), NumLayers);
	GroupEnd.Init(0.f, NumLayers);

	// Value-driven layers, when they follow their values, have no place on the clock: they neither lead a loop group
	// nor stretch one, so the rest of the animation times, loops and finishes exactly as if they weren't there.
	auto FollowsValue = [&Animations, bDriveValueLayers](int32 Index) { return bDriveValueLayers && Animations[Index].IsValueDriven(); };

	// Layers following a progress clock (see FWidgetAnimProgressDrive) are off the clock too, each with the first layer its
	// drive moves, where the drive's state is. A drive with a Duration stays on the clock until a progress is set; and,
	// like value-driven layers, none follows its progress outside the track player - every preview plays it on the clock.
	TArray<int32, TInlineAllocator<16>> DriveLeaderOf;
	DriveLeaderOf.Init(INDEX_NONE, NumLayers);
	bool bHasDrives = false;
	if (bDriveValueLayers)
	{
		TArray<TPair<const FWidgetAnimProgressDrive*, int32>, TInlineAllocator<4>> DriveLeaders;
		for (int32 Index = 0; Index < NumLayers; ++Index)
		{
			const FWidgetAnimProgressDrive* Drive = Animations[Index].ProgressDrive.Get();
			if (!Drive || Animations[Index].IsValueDriven())
			{
				continue;
			}

			const TPair<const FWidgetAnimProgressDrive*, int32>* Known = DriveLeaders.FindByPredicate([Drive](const TPair<const FWidgetAnimProgressDrive*, int32>& Pair) { return Pair.Key == Drive; });
			const int32 Leader = Known ? Known->Value : Index;
			if (!Known)
			{
				DriveLeaders.Emplace(Drive, Index);
			}

			if (Drive->Duration > 0.f && !States[Leader].bDriveTaken)
			{
				continue;
			}
			DriveLeaderOf[Index] = Leader;
			bHasDrives = true;
		}
	}
	auto OffClock = [&FollowsValue, &DriveLeaderOf](int32 Index) { return FollowsValue(Index) || DriveLeaderOf[Index] != INDEX_NONE; };

	// Cues fire from the stretches of timeline the clocks cross, so those are recorded - but only when the stack has a
	// cue in it; everything else plays exactly as it did. See WidgetAnimCue.h.
	TArray<bool, TInlineAllocator<16>> IsCueLayer;
	IsCueLayer.Init(false, NumLayers);
	bool bHasCues = false;
	for (int32 Index = 0; Index < NumLayers; ++Index)
	{
		if (Animations[Index].AnimType == EWidgetAnimType::Custom)
		{
			const FWidgetAnimEvaluator* Evaluator = Animations[Index].FindEvaluator();
			IsCueLayer[Index] = Evaluator && Evaluator->IsCue();
			bHasCues = bHasCues || IsCueLayer[Index];
		}
	}
	WidgetAnimLoop::FClockPieces ClockPieces;
	const int32 FirstCueFire = OutResult.CueFires.Num();

	// Each loop group's leader is its first layer on the clock, remembered as it is met: looking back over every earlier
	// layer for it made a staggered track of N items cost N squared every frame.
	TArray<TPair<int32, int32>, TInlineAllocator<8>> GroupLeaders;
	for (int32 Index = 0; Index < NumLayers; ++Index)
	{
		LeaderOf[Index] = Index;
		if (OffClock(Index))
		{
			continue;
		}

		const int32 Group = Animations[Index].LoopGroup;
		if (Group != INDEX_NONE)
		{
			const TPair<int32, int32>* Known = GroupLeaders.FindByPredicate([Group](const TPair<int32, int32>& Pair) { return Pair.Key == Group; });
			if (Known)
			{
				LeaderOf[Index] = Known->Value;
			}
			else
			{
				GroupLeaders.Emplace(Group, Index);
			}
		}

		Spans[Index] = WidgetAnimLoop::GetLayerSpan(Animations[Index]);

		const int32 Leader = LeaderOf[Index];
		GroupLead[Leader] = FMath::Min(GroupLead[Leader], Spans[Index].Start);
		GroupEnd[Leader] = FMath::Max(GroupEnd[Leader], Spans[Index].Start + Spans[Index].Length);
	}

	// One advance per group, on its leader.
	for (int32 Index = 0; Index < NumLayers; ++Index)
	{
		if (LeaderOf[Index] == Index && !OffClock(Index))
		{
			WidgetAnimLoop::AdvanceTimeline(Animations[Index], States[Index], GroupLead[Index], GroupEnd[Index], DeltaTime, bHasCues ? &ClockPieces : nullptr, Index);
		}
	}

	// One advance per progress clock, on its first layer: its progress moves toward the last one set the way the animation
	// says, in real time - not at the layers' speed, which is the animation's pace on the clock - and puts the timeline
	// there. The stretch it crossed is recorded like a clock's, so cues fire as the progress passes them, either way.
	if (bHasDrives)
	{
		for (int32 Index = 0; Index < NumLayers; ++Index)
		{
			if (DriveLeaderOf[Index] != Index)
			{
				continue;
			}

			FWidgetAnimState& State = States[Index];
			const FWidgetAnimProgressDrive& Drive = *Animations[Index].ProgressDrive;
			WidgetAnimValue::Advance(Drive.Options, Drive.GetMotion(), State.DriveState, DeltaTime);

			const float Progress = FMath::Clamp(State.DriveState.Progress.X, 0.f, 1.f);
			const float To = WidgetAnimDrives::GetTime(Animations, Index, Progress);
			const float From = State.bDriveStarted ? State.DriveTime : To;
			if (bHasCues && (!State.bDriveStarted || From != To))
			{
				WidgetAnimLoop::FClockPiece& Piece = ClockPieces.AddDefaulted_GetRef();
				Piece.Leader = Index;
				Piece.From = From;
				Piece.To = To;
				Piece.bForward = To >= From;
				Piece.bIncludeFrom = !State.bDriveStarted;
				Piece.LegEnd = WidgetAnimDrives::GetTime(Animations, Index, 1.f);
			}
			State.DriveTime = To;
			State.bDriveStarted = true;
		}
	}

	bool bAllSettled = true;
	bool bAllComplete = true;

	for (int32 Index = 0; Index < NumLayers; ++Index)
	{
		const FWidgetAnimSettings& Settings = Animations[Index];

		if (FollowsValue(Index))
		{
			// Moved toward the last value set for its name, at the entry's speed, and posed there - from the moment the
			// track starts, with no delay: it is showing data, not playing its part of a sequence. At rest it counts as
			// ended, and it never holds the stack from settling. See WidgetAnimDriver.h.
			FWidgetAnimState& State = States[Index];
			const float ValueDeltaTime = DeltaTime * Settings.GetPlaySpeed();
			State.bFinished = WidgetAnimValue::Advance(Settings.ValueOptions, Settings.GetValueMotion(), State.ValueState, ValueDeltaTime);

			// A Pulse's value is its strength; it pulses on its own clock, one pulse per Duration, for as long as it has
			// any strength - and only then counts as at rest. So is a Shake's, whose noise never repeats: its clock only
			// wraps after a long while, where the noise is as smooth as anywhere.
			if (WidgetAnimTypeOscillates(Settings.AnimType))
			{
				const float Period = Settings.AnimType == EWidgetAnimType::Shake ? 3600.f : FMath::Max(Settings.Duration, KINDA_SMALL_NUMBER);
				State.ValueState.PulseTime = FMath::Fmod(State.ValueState.PulseTime + ValueDeltaTime, Period);
				State.bFinished = State.bFinished && State.ValueState.Progress.X <= 0.f;
			}

			// A cue poses nothing: it fires as its value crosses its points.
			if (IsCueLayer[Index])
			{
				if (!Settings.bMuted)
				{
					WidgetAnimLoop::GatherValueCues(Settings, Index, State, OutResult);
				}
				bAllComplete = bAllComplete && State.bFinished;
				continue;
			}

			if (!Settings.bMuted)
			{
				ApplyValueLayerContribution(Settings, State.ValueState, OutResult, Baselines);
			}
			else if (Baselines && Baselines->bCaptured)
			{
				TouchLayerTargets(Settings, OutResult, *Baselines);
			}

			bAllComplete = bAllComplete && State.bFinished;
			continue;
		}

		if (DriveLeaderOf[Index] != INDEX_NONE)
		{
			// Posed where its progress clock puts the timeline, exactly as a scrub there would pose it - so it looks the
			// same whichever way the progress got there - and at rest whenever the progress is. It never holds the stack
			// from settling: like a value, a progress is shown until the next one.
			const int32 DriveLeader = DriveLeaderOf[Index];
			const FWidgetAnimState& DriveClock = States[DriveLeader];
			const WidgetAnimLoop::FLayerSpan DrivenSpan = WidgetAnimLoop::GetLayerSpan(Settings);
			const float DrivenTime = DriveClock.DriveTime - DrivenSpan.Start;
			const bool bDriveAtRest = !DriveClock.DriveState.IsMoving();
			const bool bDrivenInvert = GetEffectiveInvert(Settings, bIsOutAnimation);
			const float DrivenAlpha = WidgetAnimLoop::GetLayerAlpha(DrivenTime, DrivenSpan);

			FWidgetAnimState& State = States[Index];
			State.bFinished = bDriveAtRest;
			State.bPlayingForward = true;
			State.DelayRemaining = FMath::Max(-DrivenTime, 0.f);
			State.CycleTime = FMath::Clamp(DrivenTime, 0.f, DrivenSpan.Length);

			if (IsCueLayer[Index])
			{
				if (!Settings.bMuted)
				{
					WidgetAnimLoop::GatherClockCues(Settings, Index, DrivenSpan, bDrivenInvert, DriveLeader, ClockPieces, OutResult);
				}
			}
			else if (!Settings.bMuted && (DrivenTime >= 0.f || bDrivenInvert))
			{
				ApplyAnimationContribution(Settings, bDrivenInvert ? 1.f - DrivenAlpha : DrivenAlpha, OutResult, Baselines, /*bTowardStart=*/bDrivenInvert);
			}
			else if (Settings.bHoldStartPose && !Settings.bMuted)
			{
				ApplyAnimationContribution(Settings, 0.f, OutResult, Baselines, /*bTowardStart=*/false);
			}
			else if (Baselines && Baselines->bCaptured)
			{
				TouchLayerTargets(Settings, OutResult, *Baselines);
			}

			bAllComplete = bAllComplete && bDriveAtRest;
			continue;
		}

		const int32 Leader = LeaderOf[Index];
		const FWidgetAnimState& Clock = States[Leader];
		const WidgetAnimLoop::FLayerSpan& Span = Spans[Index];

		const float TimelineTime = Clock.bFinished ? GroupEnd[Leader] : Clock.CycleTime;
		const float LocalTime = TimelineTime - Span.Start;

		// After its end the layer holds its end pose. Before its start it contributes nothing - the widget is left
		// as it is, or, with a baseline captured, put back at that baseline, so what is on screen never depends
		// on where the clock was a frame ago - unless it plays inverted, where what comes before its start is its
		// end pose: a Play Out has to hold the pose the entrance left the widget in until each layer begins
		// undoing it. Direction needs no special case: a return leg is the timeline running backwards, so the
		// alpha falls on its own.
		const bool bInvert = GetEffectiveInvert(Settings, bIsOutAnimation);
		const bool bBeforeStart = (LocalTime < 0.f && !bInvert) || Settings.bMuted;

		// A clock that has come to rest has every layer at its very end, however the arithmetic rounds.
		const float LinearAlpha = Clock.bFinished ? 1.f : WidgetAnimLoop::GetLayerAlpha(LocalTime, Span);

		if (Index != Leader)
		{
			// Members mirror the group's progress so anything reading a single layer's state sees the truth.
			FWidgetAnimState& State = States[Index];
			State.bFinished = Clock.bFinished;
			State.bPlayingForward = Clock.bPlayingForward;
			State.LoopsCompleted = Clock.LoopsCompleted;
			State.bSettleRequested = Clock.bSettleRequested;
			State.DelayRemaining = FMath::Max(-LocalTime, 0.f);
			State.CycleTime = FMath::Clamp(LocalTime, 0.f, Span.Length);
		}

		if (IsCueLayer[Index])
		{
			// A cue poses nothing - not even its baseline - and fires as the clock passes its points. Muted (hidden in the
			// editor) it keeps its place in time but never fires.
			if (!Settings.bMuted)
			{
				WidgetAnimLoop::GatherClockCues(Settings, Index, Span, bInvert, Leader, ClockPieces, OutResult);
			}
		}
		else if (!bBeforeStart)
		{
			// Heading back toward the start values because the track is inverted - only then. A ping-pong's return leg is
			// the same track played backwards, exactly as the editor's preview plays it: the timeline runs back, the alpha
			// falls with it, and every key and curve is met in reverse. Taking the return leg as "toward the start" ran a
			// spring forward to its start instead - an inverted track, not the track rewound. Only springs read it - see
			// ApplyAnimationContribution.
			ApplyAnimationContribution(Settings, bInvert ? 1.f - LinearAlpha : LinearAlpha, OutResult, Baselines, /*bTowardStart=*/bInvert);
		}
		else if (Settings.bHoldStartPose && !Settings.bMuted)
		{
			// A staggered item waiting its turn shows where it will start from - a letter fading in is not there yet.
			ApplyAnimationContribution(Settings, 0.f, OutResult, Baselines, /*bTowardStart=*/false);
		}
		else if (Baselines && Baselines->bCaptured)
		{
			// Not started, or hidden: its widget sits at its baseline rather than wherever it was last posed.
			TouchLayerTargets(Settings, OutResult, *Baselines);
		}

		if (!States[Index].bFinished)
		{
			bAllComplete = false;
		}

		if (BlocksSettle(Settings, States[Index]))
		{
			bAllSettled = false;
		}
	}

	// This frame's cues in the order the playhead met them, whichever layers they are on.
	if (OutResult.CueFires.Num() - FirstCueFire > 1)
	{
		TArrayView<FWidgetAnimCueFire> Fired(OutResult.CueFires.GetData() + FirstCueFire, OutResult.CueFires.Num() - FirstCueFire);
		Algo::StableSortBy(Fired, &FWidgetAnimCueFire::Order);
	}

	if (bOutAllComplete)
	{
		*bOutAllComplete = bAllComplete;
	}

	return bAllSettled;
}

FWidgetAnimResult UWidgetAnimLibrary::TickWidgetAnimations(const TArray<FWidgetAnimSettings>& Animations, TArray<FWidgetAnimState>& States, float DeltaTime, bool bIsOutAnimation)
{
	FWidgetAnimResult Result;
	Result.bAllFinished = TickAnimationsInto(Animations, States, DeltaTime, bIsOutAnimation, Result);

	// The one place a result leaves the call that built it: the caller may apply it after these settings are gone,
	// so registered-type writes take their own copy of their settings. See FWidgetAnimAuthoritativeWrite.
	for (FWidgetAnimAuthoritativeWrite& Write : Result.AuthoritativeWrites)
	{
		Write.DetachParams();
	}

	// With no widget to fire them from, and settings that may be gone by the time the result is used, cues passed here
	// are dropped: a caller clocking animations by hand through this node poses them, and gets no cues.
	Result.CueFires.Reset();
	return Result;
}

namespace WidgetAnimTransformDebug
{
	/**
	 * WidgetAnim.DebugTransforms: warnings for a widget pose that is wrong at runtime - each fires only on an actual fault,
	 * never on normal playback, so it is on by default and silent while all is well. 0 turns it off.
	 *  - A player ticked with time twice in one frame (the second tick is played as a re-pose - see TickWidgetAnimPlayer).
	 *  - One widget posed in one frame by two different players with different poses: they fight over it, and whichever
	 *    writes last is drawn.
	 *  - A pose that jumps for exactly one frame and comes back while its clock runs evenly: a flicker.
	 * Each comes with the call stack of the write that caused it, and is reported at most once a second per widget.
	 */
	static TAutoConsoleVariable<int32> CVarDebugTransforms(
		TEXT("WidgetAnim.DebugTransforms"),
		1,
		TEXT("Widget Animations: 1 (default) warns, with a call stack, when a widget's animated pose is actually wrong - a player ticked twice in a frame, two players fighting over one widget, or a one-frame flicker. 0 turns it off."),
		ECVF_Default);

	/** The player posing right now, and the time it was ticked with. Set by TickWidgetAnimPlayer for as long as it applies. */
	struct FWriter
	{
		const void* Player = nullptr;
		const UWidget* Host = nullptr;
		float DeltaTime = 0.f;
	};
	static FWriter& CurrentWriter()
	{
		static FWriter Writer;
		return Writer;
	}

	struct FWriterScope
	{
		FWriter Saved;
		FWriterScope(const void* Player, const UWidget* Host, float DeltaTime)
			: Saved(CurrentWriter())
		{
			CurrentWriter().Player = Player;
			CurrentWriter().Host = Host;
			CurrentWriter().DeltaTime = DeltaTime;
		}
		~FWriterScope()
		{
			CurrentWriter() = Saved;
		}
	};

	static FString DescribeWriter(const FWriter& Writer)
	{
		return Writer.Player
			? FString::Printf(TEXT("the player on %s, ticked with %.4f s"), Writer.Host ? *Writer.Host->GetName() : TEXT("(none)"), Writer.DeltaTime)
			: FString(TEXT("a direct apply (a Blueprint, a scrub)"));
	}

	static FString Describe(const FWidgetTransform& Pose)
	{
		return FString::Printf(TEXT("translation (%.2f, %.2f) scale (%.4f, %.4f) shear (%.3f, %.3f) angle %.2f"),
			Pose.Translation.X, Pose.Translation.Y, Pose.Scale.X, Pose.Scale.Y, Pose.Shear.X, Pose.Shear.Y, Pose.Angle);
	}

	static bool SamePose(const FWidgetTransform& A, const FWidgetTransform& B)
	{
		return A.Translation.Equals(B.Translation, 0.01) && A.Scale.Equals(B.Scale, 0.0001) && A.Shear.Equals(B.Shear, 0.0001) && FMath::IsNearlyEqual(A.Angle, B.Angle, 0.01f);
	}

	static bool IsEnabled()
	{
		return CVarDebugTransforms.GetValueOnGameThread() > 0;
	}

	/** At most once a second per widget and kind of fault, so a fault every frame doesn't bury the log. */
	static bool ShouldReport(const void* Key, int32 Kind)
	{
		static TMap<TPair<const void*, int32>, double> LastReported;
		if (LastReported.Num() > 1024)
		{
			LastReported.Reset();
		}
		const double Now = FPlatformTime::Seconds();
		double& Last = LastReported.FindOrAdd(TPair<const void*, int32>(Key, Kind), -1000.0);
		if (Now - Last < 1.0)
		{
			return false;
		}
		Last = Now;
		return true;
	}

	/**
	 * The call stack, for a fault just reported - at most every few seconds across all widgets. Walking and symbolizing a
	 * stack takes many milliseconds in the editor: a fault repeating on a hundred widgets at once (two players fighting
	 * over every button in a list) stalled the whole program, once a second, for each of them. The first stack says where
	 * the fault comes from; the warnings themselves are still logged for every widget.
	 */
	static void DumpStackRarely()
	{
		static double LastDumped = -1000.0;
		const double Now = FPlatformTime::Seconds();
		if (Now - LastDumped < 5.0)
		{
			UE_LOG(LogTemp, Warning, TEXT("WidgetAnim.DebugTransforms: (call stack left out - one was logged in the last few seconds.)"));
			return;
		}
		LastDumped = Now;
		FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
	}

	/** A player ticked with time a second time in one frame - see TickWidgetAnimPlayer. A fault in whatever ticked it. */
	static void NoteSecondAdvance(const void* Player, const UWidget* Host, float DeltaTime)
	{
		if (IsEnabled() && ShouldReport(Player, 0))
		{
			UE_LOG(LogTemp, Warning, TEXT("WidgetAnim.DebugTransforms: the player on %s was ticked with time (%.4f s) a second time in frame %llu - played as a re-pose instead, so nothing jumps. Whatever made the second tick is in this call stack:"),
				Host ? *Host->GetPathName() : TEXT("(none)"), DeltaTime, GFrameCounter);
			DumpStackRarely();
		}
	}

	struct FWrite
	{
		uint64 Frame = 0;
		FWidgetTransform Pose;
		FWriter Writer;
	};

	/** The last two frames' writes to one widget, for the checks below. */
	struct FHistory
	{
		FWrite Last;
		FWrite BeforeLast;
	};

	/** Called just before Target is posed by the track player, with the pose about to be written. */
	static void Check(UWidget* Target, const FWidgetTransform& Pose)
	{
		// A development aid: a shipped game pays nothing for it on every write.
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		const FWriter& Writer = CurrentWriter();
		if (!IsEnabled() || !Target || !Writer.Player || Target->IsDesignTime())
		{
			return;
		}

		// Only the last two frames matter to the checks below, so once the map is large, widgets not posed in that time are
		// dropped - every one of a few thousand animated widgets keeps its history. (It used to be emptied wholesale past
		// 4096, which with more widgets than that animating emptied and refilled it every frame.)
		static TMap<TObjectKey<UWidget>, FHistory> Histories;
		static int32 PruneAt = 4096;
		if (Histories.Num() > PruneAt)
		{
			for (auto It = Histories.CreateIterator(); It; ++It)
			{
				if (It->Value.Last.Frame + 2 < GFrameCounter)
				{
					It.RemoveCurrent();
				}
			}
			PruneAt = FMath::Max(4096, Histories.Num() * 2);
		}
		FHistory& History = Histories.FindOrAdd(TObjectKey<UWidget>(Target));

		// Two different players posing one widget differently in one frame: they fight over it. One player posing it
		// again in the same frame - right after starting a track - is how it is meant to work, and isn't reported.
		if (History.Last.Frame == GFrameCounter && History.Last.Writer.Player != Writer.Player && !SamePose(History.Last.Pose, Pose)
			&& ShouldReport(Target, 1))
		{
			UE_LOG(LogTemp, Warning, TEXT("WidgetAnim.DebugTransforms: %s is posed by two players in frame %llu, and they disagree: %s by %s, then %s by %s. Only the last is drawn. The second's call stack:"),
				*Target->GetPathName(), GFrameCounter, *Describe(History.Last.Pose), *DescribeWriter(History.Last.Writer), *Describe(Pose), *DescribeWriter(Writer));
			DumpStackRarely();
		}

		// A one-frame jump: three frames in a row from the same player, each moved on by time, where the middle one stands
		// well outside the other two while they agree with each other. Smooth motion - a spring's bounce included - never
		// does that; a loop starting again jumps and stays.
		const bool bEvenRun = History.Last.Frame + 1 == GFrameCounter && History.BeforeLast.Frame + 2 == GFrameCounter
			&& History.Last.Writer.Player == Writer.Player && History.BeforeLast.Writer.Player == Writer.Player
			&& Writer.DeltaTime > 0.f && History.Last.Writer.DeltaTime > 0.f;
		if (bEvenRun)
		{
			auto Spiked = [](double Before, double During, double After, double Tolerance)
			{
				const double Low = FMath::Min(Before, After) - Tolerance;
				const double High = FMath::Max(Before, After) + Tolerance;
				const double Step = FMath::Abs(After - Before);
				return (During < Low || During > High) && FMath::Min(FMath::Abs(During - Before), FMath::Abs(During - After)) > Step * 2.0 + Tolerance;
			};
			const FWidgetTransform& Before = History.BeforeLast.Pose;
			const FWidgetTransform& During = History.Last.Pose;
			const bool bSpike = Spiked(Before.Scale.X, During.Scale.X, Pose.Scale.X, 0.02) || Spiked(Before.Scale.Y, During.Scale.Y, Pose.Scale.Y, 0.02)
				|| Spiked(Before.Angle, During.Angle, Pose.Angle, 2.0)
				|| Spiked(Before.Translation.X, During.Translation.X, Pose.Translation.X, 2.0) || Spiked(Before.Translation.Y, During.Translation.Y, Pose.Translation.Y, 2.0);
			if (bSpike && ShouldReport(Target, 2))
			{
				UE_LOG(LogTemp, Warning, TEXT("WidgetAnim.DebugTransforms: %s flickered in frame %llu: %s, then %s (ticked with %.4f s), then %s (ticked with %.4f s)."),
					*Target->GetPathName(), History.Last.Frame, *Describe(Before), *Describe(During), History.Last.Writer.DeltaTime, *Describe(Pose), Writer.DeltaTime);
			}
		}

		if (History.Last.Frame != GFrameCounter)
		{
			History.BeforeLast = History.Last;
		}
		History.Last.Frame = GFrameCounter;
		History.Last.Pose = Pose;
		History.Last.Writer = Writer;
#endif
	}
}

namespace WidgetAnimPixelSnapping
{
	/**
	 * Pixel snapping, off on a widget while an animation scales, turns or shears it. Slate rounds a widget's edges - and
	 * everything inside it - to whole screen pixels; under a fractional scale that rounding flips from one frame to the
	 * next, so a thin outline (a radio button's ring) shook by a pixel as it scaled. Turned off with the widget's own Pixel
	 * Snapping (UWidget::SetPixelSnapping, which carries over a Slate rebuild) only while its pose isn't plain (scale 1, no
	 * angle, no shear) - children inherit it - and put back to what it was the moment it is, so a widget at rest draws
	 * exactly as authored.
	 */
	static TMap<TWeakObjectPtr<UWidget>, EWidgetPixelSnapping>& SavedMethods()
	{
		static TMap<TWeakObjectPtr<UWidget>, EWidgetPixelSnapping> Saved;
		return Saved;
	}

	/** Puts back the snapping a widget had before an animation turned it off. Nothing for one it never touched. */
	static void Restore(UWidget* Widget)
	{
		EWidgetPixelSnapping Original;
		if (!Widget || !SavedMethods().RemoveAndCopyValue(Widget, Original))
		{
			return;
		}
		Widget->SetPixelSnapping(Original);
	}

	/** Called with every pose written to a widget. */
	static void Update(UWidget* Widget, const FWidgetTransform& Pose)
	{
		if (!Widget)
		{
			return;
		}

		const bool bPlain = Pose.Scale.Equals(FVector2D(1.0, 1.0), 1.e-4) && FMath::IsNearlyZero(Pose.Angle, 1.e-3f) && Pose.Shear.IsNearlyZero(1.e-4);
		if (bPlain)
		{
			// Nearly every write is plain and nothing is waiting to be put back: no lookup at all.
			if (SavedMethods().Num() > 0)
			{
				Restore(Widget);
			}
			return;
		}

		const EWidgetPixelSnapping Current = Widget->GetPixelSnapping();
		if (Current == EWidgetPixelSnapping::Disabled)
		{
			return;
		}

		// Its own setting, kept the first time only.
		TMap<TWeakObjectPtr<UWidget>, EWidgetPixelSnapping>& Saved = SavedMethods();
		if (!Saved.Contains(Widget))
		{
			if (Saved.Num() > 4096)
			{
				for (auto It = Saved.CreateIterator(); It; ++It)
				{
					if (!It->Key.IsValid())
					{
						It.RemoveCurrent();
					}
				}
			}
			Saved.Add(Widget, Current);
		}
		Widget->SetPixelSnapping(EWidgetPixelSnapping::Disabled);
	}
}

namespace
{
	/**
	 * A render scale Slate can always invert. At exactly 0 on an axis, together with an angle or a shear, the widget's
	 * render transform has no inverse, and Slate lays that frame out with a garbage one - the widget flashed up huge
	 * for a frame whenever a Scale from 0 and a Rotate played on it together. A ten-thousandth is invisible, and keeps
	 * the transform invertible; the sign is kept, so a mirror stays a mirror.
	 */
	FVector2D MakeInvertibleScale(FVector2D Scale)
	{
		constexpr double MinScale = 0.0001;
		if (FMath::Abs(Scale.X) < MinScale)
		{
			Scale.X = Scale.X < 0.0 ? -MinScale : MinScale;
		}
		if (FMath::Abs(Scale.Y) < MinScale)
		{
			Scale.Y = Scale.Y < 0.0 ? -MinScale : MinScale;
		}
		return Scale;
	}
}

void UWidgetAnimLibrary::ApplyResultInternal(UWidget* FallbackWidget, const FWidgetAnimResult& Result, const FWidgetAnimBaselineSet* Baselines)
{
	// Parts - letters - gathered per widget, and handed over together below: a part host takes all of a frame's poses
	// in one call, and a part left out of it is at rest. Only allocated when a stagger is posing parts.
	TArray<TPair<UWidget*, TArray<FWidgetAnimPartPose>>, TInlineAllocator<2>> PartPoses;

	// Where the last widget's baseline was found: results and baseline sets list widgets in much the same order (both are
	// built by walking the same settings), so the next one is nearly always right after it - a staggered list of hundreds
	// of children doesn't search every baseline for every child.
	int32 BaselineHint = 0;
	auto FindBaseline = [Baselines, &BaselineHint](const UWidget* Target) -> const FWidgetAnimBaseline*
	{
		const TArray<FWidgetAnimBaseline>& All = Baselines->Baselines;
		for (int32 Step = 0; Step < All.Num(); ++Step)
		{
			const int32 Index = (BaselineHint + Step) % All.Num();
			if (All[Index].Widget.Get() == Target)
			{
				BaselineHint = Index + 1;
				return &All[Index];
			}
		}
		return nullptr;
	};

	// The whole render transform in one write, and only when it changed. Set field by field, each of the four setters pushed
	// a transform of its own to Slate and invalidated the widget four times a frame - scale with last frame's angle, then the
	// new translation, and so on - which, under invalidation (Global Invalidation, an Invalidation or Retainer Box), was the
	// suspect for a rotating widget flashing up larger for a frame or two at runtime only. "Changed" is exact: compared
	// within a tolerance, a scale landing on 1 from 0.99995 was never written, and the widget kept the last frame's.
	auto WritePose = [](UWidget* Target, float Opacity, const FWidgetTransform& Pose)
	{
		WidgetAnimTransformDebug::Check(Target, Pose);

		if (Target->GetRenderOpacity() != Opacity)
		{
			Target->SetRenderOpacity(Opacity);
		}
		// Snapping off while it scales or turns, so its edges don't shake by a pixel. See WidgetAnimPixelSnapping.
		WidgetAnimPixelSnapping::Update(Target, Pose);

		const FWidgetTransform& Current = Target->GetRenderTransform();
		if (Current.Translation != Pose.Translation || Current.Scale != Pose.Scale || Current.Shear != Pose.Shear || Current.Angle != Pose.Angle)
		{
			Target->SetRenderTransform(Pose);
		}
	};

	// Composed over the baselines only when they are composed over - see FWidgetAnimBaselineSet::bComposeTransforms.
	const bool bCompose = Baselines && Baselines->bComposeTransforms;

	for (const FWidgetAnimTargetedTransform& Transform : Result.TargetedTransforms)
	{
		UWidget* Target = Transform.TargetWidget ? Transform.TargetWidget : FallbackWidget;
		if (!Target)
		{
			continue;
		}

		if (Transform.Part.IsSet())
		{
			// Never over a baseline: a part at rest is exactly where its widget lays it out.
			TPair<UWidget*, TArray<FWidgetAnimPartPose>>* Host = PartPoses.FindByPredicate([Target](const TPair<UWidget*, TArray<FWidgetAnimPartPose>>& Entry)
			{
				return Entry.Key == Target;
			});
			if (!Host)
			{
				Host = &PartPoses.Emplace_GetRef(Target, TArray<FWidgetAnimPartPose>());
			}

			FWidgetAnimPartPose& Pose = Host->Value.AddDefaulted_GetRef();
			Pose.Part = Transform.Part;
			Pose.Opacity = Transform.Opacity;
			Pose.Scale = Transform.Scale;
			Pose.Translation = Transform.Translation;
			Pose.Shear = Transform.Shear;
			Pose.Angle = Transform.Angle;
			continue;
		}

		// Composed over a baseline when there is one: the animation says "half as bright, eight pixels up", not "opacity
		// 0.5, y = -8".
		float Opacity = Transform.Opacity;
		FWidgetTransform Pose(Transform.Translation, MakeInvertibleScale(Transform.Scale), Transform.Shear, Transform.Angle);
		if (bCompose)
		{
			// Identity: a widget nobody captured is animated exactly the way it always was.
			static const FWidgetAnimBaseline Identity;
			const FWidgetAnimBaseline* Found = FindBaseline(Target);
			const FWidgetAnimBaseline& Baseline = Found ? *Found : Identity;
			Opacity = Baseline.Opacity * Transform.Opacity;
			Pose = FWidgetTransform(Baseline.Translation + Transform.Translation, MakeInvertibleScale(Baseline.Scale * Transform.Scale), Baseline.Shear + Transform.Shear, Baseline.Angle + Transform.Angle);
		}

		WritePose(Target, Opacity, Pose);
	}

	// Widgets touched without being posed, over baselines only put back: at their captured pose - exactly where they were
	// before anything animated them - unless a track posed them above. A widget nobody captured is left as it is.
	if (Baselines && !bCompose && Result.RestingWidgets.Num() > 0)
	{
		TSet<const UWidget*> Written;
		Written.Reserve(Result.TargetedTransforms.Num() + Result.RestingWidgets.Num());
		for (const FWidgetAnimTargetedTransform& Transform : Result.TargetedTransforms)
		{
			if (!Transform.Part.IsSet())
			{
				Written.Add(Transform.TargetWidget ? Transform.TargetWidget : FallbackWidget);
			}
		}

		for (UWidget* Resting : Result.RestingWidgets)
		{
			UWidget* Target = Resting ? Resting : FallbackWidget;
			bool bAlreadyWritten = false;
			Written.Add(Target, &bAlreadyWritten);
			if (!Target || bAlreadyWritten)
			{
				continue;
			}

			if (const FWidgetAnimBaseline* Baseline = FindBaseline(Target))
			{
				WritePose(Target, Baseline->Opacity, FWidgetTransform(Baseline->Translation, MakeInvertibleScale(Baseline->Scale), Baseline->Shear, Baseline->Angle));
			}
		}
	}

	for (const TPair<UWidget*, TArray<FWidgetAnimPartPose>>& Host : PartPoses)
	{
		if (IWidgetAnimPartHost* PartHost = Cast<IWidgetAnimPartHost>(Host.Key))
		{
			PartHost->SetWidgetAnimPartPoses(Host.Value);
		}
	}

	// Shapes are not part of the render transform and are never composed over a baseline - the animation
	// says what the outline IS, and the baseline's job is only to put it back afterwards.
	ApplyShapeAnimResult(Result, FallbackWidget);

	// Registered types, the same way: each write says what its channel IS.
	ApplyAuthoritativeWrites(Result, FallbackWidget, Baselines);
}

void UWidgetAnimLibrary::ApplyShapeAnimResult(const FWidgetAnimResult& Result, UWidget* FallbackWidget)
{
	for (const FWidgetAnimShapeTarget& Target : Result.ShapeTargets)
	{
		// No box named: the layer animates the widget it was played on, so the outline goes to that widget's
		// Shape Box - the widget itself, or the Shape Box it sits in.
		UShapeBox* Box = Target.TargetBox ? Target.TargetBox.Get() : ResolveShapeBox(FallbackWidget);
		if (Box)
		{
			ApplyShapeToBox(Box, Target.Shape);
		}
	}
}

void UWidgetAnimLibrary::ApplyShapeToBox(UShapeBox* Box, const FShapeBoxShape& Shape)
{
	if (!Box || Box->GetShape().Equals(Shape))
	{
		return;
	}

	Box->SetShape(Shape);

	// SetShape writes the UObject's copy. The Slate widget that actually draws the outline keeps its own,
	// handed to it in SynchronizeProperties - which is why the Shape Box reacts to a Details panel edit (that
	// path always synchronizes) but an animation writing the same field changed nothing on screen, and why
	// only invalidating the Slate widget was not enough: it repainted the old outline. Synchronizing pushes
	// the new shape across the same way the Details panel does, whatever UShapeBox::SetShape does or doesn't
	// forward on its own.
	//
	// Layout as well as paint, because the shape decides where the box's content is arranged, not just how
	// it is drawn. Only when the shape actually changed, so a held pose costs nothing, and only once the
	// Slate widget exists - before that, building it synchronizes the UObject's shape by itself.
	if (const TSharedPtr<SWidget> SlateBox = Box->GetCachedWidget())
	{
		// Through UWidget, where SynchronizeProperties is public, in case UShapeBox re-declares it protected.
		static_cast<UWidget*>(Box)->SynchronizeProperties();
		SlateBox->Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
	}
}

UShapeBox* UWidgetAnimLibrary::ResolveShapeBox(UWidget* Widget)
{
	if (!Widget)
	{
		return nullptr;
	}

	if (UShapeBox* Direct = Cast<UShapeBox>(Widget))
	{
		return Direct;
	}

	// A Shape Box holds exactly one child, so a Shape Box parent always means "this widget alone inside it".
	return Cast<UShapeBox>(Widget->GetParent());
}

void UWidgetAnimLibrary::ApplyWidgetAnimResult(UWidget* FallbackWidget, const FWidgetAnimResult& Result)
{
	ApplyResultInternal(FallbackWidget, Result, nullptr);
}

void UWidgetAnimLibrary::ApplyWidgetAnimResultOver(UWidget* FallbackWidget, const FWidgetAnimResult& Result, const FWidgetAnimBaselineSet& Baselines)
{
	ApplyResultInternal(FallbackWidget, Result, &Baselines);
}

void UWidgetAnimLibrary::ApplyMaterialAnimResult(const FWidgetAnimResult& Result, UWidget* FallbackWidget)
{
	// Only a value that changed is set: setting one sends it to the render thread, every time, whatever it was - and a
	// parameter held at its end, or not moving this frame, would otherwise be sent again for every widget, every frame.
	for (const FWidgetAnimScalarTarget& Scalar : Result.ScalarParameters)
	{
		UWidget* Widget = Scalar.TargetWidget ? Scalar.TargetWidget.Get() : FallbackWidget;
		if (UMaterialInstanceDynamic* MID = GetWidgetDynamicMaterial(Widget, Scalar.Slot))
		{
			float Current = 0.f;
			if (!static_cast<const UMaterialInterface*>(MID)->GetScalarParameterValue(FHashedMaterialParameterInfo(Scalar.ParameterName), Current) || Current != Scalar.Value)
			{
				MID->SetScalarParameterValue(Scalar.ParameterName, Scalar.Value);
			}
		}
	}

	for (const FWidgetAnimVectorTarget& Vector : Result.VectorParameters)
	{
		UWidget* Widget = Vector.TargetWidget ? Vector.TargetWidget.Get() : FallbackWidget;
		if (UMaterialInstanceDynamic* MID = GetWidgetDynamicMaterial(Widget, Vector.Slot))
		{
			FLinearColor Current;
			if (!static_cast<const UMaterialInterface*>(MID)->GetVectorParameterValue(FHashedMaterialParameterInfo(Vector.ParameterName), Current) || Current != Vector.Value)
			{
				MID->SetVectorParameterValue(Vector.ParameterName, Vector.Value);
			}
		}
	}
}

namespace WidgetAnimMaterials
{
	/** A brush held in a property of Widget's class by name - read through reflection, as the property may be deprecated to touch directly. */
	const FSlateBrush* FindBrushProperty(const UWidget* Widget, FName PropertyName)
	{
		const FStructProperty* Property = Widget ? FindFProperty<FStructProperty>(Widget->GetClass(), PropertyName) : nullptr;
		return Property && Property->Struct == FSlateBrush::StaticStruct() ? Property->ContainerPtrToValuePtr<FSlateBrush>(Widget) : nullptr;
	}

	UMaterialInterface* FindObjectProperty(const UWidget* Widget, FName PropertyName)
	{
		const FObjectPropertyBase* Property = Widget ? FindFProperty<FObjectPropertyBase>(Widget->GetClass(), PropertyName) : nullptr;
		return Property ? Cast<UMaterialInterface>(Property->GetObjectPropertyValue_InContainer(Widget)) : nullptr;
	}
}

bool UWidgetAnimLibrary::WidgetHasMaterialSlot(const UWidget* Widget, EWidgetAnimMaterialSlot Slot)
{
	switch (Slot)
	{
	case EWidgetAnimMaterialSlot::Brush:       return Widget && (Widget->IsA<UImage>() || Widget->IsA<UBorder>());
	case EWidgetAnimMaterialSlot::Font:
	case EWidgetAnimMaterialSlot::FontOutline: return Widget && Widget->IsA<UTextBlock>();
	case EWidgetAnimMaterialSlot::Effect:      return Widget && Widget->IsA<URetainerBox>();
	default:                                   return false;
	}
}

UMaterialInterface* UWidgetAnimLibrary::FindWidgetMaterial(const UWidget* Widget, EWidgetAnimMaterialSlot Slot)
{
	using namespace WidgetAnimMaterials;

	if (!Widget)
	{
		return nullptr;
	}
	switch (Slot)
	{
	case EWidgetAnimMaterialSlot::Brush:
		if (const UImage* Image = Cast<UImage>(Widget))
		{
			return Cast<UMaterialInterface>(Image->GetBrush().GetResourceObject());
		}
		if (Widget->IsA<UBorder>())
		{
			// Found once, not by name every time: asked for every material parameter written, every frame.
			static const FStructProperty* const BackgroundProperty = FindFProperty<FStructProperty>(UBorder::StaticClass(), TEXT("Background"));
			const FSlateBrush* Brush = BackgroundProperty && BackgroundProperty->Struct == FSlateBrush::StaticStruct()
				? BackgroundProperty->ContainerPtrToValuePtr<FSlateBrush>(Widget)
				: FindBrushProperty(Widget, TEXT("Background"));
			return Brush ? Cast<UMaterialInterface>(Brush->GetResourceObject()) : nullptr;
		}
		return nullptr;

	case EWidgetAnimMaterialSlot::Font:
		if (const UTextBlock* Text = Cast<UTextBlock>(Widget))
		{
			return Cast<UMaterialInterface>(Text->GetFont().FontMaterial);
		}
		return nullptr;

	case EWidgetAnimMaterialSlot::FontOutline:
		if (const UTextBlock* Text = Cast<UTextBlock>(Widget))
		{
			return Cast<UMaterialInterface>(Text->GetFont().OutlineSettings.OutlineMaterial);
		}
		return nullptr;

	case EWidgetAnimMaterialSlot::Effect:
		if (const URetainerBox* Retainer = Cast<URetainerBox>(Widget))
		{
			if (UMaterialInterface* Live = Retainer->GetEffectMaterial())
			{
				return Live;
			}
			return FindObjectProperty(Widget, TEXT("EffectMaterial"));
		}
		return nullptr;

	default:
		return nullptr;
	}
}

UMaterialInstanceDynamic* UWidgetAnimLibrary::GetWidgetDynamicMaterial(UWidget* Widget, EWidgetAnimMaterialSlot Slot)
{
	// Only where there is a material to begin with: the widgets' own Get Dynamic Material would otherwise make an instance
	// of nothing, or of the default material.
	if (!Widget || !FindWidgetMaterial(Widget, Slot))
	{
		return nullptr;
	}
	switch (Slot)
	{
	case EWidgetAnimMaterialSlot::Brush:
		if (UImage* Image = Cast<UImage>(Widget))
		{
			return Image->GetDynamicMaterial();
		}
		if (UBorder* Border = Cast<UBorder>(Widget))
		{
			return Border->GetDynamicMaterial();
		}
		return nullptr;
	case EWidgetAnimMaterialSlot::Font:
		if (UTextBlock* Text = Cast<UTextBlock>(Widget))
		{
			return Text->GetDynamicFontMaterial();
		}
		return nullptr;
	case EWidgetAnimMaterialSlot::FontOutline:
		if (UTextBlock* Text = Cast<UTextBlock>(Widget))
		{
			return Text->GetDynamicOutlineMaterial();
		}
		return nullptr;
	case EWidgetAnimMaterialSlot::Effect:
		if (URetainerBox* Retainer = Cast<URetainerBox>(Widget))
		{
			return Retainer->GetEffectMaterial();
		}
		return nullptr;
	default:
		return nullptr;
	}
}

UWidget* FWidgetAnimSettings::GetMaterialWidget() const
{
	if (MaterialTargetWidget)
	{
		return MaterialTargetWidget;
	}
	if (MaterialTargetImage)
	{
		return MaterialTargetImage;
	}
	return TargetWidget;
}

namespace WidgetAnimMaterialLookups
{
	/**
	 * The entry for Widget's Parameter in Slot, or null - walked while there are a few, looked up past that. See
	 * FWidgetAnimMaterialLookup.
	 */
	template <typename TTarget>
	TTarget* Find(TArray<TTarget>& Targets, FWidgetAnimMaterialLookup& Lookup, const UWidget* Widget, EWidgetAnimMaterialSlot Slot, FName Parameter)
	{
		auto Matches = [Widget, Slot, Parameter](const TTarget& Target)
		{
			return Target.TargetWidget.Get() == Widget && Target.Slot == Slot && Target.ParameterName == Parameter;
		};

		const int32 Num = Targets.Num();
		static constexpr int32 LookupThreshold = 16;
		if (Num < LookupThreshold)
		{
			return Targets.FindByPredicate(Matches);
		}

		if (Lookup.Indexed > Num)
		{
			Lookup.Reset();
		}
		for (int32 Index = Lookup.Indexed; Index < Num; ++Index)
		{
			// The first entry for a parameter is the one a walk would find.
			const FWidgetAnimMaterialKey EntryKey{ Targets[Index].TargetWidget.Get(), Targets[Index].Slot, Targets[Index].ParameterName };
			if (!Lookup.Index.Contains(EntryKey))
			{
				Lookup.Index.Add(EntryKey, Index);
			}
		}
		Lookup.Indexed = Num;

		const int32* Found = Lookup.Index.Find(FWidgetAnimMaterialKey{ Widget, Slot, Parameter });
		if (!Found)
		{
			return nullptr;
		}
		if (Targets.IsValidIndex(*Found) && Matches(Targets[*Found]))
		{
			return &Targets[*Found];
		}

		// Moved since it was indexed: walked, and indexed again from nothing next time.
		Lookup.Reset();
		return Targets.FindByPredicate(Matches);
	}
}

FWidgetAnimScalarTarget* FWidgetAnimResult::FindScalar(const UWidget* Widget, EWidgetAnimMaterialSlot Slot, FName Parameter)
{
	return WidgetAnimMaterialLookups::Find(ScalarParameters, ScalarLookup, Widget, Slot, Parameter);
}

FWidgetAnimVectorTarget* FWidgetAnimResult::FindVector(const UWidget* Widget, EWidgetAnimMaterialSlot Slot, FName Parameter)
{
	return WidgetAnimMaterialLookups::Find(VectorParameters, VectorLookup, Widget, Slot, Parameter);
}

FWidgetAnimScalarTarget& FWidgetAnimResult::FindOrAddScalar(UWidget* Widget, EWidgetAnimMaterialSlot Slot, FName Parameter)
{
	if (FWidgetAnimScalarTarget* Found = FindScalar(Widget, Slot, Parameter))
	{
		return *Found;
	}
	FWidgetAnimScalarTarget& Added = ScalarParameters.AddDefaulted_GetRef();
	Added.TargetWidget = Widget;
	Added.Slot = Slot;
	Added.ParameterName = Parameter;
	return Added;
}

FWidgetAnimVectorTarget& FWidgetAnimResult::FindOrAddVector(UWidget* Widget, EWidgetAnimMaterialSlot Slot, FName Parameter)
{
	if (FWidgetAnimVectorTarget* Found = FindVector(Widget, Slot, Parameter))
	{
		return *Found;
	}
	FWidgetAnimVectorTarget& Added = VectorParameters.AddDefaulted_GetRef();
	Added.TargetWidget = Widget;
	Added.Slot = Slot;
	Added.ParameterName = Parameter;
	return Added;
}

void FWidgetAnimResult::FoldMaterialParameters(const FWidgetAnimResult& Other)
{
	for (const FWidgetAnimScalarTarget& Scalar : Other.ScalarParameters)
	{
		const int32 Before = ScalarParameters.Num();
		FWidgetAnimScalarTarget& Into = FindOrAddScalar(Scalar.TargetWidget, Scalar.Slot, Scalar.ParameterName);
		const bool bNew = ScalarParameters.Num() != Before;
		if (bNew || !Scalar.bIsBaseline || Into.bIsBaseline)
		{
			Into = Scalar;
		}
	}
	for (const FWidgetAnimVectorTarget& Vector : Other.VectorParameters)
	{
		const int32 Before = VectorParameters.Num();
		FWidgetAnimVectorTarget& Into = FindOrAddVector(Vector.TargetWidget, Vector.Slot, Vector.ParameterName);
		const bool bNew = VectorParameters.Num() != Before;
		if (bNew || !Vector.bIsBaseline || Into.bIsBaseline)
		{
			Into = Vector;
		}
	}
}

void UWidgetAnimLibrary::PlayInternal(UWidget* FallbackWidget, const TArray<FWidgetAnimSettings>& InAnimations, TArray<FWidgetAnimState>& States, float DeltaTime, bool bIsOutAnimation, const FWidgetAnimBaselineSet* Baselines)
{
	// Staggers fan out on every call - a caller keeping only settings and states has nowhere to keep the fanned-out
	// copies - and the same way every time, so its states keep lining up with them.
	TArray<FWidgetAnimSettings> Expanded;
	const TArray<FWidgetAnimSettings>& Animations = ExpandSettings(FallbackWidget, InAnimations, Expanded) ? Expanded : InAnimations;

	// Ticked through the internal entry point rather than TickWidgetAnimations, so a Transform layer's
	// keys can resolve Use Authored Shape against the captured baseline.
	FWidgetAnimResult Result;
	Result.bAllFinished = TickAnimationsInto(Animations, States, DeltaTime, bIsOutAnimation, Result, nullptr, Baselines);

	ApplyResultInternal(FallbackWidget, Result, Baselines);
	ApplyMaterialAnimResult(Result, FallbackWidget);

	// Fired next frame - see WidgetAnimCue.h. No Anim ID here: these stacks are played without entries' names.
	WidgetAnimCues::Queue(Result.CueFires, FallbackWidget, NAME_None);
}

void UWidgetAnimLibrary::PlayWidgetAnimations(UWidget* FallbackWidget, const TArray<FWidgetAnimSettings>& Animations, TArray<FWidgetAnimState>& States, float DeltaTime, bool bIsOutAnimation)
{
	PlayInternal(FallbackWidget, Animations, States, DeltaTime, bIsOutAnimation, nullptr);
}

void UWidgetAnimLibrary::PlayWidgetAnimationsOver(UWidget* FallbackWidget, const TArray<FWidgetAnimSettings>& Animations, TArray<FWidgetAnimState>& States, float DeltaTime, bool bIsOutAnimation, const FWidgetAnimBaselineSet& Baselines)
{
	PlayInternal(FallbackWidget, Animations, States, DeltaTime, bIsOutAnimation, &Baselines);
}

void UWidgetAnimLibrary::InitializeInternal(UWidget* FallbackWidget, const TArray<FWidgetAnimSettings>& InAnimations, TArray<FWidgetAnimState>& States, const FWidgetAnimBaselineSet* Baselines)
{
	TArray<FWidgetAnimSettings> Expanded;
	const TArray<FWidgetAnimSettings>& Animations = ExpandSettings(FallbackWidget, InAnimations, Expanded) ? Expanded : InAnimations;

	ResetWidgetAnimations(Animations, States);

	// The pose at time 0: every layer that starts right away, at its start. A layer with a delay does nothing
	// until it starts - see TickAnimationsInto - so it leaves the widget alone here too.
	FWidgetAnimResult StartResult;
	for (const FWidgetAnimSettings& Settings : Animations)
	{
		const bool bInvert = GetEffectiveInvert(Settings, /*bIsOutAnimation=*/false);
		if (!Settings.bMuted && !bInvert && Settings.bHoldStartPose)
		{
			// A staggered item, whenever its turn comes: at its start pose from the first frame.
			ApplyAnimationContribution(Settings, 0.f, StartResult, Baselines, /*bTowardStart=*/false);
			continue;
		}
		if (Settings.bMuted || (!bInvert && WidgetAnimLoop::GetLayerSpan(Settings).Start > 0.f))
		{
			// Back to the baseline, not left at wherever the last play finished - a loop restarting would
			// otherwise begin with a delayed layer still showing the end of the previous one.
			if (Baselines && Baselines->bCaptured)
			{
				TouchLayerTargets(Settings, StartResult, *Baselines);
			}
			continue;
		}
		ApplyAnimationContribution(Settings, bInvert ? 1.f : 0.f, StartResult, Baselines, /*bTowardStart=*/bInvert);
	}

	ApplyResultInternal(FallbackWidget, StartResult, Baselines);
	ApplyMaterialAnimResult(StartResult, FallbackWidget);
}

void UWidgetAnimLibrary::InitializeWidgetAnimations(UWidget* FallbackWidget, const TArray<FWidgetAnimSettings>& Animations, TArray<FWidgetAnimState>& States)
{
	InitializeInternal(FallbackWidget, Animations, States, nullptr);
}

void UWidgetAnimLibrary::InitializeWidgetAnimationsOver(UWidget* FallbackWidget, const TArray<FWidgetAnimSettings>& Animations, TArray<FWidgetAnimState>& States, const FWidgetAnimBaselineSet& Baselines)
{
	InitializeInternal(FallbackWidget, Animations, States, &Baselines);
}

void UWidgetAnimLibrary::ResetWidgetAnimations(const TArray<FWidgetAnimSettings>& Animations, TArray<FWidgetAnimState>& States)
{
	States.SetNum(Animations.Num());
	for (int32 Index = 0; Index < Animations.Num(); ++Index)
	{
		States[Index] = FWidgetAnimState();
		States[Index].DelayRemaining = FMath::Max(Animations[Index].Delay, 0.f);
	}
}

bool UWidgetAnimLibrary::AreWidgetAnimationsFinished(const TArray<FWidgetAnimSettings>& Animations, const TArray<FWidgetAnimState>& States)
{
	for (int32 Index = 0; Index < Animations.Num() && Index < States.Num(); ++Index)
	{
		if (BlocksSettle(Animations[Index], States[Index]))
		{
			return false;
		}
	}

	return true;
}


void UWidgetAnimLibrary::PlayWidgetIn(UWidget* FallbackWidget, const TArray<FWidgetAnimSettings>& InAnimations, FWidgetAnimPlaybackState& PlaybackState)
{
	PlaybackState.OutStates.Reset();

	InitializeWidgetAnimations(FallbackWidget, InAnimations, PlaybackState.InStates);
	PlaybackState.Phase = EWidgetAnimPhase::PlayingIn;
}

void UWidgetAnimLibrary::PlayWidgetOut(FWidgetAnimPlaybackState& PlaybackState, const TArray<FWidgetAnimSettings>& OutAnimations)
{
	ResetWidgetAnimations(OutAnimations, PlaybackState.OutStates);
	PlaybackState.Phase = EWidgetAnimPhase::PlayingOut;
}

bool UWidgetAnimLibrary::TickPlaybackInternal(UWidget* FallbackWidget, const TArray<FWidgetAnimSettings>& InAnimationsToExpand, const TArray<FWidgetAnimSettings>& OutAnimationsToExpand, FWidgetAnimPlaybackState& PlaybackState, float DeltaTime, const FWidgetAnimBaselineSet* Baselines)
{
	// Fanned out once here, so everything below - playing, and asking whether it has finished - sees the same layers.
	TArray<FWidgetAnimSettings> ExpandedIn;
	TArray<FWidgetAnimSettings> ExpandedOut;
	const TArray<FWidgetAnimSettings>& InAnimations = ExpandSettings(FallbackWidget, InAnimationsToExpand, ExpandedIn) ? ExpandedIn : InAnimationsToExpand;
	const TArray<FWidgetAnimSettings>& OutAnimations = ExpandSettings(FallbackWidget, OutAnimationsToExpand, ExpandedOut) ? ExpandedOut : OutAnimationsToExpand;

	switch (PlaybackState.Phase)
	{
	case EWidgetAnimPhase::PlayingIn:
	{
		PlayInternal(FallbackWidget, InAnimations, PlaybackState.InStates, DeltaTime, /*bIsOutAnimation=*/false, Baselines);
		if (AreWidgetAnimationsFinished(InAnimations, PlaybackState.InStates))
		{
			PlaybackState.Phase = EWidgetAnimPhase::Shown;
		}
		return false;
	}

	case EWidgetAnimPhase::PlayingOut:
	{
		PlayInternal(FallbackWidget, OutAnimations, PlaybackState.OutStates, DeltaTime, /*bIsOutAnimation=*/true, Baselines);
		if (AreWidgetAnimationsFinished(OutAnimations, PlaybackState.OutStates))
		{
			PlaybackState.Phase = EWidgetAnimPhase::Hidden;
			return true; 
		}
		return false;
	}

	case EWidgetAnimPhase::Shown:
	{
		// The entrance has landed, but endless loops in it keep running. This used to return without
		// ticking, which is what froze every looping layer the moment the rest of the stack finished.
		// Skipped when nothing is moving, so a settled widget is not re-posed every frame for nothing.
		if (HasRunningLayers(InAnimations, PlaybackState.InStates))
		{
			PlayInternal(FallbackWidget, InAnimations, PlaybackState.InStates, DeltaTime, /*bIsOutAnimation=*/false, Baselines);
		}
		return false;
	}

	case EWidgetAnimPhase::Hidden:
	{
		if (HasRunningLayers(OutAnimations, PlaybackState.OutStates))
		{
			PlayInternal(FallbackWidget, OutAnimations, PlaybackState.OutStates, DeltaTime, /*bIsOutAnimation=*/true, Baselines);
		}
		return false;
	}

	case EWidgetAnimPhase::Idle:
	default:
		return false;
	}
}

bool UWidgetAnimLibrary::TickWidgetAnimPlayback(UWidget* FallbackWidget, const TArray<FWidgetAnimSettings>& InAnimations, const TArray<FWidgetAnimSettings>& OutAnimations, FWidgetAnimPlaybackState& PlaybackState, float DeltaTime)
{
	return TickPlaybackInternal(FallbackWidget, InAnimations, OutAnimations, PlaybackState, DeltaTime, nullptr);
}

bool UWidgetAnimLibrary::TickWidgetAnimPlaybackOver(UWidget* FallbackWidget, const TArray<FWidgetAnimSettings>& InAnimations, const TArray<FWidgetAnimSettings>& OutAnimations, FWidgetAnimPlaybackState& PlaybackState, float DeltaTime, const FWidgetAnimBaselineSet& Baselines)
{
	return TickPlaybackInternal(FallbackWidget, InAnimations, OutAnimations, PlaybackState, DeltaTime, &Baselines);
}

// =====================================================================================================
// Baselines
// =====================================================================================================

FWidgetAnimBaseline UWidgetAnimLibrary::FindBaselineFor(const FWidgetAnimBaselineSet& Baselines, UWidget* Widget)
{
	for (const FWidgetAnimBaseline& Baseline : Baselines.Baselines)
	{
		if (Baseline.Widget == Widget)
		{
			return Baseline;
		}
	}

	// Identity: a widget nobody captured is animated exactly the way it always was.
	return FWidgetAnimBaseline();
}

namespace WidgetAnimAuthoredPoses
{
	/**
	 * What each design-time widget was authored as, shared by every preview that poses it - the Widget Animations panel's,
	 * a Widget Animation Component's toolbar preview, a state machine's. A preview reads a widget's pose as authored when
	 * it starts, then composes every frame over that; started while another preview had the widget mid-animation, it read
	 * that preview's pose instead - a cell at 0.97 of its size - and played every frame of its own scaled by it, so it ended
	 * short of where it should and never quite put the widget back. Read once, by whichever preview takes the widget first,
	 * and handed to each one after it until the last lets go. Only at design time: a playing game has one player per widget.
	 */
	struct FHeldPose
	{
		FWidgetAnimBaseline Pose;
		int32 Holders = 0;
	};

	struct FParameterKey
	{
		TWeakObjectPtr<UWidget> Widget;
		EWidgetAnimMaterialSlot Slot = EWidgetAnimMaterialSlot::Brush;
		FName Parameter;
		bool bVector = false;

		bool operator==(const FParameterKey& Other) const
		{
			return Widget == Other.Widget && Slot == Other.Slot && Parameter == Other.Parameter && bVector == Other.bVector;
		}

		friend uint32 GetTypeHash(const FParameterKey& Key)
		{
			return HashCombine(HashCombine(GetTypeHash(Key.Widget), GetTypeHash(static_cast<uint8>(Key.Slot))), HashCombine(GetTypeHash(Key.Parameter), GetTypeHash(Key.bVector)));
		}
	};

	struct FHeldParameter
	{
		float Scalar = 0.f;
		FLinearColor Vector = FLinearColor::White;
		int32 Holders = 0;
	};

	static TMap<TWeakObjectPtr<UWidget>, FHeldPose>& Poses()
	{
		static TMap<TWeakObjectPtr<UWidget>, FHeldPose> Held;
		return Held;
	}

	static TMap<FParameterKey, FHeldParameter>& Parameters()
	{
		static TMap<FParameterKey, FHeldParameter> Held;
		return Held;
	}

	static bool IsShared(const UWidget* Widget)
	{
		return Widget && Widget->IsDesignTime();
	}

	/** Every entry for a widget that is gone - design-time widgets are thrown away and made again all the time. */
	static void Compact()
	{
		for (auto It = Poses().CreateIterator(); It; ++It)
		{
			if (!It->Key.IsValid())
			{
				It.RemoveCurrent();
			}
		}
		for (auto It = Parameters().CreateIterator(); It; ++It)
		{
			if (!It->Key.Widget.IsValid())
			{
				It.RemoveCurrent();
			}
		}
	}

	static void ReleasePose(const UWidget* Widget)
	{
		if (!IsShared(Widget))
		{
			return;
		}
		const TWeakObjectPtr<UWidget> Key(const_cast<UWidget*>(Widget));
		if (FHeldPose* Held = Poses().Find(Key))
		{
			if (--Held->Holders <= 0)
			{
				Poses().Remove(Key);
			}
		}
	}

	static void ReleaseParameter(const FWidgetAnimMaterialBaseline& Baseline)
	{
		if (!IsShared(Baseline.Widget))
		{
			return;
		}
		const FParameterKey Key{ Baseline.Widget.Get(), Baseline.Slot, Baseline.ParameterName, Baseline.bVector };
		if (FHeldParameter* Held = Parameters().Find(Key))
		{
			if (--Held->Holders <= 0)
			{
				Parameters().Remove(Key);
			}
		}
	}
}

void UWidgetAnimLibrary::CaptureOne(FWidgetAnimBaselineSet& Baselines, UWidget* Widget)
{
	if (!Widget)
	{
		return;
	}

	for (const FWidgetAnimBaseline& Existing : Baselines.Baselines)
	{
		if (Existing.Widget == Widget)
		{
			return;
		}
	}

	FWidgetAnimBaseline& Baseline = Baselines.Baselines.AddDefaulted_GetRef();

	// Held by another preview already: what it read before it posed the widget, not the pose it has it in now.
	if (WidgetAnimAuthoredPoses::IsShared(Widget))
	{
		if (WidgetAnimAuthoredPoses::FHeldPose* Held = WidgetAnimAuthoredPoses::Poses().Find(Widget))
		{
			++Held->Holders;
			Baseline = Held->Pose;
			Baseline.Widget = Widget;
			return;
		}
	}

	const FWidgetTransform& Transform = Widget->GetRenderTransform();
	Baseline.Widget = Widget;
	Baseline.Opacity = Widget->GetRenderOpacity();
	Baseline.Scale = Transform.Scale;
	Baseline.Translation = Transform.Translation;
	Baseline.Shear = Transform.Shear;
	Baseline.Angle = Transform.Angle;

	if (WidgetAnimAuthoredPoses::IsShared(Widget))
	{
		if (WidgetAnimAuthoredPoses::Poses().Num() > 4096)
		{
			WidgetAnimAuthoredPoses::Compact();
		}
		WidgetAnimAuthoredPoses::FHeldPose& Held = WidgetAnimAuthoredPoses::Poses().Add(Widget);
		Held.Pose = Baseline;
		Held.Holders = 1;
	}
}

void UWidgetAnimLibrary::CaptureOneMaterialParameter(FWidgetAnimBaselineSet& Baselines, const FWidgetAnimSettings& Settings, UWidget* FallbackWidget)
{
	const bool bVector = Settings.AnimType == EWidgetAnimType::MaterialVector;
	const FName Parameter = bVector ? Settings.VectorParameterName : Settings.ScalarParameterName;
	UWidget* Named = Settings.GetMaterialWidget();
	UWidget* Widget = Named ? Named : FallbackWidget;
	UMaterialInterface* Material = FindWidgetMaterial(Widget, Settings.MaterialSlot);
	if (Parameter.IsNone() || !Material || Baselines.FindMaterialBaseline(Named, Settings.MaterialSlot, Parameter, bVector))
	{
		return;
	}

	FWidgetAnimMaterialBaseline& Baseline = Baselines.MaterialBaselines.AddDefaulted_GetRef();
	Baseline.Widget = Widget;
	Baseline.Slot = Settings.MaterialSlot;
	Baseline.ParameterName = Parameter;
	Baseline.bVector = bVector;
	Baseline.bIsFallback = Named == nullptr;

	// Held by another preview already: what the material held before that preview drove it. See WidgetAnimAuthoredPoses.
	const bool bShared = WidgetAnimAuthoredPoses::IsShared(Widget);
	const WidgetAnimAuthoredPoses::FParameterKey Key{ Widget, Settings.MaterialSlot, Parameter, bVector };
	if (bShared)
	{
		if (WidgetAnimAuthoredPoses::FHeldParameter* Held = WidgetAnimAuthoredPoses::Parameters().Find(Key))
		{
			++Held->Holders;
			Baseline.Scalar = Held->Scalar;
			Baseline.Vector = Held->Vector;
			return;
		}
	}

	const FHashedMaterialParameterInfo Info(Parameter);
	if (bVector)
	{
		Material->GetVectorParameterValue(Info, Baseline.Vector);
	}
	else
	{
		Material->GetScalarParameterValue(Info, Baseline.Scalar);
	}

	if (bShared)
	{
		if (WidgetAnimAuthoredPoses::Parameters().Num() > 4096)
		{
			WidgetAnimAuthoredPoses::Compact();
		}
		WidgetAnimAuthoredPoses::FHeldParameter& Held = WidgetAnimAuthoredPoses::Parameters().Add(Key);
		Held.Scalar = Baseline.Scalar;
		Held.Vector = Baseline.Vector;
		Held.Holders = 1;
	}
}

void UWidgetAnimLibrary::CaptureOneShape(FWidgetAnimBaselineSet& Baselines, UShapeBox* Box, bool bIsFallback)
{
	if (!Box)
	{
		return;
	}

	for (FWidgetAnimShapeBaseline& Existing : Baselines.ShapeBaselines)
	{
		if (Existing.Box == Box)
		{
			// Already captured - under a name, or as the fallback. The same box can be both, and must not be
			// captured twice or the second read would record a pose the animation had already written.
			Existing.bIsFallback |= bIsFallback;
			return;
		}
	}

	FWidgetAnimShapeBaseline& Baseline = Baselines.ShapeBaselines.AddDefaulted_GetRef();
	Baseline.Box = Box;
	Baseline.Shape = Box->GetShape();
	Baseline.bIsFallback = bIsFallback;
}

void UWidgetAnimLibrary::CaptureWidgetAnimBaselines(UWidget* FallbackWidget, const TArray<FWidgetAnimSettings>& InAnimationsToExpand, const TArray<FWidgetAnimSettings>& OutAnimationsToExpand, FWidgetAnimBaselineSet& Baselines)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(WidgetAnimCaptureBaselines);
	// Fanned out first, so a stagger's children are captured one by one - each composes over, and goes back to, its own
	// authored pose - and a text stagger's Animated Text Block is captured, so putting it back also puts its letters back.
	// Children added after this capture have none, and are composed over no pose of their own.
	TArray<FWidgetAnimSettings> ExpandedIn;
	TArray<FWidgetAnimSettings> ExpandedOut;
	const TArray<FWidgetAnimSettings>& InAnimations = ExpandSettings(FallbackWidget, InAnimationsToExpand, ExpandedIn) ? ExpandedIn : InAnimationsToExpand;
	const TArray<FWidgetAnimSettings>& OutAnimations = ExpandSettings(FallbackWidget, OutAnimationsToExpand, ExpandedOut) ? ExpandedOut : OutAnimationsToExpand;

	// Capturing twice would read back a pose the animation itself wrote, and every capture after the first
	// would drift a little further from what was actually authored. Call ResetBaselines first if the set
	// really does need rebuilding - after restoring it, not before.
	if (Baselines.bCaptured)
	{
		return;
	}

	Baselines.bCaptured = true;
	Baselines.Baselines.Reset();
	Baselines.ShapeBaselines.Reset();
	Baselines.AuthoritativeBaselines.Reset();

	bool bNeedsFallback = false;
	bool bNeedsShapeFallback = false;

	// What has been captured already, so each widget and each parameter is looked for in the set only once: hundreds of tracks
	// on the same widgets would otherwise search every baseline taken so far, for every track.
	TSet<const UWidget*> CapturedWidgets;
	TSet<TTuple<const UWidget*, uint8, FName, bool>> CapturedParameters;

	auto Gather = [&Baselines, &bNeedsFallback, &bNeedsShapeFallback, &CapturedWidgets, &CapturedParameters, FallbackWidget](const TArray<FWidgetAnimSettings>& Animations)
	{
		for (const FWidgetAnimSettings& Settings : Animations)
		{
			// A registered type keeps what its own channel held - a Text Block's text, say - and never touches the
			// transform, so there is no pose of its widget to keep. Resolved here, against the fallback widget when the
			// track names none, the same way the result is applied.
			if (Settings.AnimType == EWidgetAnimType::Custom)
			{
				// A cue writes nothing, so has nothing to keep.
				const FWidgetAnimEvaluator* Evaluator = Settings.FindEvaluator();
				if (Evaluator && !Evaluator->IsCue())
				{
					UWidget* Widget = Settings.TargetWidget ? Settings.TargetWidget.Get() : FallbackWidget;
					CaptureOneAuthoritative(Baselines, *Evaluator, Settings.TypeParams, Widget);
				}
				continue;
			}

			// A Shape Box layer needs the box's authored outline kept, both to restore it afterwards and because a
			// key's Use Authored Shape resolves against it. It poses nothing else.
			if (Settings.AnimType == EWidgetAnimType::ShapeBox)
			{
				if (Settings.Part.IsSet())
				{
					continue;
				}
				if (Settings.ShapeTargetBox)
				{
					CaptureOneShape(Baselines, Settings.ShapeTargetBox);
				}
				else
				{
					bNeedsShapeFallback = true;
				}
				continue;
			}

			// A Shape layer keeps its shape's settings as they were authored, the way a registered type keeps its
			// channel - to put back, and for its keys' Use Authored Shape - and a Design layer its widget's look.
			if (Settings.AnimType == EWidgetAnimType::Shape || Settings.AnimType == EWidgetAnimType::Design)
			{
				const FWidgetAnimEvaluator* Evaluator = Settings.AnimType == EWidgetAnimType::Design ? WidgetAnimDesignTrack::FindEvaluator() : WidgetAnimShapeTrack::FindEvaluator();
				if (Evaluator && !Settings.Part.IsSet())
				{
					UWidget* Widget = Settings.TargetWidget ? Settings.TargetWidget.Get() : FallbackWidget;
					CaptureOneAuthoritative(Baselines, *Evaluator, Settings.TypeParams, Widget);
				}
				continue;
			}

			if (Settings.AnimType == EWidgetAnimType::MaterialScalar || Settings.AnimType == EWidgetAnimType::MaterialVector)
			{
				// Material entries write parameters, not a transform: what the parameter held is kept instead, read off the
				// widget's material, to go back to when the track hasn't started or the animation is let go.
				const bool bVector = Settings.AnimType == EWidgetAnimType::MaterialVector;
				const TTuple<const UWidget*, uint8, FName, bool> Key(Settings.GetMaterialWidget(), static_cast<uint8>(Settings.MaterialSlot),
					bVector ? Settings.VectorParameterName : Settings.ScalarParameterName, bVector);
				bool bAlready = false;
				CapturedParameters.Add(Key, &bAlready);
				if (!bAlready)
				{
					CaptureOneMaterialParameter(Baselines, Settings, FallbackWidget);
				}
				continue;
			}

			if (Settings.TargetWidget)
			{
				bool bAlready = false;
				CapturedWidgets.Add(Settings.TargetWidget.Get(), &bAlready);
				if (!bAlready)
				{
					CaptureOne(Baselines, Settings.TargetWidget);
				}
			}
			else
			{
				bNeedsFallback = true;
			}
		}
	};

	Gather(InAnimations);
	Gather(OutAnimations);

	if (bNeedsFallback)
	{
		CaptureOne(Baselines, FallbackWidget);
	}

	if (bNeedsShapeFallback)
	{
		CaptureOneShape(Baselines, ResolveShapeBox(FallbackWidget), /*bIsFallback=*/true);
	}
}

void UWidgetAnimLibrary::RestoreWidgetAnimBaselines(const FWidgetAnimBaselineSet& Baselines)
{
	for (const FWidgetAnimBaseline& Baseline : Baselines.Baselines)
	{
		UWidget* Widget = Baseline.Widget;
		if (!Widget)
		{
			continue;
		}

		// No longer posed by this preview - see WidgetAnimAuthoredPoses.
		WidgetAnimAuthoredPoses::ReleasePose(Widget);

		Widget->SetRenderOpacity(Baseline.Opacity);
		Widget->SetRenderScale(Baseline.Scale);
		Widget->SetRenderTranslation(Baseline.Translation);
		Widget->SetRenderShear(Baseline.Shear);
		Widget->SetRenderTransformAngle(Baseline.Angle);
		WidgetAnimPixelSnapping::Restore(Widget);

		// Its parts too: a part at rest is where the widget lays it out, which is all there is to put back.
		if (IWidgetAnimPartHost* PartHost = Cast<IWidgetAnimPartHost>(Widget))
		{
			PartHost->ClearWidgetAnimPartPoses();
		}
	}

	// Outlines go back whole. Nothing partial to undo: a Shape Box track replaced the shape rather than
	// composing with it, so the authored one is the only thing there is to restore.
	for (const FWidgetAnimShapeBaseline& ShapeBaseline : Baselines.ShapeBaselines)
	{
		ApplyShapeToBox(ShapeBaseline.Box, ShapeBaseline.Shape);
	}

	// Material parameters go back to what they held - on the widget's own dynamic material, where an animation set them.
	// A widget whose material was never made dynamic was never changed, and is left as it is.
	for (const FWidgetAnimMaterialBaseline& Baseline : Baselines.MaterialBaselines)
	{
		WidgetAnimAuthoredPoses::ReleaseParameter(Baseline);

		UWidget* Widget = Baseline.Widget;
		UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(FindWidgetMaterial(Widget, Baseline.Slot));
		if (!MID)
		{
			continue;
		}
		if (Baseline.bVector)
		{
			MID->SetVectorParameterValue(Baseline.ParameterName, Baseline.Vector);
		}
		else
		{
			MID->SetScalarParameterValue(Baseline.ParameterName, Baseline.Scalar);
		}
	}

	// Registered types' channels go back whole too, each by the type that captured it.
	for (const FWidgetAnimAuthoritativeBaseline& Baseline : Baselines.AuthoritativeBaselines)
	{
		UObject* Target = Baseline.Target;
		const FWidgetAnimEvaluator* Capturer = FWidgetAnimEvaluatorRegistry::Get().Find(Baseline.CapturedBy.Get());
		if (Target && Capturer)
		{
			Capturer->RestoreBaseline(Target, Baseline.Captured);
			ForgetAuthoritativeWrite(Target, Baseline.Channel);
		}
	}
}

void UWidgetAnimLibrary::ResetWidgetRenderState(UWidget* Widget)
{
	if (!Widget)
	{
		return;
	}

	Widget->SetRenderOpacity(1.f);
	Widget->SetRenderScale(FVector2D(1.f, 1.f));
	Widget->SetRenderTranslation(FVector2D::ZeroVector);
	Widget->SetRenderShear(FVector2D::ZeroVector);
	Widget->SetRenderTransformAngle(0.f);
	WidgetAnimPixelSnapping::Restore(Widget);

	if (IWidgetAnimPartHost* PartHost = Cast<IWidgetAnimPartHost>(Widget))
	{
		PartHost->ClearWidgetAnimPartPoses();
	}
}

// =====================================================================================================
// Posing and preview
// =====================================================================================================

void UWidgetAnimLibrary::ApplyWidgetAnimationsAtAlpha(UWidget* FallbackWidget, const TArray<FWidgetAnimSettings>& InAnimations, float Alpha, bool bIsOutAnimation, const FWidgetAnimBaselineSet& Baselines)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(WidgetAnimApplyAtAlpha);
	// Alpha is along the pass with every stagger fanned out, which is the pass the Widget Animations editor shows.
	TArray<FWidgetAnimSettings> Expanded;
	const TArray<FWidgetAnimSettings>& Animations = ExpandSettings(FallbackWidget, InAnimations, Expanded) ? Expanded : InAnimations;

	const float ClampedAlpha = FMath::Clamp(Alpha, 0.f, 1.f);

	// Alpha is a point in TIME along one pass of the stack, and every layer is posed where it actually is
	// at that time - its own Delay included, the same way TickAnimationsInto places it on a playing clock.
	// Posing every layer at the same alpha instead is what made a scrub show a staggered animation as if
	// every layer started together. A layer that hasn't started yet leaves the widget alone, exactly as it
	// does on a playing clock - an inverted one excepted, which holds its end pose until it starts.
	const float StackTime = ClampedAlpha * GetStackPassDuration(Animations);

	FWidgetAnimResult Result;
	Result.bAllFinished = true;
	EvaluateExpandedStackAtTime(Animations, StackTime, /*bAtEnd=*/ClampedAlpha >= 1.f, bIsOutAnimation, Baselines, Result);

	ApplyResultInternal(FallbackWidget, Result, Baselines.bCaptured ? &Baselines : nullptr);
	ApplyMaterialAnimResult(Result, FallbackWidget);
}

void UWidgetAnimLibrary::EvaluateExpandedStackAtTime(const TArray<FWidgetAnimSettings>& Animations, float StackTime, bool bAtEnd, bool bIsOutAnimation, const FWidgetAnimBaselineSet& Baselines, FWidgetAnimResult& Result)
{
	for (const FWidgetAnimSettings& Settings : Animations)
	{
		const WidgetAnimLoop::FLayerSpan Span = WidgetAnimLoop::GetLayerSpan(Settings);
		const float LayerAlpha = bAtEnd ? 1.f : WidgetAnimLoop::GetLayerAlpha(StackTime - Span.Start, Span);

		const bool bInvert = GetEffectiveInvert(Settings, bIsOutAnimation);
		if (!Settings.bMuted && !bInvert && StackTime < Span.Start && Settings.bHoldStartPose)
		{
			// A staggered item scrubbed to before its turn: at its start pose, as it is while it waits on a clock.
			ApplyAnimationContribution(Settings, 0.f, Result, Baselines.bCaptured ? &Baselines : nullptr, /*bTowardStart=*/false);
			continue;
		}
		if (Settings.bMuted || (!bInvert && StackTime < Span.Start))
		{
			// Scrubbed back to before it starts: its widget goes back to how it was authored, rather than
			// keeping the pose the last frame left - which is what left a fade half done after a fast scrub.
			if (Baselines.bCaptured)
			{
				TouchLayerTargets(Settings, Result, Baselines);
			}
			continue;
		}
		// One pass has no return legs, so the only thing that turns a layer toward its start here is inversion.
		ApplyAnimationContribution(Settings, bInvert ? 1.f - LayerAlpha : LayerAlpha, Result, Baselines.bCaptured ? &Baselines : nullptr, /*bTowardStart=*/bInvert);
	}
}

FWidgetTransform UWidgetAnimLibrary::GetResultPose(const FWidgetAnimResult& Result, const UWidget* Widget, const UWidget* FallbackWidget, const FWidgetAnimBaselineSet& Baselines)
{
	if (!Widget)
	{
		return FWidgetTransform();
	}

	const FWidgetAnimBaseline* Baseline = nullptr;
	if (Baselines.bCaptured)
	{
		Baseline = Baselines.Baselines.FindByPredicate([Widget](const FWidgetAnimBaseline& Candidate) { return Candidate.Widget.Get() == Widget; });
	}

	// The same composition ApplyResultInternal writes: over the baseline when transforms are composed, else as it is.
	for (const FWidgetAnimTargetedTransform& Transform : Result.TargetedTransforms)
	{
		const UWidget* Target = Transform.TargetWidget ? Transform.TargetWidget : FallbackWidget;
		if (Target != Widget || Transform.Part.IsSet())
		{
			continue;
		}
		if (Baselines.bCaptured && Baselines.bComposeTransforms && Baseline)
		{
			return FWidgetTransform(Baseline->Translation + Transform.Translation, MakeInvertibleScale(Baseline->Scale * Transform.Scale), Baseline->Shear + Transform.Shear, Baseline->Angle + Transform.Angle);
		}
		return FWidgetTransform(Transform.Translation, MakeInvertibleScale(Transform.Scale), Transform.Shear, Transform.Angle);
	}

	// Not posed: at rest.
	if (Baseline)
	{
		return FWidgetTransform(Baseline->Translation, MakeInvertibleScale(Baseline->Scale), Baseline->Shear, Baseline->Angle);
	}
	return Widget->GetRenderTransform();
}

void UWidgetAnimLibrary::FirePreviewCues(UWidget* FallbackWidget, const TArray<FWidgetAnimSettings>& InAnimations, float FromAlpha, float ToAlpha, bool bIsOutAnimation, bool bIncludeFrom)
{
	if (!FallbackWidget)
	{
		return;
	}

	// Asked of the layers as authored - a stagger's items are the same type - so a preview with no cue costs nothing more.
	const bool bHasCues = InAnimations.ContainsByPredicate([](const FWidgetAnimSettings& Settings)
	{
		return Settings.AnimType == EWidgetAnimType::Custom && WidgetAnimIsRegisteredCue(Settings.TypeParams);
	});
	if (!bHasCues)
	{
		return;
	}

	// The same pass ApplyWidgetAnimationsAtAlpha poses along, stagger items and all.
	TArray<FWidgetAnimSettings> Expanded;
	const TArray<FWidgetAnimSettings>& Animations = ExpandSettings(FallbackWidget, InAnimations, Expanded) ? Expanded : InAnimations;

	// One stretch of one pass, laid out the way a clock's would be: forward from FromAlpha to ToAlpha, or back. A preview
	// posing by alpha has no loop legs of its own, so each call is one leg's worth - the preview splits a wrap in two.
	const float PassSeconds = GetStackPassDuration(Animations);
	WidgetAnimLoop::FClockPieces Pieces;
	WidgetAnimLoop::FClockPiece& Piece = Pieces.AddDefaulted_GetRef();
	Piece.Leader = 0;
	Piece.From = FMath::Clamp(FromAlpha, 0.f, 1.f) * PassSeconds;
	Piece.To = FMath::Clamp(ToAlpha, 0.f, 1.f) * PassSeconds;
	Piece.bForward = ToAlpha >= FromAlpha;
	Piece.bIncludeFrom = bIncludeFrom && Piece.bForward;
	Piece.LegEnd = PassSeconds;

	FWidgetAnimResult Result;
	for (int32 Index = 0; Index < Animations.Num(); ++Index)
	{
		const FWidgetAnimSettings& Settings = Animations[Index];
		if (Settings.bMuted || Settings.AnimType != EWidgetAnimType::Custom || !WidgetAnimIsRegisteredCue(Settings.TypeParams))
		{
			continue;
		}
		WidgetAnimLoop::GatherClockCues(Settings, Index, WidgetAnimLoop::GetLayerSpan(Settings), GetEffectiveInvert(Settings, bIsOutAnimation), /*Leader=*/0, Pieces, Result);
	}

	Algo::StableSortBy(Result.CueFires, &FWidgetAnimCueFire::Order);
	WidgetAnimCues::Queue(Result.CueFires, FallbackWidget, NAME_None, /*bPreview=*/true);
}

bool FWidgetAnimLayer::FitsAnimationToTrack() const
{
	return IsAnimationTrack() && (bFitToTrack || IsValueDriven() || InsertedAnimation->IsPercentTime());
}

float FWidgetAnimLayer::GetRunLength() const
{
	// An animation track is as long as its animation plays - or, stretched across the track, as long as the track is.
	if (IsAnimationTrack())
	{
		return FitsAnimationToTrack()
			? FMath::Max(Duration, KINDA_SMALL_NUMBER)
			: FMath::Max(UWidgetAnimLibrary::GetAnimationPassSeconds(InsertedAnimation.Get()), KINDA_SMALL_NUMBER);
	}

	if (WidgetAnimTypeIsKeyframed(AnimType))
	{
		return Transform.GetDuration();
	}

	// The same number GetEffectiveLayerDuration gives the settings built from this layer, so a layer is
	// placed after the one above it by exactly the length it then plays for.
	if (IsSpringTimed())
	{
		return UWidgetAnimSpringLibrary::GetSettleTime(Spring);
	}

	return FMath::Max(Duration, KINDA_SMALL_NUMBER);
}

FString FWidgetAnimLayerGroup::GetDisplayName(int32 GroupIndex) const
{
	return GroupName.TrimStartAndEnd().IsEmpty() ? MakeDefaultName(GroupIndex) : GroupName;
}

FString FWidgetAnimLayerGroup::MakeDefaultName(int32 GroupIndex)
{
	return FString::Printf(TEXT("Track Group %d"), GroupIndex + 1);
}

FString FWidgetAnimLayerGroup::UpgradeLegacyName(const FString& Name)
{
	static const FString LegacyStem(TEXT("Layer Group "));
	if (Name.StartsWith(LegacyStem, ESearchCase::CaseSensitive))
	{
		const FString Number = Name.Mid(LegacyStem.Len());
		if (!Number.IsEmpty() && Number.IsNumeric() && !Number.Contains(TEXT(".")) && !Number.Contains(TEXT("-")))
		{
			return TEXT("Track Group ") + Number;
		}
	}
	return Name;
}

FString FWidgetAnimLayerGroup::MakeLegacyDefaultName(const FString& Name)
{
	static const FString Stem(TEXT("Track Group "));
	if (Name.StartsWith(Stem, ESearchCase::CaseSensitive))
	{
		const FString Number = Name.Mid(Stem.Len());
		if (!Number.IsEmpty() && Number.IsNumeric() && !Number.Contains(TEXT(".")) && !Number.Contains(TEXT("-")))
		{
			return TEXT("Layer Group ") + Number;
		}
	}
	return FString();
}

bool UWidgetAnimLibrary::NormalizeLayerGroups(TArray<FWidgetAnimLayerGroup>& Groups, TArray<FWidgetAnimLayer>& Layers)
{
	bool bChanged = false;

	// Layers held inside their groups: into the single list, group by group, each remembering its group.
	for (int32 GroupIndex = 0; GroupIndex < Groups.Num(); ++GroupIndex)
	{
		TArray<FWidgetAnimLayer>& Held = Groups[GroupIndex].Layers;
		if (Held.Num() == 0)
		{
			continue;
		}

		for (FWidgetAnimLayer& Layer : Held)
		{
			Layer.LayerGroup = GroupIndex;
			Layers.Add(MoveTemp(Layer));
		}
		Held.Reset();
		bChanged = true;
	}

	// Layers from before groups belong to the one group they were always played on.
	if (Layers.Num() > 0 && Groups.Num() == 0)
	{
		Groups.AddDefaulted();
		bChanged = true;
	}

	for (FWidgetAnimLayer& Layer : Layers)
	{
		const int32 InRange = GetLayerGroupIndex(Layer, Groups.Num());
		if (InRange != INDEX_NONE && InRange != Layer.LayerGroup)
		{
			Layer.LayerGroup = InRange;
			bChanged = true;
		}
	}

	return bChanged;
}

int32 UWidgetAnimLibrary::GetLayerGroupIndex(const FWidgetAnimLayer& Layer, int32 NumGroups)
{
	return NumGroups > 0 ? FMath::Clamp(Layer.LayerGroup, 0, NumGroups - 1) : INDEX_NONE;
}

namespace
{
	/**
	 * Process-unique ids for the lists BuildLayerSettings places, so a stagger fanning out at play time moves only the
	 * layers placed in the same list as it - never those of another entry played alongside. Masked to stay
	 * non-negative, since INDEX_NONE means "not placed there".
	 */
	int32 AllocatePlacementChain()
	{
		static std::atomic<uint32> NextChain{ 0 };
		return static_cast<int32>(NextChain.fetch_add(1, std::memory_order_relaxed) & 0x7FFFFFFFu);
	}

	/**
	 * The one placing rule, applied to one layer after another: where a layer starts, from the start and end of the one
	 * above it. A negative Delay overlaps the layer above - see FWidgetAnimLayer::Delay - but never reaches back before
	 * the animation starts.
	 */
	struct FLayerPlacer
	{
		float PreviousStart = 0.f;
		float PreviousEnd = 0.f;

		/** Places one layer and returns its start. Span is how long it keeps the next layer waiting if that one is After Previous. */
		float Place(bool bAfterPrevious, float Delay, float Span)
		{
			const float Anchor = bAfterPrevious ? PreviousEnd : PreviousStart;
			const float Start = FMath::Max(Anchor + Delay, 0.f);
			PreviousStart = Start;
			PreviousEnd = Start + Span;
			return Start;
		}
	};
}

void UWidgetAnimLibrary::PlaceLayerStarts(const TArray<FWidgetAnimLayer>& Layers, TConstArrayView<float> ExtraSpans, TArray<float>& OutStarts)
{
	OutStarts.Reset(Layers.Num());

	// Both begin at 0, so the first layer counts from the start whichever way it is set to start.
	FLayerPlacer Placer;
	for (int32 Index = 0; Index < Layers.Num(); ++Index)
	{
		const FWidgetAnimLayer& Layer = Layers[Index];
		const float Extra = ExtraSpans.IsValidIndex(Index) ? ExtraSpans[Index] : 0.f;
		OutStarts.Add(Placer.Place(Layer.Start == EWidgetAnimLayerStart::AfterPrevious, Layer.Delay, FMath::Max(Layer.GetRunLength() + Extra, 0.f)));
	}
}

void UWidgetAnimLibrary::BuildLayerSettings(const TArray<FWidgetAnimLayer>& Layers, TArray<FWidgetAnimSettings>& OutSettings)
{
	OutSettings.Reserve(OutSettings.Num() + Layers.Num());

	// Staggers haven't fanned out yet - that happens where the list plays, on its widget - so each is placed as its
	// first item alone. How it was placed is kept, for moving what follows it once it has fanned out; see ExpandSettings.
	TArray<float> Starts;
	PlaceLayerStarts(Layers, TConstArrayView<float>(), Starts);
	const int32 Chain = AllocatePlacementChain();

	// Where each animation track's placeholder went, for its animation's layers to follow once the list's own are in.
	TArray<int32, TInlineAllocator<4>> AnimationTracks;

	for (int32 Index = 0; Index < Layers.Num(); ++Index)
	{
		const FWidgetAnimLayer& Layer = Layers[Index];
		FWidgetAnimSettings Settings = Layer.ToSettings();

		// From here on Delay means seconds from the start of the animation, which is what the clock, the
		// scrub and every duration helper read it as. Speed is applied to it later, uniformly, so the
		// sequence keeps its proportions at any playback speed.
		Settings.Delay = Starts[Index];

		Settings.PlacementChain = Chain;
		Settings.bPlacedAfterPrevious = Layer.Start == EWidgetAnimLayerStart::AfterPrevious;
		Settings.PlacedDelay = Layer.Delay;
		Settings.PlacedRun = Layer.GetRunLength();

		// An animation track keeps its place as a placeholder as long as its animation, which poses nothing - its
		// animation's own layers do the playing - so the layers after it are placed, and moved, as they always were.
		if (Layer.IsAnimationTrack())
		{
			Settings.Duration = Settings.PlacedRun;
			Settings.Expander.Reset();
			Settings.bMuted = true;
			Settings.PlacementAnchor = AllocatePlacementChain();
			AnimationTracks.Add(Index);

			// Its Value timing is its animation's - a value moving it along the track (see below) - not one of its own:
			// the placeholder keeps its place on the clock like any track.
			Settings.Timing = EWidgetAnimTiming::Tween;
		}

		OutSettings.Add(MoveTemp(Settings));
	}

	if (AnimationTracks.Num() == 0)
	{
		return;
	}

	// Deep enough for any animation made of animations, and no deeper: an animation inserted into itself, however
	// roundabout, stops here rather than building forever. The panel won't insert one; an asset edited by hand might.
	static constexpr int32 MaxInsertDepth = 8;
	static thread_local int32 InsertDepth = 0;
	if (InsertDepth >= MaxInsertDepth)
	{
		return;
	}
	TGuardValue<int32> DepthGuard(InsertDepth, InsertDepth + 1);

	const int32 FirstPlaceholder = OutSettings.Num() - Layers.Num();
	for (const int32 Index : AnimationTracks)
	{
		const FWidgetAnimLayer& Track = Layers[Index];
		const UWidgetAnimPresetAsset* Animation = Track.InsertedAnimation.Get();
		if (!Animation)
		{
			continue;
		}

		// The inserted animation as it plays on its own - its own animations inserted in turn - then moved to where the
		// track starts, at its own pace. With its parameters: its defaults, and the values of the animation it plays in by name.
		TArray<FWidgetAnimSettings> Inserted;
		{
			FWidgetAnimParameterResolver InsertedParameters;
			TArray<FWidgetAnimLayer> ResolvedLayers;
			TSet<int32> MutedLayers;
			const TArray<FWidgetAnimLayer>* InsertedLayers = &Animation->Layers;
			const bool bHasParameterValues = InsertedParameters.Resolve(*Animation, nullptr, FWidgetAnimParameterResolver::GetCurrent());
			if (bHasParameterValues && InsertedParameters.Apply(Animation->Layers, ResolvedLayers, MutedLayers))
			{
				InsertedLayers = &ResolvedLayers;
			}
			const FWidgetAnimParameterResolver::FCurrentScope CurrentParameters(bHasParameterValues ? &InsertedParameters : FWidgetAnimParameterResolver::GetCurrent());
			BuildLayerSettings(*InsertedLayers, Inserted);

			// A track of it whose Plays When doesn't hold poses nothing, nor does what it inserts in turn.
			if (MutedLayers.Num() > 0)
			{
				const int32 NumInsertedOwn = Animation->Layers.Num();
				for (int32 InsertedIndex = 0; InsertedIndex < Inserted.Num(); ++InsertedIndex)
				{
					const int32 LayerIndex = InsertedIndex < NumInsertedOwn ? InsertedIndex : Inserted[InsertedIndex].InsertedFromLayer;
					if (MutedLayers.Contains(LayerIndex))
					{
						Inserted[InsertedIndex].bMuted = true;
					}
				}
			}
		}

		// Copied rather than referred to: OutSettings grows below.
		const float TrackStart = OutSettings[FirstPlaceholder + Index].Delay;
		const int32 TrackAnchor = OutSettings[FirstPlaceholder + Index].PlacementAnchor;
		const float Speed = GetInsertedPlaySpeed(Track);
		const int32 NumInsertedGroups = Animation->LayerGroups.Num();
		const int32 NumOwn = Animation->Layers.Num();

		// Timing set to Value: the animation follows a float value of its own along the track, from 0 at its start to 1 at
		// its end - set by the track's Value Name, smoothed and limited as the track says - rather than the clock of the list
		// it is in. One progress clock for all of it, anchored to the track. See FWidgetAnimProgressDrive.
		TSharedPtr<FWidgetAnimProgressDrive> Drive;
		if (Track.IsValueDriven() && !Track.ValueName.IsNone())
		{
			Drive = MakeShared<FWidgetAnimProgressDrive>();
			Drive->ValueName = Track.ValueName;
			Drive->Options = Track.ValueOptions;
			Drive->Options.Kind = EWidgetAnimValueKind::Float;
			const FWidgetAnimValueMotion Motion = Track.GetValueMotion();
			Drive->Smoothing = Motion.Smoothing;
			Drive->TweenDuration = Motion.TweenDuration;
			Drive->TweenEasing = Motion.TweenEasing;
			Drive->TweenCurve = Motion.TweenCurve;
			Drive->Spring = Track.Spring;
			Drive->SteadySpeed = Motion.SteadySpeed;
			Drive->SteadyCatchUpTime = Motion.SteadyCatchUpTime;
			Drive->Anchor = TrackAnchor;
			Drive->ValueInput = Track.ValueInput;
		}

		OutSettings.Reserve(OutSettings.Num() + Inserted.Num());
		for (int32 InsertedIndex = 0; InsertedIndex < Inserted.Num(); ++InsertedIndex)
		{
			FWidgetAnimSettings& Settings = Inserted[InsertedIndex];

			// Its clock runs at the inserted animation's pace times whatever it already ran at, and its Delay - read in
			// its own seconds - gets the track's start added in those seconds, so it starts exactly TrackStart into this
			// list plus however far into the inserted animation it did.
			Settings.NestedSpeed *= Speed;
			Settings.Delay += TrackStart * Settings.NestedSpeed;

			// It moves with the track, or with the track of its own it is in - which moves with this one in turn.
			if (Settings.AnchoredTo == INDEX_NONE)
			{
				Settings.AnchoredTo = TrackAnchor;
			}

			// The group it plays in here: its own group in the inserted animation, as that animation's track says -
			// or, for a layer inserted deeper, the group it was already given there - mapped by this track.
			const int32 InsertedGroup = InsertedIndex < NumOwn
				? GetLayerGroupIndex(Animation->Layers[InsertedIndex], NumInsertedGroups)
				: Settings.InsertedGroup;
			Settings.InsertedGroup = Track.InsertedGroups.IsValidIndex(InsertedGroup) && Track.InsertedGroups[InsertedGroup] >= 0
				? Track.InsertedGroups[InsertedGroup]
				: Track.LayerGroup;
			Settings.InsertedFromLayer = Index;

			// Which animation an event it fires names: the one it is authored in - or, inserted deeper, the one it already
			// has. Its values still belong to the animation it plays as part of, stamped by whoever plays this list.
			if (!Settings.AuthoredIn)
			{
				Settings.AuthoredIn = const_cast<UWidgetAnimPresetAsset*>(Animation);
			}

			// Following the track's value - unless an animation inserted deeper follows one of its own, or the layer is a
			// value track, which follows its own value wherever it is.
			if (Drive && !Settings.ProgressDrive && !Settings.IsValueDriven())
			{
				Settings.ProgressDrive = Drive;
			}

			OutSettings.Add(MoveTemp(Settings));
		}
	}
}

float UWidgetAnimLibrary::GetAnimationPassSeconds(const UWidgetAnimPresetAsset* Animation)
{
	// The same depth BuildLayerSettings stops inserting at: an animation inserted into itself has no length to find.
	static constexpr int32 MaxInsertDepth = 8;
	static thread_local int32 InsertDepth = 0;
	if (!Animation || InsertDepth >= MaxInsertDepth)
	{
		return 0.f;
	}
	TGuardValue<int32> DepthGuard(InsertDepth, InsertDepth + 1);

	TArray<float> Starts;
	PlaceLayerStarts(Animation->Layers, TConstArrayView<float>(), Starts);

	float End = 0.f;
	for (int32 Index = 0; Index < Starts.Num(); ++Index)
	{
		End = FMath::Max(End, Starts[Index] + Animation->Layers[Index].GetRunLength());
	}

	// Time after its last track, when it has some - see AddMinimumLengthPlaceholder.
	End = FMath::Max(End, Animation->GetEffectiveMinimumLength());
	return End / Animation->GetEffectiveSpeedMultiplier();
}

float UWidgetAnimLibrary::GetAnimationAuthoredLength(const UWidgetAnimPresetAsset* Animation)
{
	if (!Animation)
	{
		return 0.f;
	}

	// 100%, whatever its tracks reach: a progress of 1 is one second into it.
	if (Animation->IsPercentTime())
	{
		return 1.f;
	}
	return GetAnimationPassSeconds(Animation) * Animation->GetEffectiveSpeedMultiplier();
}

float UWidgetAnimLibrary::GetInsertedPlaySpeed(const FWidgetAnimLayer& Track)
{
	const UWidgetAnimPresetAsset* Animation = Track.InsertedAnimation.Get();
	if (!Animation)
	{
		return 1.f;
	}

	if (Track.FitsAnimationToTrack())
	{
		const float Length = GetAnimationAuthoredLength(Animation);
		return Length > KINDA_SMALL_NUMBER ? FMath::Max(Length / FMath::Max(Track.Duration, KINDA_SMALL_NUMBER), 0.0001f) : 1.f;
	}
	return Animation->GetEffectiveSpeedMultiplier();
}

void UWidgetAnimLibrary::RetimeLayer(FWidgetAnimLayer& Layer, float Speed, float MinSeconds)
{
	if (FMath::IsNearlyEqual(Speed, 1.f) || Speed <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	// An animation track is retimed by stretching its animation across its new length.
	if (Layer.IsAnimationTrack() && !Layer.FitsAnimationToTrack())
	{
		Layer.Duration = Layer.GetRunLength();
		Layer.bFitToTrack = true;
	}

	Layer.Duration = FMath::Max(Layer.Duration / Speed, MinSeconds);
	Layer.Delay /= Speed;
	if (FRichCurve* Curve = WidgetAnimTypeIsKeyframed(Layer.AnimType) && Layer.Transform.Curve.ExternalCurve == nullptr ? Layer.Transform.Curve.GetRichCurve() : nullptr)
	{
		for (FRichCurveKey& Key : Curve->Keys)
		{
			Key.Time /= Speed;
		}
	}
	if (Layer.IsSpringTimed())
	{
		// The same spring, Speed times faster: see RetimeWidgetAnimation.
		Layer.Spring.VisualDuration = FMath::Max(Layer.Spring.VisualDuration / Speed, 0.02f);
		Layer.Spring.Stiffness *= Speed * Speed;
		Layer.Spring.Damping *= Speed;
		Layer.Spring.InitialVelocity *= Speed;
	}
	if (FWidgetAnimExpander* Stagger = Layer.Expander.GetMutablePtr<FWidgetAnimExpander>())
	{
		Stagger->Interval /= Speed;
		Stagger->Spread /= Speed;
	}
	if (Layer.AnimType == EWidgetAnimType::Shake)
	{
		Layer.Shake.Frequency *= Speed;
	}
}

void UWidgetAnimLibrary::ConvertTimeUnit(TArray<FWidgetAnimLayer>& Layers, EWidgetAnimTimeUnit From, EWidgetAnimTimeUnit To, float& InOutMinimumLength, float& InOutSpeedMultiplier)
{
	if (From == To)
	{
		return;
	}

	if (To == EWidgetAnimTimeUnit::Seconds)
	{
		// 100% is one second already: nothing moves, and the animation keeps running its full second.
		InOutMinimumLength = 1.f;
		InOutSpeedMultiplier = 1.f;
		return;
	}

	// The whole of one pass as authored - to its last track's end, or its Minimum Length - becomes 0% - 100%.
	TArray<float> Starts;
	PlaceLayerStarts(Layers, TConstArrayView<float>(), Starts);
	float End = FMath::Max(InOutMinimumLength, 0.f);
	for (int32 Index = 0; Index < Layers.Num(); ++Index)
	{
		End = FMath::Max(End, Starts[Index] + Layers[Index].GetRunLength());
	}

	// Every animation track is stretched across its place now, as everything in a percentage timeline is.
	for (FWidgetAnimLayer& Layer : Layers)
	{
		if (Layer.IsAnimationTrack() && !Layer.FitsAnimationToTrack())
		{
			Layer.Duration = Layer.GetRunLength();
			Layer.bFitToTrack = true;
		}
	}

	if (End > KINDA_SMALL_NUMBER)
	{
		for (FWidgetAnimLayer& Layer : Layers)
		{
			RetimeLayer(Layer, End);
		}
	}

	// Its pace is folded into where its tracks now are, and its end is 100%.
	InOutMinimumLength = 0.f;
	InOutSpeedMultiplier = 1.f;
}

void UWidgetAnimLibrary::AddMinimumLengthPlaceholder(const UWidgetAnimPresetAsset* Animation, TArray<FWidgetAnimSettings>& OutSettings)
{
	if (!Animation || Animation->GetEffectiveMinimumLength() <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	// Muted, so it poses nothing, and on no placement chain, so a stagger fanning out never moves it: the animation's
	// clock runs to its Minimum Length - however its tracks end - and on past it with them where they end later. Its
	// span is the shortest a layer's can be, ending right on the length, so it never moves where a loop starts again.
	FWidgetAnimSettings Settings;
	Settings.Duration = KINDA_SMALL_NUMBER;
	Settings.Delay = FMath::Max(Animation->GetEffectiveMinimumLength() - Settings.Duration, 0.f);
	Settings.bMuted = true;
	OutSettings.Add(MoveTemp(Settings));
}

bool UWidgetAnimLibrary::AnimationPlays(const UWidgetAnimPresetAsset* Animation, const UObject* Other)
{
	if (!Animation || !Other)
	{
		return false;
	}

	// Breadth first over every animation it inserts, each looked into once, so a loop already in the data ends.
	TArray<const UWidgetAnimPresetAsset*, TInlineAllocator<8>> Pending;
	TSet<const UWidgetAnimPresetAsset*> Seen;
	Pending.Add(Animation);
	while (Pending.Num() > 0)
	{
		const UWidgetAnimPresetAsset* Current = Pending.Pop();
		if (Current == Other)
		{
			return true;
		}
		if (Seen.Contains(Current))
		{
			continue;
		}
		Seen.Add(Current);

		for (const FWidgetAnimLayer& Layer : Current->Layers)
		{
			if (Layer.InsertedAnimation)
			{
				Pending.Add(Layer.InsertedAnimation.Get());
			}
		}
	}
	return false;
}

void UWidgetAnimLibrary::ForEachLayerIncludingInserted(const TArray<FWidgetAnimLayer>& Layers, const UWidgetAnimPresetAsset* In, TFunctionRef<void(const UWidgetAnimPresetAsset*, const FWidgetAnimLayer&)> Visit)
{
	TArray<const UWidgetAnimPresetAsset*, TInlineAllocator<8>> Inserted;
	for (const FWidgetAnimLayer& Layer : Layers)
	{
		Visit(In, Layer);
		const UWidgetAnimPresetAsset* Animation = Layer.InsertedAnimation.Get();
		if (Animation && Animation != In)
		{
			Inserted.AddUnique(Animation);
		}
	}

	// Each inserted animation once, in the order first inserted, then whatever it inserts - never one twice.
	TSet<const UWidgetAnimPresetAsset*> Seen;
	if (In)
	{
		Seen.Add(In);
	}
	for (int32 Next = 0; Next < Inserted.Num(); ++Next)
	{
		const UWidgetAnimPresetAsset* Animation = Inserted[Next];
		if (Seen.Contains(Animation))
		{
			continue;
		}
		Seen.Add(Animation);

		for (const FWidgetAnimLayer& Layer : Animation->Layers)
		{
			Visit(Animation, Layer);
			const UWidgetAnimPresetAsset* Deeper = Layer.InsertedAnimation.Get();
			if (Deeper && !Seen.Contains(Deeper))
			{
				Inserted.AddUnique(Deeper);
			}
		}
	}
}

// =====================================================================================================
// Staggers
// =====================================================================================================

bool UWidgetAnimLibrary::TypeMovesParts(EWidgetAnimType AnimType)
{
	switch (AnimType)
	{
	case EWidgetAnimType::Fade:
	case EWidgetAnimType::Scale:
	case EWidgetAnimType::Slide:
	case EWidgetAnimType::Rotate:
	case EWidgetAnimType::Pulse:
	case EWidgetAnimType::Transform:
	case EWidgetAnimType::Shake:
		return true;
	default:
		return false;
	}
}

bool UWidgetAnimLibrary::TypeTakesParts(EWidgetAnimType AnimType, const FInstancedStruct& TypeParams)
{
	return TypeMovesParts(AnimType) || (AnimType == EWidgetAnimType::Custom && WidgetAnimIsRegisteredCue(TypeParams));
}

bool UWidgetAnimLibrary::HasExpanders(const TArray<FWidgetAnimSettings>& Settings)
{
	for (const FWidgetAnimSettings& Layer : Settings)
	{
		if (Layer.Expander.IsValid() || Layer.TextTarget.IsSet() || NeedsPlayPreparation(Layer))
		{
			return true;
		}
	}
	return false;
}

bool UWidgetAnimLibrary::NeedsPlayPreparation(const FWidgetAnimSettings& Layer)
{
	if (Layer.bPlayPrepared || Layer.AnimType != EWidgetAnimType::Custom)
	{
		return false;
	}
	const FWidgetAnimEvaluator* Evaluator = Layer.FindEvaluator();
	return Evaluator && Evaluator->NeedsPlayPreparation(Layer.TypeParams);
}

namespace
{
	/** What preparing one layer for play came to. See UWidgetAnimLibrary::ExpandSettings. */
	struct FLayerPreparation
	{
		bool bPrepared = false;
		FInstancedStruct Params;

		/** Its new run length, or negative to keep its own. */
		float Run = -1.f;

		/** Whether that length comes from the type rather than Duration, at a pace the type wants even (Linear). */
		bool bEvenPace = false;

		bool bHoldStartPose = false;
		bool bReady = true;
	};

	FLayerPreparation PrepareLayerForPlay(UWidget* FallbackWidget, const FWidgetAnimSettings& Layer)
	{
		FLayerPreparation Preparation;
		if (!UWidgetAnimLibrary::NeedsPlayPreparation(Layer))
		{
			return Preparation;
		}

		const FWidgetAnimEvaluator* Evaluator = Layer.FindEvaluator();
		UObject* Target = Evaluator->ResolveTarget(Layer.TargetWidget ? Layer.TargetWidget.Get() : FallbackWidget, Layer.TypeParams);

		Preparation.bPrepared = true;
		Preparation.Params = Layer.TypeParams;
		Preparation.bReady = !Target || Evaluator->IsReadyToPrepare(Target, Layer.TypeParams);
		Preparation.bHoldStartPose = Evaluator->HoldsStartPose(Layer.TypeParams);

		// A value-driven layer is prepared too - what it works out may be how its value is shown - but has no length to
		// change: it follows its value.
		const float AuthoredRun = WidgetAnimLoop::GetEffectiveLayerDuration(Layer);
		const float Run = Evaluator->PrepareForPlay(Target, Preparation.Params, AuthoredRun);
		if (!Layer.IsValueDriven() && Run >= 0.f)
		{
			Preparation.Run = FMath::Max(Run, 0.01f);
			Preparation.bEvenPace = Evaluator->HasDerivedLength(Layer.TypeParams) && Evaluator->PrefersLinearEasing();
		}
		return Preparation;
	}

	/**
	 * A prepared layer's settings as it will play: its worked-out settings and its new length - a length that isn't its
	 * Duration can't be a spring's - at an even pace where its type wants one.
	 */
	void ApplyLayerPreparation(const FLayerPreparation& Preparation, FWidgetAnimSettings& Layer)
	{
		if (!Preparation.bPrepared)
		{
			return;
		}

		Layer.bPlayPrepared = true;
		Layer.TypeParams = Preparation.Params;
		Layer.bHoldStartPose = Layer.bHoldStartPose || Preparation.bHoldStartPose;
		if (Preparation.Run >= 0.f)
		{
			Layer.Duration = Preparation.Run;
			if (Layer.Timing == EWidgetAnimTiming::Spring)
			{
				Layer.Timing = EWidgetAnimTiming::Tween;
			}
			if (Preparation.bEvenPace)
			{
				Layer.Easing = EWidgetAnimEasing::Linear;
				Layer.CustomCurve = nullptr;
			}
		}
	}
}

namespace WidgetAnimTextTargets
{
	/**
	 * Where a layer's Part of the Text is in its text, as letter ranges - see FWidgetAnimTextTarget. False when the layer isn't
	 * limited to part of its text at all: none typed, a type that can't be (it plays on the whole text), or a widget that isn't
	 * an animated text - whose text has no letters of its own to move. True with no ranges: typed, and not there.
	 */
	bool Find(const FWidgetAnimSettings& Layer, UWidget* Target, TArray<FIntPoint>& OutRanges)
	{
		OutRanges.Reset();
		if (!Layer.TextTarget.IsSet())
		{
			return false;
		}

		// A type that moves letters finds them on the widget a text stagger would; one that changes them, on its own text.
		const bool bMovesParts = UWidgetAnimLibrary::TypeMovesParts(Layer.AnimType);
		const FWidgetAnimEvaluator* Evaluator = !bMovesParts && Layer.AnimType == EWidgetAnimType::Custom ? Layer.FindEvaluator() : nullptr;
		if (!bMovesParts && !(Evaluator && Evaluator->TakesTextRange(Layer.TypeParams)))
		{
			return false;
		}

		UObject* Text = bMovesParts ? static_cast<UObject*>(WidgetAnimExpand::FindPartHost(Target)) : WidgetAnimText::ResolveTarget(Target);
		if (!WidgetAnimText::AsHost(Text))
		{
			return false;
		}
		Layer.TextTarget.FindLetterRanges(WidgetAnimText::GetDisplayString(Text), OutRanges);
		return true;
	}

	/**
	 * A text stagger's items kept to a Part of the Text: the letters in it, or the words with a letter in it. Lines are left as
	 * they are - which letters a line holds is only known once the text is laid out.
	 */
	void Filter(UWidget* Target, const TArray<FIntPoint>& Ranges, TArray<FWidgetAnimExpandedItem>& Items)
	{
		if (Items.Num() == 0 || !Items[0].Part.IsSet())
		{
			return;
		}

		const FName Unit = Items[0].Part.Unit;
		if (Unit == WidgetAnimPartUnits::Letter)
		{
			Items.RemoveAll([&Ranges](const FWidgetAnimExpandedItem& Item) { return Ranges.Num() == 0 || !WidgetAnimTextParts::Covers(Ranges, Item.Part.Index); });
			return;
		}
		if (Unit == WidgetAnimPartUnits::Word)
		{
			TArray<WidgetAnimTextParts::FRange> Letters;
			TArray<WidgetAnimTextParts::FRange> Words;
			TArray<int32> WordOfLetter;
			WidgetAnimTextParts::GetLetters(WidgetAnimText::GetDisplayString(WidgetAnimExpand::FindPartHost(Target)), Letters);
			WidgetAnimTextParts::GetWords(Letters, Words, &WordOfLetter);
			TSet<int32> Kept;
			for (int32 LetterIndex = 0; LetterIndex < WordOfLetter.Num(); ++LetterIndex)
			{
				if (Ranges.Num() > 0 && WidgetAnimTextParts::Covers(Ranges, LetterIndex))
				{
					Kept.Add(WordOfLetter[LetterIndex]);
				}
			}
			Items.RemoveAll([&Kept](const FWidgetAnimExpandedItem& Item) { return !Kept.Contains(Item.Part.Index); });
		}
	}
}

float UWidgetAnimLibrary::GetPreparedRunLength(UWidget* FallbackWidget, const FWidgetAnimSettings& Layer)
{
	const FLayerPreparation Preparation = PrepareLayerForPlay(FallbackWidget, Layer);
	return Preparation.Run >= 0.f ? Preparation.Run : WidgetAnimLoop::GetEffectiveLayerDuration(Layer);
}

int32 UWidgetAnimLibrary::CountExpandedItems(UWidget* FallbackWidget, const FWidgetAnimSettings& Settings)
{
	const FWidgetAnimExpander* Expander = Settings.Expander.GetPtr<FWidgetAnimExpander>();
	if (!Expander)
	{
		return INDEX_NONE;
	}

	// A text stagger on a type that can't move a part plays on the whole widget: one item.
	if (Expander->GathersParts() && !TypeTakesParts(Settings.AnimType, Settings.TypeParams))
	{
		return 1;
	}

	UWidget* Target = Settings.TargetWidget ? Settings.TargetWidget.Get() : FallbackWidget;
	TArray<FWidgetAnimExpandedItem> Items;
	Expander->GatherItems(Target, Items);

	// A text stagger kept to a Part of the Text fans out over its letters or words only.
	TArray<FIntPoint> TextRanges;
	if (Expander->GathersParts() && WidgetAnimTextTargets::Find(Settings, Target, TextRanges))
	{
		WidgetAnimTextTargets::Filter(Target, TextRanges, Items);
	}
	return Items.Num();
}

bool UWidgetAnimLibrary::ExpandSettings(UWidget* FallbackWidget, const TArray<FWidgetAnimSettings>& Settings, TArray<FWidgetAnimSettings>& OutSettings, bool* bOutReady)
{
	if (bOutReady)
	{
		*bOutReady = true;
	}

	if (!HasExpanders(Settings))
	{
		return false;
	}

	const int32 NumLayers = Settings.Num();

	// Each registered type that works something out on its widget does it first, since a length it comes to places what
	// follows it just as a stagger does.
	TArray<FLayerPreparation> Preparations;
	Preparations.SetNum(NumLayers);
	for (int32 Index = 0; Index < NumLayers; ++Index)
	{
		Preparations[Index] = PrepareLayerForPlay(FallbackWidget, Settings[Index]);
		if (bOutReady && !Preparations[Index].bReady)
		{
			*bOutReady = false;
		}
	}

	// What each staggered layer fans out to on its widget right now, when each item starts, and how much further along
	// the timeline the layer now reaches than its first item alone did.
	struct FFan
	{
		const FWidgetAnimExpander* Expander = nullptr;
		TArray<FWidgetAnimExpandedItem> Items;
		TArray<float> Offsets;
		float Extra = 0.f;

		/** Fanned out to where its Part of the Text appears, each place as one piece - with no stagger, all at once. */
		bool bText = false;
	};
	TArray<FFan> Fans;
	Fans.SetNum(NumLayers);

	// A layer limited to part of its text by a type that changes letters rather than moving them: the letters it is limited
	// to, and whether it is limited at all (typed, and its type and widget can be) - with no letters, its text wasn't there.
	TArray<TArray<FIntPoint>> TextLetters;
	TArray<bool> bTextLimited;
	TextLetters.SetNum(NumLayers);
	bTextLimited.Init(false, NumLayers);

	for (int32 Index = 0; Index < NumLayers; ++Index)
	{
		const FWidgetAnimSettings& Layer = Settings[Index];
		const FWidgetAnimExpander* Expander = Layer.Expander.GetPtr<FWidgetAnimExpander>();
		UWidget* Target = Layer.TargetWidget ? Layer.TargetWidget.Get() : FallbackWidget;

		// Part of the Text: where it is in the layer's text right now - found every time it starts, like a stagger's items, so
		// it follows text set since.
		TArray<FIntPoint> TextRanges;
		const bool bTextTargeted = WidgetAnimTextTargets::Find(Layer, Target, TextRanges);
		const bool bMovesParts = TypeMovesParts(Layer.AnimType);
		if (bTextTargeted && !bMovesParts)
		{
			bTextLimited[Index] = true;
			TextLetters[Index] = TextRanges;
		}

		// Letters can only be moved by what moves a render transform - or fired at, by a cue; on anything else the layer
		// plays once, on the whole widget, as if it had no stagger.
		if (!Expander || (Expander->GathersParts() && !TypeTakesParts(Layer.AnimType, Layer.TypeParams)))
		{
			// Without a stagger, a Part of the Text is still parts: each place it appears, posed as one piece, together.
			if (bTextTargeted && bMovesParts)
			{
				FFan& TextFan = Fans[Index];
				TextFan.bText = true;
				UWidget* Host = WidgetAnimExpand::FindPartHost(Target);
				for (const FIntPoint& Range : TextRanges)
				{
					FWidgetAnimExpandedItem& Item = TextFan.Items.AddDefaulted_GetRef();
					Item.Widget = Host;
					Item.Part = FWidgetAnimPartKey(WidgetAnimPartUnits::Span, Range.X, Range.Y);
				}
			}
			continue;
		}

		if (bOutReady && !Expander->IsReadyToExpand(Target))
		{
			*bOutReady = false;
		}

		FFan& Fan = Fans[Index];
		Fan.Expander = Expander;
		Expander->GatherItems(Target, Fan.Items);

		// A text stagger kept to a Part of the Text: just its letters or words take their turns.
		if (bTextTargeted && bMovesParts && Expander->GathersParts())
		{
			WidgetAnimTextTargets::Filter(Target, TextRanges, Fan.Items);
		}

		// A value-driven layer shows its value from the moment it starts, with no delay (see TickAnimationsInto), so its
		// items follow the value together and nothing waits for them.
		if (!Layer.IsValueDriven())
		{
			Expander->ComputeOffsets(Fan.Items.Num(), Fan.Offsets);
			for (const float Offset : Fan.Offsets)
			{
				Fan.Extra = FMath::Max(Fan.Extra, Offset);
			}
		}
	}

	// Everything placed after a staggered layer in the same list moves along by however much longer the stagger made it:
	// the list placed again the way BuildLayerSettings placed it, once as it was and once with every stagger's extra, and
	// each layer moved by the difference. Moving rather than replacing keeps whatever was done to the starts since - an
	// entry's extra delay, say - and leaves every layer before the first stagger exactly where it was.
	TArray<float> Shift;
	Shift.Init(0.f, NumLayers);
	{
		struct FChain
		{
			int32 Id = INDEX_NONE;
			FLayerPlacer Before;
			FLayerPlacer After;
		};
		TArray<FChain, TInlineAllocator<4>> Chains;

		for (int32 Index = 0; Index < NumLayers; ++Index)
		{
			const FWidgetAnimSettings& Layer = Settings[Index];
			if (Layer.PlacementChain == INDEX_NONE)
			{
				continue;
			}

			FChain* Chain = Chains.FindByPredicate([&Layer](const FChain& Candidate) { return Candidate.Id == Layer.PlacementChain; });
			if (!Chain)
			{
				Chain = &Chains.AddDefaulted_GetRef();
				Chain->Id = Layer.PlacementChain;
			}

			// A prepared length replaces the placed one, shorter or longer; a stagger's spread adds to either.
			const float PreparedRun = Preparations[Index].Run >= 0.f ? Preparations[Index].Run : Layer.PlacedRun;
			const float StartBefore = Chain->Before.Place(Layer.bPlacedAfterPrevious, Layer.PlacedDelay, Layer.PlacedRun);
			const float StartAfter = Chain->After.Place(Layer.bPlacedAfterPrevious, Layer.PlacedDelay, PreparedRun + Fans[Index].Extra);
			Shift[Index] = StartAfter - StartBefore;
		}

		// A layer of an animation inserted as a track moves with its track too: a stagger fanning out before the track
		// moves the whole inserted animation along. Its track's placeholder was built before it, so the placeholder's
		// shift - its own track's included, however deep - is already final; it is taken into this layer's seconds.
		for (int32 Index = 0; Index < NumLayers; ++Index)
		{
			const FWidgetAnimSettings& Layer = Settings[Index];
			if (Layer.AnchoredTo == INDEX_NONE)
			{
				continue;
			}
			for (int32 Anchor = Index - 1; Anchor >= 0; --Anchor)
			{
				if (Settings[Anchor].PlacementAnchor == Layer.AnchoredTo)
				{
					Shift[Index] += Shift[Anchor] * Layer.NestedSpeed / FMath::Max(Settings[Anchor].NestedSpeed, 0.01f);
					break;
				}
			}
		}
	}

	OutSettings.Reset();
	OutSettings.Reserve(NumLayers);

	for (int32 Index = 0; Index < NumLayers; ++Index)
	{
		FWidgetAnimSettings Layer = Settings[Index];
		Layer.Delay = FMath::Max(Layer.Delay + Shift[Index], 0.f);
		ApplyLayerPreparation(Preparations[Index], Layer);

		// Fanned out, or not staggerable here: either way nothing is left to fan out, so doing it again does nothing. Its Part
		// of the Text is found, too.
		Layer.Expander.Reset();
		Layer.TextTarget = FWidgetAnimTextTarget();

		// A text track limited to part of its text: the letters, for it to read as it plays - or, not there, nothing to play on.
		if (bTextLimited[Index])
		{
			if (TextLetters[Index].Num() == 0)
			{
				Layer.bMuted = true;
			}
			else if (FWidgetAnimTypeParams* TypeParams = Layer.TypeParams.GetMutablePtr<FWidgetAnimTypeParams>())
			{
				TypeParams->TextLetters = TextLetters[Index];
			}
		}

		const FFan& Fan = Fans[Index];
		if (!Fan.Expander && !Fan.bText)
		{
			OutSettings.Add(MoveTemp(Layer));
			continue;
		}

		// Nothing to fan out to - an empty list, a text with no letters, a text stagger with no Animated Text Block to
		// act on. The layer keeps its place in time, so nothing after it moves, but poses nothing.
		if (Fan.Items.Num() == 0)
		{
			Layer.bMuted = true;
			OutSettings.Add(MoveTemp(Layer));
			continue;
		}

		for (int32 ItemIndex = 0; ItemIndex < Fan.Items.Num(); ++ItemIndex)
		{
			const FWidgetAnimExpandedItem& Item = Fan.Items[ItemIndex];
			FWidgetAnimSettings& Copy = OutSettings.Add_GetRef(Layer);
			Copy.TargetWidget = Item.Widget;
			Copy.Part = Item.Part;
			Copy.bHoldStartPose = Copy.bHoldStartPose || (Fan.Expander && Fan.Expander->bHoldStartPose);
			if (Fan.Offsets.IsValidIndex(ItemIndex))
			{
				Copy.Delay += Fan.Offsets[ItemIndex];
			}

			// Its own Image and Shape Box, as an entry derives them from its group's target: each child's material and
			// outline, not the list's.
			if (!Item.Part.IsSet())
			{
				Copy.MaterialTargetWidget = Item.Widget;
				Copy.MaterialTargetImage = nullptr;
				Copy.ShapeTargetBox = ResolveShapeBox(Item.Widget);

				// And prepared on its own widget - each child's own text counted, not the list's first. The layers after
				// were placed by the first child's length; each child runs its own.
				if (Preparations[Index].bPrepared)
				{
					FWidgetAnimSettings Unprepared = Settings[Index];
					Unprepared.TargetWidget = Item.Widget;
					Copy.bPlayPrepared = false;
					ApplyLayerPreparation(PrepareLayerForPlay(FallbackWidget, Unprepared), Copy);
				}
			}
		}
	}

	return true;
}

bool UWidgetAnimLibrary::ExpandTrack(UWidget* FallbackWidget, const FWidgetAnimPlayer& Player, FWidgetAnimTrack& Track)
{
	bool bReady = true;
	TArray<FWidgetAnimSettings> Expanded;
	if (!ExpandSettings(FallbackWidget, Track.Settings, Expanded, &bReady))
	{
		Track.bNeedsExpansion = false;
		return true;
	}

	// Waits for what it counts - a wrapping text's lines before it has a width - but only a few frames: a widget that is
	// never drawn, collapsed say, would otherwise hold its track, and an exit waiting on it, for good. In that time the
	// widget has not been on screen, so nothing visibly waits.
	constexpr uint64 MaxFramesToWait = 3;
	if (Track.ExpansionFirstFrame == 0)
	{
		Track.ExpansionFirstFrame = GFrameCounter;
	}
	if (!bReady && GFrameCounter - Track.ExpansionFirstFrame < MaxFramesToWait)
	{
		return false;
	}

	// A progress set before the track fanned out - set right after it was started - is kept: progress isn't remembered by
	// name, as values are, so it would otherwise be lost.
	TArray<TPair<const FWidgetAnimProgressDrive*, FWidgetAnimState>, TInlineAllocator<2>> DriveStates;
	for (int32 Index = 0; Index < Track.Settings.Num() && Index < Track.States.Num(); ++Index)
	{
		const FWidgetAnimProgressDrive* Drive = Track.Settings[Index].ProgressDrive.Get();
		if (Drive && Track.States[Index].bDriveTaken && WidgetAnimDrives::FindLeader(Track.Settings, Drive) == Index)
		{
			DriveStates.Emplace(Drive, Track.States[Index]);
		}
	}

	Track.Settings = MoveTemp(Expanded);
	Track.bNeedsExpansion = false;
	Track.ExpansionFirstFrame = 0;
	ResetWidgetAnimations(Track.Settings, Track.States);
	InitializeValueLayers(Player, Track);

	for (const TPair<const FWidgetAnimProgressDrive*, FWidgetAnimState>& Kept : DriveStates)
	{
		const int32 Leader = WidgetAnimDrives::FindLeader(Track.Settings, Kept.Key);
		if (Track.States.IsValidIndex(Leader))
		{
			Track.States[Leader].DriveState = Kept.Value.DriveState;
			Track.States[Leader].bDriveTaken = true;
		}
	}
	return true;
}

#if WITH_EDITOR
void UWidgetAnimLibrary::MuteHiddenLayers(const TArray<FWidgetAnimLayer>& Layers, TArray<FWidgetAnimSettings>& Settings, int32 FirstSetting)
{
	for (int32 LayerIndex = 0; LayerIndex < Layers.Num(); ++LayerIndex)
	{
		const int32 SettingIndex = FirstSetting + LayerIndex;
		if (Settings.IsValidIndex(SettingIndex) && (Layers[LayerIndex].bHiddenInEditor || Layers[LayerIndex].bMutedByEditorScope))
		{
			Settings[SettingIndex].bMuted = true;
		}
	}

	// Then the layers of the animations inserted as tracks, right after them: hidden with their track.
	for (int32 SettingIndex = FirstSetting + Layers.Num(); Settings.IsValidIndex(SettingIndex); ++SettingIndex)
	{
		FWidgetAnimSettings& Inserted = Settings[SettingIndex];
		if (!Layers.IsValidIndex(Inserted.InsertedFromLayer))
		{
			break;
		}
		if (Layers[Inserted.InsertedFromLayer].bHiddenInEditor || Layers[Inserted.InsertedFromLayer].bMutedByEditorScope)
		{
			Inserted.bMuted = true;
		}
	}
}
#endif

float UWidgetAnimLibrary::GetStackPassDuration(const TArray<FWidgetAnimSettings>& Settings)
{
	float End = 0.f;
	for (const FWidgetAnimSettings& Layer : Settings)
	{
		const WidgetAnimLoop::FLayerSpan Span = WidgetAnimLoop::GetLayerSpan(Layer);
		End = FMath::Max(End, Span.Start + Span.Length);
	}

	return FMath::Max(End, KINDA_SMALL_NUMBER);
}

float UWidgetAnimLibrary::GetExpandedPassDuration(UWidget* FallbackWidget, const TArray<FWidgetAnimSettings>& Settings)
{
	TArray<FWidgetAnimSettings> Expanded;
	return GetStackPassDuration(ExpandSettings(FallbackWidget, Settings, Expanded) ? Expanded : Settings);
}

float UWidgetAnimLibrary::GetLayerRunSeconds(const FWidgetAnimSettings& Layer)
{
	return WidgetAnimLoop::GetEffectiveLayerDuration(Layer);
}

float UWidgetAnimLibrary::GetKeyStackTime(const FWidgetAnimSettings& Layer, int32 KeyIndex)
{
	float KeyTime = -1.f;

	if (WidgetAnimTypeIsKeyframed(Layer.AnimType))
	{
		const FRichCurve* Curve = Layer.Transform.GetActiveCurve();
		if (!Curve || !Curve->GetConstRefOfKeys().IsValidIndex(KeyIndex))
		{
			return -1.f;
		}
		KeyTime = FMath::Max(Curve->GetConstRefOfKeys()[KeyIndex].Time, 0.f);
	}
	else if (WidgetAnimTypeSupportsValueKeys(Layer.AnimType))
	{
		// Start, keys, end - each at its fraction of the layer's run.
		if (KeyIndex < 0 || KeyIndex >= Layer.ValueKeys.NumPoints())
		{
			return -1.f;
		}
		KeyTime = Layer.ValueKeys.GetPointTime(KeyIndex) * WidgetAnimLoop::GetEffectiveLayerDuration(Layer);
	}
	else
	{
		return -1.f;
	}

	// Same placement GetLayerSpan gives the layer - delay and key time both scaled by speed - so the result
	// is a point on the very clock ApplyWidgetAnimationsAtAlpha and the playing modes run on.
	const float Speed = Layer.GetPlaySpeed();
	return (FMath::Max(Layer.Delay, 0.f) + KeyTime) / Speed;
}

void UWidgetAnimLibrary::RestartWidgetAnimPreview(FWidgetAnimPreviewState& State)
{
	State.bStarted = false;
	State.HoldRemaining = 0.f;
	State.Playback = FWidgetAnimPlaybackState();

	// Or a restart mid-scrub would leave the widget posed while the state still claimed that pose was
	// current, and the next frame at the same alpha would be skipped as "unchanged".
	State.LastScrubAlpha = -1.f;
	State.bLastScrubWasOut = false;
}

bool UWidgetAnimLibrary::TickWidgetAnimPreview(UWidget* FallbackWidget, const TArray<FWidgetAnimSettings>& InAnimationsToExpand, const TArray<FWidgetAnimSettings>& OutAnimationsToExpand, FWidgetAnimPreviewState& State, const FWidgetAnimPreviewSettings& Settings, const FWidgetAnimBaselineSet& Baselines, float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(WidgetAnimTickDesignerPreview);
	// Fanned out once for the whole frame, so every pose and every state below lines up with the same layers.
	TArray<FWidgetAnimSettings> ExpandedIn;
	TArray<FWidgetAnimSettings> ExpandedOut;
	const TArray<FWidgetAnimSettings>& InAnimations = ExpandSettings(FallbackWidget, InAnimationsToExpand, ExpandedIn) ? ExpandedIn : InAnimationsToExpand;
	const TArray<FWidgetAnimSettings>& OutAnimations = ExpandSettings(FallbackWidget, OutAnimationsToExpand, ExpandedOut) ? ExpandedOut : OutAnimationsToExpand;

	const FWidgetAnimBaselineSet* BaselinePtr = Baselines.bCaptured ? &Baselines : nullptr;

	// Changing mode starts the harness over rather than continuing from wherever the previous one stopped,
	// so flipping Loop In and Out -> Play In Once doesn't leave half an exit animation on screen.
	if (State.LastMode != Settings.Mode)
	{
		State.LastMode = Settings.Mode;
		RestartWidgetAnimPreview(State);

		if (Settings.Mode == EWidgetAnimPreviewMode::Off)
		{
			RestoreWidgetAnimBaselines(Baselines);
			return false;
		}
	}

	switch (Settings.Mode)
	{
	case EWidgetAnimPreviewMode::Off:
		return false;

	case EWidgetAnimPreviewMode::StartPose:
		ApplyWidgetAnimationsAtAlpha(FallbackWidget, InAnimations, 0.f, /*bIsOutAnimation=*/false, Baselines);
		return false;

	case EWidgetAnimPreviewMode::EndPose:
		ApplyWidgetAnimationsAtAlpha(FallbackWidget, InAnimations, 1.f, /*bIsOutAnimation=*/false, Baselines);
		return false;

	case EWidgetAnimPreviewMode::Scrub:
	{
		const float ClampedAlpha = FMath::Clamp(Settings.ScrubAlpha, 0.f, 1.f);

		if (State.bLastScrubWasOut != Settings.bScrubOutAnimation
			|| !FMath::IsNearlyEqual(State.LastScrubAlpha, ClampedAlpha, KINDA_SMALL_NUMBER))
		{
			ApplyWidgetAnimationsAtAlpha(
				FallbackWidget,
				Settings.bScrubOutAnimation ? OutAnimations : InAnimations,
				ClampedAlpha,
				Settings.bScrubOutAnimation,
				Baselines);

			State.LastScrubAlpha = ClampedAlpha;
			State.bLastScrubWasOut = Settings.bScrubOutAnimation;
		}

		// True, where the other two held poses return false. They are finished the moment they have
		// drawn; a scrub is not finished until the mode changes. Returning false here stopped the active
		// timer after a single frame, and the active timer is the thing that forces the designer viewport
		// to repaint - which is why dragging the slider only ever showed the value the mouse was released
		// on. Staying registered is what makes it continuous.
		return true;
	}

	default:
		break;
	}

	const bool bLooping = (Settings.Mode == EWidgetAnimPreviewMode::LoopIn || Settings.Mode == EWidgetAnimPreviewMode::LoopInOut);

	if (!State.bStarted)
	{
		State.bStarted = true;
		State.HoldRemaining = 0.f;

		if (Settings.Mode == EWidgetAnimPreviewMode::PlayOut)
		{
			// Sit on the settled pose first, or the exit appears to start from nowhere.
			ApplyWidgetAnimationsAtAlpha(FallbackWidget, InAnimations, 1.f, /*bIsOutAnimation=*/false, Baselines);
			ResetWidgetAnimations(OutAnimations, State.Playback.OutStates);
			State.Playback.Phase = EWidgetAnimPhase::PlayingOut;
		}
		else
		{
			InitializeInternal(FallbackWidget, InAnimations, State.Playback.InStates, BaselinePtr);
			State.Playback.OutStates.Reset();
			State.Playback.Phase = EWidgetAnimPhase::PlayingIn;
		}
	}

	const float ScaledDelta = DeltaTime * FMath::Max(Settings.PlaybackSpeed, 0.01f);

	// Holds run on real time, not scaled time: they're dead air between animations, not part of them.
	if (State.HoldRemaining > 0.f)
	{
		State.HoldRemaining -= DeltaTime;
		if (State.HoldRemaining > 0.f)
		{
			// Only Shown / Hidden are ever held, and ticking those just keeps any looping layers moving
			// through the hold instead of freezing them for its length.
			TickPlaybackInternal(FallbackWidget, InAnimations, OutAnimations, State.Playback, ScaledDelta, BaselinePtr);
			return true;
		}
		State.HoldRemaining = 0.f;
	}

	// Reached with no hold left to run, either because it just expired or because it was zero to begin
	// with - which is why the hold is armed after the tick below rather than at the transition itself.
	if (State.Playback.Phase == EWidgetAnimPhase::Shown && bLooping)
	{
		if (Settings.Mode == EWidgetAnimPreviewMode::LoopInOut)
		{
			ResetWidgetAnimations(OutAnimations, State.Playback.OutStates);
			State.Playback.Phase = EWidgetAnimPhase::PlayingOut;
		}
		else
		{
			InitializeInternal(FallbackWidget, InAnimations, State.Playback.InStates, BaselinePtr);
			State.Playback.Phase = EWidgetAnimPhase::PlayingIn;
		}
	}
	else if (State.Playback.Phase == EWidgetAnimPhase::Hidden && Settings.Mode == EWidgetAnimPreviewMode::LoopInOut)
	{
		InitializeInternal(FallbackWidget, InAnimations, State.Playback.InStates, BaselinePtr);
		State.Playback.Phase = EWidgetAnimPhase::PlayingIn;
	}

	const EWidgetAnimPhase PhaseBefore = State.Playback.Phase;
	TickPlaybackInternal(FallbackWidget, InAnimations, OutAnimations, State.Playback, ScaledDelta, BaselinePtr);

	if (State.Playback.Phase != PhaseBefore && bLooping)
	{
		if (State.Playback.Phase == EWidgetAnimPhase::Shown)
		{
			State.HoldRemaining = FMath::Max(Settings.HoldShown, 0.f);
		}
		else if (State.Playback.Phase == EWidgetAnimPhase::Hidden)
		{
			State.HoldRemaining = FMath::Max(Settings.HoldHidden, 0.f);
		}
	}

	const bool bIsPlaying = State.Playback.Phase == EWidgetAnimPhase::PlayingIn || State.Playback.Phase == EWidgetAnimPhase::PlayingOut;

	// Play In Once / Play Out Once: the stack has landed, but a looping layer in it is still moving and
	// needs frames. Returning false here stopped the active timer, so the loop never visibly ran.
	const bool bShowingOut = State.Playback.Phase == EWidgetAnimPhase::PlayingOut || State.Playback.Phase == EWidgetAnimPhase::Hidden;
	const bool bLayersStillRunning = bShowingOut
		? HasRunningLayers(OutAnimations, State.Playback.OutStates)
		: HasRunningLayers(InAnimations, State.Playback.InStates);

	return bIsPlaying || State.HoldRemaining > 0.f || bLooping || bLayersStillRunning;
}

// =====================================================================================================
// Authoring helpers
// =====================================================================================================

FWidgetAnimSettings UWidgetAnimLibrary::RetimeWidgetAnimation(const FWidgetAnimSettings& Settings, float Speed, float ExtraDelay)
{
	FWidgetAnimSettings Result = Settings;

	const float SafeSpeed = FMath::Max(Speed, 0.01f);
	Result.Duration = FMath::Max(Settings.Duration / SafeSpeed, 0.01f);
	Result.Delay = FMath::Max(Settings.Delay / SafeSpeed + ExtraDelay, 0.f);

	// Its placement retimed with it, so a stagger fanning out later moves what follows it by retimed seconds too.
	Result.PlacedDelay = Settings.PlacedDelay / SafeSpeed;
	Result.PlacedRun = Settings.PlacedRun / SafeSpeed;

	// A spring's length is its own, so Duration alone would retime nothing. The same spring played Speed times
	// faster is the same shape in less time: its frequency scaled by Speed with its damping ratio unchanged -
	// which is Visual Duration divided by it, or Stiffness by its square and Damping by it - and its initial
	// velocity, in travels per second, scaled with it.
	if (Settings.IsSpringTimed())
	{
		Result.Spring.VisualDuration = FMath::Max(Settings.Spring.VisualDuration / SafeSpeed, 0.02f);
		Result.Spring.Stiffness = Settings.Spring.Stiffness * SafeSpeed * SafeSpeed;
		Result.Spring.Damping = Settings.Spring.Damping * SafeSpeed;
		Result.Spring.InitialVelocity = Settings.Spring.InitialVelocity * SafeSpeed;
	}

	return Result;
}

void UWidgetAnimLibrary::AppendWithMirroring(const TArray<FWidgetAnimSettings>& Source, TArray<FWidgetAnimSettings>& Destination, TArray<FWidgetAnimSettings>& MirrorDestination)
{
	Destination.Reserve(Destination.Num() + Source.Num());

	for (const FWidgetAnimSettings& Settings : Source)
	{
		Destination.Add(Settings);

		// Direction is decided at play time by bIsOutAnimation, so the mirrored copy is a straight copy:
		// the same entry simply runs backwards when it plays as the other half of the pair.
		if (Settings.bMirrorInOut)
		{
			MirrorDestination.Add(Settings);
		}
	}
}

float UWidgetAnimLibrary::GetStackDuration(const TArray<FWidgetAnimSettings>& Settings)
{
	if (Settings.Num() == 0)
	{
		return 0.f;
	}

	TArray<int32, TInlineAllocator<16>> LeaderOf;
	TArray<WidgetAnimLoop::FLayerSpan, TInlineAllocator<16>> Spans;
	TArray<float, TInlineAllocator<16>> GroupLead;
	TArray<float, TInlineAllocator<16>> GroupEnd;
	WidgetAnimLoop::ComputeGroupSpans(Settings, LeaderOf, Spans, GroupLead, GroupEnd);

	float Duration = 0.f;
	WidgetAnimLoop::FindDrivingLeader(Settings, LeaderOf, GroupLead, GroupEnd, Duration);
	return Duration;
}

float UWidgetAnimLibrary::GetStackElapsed(const TArray<FWidgetAnimSettings>& Settings, const TArray<FWidgetAnimState>& States)
{
	const int32 NumLayers = Settings.Num();
	if (NumLayers == 0 || States.Num() != NumLayers)
	{
		// Not yet initialized for this stack - nothing has played, so nothing has elapsed.
		return 0.f;
	}

	TArray<int32, TInlineAllocator<16>> LeaderOf;
	TArray<WidgetAnimLoop::FLayerSpan, TInlineAllocator<16>> Spans;
	TArray<float, TInlineAllocator<16>> GroupLead;
	TArray<float, TInlineAllocator<16>> GroupEnd;
	WidgetAnimLoop::ComputeGroupSpans(Settings, LeaderOf, Spans, GroupLead, GroupEnd);

	// Only a leader's clock sits on the group's absolute timeline; a member's CycleTime is local to its
	// own span instead (see TickAnimationsInto), so only leaders are read here.
	float Elapsed = 0.f;
	for (int32 Index = 0; Index < NumLayers; ++Index)
	{
		if (LeaderOf[Index] != Index)
		{
			continue;
		}

		const float GroupElapsed = WidgetAnimLoop::GetGroupElapsed(Settings[Index], States[Index], GroupLead[Index], GroupEnd[Index]);
		Elapsed = FMath::Max(Elapsed, GroupElapsed);
	}

	return Elapsed;
}

bool UWidgetAnimLibrary::GetStackDrivingLoopInfo(const TArray<FWidgetAnimSettings>& Settings, bool& OutPingPong, float& OutLoopSeconds)
{
	OutPingPong = false;
	OutLoopSeconds = 0.f;

	if (Settings.Num() == 0)
	{
		return false;
	}

	TArray<int32, TInlineAllocator<16>> LeaderOf;
	TArray<WidgetAnimLoop::FLayerSpan, TInlineAllocator<16>> Spans;
	TArray<float, TInlineAllocator<16>> GroupLead;
	TArray<float, TInlineAllocator<16>> GroupEnd;
	WidgetAnimLoop::ComputeGroupSpans(Settings, LeaderOf, Spans, GroupLead, GroupEnd);

	float Duration = 0.f;
	const int32 Driving = WidgetAnimLoop::FindDrivingLeader(Settings, LeaderOf, GroupLead, GroupEnd, Duration);

	if (Driving == INDEX_NONE || !Settings[Driving].bLoop)
	{
		return false;
	}

	const float ReturnLegLength = GroupEnd[Driving] - GroupLead[Driving];
	OutPingPong = Settings[Driving].bPingPong;

	// A round trip is there-and-back, so what a Ping Pong loop visibly repeats is double the one-way span.
	OutLoopSeconds = OutPingPong ? ReturnLegLength * 2.f : ReturnLegLength;
	return true;
}

// =====================================================================================================
// Layers
// =====================================================================================================

FWidgetAnimSettings FWidgetAnimLayer::ToSettings() const
{
	FWidgetAnimSettings Out;

	Out.AnimType = AnimType;
	Out.Timing = Timing;
	Out.Duration = Duration;
	Out.Delay = Delay;
	Out.Easing = Easing;
	Out.CustomCurve = CustomCurve;
	Out.Spring = Spring;
	Out.ValueName = ValueName;
	Out.ValueSmoothing = ValueSmoothing;
	Out.SteadySpeed = SteadySpeed;
	Out.SteadyCatchUpTime = SteadyCatchUpTime;
	Out.ValueDrives = ValueDrives;
	Out.ValueOptions = ValueOptions;
	// Only a value type this track can take reaches the runtime; one it can't is read as Float.
	Out.ValueOptions.Kind = GetEffectiveValueKind();
	Out.ValueInput = ValueInput;
	Out.ValueKeys = ValueKeys;
	Out.Expander = Expander;
	Out.TextTarget = TextTarget;

	Out.StartOpacity = StartOpacity;
	Out.EndOpacity = EndOpacity;

	Out.StartScale = StartScale;
	Out.EndScale = EndScale;
	Out.Overshoot = Overshoot;

	Out.SlideFromDirection = SlideFromDirection;
	Out.SlideDistance = SlideDistance;
	Out.SlideOvershoot = SlideOvershoot;

	Out.StartAngle = StartAngle;
	Out.EndAngle = EndAngle;

	Out.PulseScaleAmount = PulseScaleAmount;
	Out.Shake = Shake;

	// A track inverted on its own: the one per-use flag a layer carries itself. Every use ORs its own Invert onto it.
	Out.bInvertAnimation = bInvertTrack;

	Out.MaterialSlot = MaterialSlot;
	Out.ScalarParameterName = ScalarParameterName;
	Out.StartScalarValue = StartScalarValue;
	Out.EndScalarValue = EndScalarValue;

	Out.VectorParameterName = VectorParameterName;
	Out.StartVectorValue = StartVectorValue;
	Out.EndVectorValue = EndVectorValue;

	// One line rather than a dozen, which is the whole point of the nested payload: the next family costs
	// one line here too instead of a copy of every one of its fields. What its keys hold is the track's type's.
	Out.Transform = Transform;
	Out.Transform.Content = WidgetAnimKeyframeContentFor(AnimType);

	// And a registered type's settings are one value whatever the type, which is what makes the next family cost
	// nothing here at all. A Shape track carries its keys' settings there, as its writes play them - see
	// WidgetAnimShapeTrack.h.
	Out.TypeParams = AnimType == EWidgetAnimType::Shape ? WidgetAnimShapeTrack::MakeParams(Transform)
		: AnimType == EWidgetAnimType::Design ? WidgetAnimDesignTrack::MakeParams(Transform)
		: TypeParams;

	// bLoop / bPingPong / LoopCount / bInvertAnimation / bMirrorInOut / the two targets are all decisions
	// about ONE USE of this layer, so they are deliberately left at their defaults here and filled in by
	// the entry that references the animation.
	return Out;
}

void FWidgetAnimLayer::PostSerialize(const FArchive& Ar)
{
	// Immediate is no longer offered: on a value-driven track it is Tween with Constant easing, which jumps the same way
	// (FWidgetAnimValueMotion::JumpsToValues). A track that isn't value-driven keeps it - it means nothing there, and its
	// Easing is its own tween's.
	if (Ar.IsLoading() && Timing == EWidgetAnimTiming::Value && ValueSmoothing == EWidgetAnimValueSmoothing::Immediate)
	{
		ValueSmoothing = EWidgetAnimValueSmoothing::Tween;
		Easing = EWidgetAnimEasing::Hold;
	}
}

FWidgetAnimLayer FWidgetAnimLayer::FromSettings(const FWidgetAnimSettings& Settings)
{
	FWidgetAnimLayer Out;

	Out.AnimType = Settings.AnimType;
	Out.Timing = Settings.Timing;
	Out.Duration = Settings.Duration;
	Out.Delay = Settings.Delay;
	Out.Easing = Settings.Easing;
	Out.CustomCurve = Settings.CustomCurve;
	Out.Spring = Settings.Spring;
	Out.ValueName = Settings.ValueName;
	Out.ValueSmoothing = Settings.ValueSmoothing;
	Out.SteadySpeed = Settings.SteadySpeed;
	Out.SteadyCatchUpTime = Settings.SteadyCatchUpTime;
	Out.ValueDrives = Settings.ValueDrives;
	Out.ValueOptions = Settings.ValueOptions;
	Out.ValueInput = Settings.ValueInput;
	Out.ValueKeys = Settings.ValueKeys;
	Out.Expander = Settings.Expander;
	Out.TextTarget = Settings.TextTarget;

	Out.StartOpacity = Settings.StartOpacity;
	Out.EndOpacity = Settings.EndOpacity;

	Out.StartScale = Settings.StartScale;
	Out.EndScale = Settings.EndScale;
	Out.Overshoot = Settings.Overshoot;

	Out.SlideFromDirection = Settings.SlideFromDirection;
	Out.SlideDistance = Settings.SlideDistance;
	Out.SlideOvershoot = Settings.SlideOvershoot;

	Out.StartAngle = Settings.StartAngle;
	Out.EndAngle = Settings.EndAngle;

	Out.PulseScaleAmount = Settings.PulseScaleAmount;
	Out.Shake = Settings.Shake;

	Out.MaterialSlot = Settings.MaterialSlot;
	Out.ScalarParameterName = Settings.ScalarParameterName;
	Out.StartScalarValue = Settings.StartScalarValue;
	Out.EndScalarValue = Settings.EndScalarValue;

	Out.VectorParameterName = Settings.VectorParameterName;
	Out.StartVectorValue = Settings.StartVectorValue;
	Out.EndVectorValue = Settings.EndVectorValue;

	Out.Transform = Settings.Transform;
	Out.TypeParams = Settings.AnimType == EWidgetAnimType::Shape || Settings.AnimType == EWidgetAnimType::Design ? FInstancedStruct() : Settings.TypeParams;

	return Out;
}

const FWidgetAnimEvaluator* FWidgetAnimLayer::FindEvaluator() const
{
	return AnimType == EWidgetAnimType::Custom ? FWidgetAnimEvaluatorRegistry::Get().Find(TypeParams) : nullptr;
}

const FWidgetAnimEvaluator* FWidgetAnimSettings::FindEvaluator() const
{
	return AnimType == EWidgetAnimType::Custom ? FWidgetAnimEvaluatorRegistry::Get().Find(TypeParams) : nullptr;
}

// =====================================================================================================
// Track player
// =====================================================================================================

void UWidgetAnimLibrary::StartWidgetAnimTrack(FWidgetAnimPlayer& Player, FName AnimId, int32 SourceIndex, const TArray<FWidgetAnimSettings>& Settings, bool bIsOut, float Speed, bool bRestartIfPlaying, bool bHoldWhenFinished)
{
	if (Settings.Num() == 0)
	{
		return;
	}

	// Matched on all three: an entry with no Anim ID is still a distinct animation, and the same entry
	// played forwards and backwards are two different things that may legitimately overlap.
	FWidgetAnimTrack* Existing = Player.Tracks.FindByPredicate(
		[AnimId, SourceIndex, bIsOut](const FWidgetAnimTrack& Track)
		{
			return Track.AnimId == AnimId && Track.SourceIndex == SourceIndex && Track.bIsOut == bIsOut;
		});

	if (Existing && !bRestartIfPlaying && !Existing->bFinished)
	{
		return;
	}

	Player.MarkChanged();
	FWidgetAnimTrack& Track = Existing ? *Existing : Player.Tracks.AddDefaulted_GetRef();

	Track.AnimId = AnimId;
	Track.SourceIndex = SourceIndex;
	Track.bIsOut = bIsOut;
	Track.bFinished = false;
	Track.bSettled = false;
	Track.bHoldWhenFinished = bHoldWhenFinished;
	Track.Speed = FMath::Max(Speed, 0.01f);
	Track.Settings = Settings;

	// Staggers fan out on the track's first tick, where the widget it plays on is known - so they see the list's
	// children and the label's letters as they are when it starts. See ExpandTrack.
	Track.bNeedsExpansion = HasExpanders(Track.Settings);
	Track.ExpansionFirstFrame = 0;

	ResetWidgetAnimations(Track.Settings, Track.States);
	InitializeValueLayers(Player, Track);

	// A value shown is shown until the next one: a track with value-driven layers always holds, or letting go would
	// snap its widgets back the moment its clock layers and values came to rest. A progress the same.
	if (Track.Settings.ContainsByPredicate([](const FWidgetAnimSettings& Layer) { return Layer.IsValueDriven() || Layer.IsProgressDriven(); }))
	{
		Track.bHoldWhenFinished = true;
	}
}

void UWidgetAnimLibrary::InitializeValueLayers(const FWidgetAnimPlayer& Player, FWidgetAnimTrack& Track)
{
	for (int32 Index = 0; Index < Track.Settings.Num() && Index < Track.States.Num(); ++Index)
	{
		const FWidgetAnimSettings& Layer = Track.Settings[Index];
		if (!Layer.IsValueDriven())
		{
			continue;
		}

		// Where it was last left - the widget shown again, Play In restarting everything - or, before any value, its
		// Start Value. Each layer maps the raw value onto itself through its own range.
		const FWidgetAnimRememberedValue* Remembered = FindRememberedValue(Player, Layer.ValueScope, Layer.ValueName);
		WidgetAnimValue::Snap(Track.States[Index].ValueState, Layer.ValueOptions.ToProgress(Remembered ? Remembered->Value : Layer.ValueOptions.GetStartValue()));
	}

	// Progress clocks the same, on the first layer each moves - one that plays on its clock first (a Duration) starts there
	// instead, and is handed its progress only once one is set. An animation's own progress always starts at 0%.
	for (int32 Index = 0; Index < Track.Settings.Num() && Index < Track.States.Num(); ++Index)
	{
		const FWidgetAnimSettings& Layer = Track.Settings[Index];
		const FWidgetAnimProgressDrive* Drive = Layer.ProgressDrive.Get();
		if (!Drive || Drive->Duration > 0.f || WidgetAnimDrives::FindLeader(Track.Settings, Drive) != Index)
		{
			continue;
		}

		const FWidgetAnimRememberedValue* Remembered = Drive->ValueName == WidgetAnimValue::ProgressValueName ? nullptr : FindRememberedValue(Player, Layer.ValueScope, Drive->ValueName);
		FWidgetAnimState& State = Track.States[Index];
		WidgetAnimValue::Snap(State.DriveState, Drive->Options.ToProgress(Remembered ? Remembered->Value : Drive->Options.GetStartValue()));
		State.bDriveTaken = true;
	}
}

const FWidgetAnimRememberedValue* UWidgetAnimLibrary::FindRememberedValue(const FWidgetAnimPlayer& Player, const UObject* Scope, FName Name)
{
	if (Name.IsNone())
	{
		return nullptr;
	}

	// Set in its own scope first; failing that, set for the name anywhere.
	const FWidgetAnimRememberedValue* Anywhere = nullptr;
	for (const FWidgetAnimRememberedValue& Remembered : Player.Values)
	{
		if (Remembered.ValueName != Name)
		{
			continue;
		}
		if (!Remembered.bAnywhere && Remembered.Animation.Get() == Scope)
		{
			return &Remembered;
		}
		if (Remembered.bAnywhere)
		{
			Anywhere = &Remembered;
		}
	}
	return Anywhere;
}

int32 UWidgetAnimLibrary::SetWidgetAnimTrackValue(FWidgetAnimPlayer& Player, const FWidgetAnimValueId& Id, const FVector4& Value, bool bSnap)
{
	if (Id.ValueName.IsNone())
	{
		return 0;
	}

	int32 Taken = 0;
	for (FWidgetAnimTrack& Track : Player.Tracks)
	{
		// Each progress clock Id names once, on its first layer: every layer it moves follows that one progress.
		TArray<const FWidgetAnimProgressDrive*, TInlineAllocator<4>> DrivesSet;

		for (int32 Index = 0; Index < Track.Settings.Num(); ++Index)
		{
			const FWidgetAnimSettings& Layer = Track.Settings[Index];
			if (WidgetAnimDrives::Names(Id, Track.AnimId, Layer) && !DrivesSet.Contains(Layer.ProgressDrive.Get()))
			{
				const FWidgetAnimProgressDrive* Drive = Layer.ProgressDrive.Get();
				DrivesSet.Add(Drive);

				if (Track.States.Num() != Track.Settings.Num())
				{
					ResetWidgetAnimations(Track.Settings, Track.States);
					InitializeValueLayers(Player, Track);
				}

				const int32 Leader = WidgetAnimDrives::FindLeader(Track.Settings, Drive);
				if (Leader == INDEX_NONE)
				{
					continue;
				}

				// Played over its Progress Animation Duration until now: it carries on from where its clock had got to.
				WidgetAnimDrives::TakeOver(Track.Settings, Track.States, Leader);

				++Taken;
				FWidgetAnimState& State = Track.States[Leader];
				WidgetAnimValue::SetTarget(Drive->Options, Drive->GetMotion(), State.DriveState, Drive->Options.ToProgress(Value), bSnap);

				// Run again to get there - and a snap, which moves nothing over time, is posed by the next tick all the same.
				State.bFinished = false;
				Track.bFinished = false;
				continue;
			}

			if (!Layer.IsValueDriven() || !Id.Matches(Layer.ValueScope, Layer.ValueName) || (!Id.AnimId.IsNone() && Track.AnimId != Id.AnimId))
			{
				continue;
			}

			// Normally already in step; a track not yet ticked is sized here, its value layers put where they start.
			if (Track.States.Num() != Track.Settings.Num())
			{
				ResetWidgetAnimations(Track.Settings, Track.States);
				InitializeValueLayers(Player, Track);
			}

			++Taken;
			FWidgetAnimState& State = Track.States[Index];
			if (WidgetAnimValue::SetTarget(Layer.ValueOptions, Layer.GetValueMotion(), State.ValueState, Layer.ValueOptions.ToProgress(Value), bSnap) && State.ValueState.IsMoving())
			{
				// Moving again: the track runs, and is reported finished again once everything in it has come to rest.
				State.bFinished = false;
				Track.bFinished = false;
			}

			// A Pulse or Shake given strength goes on: its track runs until the strength is back to nothing.
			if (WidgetAnimTypeOscillates(Layer.AnimType) && State.ValueState.Target.X > 0.f)
			{
				State.bFinished = false;
				Track.bFinished = false;
			}
		}
	}

	// Something will be posed differently - a value on its way, or one snapped to, which moves nothing over time.
	if (Taken > 0)
	{
		Player.MarkChanged();
	}

	// An animation's own progress belongs to the use of it playing, and goes with it: never remembered.
	if (Id.ValueName == WidgetAnimValue::ProgressValueName)
	{
		return Taken;
	}

	// Remembered in the scope it was set in, replacing what was remembered there.
	FWidgetAnimRememberedValue* Remembered = Player.Values.FindByPredicate([&Id](const FWidgetAnimRememberedValue& Candidate)
	{
		return Candidate.ValueName == Id.ValueName && Candidate.bAnywhere == Id.IsAnywhere() && Candidate.Animation.Get() == Id.Animation;
	});
	if (!Remembered)
	{
		Remembered = &Player.Values.AddDefaulted_GetRef();
		Remembered->ValueName = Id.ValueName;
		Remembered->bAnywhere = Id.IsAnywhere();
		Remembered->Animation = const_cast<UObject*>(Id.Animation);
	}
	Remembered->Value = Value;

	// A value set anywhere is the newest word on every scope's copy of it too.
	if (Id.IsAnywhere())
	{
		for (FWidgetAnimRememberedValue& Other : Player.Values)
		{
			if (Other.ValueName == Id.ValueName)
			{
				Other.Value = Value;
			}
		}
	}

	return Taken;
}

bool UWidgetAnimLibrary::GetWidgetAnimTrackValue(const FWidgetAnimPlayer& Player, const FWidgetAnimValueId& Id, bool bTargetValue, FVector4& OutValue)
{
	for (const FWidgetAnimTrack& Track : Player.Tracks)
	{
		for (int32 Index = 0; Index < Track.Settings.Num() && Index < Track.States.Num(); ++Index)
		{
			const FWidgetAnimSettings& Layer = Track.Settings[Index];
			if (WidgetAnimDrives::Names(Id, Track.AnimId, Layer))
			{
				const int32 Leader = WidgetAnimDrives::FindLeader(Track.Settings, Layer.ProgressDrive.Get());
				const float Progress = WidgetAnimDrives::GetProgress(Track.Settings, Track.States, Leader, bTargetValue);
				OutValue = Layer.ProgressDrive->Options.FromProgress(FVector4f(Progress, Progress, Progress, Progress));
				return true;
			}

			if (Layer.IsValueDriven() && Id.Matches(Layer.ValueScope, Layer.ValueName) && (Id.AnimId.IsNone() || Track.AnimId == Id.AnimId))
			{
				const FWidgetAnimValueState& State = Track.States[Index].ValueState;
				OutValue = Layer.ValueOptions.FromProgress(bTargetValue ? State.Target : State.Progress);
				return true;
			}
		}
	}

	// Not running: what was last set - for the name anywhere, any scope's; for one scope, its own or the name anywhere's.
	const FWidgetAnimRememberedValue* Remembered = Id.IsAnywhere()
		? Player.Values.FindByPredicate([&Id](const FWidgetAnimRememberedValue& Candidate) { return Candidate.ValueName == Id.ValueName; })
		: FindRememberedValue(Player, Id.Animation, Id.ValueName);
	if (Remembered)
	{
		OutValue = Remembered->Value;
		return true;
	}

	OutValue = FVector4(0.0, 0.0, 0.0, 0.0);
	return false;
}

bool UWidgetAnimLibrary::HasValueLayer(const TArray<FWidgetAnimSettings>& Settings, const FWidgetAnimValueId& Id)
{
	return Settings.ContainsByPredicate([&Id](const FWidgetAnimSettings& Layer)
	{
		return (Layer.IsValueDriven() && Id.Matches(Layer.ValueScope, Layer.ValueName)) || WidgetAnimDrives::Names(Id, Id.AnimId, Layer);
	});
}

bool UWidgetAnimLibrary::AssignDefaultValueNames(TArray<FWidgetAnimLayer>& Layers, TSet<FName>* TakenNames)
{
	TSet<FName> LocalTaken;
	TSet<FName>& Taken = TakenNames ? *TakenNames : LocalTaken;

	// The names already given count as taken first, wherever they are in the list, so a new name never repeats one.
	for (const FWidgetAnimLayer& Layer : Layers)
	{
		if (Layer.IsValueDriven() && !Layer.ValueName.IsNone())
		{
			Taken.Add(Layer.ValueName);
		}
	}

	bool bChanged = false;
	for (FWidgetAnimLayer& Layer : Layers)
	{
		if (!Layer.IsValueDriven() || !Layer.ValueName.IsNone())
		{
			continue;
		}

		// The type's name: its registered type's, or the built-in one's display name - or, for an animation track, its
		// animation's ("Hold Ring" for WA_Hold_Ring).
		FString Base;
		if (Layer.IsAnimationTrack())
		{
			Base = Layer.InsertedAnimation->GetName();
			Base.RemoveFromStart(TEXT("WA_"));
			Base.ReplaceInline(TEXT("_"), TEXT(" "));
		}
		else if (Layer.AnimType == EWidgetAnimType::Custom)
		{
			const FWidgetAnimEvaluator* Evaluator = Layer.FindEvaluator();
			Base = Evaluator ? Evaluator->GetTrackDisplayName(Layer.TypeParams).ToString() : FString(TEXT("Value"));
		}
		else
		{
			Base = StaticEnum<EWidgetAnimType>()->GetDisplayNameTextByValue(static_cast<int64>(Layer.AnimType)).ToString();
		}
		Base.TrimStartAndEndInline();
		if (Base.IsEmpty())
		{
			Base = TEXT("Value");
		}

		// "Scale", then "Scale 1", "Scale 2" - the first free one.
		FName Candidate(*Base);
		for (int32 Suffix = 1; Taken.Contains(Candidate); ++Suffix)
		{
			Candidate = FName(*FString::Printf(TEXT("%s %d"), *Base, Suffix));
		}

		Layer.ValueName = Candidate;
		Taken.Add(Candidate);
		bChanged = true;
	}

	return bChanged;
}

namespace
{
	/** Whether an exit starting now would have to wait for this value-driven layer. */
	bool ValueLayerHoldsExit(const FWidgetAnimSettings& Layer, const FWidgetAnimValueState& State)
	{
		// A Pulse's value is how strong it pulses, not somewhere it is going: nothing for an exit to wait for. A Shake's too.
		if (WidgetAnimTypeOscillates(Layer.AnimType))
		{
			return false;
		}

		switch (Layer.ValueOptions.OnExit)
		{
		case EWidgetAnimValueExit::WaitForValue:
			return State.IsMoving();

		case EWidgetAnimValueExit::CompleteFirst:
		{
			if (State.IsMoving())
			{
				return true;
			}
			for (int32 Channel = 0; Channel < Layer.ValueOptions.GetNumChannels(); ++Channel)
			{
				if (State.Progress[Channel] < 1.f)
				{
					return true;
				}
			}
			return false;
		}

		default:
			return false;
		}
	}
}

bool UWidgetAnimLibrary::PrepareValueLayersForExit(FWidgetAnimPlayer& Player)
{
	bool bAnyHolding = false;
	Player.MarkChanged();

	for (FWidgetAnimTrack& Track : Player.Tracks)
	{
		if (Track.bIsOut)
		{
			continue;
		}

		for (int32 Index = 0; Index < Track.Settings.Num() && Index < Track.States.Num(); ++Index)
		{
			const FWidgetAnimSettings& Layer = Track.Settings[Index];
			if (!Layer.IsValueDriven())
			{
				continue;
			}

			FWidgetAnimState& State = Track.States[Index];
			if (!WidgetAnimTypeOscillates(Layer.AnimType) && Layer.ValueOptions.OnExit == EWidgetAnimValueExit::CompleteFirst
				&& WidgetAnimValue::SetTarget(Layer.ValueOptions, Layer.GetValueMotion(), State.ValueState, FVector4f(1.f, 1.f, 1.f, 1.f), /*bSnap=*/false)
				&& State.ValueState.IsMoving())
			{
				State.bFinished = false;
				Track.bFinished = false;
			}

			bAnyHolding = ValueLayerHoldsExit(Layer, State.ValueState) || bAnyHolding;
		}
	}

	return bAnyHolding;
}

bool UWidgetAnimLibrary::IsAnyValueLayerHoldingExit(const FWidgetAnimPlayer& Player)
{
	for (const FWidgetAnimTrack& Track : Player.Tracks)
	{
		if (Track.bIsOut)
		{
			continue;
		}

		for (int32 Index = 0; Index < Track.Settings.Num() && Index < Track.States.Num(); ++Index)
		{
			if (Track.Settings[Index].IsValueDriven() && ValueLayerHoldsExit(Track.Settings[Index], Track.States[Index].ValueState))
			{
				return true;
			}
		}
	}

	return false;
}

namespace WidgetAnimValueShape
{
	/** Copies one shape setting - its X from FromX, its Y from FromY, as bX and bY say - into Into. */
	void CopyShapeField(EWidgetAnimTransformPart Field, bool bX, bool bY, const FShapeBoxShape& FromX, const FShapeBoxShape& FromY, FShapeBoxShape& Into)
	{
		auto Axes = [bX, bY](const FVector2D& X, const FVector2D& Y, FVector2D& Out)
		{
			if (bX)
			{
				Out.X = X.X;
			}
			if (bY)
			{
				Out.Y = Y.Y;
			}
		};

		// A corner's cut size, and its style with it - a size means little under the other style.
		auto Corner = [&Axes](const FShapeBoxCorner& X, const FShapeBoxCorner& Y, FShapeBoxCorner& Out)
		{
			Axes(X.Size, Y.Size, Out.Size);
			Out.Style = X.Style;
		};

		switch (Field)
		{
		case EWidgetAnimTransformPart::ShapeAngle:             Axes(FromX.Angle, FromY.Angle, Into.Angle); break;
		case EWidgetAnimTransformPart::ShapeBend:              Axes(FromX.Bend, FromY.Bend, Into.Bend); break;
		case EWidgetAnimTransformPart::ShapeCornerBottomLeft:  Corner(FromX.Corners.BottomLeft, FromY.Corners.BottomLeft, Into.Corners.BottomLeft); break;
		case EWidgetAnimTransformPart::ShapeCornerBottomRight: Corner(FromX.Corners.BottomRight, FromY.Corners.BottomRight, Into.Corners.BottomRight); break;
		case EWidgetAnimTransformPart::ShapeCornerTopLeft:     Corner(FromX.Corners.TopLeft, FromY.Corners.TopLeft, Into.Corners.TopLeft); break;
		case EWidgetAnimTransformPart::ShapeCornerTopRight:    Corner(FromX.Corners.TopRight, FromY.Corners.TopRight, Into.Corners.TopRight); break;
		case EWidgetAnimTransformPart::ShapeEdgeBowBottom:     Into.EdgeBow.Bottom = FromX.EdgeBow.Bottom; break;
		case EWidgetAnimTransformPart::ShapeEdgeBowLeft:       Into.EdgeBow.Left = FromX.EdgeBow.Left; break;
		case EWidgetAnimTransformPart::ShapeEdgeBowRight:      Into.EdgeBow.Right = FromX.EdgeBow.Right; break;
		case EWidgetAnimTransformPart::ShapeEdgeBowTop:        Into.EdgeBow.Top = FromX.EdgeBow.Top; break;
		case EWidgetAnimTransformPart::ShapeExtrude:           Axes(FromX.Extrude, FromY.Extrude, Into.Extrude); break;
		case EWidgetAnimTransformPart::ShapeInset:             Axes(FromX.Inset, FromY.Inset, Into.Inset); break;
		case EWidgetAnimTransformPart::ShapePerspective:       Into.Perspective = FromX.Perspective; break;
		case EWidgetAnimTransformPart::ShapeRotation:          Into.Rotation = FromX.Rotation; break;
		case EWidgetAnimTransformPart::ShapeWrap:              Axes(FromX.Wrap, FromY.Wrap, Into.Wrap); break;
		default: break;
		}
	}
}

void UWidgetAnimLibrary::ApplyValueLayerContribution(const FWidgetAnimSettings& Settings, const FWidgetAnimValueState& ValueState, FWidgetAnimResult& OutResult, const FWidgetAnimBaselineSet* Baselines)
{
	// A value poses the same whichever direction its track was started in: only the layer's own Invert Animation turns
	// it round, so a mirrored exit shows the value the entrance was showing.
	auto Directed = [&Settings, &ValueState](int32 Channel)
	{
		const float Progress = FMath::Clamp(ValueState.Progress[Channel], 0.f, 1.f);
		return Settings.bInvertAnimation ? 1.f - Progress : Progress;
	};

	// A Shake: its noise at its own clock, as strong as the value says - no falloff over a run, since it has none.
	if (Settings.AnimType == EWidgetAnimType::Shake)
	{
		FVector2D Offset;
		float Angle = 0.f;
		Settings.Shake.Sample(ValueState.PulseTime, Directed(0), GetShakeItemSeed(Settings), Offset, Angle);
		FWidgetAnimTargetedTransform& Into = FindOrAddTargetedTransform(OutResult, Settings.TargetWidget, Settings.Part);
		Into.Translation += Offset;
		Into.Angle += Angle;
		return;
	}

	// A Pulse: one pulse of its own clock, its size scaled by how strong the value says it is.
	if (Settings.AnimType == EWidgetAnimType::Pulse)
	{
		const float Period = FMath::Max(Settings.Duration, KINDA_SMALL_NUMBER);
		FWidgetAnimResult Pulse;
		ApplyAnimationContribution(Settings, FMath::Clamp(ValueState.PulseTime / Period, 0.f, 1.f), Pulse, Baselines, /*bTowardStart=*/false);

		const float Strength = Directed(0);
		for (const FWidgetAnimTargetedTransform& Pose : Pulse.TargetedTransforms)
		{
			FWidgetAnimTargetedTransform& Into = FindOrAddTargetedTransform(OutResult, Pose.TargetWidget, Pose.Part);
			Into.Scale *= FVector2D::UnitVector + (Pose.Scale - FVector2D::UnitVector) * Strength;
		}
		return;
	}

	const int32 NumChannels = Settings.ValueOptions.GetNumChannels();
	const bool bWholePath = !WidgetAnimTypeHasValueDrives(Settings.AnimType) || Settings.ValueDrives == EWidgetAnimTransformValueTarget::WholePath;
	if (NumChannels <= 1 && bWholePath)
	{
		ApplyAnimationContribution(Settings, Directed(0), OutResult, Baselines, /*bTowardStart=*/false);
		return;
	}

	// Several channels, or one part of a Transform: the track posed once per channel, each pose lending its own axis or
	// color channel. The first carries everything a single value would - so a type with one axis follows X, or R.
	FWidgetAnimResult Parts[4];
	for (int32 Channel = 0; Channel < FMath::Max(NumChannels, 1); ++Channel)
	{
		ApplyAnimationContribution(Settings, Directed(Channel), Parts[Channel], Baselines, /*bTowardStart=*/false);
	}
	const FWidgetAnimResult& First = Parts[0];
	const FWidgetAnimResult& Second = NumChannels >= 2 ? Parts[1] : Parts[0];

	// A Transform driving one field, or one axis of it: the rest of its keys stay out of it.
	bool bX = true;
	bool bY = true;
	const EWidgetAnimTransformPart Driven = bWholePath ? EWidgetAnimTransformPart::WholePath : WidgetAnimValue::GetTransformPart(Settings.ValueDrives, bX, bY);

	for (const FWidgetAnimTargetedTransform& Pose : First.TargetedTransforms)
	{
		FWidgetAnimTargetedTransform Combined = Pose;
		const FWidgetAnimTargetedTransform* YPose = Second.TargetedTransforms.FindByPredicate([&Pose](const FWidgetAnimTargetedTransform& Other)
		{
			return Other.TargetWidget == Pose.TargetWidget && Other.Part == Pose.Part;
		});
		if (YPose)
		{
			Combined.Scale.Y = YPose->Scale.Y;
			Combined.Translation.Y = YPose->Translation.Y;
			Combined.Shear.Y = YPose->Shear.Y;
		}

		// Everything but the driven field - all of the pose, when it is a shape setting - as the identity.
		if (!bWholePath)
		{
			auto Keep = [bX, bY](const FVector2D& Posed, double Identity)
			{
				return FVector2D(bX ? Posed.X : Identity, bY ? Posed.Y : Identity);
			};

			Combined.Opacity = 1.f;
			Combined.Translation = Driven == EWidgetAnimTransformPart::Translation ? Keep(Combined.Translation, 0.0) : FVector2D::ZeroVector;
			Combined.Scale = Driven == EWidgetAnimTransformPart::Scale ? Keep(Combined.Scale, 1.0) : FVector2D::UnitVector;
			Combined.Shear = Driven == EWidgetAnimTransformPart::Shear ? Keep(Combined.Shear, 0.0) : FVector2D::ZeroVector;
			Combined.Angle = Driven == EWidgetAnimTransformPart::Angle ? Combined.Angle : 0.f;
		}

		// Folded in the way layers always fold: opacity and scale multiply, the rest add.
		FWidgetAnimTargetedTransform& Into = FindOrAddTargetedTransform(OutResult, Pose.TargetWidget, Pose.Part);
		Into.Opacity *= Combined.Opacity;
		Into.Scale *= Combined.Scale;
		Into.Translation += Combined.Translation;
		Into.Shear += Combined.Shear;
		Into.Angle += Combined.Angle;
	}

	for (const FWidgetAnimScalarTarget& Scalar : First.ScalarParameters)
	{
		FWidgetAnimScalarTarget& Into = OutResult.FindOrAddScalar(Scalar.TargetWidget, Scalar.Slot, Scalar.ParameterName);
		Into = Scalar;
	}

	for (const FWidgetAnimVectorTarget& Vector : First.VectorParameters)
	{
		FWidgetAnimVectorTarget Combined = Vector;
		for (int32 Channel = 1; Channel < NumChannels && Channel < 4; ++Channel)
		{
			const FWidgetAnimVectorTarget* ChannelPose = Parts[Channel].VectorParameters.FindByPredicate([&Vector](const FWidgetAnimVectorTarget& Other)
			{
				return Other.TargetWidget == Vector.TargetWidget && Other.Slot == Vector.Slot && Other.ParameterName == Vector.ParameterName;
			});
			if (ChannelPose)
			{
				Combined.Value.Component(Channel) = ChannelPose->Value.Component(Channel);
			}
		}
		OutResult.FindOrAddVector(Combined.TargetWidget, Combined.Slot, Combined.ParameterName) = Combined;
	}

	// The outline: whole on the whole path; on a shape setting, that setting from the keys - X from the first channel's
	// pose, Y from the second's - over the box's own authored outline.
	if (bWholePath)
	{
		for (const FWidgetAnimShapeTarget& Shape : First.ShapeTargets)
		{
			FWidgetAnimShapeTarget& Into = FindOrAddShapeTarget(OutResult, Shape.TargetBox);
			Into.Shape = Shape.Shape;
			Into.bIsBaseline = Shape.bIsBaseline;
		}
	}
	else if (WidgetAnimValue::IsShapePart(Driven))
	{
		for (const FWidgetAnimShapeTarget& Shape : First.ShapeTargets)
		{
			const FWidgetAnimShapeTarget* YShape = Second.ShapeTargets.FindByPredicate([&Shape](const FWidgetAnimShapeTarget& Other)
			{
				return Other.TargetBox == Shape.TargetBox;
			});
			const FShapeBoxShape* Authored = Baselines ? Baselines->FindShapeBaseline(Shape.TargetBox.Get()) : nullptr;

			FShapeBoxShape Outline = Authored ? *Authored : FShapeBoxShape();
			WidgetAnimValueShape::CopyShapeField(Driven, bX, bY, Shape.Shape, YShape ? YShape->Shape : Shape.Shape, Outline);

			FWidgetAnimShapeTarget& Into = FindOrAddShapeTarget(OutResult, Shape.TargetBox);
			Into.Shape = Outline;
			Into.bIsBaseline = false;
		}
	}

	for (const FWidgetAnimAuthoritativeWrite& Write : First.AuthoritativeWrites)
	{
		if (FWidgetAnimAuthoritativeWrite* Existing = FindAuthoritativeWrite(OutResult, Write.TargetWidget, Write.Channel))
		{
			*Existing = Write;
		}
		else
		{
			OutResult.AuthoritativeWrites.Add(Write);
		}
	}
}

void UWidgetAnimLibrary::StopWidgetAnimTracks(FWidgetAnimPlayer& Player, const TArray<FName>& AnimIds, bool bRemoveTracks)
{
	const bool bAll = AnimIds.Num() == 0;
	Player.MarkChanged();

	for (int32 Index = Player.Tracks.Num() - 1; Index >= 0; --Index)
	{
		if (!bAll && !AnimIds.Contains(Player.Tracks[Index].AnimId))
		{
			continue;
		}

		if (bRemoveTracks)
		{
			Player.Tracks.RemoveAt(Index);
		}
		else
		{
			// Left in place and marked done, so a track asked to stop holds where it got to rather than
			// snapping back to the baseline mid-motion.
			Player.Tracks[Index].bFinished = true;
			Player.Tracks[Index].bSettled = true;
		}
	}
}

bool UWidgetAnimLibrary::EndWidgetAnimTrackLoops(FWidgetAnimPlayer& Player, FName AnimId, bool bSoftSettle)
{
	if (AnimId.IsNone())
	{
		return false;
	}

	bool bAnyAffected = false;

	for (FWidgetAnimTrack& Track : Player.Tracks)
	{
		if (Track.bFinished || Track.AnimId != AnimId)
		{
			continue;
		}

		// Normally already in step; guarded so a track that has not been ticked yet cannot index past the end.
		if (Track.States.Num() != Track.Settings.Num())
		{
			ResetWidgetAnimations(Track.Settings, Track.States);
		}

		for (int32 Index = 0; Index < Track.Settings.Num(); ++Index)
		{
			const FWidgetAnimSettings& Settings = Track.Settings[Index];
			FWidgetAnimState& State = Track.States[Index];

			// A value-driven layer has no loop to end: it goes where its value sends it.
			if (State.bFinished || Settings.IsValueDriven())
			{
				continue;
			}

			if (bSoftSettle)
			{
				// Non-looping layers already run to their end without being told.
				if (Settings.bLoop)
				{
					State.bSettleRequested = true;
				}
			}
			else
			{
				// Straight to the end pose. The next tick sees every layer finished, marks the track
				// finished and reports it, and applies this pose (or drops the track if it doesn't hold).
				// A finished clock is always evaluated at the end of its timeline, whatever CycleTime says.
				State.DelayRemaining = 0.f;
				State.bPlayingForward = true;
				State.bFinished = true;
				State.bSettleRequested = false;
			}
		}

		bAnyAffected = true;
	}

	if (bAnyAffected)
	{
		Player.MarkChanged();
	}
	return bAnyAffected;
}

bool UWidgetAnimLibrary::AreWidgetAnimTracksPlaying(const FWidgetAnimPlayer& Player, const TArray<FName>& AnimIds)
{
	const bool bAll = AnimIds.Num() == 0;

	for (const FWidgetAnimTrack& Track : Player.Tracks)
	{
		if (Track.bFinished)
		{
			continue;
		}

		if (bAll || AnimIds.Contains(Track.AnimId))
		{
			return true;
		}
	}

	return false;
}

void UWidgetAnimLibrary::ReadValueInputs(UWidget* HostWidget, FWidgetAnimPlayer& Player, float DeltaTime)
{
	// The designer's previews play value tracks on the clock; a pointer over the editor isn't a player's hand.
	if (HostWidget && HostWidget->IsDesignTime())
	{
		return;
	}

	// Read with time once a frame: a second read with time in the same frame - a host checking its inputs before it decides
	// to tick, then the tick - is read as a mid-frame pose is (below), so nothing is measured twice over one frame.
	if (DeltaTime > 0.f)
	{
		if (Player.LastInputReadFrame == GFrameCounter)
		{
			DeltaTime = 0.f;
		}
		else
		{
			Player.LastInputReadFrame = GFrameCounter;
		}
	}

	// Leaving: an entrance's inputs stop, so an exit waiting for a value to arrive (On Exit) isn't held open by a pointer
	// that keeps moving, and the exit plays the way it was authored.
	const bool bLeaving = Player.Phase == EWidgetAnimPhase::PlayingOut || Player.Phase == EWidgetAnimPhase::Hidden;

	FWidgetAnimInputContext Context;
	Context.HostWidget = HostWidget;
	Context.DeltaTime = DeltaTime;
	WidgetAnimInput::ReadPointer(Context);

	// Read first and set after: setting a value can size a track's states, which mustn't happen under the loop.
	struct FFeed
	{
		FWidgetAnimValueId Id;
		FVector4 Value;
		bool bSnap = false;
	};
	TArray<FFeed, TInlineAllocator<4>> Feeds;

	// The values an input has already claimed this frame, changed or not - see below.
	TArray<TPair<const UObject*, FName>, TInlineAllocator<4>> Claimed;

	for (FWidgetAnimTrack& Track : Player.Tracks)
	{
		if ((bLeaving && !Track.bIsOut) || Track.States.Num() != Track.Settings.Num())
		{
			continue;
		}

		for (int32 Index = 0; Index < Track.Settings.Num(); ++Index)
		{
			FWidgetAnimSettings& Layer = Track.Settings[Index];
			FWidgetAnimInputSource* Source = Layer.IsValueDriven() && !Layer.ValueName.IsNone() ? Layer.ValueInput.GetMutablePtr<FWidgetAnimInputSource>() : nullptr;
			FWidgetAnimState& State = Track.States[Index];
			if (!Source)
			{
				continue;
			}

			// Tracks sharing a Value Name in one animation share one value, so one input feeds it: the first one's, even on
			// a frame it has nothing new to say - otherwise a second input on the same name would slip in between.
			const TPair<const UObject*, FName> Key(Layer.ValueScope.Get(), Layer.ValueName);
			if (Claimed.Contains(Key))
			{
				continue;
			}
			Claimed.Add(Key);

			// A pose applied mid-frame (zero time) reads only inputs that haven't read yet: a Speed read over no time is 0.
			if (DeltaTime <= 0.f && State.bInputSent)
			{
				continue;
			}
			const FWidgetAnimValueId Id = FWidgetAnimValueId::In(Key.Key, Key.Value);

			// Measured on the widget the track animates, as its Track Group's target resolved it.
			Context.MeasuredWidget = Layer.TargetWidget ? Layer.TargetWidget.Get() : HostWidget;
			if (!Context.MeasuredWidget)
			{
				continue;
			}

			FVector4 Value;
			if (!Source->Sample(Context, Value) || (State.bInputSent && Value == State.InputLastValue))
			{
				continue;
			}

			FFeed& Feed = Feeds.AddDefaulted_GetRef();
			Feed.Id = Id;
			Feed.Value = Value;

			// The first value since the track started is jumped to, so a card doesn't glide in from its Start Value.
			Feed.bSnap = !State.bInputSent;
			State.InputLastValue = Value;
			State.bInputSent = true;
		}

		// A progress clock's Progress Input - an animation's own, or an animation track's - read the same way, on the first
		// layer it moves. An animation's own progress is set for this track's entry only, by its Anim ID.
		TArray<const FWidgetAnimProgressDrive*, TInlineAllocator<4>> DrivesRead;
		for (int32 Index = 0; Index < Track.Settings.Num(); ++Index)
		{
			const FWidgetAnimSettings& Layer = Track.Settings[Index];
			FWidgetAnimProgressDrive* Drive = Layer.ProgressDrive.Get();
			FWidgetAnimInputSource* Source = Drive && !Layer.IsValueDriven() && !Drive->ValueName.IsNone() ? Drive->ValueInput.GetMutablePtr<FWidgetAnimInputSource>() : nullptr;
			if (!Source || DrivesRead.Contains(Drive))
			{
				continue;
			}
			DrivesRead.Add(Drive);

			FWidgetAnimState& State = Track.States[Index];
			if (DeltaTime <= 0.f && State.bDriveInputSent)
			{
				continue;
			}

			Context.MeasuredWidget = Layer.TargetWidget ? Layer.TargetWidget.Get() : HostWidget;
			if (!Context.MeasuredWidget)
			{
				continue;
			}

			FVector4 Value;
			if (!Source->Sample(Context, Value) || (State.bDriveInputSent && Value == State.DriveInputLastValue))
			{
				continue;
			}

			FFeed& Feed = Feeds.AddDefaulted_GetRef();
			Feed.Id = FWidgetAnimValueId::In(Layer.ValueScope.Get(), Drive->ValueName);
			if (Drive->ValueName == WidgetAnimValue::ProgressValueName)
			{
				Feed.Id.AnimId = Track.AnimId;
			}
			Feed.Value = Value;
			Feed.bSnap = !State.bDriveInputSent;
			State.DriveInputLastValue = Value;
			State.bDriveInputSent = true;
		}
	}

	for (const FFeed& Feed : Feeds)
	{
		SetWidgetAnimTrackValue(Player, Feed.Id, Feed.Value, Feed.bSnap);
	}
}

bool UWidgetAnimLibrary::HasValueInputs(const FWidgetAnimPlayer& Player)
{
	for (const FWidgetAnimTrack& Track : Player.Tracks)
	{
		if (Track.Settings.ContainsByPredicate([](const FWidgetAnimSettings& Layer) { return (Layer.IsValueDriven() && Layer.ValueInput.IsValid()) || (Layer.ProgressDrive.IsValid() && Layer.ProgressDrive->ValueInput.IsValid()); }))
		{
			return true;
		}
	}
	return false;
}

bool UWidgetAnimLibrary::AnimatesAtRest(const FWidgetAnimPlayer& Player)
{
	for (const FWidgetAnimTrack& Track : Player.Tracks)
	{
		for (const FWidgetAnimSettings& Layer : Track.Settings)
		{
			if (Layer.AnimType != EWidgetAnimType::Custom || Layer.bMuted)
			{
				continue;
			}
			const FWidgetAnimEvaluator* Evaluator = Layer.FindEvaluator();
			if (Evaluator && Evaluator->AnimatesAtRest(Layer.TypeParams))
			{
				return true;
			}
		}
	}
	return false;
}

bool UWidgetAnimLibrary::TickWidgetAnimPlayer(UWidget* FallbackWidget, FWidgetAnimPlayer& Player, const FWidgetAnimBaselineSet& Baselines, float DeltaTime, TArray<FName>& OutJustFinished)
{
	const WidgetAnimScratch::FScope Scratch;
	FWidgetAnimResult& Result = Scratch.Result;
	const bool bAnyPlaying = EvaluateWidgetAnimPlayer(FallbackWidget, Player, Baselines, DeltaTime, OutJustFinished, Result);

	// One apply for the whole player. Two animations touching the same widget therefore combine - opacity
	// and scale multiply, translation and angle add - instead of the later one erasing the earlier one.
	{
		// Who is writing, for WidgetAnim.DebugTransforms.
		const WidgetAnimTransformDebug::FWriterScope Writer(&Player, FallbackWidget, DeltaTime);
		ApplyComposedResult(FallbackWidget, Result, Baselines);
	}

	ReleaseFinishedTracks(Player);
	return bAnyPlaying;
}

void UWidgetAnimLibrary::ApplyComposedResult(UWidget* FallbackWidget, const FWidgetAnimResult& Result, const FWidgetAnimBaselineSet& Baselines)
{
	SCOPE_CYCLE_COUNTER(STAT_WidgetAnimApply);
	TRACE_CPUPROFILER_EVENT_SCOPE(WidgetAnimApplyPoses);

	ApplyResultInternal(FallbackWidget, Result, Baselines.bCaptured ? &Baselines : nullptr);
	ApplyMaterialAnimResult(Result, FallbackWidget);
}

void UWidgetAnimLibrary::ReleaseFinishedTracks(FWidgetAnimPlayer& Player)
{
	Player.Tracks.RemoveAll([](const FWidgetAnimTrack& Track) { return Track.bFinished && !Track.bHoldWhenFinished; });
}

bool UWidgetAnimLibrary::SettleWidgetAnimTrack(FWidgetAnimTrack& Track, const FWidgetAnimBaselineSet& Baselines)
{
	if (Track.bNeedsExpansion)
	{
		return false;
	}

	// A pose has no loop: every layer ends where one pass ends.
	for (FWidgetAnimSettings& Layer : Track.Settings)
	{
		Layer.bLoop = false;
		Layer.LoopCount = 0;
	}

	// Long steps, their cues thrown away: a spring settles within a few seconds, and jumping to a pose passes nothing.
	static constexpr int32 MaxSteps = 32;
	for (int32 Step = 0; Step < MaxSteps && !Track.bFinished; ++Step)
	{
		FWidgetAnimResult Discarded;
		bool bAllComplete = false;
		Track.bSettled = TickAnimationsInto(Track.Settings, Track.States, 1.f, Track.bIsOut, Discarded, &bAllComplete, &Baselines, /*bDriveValueLayers=*/true);
		if (bAllComplete)
		{
			Track.bFinished = true;
			Track.bSettled = true;
		}
	}
	return true;
}

bool UWidgetAnimLibrary::EvaluateWidgetAnimPlayer(UWidget* FallbackWidget, FWidgetAnimPlayer& Player, const FWidgetAnimBaselineSet& Baselines, float DeltaTime, TArray<FName>& OutJustFinished, FWidgetAnimResult& OutResult)
{
	SCOPE_CYCLE_COUNTER(STAT_WidgetAnimEvaluate);
	TRACE_CPUPROFILER_EVENT_SCOPE(WidgetAnimEvaluatePlayer);
	INC_DWORD_STAT_BY(STAT_WidgetAnimTracksEvaluated, Player.Tracks.Num());

	OutJustFinished.Reset();

	// Time moves a player once a frame. Ticked again with time in the same frame, it re-poses where it is instead: a
	// second advance put every track a frame ahead for that frame - the widget drawn a step further on - and, if the extra
	// tick came from something that only runs now and then, looked like a one-frame jump. Never in the editor's
	// previews, which clock their players by hand.
	const bool bRuntime = !FallbackWidget || !FallbackWidget->IsDesignTime();
	if (DeltaTime > 0.f && bRuntime)
	{
		if (Player.LastAdvanceFrame == GFrameCounter)
		{
			WidgetAnimTransformDebug::NoteSecondAdvance(&Player, FallbackWidget, DeltaTime);
			DeltaTime = 0.f;
		}
		else
		{
			Player.LastAdvanceFrame = GFrameCounter;
		}
	}

	// Who is writing, for WidgetAnim.DebugTransforms.
	const WidgetAnimTransformDebug::FWriterScope Writer(&Player, FallbackWidget, DeltaTime);

	// Staggers of tracks just started fan out first, on this widget, so everything below sees their items.
	for (FWidgetAnimTrack& Track : Player.Tracks)
	{
		if (Track.bNeedsExpansion)
		{
			ExpandTrack(FallbackWidget, Player, Track);
		}
	}

	// Value Inputs first, so this frame's tracks move toward what the pointer or the scroll says now.
	ReadValueInputs(FallbackWidget, Player, DeltaTime);

	FWidgetAnimResult& Result = OutResult;
	bool bAnyPlaying = false;

	for (FWidgetAnimTrack& Track : Player.Tracks)
	{
		// Still waiting for a stagger's items to be countable: not started yet, but on its way.
		if (Track.bNeedsExpansion)
		{
			bAnyPlaying = true;
			continue;
		}

		if (Track.bFinished)
		{
			// Zero delta: re-evaluates the resting pose so a finished entrance keeps contributing rather
			// than vanishing from the combined result and letting the widget snap back.
			if (Track.bHoldWhenFinished)
			{
				TickAnimationsInto(Track.Settings, Track.States, 0.f, Track.bIsOut, Result, nullptr, &Baselines, /*bDriveValueLayers=*/true);
			}
			continue;
		}

		// Finished and settled are separate questions. A track is only finished once EVERY layer has
		// stopped - an endless loop never does until something ends it. Marking a track finished as
		// soon as its non-looping layers were done (which, for a stack made only of loops, was on its
		// very first frame) is what stopped loops from ever advancing: finished tracks are only
		// re-posed at zero delta.
		//
		// Evaluated on its own first, so that a track ending this frame without holding can hand back its
		// widgets instead of its pose - see below.
		const WidgetAnimScratch::FScope TrackScratch;
		FWidgetAnimResult& TrackResult = TrackScratch.Result;
		bool bAllComplete = false;
		Track.bSettled = TickAnimationsInto(Track.Settings, Track.States, DeltaTime * Track.Speed, Track.bIsOut, TrackResult, &bAllComplete, &Baselines, /*bDriveValueLayers=*/true);

		// The cues it passed, handed on now - copied, so a track let go below still fires the ones on its last frame - and
		// fired next frame, never while this player is in the middle of a tick.
		WidgetAnimCues::Queue(TrackResult.CueFires, FallbackWidget, Track.AnimId);

		if (bAllComplete)
		{
			Track.bFinished = true;
			Track.bSettled = true;
			OutJustFinished.AddUnique(Track.AnimId);
		}
		else
		{
			bAnyPlaying = true;
		}

		if (bAllComplete && !Track.bHoldWhenFinished)
		{
			// Done, and not holding: its widgets go back to what the other tracks and their baselines say, this
			// frame. Its end pose used to be applied on this last frame and then nothing ever wrote the widget
			// again, so a track that was meant to let go left its end pose behind for good. Touched as the
			// identity, so they are written even when no other track is moving them - or, over baselines only put back
			// (Preserve Authored Transforms off), noted as resting, so they are written at their captured pose rather than
			// at an identity that was never authored. Material parameters have nothing to go back to and keep the value
			// they ended on.
			for (const FWidgetAnimTargetedTransform& Touched : TrackResult.TargetedTransforms)
			{
				if (Baselines.bCaptured && !Baselines.bComposeTransforms && !Touched.Part.IsSet())
				{
					Result.RestingWidgets.Add(Touched.TargetWidget);
				}
				else
				{
					FindOrAddTargetedTransform(Result, Touched.TargetWidget, Touched.Part);
				}
			}
			Result.RestingWidgets.Append(TrackResult.RestingWidgets);

			if (Baselines.bCaptured)
			{
				for (const FWidgetAnimShapeTarget& Touched : TrackResult.ShapeTargets)
				{
					const bool bAlreadyWritten = Result.ShapeTargets.ContainsByPredicate([&Touched](const FWidgetAnimShapeTarget& Target)
					{
						return Target.TargetBox == Touched.TargetBox;
					});

					const FShapeBoxShape* Authored = Baselines.FindShapeBaseline(Touched.TargetBox);
					if (!bAlreadyWritten && Authored)
					{
						FWidgetAnimShapeTarget& ShapeTarget = FindOrAddShapeTarget(Result, Touched.TargetBox);
						ShapeTarget.Shape = *Authored;
						ShapeTarget.bIsBaseline = true;
					}
				}

				// Registered types' channels go back to their baselines the same way - a Count let go shows its
				// Text Block's own text again.
				for (const FWidgetAnimAuthoritativeWrite& Touched : TrackResult.AuthoritativeWrites)
				{
					AddAuthoritativeBaselineWrite(Result, Touched.TargetWidget, Touched.Channel, Touched.Evaluator, Touched.Params);
				}
			}
			continue;
		}

		// Folded into the frame's one result the way layers inside one track fold together: opacity and scale
		// multiply, translation, shear and angle add, and parameters and shapes are simply the last written.
		for (const FWidgetAnimTargetedTransform& Contribution : TrackResult.TargetedTransforms)
		{
			FWidgetAnimTargetedTransform& Combined = FindOrAddTargetedTransform(Result, Contribution.TargetWidget, Contribution.Part);
			Combined.Opacity *= Contribution.Opacity;
			Combined.Scale *= Contribution.Scale;
			Combined.Translation += Contribution.Translation;
			Combined.Shear += Contribution.Shear;
			Combined.Angle += Contribution.Angle;
		}
		Result.RestingWidgets.Append(TrackResult.RestingWidgets);
		Result.FoldMaterialParameters(TrackResult);
		for (const FWidgetAnimShapeTarget& Shape : TrackResult.ShapeTargets)
		{
			// Only an authored outline - a layer not started yet - never replaces one another track is animating.
			FWidgetAnimShapeTarget* Existing = Result.ShapeTargets.FindByPredicate([&Shape](const FWidgetAnimShapeTarget& Target)
			{
				return Target.TargetBox == Shape.TargetBox;
			});
			if (Shape.bIsBaseline && Existing)
			{
				continue;
			}

			FWidgetAnimShapeTarget& Combined = Existing ? *Existing : FindOrAddShapeTarget(Result, Shape.TargetBox);
			Combined.Shape = Shape.Shape;
			Combined.bIsBaseline = Shape.bIsBaseline;
		}
		for (const FWidgetAnimAuthoritativeWrite& Write : TrackResult.AuthoritativeWrites)
		{
			// The same rule as shapes: the last track to write a channel wins, but a baseline - a track not started
			// yet - never replaces a value another track is writing.
			FWidgetAnimAuthoritativeWrite* Existing = FindAuthoritativeWrite(Result, Write.TargetWidget, Write.Channel);
			if (Write.bIsBaseline && Existing)
			{
				continue;
			}

			if (Existing)
			{
				*Existing = Write;
			}
			else
			{
				Result.AuthoritativeWrites.Add(Write);
			}
		}
	}

	return bAnyPlaying;
}

// =====================================================================================================
// Widget targeting
// =====================================================================================================

namespace WidgetAnimTree
{
	/**
	 * Depth-first walk of Widget and everything beneath it, regardless of Is Variable.
	 *
	 * Widgets in the same WidgetTree are identified by their plain name, since names are unique per tree.
	 * Widgets inside a nested User Widget become "NestedUserWidgetName/InnerWidgetName", because there the
	 * plain name is only unique within the nested widget's own tree.
	 */
	static void Gather(UWidget* Widget, const FString& TreePrefix, TArray<TPair<FString, UWidget*>>& Out, int32 Depth = 0)
	{
		static constexpr int32 MaxDepth = 128;
		if (!Widget || Depth > MaxDepth)
		{
			return;
		}

		const FString Path = TreePrefix.IsEmpty() ? Widget->GetName() : (TreePrefix + TEXT("/") + Widget->GetName());
		Out.Emplace(Path, Widget);

		// UContentWidget (Button, Border, SizeBox, ...) derives from UPanelWidget, so this covers both.
		if (const UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		{
			for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
			{
				Gather(Panel->GetChildAt(Index), TreePrefix, Out, Depth + 1);
			}
		}
		else if (const UUserWidget* NestedUserWidget = Cast<UUserWidget>(Widget))
		{
			if (NestedUserWidget->WidgetTree)
			{
				Gather(NestedUserWidget->WidgetTree->RootWidget, Path, Out, Depth + 1);
			}
		}
	}
}

void UWidgetAnimLibrary::GatherTargetableWidgetPaths(UWidget* Root, TArray<FString>& OutPaths)
{
	TArray<TPair<FString, UWidget*>> All;
	WidgetAnimTree::Gather(Root, FString(), All);

	OutPaths.Reset(All.Num());
	for (const TPair<FString, UWidget*>& Entry : All)
	{
		OutPaths.AddUnique(Entry.Key);
	}
}

namespace WidgetAnimTree
{
	/**
	 * The widget at Segments - one name per tree, as Gather writes a path - found by walking the trees by name: the widget
	 * named Segments[0] in Widget's tree, then, inside it if it is a User Widget, the one named Segments[1], and so on. The
	 * same widget Gather's list would give for the path, without building that list: every path as a string, for every
	 * widget in the tree, on every lookup - and a lookup is made for every target of every animation started, and every
	 * frame for a state machine input measured on a target.
	 */
	static UWidget* FindBySegments(UWidget* Widget, TConstArrayView<FName> Segments, int32 Depth = 0)
	{
		static constexpr int32 MaxDepth = 128;
		if (!Widget || Depth > MaxDepth || Segments.Num() == 0)
		{
			return nullptr;
		}

		// FName comparison ignores case, as the string comparison did.
		if (Widget->GetFName() == Segments[0])
		{
			if (Segments.Num() == 1)
			{
				return Widget;
			}
			if (const UUserWidget* NestedUserWidget = Cast<UUserWidget>(Widget))
			{
				if (NestedUserWidget->WidgetTree)
				{
					if (UWidget* Found = FindBySegments(NestedUserWidget->WidgetTree->RootWidget, Segments.RightChop(1), Depth + 1))
					{
						return Found;
					}
				}
			}
		}

		// A panel's children are in its own tree: their paths go on from the same place.
		if (const UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		{
			for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
			{
				if (UWidget* Found = FindBySegments(Panel->GetChildAt(Index), Segments, Depth + 1))
				{
					return Found;
				}
			}
		}
		return nullptr;
	}
}

UWidget* UWidgetAnimLibrary::FindWidgetByPath(UWidget* Root, FName Path)
{
	if (Path.IsNone() || !Root)
	{
		return nullptr;
	}

	// One name per tree. A name no FName has ever held can't be any widget's: the lookup stops there.
	TArray<FName, TInlineAllocator<4>> Segments;
	const FString PathString = Path.ToString();
	int32 Start = 0;
	for (int32 Index = 0; Index <= PathString.Len(); ++Index)
	{
		if (Index == PathString.Len() || PathString[Index] == TEXT('/'))
		{
			const FString Segment = PathString.Mid(Start, Index - Start);
			const FName SegmentName(*Segment, FNAME_Find);
			if (Segment.IsEmpty() || SegmentName.IsNone())
			{
				return nullptr;
			}
			Segments.Add(SegmentName);
			Start = Index + 1;
		}
	}

	return WidgetAnimTree::FindBySegments(Root, Segments);
}

void UWidgetAnimLibrary::GatherTargetableWidgetPathsInTree(UUserWidget* Owner, TArray<FString>& OutPaths)
{
	OutPaths.Reset();

	if (Owner && Owner->WidgetTree)
	{
		GatherTargetableWidgetPaths(Owner->WidgetTree->RootWidget, OutPaths);
	}
}

UWidget* UWidgetAnimLibrary::FindWidgetByPathInTree(UUserWidget* Owner, FName Path)
{
	if (!Owner || !Owner->WidgetTree)
	{
		return nullptr;
	}

	return FindWidgetByPath(Owner->WidgetTree->RootWidget, Path);
}

// =====================================================================================================
// Designer interactive-edit sync
// =====================================================================================================

#if WITH_EDITOR

namespace WidgetAnimDesignerSync
{
	static TArray<TWeakObjectPtr<UObject>>& GetRegistry()
	{
		static TArray<TWeakObjectPtr<UObject>> Registry;
		return Registry;
	}

	void RegisterPreview(UObject* PreviewWidget)
	{
		if (!PreviewWidget)
		{
			return;
		}

		TArray<TWeakObjectPtr<UObject>>& Registry = GetRegistry();

		// Compacted on the way past. Design-time widgets are thrown away and rebuilt constantly, and
		// nothing else would ever clear the stale entries.
		Registry.RemoveAll([](const TWeakObjectPtr<UObject>& Entry) { return !Entry.IsValid(); });
		Registry.AddUnique(PreviewWidget);
	}

	void UnregisterPreview(UObject* PreviewWidget)
	{
		GetRegistry().RemoveAll([PreviewWidget](const TWeakObjectPtr<UObject>& Entry)
		{
			return !Entry.IsValid() || Entry.Get() == PreviewWidget;
		});
	}

	void ForEachPreviewOf(const UObject* Template, TFunctionRef<void(UObject*)> Update)
	{
		if (!Template)
		{
			return;
		}

		for (const TWeakObjectPtr<UObject>& Entry : GetRegistry())
		{
			UObject* Preview = Entry.Get();
			if (!Preview || Preview == Template)
			{
				continue;
			}

			// Class and name together. Names are unique within a WidgetTree, and a preview is built from
			// the template as its archetype, so it carries the same name - which makes this exact rather
			// than a guess. Anything that somehow isn't a match simply doesn't get updated, and the
			// ordinary commit-time rebuild still puts it right.
			if (Preview->GetClass() != Template->GetClass())
			{
				continue;
			}

			// Editing Class Defaults on the widget being edited goes through that class's CDO, whose name
			// is "Default__WBP_Thing_C" and matches nothing. Class alone is the right test there: a CDO
			// stands for every instance of its class by definition.
			const bool bTemplateIsClassDefault = Template->HasAnyFlags(RF_ClassDefaultObject);
			if (bTemplateIsClassDefault || Preview->GetFName() == Template->GetFName())
			{
				Update(Preview);
			}
		}
	}

	bool IsInteractiveChange(const FPropertyChangedEvent& PropertyChangedEvent)
	{
		// Interactive is the mid-drag case this exists for. ValueSet is here too because a spin box sends
		// exactly one of those as the drag ENDS, and the preview rebuild that would otherwise pick that
		// value up is asynchronous - so without it the final value of a drag is the one frame that never
		// reaches the preview. Pushing a pose twice is free; missing the last one is what you notice.
		return PropertyChangedEvent.ChangeType == EPropertyChangeType::Interactive
			|| PropertyChangedEvent.ChangeType == EPropertyChangeType::ValueSet;
	}
}

#endif // WITH_EDITOR