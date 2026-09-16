// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimHost.h"

#include "Components/Widget.h"

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