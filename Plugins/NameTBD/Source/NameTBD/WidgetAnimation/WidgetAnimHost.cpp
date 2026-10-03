// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimHost.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Components/Widget.h"
#include "WidgetAnimationComponent.h"
#include "WidgetAnimPreset.h"

IWidgetAnimHost* UWidgetAnimPlayLibrary::ResolveHost(UObject* Target, bool bSearchParentWidgets)
{
	if (!Target)
	{
		return nullptr;
	}

	if (IWidgetAnimHost* Direct = Cast<IWidgetAnimHost>(Target))
	{
		return Direct;
	}

	if (!bSearchParentWidgets)
	{
		return nullptr;
	}

	// Outwards until something claims it. Widgets nest deeply and a Blueprint graph almost always calls
	// this with Self, so refusing anything but an exact hit would mean wiring an explicit reference in
	// every single case.
	static constexpr int32 MaxSearchDepth = 64;

	UObject* Search = Target->GetOuter();
	for (int32 Depth = 0; Search && Depth < MaxSearchDepth; ++Depth)
	{
		if (IWidgetAnimHost* Host = Cast<IWidgetAnimHost>(Search))
		{
			return Host;
		}

		Search = Search->GetOuter();
	}

	return nullptr;
}

void UWidgetAnimPlayLibrary::PlayWidgetAnimation(UObject* Target, const TArray<FName>& AnimIds, EWidgetAnimPlayDirection Direction, float PlaybackSpeed, bool bHoldFinalPose, bool bRestartIfPlaying, bool bSearchParentWidgets)
{
	IWidgetAnimHost* Host = ResolveHost(Target, bSearchParentWidgets);
	if (!Host)
	{
		return;
	}

	Host->PlayWidgetAnimationsById(
		AnimIds,
		Direction == EWidgetAnimPlayDirection::Reverse,
		FMath::Max(PlaybackSpeed, 0.01f),
		bHoldFinalPose,
		bRestartIfPlaying);
}

void UWidgetAnimPlayLibrary::StopWidgetAnimation(UObject* Target, const TArray<FName>& AnimIds, bool bResetToStart, bool bSearchParentWidgets)
{
	if (IWidgetAnimHost* Host = ResolveHost(Target, bSearchParentWidgets))
	{
		Host->StopWidgetAnimationsById(AnimIds, bResetToStart);
	}
}

bool UWidgetAnimPlayLibrary::IsWidgetAnimationPlaying(UObject* Target, const TArray<FName>& AnimIds, bool bSearchParentWidgets)
{
	const IWidgetAnimHost* Host = ResolveHost(Target, bSearchParentWidgets);
	return Host ? Host->AreWidgetAnimationsPlayingById(AnimIds) : false;
}

