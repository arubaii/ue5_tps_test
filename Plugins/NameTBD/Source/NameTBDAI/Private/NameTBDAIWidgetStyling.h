// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UClass;
class UWidget;
class UWidgetBlueprint;
class UWidgetSVG;
struct FNameTBDAIToolContext;

/**
 * What makes a first build look finished and read well, whatever the model writes: engine widgets themed in the house palette
 * (an engine Button is light grey; light theme text on it can't be read), icons as SVGs instead of symbol characters the font
 * doesn't have, the text of buttons put inside them, and every text checked against what is behind it.
 */
namespace NameTBDAIWidgetStyling
{
	/** A button's look: primary (accent filled), secondary (raised, outlined - the default), ghost (no fill until hovered). */
	enum class EButtonVariant : uint8
	{
		Primary,
		Secondary,
		Ghost
	};

	EButtonVariant ParseVariant(const FString& Variant);

	/** Whether the widget is an engine widget themed here (Button - not a plugin button -, Slider, Progress Bar, Check Box, Editable Text Box). */
	bool IsThemedEngineWidget(const UWidget* Widget);

	/** Gives an engine widget the house look in the dark or light palette (rounded, palette colours, readable foreground). */
	void ThemeEngineWidget(UWidget* Widget, bool bDark, EButtonVariant Variant);

	/** The colour a themed (or any) engine Button is filled with at rest; false when it isn't a Button or draws nothing. */
	bool GetButtonFill(const UWidget* Widget, FLinearColor& OutFill);

	/** Text (or an icon) that reads on Background: the dark palette's text or the light palette's, whichever contrasts more. */
	FLinearColor ReadableOn(const FLinearColor& Background);

	/** WCAG contrast ratio of two colours (linear), 1..21. */
	float ContrastRatio(const FLinearColor& A, const FLinearColor& B);

	// ---- Icons ----

	/** The icon names (and aliases) there are, for list_options icons. */
	TSharedPtr<FJsonObject> ListIcons();

	/** An icon by name (or alias) - an SVG asset made once in the Output Folder's Icons folder - or an asset path (SVG or texture). */
	UObject* FindOrMakeIcon(const FString& NameOrPath, FNameTBDAIToolContext& Context, FString& OutError);

	/** Text made only of a symbol the font lacks ("▶", "⏸", "🔊") -> the icon it means; empty when it isn't one. */
	FString IconForGlyph(const FString& Text);

	/**
	 * Text without the characters the default font can't draw (symbols, emoji, private use) - they render as boxes or garbage.
	 * OutRemoved: what was taken out.
	 */
	FString RemoveUndrawableGlyphs(const FString& Text, FString& OutRemoved);

	// ---- Checks ----

	/**
	 * Every text (and SVG icon) among Widgets checked against what is behind it (a button's fill, a border, a filled shape, an earlier
	 * sibling in an overlay, else the theme's background): below 4.5:1 (3:1 for large text) its colour becomes the readable one.
	 * Text in plugin buttons is left to their style. One note per fix.
	 */
	void FixContrast(const TArray<UWidget*>& Widgets, bool bDark, TArray<FString>& OutNotes);

	/** The parts a Widget Blueprint's parent class binds by name (BindWidget / BindWidgetOptional): [{name, class, optional}]. */
	TArray<TSharedPtr<FJsonValue>> DescribeBoundParts(const UClass* ParentClass);

	/** The class a part named Name must be (or derive from) for ParentClass, or null when it binds no such part. */
	UClass* FindBoundPartClass(const UClass* ParentClass, const FString& Name, bool& bOutOptional);
}
