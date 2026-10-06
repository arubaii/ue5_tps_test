// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimShapeTrack.h"

#include "NameTBD/WidgetAnimation/WidgetAnimTransform.h"
#include "NameTBD/Widgets/Shapes/WidgetShapeAnimation.h"

#define LOCTEXT_NAMESPACE "WidgetAnimShapeTrack"

namespace WidgetAnimShapeTrackPrivate
{
	/** The one channel a shape's keyed settings go through: two Shape tracks on one shape take turns, as two texts would. */
	const FName Channel(TEXT("ShapeSettings"));
}

FText FWidgetAnimShapeKeysEvaluator::GetDisplayName() const
{
	return LOCTEXT("DisplayName", "Shape");
}

FText FWidgetAnimShapeKeysEvaluator::GetDescription() const
{
	return LOCTEXT("Description", "Keys a shape's settings - its fill, stroke and trim, a Star's points, an Ellipse's sweep, a Custom Shape's path.");
}

UClass* FWidgetAnimShapeKeysEvaluator::GetTargetClass() const
{
	return UShapeWidget::StaticClass();
}

UShapeWidget* FWidgetAnimShapeKeysEvaluator::FindShape(UWidget* Widget, const UClass* Kind)
{
	if (UShapeWidget* Shape = Cast<UShapeWidget>(Widget))
	{
		return Shape;
	}

	// Inside a widget that isn't a shape: the first of the keys' kind, or - none of it there - the first shape of any kind,
	// whose shared settings still animate.
	if (Kind && Kind->IsChildOf(UShapeWidget::StaticClass()))
	{
		if (UShapeWidget* OfKind = Cast<UShapeWidget>(FindFirstTarget(Widget, Kind)))
		{
			return OfKind;
		}
	}
	return Cast<UShapeWidget>(FindFirstTarget(Widget, UShapeWidget::StaticClass()));
}

UObject* FWidgetAnimShapeKeysEvaluator::ResolveTarget(UWidget* Widget, const FInstancedStruct& Params) const
{
	const FWidgetAnimShapeKeysParams* Keys = GetParams(Params);
	return FindShape(Widget, Keys ? Keys->ShapeKind.Get() : nullptr);
}

FName FWidgetAnimShapeKeysEvaluator::GetChannel(const FInstancedStruct& Params) const
{
	return WidgetAnimShapeTrackPrivate::Channel;
}

void FWidgetAnimShapeKeysEvaluator::Apply(UObject* Target, const FInstancedStruct& Params, double Value) const
{
	// Without the rest of a write, Value is read as a place along the keys: 1.5 halfway from the second to the third.
	const FWidgetAnimShapeKeysParams* Keys = GetParams(Params);
	UShapeWidget* Shape = Cast<UShapeWidget>(Target);
	if (!Keys || !Shape || Keys->Keys.Num() == 0)
	{
		return;
	}
	const int32 Segment = FMath::Clamp(FMath::FloorToInt32(Value), 0, FMath::Max(Keys->Keys.Num() - 2, 0));
	ApplyKeys(Shape, *Keys, Segment, static_cast<float>(Value - Segment), nullptr);
}

void FWidgetAnimShapeKeysEvaluator::ApplyWrite(UObject* Target, const FInstancedStruct& Params, const FWidgetAnimAuthoritativeWrite& Write, const FInstancedStruct* Captured) const
{
	const FWidgetAnimShapeKeysParams* Keys = GetParams(Params);
	UShapeWidget* Shape = Cast<UShapeWidget>(Target);
	if (!Keys || !Shape || Keys->Keys.Num() == 0)
	{
		return;
	}
	const FWidgetAnimShapeKeysBaseline* Authored = Captured ? Captured->GetPtr<FWidgetAnimShapeKeysBaseline>() : nullptr;
	const int32 Segment = Write.KeySegment != INDEX_NONE ? Write.KeySegment : 0;
	ApplyKeys(Shape, *Keys, Segment, static_cast<float>(Write.Value), Authored);
}

