// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "NameTBD/WidgetAnimation/WidgetAnimLibrary.h"

#include "WidgetAnimEditorSettings.generated.h"

/**
 * The Widget Animations panel's per-user preferences: Editor Preferences > Plugins > Widget Animations Editor.
 *
 * Per user and per project (EditorPerProjectUserSettings), like the rest of the editor's layout preferences,
 * so one person's track colours never land in anyone else's editor. The Track Controls toggles in the panel
 * write here too, so they are remembered between sessions.
 */
UCLASS(config = EditorPerProjectUserSettings, meta = (DisplayName = "Widget Animations Editor"))
class UWidgetAnimEditorSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:

	UWidgetAnimEditorSettings();

	// -------------------------------------------------------------------------------------------------
	// Track colours
	// -------------------------------------------------------------------------------------------------

	/**
	 * The colour of each track type: its sections in the Track Editor, and the accent on their tooltips. Reset
	 * to Default puts back the colours from NameTBDColors.h; a type taken out of the list is drawn in its
	 * default colour, and put back in it the next time the editor starts.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Track Colors", meta = (DisplayName = "Track Colors", ReadOnlyKeys))
	TMap<EWidgetAnimType, FLinearColor> TrackColors;

	/**
	 * The colour of every Blueprint track type that doesn't set a Track Color of its own in its class defaults. Other
	 * registered types - Count Up, and any C++ type - use Registered Type in Track Colors above.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Track Colors", meta = (DisplayName = "Blueprint Type Track Color"))
	FLinearColor BlueprintTypeTrackColor;

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

	/** The colour a layer type's tracks are drawn in: the user's, or the default for any type not set. */
	FLinearColor GetTrackColor(EWidgetAnimType AnimType) const;

	/** The colour NameTBDColors.h gives a layer type - what Track Colors starts from. */
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

	/** Adds a default colour for any layer type the map doesn't have yet. */
	void FillMissingTrackColors();
};