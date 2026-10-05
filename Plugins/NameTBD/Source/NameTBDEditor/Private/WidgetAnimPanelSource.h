// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "UObject/StrongObjectPtr.h"

#include "NameTBD/WidgetAnimation/WidgetAnimLibrary.h"

class UUserWidget;
class UWidget;
class UWidgetAnimBuilder;
class UWidgetAnimPresetAsset;
class UWidgetBlueprint;
class IPropertyHandle;

/**
 * What the Widget Animations panel edits, and how it plays it.
 *
 * Two kinds, one panel:
 *
 *  - FWidgetAnimBuilderPanelSource, in a WidgetAnimationBuilder's editor: the builder's own working copy on
 *    its Class Defaults, saved as a new WA_ asset - or an existing asset, edited in place. The place for
 *    generic animations meant for many widgets.
 *
 *  - FWidgetAnimAssetPanelSource, in every other Widget Blueprint's editor: a new animation, or a WA_ asset
 *    edited directly, each Layer Group playing on a widget of this Widget Blueprint - its Preview Target -
 *    in the designer. The place for an animation that moves several parts of one sophisticated widget.
 *
 * The panel only talks to this interface, so layers, groups, keys, the transport and the Selection panel
 * behave the same in both.
 */
class IWidgetAnimPanelSource : public TSharedFromThis<IWidgetAnimPanelSource>
{
public:

	virtual ~IWidgetAnimPanelSource() = default;

	// ---------------------------------------------------------------------------------------------
	// The animation
	// ---------------------------------------------------------------------------------------------

	/** The object holding the LayerGroups being edited - Modify()'d for every edit, and what the Selection panel shows. Null when there is none. */
	virtual UObject* GetEditedObject() const = 0;

	/** The groups on screen. Editable ones, or - for a builder previewing an asset - that asset's, read-only. */
	virtual TArray<FWidgetAnimLayerGroup>* GetGroups() const = 0;

	/** The layers on screen, in sequence, each naming its group. From the same object as GetGroups. */
	virtual TArray<FWidgetAnimLayer>* GetLayers() const = 0;

	/** Whether the groups on screen can be edited here. Says nothing of the panel's lock - see IsReadOnly. */
	virtual bool CanEdit() const = 0;

	/**
	 * The panel's lock: the animation is shown and played but not edited - a read-only preview. Kept here rather
	 * than on the panel, so the drawer and the docked tab of one editor are always locked or unlocked together.
	 */
	bool IsReadOnly() const { return bReadOnly; }
	void SetReadOnly(bool bInReadOnly) { bReadOnly = bInReadOnly; }

	/** After an edit made outside the property system: keys brought in step with curves, previews refreshed. */
	virtual void NotifyGroupsChanged(EPropertyChangeType::Type ChangeType) = 0;

	/**
	 * Names of the LayerGroups and Layers properties on GetEditedObject's class - what the Selection panel binds its
	 * rows to. The same on a builder and an asset; a source editing some other object's list of tracks names that
	 * list, and None for groups it doesn't keep.
	 */
	virtual FName GetGroupsPropertyName() const { return TEXT("LayerGroups"); }
	virtual FName GetLayersPropertyName() const { return TEXT("Layers"); }

	/**
	 * Whether the tracks can be split between Layer Groups - each moving a widget of its own. A plain list of tracks,
	 * such as a component's Additive Anim Layers, plays on the one widget it belongs to: it has a single group,
	 * which the panel neither shows nor lets a track leave.
	 */
	virtual bool SupportsLayerGroups() const { return true; }

	/**
	 * Whether each track names the widget it moves itself - FWidgetAnimLayer::TargetWidget - as the tracks of a plain
	 * list do, where there are no groups to say it. The Selection panel then shows a Target Widget row for a track,
	 * offering GetLayerTargetOptions.
	 */
	virtual bool SupportsLayerTargets() const { return false; }
	virtual TArray<FString> GetLayerTargetOptions() const { return GetTargetOptions(); }

	// ---------------------------------------------------------------------------------------------
	// Where the tracks start - and what they play within
	// ---------------------------------------------------------------------------------------------

	/** One choice of where a list starts: right away (Id INDEX_NONE), or within one of the animations the source offers. */
	struct FStartOption
	{
		FText Label;
		FText ToolTip;
		int32 Id = INDEX_NONE;
	};

	/** Whether the list can start within another animation - a component's Additive Anim Layers within one of its entries. */
	virtual bool SupportsStartOptions() const { return false; }
	virtual void GetStartOptions(TArray<FStartOption>& OutOptions) const {}

	/** The Id of the option now in force: INDEX_NONE for right away. Setting it is one undoable edit. */
	virtual int32 GetStartOption() const { return INDEX_NONE; }
	virtual void SetStartOption(int32 Id) {}

