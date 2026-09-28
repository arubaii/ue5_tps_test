// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimHost.h"

#include "Components/Widget.h"
#include "WidgetAnimationComponent.h"

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

bool UWidgetAnimPlayLibrary::SetValueOn(UObject* Target, FName ValueName, const FVector4& Value, bool bSnap, bool bStartIfNotPlaying, bool bSearchParentWidgets)
{
	UWidgetAnimationComponent* Component = nullptr;
	IWidgetAnimHost* Host = nullptr;
	if (!ResolveValueOwner(Target, bSearchParentWidgets, Component, Host))
	{
		return false;
	}

	return Component
		? Component->SetWidgetAnimationValues(ValueName, Value, bSnap, bStartIfNotPlaying)
		: Host->SetWidgetAnimationValues(ValueName, Value, bSnap, bStartIfNotPlaying);
}

FVector4 UWidgetAnimPlayLibrary::GetValueFrom(UObject* Target, FName ValueName, bool bTargetValue, bool bSearchParentWidgets)
{
	UWidgetAnimationComponent* Component = nullptr;
	IWidgetAnimHost* Host = nullptr;
	if (!ResolveValueOwner(Target, bSearchParentWidgets, Component, Host))
	{
		return FVector4(0.0, 0.0, 0.0, 0.0);
	}

	return Component ? Component->GetWidgetAnimationValues(ValueName, bTargetValue) : Host->GetWidgetAnimationValues(ValueName, bTargetValue);
}

bool UWidgetAnimPlayLibrary::SetWidgetAnimationValue(UObject* Target, FName ValueName, float Value, bool bSnap, bool bStartIfNotPlaying, bool bSearchParentWidgets)
{
	return SetValueOn(Target, ValueName, WidgetAnimValue::FromFloat(Value), bSnap, bStartIfNotPlaying, bSearchParentWidgets);
}

bool UWidgetAnimPlayLibrary::SetWidgetAnimationValueInteger(UObject* Target, FName ValueName, int32 Value, bool bSnap, bool bStartIfNotPlaying, bool bSearchParentWidgets)
{
	return SetValueOn(Target, ValueName, WidgetAnimValue::FromFloat(static_cast<double>(Value)), bSnap, bStartIfNotPlaying, bSearchParentWidgets);
}

bool UWidgetAnimPlayLibrary::SetWidgetAnimationValueVector2D(UObject* Target, FName ValueName, FVector2D Value, bool bSnap, bool bStartIfNotPlaying, bool bSearchParentWidgets)
{
	return SetValueOn(Target, ValueName, WidgetAnimValue::FromVector2D(Value), bSnap, bStartIfNotPlaying, bSearchParentWidgets);
}

bool UWidgetAnimPlayLibrary::SetWidgetAnimationValueColor(UObject* Target, FName ValueName, FLinearColor Value, bool bSnap, bool bStartIfNotPlaying, bool bSearchParentWidgets)
{
	return SetValueOn(Target, ValueName, WidgetAnimValue::FromColor(Value), bSnap, bStartIfNotPlaying, bSearchParentWidgets);
}

float UWidgetAnimPlayLibrary::GetWidgetAnimationValue(UObject* Target, FName ValueName, bool bTargetValue, bool bSearchParentWidgets)
{
	return static_cast<float>(GetValueFrom(Target, ValueName, bTargetValue, bSearchParentWidgets).X);
}

int32 UWidgetAnimPlayLibrary::GetWidgetAnimationValueInteger(UObject* Target, FName ValueName, bool bTargetValue, bool bSearchParentWidgets)
{
	return FMath::RoundToInt32(GetValueFrom(Target, ValueName, bTargetValue, bSearchParentWidgets).X);
}

FVector2D UWidgetAnimPlayLibrary::GetWidgetAnimationValueVector2D(UObject* Target, FName ValueName, bool bTargetValue, bool bSearchParentWidgets)
{
	const FVector4 Value = GetValueFrom(Target, ValueName, bTargetValue, bSearchParentWidgets);
	return FVector2D(Value.X, Value.Y);
}

FLinearColor UWidgetAnimPlayLibrary::GetWidgetAnimationValueColor(UObject* Target, FName ValueName, bool bTargetValue, bool bSearchParentWidgets)
{
	const FVector4 Value = GetValueFrom(Target, ValueName, bTargetValue, bSearchParentWidgets);
	return FLinearColor(static_cast<float>(Value.X), static_cast<float>(Value.Y), static_cast<float>(Value.Z), static_cast<float>(Value.W));
}