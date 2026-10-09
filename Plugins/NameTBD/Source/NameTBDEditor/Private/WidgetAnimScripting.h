// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ScopedTransaction.h"
#include "UObject/Object.h"

#include "NameTBD/WidgetAnimation/WidgetAnimLibrary.h"

#include "WidgetAnimScripting.generated.h"

class IWidgetAnimPanelSource;
class SWidgetAnimTimeline;
class UUserWidget;
class UPanelWidget;
class UWidget;
class UWidgetAnimPresetAsset;
class UWidgetBlueprint;

/**
 * Track scripting: an animation's groups and tracks made and changed from code - Python in the Widget Animations panel's
 * Script area or the editor's console, or an Editor Utility Blueprint - for the patterns over many widgets that are
 * tedious by hand (eight borders each pulsing a little after the one before) and should be one edit to change.
 *
 * This is the plugin's half: one object standing for one animation, edited exactly as the panel edits it - through the
 * panel's source (IWidgetAnimPanelSource), every change on the edited object inside a transaction and followed by
 * NotifyGroupsChanged - so a script works wherever the panel does, and what it makes is what the panel would have made.
 * Its functions are plain and index-based so Blueprint can call them too; Content/Python/widget_anim_scripting.py wraps
 * them in the API scripts are written against (anim.add_track(TrackType.SCALE, target="Border_0", start=0.1, ...)).
 *
 * Every function that can be refused answers false, -1 or an empty string, and GetLastError says why - the Python
 * wrapper raises it as an exception.
 *
 * Time is in the animation's own seconds, as Duration and Delay are typed. Like the panel, changing a track never moves
 * another one: every other track keeps its time, its Delay rewritten to hold it there. Locked tracks take no edits but
 * their editor-only flags (locked, hidden, pinned, collapsed), and aren't removed.
 */
UCLASS(BlueprintType, Transient, meta = (DisplayName = "Widget Animation (Script)"))
class UWidgetAnimScriptAnimation : public UObject
{
	GENERATED_BODY()

public:

	/** For the animation a panel shows: its source, and the panel for what is selected there (may be null). */
	void InitForPanel(const TSharedPtr<IWidgetAnimPanelSource>& InSource, const TSharedPtr<SWidgetAnimTimeline>& InPanel);

	/** For a WA_ asset edited directly, with no panel - and so no widgets to read. */
	void InitForAsset(UWidgetAnimPresetAsset* InAsset);

	/** The panel source this edits, if it was made for one. */
	TSharedPtr<IWidgetAnimPanelSource> GetPanelSource() const { return Source.Pin(); }

	/** How many tracks and groups the animation has - the panel's Run compares them before and after. */
	int32 CountTracks() const;
	int32 CountGroups() const;

	// ---------------------------------------------------------------------------------------------
	// About it
	// ---------------------------------------------------------------------------------------------

	/** Why the last call that could be refused was - empty if it wasn't. */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|Scripting")
	FString GetLastError() const { return LastError; }

	/** Whether it can be edited now: false while the panel is locked, or when what it shows is read-only. */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|Scripting")
	bool IsEditable() const;

	/** The animation's name: the asset's, or what the panel calls a new animation. */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|Scripting")
	FString GetAnimationName() const;

	/** False for a plain list of tracks - a component's Additive Anim Layers - which has no Layer Groups: each track names its own widget. */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|Scripting")
	bool SupportsGroups() const;

	/** The animation's Animation Speed Multiplier: how much faster than authored it plays. */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|Scripting")
	float GetSpeedMultiplier() const;

	/** Seconds one pass takes, authored: where the last track ends, or the Minimum Length if longer. */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|Scripting")
	float GetLength() const;

	/**
	 * Starts one undoable edit: every change until the matching EndEdit is one step of undo, and the panel and previews are
	 * told once at the end rather than after every change. Nests. False when it can't be edited.
	 */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Scripting")
	bool BeginEdit(const FString& Description);

	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Scripting")
	void EndEdit();

	// ---------------------------------------------------------------------------------------------
	// Layer Groups
	// ---------------------------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Widget Animation|Scripting")
	int32 GetNumGroups() const;

