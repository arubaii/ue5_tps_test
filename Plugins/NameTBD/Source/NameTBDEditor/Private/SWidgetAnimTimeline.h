// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorUndoClient.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"

#include "NameTBD/WidgetAnimation/WidgetAnimLibrary.h"

#include "WidgetAnimLayerEditProxy.h"

class IDetailsView;
class IWidgetAnimPanelSource;
class SScrollBar;
class SVerticalBox;
class SWidgetAnimTimelineTracks;
class FScopedTransaction;
struct FPropertyChangedChainEvent;

/**
 * The Widget Animations panel: an animation laid out the way the native Animations panel lays out a UMG
 * animation, with its layers grouped by the widget each group moves.
 *
 * Left, Animation Layers: a list, group by group, of every layer in the order it plays, each with the stretch
 * of time it runs - what the animation does at a glance, without reading the tracks. Groups are added,
 * duplicated, reordered and deleted from here; clicking a layer selects its track. The list is its own panel,
 * with its own scroll: it doesn't line up with the tracks and isn't dragged.
 *
 * Middle, the Track Editor: a time ruler, and under it each group's line - its name and the span it moves -
 * followed by its tracks, on lines of their own or sharing a line one after another. Tracks are dragged along
 * the timeline, up and down between their group's lines, resized from either edge and snapped to their
 * neighbours; every track's keys are selected, dragged, added and deleted there, and the stretch between two
 * keys can be given a curve of its own. Layers are selected one at a time, or several with Shift (a range) and
 * Ctrl / Cmd (one more), and several move and resize together; keys likewise. A box dragged out over empty space
 * selects the tracks it takes in - or, when it holds no whole track, the keys inside it.
 * Hovering a track or a key shows what it does. Right-drag pans, in time and down the lines.
 *
 * Right, the Selection: only what is selected - a key's pose, a layer's settings, what several layers share, a
 * group's name and target, or the animation itself.
 *
 * Locking: the lock in the header makes the whole animation read-only - a preview, every editable thing greyed
 * out. A single track can be locked from its context menu, which leaves it selectable but unmovable.
 *
 * What is being edited and how it plays comes from an IWidgetAnimPanelSource: a builder's working copy in a
 * WidgetAnimationBuilder's editor, or a WA_ asset edited directly in any other Widget Blueprint's editor.
 * Either way every edit is on that object in a details view of the panel's own, which UMG's designer never
 * hears about - nothing flickers - and the designer's preview follows every edit, mid-drag included.
 *
 * Hosted by the status bar drawer, or by a tab when docked in the layout; each is its own instance.
 */
class SWidgetAnimTimeline : public SCompoundWidget, public FSelfRegisteringEditorUndoClient
{
public:

	SLATE_BEGIN_ARGS(SWidgetAnimTimeline) {}

		/** What the panel edits and plays. Shared between the drawer and the docked tab of one editor. */
		SLATE_ARGUMENT(TSharedPtr<IWidgetAnimPanelSource>, Source)

		/** Shows the Dock in Layout button, and is what it does. Left unbound where the panel already is a tab. */
		SLATE_EVENT(FSimpleDelegate, OnDockInLayout)

	SLATE_END_ARGS()

	/** How tall the Track Editor's lines may be dragged, and how tall they start. */
	static constexpr float MinRowHeight = 18.f;
	static constexpr float MaxRowHeight = 64.f;
	static constexpr float DefaultRowHeight = 26.f;

	/** Height of the time ruler across the top of the Track Editor. */
	static constexpr float RulerHeight = 28.f;

	/** Height of a row of the Animation Layers list. */
	static constexpr float ListRowHeight = 24.f;

	/** The shortest a track can be resized to, in seconds. */
	static constexpr float MinLayerSeconds = 0.01f;

	/**
	 * One line of the Track Editor: a group's own line - its name, and the span it moves - or one of the lines
	 * its tracks sit on. A line holds any number of tracks, one after another, never overlapping.
	 */
	struct FTrackLine
	{
		enum class EKind : uint8
		{
			Group,
			Lane
		};

		EKind Kind = EKind::Lane;
		int32 Group = INDEX_NONE;

		/** A track line's place among its group's track lines, from 0. */
		int32 Ordinal = INDEX_NONE;

		/** The layers on a track line, in the order they run. */
		TArray<int32> Layers;
	};

