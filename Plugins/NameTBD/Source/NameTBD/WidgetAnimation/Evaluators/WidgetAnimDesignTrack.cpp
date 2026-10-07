// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimDesignTrack.h"

#include "NameTBD/WidgetAnimation/WidgetAnimTransform.h"
#include "NameTBD/WidgetAnimation/WidgetDesignAnimation.h"
#include "NameTBD/Widgets/Shapes/WidgetShapeAnimation.h"

#include "Components/Widget.h"

#define LOCTEXT_NAMESPACE "WidgetAnimDesignTrack"

namespace WidgetAnimDesignTrackPrivate
{
	/** The one channel a widget's keyed look goes through: two Design tracks on one widget take turns, as two texts would. */
	const FName Channel(TEXT("DesignSettings"));
}

FText FWidgetAnimDesignKeysEvaluator::GetDisplayName() const
{
	return LOCTEXT("DisplayName", "Design");
}

FText FWidgetAnimDesignKeysEvaluator::GetDescription() const
{
	return LOCTEXT("Description", "Keys a widget's look - an Image's brush and tint, a Text Block's color and font, a Border's background - blending one design into another.");
}

UClass* FWidgetAnimDesignKeysEvaluator::GetTargetClass() const
{
	return UWidget::StaticClass();
}

UWidget* FWidgetAnimDesignKeysEvaluator::FindDesignWidget(UWidget* Widget, const UClass* Kind)
{
	if (!Widget)
	{
		return nullptr;
	}
	if (!Kind || Widget->IsA(Kind))
	{
		return Widget;
	}

	// Inside a widget of another kind: the first of the keys' kind - an Image inside a button - or, none of it there, the
	// widget itself, whose settings in common with the kind still animate.
	if (UWidget* OfKind = Cast<UWidget>(FindFirstTarget(Widget, Kind)))
	{
		return OfKind;
	}
	return Widget;
}

UObject* FWidgetAnimDesignKeysEvaluator::ResolveTarget(UWidget* Widget, const FInstancedStruct& Params) const
{
	const FWidgetAnimDesignKeysParams* Keys = GetParams(Params);
	return FindDesignWidget(Widget, Keys ? Keys->DesignKind.Get() : nullptr);
}

FName FWidgetAnimDesignKeysEvaluator::GetChannel(const FInstancedStruct& Params) const
{
	return WidgetAnimDesignTrackPrivate::Channel;
}

void FWidgetAnimDesignKeysEvaluator::Apply(UObject* Target, const FInstancedStruct& Params, double Value) const
{
	// Without the rest of a write, Value is read as a place along the keys: 1.5 halfway from the second to the third.
	const FWidgetAnimDesignKeysParams* Keys = GetParams(Params);
	UWidget* Widget = Cast<UWidget>(Target);
	if (!Keys || !Widget || Keys->Keys.Num() == 0)
	{
		return;
	}
	const int32 Segment = FMath::Clamp(FMath::FloorToInt32(Value), 0, FMath::Max(Keys->Keys.Num() - 2, 0));
	ApplyKeys(Widget, *Keys, Segment, static_cast<float>(Value - Segment), nullptr);
}

void FWidgetAnimDesignKeysEvaluator::ApplyWrite(UObject* Target, const FInstancedStruct& Params, const FWidgetAnimAuthoritativeWrite& Write, const FInstancedStruct* Captured) const
{
	const FWidgetAnimDesignKeysParams* Keys = GetParams(Params);
	UWidget* Widget = Cast<UWidget>(Target);
	if (!Keys || !Widget || Keys->Keys.Num() == 0)
	{
		return;
	}
	const FWidgetAnimDesignKeysBaseline* Authored = Captured ? Captured->GetPtr<FWidgetAnimDesignKeysBaseline>() : nullptr;
	const int32 Segment = Write.KeySegment != INDEX_NONE ? Write.KeySegment : 0;
	ApplyKeys(Widget, *Keys, Segment, static_cast<float>(Write.Value), Authored);
}

