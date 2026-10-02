// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimPreset.h"

#include "Components/Image.h"
#include "Components/Widget.h"
#include "NameTBD/Widgets/ShapeBox.h"
#include "WidgetAnimTransform.h"

#if WITH_EDITOR
#include "WidgetAnimBuilder.h"
#endif

#include <atomic>

// =====================================================================================================
// UWidgetAnimPresetAsset
// =====================================================================================================

void UWidgetAnimPresetAsset::PostLoad()
{
	Super::PostLoad();

	// Oldest first: settings stacks become layers, then layers become a group.
	if (AnimIn_DEPRECATED.Num() > 0 || AnimOut_DEPRECATED.Num() > 0)
	{
		MigrateSettingsStacks();
	}

	// Groups made to fit the layers: from before groups, every layer in one group, which animates whatever
	// each use pointed its single Target Widget at - FWidgetAnimEntry carries that target across on its own.
	UWidgetAnimLibrary::NormalizeLayerGroups(LayerGroups, Layers);

	// A value-driven track saved without a name gets its default one, so it can be set.
	UWidgetAnimLibrary::AssignDefaultValueNames(Layers);
}

void UWidgetAnimPresetAsset::MigrateSettingsStacks()
{
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

void UWidgetAnimPresetAsset::ViewAnimationPreview()
{
#if WITH_EDITOR
	UWidgetAnimBuilder::OpenPreviewFor(this);
#endif
}

#if WITH_EDITOR
void UWidgetAnimPresetAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Keys follow the curve on the next tick, once this edit has unwound. See RequestSyncTransformAnims.
	UWidgetAnimTransformLibrary::RequestSyncTransformAnims(this, PropertyChangedEvent);

	// A track just made value-driven, with no name typed, is named after its type - "Scale", "Scale 1" - once the
	// edit is committed, so a name isn't handed out for every step of a drag.
	if (PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		UWidgetAnimLibrary::AssignDefaultValueNames(Layers);
	}

	// Keeps an open builder in step with the asset. Not forced to restart: the builder starts over by itself
	// when the animation's shape changes (a layer added, a type changed, a key added), and otherwise keeps
	// whatever it is showing - still playing, or paused on the key being edited - with the new values in it.
	UWidgetAnimBuilder::NotifyAnimationAssetEdited(this, /*bForceRestart=*/false);

	// Editing a keyframe's values takes any builder previewing this asset straight to that key's time, so
	// the change is seen as it is made rather than whenever the playing clock next passes it.
	const int32 LayerIndex = PropertyChangedEvent.GetArrayIndex(GET_MEMBER_NAME_STRING_CHECKED(UWidgetAnimPresetAsset, Layers));
	const int32 KeyIndex = PropertyChangedEvent.GetArrayIndex(GET_MEMBER_NAME_STRING_CHECKED(FWidgetTransformAnim, Keys));
	if (LayerIndex != INDEX_NONE && KeyIndex != INDEX_NONE)
	{
		UWidgetAnimBuilder::NotifyAnimationAssetKeyEdited(this, LayerIndex, KeyIndex);
	}
}
#endif

// =====================================================================================================
// FWidgetAnimEntryBase
// =====================================================================================================

namespace
{
	/**
	 * Process-unique loop group ids. Unique rather than per-array, so stacks built separately - an
	 * entry list plus extra layers, In plus mirrored Out - can be concatenated without two different
	 * animations ending up on one clock. Masked to stay non-negative, since INDEX_NONE means "ungrouped".
	 */
	int32 AllocateLoopGroup()
	{
		static std::atomic<uint32> NextLoopGroup{ 0 };
		return static_cast<int32>(NextLoopGroup.fetch_add(1, std::memory_order_relaxed) & 0x7FFFFFFFu);
	}
}

void FWidgetAnimEntryBase::BuildSettings(TArray<FWidgetAnimSettings>& OutSettings, UWidget* Target) const
{
	// Every group on the one widget: an entry with nowhere to point each group separately.
	BuildSettings(OutSettings, [Target](int32) { return Target; });
}