	/** The group's name as shown: its Group Name, or "Layer Group N". */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|Scripting")
	FString GetGroupName(int32 Group) const;

	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Scripting")
	bool SetGroupName(int32 Group, const FString& Name);

	/** The group's Preview Target, as a path into the Widget Blueprint - empty for the widget itself. */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|Scripting")
	FString GetGroupTarget(int32 Group) const;

	/** Sets the group's Preview Target from a widget's name or path; empty or "None" for the widget itself. */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Scripting")
	bool SetGroupTarget(int32 Group, const FString& Target);

	/** The List Folder the group is shown under in the panel's Animation Tracks list - empty for none. Only the panel reads it. */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|Scripting")
	FString GetGroupFolder(int32 Group) const;

	/** Shows the group under a List Folder in the panel's Animation Tracks list; empty takes it out. Changes nothing about playing. */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Scripting")
	bool SetGroupFolder(int32 Group, const FString& Folder);

	/** A new group, last. Returns its index, or -1. */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Scripting")
	int32 AddGroup(const FString& Name, const FString& Target);

	/** Removes a group and every track in it; every other track keeps its time, and later groups move up one. */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Scripting")
	bool RemoveGroup(int32 Group);

	// ---------------------------------------------------------------------------------------------
	// Tracks
	// ---------------------------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Widget Animation|Scripting")
	int32 GetNumTracks() const;

	/** Every type AddTrack takes: the built-in ones, then registered and Blueprint types by name, as the Add Track menu lists them. */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|Scripting")
	TArray<FString> GetTrackTypes() const;

	/**
	 * A new track of Type - a built-in type ("Scale"), or a registered or Blueprint type by its name ("Count") - at the end,
	 * starting after the track before it, as long as Default Track Length. Group -1: the last group (one is made if there is
	 * none). Returns its index, or -1.
	 */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Scripting")
	int32 AddTrack(const FString& Type, int32 Group);

	/** A copy of a track right after it, starting with it. Returns the copy's index, or -1. */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Scripting")
	int32 DuplicateTrack(int32 Track);

	/** Removes tracks. Every other track keeps its time. */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Scripting")
	bool RemoveTracks(const TArray<int32>& Tracks);

	/** The track's type as the panel names it: "Fade", "Count", a Blueprint type's name, "Widget Animation" for an inserted one. */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|Scripting")
	FString GetTrackType(int32 Track) const;

	/** The name the track was given (see SWidgetAnimTimeline::RenameLayer) - empty while it is called by its type. */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|Scripting")
	FString GetTrackName(int32 Track) const;

	/** Names the track - numbered if another renamed track has the name; empty names it by its type again. Returns the name it got. */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Scripting")
	FString SetTrackName(int32 Track, const FString& Name);

	UFUNCTION(BlueprintPure, Category = "Widget Animation|Scripting")
	int32 GetTrackGroup(int32 Track) const;

	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Scripting")
	bool SetTrackGroup(int32 Track, int32 Group);

	/** The path of the widget the track moves - its group's Preview Target, or in a plain list its Target Widget. Empty for the widget itself. */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|Scripting")
	FString GetTrackTarget(int32 Track) const;

	/** Moves the track to the group for that widget, made if there is none - or in a plain list sets its Target Widget. */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Scripting")
	bool SetTrackTarget(int32 Track, const FString& Target);

	/** Where the track starts, in seconds from the start of the animation. */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|Scripting")
	float GetTrackStart(int32 Track) const;

	/** Moves the track to start at Seconds - its Delay rewritten, its Starts kept. Every other track keeps its time. */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Scripting")
	bool SetTrackStart(int32 Track, float Seconds);

	/** How long the track runs: its Duration, spring or keys - and a Stagger's items, as the panel draws it. */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|Scripting")
	float GetTrackLength(int32 Track) const;