	/**
	 * What is selected, first and foremost: the animation (nothing), a group (Group only), a layer (Layer, with
	 * its Group), or a key of one. With several layers selected this is the one clicked last - see
	 * GetSelectedLayers for all of them.
	 */
	struct FSelection
	{
		int32 Group = INDEX_NONE;
		int32 Layer = INDEX_NONE;
		int32 Key = INDEX_NONE;

		bool operator==(const FSelection& Other) const { return Group == Other.Group && Layer == Other.Layer && Key == Other.Key; }
		bool operator!=(const FSelection& Other) const { return !(*this == Other); }
	};

	/**
	 * One key of one track. Keys are counted along the track: a Transform track's are its curve's, and any other
	 * keyed track's start value is key 0, its keys in between follow, and its end value is the last.
	 */
	struct FKeyRef
	{
		int32 Layer = INDEX_NONE;
		int32 Key = INDEX_NONE;

		bool operator==(const FKeyRef& Other) const { return Layer == Other.Layer && Key == Other.Key; }
		bool operator!=(const FKeyRef& Other) const { return !(*this == Other); }
	};

	/** What a drag on a track's section does to it. */
	enum class ELayerDrag : uint8
	{
		/** Moves it along the timeline, keeping its length - and up or down its group's lines. */
		Move,

		/** Moves its start edge, keeping its end. */
		ResizeStart,

		/** Moves its end edge, keeping its start. */
		ResizeEnd
	};

	SWidgetAnimTimeline();
	virtual ~SWidgetAnimTimeline() override;

	void Construct(const FArguments& InArgs);

	// SWidget - the panel's keyboard commands, from wherever in it the focus is: Delete / Backspace, Ctrl+A,
	// Escape, F, Space, Comma and Period.
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	// ---------------------------------------------------------------------------------------------
	// Reading the animation
	// ---------------------------------------------------------------------------------------------

	IWidgetAnimPanelSource* GetSource() const { return Source.Get(); }

	/** The groups on screen, or null when there is nothing to show. */
	const TArray<FWidgetAnimLayerGroup>* GetGroups() const;

	/** The layers on screen, in sequence, or null when there is nothing to show. */
	const TArray<FWidgetAnimLayer>* GetLayers() const;

	/** The group a layer plays in, or INDEX_NONE. */
	int32 GetLayerGroup(int32 LayerIndex) const;

	/** Whether there is an animation on screen at all - editable or not. */
	bool HasAnimation() const;

	/** Whether the animation on screen can be edited: there is one, the source allows it, and the panel isn't locked. */
	bool CanEdit() const;

	/** The panel's lock: the whole animation read-only, a preview. Shared with the other panel of the same editor. */
	bool IsReadOnly() const;
	void SetReadOnly(bool bReadOnly);

	/** Whether a single track is locked - see FWidgetAnimLayer::bLockedInEditor. */
	bool IsLayerLocked(int32 LayerIndex) const;

	/** Whether a track is hidden from the previews - see FWidgetAnimLayer::bHiddenInEditor. */
	bool IsLayerHidden(int32 LayerIndex) const;

	/**
	 * How the timeline's seconds relate to the authored ones: 1 / the animation's Speed Multiplier. Everything
	 * the panel shows - spans, keys, the playhead - is in played seconds, and every edit converts back.
	 */
	float GetTimeScale() const;

	/** Whether a layer can be edited at all: the animation can be, and the track isn't locked. */
	bool CanEditLayer(int32 LayerIndex) const;

	/**
	 * Whether a layer's keys can be added, deleted and dragged here: an editable layer of a type that has keys -
	 * a Transform layer on an inline curve, or any layer that travels from a start value to an end value.
	 */
	bool CanEditKeys(int32 LayerIndex) const;

	/** Whether one key can be dragged along its track. A keyed track's first and last keys are its start and end, which stay put. */
	bool CanMoveKey(int32 LayerIndex, int32 KeyIndex) const;

	/** Whether one key can be deleted. Never a keyed track's start or end, and never below the two a Transform track needs. */
	bool CanDeleteKey(int32 LayerIndex, int32 KeyIndex) const;

	/** Whether a layer's section can be dragged along the timeline, and up and down its group's lines. */
	bool CanMoveLayer(int32 LayerIndex) const { return CanEditLayer(LayerIndex); }

	/**
	 * Whether a track's section can be resized from that edge. Never a Transform track timed by a shared Curve
	 * asset, whose keys aren't this animation's to stretch. A track starting at 0 can still be shortened from its
	 * start; it just can't be pulled back past 0.
	 */
	bool CanResizeLayer(int32 LayerIndex, bool bStartEdge) const;

