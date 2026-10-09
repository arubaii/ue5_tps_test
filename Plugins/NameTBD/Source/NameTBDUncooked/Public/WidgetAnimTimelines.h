// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "WidgetBlueprintExtension.h"

#include "WidgetAnimTimelines.generated.h"

class UWidgetAnimPresetAsset;
class UWidgetAnimStateMachineAsset;
class UWidgetBlueprint;

/**
 * One animation timeline: a name, and the animation itself - groups, tracks, speed and minimum length, exactly as a
 * Widget Animation asset holds them.
 */
USTRUCT()
struct NAMETBDUNCOOKED_API FWidgetAnimTimeline
{
	GENERATED_BODY()

	/** What it is called in the Widget Animations panel, unique within its Widget Blueprint. */
	UPROPERTY()
	FString Name;

	/**
	 * The animation. A Widget Animation object kept inside the Widget Blueprint rather than an asset of its own - nothing
	 * outside this Widget Blueprint can pick it - so that everything that edits and plays an asset edits and plays it too.
	 */
	UPROPERTY()
	TObjectPtr<UWidgetAnimPresetAsset> Animation;

	/**
	 * The Widget Animation asset this timeline was last saved as, from the Widget Animations panel's Save Animation To - so
	 * saving it again updates that asset rather than asking where to make another. None until it has been saved once, and
	 * once that asset is gone.
	 */
	UPROPERTY()
	TSoftObjectPtr<UWidgetAnimPresetAsset> SavedAsset;
};

/**
 * The animation timelines of one Widget Blueprint: animations authored in its Widget Animations panel against its own
 * widgets, and saved with it.
 *
 * Why an extension of the Widget Blueprint: a timeline belongs to the Widget Blueprint it was made in - its groups name
 * that Blueprint's widgets - so it is saved, duplicated, undone and dirtied with it, without being an asset anything
 * else could pick. Moving one to another widget is Save as Animation Asset, which makes a Widget Animation asset of it.
 *
 * Editor data only: it lives on the Widget Blueprint, not its generated class, so nothing of it is cooked. Kept in this
 * UncookedOnly module so a Widget Blueprint holding timelines still loads cleanly wherever the editor's own modules
 * aren't loaded (a commandlet, the cooker).
 */
UCLASS()
class NAMETBDUNCOOKED_API UWidgetAnimTimelinesExtension : public UWidgetBlueprintExtension
{
	GENERATED_BODY()

public:

	/** In the order the panel lists them. */
	UPROPERTY()
	TArray<FWidgetAnimTimeline> Timelines;

	/**
	 * The Widget Blueprint's own state machine - the State Machine editor's State Machine mode, as Animation Timeline is the
	 * Widget Animations panel's: made the first time that mode is opened here, kept and saved with the Widget Blueprint, and
	 * made into an asset any widget can play with Save State Machine To. A Widget Animation State Machine object inside the
	 * Widget Blueprint, so everything that edits and previews an asset edits and previews it too. Editor data, never cooked.
	 */
	UPROPERTY()
	TObjectPtr<UWidgetAnimStateMachineAsset> StateMachine;

	/** The asset StateMachine was last saved as (Save State Machine To) - Save puts it there again. None until saved once. */
	UPROPERTY()
	TSoftObjectPtr<UWidgetAnimStateMachineAsset> SavedStateMachine;

	/**
	 * What StateMachine is called in the State Machine editor - renamed by double-clicking it on the editor's bar, or in its
	 * Details. Empty: the default, "Card State Machine" for WBP_Card. Save State Machine To names the asset after it.
	 */
	UPROPERTY()
	FString StateMachineName;

	/** The Widget Blueprint's timelines, or null when it has never had one. Never makes one. */
	static UWidgetAnimTimelinesExtension* Find(const UWidgetBlueprint* WidgetBlueprint);

	/**
	 * The Widget Blueprint's timelines, made when it has none - inside the caller's transaction, so undoing it takes the
	 * extension back off. Null without a Widget Blueprint.
	 */
	static UWidgetAnimTimelinesExtension* FindOrAdd(UWidgetBlueprint* WidgetBlueprint);

	/** The index of the timeline called Name (case ignored), or INDEX_NONE. */
	int32 IndexOf(const FString& Name) const;

	/** The index of the timeline playing Animation, or INDEX_NONE. */
	int32 IndexOf(const UWidgetAnimPresetAsset* Animation) const;

	/** Base, or Base numbered ("Intro 2") until no timeline is called that. Ignore is a timeline that may keep its name. */
	FString MakeUniqueName(const FString& Base, int32 Ignore = INDEX_NONE) const;

	/** The name a timeline gets when none is typed: "Timeline 1", "Timeline 2"... the lowest number no timeline has. */
	FString MakeDefaultName() const;

	/**
	 * Adds a timeline called a unique version of Name: empty with one group, or a copy of CopyFrom's animation. The caller
	 * has a transaction open and has called Modify on this. Returns its index.
	 */
	int32 AddTimeline(const FString& Name, const UWidgetAnimPresetAsset* CopyFrom = nullptr);
};
