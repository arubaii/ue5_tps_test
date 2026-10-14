// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimInteractionTracks.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "NameTBD/Widgets/Constraints/WidgetConstraintComponent.h"

#define LOCTEXT_NAMESPACE "WidgetAnimInteractionTracks"

namespace WidgetAnimInteractionPrivate
{
	/** The first component Find gives at or beneath Widget, depth first, through panels and into User Widgets. */
	template <typename TComponent>
	TComponent* FindComponentIn(UWidget* Widget, TComponent* (*Find)(const UWidget*), int32 Depth = 0)
	{
		if (!Widget || Depth > 8)
		{
			return nullptr;
		}
		if (TComponent* Found = Find(Widget))
		{
			return Found;
		}
		if (const UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		{
			for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
			{
				if (TComponent* Found = FindComponentIn(Panel->GetChildAt(Index), Find, Depth + 1))
				{
					return Found;
				}
			}
		}
		else if (const UUserWidget* UserWidget = Cast<UUserWidget>(Widget))
		{
			if (UserWidget->WidgetTree)
			{
				return FindComponentIn(UserWidget->WidgetTree->RootWidget, Find, Depth + 1);
			}
		}
		return nullptr;
	}

	FText ChannelName(const UEnum* Enum, int64 Value)
	{
		return Enum ? Enum->GetDisplayNameTextByValue(Value) : FText::GetEmpty();
	}
}

// =====================================================================================================
// Constraint
// =====================================================================================================

FText FWidgetAnimConstraintEvaluator::GetDisplayName() const
{
	return LOCTEXT("ConstraintName", "Constraint");
}

FText FWidgetAnimConstraintEvaluator::GetDescription() const
{
	return LOCTEXT("ConstraintDescription", "Animates a value of the widget's Constraints - a constraint's Strength, a Follow Path's Path Position (a ball sent round a ring), an Offset, an Angle Offset or a Distance. On the Track Group's widget, or the first widget inside it with Constraints.");
}

FText FWidgetAnimConstraintEvaluator::GetValueLabel() const
{
	return LOCTEXT("ConstraintValue", "Value");
}

FText FWidgetAnimConstraintEvaluator::GetCategory() const
{
	return LOCTEXT("InteractionCategory", "Interaction");
}

UObject* FWidgetAnimConstraintEvaluator::ResolveTarget(UWidget* Widget, const FInstancedStruct& Params) const
{
	return WidgetAnimInteractionPrivate::FindComponentIn<UWidgetConstraintComponent>(Widget, &UWidgetConstraintComponent::FindConstraintComponent);
}

FName FWidgetAnimConstraintEvaluator::GetChannel(const FInstancedStruct& Params) const
{
	const FWidgetAnimConstraintParams* Settings = GetParams(Params);
	if (!Settings)
	{
		return NAME_None;
	}
	return FName(*FString::Printf(TEXT("Constraint.%s.%d"), *Settings->ConstraintName.ToString(), static_cast<int32>(Settings->Channel)));
}

double FWidgetAnimConstraintEvaluator::GetEndpointValue(const FInstancedStruct& Params, bool bStart) const
{
	const FWidgetAnimConstraintParams* Settings = GetParams(Params);
	return Settings ? (bStart ? Settings->StartValue : Settings->EndValue) : 0.0;
}

void FWidgetAnimConstraintEvaluator::Apply(UObject* Target, const FInstancedStruct& Params, double Value) const
{
	UWidgetConstraintComponent* Component = Cast<UWidgetConstraintComponent>(Target);
	const FWidgetAnimConstraintParams* Settings = GetParams(Params);
	if (Component && Settings)
	{
		Component->SetConstraintValue(Settings->ConstraintName, Settings->Channel, static_cast<float>(Value));
	}
}

bool FWidgetAnimConstraintEvaluator::CaptureBaseline(UObject* Target, const FInstancedStruct& Params, FInstancedStruct& OutBaseline) const
{
	const UWidgetConstraintComponent* Component = Cast<UWidgetConstraintComponent>(Target);
	const FWidgetAnimConstraintParams* Settings = GetParams(Params);
	FWidgetAnimInteractionBaseline Baseline;
	if (!Component || !Settings || !Component->GetConstraintValue(Settings->ConstraintName, Settings->Channel, Baseline.Value))
	{
		return false;
	}
	Baseline.ConstraintName = Settings->ConstraintName;
	Baseline.Channel = static_cast<uint8>(Settings->Channel);
	OutBaseline.InitializeAs<FWidgetAnimInteractionBaseline>(Baseline);
	return true;
}

void FWidgetAnimConstraintEvaluator::RestoreBaseline(UObject* Target, const FInstancedStruct& Baseline) const
{
	UWidgetConstraintComponent* Component = Cast<UWidgetConstraintComponent>(Target);
	const FWidgetAnimInteractionBaseline* Captured = Baseline.GetPtr<FWidgetAnimInteractionBaseline>();
	if (Component && Captured)
	{
		Component->SetConstraintValue(Captured->ConstraintName, static_cast<EWidgetConstraintChannel>(Captured->Channel), Captured->Value);
	}
}

FText FWidgetAnimConstraintEvaluator::GetTrackSummary(const FInstancedStruct& Params) const
{
	const FWidgetAnimConstraintParams* Settings = GetParams(Params);
	if (!Settings)
	{
		return FText::GetEmpty();
	}
	const FText Channel = WidgetAnimInteractionPrivate::ChannelName(StaticEnum<EWidgetConstraintChannel>(), static_cast<int64>(Settings->Channel));
	return Settings->ConstraintName.IsNone() ? Channel : FText::Format(LOCTEXT("ConstraintSummary", "{0} {1}"), FText::FromName(Settings->ConstraintName), Channel);
}

// =====================================================================================================
// Pointer Effects
// =====================================================================================================

FText FWidgetAnimPointerEffectsEvaluator::GetDisplayName() const
{
	return LOCTEXT("PointerEffectsName", "Pointer Effects");
}

FText FWidgetAnimPointerEffectsEvaluator::GetDescription() const
{
	return LOCTEXT("PointerEffectsDescription", "Animates a value of the widget's Pointer Effects - Intensity (every effect at once), Tilt Angle, Magnet Pull or Light Strength: a card's tilt and shine switched on as it is selected. On the Track Group's widget, or the first widget inside it with Pointer Effects.");
}

FText FWidgetAnimPointerEffectsEvaluator::GetValueLabel() const
{
	return LOCTEXT("PointerEffectsValue", "Value");
}

FText FWidgetAnimPointerEffectsEvaluator::GetCategory() const
{
	return LOCTEXT("InteractionCategory", "Interaction");
}

UObject* FWidgetAnimPointerEffectsEvaluator::ResolveTarget(UWidget* Widget, const FInstancedStruct& Params) const
{
	return WidgetAnimInteractionPrivate::FindComponentIn<UPointerEffectsComponent>(Widget, &UPointerEffectsComponent::FindPointerEffects);
}

FName FWidgetAnimPointerEffectsEvaluator::GetChannel(const FInstancedStruct& Params) const
{
	const FWidgetAnimPointerEffectsParams* Settings = GetParams(Params);
	return Settings ? FName(*FString::Printf(TEXT("PointerEffects.%d"), static_cast<int32>(Settings->Channel))) : NAME_None;
}

double FWidgetAnimPointerEffectsEvaluator::GetEndpointValue(const FInstancedStruct& Params, bool bStart) const
{
	const FWidgetAnimPointerEffectsParams* Settings = GetParams(Params);
	return Settings ? (bStart ? Settings->StartValue : Settings->EndValue) : 0.0;
}

void FWidgetAnimPointerEffectsEvaluator::Apply(UObject* Target, const FInstancedStruct& Params, double Value) const
{
	UPointerEffectsComponent* Component = Cast<UPointerEffectsComponent>(Target);
	const FWidgetAnimPointerEffectsParams* Settings = GetParams(Params);
	if (Component && Settings)
	{
		Component->SetEffectValue(Settings->Channel, static_cast<float>(Value));
	}
}

bool FWidgetAnimPointerEffectsEvaluator::CaptureBaseline(UObject* Target, const FInstancedStruct& Params, FInstancedStruct& OutBaseline) const
{
	const UPointerEffectsComponent* Component = Cast<UPointerEffectsComponent>(Target);
	const FWidgetAnimPointerEffectsParams* Settings = GetParams(Params);
	if (!Component || !Settings)
	{
		return false;
	}
	FWidgetAnimInteractionBaseline Baseline;
	Baseline.Value = Component->GetEffectValue(Settings->Channel);
	Baseline.Channel = static_cast<uint8>(Settings->Channel);
	OutBaseline.InitializeAs<FWidgetAnimInteractionBaseline>(Baseline);
	return true;
}

void FWidgetAnimPointerEffectsEvaluator::RestoreBaseline(UObject* Target, const FInstancedStruct& Baseline) const
{
	UPointerEffectsComponent* Component = Cast<UPointerEffectsComponent>(Target);
	const FWidgetAnimInteractionBaseline* Captured = Baseline.GetPtr<FWidgetAnimInteractionBaseline>();
	if (Component && Captured)
	{
		Component->SetEffectValue(static_cast<EPointerEffectsChannel>(Captured->Channel), Captured->Value);
	}
}

FText FWidgetAnimPointerEffectsEvaluator::GetTrackSummary(const FInstancedStruct& Params) const
{
	const FWidgetAnimPointerEffectsParams* Settings = GetParams(Params);
	return Settings ? WidgetAnimInteractionPrivate::ChannelName(StaticEnum<EPointerEffectsChannel>(), static_cast<int64>(Settings->Channel)) : FText::GetEmpty();
}

// =====================================================================================================
// Pointer Distance and Motion
// =====================================================================================================

bool FWidgetAnimInput_PointerMotion::Sample(const FWidgetAnimInputContext& Context, FVector4& OutValue)
{
	if (!Context.bHasPointer)
	{
		bHasLast = false;
		if (Measure == EWidgetAnimPointerMeasure::Proximity)
		{
			OutValue = WidgetAnimValue::FromFloat(0.0);
			return true;
		}
		return false;
	}

	FVector2f Local;
	FVector2f Size;
	if (!WidgetAnimInput::ToUnanimatedLocal(Context.MeasuredWidget, Context.PointerPosition, Local, Size))
	{
		return false;
	}

	// The pointer's speed in the widget's units, eased so one jumpy frame doesn't spike it.
	if (bHasLast && Context.DeltaTime > 0.f)
	{
		const FVector2f Raw = (Local - LastLocal) / Context.DeltaTime;
		Velocity = FMath::Lerp(Velocity, Raw, 1.f - FMath::Exp(-Context.DeltaTime * 12.f));
	}
	LastLocal = Local;
	bHasLast = true;

	const FVector2f Middle = Size * 0.5f;
	const float DX = FMath::Max3(-Local.X, 0.f, Local.X - Size.X);
	const float DY = FMath::Max3(-Local.Y, 0.f, Local.Y - Size.Y);
	const float Distance = bFromEdges ? FMath::Sqrt(DX * DX + DY * DY) : FVector2f::Distance(Local, Middle);

	double Value = 0.0;
	switch (Measure)
	{
	case EWidgetAnimPointerMeasure::Distance:
		Value = Distance;
		break;
	case EWidgetAnimPointerMeasure::Proximity:
		Value = 1.0 - FMath::Clamp(Distance / FMath::Max(Range, 1.f), 0.f, 1.f);
		break;
	case EWidgetAnimPointerMeasure::Direction:
		Value = FMath::RadiansToDegrees(FMath::Atan2(Local.Y - Middle.Y, Local.X - Middle.X));
		break;
	case EWidgetAnimPointerMeasure::Speed:
		Value = Velocity.Size();
		break;
	case EWidgetAnimPointerMeasure::VelocityX:
		Value = Velocity.X;
		break;
	case EWidgetAnimPointerMeasure::VelocityY:
		Value = Velocity.Y;
		break;
	}
	OutValue = WidgetAnimValue::FromFloat(Value);
	return true;
}

FString FWidgetAnimInput_PointerMotion::GetSummary() const
{
	const UEnum* Enum = StaticEnum<EWidgetAnimPointerMeasure>();
	return FString::Printf(TEXT("Pointer %s"), Enum ? *Enum->GetDisplayNameTextByValue(static_cast<int64>(Measure)).ToString() : TEXT(""));
}

#undef LOCTEXT_NAMESPACE
