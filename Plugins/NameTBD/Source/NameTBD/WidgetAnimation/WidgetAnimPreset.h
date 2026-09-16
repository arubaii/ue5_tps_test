// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "WidgetAnimLibrary.h"

#include "WidgetAnimPreset.generated.h"

class UImage;
class UWidget;

/**
 * A reusable animation, authored as a stack of layers.
 *
 * The asset holds MOTION and nothing else: how long, how far, what curve. Everything that is a decision
 * about one particular use of it - does this one loop, is it mirrored, which widget does it move - lives
 * on the FWidgetAnimEntry that references it, so the same asset can pop a menu open in one place and
 * loop as an idle nudge in another without needing two copies.
 *
 * Layers combine rather than overwrite. Opacity and scale multiply, translation and angle add, so "pop"
 * is a Fade plus a Scale plus a small Slide sitting on top of each other, with Delay staggering them.
 * That is why this is a list and not a single type.
 *
 * PREVIEWING ONE
 * Drop a Widget Anim Preview Box into any Widget Blueprint, add an entry pointing at this asset and set
 * Preview Mode to Loop In and Out. It plays in the UMG designer, no compile and no PIE. The box is a
 * plain UBorder underneath, so size and colour it however you like.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Widget Animation"))
class NAMETBD_API UWidgetAnimPresetAsset : public UDataAsset
{
	GENERATED_BODY()

public:

	/** What this animation is for, in words. Shows up as the tooltip wherever the asset is picked. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (MultiLine = true))
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (TitleProperty = "AnimType"))
	TArray<FWidgetAnimLayer> Layers;

	virtual void PostLoad() override;

private:

	/**
	 * Authored before animations were split into layers and per-use entries. Migrated into Layers on
	 * load and emptied, so re-saving the asset drops them. Both halves are taken: an exit stack is a
	 * perfectly good set of layers, and which direction it runs in is now the entry's decision.
	 */
	UPROPERTY()
	TArray<FWidgetAnimSettings> AnimIn_DEPRECATED;

	UPROPERTY()
	TArray<FWidgetAnimSettings> AnimOut_DEPRECATED;
};

/**
 * One use of an animation, with nowhere to point it.
 *
 * Deliberately carries no widget target, so it is safe on a Data Asset and anywhere else outside a Widget
 * Blueprint - a widget dropdown in those places would have no tree to list. Use FWidgetAnimEntry where
 * there is one.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimEntryBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UWidgetAnimPresetAsset> Animation = nullptr;

	/**
	 * Optional name for this animation on this widget, for the Play Widget Animation node.
	 *
	 * Leave it empty for anything that only ever plays as part of the widget's own entrance or exit. Give
	 * it one - "Press", "Alert", "Refresh" - for anything you want to fire from Blueprint by name.
	 * Several entries may share an ID, in which case they all start and stop together, which is how you
	 * build one named effect out of animations on three different child widgets.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (DisplayName = "Anim ID"))
	FName AnimId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	bool bLoop = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (EditCondition = "bLoop", EditConditionHides))
	bool bPingPong = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (EditCondition = "bLoop", EditConditionHides, ClampMin = "0", ToolTip = "0 loops forever."))
	int32 LoopCount = 0;

	/** Also plays as the other half of the pair, backwards. An entrance authored once becomes its own exit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (DisplayName = "Mirror In Out"))
	bool bMirrorInOut = false;

	/** 
	 * Runs this use of the animation backwards. Independent of Mirror In Out, which is about the other stack. 
	 * 
	 * Out animations already invert internally.  
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (DisplayName = "Invert Animation"))
	bool bInvertAnimation = false;

	/**
	 * How fast this use of the animation runs. 2 is twice as fast, 0.5 is half speed.
	 *
	 * The asset holds the motion; this is the per-use decision about it, which is why it lives here and
	 * not on the layers. Every layer of the referenced animation is scaled by the same factor, delays
	 * included, so a "pop" authored as a fade at 0s plus a scale at 0.05s still lands in that order with
	 * the same proportions at any speed.
	 *
	 * Stamped onto each layer as it is built, so it applies wherever the entry is used - the entrance,
	 * the mirrored exit, and a named Play Widget Animation call alike. The Playback Speed argument on
	 * that node multiplies on top of this one rather than replacing it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (DisplayName = "Playback Speed", ClampMin = "0.01", UIMin = "0.1", UIMax = "4.0"))
	float PlaybackSpeed = 1.f;

	bool IsUsable() const { return Animation != nullptr && Animation->Layers.Num() > 0; }

	/**
	 * Every layer of the referenced animation as runtime settings, with this entry's flags stamped onto
	 * each of them and both targets set to the widget the caller resolved.
	 */
	void BuildSettings(TArray<FWidgetAnimSettings>& OutSettings, UWidget* Target) const;
};

/**
 * An animation entry with somewhere to point it. Use this inside a Widget Blueprint.
 *
 * Target Widget is a path rather than an object pointer on purpose: a name gives the searchable UE5
 * dropdown rather than the asset-picker row an object property gets, and it survives the widget tree
 * being rebuilt, which happens constantly in the designer.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimEntry : public FWidgetAnimEntryBase
{
	GENERATED_BODY()

	/**
	 * Which widget this animates. Empty animates whatever the entry is attached to.
	 *
	 * Material Scalar / Material Vector layers need this to name an Image; any other widget type and
	 * those layers do nothing (the rest of the animation still plays).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (DisplayName = "Target Widget", GetOptions = "GetAnimTargetWidgetOptions"))
	FName TargetWidgetName;
};

UCLASS()
class NAMETBD_API UWidgetAnimEntryLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/**
	 * Appends Source to Destination, copying every entry marked Mirror In Out into MirrorDestination too.
	 * The one place the mirroring rule lives, so cards, panels, buttons and the preview box all agree.
	 */
	static void AppendEntriesWithMirroring(const TArray<FWidgetAnimEntry>& Source, TArray<FWidgetAnimEntry>& Destination, TArray<FWidgetAnimEntry>& MirrorDestination);

	/** The FWidgetAnimEntryBase version, for anything authored outside a Widget Blueprint. */
	static void AppendEntriesWithMirroring(const TArray<FWidgetAnimEntryBase>& Source, TArray<FWidgetAnimEntryBase>& Destination, TArray<FWidgetAnimEntryBase>& MirrorDestination);

	/** Flattens a list of entries into one settings stack, resolving each entry's target through Resolver. */
	static void FlattenEntries(const TArray<FWidgetAnimEntry>& Entries, TFunctionRef<UWidget*(FName)> Resolver, TArray<FWidgetAnimSettings>& OutSettings);

	/**
	 * Starts every entry whose Anim ID matches, on the given player. An empty ID list starts all of them.
	 *
	 * Shared by every animation host so "play by name" means the same thing on a card, a panel and a
	 * button. Returns how many tracks were started, which is how a caller can warn about an ID nothing
	 * matched rather than silently doing nothing.
	 */
	static int32 StartEntriesById(FWidgetAnimPlayer& Player, const TArray<FWidgetAnimEntry>& Entries, TFunctionRef<UWidget*(FName)> Resolver, const TArray<FName>& AnimIds, bool bIsOut, float PlaybackSpeed, bool bRestartIfPlaying, bool bHoldWhenFinished);

	/** True if any entry points at an animation that actually has layers. */
	UFUNCTION(BlueprintPure, Category = "Widget Animation")
	static bool HasAnyUsableAnimation(const TArray<FWidgetAnimEntryBase>& Entries);
};