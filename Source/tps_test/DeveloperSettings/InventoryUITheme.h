#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Fonts/SlateFontInfo.h"
#include "InventoryUITheme.generated.h"

/**
 * All fonts the inventory UI uses, in one asset. Swapping the whole UI's typography is
 * "point every widget at a different UInventoryTheme" instead of opening each WBP and
 * changing its TextBlocks one at a time.
 *
 * Named roles rather than a single font, because a heading, a stack-count label, and a
 * search-bar hint legitimately want different sizes/weights even within one visual
 * style — flattening them into one FSlateFontInfo would force every text element in the
 * UI to look identical regardless of what it's for.
 */
UCLASS(BlueprintType)
class TPS_TEST_API UInventoryUITheme : public UDataAsset
{
	GENERATED_BODY()
 
public:
	/** Panel titles ("Inventory", "Weapons"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fonts")
	FSlateFontInfo HeadingFont;
 
	/** Item names, tooltip body text — the default for anything not covered below. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fonts")
	FSlateFontInfo BodyFont;
 
	/** Small incidental text: stack counts, slot indices, filter dropdown entries. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fonts")
	FSlateFontInfo LabelFont;
 
	/** Placeholder/hint text, e.g. the search bar's "Search to filter...". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fonts")
	FSlateFontInfo HintFont;
 
	/**
	 * Multiplies every font's Size when applied via Scaled(). Keeps the relative
	 * hierarchy intact (heading stays bigger than body) while letting the whole UI
	 * scale up or down as one number — an accessibility setting or a "compact mode"
	 * toggle, without re-tuning four font sizes by hand and having them drift out of
	 * proportion with each other over time.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fonts", meta = (ClampMin = "0.1", UIMin = "0.5", UIMax = "2.0"))
	float GlobalFontScale = 1.0f;
 
	/** Returns Font with GlobalFontScale applied to its Size. Widgets call this instead of reading a font field directly. */
	UFUNCTION(BlueprintPure, Category = "Fonts")
	FSlateFontInfo Scaled(const FSlateFontInfo& Font) const
	{
		FSlateFontInfo scaled = Font;
		scaled.Size = FMath::RoundToInt(Font.Size * GlobalFontScale);
		return scaled;
	}
 
	/**
	 * Takes over ThemeFont's typeface (which font asset, which weight/style within it)
	 * while keeping ExistingFont's Size — GlobalFontScale still applies to that size, so
	 * the widget's designer-set number remains "the size," it just gets multiplied the
	 * same as everything else.
	 *
	 * This is the one to reach for when a widget's size was deliberately hand-tuned in
	 * the Designer (a title that needs to be exactly this big to fit its layout) but the
	 * typeface should still track the shared theme — Scaled() would overwrite that
	 * hand-tuned size with whatever the theme's role happens to specify, which is
	 * usually not what you want for a size that was set for layout reasons rather than
	 * hierarchy reasons.
	 */
	UFUNCTION(BlueprintPure, Category = "Fonts")
	FSlateFontInfo ApplyTypeface(const FSlateFontInfo& ThemeFont, const FSlateFontInfo& ExistingFont) const
	{
		FSlateFontInfo result = ExistingFont;
		result.Size = FMath::RoundToInt(ExistingFont.Size * GlobalFontScale);
 
		// Only take over the typeface if ThemeFont actually has one. An unassigned
		// theme font field (Details panel never touched) is an empty FSlateFontInfo —
		// no FontObject, no CompositeFont — and copying that onto ExistingFont would
		// blank out a perfectly working font rather than "not change the typeface",
		// which then crashes whatever widget receives it. Leaving ExistingFont's
		// typeface alone in that case degrades to "theme didn't override this" instead.
		if (ThemeFont.HasValidFont())
		{
			result.FontObject = ThemeFont.FontObject;
			result.TypefaceFontName = ThemeFont.TypefaceFontName;
		}
 
		return result;
	}
 
	// -----------------------------------------------------------------------------
	// Colors — named by MEANING, not by which widget uses them. A slot border, an
	// item name, and a tooltip line can all be "invalid" for different reasons, but
	// they should all turn the same red — so widgets ask for InvalidColor, not for
	// "the item-name-when-broken color" and "the border-when-broken color" separately.
	// -----------------------------------------------------------------------------
 
	/** Default/neutral text color — used when nothing more specific applies. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colors")
	FLinearColor NeutralColor = FLinearColor::White;
 
	/** An item, slot, or action that's usable/legal right now (valid drop target, etc). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colors")
	FLinearColor ValidColor = FLinearColor(0.2f, 0.9f, 0.3f, 1.f);
 
	/** An item, slot, or action that's broken, illegal, or can't be used right now. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colors")
	FLinearColor InvalidColor = FLinearColor(0.9f, 0.2f, 0.2f, 1.f);
 
	/** Something the player should notice but that isn't outright invalid (low durability, near-full stack). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colors")
	FLinearColor WarningColor = FLinearColor(0.95f, 0.7f, 0.1f, 1.f);
 
	/** Currently-selected or hovered emphasis (a highlighted slot, active filter). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colors")
	FLinearColor HighlightColor = FLinearColor(0.3f, 0.6f, 1.f, 1.f);
};