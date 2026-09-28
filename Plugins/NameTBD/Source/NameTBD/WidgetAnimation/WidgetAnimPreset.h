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
 * Press View Animation Preview at the top of this asset. It opens the WidgetAnimationBuilder Widget
 * Blueprint playing this animation.
 *
 * EDITING ONE
 * In any Widget Blueprint - the builder included - open the Widget Animations drawer, choose Edit Existing
 * Asset, and pick it: it is edited in place against that widget's own widgets, and Update Animation Asset
 * saves it.
 *
 * MAKING ONE
 * In any Widget Blueprint: open the Widget Animations drawer on the status bar, leave it on New Animation,
 * build it against that Widget Blueprint's own widgets - each Layer Group animating the widget it is pointed
 * at - and press Save as Animation Asset. For something generic meant for many places, do the same in
 * WidgetAnimationBuilder, whose Animation Name and Save Folder decide where it is saved.
 *
 * LAYER GROUPS
 * Every layer belongs to one Layer Group, one group per widget the animation moves. The layers are still
 * one list in one sequence, so layers of different groups follow each other freely. Each use of the
 * animation picks a Target Widget per group - see FWidgetAnimEntry - so the asset stays reusable.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Widget Animation", PrioritizeCategories = "Preview"))
class NAMETBD_API UWidgetAnimPresetAsset : public UDataAsset
{
	GENERATED_BODY()

public:

	/** What this animation is for, in words. Shows up as the tooltip wherever the asset is picked. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (MultiLine = true))
	FText Description;

	/** The widgets this animation moves, one group each: a name and the widget it plays on while authored. See FWidgetAnimLayerGroup. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (TitleProperty = "GroupName"))
	TArray<FWidgetAnimLayerGroup> LayerGroups;

	/**
	 * The animation: one list of layers, each in one of the Layer Groups. Order is sequence - a layer's Starts
	 * reads the layer above it here, whichever group that is - so groups interleave freely.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (TitleProperty = "AnimType"))
	TArray<FWidgetAnimLayer> Layers;

	/**
	 * How fast the whole animation plays, wherever it is used: 2 plays every track - its delay, length and keys
	 * - in half the time, 0.5 takes twice as long. Part of the animation itself, unlike an entry's Playback
	 * Speed, which is one use's choice and multiplies on top of this.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (DisplayName = "Animation Speed Multiplier", ClampMin = "0.01", UIMin = "0.1", UIMax = "4.0"))
	float AnimationSpeedMultiplier = 1.f;

	int32 GetNumLayers() const { return Layers.Num(); }

	/**
	 * Opens WidgetAnimationBuilder playing this animation. Its Widget Animations panel opens on Edit Existing
	 * Asset with this asset picked, so it can be changed there too - as it can in any Widget Blueprint's panel.
	 *
	 * The widget blueprint 'WidgetAnimationBuilder' (exact naming) must exist somewhere in the project,
	 * otherwise this button does nothing. To create it if you deleted it:
	 *   1. Create a widget blueprint parented to 'WidgetAnimBuilder'. It can go anywhere in your project.
	 *   2. Add an image widget and name it 'PreviewImage'.
	 *   3. Choose whatever image you like (T_FivePointStar set to a gold color looks good for preview).
	 *   4. Optional, for previewing Animate Shape on Transform layers: wrap 'PreviewImage' in a Shape Box.
	 *      The Image keeps getting the render transform; the Shape Box around it takes the outline.
	 */
	UFUNCTION(CallInEditor, Category = "Preview", meta = (DisplayName = "View Animation Preview"))
	void ViewAnimationPreview();

	virtual void PostLoad() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:

	/** AnimIn / AnimOut from before layers, into Layers. */
	void MigrateSettingsStacks();

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
	 * build one named effect out of animations on three different child widgets. Two entries with the same
	 * ID on the SAME widget going the same direction are a conflict, and are reported as one at runtime.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (DisplayName = "Anim ID"))
	FName AnimId;

	/**
	 * Repeats this animation. All of its layers loop together as one animation, so the stagger between
	 * them plays again every loop. A delay every layer shares is only waited out the first time.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	bool bLoop = false;

	/** Plays back to the start between plays instead of snapping there. Always comes to rest on the end pose. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (EditCondition = "bLoop", EditConditionHides))
	bool bPingPong = true;

	/**
	 * How many times the animation plays through. 0 loops forever.
	 *
	 * With Ping Pong it plays back between plays, so 2 is forward, back, forward. Whatever the count, it
	 * finishes on its end pose.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (EditCondition = "bLoop", EditConditionHides, ClampMin = "0"))
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

	bool IsUsable() const { return Animation != nullptr && Animation->GetNumLayers() > 0; }

	/** Whether this entry's animation has a value-driven track with this Value Name. See WidgetAnimDriver.h. */
	bool HasValueTrack(FName ValueName) const;

	/**
	 * Every layer of the referenced animation as runtime settings, with this entry's flags stamped onto
	 * each of them and every group's targets set to the one widget the caller resolved.
	 */
	void BuildSettings(TArray<FWidgetAnimSettings>& OutSettings, UWidget* Target) const;

	/**
	 * The same, with a widget per Layer Group: GroupTarget(GroupIndex) answers the widget that group's layers
	 * animate, or null for the widget the animation is played on.
	 */
	void BuildSettings(TArray<FWidgetAnimSettings>& OutSettings, TFunctionRef<UWidget*(int32 /*GroupIndex*/)> GroupTarget) const;
};