	/** Where every layer starts and how long it runs, in authored seconds from the top. X is start, Y is length. */
	void GetLayerSpans(TArray<FVector2f>& OutSpans) const;

	/** The same, as worked out for the Track Editor this frame - what it draws and hit-tests against. */
	const TArray<FVector2f>& GetTrackSpans() const { return TrackSpans; }

	/** From a group's first layer starting to its last one ending. X is start, Y is end; Y below X when it has no layers. */
	FVector2f GetGroupSpan(int32 GroupIndex) const;

	/** Seconds one pass of the animation takes, as authored: where the longest group ends. */
	float GetPassSeconds() const;

	/**
	 * Key times of a layer, in seconds from the layer's own start: a Transform layer's curve keys, or any other
	 * keyed layer's start, keys and end - once it has a key between its start and end; before that, none. Empty
	 * for a type with no keys (Pulse).
	 */
	void GetKeyTimes(int32 LayerIndex, TArray<float>& OutTimes) const;

	/**
	 * The curve of segment Segment of a layer - from key Segment to the next - or null when there is no such
	 * segment. See FWidgetAnimValueKeys and FWidgetTransformKeyframe::CurveToNext.
	 */
	const FWidgetAnimSegmentCurve* GetSegmentCurve(int32 LayerIndex, int32 Segment) const;

	/** Whether any segment of a layer has a curve of its own - which is when its section sketches each segment's curve. */
	bool HasSegmentCurves(int32 LayerIndex) const;

	/** Whether a layer's segment curves are used at all - not on a Spring track, which springs across its whole run. */
	bool UsesSegmentCurves(int32 LayerIndex) const;

	/** Where the designer's preview is, in authored seconds. */
	float GetPlayheadSeconds() const;

	/** The widget a group animates in this editor's preview, as a label: its path, or "(this widget)". */
	FText GetGroupTargetLabel(int32 GroupIndex) const;

	/** A group's name as shown: its Group Name, or "Layer Group N". */
	FText GetGroupName(int32 GroupIndex) const;

	/** When a layer runs, as the list shows it: "0.27 - 0.50 s". */
	FText GetLayerTimeRange(int32 LayerIndex) const;

	// ---------------------------------------------------------------------------------------------
	// The Track Editor's lines
	// ---------------------------------------------------------------------------------------------

	/** Every line of the Track Editor, top to bottom: each group's own line, then its track lines. */
	const TArray<FTrackLine>& GetTrackLines() const { return TrackLines; }

	/** The Track Editor line a layer is drawn on, or INDEX_NONE while its group is folded. */
	int32 GetLayerLine(int32 LayerIndex) const { return LayerToLine.IsValidIndex(LayerIndex) ? LayerToLine[LayerIndex] : INDEX_NONE; }

	/** How many track lines a group has - folded or not. */
	int32 GetGroupLineCount(int32 GroupIndex) const { return GroupLineCounts.IsValidIndex(GroupIndex) ? GroupLineCounts[GroupIndex] : 0; }

	/** A group folded in the Track Editor shows only its own line; in the list, only its header. Each panel folds on its own. */
	bool IsTrackGroupCollapsed(int32 GroupIndex) const { return TrackCollapsedGroups.Contains(GroupIndex); }
	void ToggleTrackGroupCollapsed(int32 GroupIndex);
	bool IsListGroupCollapsed(int32 GroupIndex) const { return ListCollapsedGroups.Contains(GroupIndex); }
	void ToggleListGroupCollapsed(int32 GroupIndex);

	/** Height of the Track Editor's lines. Dragged from any line's edge - all of them at once - and remembered across sessions. */
	float GetRowHeight() const { return RowHeight; }
	void SetRowHeight(float NewHeight);

	/** Scrolls the Track Editor, in time and down its lines, until a layer's track - or a group's line - is in view. */
	void RevealLayer(int32 LayerIndex);
	void RevealGroup(int32 GroupIndex);

	/** What a track's tooltip shows: its type, time and settings - for a Transform layer, a table of the keys' poses. */
	TSharedRef<SWidget> MakeLayerToolTip(int32 LayerIndex) const;

	/** What a key's tooltip shows: when it is, and what of its pose isn't the identity. */
	TSharedRef<SWidget> MakeKeyToolTip(int32 LayerIndex, int32 KeyIndex) const;

	// ---------------------------------------------------------------------------------------------
	// Driving the preview
	// ---------------------------------------------------------------------------------------------

