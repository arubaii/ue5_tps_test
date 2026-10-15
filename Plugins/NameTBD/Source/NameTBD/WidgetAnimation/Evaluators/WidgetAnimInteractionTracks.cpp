// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimInteractionTracks.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "NameTBD/Widgets/Constraints/WidgetConstraintComponent.h"

#define LOCTEXT_NAMESPACE "WidgetAnimInteractionTracks"

DEFINE_LOG_CATEGORY_STATIC(LogWidgetAnimInteraction, Log, All);

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
	return LOCTEXT("ConstraintDescription", "Animates any number of the widget's Constraints - a Follow Path's Path Position (a ball sent round a ring), a constraint's Strength, a Distance, an Offset, a Copy Amount, a Radius... (Value). On the Track Group's widget, or the first widget inside it with Constraints. To key a place for constraints to follow (where eyes look), key a Constraint Target widget's Transform instead.");
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
	if (!Settings->ValueName.IsNone())
	{
		return FName(*FString::Printf(TEXT("Constraint.%s.%s"), *Settings->ConstraintName.ToString(), *Settings->ValueName.ToString()));
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
	if (!Component || !Settings)
	{
		return;
	}
	if (!Component->SetTrackValue(Settings->ConstraintName, Settings->ValueName, Settings->Channel, static_cast<float>(Value)))
	{
		// Said once per widget and value: a track writing nothing looked like a broken track.
		static TSet<FString> Told;
		const UWidget* OwnerWidget = Component->GetConstrainedWidget();
		const FString What = Settings->ValueName.IsNone()
			? StaticEnum<EWidgetConstraintChannel>()->GetNameStringByValue(static_cast<int64>(Settings->Channel))
			: Settings->ValueName.ToString();
		const FString Key = FString::Printf(TEXT("%s|%s|%s"), *GetPathNameSafe(OwnerWidget), *Settings->ConstraintName.ToString(), *What);
		if (!Told.Contains(Key))
		{
			Told.Add(Key);
			UE_LOG(LogWidgetAnimInteraction, Warning, TEXT("Constraint track on %s: %s. Constraints there: %s."), *GetNameSafe(OwnerWidget),
				Component->FindConstraint(Settings->ConstraintName)
					? *FString::Printf(TEXT("constraint '%s' has no value '%s'"), *Settings->ConstraintName.ToString(), *What)
					: *FString::Printf(TEXT("no constraint named '%s'"), *Settings->ConstraintName.ToString()),
				*FString::JoinBy(Component->GetConstraintNames(), TEXT(", "), [](const FName& Name) { return Name.ToString(); }));
		}
	}
}

bool FWidgetAnimConstraintEvaluator::CaptureBaseline(UObject* Target, const FInstancedStruct& Params, FInstancedStruct& OutBaseline) const
{
	const UWidgetConstraintComponent* Component = Cast<UWidgetConstraintComponent>(Target);
	const FWidgetAnimConstraintParams* Settings = GetParams(Params);
	FWidgetAnimInteractionBaseline Baseline;
	if (!Component || !Settings || !Component->GetTrackValue(Settings->ConstraintName, Settings->ValueName, Settings->Channel, Baseline.Value))
	{
		return false;
	}
	Baseline.ConstraintName = Settings->ConstraintName;
	Baseline.ValueName = Settings->ValueName;
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
		Component->SetTrackValue(Captured->ConstraintName, Captured->ValueName, static_cast<EWidgetConstraintChannel>(Captured->Channel), Captured->Value);
	}
}

FText FWidgetAnimConstraintEvaluator::GetTrackSummary(const FInstancedStruct& Params) const
{
	const FWidgetAnimConstraintParams* Settings = GetParams(Params);
	if (!Settings)
	{
		return FText::GetEmpty();
	}
	const FText Animated = Settings->ValueName.IsNone()
		? WidgetAnimInteractionPrivate::ChannelName(StaticEnum<EWidgetConstraintChannel>(), static_cast<int64>(Settings->Channel))
		: FText::FromName(Settings->ValueName);
	return Settings->ConstraintName.IsNone() ? Animated : FText::Format(LOCTEXT("ConstraintSummary", "{0} {1}"), FText::FromName(Settings->ConstraintName), Animated);
}

void FWidgetAnimConstraintEvaluator::InitializeNewTrack(FInstancedStruct& Params, UWidget* Widget) const
{
	FWidgetAnimConstraintParams* Settings = Params.GetMutablePtr<FWidgetAnimConstraintParams>();
	const UWidgetConstraintComponent* Component = Cast<UWidgetConstraintComponent>(ResolveTarget(Widget, Params));
	const UWidgetConstraint* First = Component ? Component->FindConstraint(NAME_None) : nullptr;
	if (!Settings || !First)
	{
		// Nothing to read: a Strength fade, as before.
		return;
	}

	// The constraint's main number, from where it is now: a lap of a path, a leash, a turn - else its Strength, faded in.
	static const FName PathPositionName(TEXT("Path Position"));
	static const FName DistanceName(TEXT("Distance"));
	static const FName StrengthName(TEXT("Strength"));
	FName Main = StrengthName;
	double Span = 0.0;
	if (First->IsA<UWidgetFollowPathConstraint>())
	{
		const UWidgetFollowPathConstraint* Path = CastChecked<UWidgetFollowPathConstraint>(First);
		Main = PathPositionName;
		Span = Path->bPositionInPixels ? 100.0 : 1.0;
	}
	else if (First->IsA<UWidgetDistanceConstraint>())
	{
		Main = DistanceName;
		Span = 40.0;
	}

	float Now = 0.f;
	Settings->ValueName = Main;
	if (Main == StrengthName || !WidgetConstraintValues::Get(First, Main, Now))
	{
		Settings->StartValue = 0.0;
		Settings->EndValue = 1.0;
		return;
	}
	Settings->StartValue = Now;
	Settings->EndValue = Now + Span;
}

