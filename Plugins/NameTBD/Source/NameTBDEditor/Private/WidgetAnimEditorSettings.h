// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "NameTBD/WidgetAnimation/WidgetAnimLibrary.h"

#include "WidgetAnimEditorSettings.generated.h"

/** What a track's text does in the Track Editor once the track is drawn too short for it. */
UENUM()
enum class EWidgetAnimTrackTextZoomOut : uint8
{
	/** Drawn smaller, down to half its size, so it still fits; left out once even that doesn't. */
	Shrink UMETA(DisplayName = "Shrink To Fit"),

	/** Left out as soon as it doesn't fit at its full size. */
	Disappear
};

/** The colors of the Script area's code editor. */
UENUM()
enum class EWidgetAnimScriptEditorTheme : uint8
{
	/** The panel's own: its dark background, and the animation orange for the scripting API's names. */
	Unreal UMETA(DisplayName = "Unreal"),

	Dark UMETA(DisplayName = "VS Code Dark"),
	Light UMETA(DisplayName = "VS Code Light"),
	HighContrast UMETA(DisplayName = "High Contrast")
};

/** How a dragged key or track follows the mouse in the Track Editor - the Drag control on the Track Editor's strip. */
UENUM()
enum class EWidgetAnimDragStepping : uint8
{
	/** Wherever the mouse is (snapping, when Snapping is on, aside). */
	FollowCursor UMETA(DisplayName = "Follow Cursor"),

	/** The mouse, in whole steps of Drag Step: a key lands on the step grid, a track moves by whole steps. */
	Steps UMETA(DisplayName = "Steps")
};

/** How much of a motion path is drawn on the designer's canvas. */
UENUM()
enum class EWidgetAnimMotionPathRange : uint8
{
	/** The whole pass, from the animation's start to its end. */
	WholeAnimation UMETA(DisplayName = "Whole Animation"),

	/** Only the stretch around the playhead - Seconds Around Playhead before it and after it - which follows it as it plays. */
	AroundPlayhead UMETA(DisplayName = "Around Playhead")
};

/**
 * The Widget Animations panel's per-user preferences: Editor Preferences > Plugins > Widget Animations Editor.
 *
 * Per user and per project (EditorPerProjectUserSettings), like the rest of the editor's layout preferences,
 * so one person's track colors never land in anyone else's editor. The Track Controls toggles in the panel
 * write here too, so they are remembered between sessions.
 */