	/**
	 * The tracks of the animation the list starts within, shown read-only above the list's own, grayed, so the list's
	 * tracks can be placed against it. GetContextTimeScale turns their authored seconds into the list's - how much
	 * faster than authored that animation plays. Null when the list starts right away.
	 */
	virtual const TArray<FWidgetAnimLayer>* GetContextLayers() const { return nullptr; }
	virtual float GetContextTimeScale() const { return 1.f; }

	// ---------------------------------------------------------------------------------------------
	// The widget it plays on
	// ---------------------------------------------------------------------------------------------

	/** The designer's preview of this Widget Blueprint, whose tree Preview Targets are paths into. Null while there is none. */
	virtual UUserWidget* GetPreviewRoot() const = 0;

	/** Every widget a group can target, as paths, "None" first. */
	TArray<FString> GetTargetOptions() const;

	/**
	 * The Widget Blueprint whose designer the preview is in - whose editor's selection picks a target by clicking a widget in
	 * the designer. Null where there is none to pick from.
	 */
	virtual UWidgetBlueprint* GetWidgetBlueprint() const { return nullptr; }

	/**
	 * The target option - one of GetTargetOptions, "None" for the preview itself - that names Widget, a widget of the
	 * designer's preview. Empty when none does.
	 */
	FString FindTargetOptionFor(const UWidget* Widget) const;

	/** The widget a group's Preview Target resolves to on the preview, for labeling. Null for "the widget itself". */
	UWidget* ResolvePreviewTarget(FName Path) const;

	/**
	 * The widget of the designer's preview that the track at LayerIndex (of GetLayers) plays on - whose material a material
	 * track drives - for checking the track against it: the Material dropdown's parameters, the warnings when the widget has no
	 * such material. By default its group's Preview Target, or the preview itself for None. Null while there is no preview.
	 */
	virtual UWidget* GetLayerPreviewWidget(int32 LayerIndex) const;

	// ---------------------------------------------------------------------------------------------
	// Playing it
	// ---------------------------------------------------------------------------------------------

	/** Where the preview is, as a fraction of one pass. */
	virtual float GetPlayheadAlpha() const = 0;

	/** Freeze the preview at Alpha of one pass. */
	virtual void ScrubToAlpha(float Alpha) = 0;

	/** Freeze the preview on key KeyIndex of flattened layer FlatLayerIndex (groups in order, then layers). */
	virtual void FocusKey(int32 FlatLayerIndex, int32 KeyIndex) = 0;

	/**
	 * How many items the Stagger of flattened layer FlatLayerIndex fans out to on the preview - so the Track Editor can
	 * draw how far the layer reaches and place the layers after it where the preview plays them. INDEX_NONE when it has
	 * no Stagger, or there is no preview to count on.
	 */
	virtual int32 GetExpandedCount(int32 FlatLayerIndex) const { return INDEX_NONE; }

	/**
	 * How long flattened layer FlatLayerIndex runs on the preview when its length comes from its widget rather than its
	 * Duration - a text typed at a Typing Speed, a count at so many a second (see FWidgetAnimEvaluator::HasDerivedLength).
	 * Negative when its Duration is its length, or there is no preview to work it out on.
	 */
	virtual float GetPreparedRun(int32 FlatLayerIndex) const { return -1.f; }

	virtual bool IsPlaying() const = 0;

	/** Play forward, first frame to last - or freeze, if playing. */
	virtual void TogglePlay() = 0;

	/**
	 * Play backward from where it is - the end, when it is at the very start - to the first frame: what Play does after Go
	 * To End. False when the source can't, and the panel walks the playhead back itself instead, scrubbing - which poses
	 * the same frames but fires no cues.
	 */
	virtual bool PlayBackward() { return false; }

	virtual bool GetLoop() const = 0;
	virtual void SetLoop(bool bValue) = 0;
	virtual bool GetPingPong() const = 0;
	virtual void SetPingPong(bool bValue) = 0;
	virtual bool GetInvert() const = 0;
	virtual void SetInvert(bool bValue) = 0;
	virtual float GetSpeed() const = 0;
	virtual void SetSpeed(float Speed) = 0;

	/** Called by the panel every frame it is on screen. A source that drives the preview itself uses it to know it's wanted. */
	virtual void NotifyPanelActive() {}

	// ---------------------------------------------------------------------------------------------
	// An animation timeline, or an existing asset
	// ---------------------------------------------------------------------------------------------

	/**
	 * What the panel is working on. Animation Timeline is an animation kept in the Widget Blueprint itself - one of its
	 * timelines (see SupportsTimelines), or the builder's own layers - saved with the Widget Blueprint, and made into a
	 * WA_ asset with Save as Animation Asset. Edit Existing Asset edits the picked WA_ asset in place; Update Animation
	 * Asset saves it.
	 */
	enum class EMode : uint8
	{
		Timeline,
		EditExistingAsset
	};