	/**
	 * A field of the track as text, in Unreal's property format, by name or dotted path ("Duration", "Spring.Bounce",
	 * "ValueKeys.Keys[0].Value", "Expander.Interval"). Names are matched loosely - case, spaces, underscores and a bool's b
	 * don't matter, and display names work - and a name the track doesn't have is looked for in a registered type's
	 * settings. Empty, with GetLastError set, when there is no such field.
	 */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|Scripting")
	FString GetTrackProperty(int32 Track, const FString& Path) const;

	/**
	 * What kind of field Path is - bool, int, float, string, name, text, enum, object, array, struct:<Name>, or
	 * instanced:<the struct it holds, empty for none> - so a caller can read GetTrackProperty's text back. Empty if none.
	 */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|Scripting")
	FString GetTrackPropertyKind(int32 Track, const FString& Path) const;

	/**
	 * Sets a field from text, as GetTrackProperty reads it: "0.4", "True", "EaseOut" (an enum by its name or display name,
	 * loosely), "(R=1,G=0,B=0,A=1)", an asset's path. Numbers are kept within the field's own limits. Setting Start or Delay
	 * places the track by them; any other field leaves it where it is.
	 */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Scripting")
	bool SetTrackProperty(int32 Track, const FString& Path, const FString& Value);

	/**
	 * Sets which struct an instanced-struct field holds - a Stagger (Path "Expander": "EachChild", "Each Letter, Word or
	 * Line"), a Value Input ("ValueInput"), a registered type's settings ("TypeParams") - at its defaults. Empty clears it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Scripting")
	bool SetTrackStruct(int32 Track, const FString& Path, const FString& StructType);

	/** The structs an instanced-struct field of a track can hold, by name ("Expander": the Staggers). */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|Scripting")
	TArray<FString> GetTrackStructOptions(const FString& Path) const;

	/** The names of the fields at Path of a track - its own with an empty Path. */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|Scripting")
	TArray<FString> GetTrackPropertyNames(int32 Track, const FString& Path) const;

	// ---------------------------------------------------------------------------------------------
	// Collapsed tracks
	// ---------------------------------------------------------------------------------------------

	/** Folds tracks into one collapsed track. Returns its name - numbered if another collapsed track has Name - or empty. */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Scripting")
	FString CollapseTracks(const TArray<int32>& Tracks, const FString& Name);

	/** Lets a collapsed track's tracks out again. */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Scripting")
	bool ExpandCollapsed(const FString& Name);

	UFUNCTION(BlueprintPure, Category = "Widget Animation|Scripting")
	TArray<FString> GetCollapsedNames() const;

	UFUNCTION(BlueprintPure, Category = "Widget Animation|Scripting")
	TArray<int32> GetCollapsedTracks(const FString& Name) const;

	// ---------------------------------------------------------------------------------------------
	// Widgets - read from the designer's preview, or the Widget Blueprint itself
	// ---------------------------------------------------------------------------------------------

	/** Every widget a track can move, as paths ("Border_0", or "Nested/Inner" inside a nested User Widget), depth first. */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|Scripting")
	TArray<FString> GetWidgetPaths() const;

	/** The top widget of the tree, as a path; empty when there is no tree. */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|Scripting")
	FString GetRootWidget() const;

	/** A widget's path from its path or its name alone (the first found, top-level widgets first). Empty when there is none. */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|Scripting")
	FString FindWidget(const FString& NameOrPath) const;

	UFUNCTION(BlueprintPure, Category = "Widget Animation|Scripting")
	FString GetWidgetClass(const FString& Path) const;

	UFUNCTION(BlueprintPure, Category = "Widget Animation|Scripting")
	FString GetWidgetParent(const FString& Path) const;

	UFUNCTION(BlueprintPure, Category = "Widget Animation|Scripting")
	TArray<FString> GetWidgetChildren(const FString& Path) const;

	/** A widget's place among its parent's children, from 0; -1 for the top widget, or one not found. */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|Scripting")
	int32 GetWidgetIndex(const FString& Path) const;