	void ScrubToSeconds(float Seconds);
	void TogglePlay();
	bool IsPlaying() const;

	/** Moves the playhead to the previous (Direction -1) or next (+1) key, layer start or layer end. */
	void StepToMarker(int32 Direction);

	// ---------------------------------------------------------------------------------------------
	// Selection
	// ---------------------------------------------------------------------------------------------

	const FSelection& GetSelection() const { return Selection; }

	/** Every selected layer, in sequence order. One entry for a single layer or a key; empty for a group or nothing. */
	const TArray<int32>& GetSelectedLayers() const { return SelectedLayers; }

	bool IsLayerSelected(int32 LayerIndex) const { return SelectedLayers.Contains(LayerIndex); }
	/** Several layers selected as layers - not several tracks' keys, which select their layers only to light them. */
	bool IsMultiSelection() const { return SelectedLayers.Num() > 1 && SelectedKeys.Num() == 0; }

	void SelectGroup(int32 GroupIndex);
	void SelectLayer(int32 LayerIndex);

	/**
	 * A click on a layer with the file browser's modifiers: Shift selects every layer from the last one clicked to
	 * this one, in the Animation Layers list's order; Ctrl (Cmd on a Mac) adds this one, or takes it out if it was
	 * in. Neither is a plain click, which selects it alone.
	 */
	void SelectLayerWithModifiers(int32 LayerIndex, bool bRange, bool bToggle);

	/** Every layer in the animation. */
	void SelectAllLayers();

	/** Selects a key and takes the designer's preview to it, frozen there. */
	void SelectKey(int32 LayerIndex, int32 KeyIndex);

	/**
	 * A click on a key with a modifier: Ctrl (Cmd on a Mac) adds it to the selected keys, or takes it out if it was
	 * in; Shift adds it. Keys of any tracks can be selected together, and moved together.
	 */
	void SelectKeyWithModifiers(int32 LayerIndex, int32 KeyIndex, bool bToggle);

	/**
	 * What a selection box took in: keys, or tracks - one or the other, never both. bAdd keeps what was selected
	 * (Shift); bToggle flips each one in or out (Ctrl / Cmd).
	 */
	void SelectInBox(const TArray<FKeyRef>& Keys, const TArray<int32>& Layers, bool bAdd, bool bToggle);

	/** Every selected key, the one clicked last among them. Empty unless keys are what is selected. */
	const TArray<FKeyRef>& GetSelectedKeys() const { return SelectedKeys; }

	bool IsKeySelected(int32 LayerIndex, int32 KeyIndex) const { return SelectedKeys.Contains(FKeyRef{ LayerIndex, KeyIndex }); }

	/** Several keys selected - the Selection panel then offers the curve between them. */
	bool IsKeyMultiSelection() const { return SelectedKeys.Num() > 1; }

	/**
	 * The segments between selected keys: every segment - named by its layer and its first key - whose keys at
	 * both ends are selected, on a track that can be edited. What the curve between selected keys is set on.
	 */
	void GetSelectedSegments(TArray<FKeyRef>& OutSegments) const;

	/** Sets the curve of every one of Segments, as one undoable step. Segment curves named by their first key. */
	void SetSegmentCurves(const TArray<FKeyRef>& Segments, const FWidgetAnimSegmentCurve& Curve);

	/** A segment curve's name as shown: its easing, or its Curve asset's name. */
	static FText GetSegmentCurveName(const FWidgetAnimSegmentCurve& Curve);
	void ClearSelection();

	// ---------------------------------------------------------------------------------------------
	// Editing - every one of these is a single undoable transaction on the edited object
	// ---------------------------------------------------------------------------------------------

	void AddGroup();
	void DuplicateGroup(int32 GroupIndex);
	void DeleteGroup(int32 GroupIndex);

	/** Moves a group so it sits before InsertBefore. Num() means last. */
	void MoveGroupTo(int32 GroupIndex, int32 InsertBefore);

	/**
	 * Adds a layer in a group - a new first group when there are none - into the sequence just after the
	 * selected layer, or at the end when no layer is selected. It gets a line of its own at the bottom of its group.
	 */
	void AddLayer(int32 GroupIndex, EWidgetAnimType AnimType);
	void DuplicateLayer(int32 LayerIndex);
	void DeleteLayer(int32 LayerIndex);

	/** Deletes every selected layer that isn't locked. */
	void DeleteSelectedLayers();

