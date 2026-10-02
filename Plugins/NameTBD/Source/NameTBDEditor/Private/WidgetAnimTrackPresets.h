// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"

#include "NameTBD/WidgetAnimation/WidgetAnimLibrary.h"

/**
 * Track presets: tracks' settings - type, values, timing, easing, keys, Stagger, everything that says what they do - saved
 * under a name, to add again from Add Animation Track > From Preset, or put on a track from the Selection panel's Preset
 * dropdown.
 *
 * A preset is saved under the name it was given in the panel: one renamed track's name (FWidgetAnimLayer::TrackName), or a
 * renamed collapsed track's - every track in it, in the time they play relative to each other, added back as a collapsed
 * track of that name.
 *
 * One file per preset in <Project>/Content/WidgetAnimTrackPresets (a header line, then one line per track: its start, in
 * seconds after the first, and the track as exported text), so a team shares them through source control the way it shares
 * the project's snippets. Read afresh whenever they are listed, so a preset added on disk shows up without a restart.
 *
 * A preset never carries where its tracks were - Starts, Delay, group, Target Widget, line, collapsed track - nor their
 * lock, hidden or pinned state.
 */
namespace WidgetAnimTrackPresets
{
	struct FPreset
	{
		/** As shown: the file's name, without its extension - the track's or collapsed track's name it was saved from. */
		FString Name;

		/** The full path of its file. */
		FString File;

		/** Its tracks, in order - one for a single track's preset - each with its start, in authored seconds after the first's. */
		TArray<FWidgetAnimLayer> Layers;
		TArray<float> Starts;

		/** Saved from a collapsed track: added back as one, named after the preset. */
		bool bCollapsed = false;
	};

	/** <Project>/Content/WidgetAnimTrackPresets, full path. Made when bCreate and it doesn't exist yet. */
	FString GetFolder(bool bCreate = false);

	/** Every preset that reads, by name. A file that doesn't read is left out (and logged). */
	void GetPresets(TArray<FPreset>& OutPresets);

	/** The file a preset of Name is saved in - whether or not there is one yet. Empty for a name that makes no file name. */
	FString GetPresetFile(const FString& Name);

	/** Whether Layer can be in a preset: not a track that plays another animation, whose settings are that animation's. */
	bool CanSave(const FWidgetAnimLayer& Layer);

	/**
	 * Saves Layers - with Starts, each one's start in authored seconds, any origin - as the preset Name, replacing one of that
	 * name. bCollapsed: they came from a collapsed track, and go back in as one. False, with OutError, when it can't.
	 */
	bool Save(const FString& Name, const TArray<FWidgetAnimLayer>& Layers, const TArray<float>& Starts, bool bCollapsed, FText& OutError);

	/** Deletes a preset's file. */
	bool Delete(const FPreset& Preset);

	/**
	 * Puts a preset track's settings on Target, keeping everything that says where Target is and how the panel shows it -
	 * its name included.
	 */
	void ApplyTo(const FWidgetAnimLayer& Preset, FWidgetAnimLayer& Target);
}