	virtual EMode GetMode() const = 0;
	virtual void SetMode(EMode Mode) = 0;

	/** The asset being edited in Edit Existing Asset - null until one is picked, and in Animation Timeline. */
	virtual UWidgetAnimPresetAsset* GetAsset() const = 0;
	virtual void SetAsset(UWidgetAnimPresetAsset* Asset) = 0;

	/**
	 * Animation Timeline: saves it as a new WA_ asset. The builder then goes on to edit the asset; a Widget Blueprint
	 * keeps its timeline open and as it was, so its own work isn't moved out from under it. Null if nothing was saved.
	 */
	virtual UWidgetAnimPresetAsset* SaveAsNewAsset() = 0;

	// ---------------------------------------------------------------------------------------------
	// Timelines: a Widget Blueprint's own animations (UWidgetAnimTimelinesExtension)
	// ---------------------------------------------------------------------------------------------

	/** Whether this source keeps several named timelines, made, renamed and deleted from the panel. Not the builder, or a plain list. */
	virtual bool SupportsTimelines() const { return false; }

	/** The timelines' names, in order. */
	virtual void GetTimelineNames(TArray<FString>& OutNames) const {}

	/** The timeline open in Animation Timeline - empty when there is none. The builder answers with its Animation Name. */
	virtual FString GetTimelineName() const { return FString(); }

	/** Opens the timeline called Name, in Animation Timeline. False if there is none. */
	virtual bool OpenTimeline(const FString& Name) { return false; }

	/**
	 * Makes a timeline, named Name (numbered when taken), or with no Name the next of "Timeline 1", "Timeline 2"... - a copy
	 * of the one called CopyOf when given - and opens it. One undo step. Returns its name, or empty if none was made.
	 */
	virtual FString CreateTimeline(const FString& Name, const FString& CopyOf = FString()) { return FString(); }

	/** Renames a timeline (numbered when taken). One undo step. Returns its new name, or empty if it wasn't renamed. */
	virtual FString RenameTimeline(const FString& Name, const FString& NewName) { return FString(); }

	/** Deletes a timeline and its animation. One undo step; an open one is replaced by the next. */
	virtual bool DeleteTimeline(const FString& Name) { return false; }

	/** Whether the panel is in Animation Timeline on a timeline of a Widget Blueprint (not the builder, not a plain list). */
	bool IsEditingTimeline() const { return SupportsTimelines() && GetMode() == EMode::Timeline; }

	/** What is being edited, named for the panel's and the Script area's headers: the timeline's name, the asset's, the builder's. */
	FText GetEditedName() const;

	/** Edit Existing Asset: saves the asset's changes to disk. */
	void UpdateAsset();

	/**
	 * The edited animation's Animation Speed Multiplier - how much faster than authored it plays wherever it is
	 * used. The panel shows time as played, so everything on the timeline is divided by it.
	 */
	float GetAnimationSpeedMultiplier() const;

	/**
	 * The edited animation's Minimum Length (UWidgetAnimPresetAsset::MinimumLength), in seconds as authored: how long
	 * one pass runs at the least, whatever its tracks. 0 for none, and for tracks that aren't an animation (a component's).
	 */
	float GetMinimumLength() const;

	/** Whether the edited tracks are an animation that keeps a Minimum Length: an asset's, or the builder's. */
	bool CanSetMinimumLength() const;

	/** Sets the Minimum Length, in seconds as authored. The caller has called Modify on GetEditedObject already. */
	void SetMinimumLength(float Seconds);

	/**
	 * The edited animation's Time In - Seconds, or Percent for one that follows a progress value (see EWidgetAnimTimeUnit).
	 * Seconds for tracks that aren't an animation (a component's).
	 */
	EWidgetAnimTimeUnit GetTimeUnit() const;

	/** Whether the edited tracks are an animation whose Time In can be changed: an asset's, or the builder's. */
	bool CanSetTimeUnit() const;

	/**
	 * Changes the Time In, converting the tracks to it (UWidgetAnimLibrary::ConvertTimeUnit). The caller has called Modify
	 * on GetEditedObject already, and notifies the change.
	 */
	void SetTimeUnit(EWidgetAnimTimeUnit Unit);

	/** Whether there is something to save as a new asset, or unsaved changes to update the asset with. */
	bool CanSaveAsNewAsset() const;
	bool CanUpdateAsset() const;