	/** Locks or unlocks tracks. Locking one is still an edit, so it is undoable - and refused while the panel is read-only. */
	void SetLayersLocked(const TArray<int32>& LayerIndices, bool bLocked);

	/**
	 * Hides tracks from the previews, or shows them again. Undoable, and allowed while the panel is read-only -
	 * hiding a track is a way of looking at the animation, not of changing what it does.
	 */
	void SetLayersHidden(const TArray<int32>& LayerIndices, bool bHidden);

	/** Moves a layer in the sequence so it sits before InsertBefore. Num() means last. Its group is unchanged. */
	void MoveLayerTo(int32 LayerIndex, int32 InsertBefore);

	/** Moves a layer into another group - another widget - leaving its place in the sequence alone. */
	void SetLayerGroup(int32 LayerIndex, int32 GroupIndex);

	/**
	 * Unwraps an existing Widget Animation's layers into this one, where a new layer would go. An animation with
	 * one group puts its layers in GroupIndex; one with several brings its groups along as new groups.
	 */
	void InsertAnimation(const class UWidgetAnimPresetAsset* Animation, int32 GroupIndex);

	/**
	 * Adds a key at LocalSeconds on a layer. On a Transform layer the new key starts at the pose already there; on
	 * any other keyed layer at the value on the way between its neighbours, and with the curve of the segment it
	 * splits - so adding a key changes nothing until it is edited.
	 */
	void AddKey(int32 LayerIndex, float LocalSeconds);

	/** Deletes a key. A Transform layer always keeps at least two; any other layer keeps its start and end. */
	void DeleteKey(int32 LayerIndex, int32 KeyIndex);

	/** Deletes every selected key that can be, as one undoable step. */
	void DeleteSelectedKeys();

	/**
	 * A key drag: one transaction from press to release, the preview following every move in between. With
	 * Track Snapping on, the key snaps to other tracks' edges, their keys and the playhead within
	 * SnapThresholdSeconds. Pressed on one of several selected keys, every one of them that can move goes with
	 * it, by the same amount - none past a neighbour that isn't moving.
	 */
	void BeginKeyDrag(int32 LayerIndex, int32 KeyIndex);
	void UpdateKeyDrag(float LocalSeconds, float SnapThresholdSeconds);
	void EndKeyDrag();
	bool IsDraggingKey() const { return KeyDragTransaction.IsValid(); }

	/**
	 * A track drag - moving the pressed layer's section, or resizing it from one edge - applied to every selected,
	 * unlocked layer if the pressed one is selected, else to it alone. One transaction from press to release.
	 *
	 * Every other layer stays exactly where it was: every Delay is rewritten so nothing else in the sequence
	 * moves. With Linked Edges on, a resize also takes along the edges of the tracks below that line up with the
	 * edge being dragged. A move also carries the tracks up or down their group's lines; a track that would
	 * overlap another on the line it lands on gets a line of its own just below. Returns false when nothing can
	 * be dragged.
	 */
	bool BeginLayerDrag(int32 PressedLayer, ELayerDrag Mode);

	/**
	 * DeltaSeconds is how far the mouse has moved since the press, in authored seconds; DeltaLines how many
	 * lines up (negative) or down it has moved - read only by a move.
	 */
	void UpdateLayerDrag(float DeltaSeconds, int32 DeltaLines, float SnapThresholdSeconds);
	void EndLayerDrag();
	bool IsDraggingLayers() const { return LayerDragTransaction.IsValid(); }

	/** Whether a layer is among those the current track drag carries. */
	bool IsLayerBeingDragged(int32 LayerIndex) const;

	/** Where the current drag last snapped to, in authored seconds - drawn as a line through the rows. */
	TOptional<float> GetSnapIndicator() const { return SnapIndicator; }

	/** Whether a drag is under way, of anything. The tracks freeze their view while one is. */
	bool IsDragging() const { return IsDraggingKey() || IsDraggingLayers(); }

	/** Told by a multi-selection proxy that one of its properties was edited. Copies that property back onto the real layer. */
	void OnProxyEdited(UWidgetAnimLayerEditProxy* Proxy, FPropertyChangedChainEvent& PropertyChangedEvent);

	// ---------------------------------------------------------------------------------------------
	// Menus
	// ---------------------------------------------------------------------------------------------

	TSharedRef<SWidget> MakeGroupContextMenu(int32 GroupIndex);
	TSharedRef<SWidget> MakeLayerContextMenu(int32 LayerIndex, int32 KeyIndex, TOptional<float> LocalSeconds);

