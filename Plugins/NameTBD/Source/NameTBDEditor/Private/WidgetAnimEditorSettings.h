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
	 * registered types - Count Up, and any C++ type - use Registered Type in Track Colors above.
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
	 * Moving selected tracks with the Left and Right arrow keys can take them past the animation's end - making it longer -
	 * or before its start, where the rest of the animation moves later to make room. Off, they stop at the animation's
	 * start and end, as a track dragged with the mouse does.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Track Controls", meta = (DisplayName = "Arrow Keys Can Extend the Animation"))
	bool bArrowKeysExtendAnimation = true;

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

	/** The color a layer type's tracks are drawn in: the user's, or the default for any type not set. */
	FLinearColor GetTrackColor(EWidgetAnimType AnimType) const;

	/** The color NameTBDColors.h gives a layer type - what Track Colors starts from. */
	static FLinearColor GetDefaultTrackColor(EWidgetAnimType AnimType);

	void SetTrackSnapping(bool bEnabled);
	void SetLinkedEdges(bool bEnabled);

	/** Show Time In Frames and its Frame Rate, saved - see bShowTimeInFrames. */
	void SetShowTimeInFrames(bool bEnabled);
	void SetPreviewFrameRate(float FrameRate);

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