	/**
	 * A source whose tracks can be handed over to an entry using them - a component's Additive Anim Layers becoming
	 * an Anim In entry - says so here, and what that is called on the button.
	 */
	virtual bool CanReplaceWithEntry() const { return false; }
	virtual FText GetReplaceWithEntryLabel() const { return FText::GetEmpty(); }

	/** Saves the tracks as a new WA_ asset, as SaveAsNewAsset does, then replaces them with an entry playing it. Null if nothing was saved. */
	virtual UWidgetAnimPresetAsset* SaveAndReplaceWithEntry() { return nullptr; }

	virtual bool IsBuilder() const { return false; }
	virtual UWidgetAnimBuilder* GetBuilder() const { return nullptr; }

private:

	bool bReadOnly = false;
};

/**
 * The designer's preview of a Widget Blueprint: a live instance of exactly its class, at design time, that is
 * not sitting inside some other widget's tree. Looking is a walk over every User Widget, so callers cache it.
 */
UUserWidget* FindWidgetAnimDesignerPreview(const UWidgetBlueprint* Blueprint);

/**
 * In a WidgetAnimationBuilder's editor: the builder's working copy - or, while its Preview Animation is set,
 * that asset, edited in place - played by the builder's own designer preview. Opening a WA_ asset's Edit in
 * Widget Animation Builder lands here, editing it.
 */
class FWidgetAnimBuilderPanelSource : public IWidgetAnimPanelSource
{
public:

	explicit FWidgetAnimBuilderPanelSource(TWeakObjectPtr<UWidgetBlueprint> InBlueprint);

	virtual UObject* GetEditedObject() const override;
	virtual TArray<FWidgetAnimLayerGroup>* GetGroups() const override;
	virtual TArray<FWidgetAnimLayer>* GetLayers() const override;
	virtual bool CanEdit() const override;
	virtual void NotifyGroupsChanged(EPropertyChangeType::Type ChangeType) override;
	virtual UUserWidget* GetPreviewRoot() const override;

	virtual float GetPlayheadAlpha() const override;
	virtual void ScrubToAlpha(float Alpha) override;
	virtual void FocusKey(int32 FlatLayerIndex, int32 KeyIndex) override;
	virtual int32 GetExpandedCount(int32 FlatLayerIndex) const override;
	virtual float GetPreparedRun(int32 FlatLayerIndex) const override;
	virtual bool IsPlaying() const override;
	virtual void TogglePlay() override;
	virtual bool GetLoop() const override;
	virtual void SetLoop(bool bValue) override;
	virtual bool GetPingPong() const override;
	virtual void SetPingPong(bool bValue) override;
	virtual bool GetInvert() const override;
	virtual void SetInvert(bool bValue) override;
	virtual float GetSpeed() const override;
	virtual void SetSpeed(float Speed) override;

	virtual EMode GetMode() const override;
	virtual void SetMode(EMode Mode) override;

	/** The asset being edited: the builder's Preview Animation, which View Animation Preview on an asset also sets. */
	virtual UWidgetAnimPresetAsset* GetAsset() const override;
	virtual void SetAsset(UWidgetAnimPresetAsset* Asset) override;

	/** The builder's Create: Animation Name and Save Folder decide where it goes. */
	virtual UWidgetAnimPresetAsset* SaveAsNewAsset() override;

	virtual bool IsBuilder() const override { return true; }
	virtual UWidgetAnimBuilder* GetBuilder() const override;
	virtual UWidgetBlueprint* GetWidgetBlueprint() const override { return Blueprint.Get(); }

private:

	UClass* GetBuilderClass() const;

	/** Sets the builder's Preview Animation - the asset being edited, or none - and tells every live preview. */
	void SetPreviewAnimation(UWidgetAnimPresetAsset* Asset);

	/** Edit Existing Asset picked, before an asset has been. The builder only records the asset itself. */
	bool bEditExistingPicked = false;

	/** Through the live previews when there are any - their setters write the Class Defaults too - or straight onto the Class Defaults. */
	void ApplyToBuilders(TFunctionRef<void(UWidgetAnimBuilder&)> Apply);

	TWeakObjectPtr<UWidgetBlueprint> Blueprint;
};

/**
 * In any other Widget Blueprint's editor: one of the Widget Blueprint's own animation timelines - kept in it and saved
 * with it (UWidgetAnimTimelinesExtension), made into a WA_ asset with Save as Animation Asset - or an existing WA_ asset
 * edited directly. Either is played on the designer's preview of this Widget Blueprint with each group on its Preview
 * Target.
 *
 * Which timeline or asset each Widget Blueprint had open is remembered across editor sessions (per project, in the
 * editor's own settings - never in the Widget Blueprint, so looking at a timeline doesn't mark it changed).
 *
 * Plays it itself - there is no builder here to do it. Every frame the panel is on screen, the animation is
 * rebuilt from the asset and posed at the playhead; when the panel goes away (the drawer closed, the tab
 * hidden) every widget it moved is put back as authored within a few frames, so nothing is left posed
 * while the widget is being designed.
 */