	// ---------------------------------------------------------------------------------------------
	// Widgets - changing the Widget Blueprint's own designer tree
	//
	// What the designer does by hand, done to the Widget Blueprint the panel previews on: add a widget, copy one (with
	// everything inside it and its slot's settings, as Ctrl+D does), move or remove one, set a widget's properties or its
	// slot's. Each is undoable - one step with the rest of a BeginEdit - and the Widget Blueprint is marked changed once
	// the edit ends, which has the designer rebuild its preview. Widgets inside a nested User Widget belong to that Widget
	// Blueprint and aren't changed here. Paths are as the widget functions above give them; a name alone works too.
	// ---------------------------------------------------------------------------------------------

	/** Whether there is a Widget Blueprint to change: false for a WA_ asset edited on its own. */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|Scripting")
	bool CanEditWidgets() const;

	/**
	 * A new widget of WidgetClass - a widget class by name ("Border", "UniformGridPanel", "TextBlock"), or a Widget Blueprint
	 * by name or path - added to Parent (a panel, or a widget that takes one child) at Index (-1: last), named Name (or after
	 * its class), numbered if taken. An empty Parent makes it the top widget of an empty Widget Blueprint. Its path, or empty.
	 */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Scripting")
	FString AddWidget(const FString& WidgetClass, const FString& Parent, const FString& Name, int32 Index);

	/**
	 * Count copies of Source - everything inside it, and its slot's settings - added to Parent (empty: Source's parent) at
	 * Index (-1: after the last child), named after Name or Source and numbered. Their paths, or none.
	 */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Scripting")
	TArray<FString> CopyWidget(const FString& Source, int32 Count, const FString& Parent, int32 Index, const FString& Name);

	/** Removes widgets and everything inside them. Tracks and groups moving them are left for the caller. */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Scripting")
	bool RemoveWidgets(const TArray<FString>& Widgets);

	/** Moves a widget into Parent (empty: where it is) at Index (-1: last). */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Scripting")
	bool MoveWidget(const FString& Widget, const FString& Parent, int32 Index);

	/** A property of a widget - or, bSlot, of its slot in its parent - as text, by name or dotted path. See GetTrackProperty. */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|Scripting")
	FString GetWidgetProperty(const FString& Widget, const FString& Path, bool bSlot) const;

	UFUNCTION(BlueprintPure, Category = "Widget Animation|Scripting")
	FString GetWidgetPropertyKind(const FString& Widget, const FString& Path, bool bSlot) const;

	/** Sets a property of a widget or, bSlot, of its slot, from text - as SetTrackProperty reads it. */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Scripting")
	bool SetWidgetProperty(const FString& Widget, const FString& Path, const FString& Value, bool bSlot);

	UFUNCTION(BlueprintPure, Category = "Widget Animation|Scripting")
	TArray<FString> GetWidgetPropertyNames(const FString& Widget, const FString& Path, bool bSlot) const;

	/** The class of a widget's slot - UniformGridSlot, HorizontalBoxSlot - or empty for the top widget. */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|Scripting")
	FString GetSlotClass(const FString& Widget) const;

	// ---------------------------------------------------------------------------------------------
	// The panel's selection
	// ---------------------------------------------------------------------------------------------

	/** The tracks selected in the panel this animation came from, in sequence order. Empty without one. */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|Scripting")
	TArray<int32> GetSelectedTracks() const;

	/** The group selected in that panel - or the selected track's - or -1. */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|Scripting")
	int32 GetSelectedGroup() const;

private:

	friend struct FWidgetAnimScriptEdit;
	friend struct FWidgetAnimScriptWidgetEdit;

	/** The Widget Blueprint whose designer tree is read and changed, or null. */
	UWidgetBlueprint* GetWidgetBlueprint() const;

	/** A widget of the Widget Blueprint's own tree by path or name - not one inside a nested User Widget. Null, with the error set, if none. */
	UWidget* FindEditableWidget(const FString& PathOrName) const;

	/** The widget or slot a property path starts from. */
	UObject* GetPropertyOwner(const FString& Widget, bool bSlot) const;

	/** A widget class by name or path. Null, with the error set, if none - or one that can't go in this Widget Blueprint. */
	UClass* FindWidgetClass(const FString& Name) const;