UCLASS(config = EditorPerProjectUserSettings, meta = (DisplayName = "Widget Animations Editor"))
class UWidgetAnimEditorSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:

	UWidgetAnimEditorSettings();

	// -------------------------------------------------------------------------------------------------
	// Track colors
	// -------------------------------------------------------------------------------------------------

	/**
	 * The color of each track type: its sections in the Track Editor, and the accent on their tooltips. Reset
	 * to Default puts back the colors from NameTBDColors.h; a type taken out of the list is drawn in its
	 * default color, and put back in it the next time the editor starts.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Track Colors", meta = (DisplayName = "Track Colors", ReadOnlyKeys))
	TMap<EWidgetAnimType, FLinearColor> TrackColors;

	/**
	 * The color of every Blueprint track type that doesn't set a Track Color of its own in its class defaults. Other
	 * registered types - Count Up, and any C++ type - use User-defined Track in Track Colors above.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Track Colors", meta = (DisplayName = "Blueprint Type Track Color"))
	FLinearColor BlueprintTypeTrackColor;

	/**
	 * The color of every cue track - Event, Sound, Play Animation, and each Blueprint cue that doesn't set a Track Color
	 * of its own - so what makes something happen reads apart from what moves something.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Track Colors", meta = (DisplayName = "Cue Track Color"))
	FLinearColor CueTrackColor;

	// -------------------------------------------------------------------------------------------------
	// Previews
	// -------------------------------------------------------------------------------------------------

	/**
	 * Sound cues play while the Widget Animations panel's preview, or the designer toolbar's, plays - so a sound can be
	 * timed where it is heard. One cue sound at a time: a new one cuts off the last. Scrubbing never plays them, and no
	 * Event or Play Animation cue ever fires in a preview. See WidgetAnimCue.h.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Previews", meta = (DisplayName = "Play Cue Sounds In Previews"))
	bool bPlayCueSoundsInPreviews = true;

	// -------------------------------------------------------------------------------------------------
	// Track controls - the toggles in the panel's Track Controls pill
	// -------------------------------------------------------------------------------------------------

	/**
	 * A track or key being dragged snaps to the start and end of other tracks, the start and end of the
	 * animation, and the playhead, once it comes within Snap Distance of one. A line through the rows shows
	 * where it snapped.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Track Controls", meta = (DisplayName = "Track Snapping"))
	bool bTrackSnapping = true;

	/** How close, in pixels on screen, a dragged edge has to come to something before it snaps to it. */
	UPROPERTY(config, EditAnywhere, Category = "Track Controls", meta = (DisplayName = "Snap Distance", ClampMin = "2", ClampMax = "32", UIMin = "4", UIMax = "16", EditCondition = "bTrackSnapping"))
	float SnapDistancePixels = 8.f;

	/**
	 * Resizing a track also resizes the tracks below it whose start or end lines up exactly with the edge being
	 * dragged, so a sequence - one track ending where the next begins - stays joined, and tracks that end
	 * together keep ending together.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Track Controls", meta = (DisplayName = "Linked Edges"))
	bool bLinkedEdges = false;

	/**
	 * While the Widget Animations panel is on screen with an animation open, what the designer's Widget Gizmo does to a widget is
	 * keyed at the playhead - on the track that holds it, its key there or a key added there (see WidgetGizmo.h). Off: the gizmo
	 * always edits the widget as authored. The Auto Key toggle in the panel's Track Controls sets it too.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Track Controls", meta = (DisplayName = "Auto Key (Widget Gizmo)"))
	bool bGizmoAutoKey = true;

	/**
	 * Every track that travels from a start value to an end value shows its start and end as keys in the Track Editor - diamonds
	 * on its edges - not only once it has a key between them. Clicked, they show the start or end value in the Selection panel;
	 * dragged, they resize the track from that edge. Always on in a Dope Sheet (see WidgetAnimDopeSheet.h), whose keys are all
	 * there is.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Track Controls", meta = (DisplayName = "Start And End Keys"))
	bool bShowEdgeKeys = true;

	/** Whether tracks show their start and end as keys: Start And End Keys (a Dope Sheet always does - see SWidgetAnimTimeline::GetKeyTimes). */
	bool ShowsEdgeKeys() const { return bShowEdgeKeys; }

	/**
	 * Which controls the panel's controls pill shows: how the preview plays (Loop, Ping Pong, Invert, Speed) or how tracks behave
	 * (Snapping, Linked Edges, Auto Key, Motion Path). Switched by the button beside the pill; remembered.
	 */
	UPROPERTY(config)
	bool bControlsShowTrackControls = false;

	/**
	 * Moving selected tracks with the Left and Right arrow keys can take them past the animation's end - making it longer -
	 * or before its start, where the rest of the animation moves later to make room. Off, they stop at the animation's
	 * start and end, as a track dragged with the mouse does.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Track Controls", meta = (DisplayName = "Arrow Keys Can Extend the Animation"))
	bool bArrowKeysExtendAnimation = true;

	/**
	 * How a dragged key or track follows the mouse: wherever it is, or in whole steps of the Drag Step below - in the timeline's
	 * own unit. The Drag control on the Track Editor's strip sets it too. Left and Right always move the selected keys or tracks
	 * by one step (ten with Shift), whichever is picked.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Track Controls", meta = (DisplayName = "Drag"))
	EWidgetAnimDragStepping DragStepping = EWidgetAnimDragStepping::FollowCursor;

	/** The step of a seconds timeline: what Steps drags in, and what Left / Right move by. */
	UPROPERTY(config, EditAnywhere, Category = "Track Controls", meta = (DisplayName = "Drag Step (Seconds)", ClampMin = "0.001", ClampMax = "10", UIMin = "0.01", UIMax = "1", Units = "s"))
	float DragStepSeconds = 0.05f;

	/** The step of a percentage timeline, in percent of it. */
	UPROPERTY(config, EditAnywhere, Category = "Track Controls", meta = (DisplayName = "Drag Step (Percent)", ClampMin = "0.01", ClampMax = "100", UIMin = "0.5", UIMax = "25"))
	float DragStepPercent = 5.f;

	/** The step while Show Time In Frames is on, in frames of its Frame Rate. */
	UPROPERTY(config, EditAnywhere, Category = "Track Controls", meta = (DisplayName = "Drag Step (Frames)", ClampMin = "1", ClampMax = "1000", UIMin = "1", UIMax = "30"))
	int32 DragStepFrames = 1;

	/**
	 * Editing a key - its value, its curve, a Transform or Shape key's pose - jumps the preview to that key, so the change
	 * shows as it is made. With a track selected rather than a key, editing its start or end value jumps to its start or end.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Track Controls", meta = (DisplayName = "Jump to Edited Key"))
	bool bJumpToEditedKey = true;

	// -------------------------------------------------------------------------------------------------
	// Track display
	// -------------------------------------------------------------------------------------------------

	/**
	 * What a track's name, length and easing do in the Track Editor as it is zoomed out and the track gets too
	 * short to hold them: shrink to fit, down to half their size, or disappear. Either way, text that can't fit is
	 * left out rather than spilling past the track.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Track Display", meta = (DisplayName = "Track Text When Zoomed Out"))
	EWidgetAnimTrackTextZoomOut TrackTextZoomOut = EWidgetAnimTrackTextZoomOut::Shrink;

	/**
	 * The Track Editor's vertical scroll bar: shown while there are more tracks than fit, when this is on; never shown when
	 * it is off - the mouse wheel still scrolls them.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Track Display", meta = (DisplayName = "Show Vertical Scroll Bar When Necessary"))
	bool bShowVerticalScrollBarWhenNeeded = true;

	/**
	 * The Track Editor's horizontal scroll bar: shown while the view shows less than the whole animation, when this is on;
	 * never shown when it is off - Shift + wheel, a right-drag, or Fit still move the view.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Track Display", meta = (DisplayName = "Show Horizontal Scroll Bar When Necessary"))
	bool bShowHorizontalScrollBarWhenNeeded = true;

	/**
	 * How tall the Track Editor's lines are, in slate units. Dragging any line's edge in the Track Editor sets it too, for
	 * every panel.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Track Display", meta = (DisplayName = "Default Track Height", ClampMin = "18", ClampMax = "64", UIMin = "18", UIMax = "64"))
	float DefaultTrackHeight = 33.f;

	/**
	 * Times in the Widget Animations panel - the ruler, the playhead, spans, keys - read in frames at Frame Rate rather than in
	 * seconds: the Time In dropdown's Frames (Preview Only). Only how the panel shows time: tracks are still stored and played in
	 * seconds, at whatever rate the game runs. Never on a percentage animation, which has no seconds to count frames of.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Track Display", meta = (DisplayName = "Show Time In Frames"))
	bool bShowTimeInFrames = false;

	/** The frame rate Show Time In Frames counts frames at - a target to author against, not a rate anything plays at. */
	UPROPERTY(config, EditAnywhere, Category = "Track Display", meta = (DisplayName = "Frame Rate", ClampMin = "1", ClampMax = "1000", UIMin = "12", UIMax = "240", EditCondition = "bShowTimeInFrames"))
	float PreviewFrameRate = 60.f;

	// -------------------------------------------------------------------------------------------------
	// Motion paths - where the animated widgets travel, drawn over the designer's canvas
	// -------------------------------------------------------------------------------------------------

	/**
	 * While the Widget Animations panel has an animation open - on screen or not - every widget the animation moves shows the path
	 * it travels along on the designer's canvas, with nothing to select: a line through every moment of the animation, a dot at
	 * every frame (close together where it moves slowly, far apart where it moves fast), and a diamond at every key, dragged to
	 * move a Transform key, Alt-dragged to pull out curve handles, right-clicked for its Path and the track's Easing Across.
	 * The Motion Path toggle in the panel's Track Controls sets it too.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Motion Paths", meta = (DisplayName = "Show Motion Paths"))
	bool bShowMotionPaths = true;

	/**
	 * How many dots a motion path has per second of the animation - one per frame at this rate, so their spacing shows the
	 * speed. With Show Time In Frames on, there is a dot at every frame of its Frame Rate instead.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Motion Paths", meta = (DisplayName = "Dots Per Second", ClampMin = "1", ClampMax = "240", UIMin = "6", UIMax = "120", EditCondition = "bShowMotionPaths"))
	float MotionPathDotsPerSecond = 30.f;

	/** How much of the path is drawn: the whole animation, or only the stretch around the playhead. */
	UPROPERTY(config, EditAnywhere, Category = "Motion Paths", meta = (DisplayName = "Path Range", EditCondition = "bShowMotionPaths"))
	EWidgetAnimMotionPathRange MotionPathRange = EWidgetAnimMotionPathRange::WholeAnimation;

	/** With Path Range Around Playhead: how many seconds of the path are drawn before the playhead, and how many after it. */
	UPROPERTY(config, EditAnywhere, Category = "Motion Paths", meta = (DisplayName = "Seconds Around Playhead", ClampMin = "0.05", ClampMax = "30", UIMin = "0.1", UIMax = "3", Units = "s", EditCondition = "bShowMotionPaths && MotionPathRange == EWidgetAnimMotionPathRange::AroundPlayhead", EditConditionHides))
	float MotionPathAroundSeconds = 0.5f;

	/** Each key's time written beside its diamond on the path - otherwise only the key under the mouse says when it is. */
	UPROPERTY(config, EditAnywhere, Category = "Motion Paths", meta = (DisplayName = "Show Key Times", EditCondition = "bShowMotionPaths"))
	bool bMotionPathKeyTimes = false;

	/**
	 * While motion paths are drawn, a click on the designer only ever reaches them: widgets can't be selected there (no click,
	 * double click or marquee), so a widget under a key or handle never takes the click meant for it. Panning and zooming the
	 * designer, and selecting widgets in the Hierarchy, still work. The Paths Only toggle beside Motion Path in the panel's
	 * Track Controls sets it too.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Motion Paths", meta = (DisplayName = "Paths Only (No Widget Selection)", EditCondition = "bShowMotionPaths"))
	bool bMotionPathPathsOnly = false;

	// -------------------------------------------------------------------------------------------------
	// New tracks
	// -------------------------------------------------------------------------------------------------

	/** How long a track added with Add Animation Track runs, in seconds - its Duration, before anything changes it. */
	UPROPERTY(config, EditAnywhere, Category = "New Tracks", meta = (DisplayName = "Default Track Length", ClampMin = "0.01", UIMin = "0.05", UIMax = "5.0", Units = "s"))
	float DefaultTrackLength = 0.3f;

	// -------------------------------------------------------------------------------------------------
	// Script editor - the code editor in the panel's Script area
	// -------------------------------------------------------------------------------------------------

	/**
	 * Scripts are written in a code editor - Monaco, the editor in VS Code: find and replace, multi-cursor, bracket matching,
	 * folding, and suggestions for the scripting API. Off: the plain code box, which only colors the code. The plain box is
	 * used anyway where the code editor can't run.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Script Editor", meta = (DisplayName = "Use Code Editor"))
	bool bUseCodeEditor = true;

	/** The code editor's colors. Unreal matches the panel; the others are VS Code's own. */
	UPROPERTY(config, EditAnywhere, Category = "Script Editor", meta = (DisplayName = "Theme", EditCondition = "bUseCodeEditor"))
	EWidgetAnimScriptEditorTheme ScriptEditorTheme = EWidgetAnimScriptEditorTheme::Unreal;

	/** The code editor's text size, in pixels. Ctrl + mouse wheel in the editor changes it for now; this is what it starts at. */
	UPROPERTY(config, EditAnywhere, Category = "Script Editor", meta = (DisplayName = "Font Size", ClampMin = "8", ClampMax = "32", EditCondition = "bUseCodeEditor"))
	int32 ScriptEditorFontSize = 13;

	/** The code editor's fonts, first choice first, as in CSS. Fonts not installed are skipped. */
	UPROPERTY(config, EditAnywhere, Category = "Script Editor", meta = (DisplayName = "Font", EditCondition = "bUseCodeEditor"))
	FString ScriptEditorFontFamily = TEXT("'Cascadia Code', Consolas, 'Courier New', monospace");

	/** How many spaces Tab puts in, and how deep a block indents. Tabs are always spaces, as Python is written. */
	UPROPERTY(config, EditAnywhere, Category = "Script Editor", meta = (DisplayName = "Tab Size", ClampMin = "1", ClampMax = "8", EditCondition = "bUseCodeEditor"))
	int32 ScriptEditorTabSize = 4;

	UPROPERTY(config, EditAnywhere, Category = "Script Editor", meta = (DisplayName = "Line Numbers", EditCondition = "bUseCodeEditor"))
	bool bScriptEditorLineNumbers = true;

	/** The small overview of the whole script down the editor's right side. */
	UPROPERTY(config, EditAnywhere, Category = "Script Editor", meta = (DisplayName = "Minimap", EditCondition = "bUseCodeEditor"))
	bool bScriptEditorMinimap = false;

	/** Long lines wrap onto the next rather than running off to the right. */
	UPROPERTY(config, EditAnywhere, Category = "Script Editor", meta = (DisplayName = "Word Wrap", EditCondition = "bUseCodeEditor"))
	bool bScriptEditorWordWrap = false;

	/** Typing ( [ { or a quote puts in the closing one too. */
	UPROPERTY(config, EditAnywhere, Category = "Script Editor", meta = (DisplayName = "Close Brackets And Quotes", EditCondition = "bUseCodeEditor"))
	bool bScriptEditorAutoClose = true;

	/**
	 * Suggestions as you type: after "anim." its members, after "TrackType." the types, inside target="..." the Widget
	 * Blueprint's widgets - with a line of help for each, and more on hovering a name. Ctrl+Space asks for them anywhere.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Script Editor", meta = (DisplayName = "Suggestions", EditCondition = "bUseCodeEditor"))
	bool bScriptEditorSuggestions = true;

	// -------------------------------------------------------------------------------------------------
	// DEVELOPER ONLY
	//
	// Look-and-feel tunables of the Track Editor, set here while the plugin is being developed so they can be tried live.
	// Taken out before the plugin is published, with the values settled on written into NameTBDColors.h as their defaults.
	// -------------------------------------------------------------------------------------------------

	/**
	 * How big a Dope Sheet's keyframes are drawn - and grabbed - in the Track Editor, in pixels across the diamond. Their outline and
	 * selection glow grow with them. Not the keys of a timeline of tracks, which follow the line height.
	 */
	UPROPERTY(config, EditAnywhere, Category = "DEVELOPER ONLY", meta = (DisplayName = "Dope Sheet Key Size", ClampMin = "6", ClampMax = "40", UIMin = "6", UIMax = "32"))
	float DopeSheetKeySize = 14.f;

	/** The animation's start line and its triangle in the ruler. */
	UPROPERTY(config, EditAnywhere, Category = "DEVELOPER ONLY", meta = (DisplayName = "Start Color"))
	FLinearColor RangeStartColor;

	/** The animation's end line and its triangle in the ruler. */
	UPROPERTY(config, EditAnywhere, Category = "DEVELOPER ONLY", meta = (DisplayName = "End Color"))
	FLinearColor RangeEndColor;

	/** How thick the start and end lines are drawn through the tracks, in pixels. */
	UPROPERTY(config, EditAnywhere, Category = "DEVELOPER ONLY", meta = (DisplayName = "Start / End Line Thickness", ClampMin = "1", ClampMax = "6", UIMin = "1", UIMax = "6"))
	float RangeLineThickness = 2.f;

	/** How opaque the start and end lines are, 0 to 1. Their triangles are always solid. */
	UPROPERTY(config, EditAnywhere, Category = "DEVELOPER ONLY", meta = (DisplayName = "Start / End Line Opacity", ClampMin = "0", ClampMax = "1"))
	float RangeLineOpacity = 1.f;

	/** How wide the start and end triangles are across their top, in pixels - also how wide they are grabbed. */
	UPROPERTY(config, EditAnywhere, Category = "DEVELOPER ONLY", meta = (DisplayName = "Start / End Triangle Width", ClampMin = "4", ClampMax = "24"))
	float RangeTriangleWidth = 12.f;

	/** How tall the start and end triangles are, in pixels. Kept inside the ruler's bottom strip, under the playhead's head. */
	UPROPERTY(config, EditAnywhere, Category = "DEVELOPER ONLY", meta = (DisplayName = "Start / End Triangle Height", ClampMin = "3", ClampMax = "8"))
	float RangeTriangleHeight = 7.f;

	/** How far toward white a start or end triangle turns while hovered or dragged, 0 to 1. */
	UPROPERTY(config, EditAnywhere, Category = "DEVELOPER ONLY", meta = (DisplayName = "Start / End Hover Lighten", ClampMin = "0", ClampMax = "1"))
	float RangeHoverLighten = 0.35f;

	/** The playhead - its head in the ruler and its line - in the Track Editor and the Graph Editor. */
	UPROPERTY(config, EditAnywhere, Category = "DEVELOPER ONLY", meta = (DisplayName = "Playhead Color"))
	FLinearColor PlayheadColor;

	/** How thick the playhead's line is, in pixels. */
	UPROPERTY(config, EditAnywhere, Category = "DEVELOPER ONLY", meta = (DisplayName = "Playhead Line Thickness", ClampMin = "1", ClampMax = "6"))
	float PlayheadLineThickness = 1.f;

	/** How opaque the playhead's line is, 0 to 1. Its head is always solid. */
	UPROPERTY(config, EditAnywhere, Category = "DEVELOPER ONLY", meta = (DisplayName = "Playhead Line Opacity", ClampMin = "0", ClampMax = "1"))
	float PlayheadLineOpacity = 0.95f;

	/** The shade over the Track Editor before the animation's start and after its end. */
	UPROPERTY(config, EditAnywhere, Category = "DEVELOPER ONLY", meta = (DisplayName = "Outside Animation Shade"))
	FLinearColor OutOfRangeShade;

	/** The Track Editor's and Graph Editor's grid lines at each labeled time, and between them. */
	UPROPERTY(config, EditAnywhere, Category = "DEVELOPER ONLY", meta = (DisplayName = "Grid Major Color"))
	FLinearColor GridMajorColor;

	UPROPERTY(config, EditAnywhere, Category = "DEVELOPER ONLY", meta = (DisplayName = "Grid Minor Color"))
	FLinearColor GridMinorColor;

	/** The icon before a cue track's name - the Event track's event, the Sound track's speaker - and its tint. */
	UPROPERTY(config, EditAnywhere, Category = "DEVELOPER ONLY", meta = (DisplayName = "Cue Track Icon Size", ClampMin = "8", ClampMax = "24"))
	float CueTrackIconSize = 12.f;

	UPROPERTY(config, EditAnywhere, Category = "DEVELOPER ONLY", meta = (DisplayName = "Cue Track Icon Tint"))
	FLinearColor CueTrackIconTint;

	/** The State Machine editor's play / pause button, floating at the graph's bottom left: the outline round its dark box. */
	UPROPERTY(config, EditAnywhere, Category = "DEVELOPER ONLY", meta = (DisplayName = "State Machine Play / Pause Outline"))
	FLinearColor MachinePlayPauseOutline;

	/**
	 * The glow round the state in play, its selection outline and the play / pause button's outline are each one closed
	 * antialiased line, which dims where its two ends meet (at the middle of the bottom edge). How far, in pixels, each line
	 * runs on past its start so its ends overlap instead - 0: they just meet, as before.
	 */
	UPROPERTY(config, EditAnywhere, Category = "DEVELOPER ONLY", meta = (DisplayName = "State Machine Ring Seam Overlap", ClampMin = "0", ClampMax = "6", UIMin = "0", UIMax = "6"))
	float MachineRingSeamOverlap = 1.f;

	/** The background of the switch beside the controls pill - Preview Controls / Track Controls. Its alpha is how solid it is. */
	UPROPERTY(config, EditAnywhere, Category = "DEVELOPER ONLY", meta = (DisplayName = "Controls Switch Color"))
	FLinearColor ControlsSwitchColor;

	/** The switch's side that is shown (lit). */
	UPROPERTY(config, EditAnywhere, Category = "DEVELOPER ONLY", meta = (DisplayName = "Controls Switch Selected Color"))
	FLinearColor ControlsSwitchSelectedColor;

	/** The switch's side under the mouse. */
	UPROPERTY(config, EditAnywhere, Category = "DEVELOPER ONLY", meta = (DisplayName = "Controls Switch Hovered Color"))
	FLinearColor ControlsSwitchHoveredColor;

	/** The background of the controls pill (Loop, Ping Pong... / Snapping, Linked Edges...). */
	UPROPERTY(config, EditAnywhere, Category = "DEVELOPER ONLY", meta = (DisplayName = "Controls Background Color"))
	FLinearColor ControlsBackgroundColor;

	/** A control of the pill that is on (lit). */
	UPROPERTY(config, EditAnywhere, Category = "DEVELOPER ONLY", meta = (DisplayName = "Controls Selected Color"))
	FLinearColor ControlsSelectedColor;

	/** A control of the pill under the mouse. */
	UPROPERTY(config, EditAnywhere, Category = "DEVELOPER ONLY", meta = (DisplayName = "Controls Hovered Color"))
	FLinearColor ControlsHoveredColor;

	// -------------------------------------------------------------------------------------------------

	/** The color a layer type's tracks are drawn in: the user's, or the default for any type not set. */
	FLinearColor GetTrackColor(EWidgetAnimType AnimType) const;

	/** The color NameTBDColors.h gives a layer type - what Track Colors starts from. */
	static FLinearColor GetDefaultTrackColor(EWidgetAnimType AnimType);

	void SetTrackSnapping(bool bEnabled);
	void SetLinkedEdges(bool bEnabled);

	/** Drag and its steps, saved - see DragStepping. */
	void SetDragStepping(EWidgetAnimDragStepping Stepping);
	void SetDragStepSeconds(float Seconds);
	void SetDragStepPercent(float Percent);
	void SetDragStepFrames(int32 Frames);

	/** Show Time In Frames and its Frame Rate, saved - see bShowTimeInFrames. */
	void SetShowTimeInFrames(bool bEnabled);
	void SetPreviewFrameRate(float FrameRate);

	/** Show Motion Paths, saved - see bShowMotionPaths. */
	void SetShowMotionPaths(bool bEnabled);

	/** Auto Key, saved - see bGizmoAutoKey. */
	void SetGizmoAutoKey(bool bEnabled);

	/** Which controls the pill shows, saved - see bControlsShowTrackControls. */
	void SetControlsShowTrackControls(bool bTrackControls);

	/** Paths Only, saved - see bMotionPathPathsOnly. */
	void SetMotionPathPathsOnly(bool bEnabled);

	static const UWidgetAnimEditorSettings& Get() { return *GetDefault<UWidgetAnimEditorSettings>(); }
	static UWidgetAnimEditorSettings& GetMutable() { return *GetMutableDefault<UWidgetAnimEditorSettings>(); }

	// UDeveloperSettings
	virtual FName GetContainerName() const override { return TEXT("Editor"); }
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
	virtual FName GetSectionName() const override { return TEXT("WidgetAnimationsEditor"); }

#if WITH_EDITOR
	virtual FText GetSectionText() const override;
	virtual FText GetSectionDescription() const override;
#endif

	/** Every layer type gets an entry, including types added after the settings were first saved. */
	virtual void PostInitProperties() override;

private:

	/** Adds a default color for any layer type the map doesn't have yet. */
	void FillMissingTrackColors();
};