class FWidgetAnimAssetPanelSource : public IWidgetAnimPanelSource
{
public:

	explicit FWidgetAnimAssetPanelSource(TWeakObjectPtr<UWidgetBlueprint> InBlueprint);
	virtual ~FWidgetAnimAssetPanelSource() override;

	virtual UObject* GetEditedObject() const override;
	virtual TArray<FWidgetAnimLayerGroup>* GetGroups() const override;
	virtual TArray<FWidgetAnimLayer>* GetLayers() const override;
	virtual bool CanEdit() const override;
	virtual void NotifyGroupsChanged(EPropertyChangeType::Type ChangeType) override;
	virtual UUserWidget* GetPreviewRoot() const override;

	virtual float GetPlayheadAlpha() const override;
	virtual void ScrubToAlpha(float Alpha) override;
	virtual void FocusKey(int32 FlatLayerIndex, int32 KeyIndex) override;
	virtual int32 GetExpandedCount(int32 FlatLayerIndex) const override { return ExpandedCounts.IsValidIndex(FlatLayerIndex) ? ExpandedCounts[FlatLayerIndex] : INDEX_NONE; }
	virtual float GetPreparedRun(int32 FlatLayerIndex) const override { return PreparedRuns.IsValidIndex(FlatLayerIndex) ? PreparedRuns[FlatLayerIndex] : -1.f; }
	virtual bool IsPlaying() const override { return bPlaying; }
	virtual void TogglePlay() override;
	virtual bool PlayBackward() override;
	virtual bool GetLoop() const override { return bLoop; }
	virtual void SetLoop(bool bValue) override { bLoop = bValue; }
	virtual bool GetPingPong() const override { return bPingPong; }
	virtual void SetPingPong(bool bValue) override { bPingPong = bValue; }
	virtual bool GetInvert() const override { return bInvert; }
	virtual void SetInvert(bool bValue) override { bInvert = bValue; }
	virtual float GetSpeed() const override { return Speed; }
	virtual void SetSpeed(float InSpeed) override { Speed = FMath::Max(InSpeed, 0.01f); }

	virtual void NotifyPanelActive() override;

	virtual EMode GetMode() const override { return Mode; }
	virtual void SetMode(EMode InMode) override;
	virtual UWidgetAnimPresetAsset* GetAsset() const override { return Mode == EMode::EditExistingAsset ? Asset.Get() : nullptr; }
	virtual void SetAsset(UWidgetAnimPresetAsset* InAsset) override;
	virtual UWidgetAnimPresetAsset* SaveAsNewAsset() override;
	virtual UWidgetBlueprint* GetWidgetBlueprint() const override { return Blueprint.Get(); }

	virtual bool SupportsTimelines() const override { return true; }
	virtual void GetTimelineNames(TArray<FString>& OutNames) const override;
	virtual FString GetTimelineName() const override;
	virtual bool OpenTimeline(const FString& Name) override;
	virtual FString CreateTimeline(const FString& Name, const FString& CopyOf = FString()) override;
	virtual FString RenameTimeline(const FString& Name, const FString& NewName) override;
	virtual bool DeleteTimeline(const FString& Name) override;

private:

	/** The ticker: advances the clock and poses the preview while the panel is showing, restores it once it isn't. */
	bool Tick(float DeltaTime);

	/** The animation being worked on: the open timeline in Animation Timeline, the picked asset in Edit Existing Asset. */
	UWidgetAnimPresetAsset* GetEditedAnimation() const;

	/**
	 * The open timeline's animation: the one opened last while it is still among the Widget Blueprint's timelines (an undo
	 * may have taken it away), else the one by its name, else the first. Null when the Widget Blueprint has none.
	 */
	UWidgetAnimPresetAsset* GetOpenTimeline() const;

	/** What is shown changes: the poses come off and the clock starts over. */
	void ResetPlayback();

	/** Remembers which timeline or asset is open, for the next time this Widget Blueprint's panel opens - this session or a later one. */
	void SaveOpenState() const;

	/** Picks up what SaveOpenState remembered. */
	void RestoreOpenState();

	/** The animation as settings for this Widget Blueprint's preview: each group on its Preview Target. Invert stamped on, loops left to the clock here. */
	void BuildFlattened(UUserWidget* Root, TArray<FWidgetAnimSettings>& OutSettings) const;

	/** Any animation edited - this one, or one it plays as an animation track: Flattened is built again on the next tick. */
	void OnObjectPropertyChanged(UObject* Object, struct FPropertyChangedEvent& Event);

	/** Every widget moved put back as authored, and forgotten. */
	void RestorePreview();

