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

	// -------------------------------------------------------------------------------------------------

	/** The color a layer type's tracks are drawn in: the user's, or the default for any type not set. */
	FLinearColor GetTrackColor(EWidgetAnimType AnimType) const;

	/** The color NameTBDColors.h gives a layer type - what Track Colors starts from. */
	static FLinearColor GetDefaultTrackColor(EWidgetAnimType AnimType);

	void SetTrackSnapping(bool bEnabled);
	void SetLinkedEdges(bool bEnabled);

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