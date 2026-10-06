// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"

class UObject;
class UClass;
class FProperty;

/**
 * Shape presets: a shape widget's settings - its outline (a Star's points and radii, a Custom Shape's vertices), fill, stroke,
 * trim and layout - saved under a name, and put on any other shape of the same kind from the Details panel's Preset row.
 *
 * Kept per kind of shape, as one file per preset in <Project>/Content/WidgetShapePresets/<Kind> (a header line, then one line
 * per setting: its name, a tab, and its value as exported text), so a team shares them through source control the way it
 * shares the track presets. Read afresh whenever they are listed.
 *
 * A preset holds every setting the shape classes declare - not the widget's own (visibility, render transform, its slot)
 * nor its Desired Size, which is layout. Applying one copies the settings: the shape doesn't stay tied to the preset.
 */
namespace WidgetShapePresets
{
	struct FPreset
	{
		/** As shown: the file's name without its extension. */
		FString Name;

		/** The full path of its file. */
		FString File;

		/** Each setting by property name, as exported text. */
		TArray<TPair<FName, FString>> Values;
	};

	/** <Project>/Content/WidgetShapePresets/<Kind>, full path. Made when bCreate and it doesn't exist yet. */
	FString GetFolder(const UClass* ShapeClass, bool bCreate = false);

	/** Every preset of ShapeClass that reads, by name. */
	void GetPresets(const UClass* ShapeClass, TArray<FPreset>& OutPresets);

	/** The file a preset of Name is saved in, whether or not there is one yet. Empty for a name that makes no file name. */
	FString GetPresetFile(const UClass* ShapeClass, const FString& Name);

	/**
	 * The settings a preset of ShapeClass holds: every editable property declared by the shape classes, in declaration
	 * order, without Desired Size. bOutlineOnly leaves out the look - fill, stroke and trim.
	 */
	void GetPresetProperties(const UClass* ShapeClass, bool bOutlineOnly, TArray<FProperty*>& OutProperties);

	/** Whether a property is part of a shape's look (fill, stroke, trim) rather than its outline. */
	bool IsLookProperty(const FProperty* Property);

	/** A shape's settings as a preset holds them. */
	void Capture(const UObject* Shape, TArray<TPair<FName, FString>>& OutValues);

	/** Saves Values as the preset Name of ShapeClass, replacing one of that name. False, with OutError, when it can't. */
	bool Save(const UClass* ShapeClass, const FString& Name, const TArray<TPair<FName, FString>>& Values, FText& OutError);

	/** Deletes a preset's file. */
	bool Delete(const FPreset& Preset);

	/** What a kind of shape is called - "Star", "N-gon" - for menus and messages. */
	FText GetKindName(const UClass* ShapeClass);
}