void FWidgetAnimShapeKeysEvaluator::ApplyKeys(UShapeWidget* Target, const FWidgetAnimShapeKeysParams& Keys, int32 Segment, float Alpha, const FWidgetAnimShapeKeysBaseline* Authored) const
{
	const int32 IndexA = FMath::Clamp(Segment, 0, Keys.Keys.Num() - 1);
	const int32 IndexB = FMath::Min(IndexA + 1, Keys.Keys.Num() - 1);
	const FWidgetAnimShapeKey& KeyA = Keys.Keys[IndexA];
	const FWidgetAnimShapeKey& KeyB = Keys.Keys[IndexB];

	// A key that uses the authored shape reads the shape as it was before anything animated it. Without that capture - a
	// result applied with no baselines - it reads its own settings, as a Shape Box key does.
	auto ValueOf = [Authored](const FWidgetAnimShapeKey& Key, const FProperty* Property) -> const void*
	{
		if (Key.bUseAuthoredShape && Authored)
		{
			if (const void* Found = WidgetShapeAnimation::FindValue(Authored->Values, Property))
			{
				return Found;
			}
		}
		return WidgetShapeAnimation::FindValue(Key.Values, Property);
	};

	TArray<const FProperty*> Properties;
	WidgetShapeAnimation::GetProperties(Target->GetClass(), Properties);

	bool bChanged = false;
	for (const FProperty* Property : Properties)
	{
		bChanged |= WidgetShapeAnimation::WriteBlend(Target, Property, ValueOf(KeyA, Property), ValueOf(KeyB, Property), IndexA == IndexB ? 0.f : Alpha);
	}

	if (bChanged)
	{
		Target->InvalidateShape();
	}
}

bool FWidgetAnimShapeKeysEvaluator::CaptureBaseline(UObject* Target, const FInstancedStruct& Params, FInstancedStruct& OutBaseline) const
{
	const UShapeWidget* Shape = Cast<UShapeWidget>(Target);
	if (!Shape)
	{
		return false;
	}

	FWidgetAnimShapeKeysBaseline Baseline;
	WidgetShapeAnimation::LayOut(Baseline.Values, Shape->GetClass(), Shape);
	OutBaseline.InitializeAs<FWidgetAnimShapeKeysBaseline>(MoveTemp(Baseline));
	return true;
}

void FWidgetAnimShapeKeysEvaluator::RestoreBaseline(UObject* Target, const FInstancedStruct& Baseline) const
{
	UShapeWidget* Shape = Cast<UShapeWidget>(Target);
	const FWidgetAnimShapeKeysBaseline* Captured = Baseline.GetPtr<FWidgetAnimShapeKeysBaseline>();
	if (!Shape || !Captured)
	{
		return;
	}

	TArray<const FProperty*> Properties;
	WidgetShapeAnimation::GetProperties(Shape->GetClass(), Properties);

	bool bChanged = false;
	for (const FProperty* Property : Properties)
	{
		const void* Value = WidgetShapeAnimation::FindValue(Captured->Values, Property);
		bChanged |= WidgetShapeAnimation::WriteBlend(Shape, Property, Value, Value, 0.f);
	}
	if (bChanged)
	{
		Shape->InvalidateShape();
	}
}

namespace WidgetAnimShapeTrack
{
	const FWidgetAnimEvaluator* FindEvaluator()
	{
		return FWidgetAnimEvaluatorRegistry::Get().Find(FWidgetAnimShapeKeysParams::StaticStruct());
	}

	FInstancedStruct MakeParams(const FWidgetTransformAnim& Anim)
	{
		FInstancedStruct Params;
		Params.InitializeAs<FWidgetAnimShapeKeysParams>();
		FWidgetAnimShapeKeysParams& Keys = Params.GetMutable<FWidgetAnimShapeKeysParams>();
		Keys.ShapeKind = Anim.ShapeKind;
		Keys.Keys.Reserve(Anim.Keys.Num());
		for (const FWidgetTransformKeyframe& Key : Anim.Keys)
		{
			FWidgetAnimShapeKey& Played = Keys.Keys.AddDefaulted_GetRef();
			Played.bUseAuthoredShape = Key.bUseAuthoredShape;
			Played.Values = Key.ShapeValues;
		}
		return Params;
	}

	UShapeWidget* ResolveShape(UWidget* Widget, const UClass* Kind)
	{
		return FWidgetAnimShapeKeysEvaluator::FindShape(Widget, Kind);
	}
}

#undef LOCTEXT_NAMESPACE
