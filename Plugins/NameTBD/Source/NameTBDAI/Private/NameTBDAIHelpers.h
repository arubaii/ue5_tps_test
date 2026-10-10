// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class FWidgetBlueprintEditor;
class UFactory;
class UUIComponent;
class UWidget;
class UWidgetBlueprint;
struct FNameTBDAIPalette;
struct FNameTBDAIToolContext;
struct FWidgetSpringParams;

/** What the tools share: finding and making assets, Widget Blueprints and their widgets, compiling, the theme. */
namespace NameTBDAIHelpers
{
	// ---- Assets ---------------------------------------------------------------------------------------------------

	/**
	 * An asset by object path ("/Game/UI/WBP_Card.WBP_Card"), package path ("/Game/UI/WBP_Card") or bare name ("WBP_Card",
	 * looked up in the asset registry - the project first, then plugins). Must be a Class if one is given.
	 */
	UObject* FindAsset(const FString& PathOrName, const UClass* Class, FString& OutError);

	/** A package folder: empty -> the settings' Output Folder; "/Game/X" as it is; "X" -> inside the Output Folder. */
	FString ResolveFolder(const FString& Folder);

	/** Name with Prefix ("WBP_", "WA_", "MI_", "M_", "NS_") unless it has one, made a valid asset name. */
	FString MakeAssetName(const FString& Name, const FString& Prefix);

	/** A new asset, its name made unique in Folder. Noted as created in the context's change set. */
	UObject* CreateAsset(const FString& Folder, const FString& Name, UClass* Class, UFactory* Factory, FNameTBDAIToolContext& Context, FString& OutError);

	/** A copy of Source under a unique name in Folder, noted as created. */
	UObject* DuplicateAsset(UObject* Source, const FString& Folder, const FString& Name, FNameTBDAIToolContext& Context, FString& OutError);

	// ---- Widget Blueprints ----------------------------------------------------------------------------------------

	/**
	 * A Widget Blueprint by path or name. Empty: the one a tool last built or changed, else the Widget Blueprint whose editor
	 * was used last.
	 */
	UWidgetBlueprint* FindWidgetBlueprint(const FString& PathOrName, FString& OutError);

	/** A widget of the Widget Blueprint's tree by name (loosely). Empty: the root. */
	UWidget* FindWidget(UWidgetBlueprint* Blueprint, const FString& Name, FString& OutError);

	/** A free, valid widget name from Base ("Title", "Title_1"...). */
	FName MakeWidgetName(UWidgetBlueprint* Blueprint, const FString& Base);

	/** Gives Widget and everything inside it a GUID in the Widget Blueprint's variable map, as the designer does. */
	void GiveGuids(UWidgetBlueprint* Blueprint, UWidget* Widget);

	/** Takes Widget's and its children's GUIDs out (they are being removed). */
	void DropGuids(UWidgetBlueprint* Blueprint, UWidget* Widget);

	/** Compiles; errors and warnings (short) into OutProblems. False when it failed. */
	bool Compile(UWidgetBlueprint* Blueprint, TArray<FString>& OutProblems);

	/** The Widget Blueprint's open editor, or null. */
	FWidgetBlueprintEditor* FindOpenEditor(const UWidgetBlueprint* Blueprint);

	/** The Widget Blueprint whose editor was used last, or null. */
	UWidgetBlueprint* GetFocusedWidgetBlueprint();

	/** The widgets selected in the Widget Blueprint's designer (the Blueprint's own templates). */
	TArray<UWidget*> GetSelectedWidgets(const UWidgetBlueprint* Blueprint);

	/** The tree as compact JSON: name, class, children, and (bProps) each widget's and slot's non-default settings. */
	TSharedPtr<FJsonObject> DescribeTree(UWidgetBlueprint* Blueprint, bool bProps, int32 MaxWidgets = 80);

	/** One widget: class, parent, slot and its non-default settings. */
	TSharedPtr<FJsonObject> DescribeWidget(UWidget* Widget, bool bProps = true);

	/**
	 * Writes Props onto Widget and Slot onto its slot - theme tokens expanded, slot shorthands ("align": "top_left",
	 * "padding": 32) turned into the slot's own fields. Problems into OutErrors.
	 */
	void ApplyWidgetProps(UWidget* Widget, TSharedPtr<FJsonObject> Props, TSharedPtr<FJsonObject> Slot, bool bDark, TArray<FString>& OutErrors);

	/**
	 * A UI component of Class on a widget of the Widget Blueprint (the Blueprint's own template, which the designer's Add
	 * Component edits) - added when bAdd and there is none. Null, with OutError, when it can't be (no UI component support).
	 */
	UUIComponent* FindOrAddComponent(UWidgetBlueprint* Blueprint, UWidget* Widget, UClass* Class, bool bAdd, FString& OutError);

	// ---- Theme ----------------------------------------------------------------------------------------------------

	/** Dark unless Mode says light ("light", "day") - or the theme's own default when Mode is empty. */
	bool IsDark(const FString& Mode);

	const FNameTBDAIPalette& GetPalette(bool bDark);

	/** A theme color by role: background, surface, raised, outline, text, muted, accent, accent2, glow1-3, success, warning, danger. */
	bool GetThemeColor(const FString& Role, bool bDark, FLinearColor& OutColor);

	/**
	 * Replaces theme tokens in values, in place and recursively: "@accent", "@text~0.6" (alpha times 0.6), "@space.3",
	 * "@radius.large", "@type.title", "@spring.bouncy" (a whole spring). Lets a model say intent and get the house values.
	 */
	void ExpandThemeTokens(const TSharedPtr<FJsonObject>& Object, bool bDark);

	TSharedPtr<FJsonObject> ThemeToJson(bool bDark);

	/** A spring preset by name - built-in (Smooth, Quick, Snappy, Gentle, Bouncy, Wobbly, Jelly...) or the project's. */
	bool FindSpringPreset(const FString& Name, FWidgetSpringParams& OutSpring);
	void GetSpringPresetNames(TArray<FString>& OutNames);
}