// =====================================================================================================
// Pointer Effects
// =====================================================================================================

namespace WidgetAnimInteractionPrivate
{
	/** The part of the pointer a Pointer Effects track animates, or false for a setting. */
	bool ToPointerPart(EWidgetAnimPointerEffectsValue Animates, EPointerEffectsInput& OutInput)
	{
		switch (Animates)
		{
		case EWidgetAnimPointerEffectsValue::PointerX: OutInput = EPointerEffectsInput::PointerX; return true;
		case EWidgetAnimPointerEffectsValue::PointerY: OutInput = EPointerEffectsInput::PointerY; return true;
		case EWidgetAnimPointerEffectsValue::Hover: OutInput = EPointerEffectsInput::Hover; return true;
		case EWidgetAnimPointerEffectsValue::Press: OutInput = EPointerEffectsInput::Press; return true;
		default: return false;
		}
	}
}

FText FWidgetAnimPointerEffectsEvaluator::GetDisplayName() const
{
	return LOCTEXT("PointerEffectsName", "Pointer Effects");
}

FText FWidgetAnimPointerEffectsEvaluator::GetDescription() const
{
	return LOCTEXT("PointerEffectsDescription", "Animates the pointer the widget's Pointer Effects see - Pointer X / Y across it, Hover, Press (a scripted click with its ripple) - so a sheen sweeps a card as it is dealt or a selection shows hovered with no one at the mouse; or a setting: Intensity, Tilt Angle, Magnet Pull, Light Strength. On the Track Group's widget, or the first widget inside it with Pointer Effects. For a keyed 2D path, point Pointer Source at a Constraint Target and key its Transform.");
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
	if (!Settings)
	{
		return NAME_None;
	}
	EPointerEffectsInput Input = EPointerEffectsInput::PointerX;
	if (WidgetAnimInteractionPrivate::ToPointerPart(Settings->Animates, Input))
	{
		return FName(*FString::Printf(TEXT("PointerEffects.Input.%d"), static_cast<int32>(Input)));
	}
	return FName(*FString::Printf(TEXT("PointerEffects.%d"), static_cast<int32>(Settings->Channel)));
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
	if (!Component || !Settings)
	{
		return;
	}
	EPointerEffectsInput Input = EPointerEffectsInput::PointerX;
	if (WidgetAnimInteractionPrivate::ToPointerPart(Settings->Animates, Input))
	{
		Component->SetAnimatedInput(Input, static_cast<float>(Value));
	}
	else
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
	EPointerEffectsInput Input = EPointerEffectsInput::PointerX;
	if (WidgetAnimInteractionPrivate::ToPointerPart(Settings->Animates, Input))
	{
		// Usually not animated before: put back as following the real pointer again.
		Baseline.PointerPart = static_cast<uint8>(Settings->Animates);
		Baseline.bWasSet = Component->GetAnimatedInput(Input, Baseline.Value);
	}
	else
	{
		Baseline.Value = Component->GetEffectValue(Settings->Channel);
	}
	Baseline.Channel = static_cast<uint8>(Settings->Channel);
	OutBaseline.InitializeAs<FWidgetAnimInteractionBaseline>(Baseline);
	return true;
}

void FWidgetAnimPointerEffectsEvaluator::RestoreBaseline(UObject* Target, const FInstancedStruct& Baseline) const
{
	UPointerEffectsComponent* Component = Cast<UPointerEffectsComponent>(Target);
	const FWidgetAnimInteractionBaseline* Captured = Baseline.GetPtr<FWidgetAnimInteractionBaseline>();
	if (!Component || !Captured)
	{
		return;
	}
	EPointerEffectsInput Input = EPointerEffectsInput::PointerX;
	if (WidgetAnimInteractionPrivate::ToPointerPart(static_cast<EWidgetAnimPointerEffectsValue>(Captured->PointerPart), Input))
	{
		if (Captured->bWasSet)
		{
			Component->SetAnimatedInput(Input, Captured->Value);
		}
		else
		{
			Component->ClearAnimatedInput(Input);
		}
		return;
	}
	Component->SetEffectValue(static_cast<EPointerEffectsChannel>(Captured->Channel), Captured->Value);
}

FText FWidgetAnimPointerEffectsEvaluator::GetTrackSummary(const FInstancedStruct& Params) const
{
	const FWidgetAnimPointerEffectsParams* Settings = GetParams(Params);
	if (!Settings)
	{
		return FText::GetEmpty();
	}
	if (Settings->Animates != EWidgetAnimPointerEffectsValue::Setting)
	{
		return WidgetAnimInteractionPrivate::ChannelName(StaticEnum<EWidgetAnimPointerEffectsValue>(), static_cast<int64>(Settings->Animates));
	}
	return WidgetAnimInteractionPrivate::ChannelName(StaticEnum<EPointerEffectsChannel>(), static_cast<int64>(Settings->Channel));
}

void FWidgetAnimPointerEffectsEvaluator::InitializeNewTrack(FInstancedStruct& Params, UWidget* Widget) const
{
	// A new track sweeps the pointer across: left edge to right edge, the effects shown as it passes.
	if (FWidgetAnimPointerEffectsParams* Settings = Params.GetMutablePtr<FWidgetAnimPointerEffectsParams>())
	{
		Settings->Animates = EWidgetAnimPointerEffectsValue::PointerX;
		Settings->StartValue = 0.0;
		Settings->EndValue = 1.0;
	}
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
