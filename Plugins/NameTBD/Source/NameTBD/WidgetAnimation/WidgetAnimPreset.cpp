// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimPreset.h"

#include "Components/Image.h"
#include "Components/Widget.h"

// =====================================================================================================
// UWidgetAnimPresetAsset
// =====================================================================================================

void UWidgetAnimPresetAsset::PostLoad()
{
	Super::PostLoad();

	if (AnimIn_DEPRECATED.Num() == 0 && AnimOut_DEPRECATED.Num() == 0)
	{
		return;
	}

	// Both halves, in order. An exit stack is a perfectly good set of layers - which direction it runs in
	// is the entry's decision now, not the asset's - so nothing is thrown away here.
	Layers.Reserve(Layers.Num() + AnimIn_DEPRECATED.Num() + AnimOut_DEPRECATED.Num());

	for (const FWidgetAnimSettings& Settings : AnimIn_DEPRECATED)
	{
		Layers.Add(FWidgetAnimLayer::FromSettings(Settings));
	}

	for (const FWidgetAnimSettings& Settings : AnimOut_DEPRECATED)
	{
		Layers.Add(FWidgetAnimLayer::FromSettings(Settings));
	}

	AnimIn_DEPRECATED.Reset();
	AnimOut_DEPRECATED.Reset();
}

// =====================================================================================================
// FWidgetAnimEntryBase
// =====================================================================================================

void FWidgetAnimEntryBase::BuildSettings(TArray<FWidgetAnimSettings>& OutSettings, UWidget* Target) const
{
	if (!Animation)
	{
		return;
	}

	// Only meaningful for the two material types, and harmless on the rest: the library ignores it unless
	// the layer is a material layer. One picker instead of two is the whole reason it's derived here.
	UImage* MaterialTarget = Cast<UImage>(Target);

	OutSettings.Reserve(OutSettings.Num() + Animation->Layers.Num());

	for (const FWidgetAnimLayer& Layer : Animation->Layers)
	{
		FWidgetAnimSettings Settings = Layer.ToSettings();

		Settings.bLoop = bLoop;
		Settings.bPingPong = bPingPong;
		Settings.LoopCount = LoopCount;
		Settings.bInvertAnimation = bInvertAnimation;
		Settings.bMirrorInOut = bMirrorInOut;
		Settings.TargetWidget = Target;
		Settings.MaterialTargetImage = MaterialTarget;

		OutSettings.Add(MoveTemp(Settings));
	}
}

// =====================================================================================================
// UWidgetAnimEntryLibrary
// =====================================================================================================

void UWidgetAnimEntryLibrary::AppendEntriesWithMirroring(const TArray<FWidgetAnimEntry>& Source, TArray<FWidgetAnimEntry>& Destination, TArray<FWidgetAnimEntry>& MirrorDestination)
{
	Destination.Reserve(Destination.Num() + Source.Num());

	for (const FWidgetAnimEntry& Entry : Source)
	{
		Destination.Add(Entry);

		// A straight copy. Direction is decided when the track starts, so the mirrored copy is the same
		// entry - it just runs backwards when it plays as the other half of the pair.
		if (Entry.bMirrorInOut)
		{
			MirrorDestination.Add(Entry);
		}
	}
}

void UWidgetAnimEntryLibrary::AppendEntriesWithMirroring(const TArray<FWidgetAnimEntryBase>& Source, TArray<FWidgetAnimEntryBase>& Destination, TArray<FWidgetAnimEntryBase>& MirrorDestination)
{
	Destination.Reserve(Destination.Num() + Source.Num());

	for (const FWidgetAnimEntryBase& Entry : Source)
	{
		Destination.Add(Entry);

		if (Entry.bMirrorInOut)
		{
			MirrorDestination.Add(Entry);
		}
	}
}

void UWidgetAnimEntryLibrary::FlattenEntries(const TArray<FWidgetAnimEntry>& Entries, TFunctionRef<UWidget*(FName)> Resolver, TArray<FWidgetAnimSettings>& OutSettings)
{
	for (const FWidgetAnimEntry& Entry : Entries)
	{
		Entry.BuildSettings(OutSettings, Resolver(Entry.TargetWidgetName));
	}
}

int32 UWidgetAnimEntryLibrary::StartEntriesById(FWidgetAnimPlayer& Player, const TArray<FWidgetAnimEntry>& Entries, TFunctionRef<UWidget*(FName)> Resolver, const TArray<FName>& AnimIds, bool bIsOut, float PlaybackSpeed, bool bRestartIfPlaying, bool bHoldWhenFinished)
{
	const bool bAll = AnimIds.Num() == 0;
	int32 Started = 0;

	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const FWidgetAnimEntry& Entry = Entries[Index];
		if (!Entry.IsUsable())
		{
			continue;
		}

		// Several entries may share an ID on purpose - that is how one named effect is built out of
		// animations on three different child widgets - so this doesn't stop at the first match.
		if (!bAll && !AnimIds.Contains(Entry.AnimId))
		{
			continue;
		}

		TArray<FWidgetAnimSettings> Settings;
		Entry.BuildSettings(Settings, Resolver(Entry.TargetWidgetName));
		if (Settings.Num() == 0)
		{
			continue;
		}

		UWidgetAnimLibrary::StartWidgetAnimTrack(Player, Entry.AnimId, Index, Settings, bIsOut, PlaybackSpeed, bRestartIfPlaying, bHoldWhenFinished);
		++Started;
	}

	return Started;
}

bool UWidgetAnimEntryLibrary::HasAnyUsableAnimation(const TArray<FWidgetAnimEntryBase>& Entries)
{
	for (const FWidgetAnimEntryBase& Entry : Entries)
	{
		if (Entry.IsUsable())
		{
			return true;
		}
	}

	return false;
}