	TWeakObjectPtr<UWidgetBlueprint> Blueprint;
	TWeakObjectPtr<UWidgetAnimPresetAsset> Asset;
	EMode Mode = EMode::Timeline;

	/** The open timeline: its animation, and its name to find it again by when the animation is gone. See GetOpenTimeline. */
	mutable TWeakObjectPtr<UWidgetAnimPresetAsset> OpenTimelineAnimation;
	mutable FString OpenTimelineName;

	/**
	 * Where the open timeline was found last, and on what - so the many times a frame it is asked for (every track's row asks
	 * through GetLayers) cost a check rather than a search. Forgotten every tick, so anything that moved it is found again.
	 */
	mutable TWeakObjectPtr<const UObject> FoundTimelines;
	mutable int32 FoundTimelineIndex = INDEX_NONE;

	/** The designer preview found last, and when it was last looked for - looking is a walk over every User Widget, so it isn't done every frame. */
	mutable TWeakObjectPtr<UUserWidget> CachedRoot;
	mutable double LastRootSearchTime = -1.0;

	/** The root the baselines were captured on, and what they were captured for; either changing means capture again. */
	TWeakObjectPtr<UUserWidget> PosedRoot;
	uint32 PosedSignature = 0;

	/** How many items each layer's Stagger fans out to on the preview, counted every tick. See GetExpandedCount. */
	TArray<int32> ExpandedCounts;

	/** How long each layer whose length comes from its widget runs on the preview, every tick. See GetPreparedRun. */
	TArray<float> PreparedRuns;
	FWidgetAnimBaselineSet Baselines;

	/**
	 * The animation flattened for the preview (BuildFlattened), kept from tick to tick with what it was built for, and built
	 * again only when that changes: an animation edited (OnObjectPropertyChanged), another preview or animation, Invert -
	 * or, whatever happened, FlattenedRefreshSeconds after it was last built. Built every tick, it read every track and found
	 * every group's widget by its path every frame: most of what posing an animation of hundreds of tracks cost. Emptied
	 * while the panel isn't showing. ExpandedCounts and PreparedRuns are counted with it.
	 */
	TArray<FWidgetAnimSettings> Flattened;
	TWeakObjectPtr<UWidgetAnimPresetAsset> FlattenedAnimation;
	TWeakObjectPtr<UUserWidget> FlattenedRoot;
	bool bFlattenedInvert = false;
	bool bFlattenedStale = true;
	double FlattenedTime = -1.0;

	/** Worked out with Flattened: what its poses are captured for (see PosedSignature), and one pass of it, in seconds. */
	uint32 FlattenedSignature = 0;
	float FlattenedPassSeconds = 0.f;

	/**
	 * Where the preview was last posed, and whether anything it was posed from has changed since: an edit, another root,
	 * the clock. Posed again only then - not every frame: posing every track of a large animation, its material parameters
	 * read back and the whole preview invalidated, cost every frame the panel was open, paused or not; and it took the
	 * widgets back from the designer toolbar's preview every frame, so that one stuttered while the panel was open.
	 */
	float PosedAlpha = -1.f;
	bool bPoseStale = true;

	FDelegateHandle PropertyChangedHandle;

	FTSTicker::FDelegateHandle TickerHandle;
	double LastPanelActiveTime = -1.0;

	float Alpha = 0.f;
	bool bPlaying = false;
	bool bPlayingForward = true;
	bool bLoop = false;
	bool bPingPong = true;
	bool bInvert = false;
	float Speed = 1.f;
};

/**
 * A plain list of tracks - any TArray<FWidgetAnimLayer> property of any object, such as a Widget Animation
 * Component's Additive Anim Layers - edited in the panel's compact form, as one implicit group playing on one widget.
 *
 * The list is edited in place on its object: every edit is a transaction on it, so undo, redo and the package's
 * dirty state all work as for any Details edit. Nothing here tells UMG's designer - that would rebuild its preview,
 * and every Details panel with it, on every drag - so the designer's preview is posed by this source itself, as
 * FWidgetAnimAssetPanelSource does it, on whatever widget FindPreviewWidget names.
 *
 * Posing starts only once the panel is used - played, scrubbed, a key picked, an edit made - so merely showing the
 * list (a Details panel with two of these in it, say) leaves the designer alone. One source poses a widget at a
 * time: the one used last takes it over, and the one before puts it back first. When the panel goes away, or
 * another takes over, the widget is put back as authored.
 */
class FWidgetAnimLayerArrayPanelSource : public IWidgetAnimPanelSource
{
public:

