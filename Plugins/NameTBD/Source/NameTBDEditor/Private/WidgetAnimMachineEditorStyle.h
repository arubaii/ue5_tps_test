// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Widgets/SToolTip.h"

class FSlateWindowElementList;
struct FGeometry;
struct FSlateBrush;

/**
 * The State Machine editor's look: its tooltips - the Widget Animations panel's own dark ones, not the editor's light ones -
 * and the shapes its graph is drawn with. Colors live in NameTBDColors.h (StateMachineEditor), text in NameTBDTooltips.h.
 */
namespace WidgetAnimMachineStyle
{
	// -------------------------------------------------------------------------------------------------
	// Tooltips
	// -------------------------------------------------------------------------------------------------

	/**
	 * A tooltip in the plugin's own style, its text read each time it opens. Written as the Widget Animations panel's are:
	 * "Name: what it does" shows Name bold in the animation orange and what it does under it; a short tip with no ": " is
	 * all name. Shows nothing while the text is empty.
	 */
	TSharedPtr<IToolTip> MakeToolTip(const TAttribute<FText>& Text);

	/** A tooltip in the plugin's own style whose content is made fresh each time it opens. Shows nothing while MakeContent gives null. */
	TSharedPtr<IToolTip> MakeDynamicToolTip(TFunction<TSharedPtr<SWidget>()> MakeContent);

	/**
	 * The content of a richer tooltip, line by line - what a node of the graph shows: a title, a kind under it, rows of
	 * "label  value", and paragraphs.
	 */
	class FTipBuilder
	{
	public:

		FTipBuilder& Title(const FText& Text, const FLinearColor& Color);
		FTipBuilder& Kind(const FText& Text);
		FTipBuilder& Row(const FText& Label, const FText& Value, const FLinearColor& ValueColor);
		FTipBuilder& Row(const FText& Label, const FText& Value);
		/** A row whose value is Value followed by Bold, in bold - "Animation: Fade In" with the name in bold. */
		FTipBuilder& Row(const FText& Label, const FText& Value, const FText& Bold, const FLinearColor& ValueColor);
		FTipBuilder& Paragraph(const FText& Text);
		FTipBuilder& Hint(const FText& Text);
		FTipBuilder& Divider();

		TSharedRef<SWidget> Build() const;

	private:

		struct FLine
		{
			enum class EType : uint8 { Title, Kind, Row, Paragraph, Hint, Divider };
			EType Type = EType::Paragraph;
			FText Label;
			FText Value;
			FText Bold;
			FLinearColor Color = FLinearColor::White;
		};
		TArray<FLine> Lines;
	};

	/** The tooltip panel's brush, and its text fonts. */
	const FSlateBrush* ToolTipBrush();
	FSlateFontInfo TitleFont();
	FSlateFontInfo BodyFont();
	FSlateFontInfo BodyBoldFont();
	FSlateFontInfo SmallFont();

	// -------------------------------------------------------------------------------------------------
	// Shapes
	// -------------------------------------------------------------------------------------------------

	/** A white disc Diameter across, tinted where drawn - a transition node. Made once per size, and kept. */
	const FSlateBrush* CircleBrush(float Diameter);

	/** A white ring Diameter across, Width thick - a transition node's outline. Made once per size, and kept. */
	const FSlateBrush* RingBrush(float Diameter, float Width);

	/** White rounded boxes - a state's body, its interior, its progress fill. */
	const FSlateBrush* RoundedBrush(float Radius);

	/** A white box with square corners and a thin black outline - a state's middle while its progress fills it. */
	const FSlateBrush* SharpBoxBrush();

	/** A rounded outline - a selected state. */
	const FSlateBrush* RoundedOutlineBrush(float Radius);

	/**
	 * Diagonal stripes across a disc - what a value-driven track wears in the Widget Animations panel, and a transition that
	 * plays an animation wears here. Each band a chord of the circle, so nothing is drawn outside it.
	 */
	void DrawStripesInCircle(FSlateWindowElementList& OutDrawElements, int32 Layer, const FGeometry& Geometry, const FVector2f& Center, float Radius,
		const FLinearColor& Color, float Width, float Spacing);

	/** Seconds as the graph labels a transition: "0.2s", "1.25s", "0s". */
	FString FormatSeconds(float Seconds);
}