/** Which widget one Layer Group of an entry's animation animates. */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimGroupTarget
{
	GENERATED_BODY()

	/** The group's name in the animation, kept here as the row's label. Refreshed whenever the entry is edited or loaded. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation", meta = (DisplayName = "Layer Group"))
	FString GroupName;

	/**
	 * Which widget this group animates. None animates whatever the entry is attached to.
	 *
	 * Material Scalar / Material Vector layers need this to name an Image, and a Transform layer's Shape
	 * channel needs it to name a Shape Box (or a widget inside one); any other widget type and those
	 * particular layers do nothing - the rest of the group still plays.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (DisplayName = "Target Widget", GetOptions = "GetAnimTargetWidgetOptions"))
	FName TargetWidgetName;
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
	 * A Target Widget for each Layer Group of the animation, in the animation's order and labelled with its
	 * group names. Sized to the animation by the owner whenever the entry is edited or loaded - see
	 * SyncGroupTargets - so it always lists exactly the groups there are. A new row starts on the group's
	 * Preview Target.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (DisplayName = "Target Widgets", EditFixedSize, TitleProperty = "GroupName"))
	TArray<FWidgetAnimGroupTarget> GroupTargets;

	/**
	 * Plays this animation when the widget it is on is constructed. Only on a Widget Animation Component's Anim In,
	 * and ignored while the component's Play Only On Construct And Destruct is on, which plays every entry.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (DisplayName = "Play In Animation On Construct", EditCondition = "bShowConstructTrigger", EditConditionHides, HideEditConditionToggle))
	bool bPlayInAnimationOnConstruct = true;

	/**
	 * Plays this animation when the widget's visibility changes. In Anim In: whenever the widget goes from hidden or
	 * collapsed to visible. In Anim Out: before the widget is hidden with Set Visibility Animated, which then hides it
	 * once the exit has played. Only on a Widget Animation Component's entries.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (DisplayName = "Play Animation On Visibility Hidden", EditCondition = "bShowVisibilityTrigger", EditConditionHides, HideEditConditionToggle))
	bool bPlayAnimationOnVisibilityHidden = false;

	/**
	 * Whether the two settings above are shown. Set by the Widget Animation Component on its own entries, on load and
	 * on every edit; every other host - a button, a card - plays its entries its own way and leaves them hidden.
	 * Saved rather than transient so a Blueprint recompile, which copies entries without their transient values,
	 * doesn't hide them until the next edit.
	 */
	UPROPERTY()
	bool bShowConstructTrigger = false;

	UPROPERTY()
	bool bShowVisibilityTrigger = false;

	/**
	 * The single Target Widget from before Layer Groups. No longer edited; kept so an entry saved with one
	 * carries it into every group it has not been given a target for yet.
	 */
	UPROPERTY()
	FName TargetWidgetName;

	/** Sizes GroupTargets to the animation's groups and refreshes their labels. True if anything changed. */
	bool SyncGroupTargets();

	/** The target path group GroupIndex animates: its row if it has one, else the default a new row would get. */
	FName GetGroupTargetName(int32 GroupIndex) const;

	/** BuildSettings with each group's target path resolved by Resolver. */
	void BuildSettingsResolved(TArray<FWidgetAnimSettings>& OutSettings, TFunctionRef<UWidget*(FName)> Resolver) const;
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
	 *
	 * Filter, when given, skips every entry it returns false for - how a host plays only the entries a given trigger
	 * (construction, becoming visible) is set for. Skipped entries keep their index, so a track's source index is
	 * always the entry's position in Entries.
	 *
	 * AppendExtra, when given, may add settings of its own to an entry's before its track starts - how a component's
	 * Additive Anim Layers play as part of the entry they start within.
	 */
	static int32 StartEntriesById(FWidgetAnimPlayer& Player, const TArray<FWidgetAnimEntry>& Entries, TFunctionRef<UWidget*(FName)> Resolver, const TArray<FName>& AnimIds, bool bIsOut, float PlaybackSpeed, bool bRestartIfPlaying, bool bHoldWhenFinished, const TFunctionRef<bool(const FWidgetAnimEntry&)>* Filter = nullptr,
		const TFunctionRef<void(int32 /*EntryIndex*/, TArray<FWidgetAnimSettings>& /*EntrySettings*/)>* AppendExtra = nullptr);

	/**
	 * Sets the value of every value-driven track with this Value Name (see WidgetAnimDriver.h) - the one call behind
	 * every host's Set Widget Animation Value, so a card, a button and a component all mean the same by it. Value is
	 * raw, up to four channels.
	 *
	 * With bStartIfNotPlaying, the Anim In entries (InEntries) whose animation has a track with this name, and that
	 * haven't been started yet, are started first - their clock tracks play as usual, their value tracks start at their
	 * Start Value (or the last value set) and travel to the new one - so gameplay needs no Play call before its first
	 * value. An entry already started is never restarted by it. Returns how many running tracks took the value.
	 */
	static int32 SetEntriesValue(FWidgetAnimPlayer& Player, const TArray<FWidgetAnimEntry>& InEntries, TFunctionRef<UWidget*(FName)> Resolver, FName ValueName, const FVector4& Value, bool bSnap, bool bStartIfNotPlaying,
		const TFunctionRef<void(int32 /*EntryIndex*/, TArray<FWidgetAnimSettings>& /*EntrySettings*/)>* AppendExtra = nullptr);

	/** True if any entry points at an animation that actually has layers. */
	UFUNCTION(BlueprintPure, Category = "Widget Animation")
	static bool HasAnyUsableAnimation(const TArray<FWidgetAnimEntryBase>& Entries);
};