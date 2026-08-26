// Copyright Bilcash 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "StructCategoryGroupsSettings.generated.h"

/**
 * Visual style used to render a colored category header.
 */
UENUM()
enum class EStructCategoryHeaderStyle : uint8
{

	/** A wash of the color behind a normal header. */
	SubtleTint      UMETA(DisplayName = "Tint Fill"),
	
	/** Solid color fills the whole header; regular-weight label. */
	SolidFill        UMETA(DisplayName = "Solid Fill"),
	
	/** Mimics the gradient look of the Macro node (when colored) */
	UnrealGradientFill    UMETA(DisplayName = "Unreal Gradient Fill"),
	
	SCGGradientFill    UMETA(DisplayName = "SCG Gradient Fill"),
	
	/** Neutral header with a thick colored stripe down the left edge. */
	LeftAccentBar   UMETA(DisplayName = "Left Accent Bar"),

	/** Normal header with a colored rule along the bottom edge. */
	Underline       UMETA(DisplayName = "Underline"),
	
	/** Thin colored outline around the whole header, transparent interior. */
	Border          UMETA(DisplayName = "Border"),

	/** Left accent stripe combined with a subtle wash of the same color. */
	LeftBarAndTint  UMETA(DisplayName = "Left Bar + Tint"),
};

/**
 * A single named color in the user's palette.
 * The palette is defined in Project Settings; category headers pick from it by name.
 */
USTRUCT()
struct FStructCategoryColorEntry
{
	GENERATED_BODY()

	/** Display name shown in the header dropdown (e.g. "Light Blue"). Must be unique. */
	UPROPERTY(EditAnywhere, Category = "Palette")
	FString Name;

	/** The color applied to the group header when this entry is selected. */
	UPROPERTY(EditAnywhere, Category = "Palette")
	FLinearColor Color = FLinearColor::White;

	FStructCategoryColorEntry() = default;
	FStructCategoryColorEntry(const FString& InName, const FLinearColor& InColor)
		: Name(InName), Color(InColor) {}
};

/**
 * Editor settings for Struct Category Groups.
 * Found under: Edit > Project Settings > Plugins > Struct Category Groups.
 *
 * WORKFLOW:
 *   1. Define your palette here (seeded with Light Blue / Light Red / Light Green).
 *      Add, rename, or recolor entries however you like.
 *   2. In the Details panel (or any struct editor panel), right-click the category to select a palette color.
 *      Once selected, the color is set automatically and persisted here under CategoryAssignments.
 *
 * When changing the header style, just click off and on the variable for it to take effect, or close and reopen the 
 * blueprint. 
 * Assignments are keyed by palette NAME, so editing a palette color later
 * updates every category currently using it.
 */