bool UWidgetAnimPlayLibrary::ResolveValueOwner(UObject* Target, bool bSearchParentWidgets, UWidgetAnimationComponent*& OutComponent, IWidgetAnimHost*& OutHost)
{
	OutComponent = nullptr;
	OutHost = nullptr;

	if (UWidgetAnimationComponent* Direct = Cast<UWidgetAnimationComponent>(Target))
	{
		OutComponent = Direct;
		return true;
	}

	// A widget's component first, then the widget as a host, one step outwards at a time: most value-driven
	// animations live on a designer-added component, which the play-by-name search never looked for.
	static constexpr int32 MaxSearchDepth = 64;

	UObject* Search = Target;
	for (int32 Depth = 0; Search && Depth < MaxSearchDepth; ++Depth)
	{
		if (const UWidget* Widget = Cast<UWidget>(Search))
		{
			if (UWidgetAnimationComponent* Component = UWidgetAnimationComponent::FindWidgetAnimationComponent(Widget))
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

		if (!bSearchParentWidgets)
		{
			break;
		}

		Search = Search->GetOuter();
	}

	return false;
}

bool UWidgetAnimPlayLibrary::SetValueOn(UObject* Target, const FWidgetAnimValueId& Id, const FVector4& Value, bool bSnap, bool bStartIfNotPlaying, bool bSearchParentWidgets)
{
	UWidgetAnimationComponent* Component = nullptr;
	IWidgetAnimHost* Host = nullptr;
	if (!ResolveValueOwner(Target, bSearchParentWidgets, Component, Host))
	{
		return false;
	}

	return Component
		? Component->SetWidgetAnimationValues(Id, Value, bSnap, bStartIfNotPlaying)
		: Host->SetWidgetAnimationValues(Id, Value, bSnap, bStartIfNotPlaying);
}

FVector4 UWidgetAnimPlayLibrary::GetValueFrom(UObject* Target, const FWidgetAnimValueId& Id, bool bTargetValue, bool bSearchParentWidgets)
{
	UWidgetAnimationComponent* Component = nullptr;
	IWidgetAnimHost* Host = nullptr;
	if (!ResolveValueOwner(Target, bSearchParentWidgets, Component, Host))
	{
		return FVector4(0.0, 0.0, 0.0, 0.0);
	}

	return Component ? Component->GetWidgetAnimationValues(Id, bTargetValue) : Host->GetWidgetAnimationValues(Id, bTargetValue);
}

bool UWidgetAnimPlayLibrary::SetWidgetAnimationValue(UObject* Target, FName ValueName, float Value, bool bSnap, bool bStartIfNotPlaying, bool bSearchParentWidgets)
{
	return SetValueOn(Target, FWidgetAnimValueId::Anywhere(ValueName), WidgetAnimValue::FromFloat(Value), bSnap, bStartIfNotPlaying, bSearchParentWidgets);
}

bool UWidgetAnimPlayLibrary::SetWidgetAnimationValueInteger(UObject* Target, FName ValueName, int32 Value, bool bSnap, bool bStartIfNotPlaying, bool bSearchParentWidgets)
{
	return SetValueOn(Target, FWidgetAnimValueId::Anywhere(ValueName), WidgetAnimValue::FromFloat(static_cast<double>(Value)), bSnap, bStartIfNotPlaying, bSearchParentWidgets);
}

bool UWidgetAnimPlayLibrary::SetWidgetAnimationValueVector2D(UObject* Target, FName ValueName, FVector2D Value, bool bSnap, bool bStartIfNotPlaying, bool bSearchParentWidgets)
{
	return SetValueOn(Target, FWidgetAnimValueId::Anywhere(ValueName), WidgetAnimValue::FromVector2D(Value), bSnap, bStartIfNotPlaying, bSearchParentWidgets);
}

bool UWidgetAnimPlayLibrary::SetWidgetAnimationValueColor(UObject* Target, FName ValueName, FLinearColor Value, bool bSnap, bool bStartIfNotPlaying, bool bSearchParentWidgets)
{
	return SetValueOn(Target, FWidgetAnimValueId::Anywhere(ValueName), WidgetAnimValue::FromColor(Value), bSnap, bStartIfNotPlaying, bSearchParentWidgets);
}

namespace
{
	/** The value the node picked: one animation's, or with none the additive tracks'. */
	FWidgetAnimValueId MakeScopedId(UWidgetAnimPresetAsset* Animation, bool bAdditiveTracks, FName ValueName)
	{
		FWidgetAnimValueId Id = FWidgetAnimValueId::Anywhere(ValueName);
		if (Animation || bAdditiveTracks)
		{
			Id = FWidgetAnimValueId::In(Animation, ValueName);
		}
		return Id;
	}
}

namespace
{
	/**
	 * Calls Visit for Widget's Widget Animation Component and every one nested under it - child widgets, and the trees
	 * of nested User Widgets - in tree order, until Visit returns false. Returns false if it was stopped.
	 */
	bool ForEachComponentOnAndUnder(UWidget* Widget, TFunctionRef<bool(UWidgetAnimationComponent*)> Visit, int32 Depth = 0)
	{
		static constexpr int32 MaxDepth = 128;
		if (!Widget || Depth > MaxDepth)
		{
			return true;
		}

		if (UWidgetAnimationComponent* Component = UWidgetAnimationComponent::FindWidgetAnimationComponent(Widget))
		{
			if (!Visit(Component))
			{
				return false;
			}
		}

		if (const UUserWidget* AsUserWidget = Cast<UUserWidget>(Widget))
		{
			return !AsUserWidget->WidgetTree || ForEachComponentOnAndUnder(AsUserWidget->WidgetTree->RootWidget, Visit, Depth + 1);
		}

		if (const UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		{
			for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
			{
				if (!ForEachComponentOnAndUnder(Panel->GetChildAt(Index), Visit, Depth + 1))
				{
					return false;
				}
			}
		}
		return true;
	}
}

bool UWidgetAnimPlayLibrary::SetScopedValueOn(UObject* Target, const FWidgetAnimValueId& Id, const FVector4& Value, bool bSnap, bool bStartIfNotPlaying, bool bSearchParentWidgets, bool bSetAllMatches)
{
	// The components that have the value, on the target and under it: a node aimed at a screen reaches the widgets in
	// it, and one aimed at a widget in the designer reaches that widget's own component.
	TArray<UWidgetAnimationComponent*> Matches;
	if (UWidgetAnimationComponent* Direct = Cast<UWidgetAnimationComponent>(Target))
	{
		if (Direct->HasWidgetAnimationValue(Id))
		{
			Matches.Add(Direct);
		}
	}
	else if (UWidget* Widget = Cast<UWidget>(Target))
	{
		ForEachComponentOnAndUnder(Widget, [&Matches, &Id, bSetAllMatches](UWidgetAnimationComponent* Component)
		{
			if (Component->HasWidgetAnimationValue(Id))
			{
				Matches.AddUnique(Component);
				return bSetAllMatches;
			}
			return true;
		});
	}

	if (Matches.Num() == 0)
	{
		return SetValueOn(Target, Id, Value, bSnap, bStartIfNotPlaying, bSearchParentWidgets);
	}

	bool bTaken = false;
	for (UWidgetAnimationComponent* Component : Matches)
	{
		bTaken |= Component->SetWidgetAnimationValues(Id, Value, bSnap, bStartIfNotPlaying);
	}
	return bTaken;
}

bool UWidgetAnimPlayLibrary::SetScopedValueFloat(UObject* Target, UWidgetAnimPresetAsset* Animation, bool bAdditiveTracks, FName ValueName, double Value, bool bSnap, bool bStartIfNotPlaying, bool bSearchParentWidgets, bool bSetAllMatches)
{
	return SetScopedValueOn(Target, MakeScopedId(Animation, bAdditiveTracks, ValueName), WidgetAnimValue::FromFloat(Value), bSnap, bStartIfNotPlaying, bSearchParentWidgets, bSetAllMatches);
}

bool UWidgetAnimPlayLibrary::SetScopedValueInteger(UObject* Target, UWidgetAnimPresetAsset* Animation, bool bAdditiveTracks, FName ValueName, int32 Value, bool bSnap, bool bStartIfNotPlaying, bool bSearchParentWidgets, bool bSetAllMatches)
{
	return SetScopedValueOn(Target, MakeScopedId(Animation, bAdditiveTracks, ValueName), WidgetAnimValue::FromFloat(static_cast<double>(Value)), bSnap, bStartIfNotPlaying, bSearchParentWidgets, bSetAllMatches);
}

bool UWidgetAnimPlayLibrary::SetScopedValueVector2D(UObject* Target, UWidgetAnimPresetAsset* Animation, bool bAdditiveTracks, FName ValueName, FVector2D Value, bool bSnap, bool bStartIfNotPlaying, bool bSearchParentWidgets, bool bSetAllMatches)
{
	return SetScopedValueOn(Target, MakeScopedId(Animation, bAdditiveTracks, ValueName), WidgetAnimValue::FromVector2D(Value), bSnap, bStartIfNotPlaying, bSearchParentWidgets, bSetAllMatches);
}

bool UWidgetAnimPlayLibrary::SetScopedValueColor(UObject* Target, UWidgetAnimPresetAsset* Animation, bool bAdditiveTracks, FName ValueName, FLinearColor Value, bool bSnap, bool bStartIfNotPlaying, bool bSearchParentWidgets, bool bSetAllMatches)
{
	return SetScopedValueOn(Target, MakeScopedId(Animation, bAdditiveTracks, ValueName), WidgetAnimValue::FromColor(Value), bSnap, bStartIfNotPlaying, bSearchParentWidgets, bSetAllMatches);
}

bool UWidgetAnimPlayLibrary::SetWidgetAnimationProgress(UObject* Target, FName AnimId, float Progress, bool bSnap, bool bStartIfNotPlaying, bool bSearchParentWidgets)
{
	// The component on or under Target that plays a percentage animation with this Anim ID - a screen holding the widget
	// with it reaches it - else outwards from Target, as Set Widget Animation Value goes.
	return SetScopedValueOn(Target, FWidgetAnimValueId::Progress(AnimId), WidgetAnimValue::FromFloat(Progress), bSnap, bStartIfNotPlaying, bSearchParentWidgets, /*bSetAllMatches=*/false);
}

float UWidgetAnimPlayLibrary::GetWidgetAnimationProgress(UObject* Target, FName AnimId, bool bTargetProgress, bool bSearchParentWidgets)
{
	return static_cast<float>(GetValueFrom(Target, FWidgetAnimValueId::Progress(AnimId), bTargetProgress, bSearchParentWidgets).X);
}

namespace
{
	/** The progress of the entries playing Animation - every percentage animation's with none. */
	FWidgetAnimValueId MakeProgressId(const UWidgetAnimPresetAsset* Animation)
	{
		return Animation ? FWidgetAnimValueId::In(Animation, WidgetAnimValue::ProgressValueName) : FWidgetAnimValueId::Progress(NAME_None);
	}
}

bool UWidgetAnimPlayLibrary::SetScopedProgress(UObject* Target, UWidgetAnimPresetAsset* Animation, double Progress, bool bSnap, bool bStartIfNotPlaying, bool bSearchParentWidgets, bool bSetAllMatches)
{
	return SetScopedValueOn(Target, MakeProgressId(Animation), WidgetAnimValue::FromFloat(Progress), bSnap, bStartIfNotPlaying, bSearchParentWidgets, bSetAllMatches);
}

float UWidgetAnimPlayLibrary::GetWidgetAnimationProgressOf(UObject* Target, UWidgetAnimPresetAsset* Animation, bool bTargetProgress, bool bSearchParentWidgets)
{
	return static_cast<float>(GetValueFrom(Target, MakeProgressId(Animation), bTargetProgress, bSearchParentWidgets).X);
}

float UWidgetAnimPlayLibrary::GetWidgetAnimationValue(UObject* Target, FName ValueName, bool bTargetValue, bool bSearchParentWidgets)
{
	return static_cast<float>(GetValueFrom(Target, FWidgetAnimValueId::Anywhere(ValueName), bTargetValue, bSearchParentWidgets).X);
}

int32 UWidgetAnimPlayLibrary::GetWidgetAnimationValueInteger(UObject* Target, FName ValueName, bool bTargetValue, bool bSearchParentWidgets)
{
	return FMath::RoundToInt32(GetValueFrom(Target, FWidgetAnimValueId::Anywhere(ValueName), bTargetValue, bSearchParentWidgets).X);
}

FVector2D UWidgetAnimPlayLibrary::GetWidgetAnimationValueVector2D(UObject* Target, FName ValueName, bool bTargetValue, bool bSearchParentWidgets)
{
	const FVector4 Value = GetValueFrom(Target, FWidgetAnimValueId::Anywhere(ValueName), bTargetValue, bSearchParentWidgets);
	return FVector2D(Value.X, Value.Y);
}

FLinearColor UWidgetAnimPlayLibrary::GetWidgetAnimationValueColor(UObject* Target, FName ValueName, bool bTargetValue, bool bSearchParentWidgets)
{
	const FVector4 Value = GetValueFrom(Target, FWidgetAnimValueId::Anywhere(ValueName), bTargetValue, bSearchParentWidgets);
	return FLinearColor(static_cast<float>(Value.X), static_cast<float>(Value.Y), static_cast<float>(Value.Z), static_cast<float>(Value.W));
}