	/**
	 * InOwner holds the list, in its property InArrayPropertyName. bInPlaysAsOut: the list plays as an exit - in
	 * reverse, as every Anim Out does - so the preview plays it that way too.
	 */
	FWidgetAnimLayerArrayPanelSource(UObject* InOwner, FName InArrayPropertyName, bool bInPlaysAsOut);
	virtual ~FWidgetAnimLayerArrayPanelSource() override;

	/** The object holding the list, while it exists. */
	UObject* GetOwner() const { return Owner.Get(); }
	FName GetArrayPropertyName() const { return ArrayPropertyName; }
	bool PlaysAsOut() const { return bPlaysAsOut; }

	// IWidgetAnimPanelSource

	virtual UObject* GetEditedObject() const override { return GetLayers() ? Owner.Get() : nullptr; }

	/** The one group every track is in. Kept here, never saved: the list has no groups of its own. */
	virtual TArray<FWidgetAnimLayerGroup>* GetGroups() const override;
	virtual TArray<FWidgetAnimLayer>* GetLayers() const override;
	virtual bool CanEdit() const override { return GetLayers() != nullptr; }
	virtual void NotifyGroupsChanged(EPropertyChangeType::Type ChangeType) override;

	virtual FName GetGroupsPropertyName() const override { return NAME_None; }
	virtual FName GetLayersPropertyName() const override { return ArrayPropertyName; }

	/** The track's Target Widget on the preview widget, or the preview widget itself - as BuildSettings aims it. */
	virtual UWidget* GetLayerPreviewWidget(int32 LayerIndex) const override;
	virtual bool SupportsLayerGroups() const override { return false; }
	virtual bool SupportsLayerTargets() const override { return true; }

	/** The User Widget the posed widget sits in, for anything that reads paths into the designer's preview. */
	virtual UUserWidget* GetPreviewRoot() const override;

	virtual float GetPlayheadAlpha() const override { return Alpha; }
	virtual void ScrubToAlpha(float InAlpha) override;
	virtual void FocusKey(int32 FlatLayerIndex, int32 KeyIndex) override;
	virtual int32 GetExpandedCount(int32 FlatLayerIndex) const override { return ExpandedCounts.IsValidIndex(FlatLayerIndex) ? ExpandedCounts[FlatLayerIndex] : INDEX_NONE; }
	virtual float GetPreparedRun(int32 FlatLayerIndex) const override { return PreparedRuns.IsValidIndex(FlatLayerIndex) ? PreparedRuns[FlatLayerIndex] : -1.f; }
	virtual bool IsPlaying() const override { return bPlaying; }
	virtual void TogglePlay() override;
	virtual bool PlayBackward() override;
	virtual bool GetLoop() const override { return bLoop; }
	virtual void SetLoop(bool bValue) override { bLoop = bValue; }
	virtual bool GetPingPong() const override { return bPingPong; }
	virtual void SetPingPong(bool bValue) override { bPingPong = bValue; }
	virtual bool GetInvert() const override { return bInvert; }
	virtual void SetInvert(bool bValue) override { bInvert = bValue; }
	virtual float GetSpeed() const override { return Speed; }
	virtual void SetSpeed(float InSpeed) override { Speed = FMath::Max(InSpeed, 0.01f); }

	virtual void NotifyPanelActive() override;

	/** Always a working list: there is no asset to switch to. Save as Animation copies it into a new one. */
	virtual EMode GetMode() const override { return EMode::Timeline; }
	virtual void SetMode(EMode InMode) override {}
	virtual UWidgetAnimPresetAsset* GetAsset() const override { return nullptr; }
	virtual void SetAsset(UWidgetAnimPresetAsset* InAsset) override {}

	/**
	 * Copies the tracks into a new WA_ asset beside the Widget Blueprint (GetNewAssetPackagePath, named from
	 * GetNewAssetBaseName), one group playing on whatever widget uses it. The list itself is left as it is.
	 */
	virtual UWidgetAnimPresetAsset* SaveAsNewAsset() override;

	/** Puts the preview widget back as authored now, if this source is posing it. */
	void RestorePreview();

	/**
	 * The Details row the list is shown in, if any - its property handle. Edits here go straight onto the object, which
	 * keeps the Details panel and UMG's designer from rebuilding on every drag; once they settle, they are told through
	 * this handle, exactly as an edit of the plain fields would be, so whatever the Details panel does with such an edit
	 * - UMG copying it from its preview onto the Widget Blueprint, marking the Blueprint modified, rebuilding its
	 * preview - happens to them too. Without a live handle, an owning Blueprint is marked modified instead.
	 */
	void SetArrayHandle(const TSharedPtr<IPropertyHandle>& InHandle);

protected:

	/** The widget the tracks play on in the designer's preview, or null while there is none. Looked for at most twice a second. */
	virtual UWidget* FindPreviewWidget() const { return nullptr; }