	/** Add Layer Group, Add Animation Track to each group, and Insert Existing Widget Animation. */
	TSharedRef<SWidget> MakeAddMenu(int32 GroupIndex);

	/** Display name and track colour of a layer type. The colour is the user's, from the editor settings. */
	static FText GetAnimTypeName(EWidgetAnimType AnimType);
	static FLinearColor GetAnimTypeColor(EWidgetAnimType AnimType);

	/** A colour washed out the way a track that can't be edited is drawn. */
	static FLinearColor GetLockedColor(const FLinearColor& Color);

	/**
	 * The colour a group is marked with - its band in the list, its bar and edge in the Track Editor. Generated
	 * for any number of groups, muted so it never competes with the layer colours - see NameTBDColors.h.
	 */
	static FLinearColor GetGroupColor(int32 GroupIndex);

	/** A layer's easing as shown on its section: the curve's name, a Curve asset's own name, or Spring. */
	static FText GetEasingName(const FWidgetAnimLayer& Layer);

	// FEditorUndoClient
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;

private:

	/** Once a frame while the panel exists: follow edits made elsewhere, compiles, and the asset being switched. */
	EActiveTimerReturnType OnRefreshTimer(double InCurrentTime, float InDeltaTime);

	/** Brings everything that shows the animation's structure up to date: the Track Editor's lines, and the list. */
	void RebuildRows();

	/** Rebuilds the Animation Layers list. Its times read live; only its structure and order need this. */
	void RebuildLayerList();

	/**
	 * Works out the Track Editor's lines from the layers' groups, their EditorLine and their times: each group's
	 * layers by line, and on each line in time order, a track that would overlap one already there moved to a
	 * line of its own just below. Cheap, and done every frame, so the lines follow every edit.
	 */
	void UpdateTrackLayout();

	/** Writes every layer's line as the Track Editor shows it now, so what a drag left on screen is what is kept. */
	void CommitTrackLines();

	/** Every layer, group by group, in the order it starts - the list's order, and what Shift-click ranges over. */
	void GetListOrder(TArray<int32>& OutOrder) const;

	/** Every edit made here, told to the source: keys synced to curves, previews refreshed. */
	void NotifyGroupsChanged(EPropertyChangeType::Type ChangeType);

	/** Points the Selection panel at the selection and rebuilds it - one layer's properties, or several layers' shared ones. */
	void RefreshDetails();

	/** Re-makes the proxies the Selection panel shows for a multi-selection, one per selected layer. */
	void RebuildLayerProxies();

	/** Copies the real layers onto their proxies where they differ - after a drag, an undo, or an edit elsewhere. */
	void SyncLayerProxies();

	/** Whether the Selection panel's fields can be edited: the animation can be, and nothing selected is locked. */
	bool IsSelectionEditable() const;

	FText GetDetailsTitle() const;

	/** Drops any part of a selection that no longer exists. */
	FSelection Validate(const FSelection& InSelection) const;

	/** Drops selected layers that no longer exist, and keeps the primary selection among them. */
	void ValidateSelectedLayers();

	/** After layers were removed: what stays selected follows the layers it pointed at; nothing left clears it. */
	void RemapSelectionAfterRemoving(const TArray<int32>& Removed);

	/** Groups, layers, types, key counts, locks, fold state: a change means the list or the Selection panel go stale. */
	uint32 ComputeLayoutSignature() const;

	/** The list's order: a change means the list is rebuilt - and only the list. */
	uint32 ComputeListSignature() const;

	/** The edited object, Modify()'d for an edit - null unless editable. Its groups and layers are GetSource()'s. */
	UObject* BeginEdit();

	/** Where a new layer goes in the sequence: after the selected layer, or at the end. */
	int32 GetInsertIndex() const;

	/** The group a new layer goes in when none is named: the selection's, else the last. INDEX_NONE when there are none. */
	int32 GetDefaultGroup(int32 GroupIndex) const;

	/** The layers a menu or a drag acts on for LayerIndex: the whole selection if it is part of it, else just it. */
	TArray<int32> GetActedOnLayers(int32 LayerIndex) const;

	/**
	 * Where a drag can snap to: 0, the end of the animation, the playhead, and the start and end of every layer
	 * not in Excluded - with their keys too when bIncludeKeys.
	 */
	void GatherSnapTargets(const TSet<int32>& Excluded, bool bIncludeKeys, TArray<float>& OutTargets) const;

