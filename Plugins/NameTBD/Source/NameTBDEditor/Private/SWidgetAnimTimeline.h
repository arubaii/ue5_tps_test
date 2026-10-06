// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorUndoClient.h"
#include "PropertyEditorDelegates.h"
#include "UObject/ObjectKey.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Layout/SSplitter.h"

#include "NameTBD/WidgetAnimation/WidgetAnimLibrary.h"

#include "WidgetAnimLayerEditProxy.h"

class IDetailsView;
class IWidgetAnimPanelSource;
class SScrollBar;
class SWindow;
class SVerticalBox;
class SWidgetAnimTimelineTracks;
class FScopedTransaction;
class UShapeWidget;
struct FPropertyChangedChainEvent;
struct FPropertyChangedEvent;
namespace WidgetAnimTrackPresets { struct FPreset; }

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
 * neighbors; every track's keys are selected, dragged, added and deleted there, and the stretch between two
 * keys can be given a curve of its own. Layers are selected one at a time, or several with Shift (a range) and
 * Ctrl / Cmd (one more), and several move and resize together; keys likewise. A box dragged out over empty space
 * selects the tracks it takes in - or, when it holds no whole track, the keys inside it.
 * Hovering a track or a key shows what it does. Right-drag pans, in time and down the lines.
 *
 * Right, the Selection: only what is selected - a key's pose, a layer's settings, what several layers share, a
 * group's name and target, or the animation itself.
 *
 * Locking: the lock in the header makes the whole animation read-only - a preview, every editable thing grayed
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

	SLATE_BEGIN_ARGS(SWidgetAnimTimeline)
		: _Compact(false)
		, _Inline(false)
		, _IsCollapsed(false)
		{}

		/** What the panel edits and plays. Shared between the drawer and the docked tab of one editor. */
		SLATE_ARGUMENT(TSharedPtr<IWidgetAnimPanelSource>, Source)

		/** Shows the Dock in Layout button, and is what it does. Left unbound where the panel already is a tab. */
		SLATE_EVENT(FSimpleDelegate, OnDockInLayout)

		/**
		 * The compact panel, for a plain list of tracks inside a Details panel - a component's Additive Anim Layers.
		 * Just the Track Editor, the transport and a small Selection panel beside the tracks: no Animation Tracks
		 * list, no Layer Groups, no Animation Timeline / Edit Existing Asset, no Dock in Layout, no Value Watcher. Its
		 * own header strip names the list and holds Add Track, Save as Animation and Expand.
		 */
		SLATE_ARGUMENT(bool, Compact)

		/** The compact panel's header: what the list is ("Additive Anim Layers In"), and a tip saying more. */
		SLATE_ATTRIBUTE(FText, Title)
		SLATE_ATTRIBUTE(FText, TitleToolTip)

		/** Shows the compact panel's Expand button, and is what it does: the same list in a window of its own. */
		SLATE_EVENT(FSimpleDelegate, OnExpand)

		/**
		 * The compact panel squeezed into a Details panel: a slim transport - the transport buttons and the time, small,
		 * under the tracks only, so the Selection panel beside them runs the whole height, drawn a little smaller - and no
		 * Animation Controls, Track Controls, track scroll bars or Selection header. Loop, Ping Pong, Invert, Speed and the track controls keep
		 * whatever they are set to; Expand shows them all.
		 */
		SLATE_ARGUMENT(bool, Inline)

		/**
		 * Whether the compact panel is folded to its header strip, and what its fold arrow does. The arrow shows only
		 * with OnToggleCollapsed bound. A folded panel leaves the designer alone, as a panel off screen does.
		 */
		SLATE_ATTRIBUTE(bool, IsCollapsed)
		SLATE_EVENT(FSimpleDelegate, OnToggleCollapsed)

	SLATE_END_ARGS()

	/** How tall the Track Editor's lines may be dragged, and how tall they start. */
	static constexpr float MinRowHeight = 18.f;
	static constexpr float MaxRowHeight = 64.f;
	static constexpr float DefaultRowHeight = 33.f;

	/** Height of the time ruler across the top of the Track Editor. Slim: the strip above it already names the panel. */
	static constexpr float RulerHeight = 24.f;

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
			Lane,

			/**
			 * Under an open animation track (see IsAnimationTrackExpanded): one of the inserted animation's own groups -
			 * its header - and the lines its tracks are on. Shown only; nothing on them is this animation's to edit.
			 */
			NestedGroup,
			NestedLane
		};

		EKind Kind = EKind::Lane;

		/** The group the line is in - for a nested line, the group the animation track is in. */
		int32 Group = INDEX_NONE;

		/** A track line's place among its group's track lines, from 0. */
		int32 Ordinal = INDEX_NONE;

		/** The layers on a track line, in the order they run. */
		TArray<int32> Layers;

		/**
		 * A nested line's animation track, which of the inserted animation's groups it shows, and - on a nested track line
		 * - which of the inserted animation's layers are on it, by their index in its Layers.
		 */
		int32 AnimationLayer = INDEX_NONE;
		int32 NestedGroup = INDEX_NONE;
		TArray<int32> NestedLayers;

		/**
		 * A pinned track's line (see FWidgetAnimLayer::bPinnedInEditor): one of the first lines, drawn under the ruler where
		 * it stays while the lines below it scroll. One pinned track to a line, whichever group it is in.
		 */
		bool bPinned = false;

		/**
		 * A collapsed track whose group isn't shown - every track of the group is folded into collapsed tracks (see
		 * IsGroupAllCollapsed) - on a line of its own at the top, under the pinned lines and above every group.
		 */
		bool bLoose = false;

		bool IsNested() const { return Kind == EKind::NestedGroup || Kind == EKind::NestedLane; }
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
		ResizeEnd,

		/**
		 * The animation's own start or end, dragged on the ruler - see BeginRangeDrag. Every track the new start or end cuts
		 * into is shortened to fit.
		 */
		RangeStart,
		RangeEnd
	};

	SWidgetAnimTimeline();
	virtual ~SWidgetAnimTimeline() override;

	void Construct(const FArguments& InArgs);

	// SWidget - the panel's keyboard commands, from wherever in it the focus is: Delete / Backspace, Ctrl+A,
	// Escape, F, Space, Comma and Period.
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	/**
	 * Left and Right, caught on their way in - before anything inside the panel, and before the editor turns an unhandled
	 * arrow into a move to the next panel - while tracks are selected and the panel (not a text box in it) has the focus.
	 */
	virtual FReply OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	/** The editor's own arrow-key navigation out of the panel: Up and Down always refused, Left and Right while they move tracks. */
	virtual FNavigationReply OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent) override;

	/** Whether Left and Right belong to the panel right now: something selected, and the focus on the panel or its Track Editor. */
	bool ArrowKeysMoveTracks() const;

	/** Letting go of Left or Right ends the keyboard move it started (see BeginKeyboardMove). */
	virtual FReply OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	/** The keyboard move ends with the focus too: a key let go elsewhere is never heard here. */
	virtual void OnFocusLost(const FFocusEvent& InFocusEvent) override;

	// While a Preview Target is picked in the designer the panel holds the mouse: the eyedropper cursor, the widget under
	// it named, the click taken as the pick. Otherwise these leave everything to the panel's children. See BeginTargetPick.
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;

	// ---------------------------------------------------------------------------------------------
	// Reading the animation
	// ---------------------------------------------------------------------------------------------

	IWidgetAnimPanelSource* GetSource() const { return Source.Get(); }

	// ---------------------------------------------------------------------------------------------
	// The Script area - Python that makes tracks, under the Track Editor (the full panel only)
	// ---------------------------------------------------------------------------------------------

	/** Whether the Script area is open (see SWidgetAnimScriptPanel). Remembered between sessions. */
	bool IsScriptOpen() const { return bScriptOpen; }
	void SetScriptOpen(bool bOpen);

	/** The Script area in a window of its own, in place of under the Track Editor - or that window brought to front. */
	void OpenScriptWindow();
	bool IsScriptWindowOpen() const { return ScriptWindow.IsValid(); }

	/**
	 * The Script area docked in the editor's layout - the Widget Animation Scripting drawer's tab, beside the Details panel,
	 * with its output under the code (see FWidgetAnimBuilderToolbar::DockScripting) - or that tab brought forward. Outside a
	 * Widget Blueprint editor it opens in a window of its own instead.
	 */
	void DockScript();
	bool IsScriptDocked() const;

	/** Whose script the panel's Script area shows: its Widget Blueprint's (see SWidgetAnimScriptPanel::MakeScriptKey). */
	FString GetScriptKey() const;

	/**
	 * A script just ran on the animation (see WidgetAnimScripting::RunPanelScript): the list, the Track Editor and the
	 * Selection panel follow it, and the tracks it added are selected.
	 */
	void OnScriptRan(int32 TracksBefore, int32 TracksAfter, int32 GroupsBefore, int32 GroupsAfter);

	/** Whether this is the compact panel - see FArguments::Compact - and whether it is inline, in a Details panel. */
	bool IsCompact() const { return bCompact; }
	bool IsInline() const { return bCompact && bInline; }

	/** Whether the compact panel is folded to its header - see FArguments::IsCollapsed. */
	bool IsBodyCollapsed() const { return bCompact && IsCollapsedAttr.Get(false); }

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

	/**
	 * Whether the animation on screen is a percentage animation (see EWidgetAnimTimeUnit): its timeline runs from 0% to
	 * 100%, one second as stored, and every time the panel shows is a percentage.
	 */
	bool IsPercentTime() const;

	/**
	 * A time on the timeline, as the panel shows it: "0.25 s" with Decimals places, or on a percentage timeline "25%" -
	 * "12.5%" where it has a fractional part (see FormatPercent).
	 */
	FString FormatTime(float Seconds, int32 Decimals = 2) const;

	/**
	 * A time on a percentage timeline as a percentage: "25%", or "12.5%" and "33.33%" where it has a fractional part -
	 * rounded to two places, with no trailing zeros. 100% is one second as stored.
	 */
	static FString FormatPercent(float Seconds);

	/** A length on the timeline, the same way, for spans: "0.25 - 0.50 s", or "25 - 50%". */
	FString FormatSpan(float Start, float End) const;

	/** Changes the animation's Time In, converting its tracks - one undoable step. See IWidgetAnimPanelSource::SetTimeUnit. */
	void SetTimeUnit(EWidgetAnimTimeUnit Unit);

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

	/**
	 * How much further than its first item a staggered layer reaches along the timeline - where its last item starts -
	 * in played seconds, as worked out this frame. 0 for a layer with no Stagger. See WidgetAnimExpander.h.
	 */
	float GetLayerFanSeconds(int32 LayerIndex) const;

	/** How many items a staggered layer fans out to on the preview this frame. INDEX_NONE when unknown or not staggered. */
	int32 GetLayerFanCount(int32 LayerIndex) const { return TrackFanCounts.IsValidIndex(LayerIndex) ? TrackFanCounts[LayerIndex] : INDEX_NONE; }

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
	 * Jump to Edited Key (Editor Preferences): an edit to the selected key - or to the selected track's start or end value -
	 * moves the preview there, so the change shows as it is made.
	 */
	void JumpToEditedKey(const FPropertyChangedEvent& Event);

	/** Set while JumpToEditedKey moves the preview, so the edits that moving may cause don't move it again. */
	bool bJumpingToEditedKey = false;

	/** The Shape track and target shape a kind was last matched for - see OnRefreshTimer - so each is matched once. */
	TPair<int32, FObjectKey> LastShapeMatch = TPair<int32, FObjectKey>(INDEX_NONE, FObjectKey());

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

	// ---------------------------------------------------------------------------------------------
	// Folders - see FWidgetAnimLayerGroup::EditorFolder. Only the list reads them; nothing about playing changes.
	// ---------------------------------------------------------------------------------------------

	/** The folder a group is listed under, trimmed - empty when none, or where the source has no groups of its own. */
	FString GetGroupFolder(int32 GroupIndex) const;

	/** Every folder in use, in the order the list shows them. */
	void GetFolders(TArray<FString>& OutFolders) const;

	/** The groups in a folder, in order. */
	void GetFolderGroups(const FString& Folder, TArray<int32>& OutGroups) const;

	/**
	 * The groups in the order the list shows them: each folder where its first group is, its groups gathered under it, the
	 * rest where they are. What a Shift-click on a group ranges over.
	 */
	void GetGroupListOrder(TArray<int32>& OutGroups) const;

	/** A folder folded in the list shows only its own row. Per panel, like a group's fold. */
	bool IsListFolderCollapsed(const FString& Folder) const { return ListCollapsedFolders.Contains(Folder); }
	void ToggleListFolderCollapsed(const FString& Folder);

	/** When a layer runs, as the list shows it: "0.27 - 0.50 s". */
	FText GetLayerTimeRange(int32 LayerIndex) const;

	// ---------------------------------------------------------------------------------------------
	// The Track Editor's lines
	// ---------------------------------------------------------------------------------------------

	/** Every line of the Track Editor, top to bottom: each group's own line, then its track lines. */
	const TArray<FTrackLine>& GetTrackLines() const { return TrackLines; }

	/**
	 * What the passes over every layer ask of each one, read in one walk over the layers as they are now: its group,
	 * whether it heads a collapsed track or is folded into one, whether it is in view - and of each group, its layers and
	 * whether every one of them is in a collapsed track. Asking each layer through the accessors (GetLayerGroup,
	 * IsCollapsedHead, IsGroupAllCollapsed) walks the layers again - and asks the source for them - every time, which made
	 * laying out an animation of hundreds of tracks cost the square of that, every frame. The same answers as the
	 * accessors, as long as nothing is edited in between. See BuildLayoutIndex.
	 */
	struct FLayoutIndex
	{
		/** Per layer: GetLayerGroup, IsCollapsedHead, IsFoldedIntoCollapsed and IsLayerInView. */
		TArray<int32> LayerGroups;
		TBitArray<> CollapsedHeads;
		TBitArray<> FoldedIntoCollapsed;
		TBitArray<> InView;

		/** Per group: its layers, in the order they sit in the sequence, and IsGroupAllCollapsed. */
		TArray<TArray<int32>> GroupMembers;
		TBitArray<> GroupsAllCollapsed;

		/** IsGroupAllCollapsed of INDEX_NONE - the group of every layer while there are no groups. */
		bool bUngroupedAllCollapsed = false;

		bool IsGroupAllCollapsed(int32 GroupIndex) const
		{
			return GroupsAllCollapsed.IsValidIndex(GroupIndex) ? static_cast<bool>(GroupsAllCollapsed[GroupIndex]) : GroupIndex == INDEX_NONE && bUngroupedAllCollapsed;
		}

		bool IsLooseCollapsedHead(int32 LayerIndex) const
		{
			return CollapsedHeads.IsValidIndex(LayerIndex) && CollapsedHeads[LayerIndex] && IsGroupAllCollapsed(LayerGroups[LayerIndex]);
		}

		/** Empties it, keeping what each array had grown to - it is built again every frame. */
		void Reset();
	};

	/** Reads every layer and group once into OutIndex, as they are now. */
	void BuildLayoutIndex(FLayoutIndex& OutIndex) const;

	/**
	 * The index the Track Editor was laid out with this frame - what it draws and hit-tests against, like GetTrackSpans.
	 * Only to be read while it still matches the layers: IsTrackLayoutCurrent.
	 */
	const FLayoutIndex& GetTrackLayoutIndex() const { return LayoutIndex; }

	/** Whether this frame's layout was worked out for as many layers and groups as there are now. */
	bool IsTrackLayoutCurrent() const;

	/** IsCollapsedHead as this frame's layout has it - what the Track Editor draws, and finds under the mouse. */
	bool IsCollapsedHeadShown(int32 LayerIndex) const;

	/** GetGroupSpan as this frame's layout has it - what the Track Editor draws. */
	FVector2f GetShownGroupSpan(int32 GroupIndex) const;

	/**
	 * A collapsed track's tracks, read in one walk: how many, whether every one is hidden, locked or selected, whether any
	 * is pinned or selected. What its row in the list and its section in the Track Editor show, every frame.
	 */
	struct FCollapsedState
	{
		int32 NumMembers = 0;
		bool bAllHidden = false;
		bool bAllLocked = false;
		bool bAllSelected = false;
		bool bAnySelected = false;
		bool bAnyPinned = false;
	};
	FCollapsedState GetCollapsedState(FName Name) const;

	/**
	 * The read-only tracks of the animation the list plays within - see IWidgetAnimPanelSource::GetContextLayers -
	 * drawn grayed on rows of their own above the list's lines, packed so tracks that don't overlap share a row.
	 * Their spans are in the list's seconds; row is which context row each is on.
	 */
	const TArray<FWidgetAnimLayer>* GetContextLayers() const;
	const TArray<FVector2f>& GetContextSpans() const { return ContextSpans; }
	int32 GetContextRow(int32 ContextIndex) const { return ContextRows.IsValidIndex(ContextIndex) ? ContextRows[ContextIndex] : 0; }
	int32 GetNumContextRows() const { return NumContextRows; }

	/** The Track Editor line a layer is drawn on, or INDEX_NONE while its group is folded. */
	int32 GetLayerLine(int32 LayerIndex) const { return LayerToLine.IsValidIndex(LayerIndex) ? LayerToLine[LayerIndex] : INDEX_NONE; }

	/** How many track lines a group has - folded or not. */
	int32 GetGroupLineCount(int32 GroupIndex) const { return GroupLineCounts.IsValidIndex(GroupIndex) ? GroupLineCounts[GroupIndex] : 0; }

	/** A group folded in the Track Editor shows only its own line; in the list, only its header. Each panel folds on its own. */
	bool IsTrackGroupCollapsed(int32 GroupIndex) const { return TrackCollapsedGroups.Contains(GroupIndex); }
	void ToggleTrackGroupCollapsed(int32 GroupIndex);
	bool IsListGroupCollapsed(int32 GroupIndex) const { return ListCollapsedGroups.Contains(GroupIndex); }
	void ToggleListGroupCollapsed(int32 GroupIndex);

	/**
	 * An animation track (see FWidgetAnimLayer::InsertedAnimation) opened, in both the list and the Track Editor: the
	 * inserted animation's own groups under it, each foldable, with its tracks - tinted, shown only. Closed to start.
	 */
	bool IsAnimationTrackExpanded(int32 LayerIndex) const { return ExpandedAnimationTracks.Contains(LayerIndex); }
	void ToggleAnimationTrackExpanded(int32 LayerIndex);
	bool IsNestedGroupFolded(int32 LayerIndex, int32 NestedGroup) const { return FoldedNestedGroups.Contains(MakeNestedGroupKey(LayerIndex, NestedGroup)); }
	void ToggleNestedGroupFolded(int32 LayerIndex, int32 NestedGroup);

	/**
	 * Where an open animation track's animation's tracks run, in the panel's seconds, one per layer of the inserted
	 * animation - worked out with the lines. Null for a track that isn't an open animation track.
	 */
	const TArray<FVector2f>* GetNestedSpans(int32 LayerIndex) const { return NestedSpans.Find(LayerIndex); }

	/** The inserted animation of an animation track, or null. */
	const class UWidgetAnimPresetAsset* GetInsertedAnimation(int32 LayerIndex) const;

	// ---------------------------------------------------------------------------------------------
	// Collapsed tracks
	//
	// Tracks folded into one, as a Blueprint folds part of a graph into a collapsed node: every track sharing a
	// FWidgetAnimLayer::CollapsedTrack name is shown as one track - its name and the stretch they cover - on the line of
	// the first of them, whichever groups they are in, and as one row in the list that opens onto them. Double-clicked,
	// the panel shows only them - their own tracks, their own groups, the preview playing only them - with a way back
	// along the top, as a collapsed graph opens in its own tab. Only how the panel shows them: each is still a track of
	// the animation, in its own group, playing exactly as it did; a group changed or removed changes or removes its
	// tracks in a collapsed track too.
	// ---------------------------------------------------------------------------------------------

	/** Whether the panel shows one collapsed track on its own - see EnterCollapsed - and which. */
	bool IsInCollapsedScope() const { return !ActiveCollapsed.IsNone(); }
	FName GetActiveCollapsed() const { return ActiveCollapsed; }

	/** Shows only the tracks of the collapsed track Name, and plays only them; ExitCollapsed goes back to the whole animation. */
	void EnterCollapsed(FName Name);
	void ExitCollapsed();

	/** The collapsed track a layer is folded into, or None. */
	FName GetLayerCollapsedName(int32 LayerIndex) const;

	/**
	 * Whether a layer is drawn as its collapsed track here - the first of its tracks, on whose line the collapsed track
	 * sits - and whether it is folded away here, the rest of its tracks. Neither while that collapsed track is open.
	 */
	bool IsCollapsedHead(int32 LayerIndex) const;
	bool IsFoldedIntoCollapsed(int32 LayerIndex) const;

	/**
	 * Whether every track of a group is folded into collapsed tracks, outside a collapsed track open on its own. Such a
	 * group isn't shown - in the Track Editor or the Animation Tracks list - since there is nothing of its own to see: its
	 * collapsed tracks are listed loose at the top instead, and opening one shows the group again. Never an empty group,
	 * which is shown so tracks can be added to it, nor in the compact panel, which shows no groups.
	 */
	bool IsGroupAllCollapsed(int32 GroupIndex) const;

	/** A collapsed track's head whose group isn't shown (IsGroupAllCollapsed): drawn and listed loose, at the top. */
	bool IsLooseCollapsedHead(int32 LayerIndex) const;

	/**
	 * Whether a layer is shown in the panel right now: every layer, or - while a collapsed track is open on its own - only
	 * its tracks.
	 */
	bool IsLayerInView(int32 LayerIndex) const;

	/** Every track of the collapsed track Name, in sequence order. The first is its head. */
	void GetCollapsedMembers(FName Name, TArray<int32>& OutMembers) const;

	/**
	 * Where a layer is drawn, in played seconds, X start and Y length: its own span - or, for a collapsed track's head, the
	 * stretch every one of its tracks covers.
	 */
	FVector2f GetShownSpan(int32 LayerIndex) const;

	/** Whether these tracks can be folded into one collapsed track: two or more of them, or one, editable. */
	bool CanCollapseLayers(const TArray<int32>& LayerIndices) const;

	/** Folds tracks into one new collapsed track - named "Collapsed Tracks", numbered to be its own - and selects it. */
	void CollapseLayers(const TArray<int32>& LayerIndices);

	/** Puts a collapsed track's tracks back where they are, each shown as itself again, and selects them. */
	void ExpandCollapsed(FName Name);

	/** Renames a collapsed track - numbered, if another has the name. */
	void RenameCollapsed(FName OldName, const FString& NewName);

	/**
	 * Gives a track a name of its own (FWidgetAnimLayer::TrackName) - numbered if another renamed track has it. Empty
	 * names it by its type again.
	 */
	void RenameLayer(int32 LayerIndex, const FString& NewName);

	/** Selects every track of a collapsed track, its head the primary one - or, with bToggle, adds them or takes them out. */
	void SelectCollapsed(FName Name, bool bToggle = false);

	/** A collapsed track's tooltip: each of its tracks, by name, how long it runs and how it moves. */
	TSharedRef<SWidget> MakeCollapsedToolTip(int32 HeadLayer) const;

	// ---------------------------------------------------------------------------------------------
	// Pinned tracks, inverted tracks, the animation's range, and picking a target in the designer
	// ---------------------------------------------------------------------------------------------

	/** Whether a track is pinned to the top of the Track Editor - see FWidgetAnimLayer::bPinnedInEditor. */
	bool IsLayerPinned(int32 LayerIndex) const;

	/** Pins tracks to the top of the Track Editor, or unpins them. Undoable, and allowed while read-only - it is a way of looking. */
	void SetLayersPinned(const TArray<int32>& LayerIndices, bool bPinned);

	/** How many lines are pinned to the top of the Track Editor. */
	int32 GetNumPinnedLines() const { return NumPinnedLines; }

	/** Whether a track plays backward in the preview: inverted on its own (Invert Track), or the preview's Invert is on. */
	bool IsLayerInverted(int32 LayerIndex) const;

	/** Whether a track is inverted on its own - see FWidgetAnimLayer::bInvertTrack. */
	bool IsLayerInvertedOnItsOwn(int32 LayerIndex) const;

	/** Inverts tracks on their own, or turns them back. One undoable edit. */
	void SetLayersInverted(const TArray<int32>& LayerIndices, bool bInverted);

	/**
	 * Dragging the animation's start or end on the ruler: the end pulled in shortens every track it cuts into, and pulled
	 * out lengthens the tracks ending where the animation does; the start
	 * pushed in shortens every track starting before it and moves everything back to start at 0, and pulled out before 0
	 * puts that much time in front of everything. A track that can't be shortened - or would be shorter than a track can
	 * be - stops the drag there. One undoable step from press to release; EndLayerDrag ends it.
	 */
	bool BeginRangeDrag(bool bEnd);
	void UpdateRangeDrag(float Seconds, float SnapThresholdSeconds);
	bool IsDraggingRange() const { return IsDraggingLayers() && (LayerDrag.Mode == ELayerDrag::RangeStart || LayerDrag.Mode == ELayerDrag::RangeEnd); }

	/**
	 * Picking a group's Preview Target by clicking a widget in the designer: until a widget is clicked there - or the pick is
	 * canceled - the next widget selected in the Widget Blueprint's designer becomes the group's target.
	 */
	bool CanPickTargetInDesigner() const;
	void BeginTargetPick(int32 GroupIndex);
	void CancelTargetPick();
	bool IsPickingTarget(int32 GroupIndex) const { return PickTargetGroup != INDEX_NONE && PickTargetGroup == GroupIndex; }
	bool IsPickingAnyTarget() const { return PickTargetGroup != INDEX_NONE; }

	/** What a group being picked for says where its target is named: the widget under the eyedropper, or what to do. */
	FText GetPickingTargetText() const;

	/**
	 * While picking: the widget of the designer's preview under ScreenPosition becomes the one the eyedropper names, and
	 * CommitPickHover picks it - false when nothing that can be picked is under the mouse. See FWidgetAnimTargetPickInput.
	 */
	void UpdatePickHover(const FVector2D& ScreenPosition);
	bool CommitPickHover();

	/** Where a new track would start, in played seconds: AtSeconds if set, else after the selected track - or at the end. */
	float GetNewTrackStart(TOptional<float> AtSeconds = TOptional<float>()) const;

	/**
	 * Whether a Stagger means anything on a track: one it could fan out to - its widget's children, or an Animated Text
	 * Block's letters for a track that moves letters - and never a value-driven track or an animation track. A track that
	 * already has one shows it, so it can be taken off.
	 */
	bool CanLayerStagger(int32 LayerIndex) const;

	/**
	 * Height of the Track Editor's lines: Default Track Height in the editor preferences (UWidgetAnimEditorSettings), so
	 * every panel shares it. Dragged from any line's edge - all of them at once - which sets that preference, saved when
	 * the drag ends (SaveRowHeight).
	 */
	float GetRowHeight() const;
	void SetRowHeight(float NewHeight);
	void SaveRowHeight();

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

	/** Go to Start, and Go to End - after which Play plays the animation backward, from its end to its start. */
	void GoToStart();
	void GoToEnd();

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
	 * A click on a group with the file browser's modifiers, as SelectLayerWithModifiers is for tracks: Shift selects every
	 * group from the last one clicked to this one, in the list's order; Ctrl (Cmd on a Mac) adds this one or takes it out.
	 */
	void SelectGroupWithModifiers(int32 GroupIndex, bool bRange, bool bToggle);

	/** Exactly these groups - Primary the one the Selection panel shows, one of them. */
	void SelectGroups(const TArray<int32>& GroupIndices, int32 Primary);

	/** Whether a group is selected, as a group - alone or among several. */
	bool IsGroupSelected(int32 GroupIndex) const;

	/** Every group selected as a group, in order. Empty when the selection is a track, a key or nothing. */
	TArray<int32> GetSelectedGroups() const;

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

	/** Several groups and their tracks, as one undoable edit. */
	void DeleteGroups(TArray<int32> GroupIndices);

	/** Lists the groups under Folder - none takes them out of their folder. One undoable edit; nothing about playing changes. */
	void SetGroupsFolder(const TArray<int32>& GroupIndices, const FString& Folder);

	/** Renames a folder: every group in it moves to NewName - joining a folder of that name, if there is one. */
	void RenameFolder(const FString& Folder, const FString& NewName);

	/** Moves a group so it sits before InsertBefore. Num() means last. */
	void MoveGroupTo(int32 GroupIndex, int32 InsertBefore);

	/**
	 * Adds a layer in a group - a new first group when there are none - into the sequence just after the
	 * selected layer, or at the end when no layer is selected. It gets a line of its own at the bottom of its group.
	 *
	 * InitialTypeParams is a Registered Type layer's type and its starting settings - a Count at its defaults, or a
	 * Blueprint type with its default values. Ignored on every built-in type.
	 *
	 * AtSeconds, when set, is where on the Track Editor's time axis (played seconds) the menu that added it was
	 * opened: the new track starts right there, With the track above it and delayed to that time, rather than After it.
	 *
	 * Preset, when given, is a track preset (see WidgetAnimTrackPresets.h) the new track is made from - its type and
	 * settings, placed as any new track is. AnimType and InitialTypeParams are then the preset's.
	 */
	void AddLayer(int32 GroupIndex, EWidgetAnimType AnimType, const FInstancedStruct* InitialTypeParams = nullptr, TOptional<float> AtSeconds = TOptional<float>(), const FWidgetAnimLayer* Preset = nullptr);
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

	/**
	 * Focus Tracks: hides every other track - marked bHiddenByFocus, so it can be told from one hidden by hand - and
	 * shows these. A focus already on is lifted first. Undoable, and allowed while the panel is read-only, as hiding is.
	 */
	void FocusLayers(const TArray<int32>& LayerIndices);

	/** Un-focus: shows the tracks Focus Tracks hid, and only those - a track hidden by hand before stays hidden. */
	void UnfocusLayers();

	/** Whether Focus Tracks is on: some track is hidden by it. */
	bool IsFocusActive() const;

	/**
	 * Copies tracks to the clipboard as text - each with where it starts and its group - for Paste here or in any
	 * other Widget Animations panel. Cut copies them, then deletes those that aren't locked.
	 */
	void CopyLayers(const TArray<int32>& LayerIndices);
	void CutLayers(const TArray<int32>& LayerIndices);

	/** Whether the clipboard holds tracks copied from a Widget Animations panel. */
	bool CanPasteLayers() const;

	/**
	 * Pastes the copied tracks after the selected one, keeping their spacing in time, the first starting at AtSeconds
	 * (played seconds on the time axis) or else at the playhead. Tracks copied from one group go into GroupIndex (or
	 * the default group); tracks copied from several keep their groups where the animation has them. The pasted tracks
	 * are selected.
	 */
	void PasteLayers(int32 GroupIndex = INDEX_NONE, TOptional<float> AtSeconds = TOptional<float>());

	/** What Cut and Copy act on from the keyboard: the selected tracks, or the one selected. */
	TArray<int32> GetClipboardLayers() const;

	/** Moves a layer in the sequence so it sits before InsertBefore. Num() means last. Its group is unchanged. */
	void MoveLayerTo(int32 LayerIndex, int32 InsertBefore);

	/** Moves a layer into another group - another widget - leaving its place in the sequence alone. */
	void SetLayerGroup(int32 LayerIndex, int32 GroupIndex);

	/** A plain list's track pointed at another widget - see FWidgetAnimLayer::TargetWidget. None is the list's own widget. */
	void SetLayerTarget(int32 LayerIndex, FName Target);

	/**
	 * An animation track's Stretch Across Track (FWidgetAnimLayer::bFitToTrack): on, its animation plays across the track's
	 * own length, which keeps the length it had so nothing moves; off, at the animation's own length.
	 */
	void SetAnimationTrackFit(int32 LayerIndex, bool bFit);

	/** What a Shape track's target is, for which fields it shows - see GetShapeTrackTarget. */
	enum class EShapeTrackTarget : uint8
	{
		/** The widget the animation is put on - (this widget) - whose kind of shape isn't known: picked on the track. */
		OwnWidget,

		/** A shape widget: the track keys that kind of shape's settings. */
		Shape,

		/** Some other widget: no shape fields apply. */
		NotAShape
	};

	/** What the Shape track at LayerIndex plays on, by the designer's preview; OutShape is the shape when it is one. */
	EShapeTrackTarget GetShapeTrackTarget(int32 LayerIndex, UShapeWidget** OutShape = nullptr) const;

	/**
	 * A Shape track's kind of shape (FWidgetTransformAnim::ShapeKind) - which settings its keys hold - in one undo step.
	 * Values its keys had that the new kind shares are kept; the rest start from the target shape, or the kind's defaults.
	 */
	void SetShapeTrackKind(int32 LayerIndex, UClass* Kind);

	/** Sets a Shape track's kind to its target shape's, when its target is a shape of another kind. */
	void MatchShapeTrackToTarget(int32 LayerIndex);

	/**
	 * Whether an animation track follows a float value of its own (Timing set to Value - see FWidgetAnimProgressDrive) or
	 * plays across the track as the timeline passes it. Following a value, it is named after its animation when it has no
	 * Value Name yet.
	 */
	void SetAnimationTrackValueDriven(int32 LayerIndex, bool bValueDriven);

	/**
	 * Which widget a track moves, as tooltips and menus name it: its group's name - or, on a plain list, its Target
	 * Widget, or "This Widget".
	 */
	FText GetLayerWhereName(int32 LayerIndex) const;

	/**
	 * Unwraps an existing Widget Animation's layers into this one, where a new layer would go. An animation with
	 * one group puts its layers in GroupIndex; one with several brings its groups along as new groups. AtSeconds, when set,
	 * is where on the Track Editor's time axis the menu was opened: it starts there, as AddLayer places a track.
	 */
	void InsertAnimation(const class UWidgetAnimPresetAsset* Animation, int32 GroupIndex, TOptional<float> AtSeconds = TOptional<float>());

	/**
	 * Unravel: replaces an animation track with a copy of every track of the animation it plays, each in the group the
	 * track played it in and starting where it played - so it plays as it did, now editable here and no longer following
	 * the animation. The copies are selected. CanUnravelLayer: an editable animation track whose animation has tracks.
	 */
	bool CanUnravelLayer(int32 LayerIndex) const;
	void UnravelAnimationTrack(int32 LayerIndex);

	/**
	 * Adds a key at LocalSeconds on a layer. On a Transform layer the new key starts at the pose already there; on
	 * any other keyed layer at the value on the way between its neighbors, and with the curve of the segment it
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
	 * it, by the same amount - none past a neighbor that isn't moving.
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

	/**
	 * Told by the Selection panel's stand-in for the animation (see UWidgetAnimPresetSelectionShadow) that a property is about
	 * to change, and that it did: the real asset is readied, then given the edit and told of it.
	 */
	void OnShadowPreEdit(UWidgetAnimPresetSelectionShadow* Shadow, FEditPropertyChain& PropertyAboutToChange);
	void OnShadowEdited(UWidgetAnimPresetSelectionShadow* Shadow, FPropertyChangedChainEvent& PropertyChangedEvent);

	/** Where the Selection panel finds the selected track among the Layers it is shown: 0 on the stand-in, else its own place. */
	int32 GetDetailsLayerIndex() const;

	// ---------------------------------------------------------------------------------------------
	// Menus
	// ---------------------------------------------------------------------------------------------

	/** AtSeconds: where on the time axis the group's line was right-clicked, for tracks added from the menu. */
	TSharedRef<SWidget> MakeGroupContextMenu(int32 GroupIndex, TOptional<float> AtSeconds = TOptional<float>());

	/** For several selected groups at once: showing, folding, folders and deleting. */
	TSharedRef<SWidget> MakeGroupsContextMenu(const TArray<int32>& GroupIndices);

	/** A folder's row in the list: its groups, renaming it, and taking its groups out of it. */
	TSharedRef<SWidget> MakeFolderContextMenu(const FString& Folder);

	/**
	 * Track presets (see WidgetAnimTrackPresets.h) for LayerIndices: with bApply, every single-track preset to put on them (the
	 * Selection panel's Preset dropdown); then Save as Preset - under the track's or collapsed track's name, see
	 * GetPresetSource - Delete Preset and Open Presets Folder (the track's menu has only these).
	 */
	void AddPresetEntries(class FMenuBuilder& Menu, const TArray<int32>& LayerIndices, bool bApply);

	/**
	 * What saving LayerIndices as a preset saves, and under what name: one renamed track, under its name; or every track of a
	 * renamed collapsed track, under the collapsed track's (bOutCollapsed). False, with OutWhyNot, for anything else - a track
	 * still called by its type, a collapsed track still called "Collapsed Tracks", several tracks that aren't one collapsed
	 * track, an animation track.
	 */
	bool GetPresetSource(const TArray<int32>& LayerIndices, FString& OutName, TArray<int32>& OutLayers, bool& bOutCollapsed, FText& OutWhyNot) const;

	/** Puts a preset's settings on the tracks, as one undoable edit - where each track is, and its name, stay its own. */
	void ApplyLayerPreset(const TArray<int32>& LayerIndices, const FWidgetAnimLayer& Preset, const FString& PresetName);

	/** Saves LayerIndices as a preset, as GetPresetSource says - asking first before replacing one. */
	void SaveLayersAsPreset(const TArray<int32>& LayerIndices);

	/**
	 * Adds a preset's tracks, as one undoable edit, starting where a new track would (see AddLayer): a single track's named
	 * after it; a collapsed track's as a collapsed track of the preset's name, each at its time relative to the first.
	 */
	void AddLayersFromPreset(int32 GroupIndex, const WidgetAnimTrackPresets::FPreset& Preset, TOptional<float> AtSeconds);

	/** LocalSeconds: where on the track's bar it was right-clicked. ClickSeconds: where on the time axis, for tracks added from it. */
	TSharedRef<SWidget> MakeLayerContextMenu(int32 LayerIndex, int32 KeyIndex, TOptional<float> LocalSeconds, TOptional<float> ClickSeconds = TOptional<float>());

	/**
	 * Add Layer Group, Add Animation Track to each group, and Insert Existing Widget Animation. Opened within a
	 * group (bWithinGroup, on an empty stretch of one of its lines), only that group's Add Animation Track, and no
	 * Add Layer Group. AtSeconds: where on the time axis it was opened, for tracks added from it.
	 */
	TSharedRef<SWidget> MakeAddMenu(int32 GroupIndex, bool bWithinGroup = false, TOptional<float> AtSeconds = TOptional<float>());

	/** Display name and track color of a layer type. The color is the user's, from the editor settings. */
	static FText GetAnimTypeName(EWidgetAnimType AnimType);
	static FLinearColor GetAnimTypeColor(EWidgetAnimType AnimType);

	/**
	 * What a layer is called: its type's name - or, on a Registered Type layer, the name its type registered with
	 * ("Count"), since every registered type shares the one enum value.
	 */
	static FText GetLayerTypeName(const FWidgetAnimLayer& Layer);

	/** What a track is shown as: the name it was given (see RenameLayer), else GetLayerTypeName. */
	static FText GetLayerDisplayName(const FWidgetAnimLayer& Layer);

	/**
	 * Base - trimmed - or, when another renamed track in Layers (other than the one at IgnoreIndex) has it, "Base 2",
	 * "Base 3"... Names compare ignoring case. Empty stays empty.
	 */
	static FString MakeUniqueTrackName(const TArray<FWidgetAnimLayer>& Layers, const FString& Base, int32 IgnoreIndex = INDEX_NONE);

	/** Numbers the name of the track at Index if another renamed track has it - for a track just copied in. */
	static void EnsureUniqueTrackName(TArray<FWidgetAnimLayer>& Layers, int32 Index);

	/**
	 * Whether a track's length comes from its widget rather than its Duration - a text typed at a Typing Speed (see
	 * FWidgetAnimEvaluator::HasDerivedLength). Drawn striped, its end not draggable.
	 */
	static bool HasDerivedLength(const FWidgetAnimLayer& Layer);

	/**
	 * What color a layer is drawn in: its type's - or, on a Blueprint type's track, that type's own Track Color if it
	 * sets one, else the Blueprint type color from the editor settings; on a cue's, the cue color, or a Blueprint cue's own.
	 */
	static FLinearColor GetLayerTypeColor(const FWidgetAnimLayer& Layer);

	/**
	 * Whether a track is a cue - it fires at moments rather than moving anything (see WidgetAnimCue.h): drawn with a line
	 * at each point it fires at, and without an easing, a spring or curves between its keys.
	 */
	static bool IsCueLayer(const FWidgetAnimLayer& Layer);

	/** A color washed out the way a track that can't be edited is drawn. */
	static FLinearColor GetLockedColor(const FLinearColor& Color);

	/**
	 * The color a group is marked with - its band in the list, its bar and edge in the Track Editor. Generated
	 * for any number of groups, muted so it never competes with the layer colors - see NameTBDColors.h.
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

	/** The list's rows under an open animation track: its animation's groups, foldable, and their tracks - tinted, shown only. */
	void AddNestedListRows(int32 LayerIndex, int32 GroupIndex);

	/**
	 * Works out the Track Editor's lines from the layers' groups, their EditorLine and their times: each group's
	 * layers by line, and on each line in time order, a track that would overlap one already there moved to a
	 * line of its own just below. Done whenever anything it reads may have changed - see IsTrackLayoutDue - so the
	 * lines follow every edit.
	 */
	void UpdateTrackLayout();

	/**
	 * Whether the Track Editor's lines may be out of date: an edit anywhere since they were worked out (see
	 * OnObjectChangedForLayout), a drag just ended, the view changed (see ComputeTrackViewHash), or a quarter second gone
	 * by, for what is read off the preview. Worked out every frame regardless, an animation of hundreds of tracks paid for laying
	 * every one of them out sixty times a second with nothing changing.
	 */
	bool IsTrackLayoutDue() const;

	/** What the lines are worked out from besides the layers themselves: which animation, its size, folds, the open collapsed track, its pace. */
	uint32 ComputeTrackViewHash() const;

	/** Any animation changed - an animation track's own tracks are laid out too - or whatever the panel edits: the lines are due. */
	void OnObjectChangedForLayout(UObject* Object, FPropertyChangedEvent& Event);

	/** Works out NestedSpans for the open animation tracks, from TrackSpans - and forgets any that no longer is one. */
	void UpdateNestedSpans();

	/** The lines under an open animation track, after its own line: its animation's groups and their tracks. */
	void AddNestedLines(int32 LayerIndex, int32 GroupIndex);

	/** Works out TrackFans, TrackFanCounts and TrackRuns from the layers' staggers and the preview's counts and lengths. */
	void UpdateStaggerFans();

	/** How long a layer runs as the panel shows it: its length worked out on the preview (TrackRuns), or its own. */
	float GetShownRunLength(int32 LayerIndex) const;

	/** What placing the layers lengthens each by: its stagger's fan, and a worked-out length's difference from its own. */
	void GetPlacementExtras(TArray<float>& OutExtras) const;

	/**
	 * Freezes the preview on a key. With a stagger anywhere, from the panel's own placing - which knows how far the
	 * staggers reach - rather than the source's.
	 */
	void FocusPreviewOnKey(int32 LayerIndex, int32 KeyIndex);

	/** Writes every layer's line as the Track Editor shows it now, so what a drag left on screen is what is kept. */
	void CommitTrackLines();

	/** Every layer, group by group, in the order it starts - the list's order, and what Shift-click ranges over. */
	void GetListOrder(TArray<int32>& OutOrder) const;

	/**
	 * The same, from an index and spans already read - and, when asked, where each group's layers are in it: per group,
	 * the first of them and how many, INDEX_NONE and 0 for a group the list doesn't order.
	 */
	void GetListOrder(TArray<int32>& OutOrder, const FLayoutIndex& Index, const TArray<FVector2f>& Spans, TArray<TPair<int32, int32>>* OutGroupSlices = nullptr) const;

	/** Every edit made here, told to the source: keys synced to curves, previews refreshed. */
	void NotifyGroupsChanged(EPropertyChangeType::Type ChangeType);

	/** Points the Selection panel at the selection and rebuilds it - one layer's properties, or several layers' shared ones. */
	void RefreshDetails();

	/** Re-makes the proxies the Selection panel shows for a multi-selection, one per selected layer. */
	void RebuildLayerProxies();

	/** Copies the real layers onto their proxies where they differ - after a drag, an undo, or an edit elsewhere. */
	void SyncLayerProxies();

	/**
	 * Makes the Selection panel's stand-in match the edited asset and the selection - see UWidgetAnimPresetSelectionShadow.
	 * Null when what is edited isn't an animation asset, which is then shown itself.
	 */
	UObject* UpdateSelectionShadow();

	/** Copies onto the stand-in whatever of the asset changed since - an edit elsewhere, a drag, an undo. Every frame, cheaply. */
	void SyncSelectionShadow();

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

	/** The same, from an index and spans already read - this frame's layout's, say. */
	uint32 ComputeListSignature(const FLayoutIndex& Index, const TArray<FVector2f>& Spans) const;

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
	void AddInsertAnimationEntry(class FMenuBuilder& Menu, int32 GroupIndex, TOptional<float> AtSeconds = TOptional<float>());

	/** Move to Folder (a submenu: the folders there are, and a new one) and Remove from Folder, for GroupIndices. */
	void AddFolderEntries(class FMenuBuilder& Menu, const TArray<int32>& GroupIndices);

	/** Paste, into GroupIndex (INDEX_NONE: see PasteLayers) from AtSeconds - disabled while nothing copied is on the clipboard. */
	void AddPasteEntry(class FMenuBuilder& Menu, int32 GroupIndex, TOptional<float> AtSeconds);

	/** Cut, Copy and Paste for a track's menu, acting on LayerIndices and pasting as AddPasteEntry does. */
	void AddClipboardEntries(class FMenuBuilder& Menu, const TArray<int32>& LayerIndices, int32 GroupIndex, TOptional<float> AtSeconds);

	/** Focus Track(s) on LayerIndices, and Un-focus while a focus is on. */
	void AddFocusEntries(class FMenuBuilder& Menu, const TArray<int32>& LayerIndices);

	/** "Add Animation Track to {group}", opening onto every track type. AtSeconds: see AddLayer. */
	void AddTrackSubMenu(class FMenuBuilder& Menu, int32 GroupIndex, TOptional<float> AtSeconds = TOptional<float>());

	/** Every track type - built-in, registered, Blueprint - each adding a track of it to GroupIndex. AtSeconds: see AddLayer. */
	void AddTrackTypeEntries(class FMenuBuilder& Menu, int32 GroupIndex, TOptional<float> AtSeconds = TOptional<float>());

	/** The compact panel's header strip: the list's name, then Add Track, Save as Animation and Expand. */
	TSharedRef<SWidget> MakeCompactHeader();

	/** The Track Editor's horizontal scroll bar, in a box that shows it only when there is something to scroll to. */
	TSharedRef<SWidget> MakeHorizontalScrollBarSlot();

	/** The way back out of a collapsed track - its name after the animation's, and a back arrow - for the Track Editor's strip. */
	TSharedRef<SWidget> MakeCollapsedBreadcrumb();

	/** The list's rows for a collapsed track: its own row, and - while it is open in the list - a row for each of its tracks. */
	void AddCollapsedListRows(FName Name, int32 GroupIndex);

	/** One track's row in the list, Indent further in for a track of a collapsed track opened in the list. */
	TSharedRef<SWidget> MakeListLayerRow(int32 LayerIndex, int32 GroupIndex, float Indent);

	/** A folder's row in the list: fold arrow, icon, name and how many groups it holds. */
	TSharedRef<SWidget> MakeListFolderRow(const FString& Folder);

	/** Save as Animation Asset, and - where the source offers it - saving and handing the tracks over to an entry. */
	void SaveCompactList(bool bReplaceWithEntry);

	/** The Selection panel: its header strip and whichever view fits what is selected. Shared by both layouts. */
	TSharedRef<SWidget> MakeSelectionPanel();

	/** The inline panel's transport: its buttons and the time, small, in one slim row. */
	TSharedRef<SWidget> MakeInlineTransport();

	/** Where the list starts - right away, or within one of the animations the source offers - as a menu of choices. */
	TSharedRef<SWidget> MakeStartMenu();

	/** The start in force, as the header's Starts button shows it: "Right Away", or the animation's name. */
	FText GetStartLabel() const;

	/**
	 * The compact panel's selection, kept per source while no panel shows it: a Details panel lays itself out again
	 * after an edit is told through it, and the panel it held is made anew - with what was selected, and where the
	 * view was, put back.
	 */
	void SaveCompactState() const;
	void RestoreCompactState();

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

	/**
	 * Moves the panel to another animation - another timeline, the other mode, another asset - by running Switch: out of
	 * an open collapsed track first (its mutes cleared while still on the animation they were set on), then the selection,
	 * fold state kept by index and the rows all started over for what is now edited.
	 */
	void SwitchEditedAnimation(TFunctionRef<void()> Switch);

	/** The header's timeline picker: the open timeline's name and icon, shown in Animation Timeline in a Widget Blueprint. */
	TSharedRef<SWidget> MakeTimelinePicker();

	/** The timeline picker's menu: every timeline of the Widget Blueprint to open, then New, Rename, Duplicate and Delete. */
	TSharedRef<SWidget> MakeTimelineMenu();

	/**
	 * The transport - its buttons, the time, and one pill of controls: Loop, Ping Pong, Invert, Speed, Snapping and Linked
	 * Edges. bStacked lays it out in two rows, for under the Animation Tracks list; otherwise in one, along the bottom.
	 */
	TSharedRef<SWidget> MakeTransport(bool bStacked = false);

	/** The strip across the top of the list, and of the Track Editor: what it is, and its buttons. */
	TSharedRef<SWidget> MakeLayerListHeader();
	TSharedRef<SWidget> MakeTrackEditorHeader();

	/** The Animation Properties button along the bottom of the list, which selects the animation itself. */
	TSharedRef<SWidget> MakeAnimationPropertiesButton();

	TSharedPtr<IWidgetAnimPanelSource> Source;
	FSimpleDelegate OnDockInLayout;

	/** The compact panel, its header's name and tip, and its Expand - see FArguments. */
	bool bCompact = false;
	bool bInline = false;
	TAttribute<bool> IsCollapsedAttr;
	FSimpleDelegate OnToggleCollapsed;
	TAttribute<FText> CompactTitle;
	TAttribute<FText> CompactTitleToolTip;
	FSimpleDelegate OnExpand;

	/** What lays the Selection panel out, kept so it can be registered for any class a source edits. */
	FOnGetDetailCustomizationInstance SelectionLayout;

	/** The Track Editor's lines, and where each layer is on them - see UpdateTrackLayout. */
	TArray<FTrackLine> TrackLines;

	/** The context tracks' spans and rows, worked out with the lines - see GetContextSpans. */
	TArray<FVector2f> ContextSpans;
	TArray<int32> ContextRows;
	int32 NumContextRows = 0;
	TArray<int32> LayerToLine;
	TArray<int32> LayerLineOrdinals;
	TArray<int32> GroupLineCounts;
	TArray<FVector2f> TrackSpans;

	/** What the lines were worked out from this frame, and each group's span on them - see GetTrackLayoutIndex. */
	FLayoutIndex LayoutIndex;
	TArray<FVector2f> TrackGroupSpans;

	/**
	 * Per layer, how much further its stagger reaches, in authored seconds, and how many items the preview counted for
	 * it - worked out at the start of every UpdateTrackLayout, and what every placement in the panel lengthens a
	 * staggered layer by, as the preview plays it. See UpdateStaggerFans.
	 */
	TArray<float> TrackFans;
	TArray<int32> TrackFanCounts;

	/** Per layer, how long it runs on the preview when its widget decides that, in authored seconds; negative when not. */
	TArray<float> TrackRuns;

	TSet<int32> TrackCollapsedGroups;
	TSet<int32> ListCollapsedGroups;

	/** The folders folded in the list, by name. */
	TSet<FString> ListCollapsedFolders;

	/** The open animation tracks, and their folded groups - see IsAnimationTrackExpanded - by position, as groups fold. */
	TSet<int32> ExpandedAnimationTracks;
	TSet<uint64> FoldedNestedGroups;
	static uint64 MakeNestedGroupKey(int32 LayerIndex, int32 NestedGroup) { return (static_cast<uint64>(static_cast<uint32>(LayerIndex)) << 32) | static_cast<uint32>(NestedGroup); }

	/** Per open animation track, where its animation's tracks run - see GetNestedSpans. */
	TMap<int32, TArray<FVector2f>> NestedSpans;

	/** The collapsed track shown on its own, or None for the whole animation - see EnterCollapsed. */
	FName ActiveCollapsed;

	/** Per collapsed track's head, the stretch its tracks cover - worked out with the lines. See GetShownSpan. */
	TMap<int32, FVector2f> CollapsedSpans;

	/** The collapsed tracks opened in the list, showing a row for each of their tracks. */
	TSet<FName> ListOpenCollapsed;

	/** Keeps every track's bMutedByEditorScope in step with the collapsed track shown on its own. */
	void SyncScopeMutes();

	/** Whether SyncScopeMutes last left any track muted - so leaving a collapsed track unmutes them once. */
	bool bScopeMutesApplied = false;

	/** Whether the Script area is open - see IsScriptOpen - and its window, while it is in one. */
	bool bScriptOpen = false;
	TWeakPtr<SWindow> ScriptWindow;

	/** How many lines at the top are pinned tracks' - see FTrackLine::bPinned. */
	int32 NumPinnedLines = 0;

	/** The group whose Preview Target the next widget clicked in the designer becomes, and what was selected there when it began. */
	int32 PickTargetGroup = INDEX_NONE;
	TWeakObjectPtr<class UWidget> PickStartSelection;
	bool bPickStartedWithSelection = false;

	/** The designer's one selected widget in the preview, or null - for picking a target. */
	class UWidget* GetDesignerSelectedWidget() const;

	/** The groups the given animation tracks play their animations' groups in - other than the tracks' own. */
	TArray<int32> GetInsertedGroupsOf(const TArray<int32>& LayerIndices) const;

	/**
	 * Removes those of Candidates no track is in and no animation track plays in any more - an animation track's own
	 * groups, once it is deleted - inside the edit already open. Returns how many went.
	 */
	int32 RemoveGroupsLeftEmpty(TArray<int32> Candidates);

	/** Watches the designer for the pick - see BeginTargetPick. */
	void UpdateTargetPick();

	/** The open Widget Blueprint editor of the source's Widget Blueprint, or null. */
	class FWidgetBlueprintEditor* FindWidgetBlueprintEditor() const;

	/**
	 * A widget of the designer's preview as the panel's preview has it: itself when the panel can target it, else the
	 * panel preview's widget of the same name (TemplateName, or its own).
	 */
	class UWidget* ToPanelPreviewWidget(class UWidget* DesignerWidget, FName TemplateName) const;

	/** Takes the mouse for the pick - see BeginTargetPick. */
	void CapturePickMouse();

	/** Ends a pick: bReleaseMouse lets go of the mouse too (not while the panel is going, or has already lost it). */
	void EndTargetPick(bool bReleaseMouse);

	/** Makes Picked the picking group's Preview Target, in one undoable edit, and ends the pick. */
	void ApplyPickedTarget(class UWidget* Picked);

	/**
	 * The smallest visible widget of the designer's preview under ScreenPosition that can be a Preview Target, and its
	 * target option in OutOption - or null, when the mouse isn't over the preview.
	 */
	class UWidget* FindPreviewWidgetAt(const FVector2D& ScreenPosition, FString& OutOption) const;

	/** While picking: what watches the mouse, the widget under it and its name, and the window showing that by the cursor. */
	TSharedPtr<class IInputProcessor> PickInput;
	TWeakObjectPtr<class UWidget> PickHoverWidget;
	FString PickHoverLabel;
	TSharedPtr<class SWindow> PickLabelWindow;

	/** Go to End was the last thing done with the playhead: Play plays backward from there. */
	bool bPlayBackwardNext = false;

	/**
	 * Playing backward, walked by the panel itself - for a source that can't play backward (see
	 * IWidgetAnimPanelSource::PlayBackward). The playhead, as a fraction of one pass.
	 */
	bool bPanelPlayingBackward = false;
	float PanelBackwardAlpha = 0.f;

	/**
	 * How far along each of the panel's splitters sits - the Animation Tracks list, the Track Editor and the Selection
	 * panel - by key, read from the editor's per-project settings the first time and saved as they are dragged, so the
	 * layout outlasts a dock or undock, a closed panel and an editor restart. See MakeSplitSize / MakeSplitResized.
	 */
	mutable TMap<FName, float> SplitSizes;
	float GetSplitSize(FName Key, float Default) const;
	void SetSplitSize(float NewSize, FName Key);
	TAttribute<float> MakeSplitSize(FName Key, float Default);
	SSplitter::FOnSlotResized MakeSplitResized(FName Key);

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

	/** The proxy SyncLayerProxies compares next, once there are too many to compare every one every frame. */
	int32 NextProxyToSync = 0;

	/** The animation the panel last showed - a different one drops the selection. See OnRefreshTimer. */
	TWeakObjectPtr<UObject> LastEditedObject;

	/** What the Selection panel shows for an animation asset. Made once, filled for each selection. */
	TStrongObjectPtr<UWidgetAnimPresetSelectionShadow> SelectionShadow;

	FSelection Selection;

	/** Every selected layer, in sequence order, and the one a Shift-click ranges from. */
	TArray<int32> SelectedLayers;
	int32 SelectionAnchor = INDEX_NONE;

	/**
	 * Every selected group while the selection is groups - Selection.Group, the one the Selection panel shows, among them -
	 * and the one a Shift-click on a group ranges from. Read only while Selection is a group: see IsGroupSelected.
	 */
	TArray<int32> SelectedGroups;
	int32 GroupSelectionAnchor = INDEX_NONE;

	uint32 LayoutSignature = 0;
	uint32 ListSignature = 0;

	/**
	 * See IsTrackLayoutDue: edits counted as they are told (TrackLayoutRevision), and what the lines were last worked out
	 * from - that count, the view, when, and whether a drag was on. The list's order is checked once after each.
	 */
	uint64 TrackLayoutRevision = 0;
	uint64 LaidOutRevision = MAX_uint64;
	uint32 LaidOutViewHash = 0;
	double LastTrackLayoutTime = -1.0;
	bool bLaidOutWhileDragging = false;
	bool bListCheckPending = true;
	FDelegateHandle ObjectChangedHandle;

	/** IsSelectionEditable's answer this frame, for a selection of this many tracks. */
	mutable uint64 EditableAnswerFrame = MAX_uint64;
	mutable int32 EditableAnswerCount = INDEX_NONE;
	mutable bool bEditableAnswer = true;

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

		/**
		 * The same as bits, one per layer, set once the drag has begun: asked of every moved layer, several times over, on
		 * every frame of the drag - searched for in the lists, a collapsed track of hundreds of tracks cost the square of
		 * that each time.
		 */
		TBitArray<> StartBits;
		TBitArray<> EndBits;

		bool IsStartEdge(int32 LayerIndex) const
		{
			if (StartBits.IsValidIndex(LayerIndex))
			{
				return static_cast<bool>(StartBits[LayerIndex]);
			}
			return StartEdges.Contains(LayerIndex);
		}

		bool IsEndEdge(int32 LayerIndex) const
		{
			if (EndBits.IsValidIndex(LayerIndex))
			{
				return static_cast<bool>(EndBits[LayerIndex]);
			}
			return EndEdges.Contains(LayerIndex);
		}

		/** Every layer at the press: start and length in seconds, and a Transform layer's key times. */
		TArray<float> OriginalStarts;
		TArray<float> OriginalLengths;
		TArray<TArray<float>> OriginalKeyTimes;

		/** Every layer's line at the press - as stored, and as shown - and how many lines each group had. */
		TArray<int32> OriginalEditorLines;
		TArray<int32> OriginalLineOrdinals;
		TArray<int32> OriginalGroupLineCounts;

		/** Where the animation ended at the press. Moves stay inside it; a track's end can be pulled past it (bEndIsFree). */
		float OriginalEnd = 0.f;
		bool bEndIsFree = false;

		/**
		 * A keyboard move (see BeginKeyboardMove) with Arrow Keys Can Extend the Animation on: its tracks can be taken before
		 * the animation's start, the rest of the animation - and its Minimum Length - moving later to make room.
		 */
		bool bStartIsFree = false;

		/**
		 * A range drag: the animation's Minimum Length at the press, in played seconds (0 for none). A keyboard move that
		 * can go before the start (bStartIsFree): the same, in authored seconds, as SetMinimumLength takes it.
		 */
		float OriginalMinimumLength = 0.f;

		/** Where the dragged edges can snap to, worked out once at the press. */
		TArray<float> SnapTargets;
	};

	FLayerDragState LayerDrag;
	TUniquePtr<FScopedTransaction> LayerDragTransaction;

	/** Whether the current track drag has moved anything yet - a press let go in place leaves no undo step. */
	bool bLayerDragChanged = false;

	/**
	 * Moving the selected tracks with Left or Right (Direction -1 or 1): a track drag driven by the keyboard, so locks,
	 * staggers, collapsed tracks and undo work as they do for the mouse. A press moves them a few pixels' worth (ten times
	 * that with Shift); held, they keep going, slowly at first and then faster, until the key is let go - one undo step for
	 * the whole move. Returns false when nothing selected can move.
	 */
	bool BeginKeyboardMove(int32 Direction, bool bLarge);
	void EndKeyboardMove();
	// Anything else that ends the drag - a switch to another animation, the panel locked - ends the keyboard move with it.
	bool IsKeyboardMoving() const { return KeyboardMoveDirection != 0 && IsDraggingLayers(); }

	/** While a keyboard move is held: carries the tracks on, faster the longer the key is down. */
	EActiveTimerReturnType TickKeyboardMove(double CurrentTime, float DeltaTime);

	/** How many played seconds one pixel of the Track Editor is, at its zoom now. */
	float GetSecondsPerPixel() const;

	/** The keyboard move under way: its direction (0 for none), how far it has gone, in played seconds, and since when. */
	int32 KeyboardMoveDirection = 0;
	float KeyboardMoveOffset = 0.f;
	double KeyboardMoveStartTime = 0.0;
	bool bKeyboardMoveLarge = false;
	TSharedPtr<FActiveTimerHandle> KeyboardMoveTimer;

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