	/** Just before this source first poses Widget: the moment to still anything else that animates it there. */
	virtual void OnPreviewTaken(UWidget* Widget) {}

	/** Just after this source put Widget back as authored. bEdited: the list was edited while it was posed. */
	virtual void OnPreviewReleased(UWidget* Widget, bool bEdited) {}

	/**
	 * The widget a track's Target Widget path names, on the preview Widget is part of. Null for None - the track moves
	 * Widget itself - or for a path that finds nothing, which plays on Widget too, as at runtime.
	 */
	virtual UWidget* ResolveLayerTarget(UWidget* Widget, FName Path) const;

	/**
	 * Settled edits are about to be told (see SetArrayHandle): the moment for a subclass to copy them onto every other
	 * copy of the list that must agree - the object saved in the Blueprint, the designer's live one.
	 */
	virtual void PrepareCommit() {}

	/** Settled edits were told - through the Details row's handle when bThroughHandle, else by marking the Blueprint modified. */
	virtual void FinishCommit(bool bThroughHandle) {}

	/** Where SaveAsNewAsset puts the new asset, and the name it starts from (numbered on a clash). */
	virtual FString GetNewAssetPackagePath() const;
	virtual FString GetNewAssetBaseName() const;

	/**
	 * The tracks as settings for Widget: placed in sequence, material and shape tracks aimed at it, hidden ones muted,
	 * Invert stamped on - after whatever animation they play within (AppendContextSettings). OutFirstOwn is where the
	 * list's own settings begin.
	 */
	void BuildSettings(UWidget* Widget, TArray<FWidgetAnimSettings>& OutSettings, int32* OutFirstOwn = nullptr) const;

	/** The animation the list plays within, as settings for Widget, placed before the list's own - nothing by default. */
	virtual void AppendContextSettings(UWidget* Widget, TArray<FWidgetAnimSettings>& OutSettings) const {}

	/** Whether the list's own tracks play inverted as part of that animation - its use of it inverts it. */
	virtual bool GetContextInvert() const { return false; }

	/** The preview widget, from FindPreviewWidget - cached, since finding it may walk every widget. */
	UWidget* GetPreviewWidget() const;

private:

	/** The ticker: advances the clock and poses the preview while the panel is in use, puts it back once it isn't. */
	bool Tick(float DeltaTime);

	/** The panel was used: from now until it goes away, this source poses the preview. */
	void Claim();

	/** Puts the posed widget back as authored and forgets it, without ending the claim. Returns it if it was still alive. */
	UWidget* PutBackPose();

	/** An edit to be told through the Details once edits settle - see SetArrayHandle. */
	void MarkCommitPending();

	/** Tells the settled edits through the Details row's handle, or marks the Blueprint modified. See SetArrayHandle. */
	void CommitEdits();

	/** Any property of the object changed - the Selection panel's fields among them: a commit is due. */
	void OnObjectPropertyChanged(UObject* Object, struct FPropertyChangedEvent& Event);

	TWeakObjectPtr<UObject> Owner;
	FName ArrayPropertyName;
	bool bPlaysAsOut = false;

	/** The implicit group - one, playing on the widget itself. */
	mutable TArray<FWidgetAnimLayerGroup> Groups;

	mutable TWeakObjectPtr<UWidget> CachedWidget;
	mutable double LastWidgetSearchTime = -1.0;

	/** The widget posed, and what it was captured for; either changing means capture again. */
	TWeakObjectPtr<UWidget> PosedWidget;
	uint32 PosedSignature = 0;

	/** How many items each layer's Stagger fans out to on the preview, counted every tick. See GetExpandedCount. */
	TArray<int32> ExpandedCounts;

	/** How long each layer whose length comes from its widget runs on the preview, every tick. See GetPreparedRun. */
	TArray<float> PreparedRuns;
	FWidgetAnimBaselineSet Baselines;

	/** Whether the list was edited while the widget was posed - told to OnPreviewReleased. */
	bool bEditedWhilePosed = false;

	/** Whether the panel has been used since it last went away - see Claim. */
	bool bClaimed = false;

	/** The Details row's handle, edits not yet told through it, when the last one was made, and whether it is being told now. */
	TWeakPtr<IPropertyHandle> ArrayHandle;
	bool bCommitPending = false;
	double LastEditTime = 0.0;
	bool bCommitting = false;
	bool bCommitScheduled = false;
	FDelegateHandle PropertyChangedHandle;

	FTSTicker::FDelegateHandle TickerHandle;
	double LastPanelActiveTime = -1.0;

	float Alpha = 0.f;
	bool bPlaying = false;
	bool bPlayingForward = true;
	bool bLoop = false;
	bool bPingPong = true;
	bool bInvert = false;
	float Speed = 1.f;
};