	/** Rewrites every layer's Delay, keeping its Starts, so each lands exactly at NewStarts - and Transform keys are stretched to NewLengths. */
	void ApplyLayerSpans(const TArray<float>& NewStarts, const TArray<float>& NewLengths);

	/** The Insert Existing Widget Animation picker. */
	void AddInsertAnimationEntry(class FMenuBuilder& Menu, int32 GroupIndex);

	/** "Add Animation Track to {group}", opening onto every track type. */
	void AddTrackSubMenu(class FMenuBuilder& Menu, int32 GroupIndex);

	/** Every layer's start in authored seconds, as the sequence places it. */
	void GetAuthoredStarts(TArray<float>& OutStarts) const;

	/**
	 * After an edit that added, removed or reordered layers: rewrites the Delay of every layer that was there
	 * before so it starts exactly where it did - Delay is measured from the layer above, so taking one away
	 * would otherwise move the one below it. OldIndexOf holds, for each layer now, where it was before, or
	 * INDEX_NONE for a new one, which is placed by its own Starts and Delay.
	 */
	void PinLayerStarts(const TArray<float>& OldStarts, const TArray<int32>& OldIndexOf);

	/** The edited object, Modify()'d for an edit that is allowed even while the panel is read-only - null unless the source can edit at all. */
	UObject* BeginEditIgnoringReadOnly();

	/** The Value Watcher, under the Selection panel. */
	TSharedRef<SWidget> MakeValueWatcher();

	/** The Selection panel while several keys are selected: the curve between them, and what they are. */
	TSharedRef<SWidget> MakeKeyCurvePanel();

	/** The curve choices - every easing, and a Curve asset - as a menu that sets them on Segments. */
	void AddSegmentCurveEntries(class FMenuBuilder& Menu, const TArray<FKeyRef>& Segments);

	/** Moves one key to LocalSeconds - played seconds from its layer's start - however that layer stores its keys. */
	void SetKeyTime(int32 LayerIndex, int32 KeyIndex, float LocalSeconds);

	/** Drops selected keys that no longer exist, and keeps the selected layers to the keys' layers. */
	void ValidateSelectedKeys();

	TSharedRef<SWidget> MakeHeader();
	TSharedRef<SWidget> MakeTransport();

	/** The strip across the top of the list, and of the Track Editor: what it is, and its buttons. */
	TSharedRef<SWidget> MakeLayerListHeader();
	TSharedRef<SWidget> MakeTrackEditorHeader();

	/** The Animation Properties button along the bottom of the list, which selects the animation itself. */
	TSharedRef<SWidget> MakeAnimationPropertiesButton();

	TSharedPtr<IWidgetAnimPanelSource> Source;
	FSimpleDelegate OnDockInLayout;

	/** The Track Editor's lines, and where each layer is on them - see UpdateTrackLayout. */
	TArray<FTrackLine> TrackLines;
	TArray<int32> LayerToLine;
	TArray<int32> LayerLineOrdinals;
	TArray<int32> GroupLineCounts;
	TArray<FVector2f> TrackSpans;

	TSet<int32> TrackCollapsedGroups;
	TSet<int32> ListCollapsedGroups;
	float RowHeight = DefaultRowHeight;

	TSharedPtr<SVerticalBox> LayerList;
	TSharedPtr<SWidgetAnimTimelineTracks> Tracks;
	TSharedPtr<SScrollBar> TracksHorizontalScrollBar;

	/** The Selection panel for one thing selected, and the one for several layers' shared properties. */
	TSharedPtr<IDetailsView> DetailsView;
	TSharedPtr<IDetailsView> MultiDetailsView;

	/** The object the Selection panel is bound to, so a compile's new Class Defaults - or another asset - is noticed. */
	TWeakObjectPtr<UObject> DetailsObject;

	/** One per selected layer while several are selected - what MultiDetailsView shows. */
	TArray<TStrongObjectPtr<UWidgetAnimLayerEditProxy>> LayerProxies;

	FSelection Selection;

	/** Every selected layer, in sequence order, and the one a Shift-click ranges from. */
	TArray<int32> SelectedLayers;
	int32 SelectionAnchor = INDEX_NONE;

	uint32 LayoutSignature = 0;
	uint32 ListSignature = 0;

	/** The key being dragged, and the transaction the whole drag is one step of. */
	int32 DragLayer = INDEX_NONE;
	int32 DragKey = INDEX_NONE;
	TUniquePtr<FScopedTransaction> KeyDragTransaction;