void FWidgetAnimEntryBase::BuildSettings(TArray<FWidgetAnimSettings>& OutSettings, TFunctionRef<UWidget*(int32)> GroupTarget) const
{
	if (!Animation)
	{
		return;
	}

	// Placed in order first - one sequence across every group, each layer after the one above it per its
	// Starts setting - so the per-use flags below are stamped onto layers that already sit where they belong.
	TArray<FWidgetAnimSettings> Placed;
	UWidgetAnimLibrary::BuildLayerSettings(Animation->Layers, Placed);

	// After every other setting, so setting N is still layer N: what keeps a pass going after its last track.
	UWidgetAnimLibrary::AddMinimumLengthPlaceholder(Animation, Placed);

	// One widget per group, resolved once each rather than once per layer.
	const int32 NumGroups = Animation->LayerGroups.Num();
	TArray<UWidget*, TInlineAllocator<4>> GroupWidgets;
	GroupWidgets.SetNumZeroed(NumGroups);
	for (int32 GroupIndex = 0; GroupIndex < NumGroups; ++GroupIndex)
	{
		GroupWidgets[GroupIndex] = GroupTarget(GroupIndex);
	}

	OutSettings.Reserve(OutSettings.Num() + Placed.Num());

	// Every layer of this use - every group - shares one clock, so the animation loops as the animation it
	// was authored as, its groups staying in step with each other.
	const int32 LoopGroup = AllocateLoopGroup();

	for (int32 Index = 0; Index < Placed.Num(); ++Index)
	{
		FWidgetAnimSettings& Settings = Placed[Index];

		// A layer of an animation inserted as a track plays in the group its track gives it - see BuildLayerSettings.
		const int32 GroupIndex = Animation->Layers.IsValidIndex(Index)
			? UWidgetAnimLibrary::GetLayerGroupIndex(Animation->Layers[Index], NumGroups)
			: (NumGroups > 0 && Settings.InsertedGroup != INDEX_NONE ? FMath::Clamp(Settings.InsertedGroup, 0, NumGroups - 1) : INDEX_NONE);
		UWidget* Target = GroupWidgets.IsValidIndex(GroupIndex) ? GroupWidgets[GroupIndex] : nullptr;

		Settings.bLoop = bLoop;
		Settings.bPingPong = bPingPong;
		Settings.LoopCount = LoopCount;
		// Or'd, not set: a track inverted on its own stays inverted when the whole animation is (see FWidgetAnimLayer::bInvertTrack).
		Settings.bInvertAnimation = Settings.bInvertAnimation || bInvertAnimation;
		Settings.bMirrorInOut = bMirrorInOut;
		// The animation's own Speed Multiplier, then this use's Playback Speed on top.
		Settings.PlaybackSpeed = PlaybackSpeed * FMath::Max(Animation->AnimationSpeedMultiplier, 0.01f);
		Settings.LoopGroup = LoopGroup;
		Settings.TargetWidget = Target;

		// What its value-driven tracks' values belong to: this animation - those of an animation inserted into it as a
		// track included, set with this one's name as its own are. See FWidgetAnimValueId.
		Settings.ValueScope = Animation;

		// Only meaningful for the two material types, and harmless on the rest: the widget whose material a material track
		// drives is the group's - an Image, a Border, a Text Block, a Retainer Box. Null: the widget it plays on.
		Settings.MaterialTargetWidget = Target;

		// Same idea for a Transform layer's Animate Shape: the group's target, or the Shape Box it sits in.
		// Null when no target was named; the library then resolves the widget the animation is played on.
		Settings.ShapeTargetBox = UWidgetAnimLibrary::ResolveShapeBox(Target);

		OutSettings.Add(MoveTemp(Settings));
	}
}

// =====================================================================================================
// FWidgetAnimEntry
// =====================================================================================================

const FName FWidgetAnimGroupTarget::ThisWidget(TEXT("(This Widget)"));

bool FWidgetAnimEntry::SyncGroupTargets(const TArray<FString>* TargetOptions)
{
	bool bChanged = false;
	const int32 NumGroups = Animation ? Animation->LayerGroups.Num() : 0;

	// Only worth checking a target against choices that list more than (This Widget): with no tree to list yet - a
	// Blueprint still loading - every target would look missing.
	const bool bCanCheck = TargetOptions && TargetOptions->Num() > 1;
	auto IsOffered = [TargetOptions](FName Name)
	{
		const FString NameString = Name.ToString();
		return TargetOptions->ContainsByPredicate([&NameString](const FString& Option) { return Option.Equals(NameString, ESearchCase::IgnoreCase); });
	};

	if (GroupTargets.Num() != NumGroups)
	{
		const int32 OldNum = GroupTargets.Num();
		GroupTargets.SetNum(NumGroups);

		// A new row starts where a target would have come from without it: the single Target Widget this
		// entry had before groups, or else the group's own Preview Target.
		for (int32 GroupIndex = OldNum; GroupIndex < NumGroups; ++GroupIndex)
		{
			FName Start = !TargetWidgetName.IsNone()
				? TargetWidgetName
				: Animation->LayerGroups[GroupIndex].PreviewTarget;

			// A Preview Target this widget's tree doesn't have - the animation was authored in another Widget
			// Blueprint - would animate nothing; the widget the entry is on is the useful place to start.
			if (bCanCheck && !FWidgetAnimGroupTarget::IsThisWidget(Start) && !IsOffered(Start))
			{
				Start = NAME_None;
			}
			GroupTargets[GroupIndex].TargetWidgetName = Start;
		}
		bChanged = true;
	}

	// Shown the way the dropdown offers it.
	if (TargetOptions)
	{
		for (FWidgetAnimGroupTarget& Row : GroupTargets)
		{
			if (Row.TargetWidgetName.IsNone())
			{
				Row.TargetWidgetName = FWidgetAnimGroupTarget::ThisWidget;
				bChanged = true;
			}
		}
	}

	for (int32 GroupIndex = 0; GroupIndex < NumGroups; ++GroupIndex)
	{
		const FString Label = Animation->LayerGroups[GroupIndex].GetDisplayName(GroupIndex);
		if (GroupTargets[GroupIndex].GroupName != Label)
		{
			GroupTargets[GroupIndex].GroupName = Label;
			bChanged = true;
		}
	}

	return bChanged;
}