void FWidgetAnimDesignKeysEvaluator::ApplyKeys(UWidget* Target, const FWidgetAnimDesignKeysParams& Keys, int32 Segment, float Alpha, const FWidgetAnimDesignKeysBaseline* Authored) const
{
	const int32 IndexA = FMath::Clamp(Segment, 0, Keys.Keys.Num() - 1);
	const int32 IndexB = FMath::Min(IndexA + 1, Keys.Keys.Num() - 1);
	const FWidgetAnimDesignKey& KeyA = Keys.Keys[IndexA];
	const FWidgetAnimDesignKey& KeyB = Keys.Keys[IndexB];

	// A key that uses the authored design reads the widget as it was before anything animated it. Without that capture - a
	// result applied with no baselines - it reads its own settings.
	auto ValueOf = [Authored](const FWidgetAnimDesignKey& Key, const FProperty* Property) -> const void*
	{
		if (Key.bUseAuthoredDesign && Authored)
		{
			if (const void* Found = WidgetShapeAnimation::FindValue(Authored->Values, Property))
			{
				return Found;
			}
		}
		return WidgetShapeAnimation::FindValue(Key.Values, Property);
	};

	TArray<const FProperty*> Properties;
	WidgetDesignAnimation::GetProperties(Target->GetClass(), Properties);

	// All of them at once: a font changing face cross-fades the colors its text is drawn in. See WidgetDesignAnimation::WriteBlends.
	TArray<const void*> From;
	TArray<const void*> To;
	From.Reserve(Properties.Num());
	To.Reserve(Properties.Num());
	for (const FProperty* Property : Properties)
	{
		From.Add(ValueOf(KeyA, Property));
		To.Add(ValueOf(KeyB, Property));
	}

	bool bNeedsSync = false;
	WidgetDesignAnimation::WriteBlends(Target, Properties, From, To, IndexA == IndexB ? 0.f : Alpha, nullptr, &bNeedsSync);
	if (bNeedsSync)
	{
		Target->SynchronizeProperties();
	}
}

bool FWidgetAnimDesignKeysEvaluator::CaptureBaseline(UObject* Target, const FInstancedStruct& Params, FInstancedStruct& OutBaseline) const
{
	const UWidget* Widget = Cast<UWidget>(Target);
	if (!Widget)
	{
		return false;
	}

	FWidgetAnimDesignKeysBaseline Baseline;
	WidgetDesignAnimation::LayOut(Baseline.Values, Widget->GetClass(), Widget);
	OutBaseline.InitializeAs<FWidgetAnimDesignKeysBaseline>(MoveTemp(Baseline));
	return true;
}

void FWidgetAnimDesignKeysEvaluator::RestoreBaseline(UObject* Target, const FInstancedStruct& Baseline) const
{
	UWidget* Widget = Cast<UWidget>(Target);
	const FWidgetAnimDesignKeysBaseline* Captured = Baseline.GetPtr<FWidgetAnimDesignKeysBaseline>();
	if (!Widget || !Captured)
	{
		return;
	}

	TArray<const FProperty*> Properties;
	WidgetDesignAnimation::GetProperties(Widget->GetClass(), Properties);

	bool bNeedsSync = false;
	for (const FProperty* Property : Properties)
	{
		const void* Value = WidgetShapeAnimation::FindValue(Captured->Values, Property);
		WidgetDesignAnimation::WriteBlend(Widget, Property, Value, Value, 0.f, bNeedsSync);
	}
	if (bNeedsSync)
	{
		Widget->SynchronizeProperties();
	}
}

namespace WidgetAnimDesignTrack
{
	const FWidgetAnimEvaluator* FindEvaluator()
	{
		return FWidgetAnimEvaluatorRegistry::Get().Find(FWidgetAnimDesignKeysParams::StaticStruct());
	}

	FInstancedStruct MakeParams(const FWidgetTransformAnim& Anim)
	{
		FInstancedStruct Params;
		Params.InitializeAs<FWidgetAnimDesignKeysParams>();
		FWidgetAnimDesignKeysParams& Keys = Params.GetMutable<FWidgetAnimDesignKeysParams>();
		Keys.DesignKind = Anim.DesignKind;
		Keys.Keys.Reserve(Anim.Keys.Num());
		for (const FWidgetTransformKeyframe& Key : Anim.Keys)
		{
			FWidgetAnimDesignKey& Played = Keys.Keys.AddDefaulted_GetRef();
			Played.bUseAuthoredDesign = Key.bUseAuthoredShape;
			Played.Values = Key.ShapeValues;
		}
		return Params;
	}

	UWidget* ResolveWidget(UWidget* Widget, const UClass* Kind)
	{
		return FWidgetAnimDesignKeysEvaluator::FindDesignWidget(Widget, Kind);
	}
}

#undef LOCTEXT_NAMESPACE