	/** A free name for a new widget, from Base: Base itself first when bTryBase, then Base_1, Base_2... */
	FName MakeWidgetName(const FString& Base, bool bTryBase) const;

	/** A copy of Source and everything inside it, in the Widget Blueprint's tree, not yet in a parent. */
	UWidget* CloneWidget(UWidget* Source, const FString& BaseName, bool bTryBase);

	/** Puts Widget in Parent at Index, or as the top widget when Parent is null. False, with the error set, if it can't go there. */
	bool PlaceWidget(UWidget* Widget, UPanelWidget* Parent, int32 Index);

	/** The Widget Blueprint marked changed, so the designer rebuilds its preview. */
	void NotifyWidgetsChanged();

	/** The groups and tracks being edited, or null when there is nothing to edit. */
	TArray<FWidgetAnimLayerGroup>* GetGroupsArray() const;
	TArray<FWidgetAnimLayer>* GetLayersArray() const;
	UObject* GetEditedObject() const;

	bool Fail(const FString& Error) const;
	bool CheckTrack(int32 Track) const;
	bool CheckGroup(int32 Group) const;

	/** Whether a track takes edits - false for a locked one, with the error set. */
	bool CheckTrackEditable(int32 Track) const;

	/** The panel source's previews and edits told of a change - or, editing an asset directly, the asset. */
	void Notify(EPropertyChangeType::Type ChangeType);

	// Placement
	//
	// Where tracks start is worked out as the panel works it out: a Stagger reaching as far as the preview fans it out, a
	// length the preview works out on the widget (a text typed at its Typing Speed). Those counts come from the source's
	// last tick, by track index, so they are copied once per frame and kept in step with every track added or removed here
	// - a script adds and removes many in one frame, long before the source ticks again.

	struct FPlacementInfo
	{
		int32 Count = INDEX_NONE;
		float PreparedRun = -1.f;
	};

	void EnsurePlacementInfo() const;
	float GetPlacementExtra(int32 Track) const;
	void GetExtras(TArray<float>& OutExtras) const;
	void GetStarts(TArray<float>& OutStarts) const;

	/**
	 * Puts every track back where it was - OldStarts, by the index it had then (OldIndexOf, INDEX_NONE for a track that is
	 * new or is to be placed by its own Starts and Delay) - by rewriting Delays. OldExtras are what the tracks reached past
	 * their own length then. See SWidgetAnimTimeline::PinLayerStarts, which this mirrors.
	 */
	void PinStarts(const TArray<float>& OldStarts, const TArray<float>& OldExtras, const TArray<int32>& OldIndexOf);

	mutable TArray<FPlacementInfo> PlacementInfo;
	mutable uint64 PlacementFrame = MAX_uint64;

	/** Widgets: the tree to read, gathered with each widget's parent. */
	struct FWidgetEntry
	{
		FString Path;
		FString Parent;
		UWidget* Widget = nullptr;
	};
	void GatherWidgets(TArray<FWidgetEntry>& OutWidgets) const;
	UWidget* GetTreeRoot() const;

	/** Where Target names a widget a track or group can move: its option path, "" for the widget itself. False if it names none. */
	bool ResolveTarget(const FString& Target, FString& OutPath) const;

	TWeakPtr<IWidgetAnimPanelSource> Source;
	TWeakPtr<SWidgetAnimTimeline> Panel;

	UPROPERTY()
	TObjectPtr<UWidgetAnimPresetAsset> Asset;

	mutable FString LastError;

	/** Open BeginEdit calls, the transaction they hold, and whether anything changed inside them - told when the last one ends. */
	int32 EditDepth = 0;
	TUniquePtr<FScopedTransaction> EditTransaction;
	bool bChangedInEdit = false;
	bool bWidgetsChangedInEdit = false;
};

/**
 * Getting an animation to script, and the handshake with the Python half for the panel's Run. See UWidgetAnimScriptAnimation.
 */