bool FWidgetAnimEntryBase::HasValueTrack(FName ValueName) const
{
	if (!Animation || ValueName.IsNone())
	{
		return false;
	}

	// The animations inserted into it as tracks too: their value tracks play as part of it.
	bool bFound = false;
	UWidgetAnimLibrary::ForEachLayerIncludingInserted(Animation->Layers, Animation, [ValueName, &bFound](const UWidgetAnimPresetAsset*, const FWidgetAnimLayer& Layer)
	{
		bFound = bFound || (Layer.IsValueDriven() && Layer.ValueName == ValueName);
	});
	return bFound;
}

FName FWidgetAnimEntry::GetGroupTargetName(int32 GroupIndex) const
{
	// The row, whatever it says - None or (This Widget) included, which deliberately mean "the widget this is on".
	if (GroupTargets.IsValidIndex(GroupIndex))
	{
		const FName Row = GroupTargets[GroupIndex].TargetWidgetName;
		return FWidgetAnimGroupTarget::IsThisWidget(Row) ? FName(NAME_None) : Row;
	}

	// A group the animation gained since this entry was last synced: the default its row would start on.
	if (!TargetWidgetName.IsNone())
	{
		return TargetWidgetName;
	}

	return Animation && Animation->LayerGroups.IsValidIndex(GroupIndex)
		? Animation->LayerGroups[GroupIndex].PreviewTarget
		: NAME_None;
}

void FWidgetAnimEntry::BuildSettingsResolved(TArray<FWidgetAnimSettings>& OutSettings, TFunctionRef<UWidget*(FName)> Resolver) const
{
	BuildSettings(OutSettings, [this, &Resolver](int32 GroupIndex)
	{
		return Resolver(GetGroupTargetName(GroupIndex));
	});
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
		Entry.BuildSettingsResolved(OutSettings, Resolver);
	}
}

int32 UWidgetAnimEntryLibrary::StartEntriesById(FWidgetAnimPlayer& Player, const TArray<FWidgetAnimEntry>& Entries, TFunctionRef<UWidget*(FName)> Resolver, const TArray<FName>& AnimIds, bool bIsOut, float PlaybackSpeed, bool bRestartIfPlaying, bool bHoldWhenFinished, const TFunctionRef<bool(const FWidgetAnimEntry&)>* Filter,
	const TFunctionRef<void(int32, TArray<FWidgetAnimSettings>&)>* AppendExtra)
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

		if (Filter && !(*Filter)(Entry))
		{
			continue;
		}

		TArray<FWidgetAnimSettings> Settings;
		Entry.BuildSettingsResolved(Settings, Resolver);
		if (Settings.Num() == 0)
		{
			continue;
		}

		if (AppendExtra)
		{
			(*AppendExtra)(Index, Settings);
		}

		UWidgetAnimLibrary::StartWidgetAnimTrack(Player, Entry.AnimId, Index, Settings, bIsOut, PlaybackSpeed, bRestartIfPlaying, bHoldWhenFinished);
		++Started;
	}

	return Started;
}

int32 UWidgetAnimEntryLibrary::SetEntriesValue(FWidgetAnimPlayer& Player, const TArray<FWidgetAnimEntry>& InEntries, TFunctionRef<UWidget*(FName)> Resolver, const FWidgetAnimValueId& Id, const FVector4& Value, bool bSnap, bool bStartIfNotPlaying,
	const TFunctionRef<void(int32, TArray<FWidgetAnimSettings>&)>* AppendExtra)
{
	if (Id.ValueName.IsNone())
	{
		return 0;
	}

	// An entry's tracks belong to its animation, so the additive tracks alone never start one.
	if (bStartIfNotPlaying && !Id.bAdditiveTracks)
	{
		// Only entries whose animation takes this value, and only those with no track yet - one already started, even
		// one whose clock tracks have finished, keeps its pose rather than playing its clock tracks again.
		auto NeedsStart = [&Player, &InEntries, &Id](const FWidgetAnimEntry& Entry)
		{
			if (!Entry.HasValueTrack(Id.ValueName) || (!Id.IsAnywhere() && Entry.Animation.Get() != Id.Animation))
			{
				return false;
			}

			const int32 EntryIndex = UE_PTRDIFF_TO_INT32(&Entry - InEntries.GetData());
			return !Player.Tracks.ContainsByPredicate([&Entry, EntryIndex](const FWidgetAnimTrack& Track)
			{
				return !Track.bIsOut && Track.SourceIndex == EntryIndex && Track.AnimId == Entry.AnimId;
			});
		};
		const TFunctionRef<bool(const FWidgetAnimEntry&)> Filter(NeedsStart);
		StartEntriesById(Player, InEntries, Resolver, TArray<FName>(), /*bIsOut=*/false, /*PlaybackSpeed=*/1.f, /*bRestartIfPlaying=*/false, /*bHoldWhenFinished=*/true, &Filter, AppendExtra);
	}

	return UWidgetAnimLibrary::SetWidgetAnimTrackValue(Player, Id, Value, bSnap);
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