UCLASS(config = Editor, defaultconfig, meta = (DisplayName = "Struct Category Groups"))
class STRUCTCATEGORYGROUPSEDITOR_API UStructCategoryGroupsSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UStructCategoryGroupsSettings()
	{
		// Seed the palette with three light defaults on first use.
		if (Palette.Num() == 0)
		{
			Palette.Add(FStructCategoryColorEntry(TEXT("Light Blue"),  FLinearColor(0.55f, 0.75f, 1.00f)));
			Palette.Add(FStructCategoryColorEntry(TEXT("Light Red"),   FLinearColor(1.00f, 0.60f, 0.60f)));
			Palette.Add(FStructCategoryColorEntry(TEXT("Light Green"), FLinearColor(0.60f, 0.90f, 0.60f)));
		}
	}

	virtual FName GetCategoryName() const override { return FName(TEXT("Plugins")); }

	/** How colored category headers are rendered in the Details panel, unless a
	 *  path has its own entry in HeaderStyleOverrides. */
	UPROPERTY(EditAnywhere, config, Category = "Style",
		meta = (ToolTip = "Default visual style used for colored category headers. Individual paths can override this via the header right-click menu."))
	EStructCategoryHeaderStyle HeaderStyle = EStructCategoryHeaderStyle::UnrealGradientFill;

	/** The user-defined color palette. Category headers choose from these by name. 
	* When changing the header style, just click off and on the variable for it to take effect, or close and reopen the 
	* blueprint. 
	 */
	UPROPERTY(EditAnywhere, config, Category = "Palette",
		meta = (ToolTip = "Define named colors here. Category header dropdowns pick from this list."))
	TArray<FStructCategoryColorEntry> Palette;

	/**
	 * Which palette color each category path is assigned to.
	 * Key   = full category path (| for nesting, e.g. "Movement|Advanced").
	 * Value = palette entry Name. Written automatically by the header dropdown.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Assignments",
		meta = (ToolTip = "Category path -> palette color name. Usually set via the header dropdown, but editable here too."))
	TMap<FString, FString> CategoryAssignments;

	/**
	 * Per-path header style overrides. A path with no entry here falls back to
	 * the project-wide HeaderStyle above. Written automatically by the "Header
	 * Style" section of the header right-click menu.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Assignments",
		meta = (ToolTip = "Category path -> header style override. Falls back to the default Style setting when a path has no entry here."))
	TMap<FString, EStructCategoryHeaderStyle> HeaderStyleOverrides;

	/** Finds a palette color by name. Returns false if the name isn't in the palette. */
	bool FindPaletteColor(const FString& EntryName, FLinearColor& OutColor) const
	{
		for (const FStructCategoryColorEntry& Entry : Palette)
		{
			if (Entry.Name.Equals(EntryName, ESearchCase::CaseSensitive))
			{
				OutColor = Entry.Color;
				return true;
			}
		}
		return false;
	}

	/**
	 * Resolves the effective color for a category path.
	 * Returns true and fills OutColor if the path is assigned to a valid palette entry.
	 * Returns false if unassigned (or assigned to a deleted palette name) -> treat as uncolored.
	 */
	bool ResolveCategoryColor(const FString& CategoryPath, FLinearColor& OutColor) const
	{
		if (const FString* AssignedName = CategoryAssignments.Find(CategoryPath))
		{
			return FindPaletteColor(*AssignedName, OutColor);
		}
		return false;
	}

	/** Returns the palette name currently assigned to a path, or empty string if none. */
	FString GetAssignedName(const FString& CategoryPath) const
	{
		if (const FString* AssignedName = CategoryAssignments.Find(CategoryPath))
		{
			return *AssignedName;
		}
		return FString();
	}

	/** Assigns a palette entry (by name) to a category path and persists to config. */
	void AssignCategory(const FString& CategoryPath, const FString& EntryName)
	{
		CategoryAssignments.Add(CategoryPath, EntryName);
		SaveConfig();
	}

	/** Clears any color assignment for a category path and persists to config. */
	void ClearCategory(const FString& CategoryPath)
	{
		CategoryAssignments.Remove(CategoryPath);
		SaveConfig();
	}

	/** Resolves the effective header style for a path: its own override if set, else the project-wide default. */
	EStructCategoryHeaderStyle ResolveHeaderStyle(const FString& CategoryPath) const
	{
		if (const EStructCategoryHeaderStyle* Override = HeaderStyleOverrides.Find(CategoryPath))
		{
			return *Override;
		}
		return HeaderStyle;
	}

	/** Sets a per-path header style override and persists to config. */
	void AssignHeaderStyle(const FString& CategoryPath, EStructCategoryHeaderStyle Style)
	{
		HeaderStyleOverrides.Add(CategoryPath, Style);
		SaveConfig();
	}

	/** Clears a path's header style override (falls back to the project-wide default) and persists to config. */
	void ClearHeaderStyle(const FString& CategoryPath)
	{
		HeaderStyleOverrides.Remove(CategoryPath);
		SaveConfig();
	}
};