UCLASS(meta = (DisplayName = "Widget Animation Scripting"))
class UWidgetAnimScriptLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/** The animation in the Widget Animations panel used last - hovered or typed in - or null if none is open. */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Scripting")
	static UWidgetAnimScriptAnimation* GetOpenAnimation();

	/** A Widget Animation asset, edited directly: no panel needed, and no widgets to read. */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Scripting")
	static UWidgetAnimScriptAnimation* EditAnimationAsset(UWidgetAnimPresetAsset* Animation);

	// The panel's Run: the script and the animation it runs on, handed to the Python half, and any error it ends with.

	UFUNCTION(BlueprintPure, Category = "Widget Animation|Scripting|Panel")
	static FString GetPanelScriptCode();

	UFUNCTION(BlueprintPure, Category = "Widget Animation|Scripting|Panel")
	static UWidgetAnimScriptAnimation* GetPanelScriptAnimation();

	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Scripting|Panel")
	static void ReportPanelScriptError(const FString& Message, int32 Line);

	/** As ReportPanelScriptError, with the file it stopped in: empty for the script, else one of your function files. */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Scripting|Panel")
	static void ReportPanelScriptErrorIn(const FString& Message, int32 Line, const FString& File);
};

/** The panel side of scripting: which panel was used last, running a script, and Python itself - reached without linking to it. */
namespace WidgetAnimScripting
{
	/** Called by a panel the user is working in, so GetOpenAnimation finds it. */
	void NotePanelUsed(const TSharedRef<SWidgetAnimTimeline>& Panel);

	/** The panel showing Source that was worked in last - null when none is open. */
	TSharedPtr<SWidgetAnimTimeline> FindLastUsedPanelFor(const IWidgetAnimPanelSource* Source);

	/** The panel of Blueprint's editor that was worked in last - its drawer's or its docked tab's - null when none is open. */
	TSharedPtr<SWidgetAnimTimeline> FindLastUsedPanelForBlueprint(const UWidgetBlueprint* Blueprint);

	/** Every Widget Animations panel still open, the one worked in last at the end. */
	void GetOpenPanels(TArray<TSharedPtr<SWidgetAnimTimeline>>& OutPanels);

	/** Whether Python can run here: the Python Editor Script Plugin loaded and working. */
	bool IsPythonAvailable();

	/** One line of what a run printed: Python's own output, its warnings and its errors. */
	struct FOutputLine
	{
		enum class EKind : uint8 { Info, Warning, Error };
		EKind Kind = EKind::Info;
		FString Text;
	};

	/** What a run of the panel's script came to. */
	struct FRunResult
	{
		bool bRan = false;
		bool bFailed = false;
		FString Error;
		int32 ErrorLine = 0;

		/** Where the error line is: empty for the script, else the full path of the function file it stopped in. */
		FString ErrorFile;
		TArray<FOutputLine> Output;
		int32 TracksBefore = 0;
		int32 TracksAfter = 0;
		int32 GroupsBefore = 0;
		int32 GroupsAfter = 0;
		bool bRolledBack = false;
		double Seconds = 0.0;
	};

	/**
	 * Runs Code on the animation Panel shows, with anim and selection set up for it, as one undoable edit. A script that
	 * stops with an error is undone: nothing it changed stays.
	 */
	FRunResult RunPanelScript(const TSharedRef<SWidgetAnimTimeline>& Panel, const FString& Code);

	/** Runs Python and collects what it printed. False if Python isn't available, or the command raised an error. */
	bool RunPython(const FString& Command, TArray<FOutputLine>& OutOutput);

	/** Runs Call - "widget_anim_scripting.print_api_reference()" - with the widget_anim_scripting module imported first. */
	bool RunScriptingCall(const FString& Call, TArray<FOutputLine>& OutOutput);

	/** The folders snippet templates are read from: the plugin's own, then the project's. */
	void GetSnippetFolders(TArray<FString>& OutFolders);

	/** Where your own snippets are saved: the project's Content/Python/WidgetAnimSnippets. */
	FString GetProjectSnippetFolder();

	/** Your functions file - the project's Content/Python/WidgetAnimFunctions/my_functions.py - made from the plugin's starter if it isn't there. */
	FString GetFunctionsFile(bool bCreate);
}