	/**
	 * Every key a key drag carries - the pressed one first - and where each was at the press, in played seconds
	 * from its layer's start. Moved by one amount from those, so a drag back and forth never drifts.
	 */
	TArray<FKeyRef> DragKeys;
	TArray<float> DragKeyOriginalTimes;

	/** Where the pressed key was at the press - what the mouse is measured against, whether or not it moves itself. */
	float DragPressedOriginalTime = 0.f;

	/** Every selected key - keys of any tracks - in the order they were picked, the primary one last. */
	TArray<FKeyRef> SelectedKeys;

	/** A track drag in progress: what moves, how, and every layer as it was when it started. */
	struct FLayerDragState
	{
		ELayerDrag Mode = ELayerDrag::Move;

		/** Which edges of which layers the drag moves. A move takes both edges of a layer. */
		TArray<int32> StartEdges;
		TArray<int32> EndEdges;

		/** Every layer at the press: start and length in seconds, and a Transform layer's key times. */
		TArray<float> OriginalStarts;
		TArray<float> OriginalLengths;
		TArray<TArray<float>> OriginalKeyTimes;

		/** Every layer's line at the press - as stored, and as shown - and how many lines each group had. */
		TArray<int32> OriginalEditorLines;
		TArray<int32> OriginalLineOrdinals;
		TArray<int32> OriginalGroupLineCounts;

		/** Where the animation ended at the press. Moves stay inside it; resizes may only pass it from a track that ended there. */
		float OriginalEnd = 0.f;
		bool bEndIsFree = false;

		/** Where the dragged edges can snap to, worked out once at the press. */
		TArray<float> SnapTargets;
	};

	FLayerDragState LayerDrag;
	TUniquePtr<FScopedTransaction> LayerDragTransaction;

	/** Whether the current track drag has moved anything yet - a press let go in place leaves no undo step. */
	bool bLayerDragChanged = false;

	/** A key drag's layer start, and what the key can snap to - both worked out at the press. */
	float KeyDragLayerStart = 0.f;
	TArray<float> KeySnapTargets;

	TOptional<float> SnapIndicator;

	/** Edits made through the multi-selection proxies this frame, told to the source once on the next. */
	TOptional<EPropertyChangeType::Type> PendingProxyNotify;

public:

	/**
	 * What the Value Watcher can watch on a widget. The two-part values - Translation, Scale, Shear - are read
	 * whole, X and Y together, as the Details panel shows a Render Transform.
	 */
	enum class EWatchedValue : uint8
	{
		Opacity,
		Translation,
		Scale,
		Shear,
		Rotation
	};

	static FText GetWatchedValueName(EWatchedValue Value);

private:

	/** One row of the Value Watcher: a widget - a path into the preview, None for the widget itself - and what of it is read. */
	struct FWatch
	{
		FName Target;
		EWatchedValue Value = EWatchedValue::Opacity;

		/** Whether its widget has been picked yet. A row that hasn't reads nothing. */
		bool bPicked = false;
	};

	/** The widget a row watches, on the designer's preview, or null. */
	class UWidget* GetWatchedWidget(FName Target) const;

	/** A row's value, read off the preview widget - X and Y, or the one number in X. False when there is nothing to read. */
	bool ReadWatchedValue(const FWatch& Watch, FVector2D& OutValue) const;

	/** A row's value as it reads: "0.500", "(12.0, -4.0)", "35.0 deg" - uniform pairs as one number. */
	FText FormatWatchedValue(const FWatch& Watch) const;

	/** Whether a track of the row's widget that moves its value is running at the playhead. */
	bool IsWatchChanging(const FWatch& Watch) const;

	/** Rebuilds the Value Watcher's rows after one is added or removed. Each row reads live; only the count needs this. */
	void RebuildWatchRows();

	/** Every row of the Value Watcher, top to bottom. Starts with one, watching nothing yet. */
	TArray<FWatch> Watches;

	/** Where the rows go, and whether the Value Watcher is open - remembered across sessions, as the row height is. */
	TSharedPtr<SVerticalBox> WatchRows;
	bool bWatcherExpanded = true;

	/**
	 * Where the layer or group a menu was opened for is jumped to: the playhead to its start, and the Track Editor
	 * scrolled until it is in view.
	 */
	void JumpToLayer(int32 LayerIndex);
	void JumpToGroup(int32 GroupIndex);

	/** The line a context menu opens with, naming what it acts on - a track and its group, or Multiple Tracks. */
	static void AddMenuHeading(class FMenuBuilder& Menu, const FText& Heading, const FLinearColor& Accent);
};