// Copyright Bilcash 2026. All rights reserved.

#include "StructCategoryGroups.h"
#include "StructCategoryGroupsSettings.h"

#include "DetailWidgetRow.h"
#include "DetailLayoutBuilder.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailGroup.h"
#include "IPropertyUtilities.h"
#include "PropertyHandle.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Colors/SComplexGradient.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSeparator.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateBrush.h"
#include "Brushes/SlateColorBrush.h"

#define LOCTEXT_NAMESPACE "StructCategoryGroups"

namespace
{
	FLinearColor GetSwatchColorForPath(const FString& CategoryPath)
	{
		const UStructCategoryGroupsSettings* Settings = GetDefault<UStructCategoryGroupsSettings>();
		FLinearColor Color;
		if (Settings && Settings->ResolveCategoryColor(CategoryPath, Color))
		{
			return Color;
		}
		// Flagged ColoredCategory but no palette color assigned yet
		return FLinearColor(0.12f, 0.12f, 0.12f, 1.f);
	}

	// True if this path currently resolves to a valid palette color 
	bool PathHasColor(const FString& CategoryPath)
	{
		const UStructCategoryGroupsSettings* Settings = GetDefault<UStructCategoryGroupsSettings>();
		FLinearColor Unused;
		return Settings && Settings->ResolveCategoryColor(CategoryPath, Unused);
	}
}

/**
 * A transparent wrapper that shows its child and, on right-click, opens a menu
 * built by MenuContentBuilder at the cursor. Used to attach the palette picker
 * to a colored category header without a visible dropdown button.
 */
class SStructHeaderRightClickZone : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_RetVal(TSharedRef<SWidget>, FOnBuildMenuContent);

	SLATE_BEGIN_ARGS(SStructHeaderRightClickZone) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_EVENT(FOnBuildMenuContent, MenuContentBuilder)
		SLATE_ARGUMENT(FText, ExtraToolTipText)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		MenuContentBuilder = InArgs._MenuContentBuilder;
		ChildSlot
		[
			InArgs._Content.Widget
		];

		const FText hint = NSLOCTEXT("StructCategoryGroups", "HeaderRightClickTip", "Right-click to set category color");
		SetToolTipText(InArgs._ExtraToolTipText.IsEmpty()
			? hint
			: FText::Format(NSLOCTEXT("StructCategoryGroups", "HeaderRightClickTipWithProperty", "{0}\n\n{1}"),
			                 InArgs._ExtraToolTipText, hint));
	}

	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton && MenuContentBuilder.IsBound())
		{
			FSlateApplication::Get().PushMenu(
				SharedThis(this),
				FWidgetPath(),
				MenuContentBuilder.Execute(),
				MouseEvent.GetScreenSpacePosition(),
				FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

private:
	FOnBuildMenuContent MenuContentBuilder;
};

TSharedRef<IPropertyTypeCustomization> FStructCategoryGroups::MakeInstance()
{
	return MakeShareable(new FStructCategoryGroups());
}

void FStructCategoryGroups::CustomizeHeader(TSharedRef<IPropertyHandle> property_handle,
                                            FDetailWidgetRow& header_row,
                                            IPropertyTypeCustomizationUtils& customization_utils)
{
	m_PropertyUtilities = customization_utils.GetPropertyUtilities();

	m_StructDisplayName.Reset();
	if (const FStructProperty* structProperty = CastField<FStructProperty>(property_handle->GetProperty()))
	{
		if (structProperty->Struct)
		{
			m_StructDisplayName = structProperty->Struct->GetName();
		}
	}

	if (m_StructDisplayName.IsEmpty())
	{
		header_row
			.NameContent()
			[
				property_handle->CreatePropertyNameWidget()
			];
		return;
	}

	// Key = the struct TYPE name (so every property of this struct type shares
	// one assignment)
	const FString categoryKey = m_StructDisplayName;
	const FString leafLabel = property_handle->GetPropertyDisplayName().ToString();

	if (PathHasColor(categoryKey))
	{
		header_row.WholeRowContent()
		[
			BuildColoredHeaderContent(categoryKey, leafLabel, property_handle->GetToolTipText())
		];
	}
	else
	{
		header_row.WholeRowContent()
		[
			SNew(SStructHeaderRightClickZone)
			.MenuContentBuilder_Lambda([this, categoryKey]() { return BuildPaletteMenuContent(categoryKey); })
			.ExtraToolTipText(property_handle->GetToolTipText())
			[
				SNew(SBox)
				.HeightOverride(22.f)
				.VAlign(VAlign_Center)
				[
					property_handle->CreatePropertyNameWidget()
				]
			]
		];
	}
}

TSharedRef<SWidget> FStructCategoryGroups::BuildPaletteMenuContent(const FString& category_path)
{
	// Weak copy of the property utilities so a selection can rebuild the panel
	// when the assignment changes (needed so switching to/from None restyles the row).
	TWeakPtr<IPropertyUtilities> utilitiesWeak = m_PropertyUtilities;

	const UStructCategoryGroupsSettings* Settings = GetDefault<UStructCategoryGroupsSettings>();

	// Every row is pinned to this width so the hover highlight always covers
	// the full row, not just however wide its label happens to be.
	static const float MenuWidth = 180.f;

	TSharedRef<SVerticalBox> menu = SNew(SVerticalBox);

	// A small grey checkmark shown next to whichever entry is currently active
	auto MakeCheckmark = [](bool bVisible) -> TSharedRef<SWidget>
	{
		return SNew(SBox)
			.WidthOverride(14.f)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SImage)
				.Visibility(bVisible ? EVisibility::Visible : EVisibility::Hidden)
				.Image(FAppStyle::GetBrush("Icons.Check"))
				.ColorAndOpacity(FSlateColor::UseForeground())
			];
	};

	// One clickable row, full menu width, with a hover-highlighted background
	auto AddClickableRow = [&menu, MakeCheckmark](TSharedRef<SWidget> LeadingIcon,
	                                              const FText& Label,
	                                              bool bIsCurrent,
	                                              TFunction<FReply()> OnClick)
	{
		menu->AddSlot()
		.AutoHeight()
		[
			SNew(SBox)
			.WidthOverride(MenuWidth)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ContentPadding(FMargin(6.f, 4.f))
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Fill)
				.OnClicked_Lambda(OnClick)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.f, 0.f, 6.f, 0.f)
					[
						LeadingIcon
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(Label)
						.Font(bIsCurrent ? IDetailLayoutBuilder::GetDetailFontBold() : IDetailLayoutBuilder::GetDetailFont())
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(6.f, 0.f, 0.f, 0.f)
					[
						MakeCheckmark(bIsCurrent)
					]
				]
			]
		];
	};

	// Engine-style section heading: small grey uppercase label + a hairline
	// filling the rest of the row, matching the look of Unreal's own context
	// menus (e.g. the "Expansion" / "Edit" headings on a details row menu).
	auto AddSectionHeading = [&menu](const FText& Label, bool bIsFirst)
	{
		menu->AddSlot()
		.AutoHeight()
		.Padding(6.f, bIsFirst ? 2.f : 10.f, 6.f, 4.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.f, 0.f, 6.f, 0.f)
			[
				SNew(STextBlock)
				.Text(Label)
				.Font(IDetailLayoutBuilder::GetDetailFontBold())
				.ColorAndOpacity(FSlateColor(FLinearColor(1.f, 1.f, 1.f, 0.45f)))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			[
				SNew(SSeparator)
				.Orientation(Orient_Horizontal)
				.Thickness(1.f)
				.ColorAndOpacity(FSlateColor(FLinearColor(1.f, 1.f, 1.f, 0.12f)))
			]
		];
	};

	// --- Color section ------------------------------------------------------
	AddSectionHeading(NSLOCTEXT("StructCategoryGroups", "ColorSectionLabel", "COLOR"), /*bIsFirst*/ true);

	const FString currentColorName = Settings ? Settings->GetAssignedName(category_path) : FString();

	auto AddColorRow = [&AddClickableRow, category_path, utilitiesWeak, &currentColorName](const FString& EntryName,
	                                                                                       const FLinearColor& SwatchColor,
	                                                                                       bool bIsNone)
	{
		const bool bIsCurrent = bIsNone ? currentColorName.IsEmpty() : currentColorName.Equals(EntryName, ESearchCase::CaseSensitive);

		TSharedRef<SWidget> swatch = SNew(SColorBlock)
			.Color(SwatchColor)
			.Size(FVector2D(14.f, 14.f))
			.CornerRadius(FVector4(3.f, 3.f, 3.f, 3.f));

		AddClickableRow(swatch, FText::FromString(bIsNone ? TEXT("None") : EntryName), bIsCurrent,
			[category_path, utilitiesWeak, bIsNone, EntryName]() -> FReply
			{
				UStructCategoryGroupsSettings* MutableSettings = GetMutableDefault<UStructCategoryGroupsSettings>();
				if (MutableSettings)
				{
					if (bIsNone)
					{
						MutableSettings->ClearCategory(category_path);
					}
					else
					{
						MutableSettings->AssignCategory(category_path, EntryName);
					}
				}
				// Dismiss the right-click menu.
				FSlateApplication::Get().DismissAllMenus();
				// Rebuild the details panel so the header switches style/color.
				if (TSharedPtr<IPropertyUtilities> utilities = utilitiesWeak.Pin())
				{
					utilities->ForceRefresh();
				}
				return FReply::Handled();
			});
	};

	// "None" clears the assignment.
	AddColorRow(FString(), FLinearColor(1.f, 1.f, 1.f, 0.05f), /*bIsNone*/ true);

	if (Settings)
	{
		for (const FStructCategoryColorEntry& Entry : Settings->Palette)
		{
			AddColorRow(Entry.Name, Entry.Color, /*bIsNone*/ false);
		}
	}

	// --- Header style section ------------------------------------------------
	AddSectionHeading(NSLOCTEXT("StructCategoryGroups", "HeaderStyleSectionLabel", "HEADER STYLE"), /*bIsFirst*/ false);

	const EStructCategoryHeaderStyle currentStyle = Settings ? Settings->ResolveHeaderStyle(category_path) : EStructCategoryHeaderStyle::SolidFill;
	const bool bHasOverride = Settings && Settings->HeaderStyleOverrides.Contains(category_path);

	auto AddStyleRow = [&AddClickableRow, category_path, utilitiesWeak](const FText& Label, bool bIsUseDefault, EStructCategoryHeaderStyle Style, bool bIsCurrent)
	{
		AddClickableRow(SNullWidget::NullWidget, Label, bIsCurrent,
			[category_path, utilitiesWeak, bIsUseDefault, Style]() -> FReply
			{
				UStructCategoryGroupsSettings* MutableSettings = GetMutableDefault<UStructCategoryGroupsSettings>();
				if (MutableSettings)
				{
					if (bIsUseDefault)
					{
						MutableSettings->ClearHeaderStyle(category_path);
					}
					else
					{
						MutableSettings->AssignHeaderStyle(category_path, Style);
					}
				}
				FSlateApplication::Get().DismissAllMenus();
				if (TSharedPtr<IPropertyUtilities> utilities = utilitiesWeak.Pin())
				{
					utilities->ForceRefresh();
				}
				return FReply::Handled();
			});
	};

	AddStyleRow(NSLOCTEXT("StructCategoryGroups", "UseDefaultHeaderStyle", "Use Default"), /*bIsUseDefault*/ true,
	            EStructCategoryHeaderStyle::SolidFill, /*bIsCurrent*/ !bHasOverride);

	if (const UEnum* EnumPtr = StaticEnum<EStructCategoryHeaderStyle>())
	{
		// NumEnums() includes the implicit _MAX sentinel Unreal adds to every
		// UENUM -- skip it, it's not a selectable value.
		const int32 numEntries = EnumPtr->NumEnums() - 1;
		for (int32 i = 0; i < numEntries; ++i)
		{
			const EStructCategoryHeaderStyle value = static_cast<EStructCategoryHeaderStyle>(EnumPtr->GetValueByIndex(i));
			AddStyleRow(EnumPtr->GetDisplayNameTextByIndex(i), /*bIsUseDefault*/ false, value,
			            /*bIsCurrent*/ bHasOverride && value == currentStyle);
		}
	}

	// Wrap in a menu-styled border so it reads as a popup.
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Menu.Background"))
		.Padding(FMargin(4.f, 6.f))
		[
			menu
		];
}

TSharedRef<SWidget> FStructCategoryGroups::BuildColoredHeaderContent(const FString& category_path,
                                                                     const FString& leaf_label,
                                                                     const FText& extra_tooltip)
{
	if (PathHasColor(category_path))
	{
		// ASSIGNED: render using this path's header style (per-path override if
		// set, else the project-wide default).
		const EStructCategoryHeaderStyle Style =
			GetDefault<UStructCategoryGroupsSettings>()
				? GetDefault<UStructCategoryGroupsSettings>()->ResolveHeaderStyle(category_path)
				: EStructCategoryHeaderStyle::SolidFill;

		// A label whose color adapts to the fill behind it (used by fill styles).
		auto MakeContrastLabel = [category_path, leaf_label](bool bBold, bool bContrastToFill)
		{
			return SNew(STextBlock)
				.Text(FText::FromString(leaf_label))
				.Font(bBold ? IDetailLayoutBuilder::GetDetailFontBold()
				            : IDetailLayoutBuilder::GetDetailFont())
				.ColorAndOpacity_Lambda([category_path, bContrastToFill]()
				{
					if (!bContrastToFill)
					{
						return FSlateColor::UseForeground();
					}
					const FLinearColor c = GetSwatchColorForPath(category_path);
					const float luminance = 0.299f * c.R + 0.587f * c.G + 0.114f * c.B;
					return FSlateColor(luminance > 0.5f ? FLinearColor::Black : FLinearColor::White);
				});
		};

		TSharedRef<SWidget> headerContent = SNullWidget::NullWidget;

		switch (Style)
		{
		// =============================================================================================================
		// Add custom header here
		// =============================================================================================================
			
			
			
			
			
			
			
			
		// =============================================================================================================
		// End custom headers
		// =============================================================================================================
		case EStructCategoryHeaderStyle::LeftAccentBar:
		{
			// Neutral (engine-default) background + a thick colored stripe on the
			// left edge. We draw no background brush of our own so the row's stock
			// header background shows through; only the accent stripe is painted.
			headerContent =
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SBox)
					.WidthOverride(4.f)
					[
						SNew(SColorBlock)
						.Color_Lambda([category_path]() { return GetSwatchColorForPath(category_path); })
					]
				]
				+ SHorizontalBox::Slot()
				.Padding(FMargin(6.f, 0.f))
				.VAlign(VAlign_Center)
				[
					MakeContrastLabel(/*bBold*/ true, /*bContrastToFill*/ false)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				.Padding(FMargin(0.f, 0.f, 4.f, 0.f))
				[
					SNullWidget::NullWidget
				];
			break;
		}

		case EStructCategoryHeaderStyle::SubtleTint:
		{
			// Low-opacity wash of the color behind a normal header.
			headerContent =
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.VAlign(VAlign_Center)
				.BorderBackgroundColor_Lambda([category_path]()
				{
					const FLinearColor c = GetSwatchColorForPath(category_path);
					return FLinearColor(c.R, c.G, c.B, 0.18f);
				})
				.Padding(FMargin(6.f, 0.f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					[
						MakeContrastLabel(/*bBold*/ true, /*bContrastToFill*/ false)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.HAlign(HAlign_Right)
					.VAlign(VAlign_Center)
					[
						SNullWidget::NullWidget
					]
				];
			break;
		}

		case EStructCategoryHeaderStyle::Underline:
		{
			// Normal header with a colored rule along the bottom edge.
			headerContent =
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.FillHeight(1.f)
				.VAlign(VAlign_Center)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.Padding(FMargin(6.f, 0.f))
					.VAlign(VAlign_Center)
					[
						MakeContrastLabel(/*bBold*/ true, /*bContrastToFill*/ false)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.HAlign(HAlign_Right)
					.VAlign(VAlign_Center)
					.Padding(FMargin(0.f, 0.f, 4.f, 0.f))
					[
						SNullWidget::NullWidget
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBox)
					.HeightOverride(2.f)
					[
						SNew(SColorBlock)
						.Color_Lambda([category_path]() { return GetSwatchColorForPath(category_path); })
					]
				];
			break;
		}

		case EStructCategoryHeaderStyle::UnrealGradientFill:
		{
		    // TUNING: Controls the thickness of the dark outer frame
		    const float BorderThickness = 0.01f;
		    const float LipHeight = 6.0f;
		    const float black = 0.0f;
		    const float CornerRadius = 7.0f;

		    // --- Label font: 1px outline instead of a drop shadow (a shadow just
		    // smears at this size), plus a little tracking so the row reads as a
		    // header rather than another property label.
		    FSlateFontInfo headerFont = IDetailLayoutBuilder::GetDetailFontBold();
		    headerFont.LetterSpacing = 25; // tuned for mixed case; raise for uppercase
		    headerFont.OutlineSettings.OutlineSize  = 1;
		    headerFont.OutlineSettings.OutlineColor = FLinearColor(0.f, 0.f, 0.f, 0.55f);
		    // Keeps the outline solid when the fill is translucent -- without this,
		    // lowering the text alpha fades the outline with it.
		    headerFont.OutlineSettings.bSeparateFillAlpha = true;

		    headerContent =
		        SNew(SOverlay)

		        // --- LAYER 1: The Dark Outer Frame & Rounded Bottom Lip ---
		        // SColorBlock provides the solid dark background with rounded corners 
		        // that creates your outer border frame.
		        + SOverlay::Slot()
		        .HAlign(HAlign_Fill)
		        .VAlign(VAlign_Fill)
		        [
		            SNew(SColorBlock)
		            .Color(FLinearColor(black, black, black, 1.f))
		            .CornerRadius(FVector4(CornerRadius, CornerRadius, CornerRadius, CornerRadius))
		        ]

		        // --- LAYER 2: The InnerGradient Content ---
		        // Inset on the sides/top for the border, and given extra room at 
		        // the bottom (LipHeight) to expose the dark backing frame as a lip.
		        + SOverlay::Slot()
		        .HAlign(HAlign_Fill)
		        .VAlign(VAlign_Fill)
		        .Padding(FMargin(BorderThickness, BorderThickness, BorderThickness, LipHeight))
		        [
		            SNew(SOverlay)
		            
		            // Inner Base Gloss
		            + SOverlay::Slot()
		            .HAlign(HAlign_Fill)
		            .VAlign(VAlign_Fill)
		            [
		                SNew(SImage)
		                .Image(FAppStyle::GetBrush("Graph.Node.TitleGloss"))
		                .ColorAndOpacity(FLinearColor::White)
		            ]
		            
		            // Color Spill & Text
		            + SOverlay::Slot()
		            .HAlign(HAlign_Fill)
		            .VAlign(VAlign_Fill)
		            [
		                SNew(SBorder)
		                .BorderImage(FAppStyle::GetBrush("Graph.Node.ColorSpill"))
		                .Padding(FMargin(10.f, 0.f))
		                .BorderBackgroundColor_Lambda([category_path]()
		                {
		                    return FSlateColor(GetSwatchColorForPath(category_path));
		                })
		                [

			                SNew(SHorizontalBox)
		                + SHorizontalBox::Slot()
		                .VAlign(VAlign_Center)
		                .Padding(FMargin(0.0f, 2.0f, 0.0f, 0.0f))
		                [
		                   SNew(STextBlock)
		                   .Text(FText::FromString(leaf_label))
		                   .Font(headerFont)
		                   .ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.8)))
		                ]
		                ]
		            ]
		            
		            // Top Highlight Sheen
		            + SOverlay::Slot()
		            .VAlign(VAlign_Top)
		            [
		                SNew(SBorder)
		                .Visibility(EVisibility::HitTestInvisible)
		                .BorderImage(FAppStyle::GetBrush("Graph.Node.TitleHighlight"))
		                .BorderBackgroundColor(FLinearColor::White)
		                [
		                    SNew(SSpacer)
		                    .Size(FVector2D(20.f, 20.f))
		                ]
		            ]
		        ];
		    break;
		}
		case EStructCategoryHeaderStyle::SCGGradientFill:
		{
			// Full-bleed glossy fill, no border/lip. Built from stacked layers:
			//   1. horizontal colour sweep (the hue journey across the row)
			//   2. upper "glass" band  - bright, hard-edged at its bottom
			//   3. lower body shade    - deep, with a bounce-light lift at the base
			//   4. 1px top rim light
			//   5. left scrim          - guarantees label contrast on light palettes
			//   6. label               - outlined + tracked-out uppercase
			//
			// TUNING: GlossSplit controls where the specular break sits. 0.45 = the
			// classic glass-button waterline; lower = thinner, sharper highlight.
			static const float GlossSplit = 0.45f;

			// Width of the dark scrim behind the label. Roughly the label column.
			static const float ScrimWidth = 180.f;

			// Shared palette-derived shading. HSV keeps the hue intact instead of
			// muddying toward grey the way a lerp-to-black would.
			// NOTE: LinearRGBToHSV packs H/S/V into .R/.G/.B respectively 
			// hence .G for saturation and .B for value below.
			auto Shade = [category_path](float ValueScale, float SatScale, float Alpha)
			{
				FLinearColor hsv = GetSwatchColorForPath(category_path).LinearRGBToHSV();
				hsv.G = FMath::Clamp(hsv.G * SatScale,   0.f, 1.f);
				hsv.B = FMath::Clamp(hsv.B * ValueScale, 0.f, 1.f);
				FLinearColor rgb = hsv.HSVToLinearRGB();
				rgb.A = Alpha;
				return rgb;
			};

			// --- Layer 1: horizontal sweep. Doubled bright stop holds the saturated
			// body toward the left before falling away.
			auto SweepStops = [Shade]() -> TArray<FLinearColor>
			{
				TArray<FLinearColor> s;
				s.Add(Shade(0.45f, 1.10f, 1.f)); // deep left shoulder
				s.Add(Shade(1.00f, 1.00f, 1.f)); // full strength
				s.Add(Shade(1.00f, 1.00f, 1.f)); // held (weights the bright zone left)
				s.Add(Shade(0.70f, 0.92f, 1.f));
				s.Add(Shade(0.22f, 0.75f, 1.f)); // falls off right
				return s;
			};

			// --- Layer 2: upper glass band. Bright at the top, and the final two
			// stops are near-identical so the band ENDS abruptly at the waterline
			// rather than feathering into the lower half.
			auto GlassStops = []() -> TArray<FLinearColor>
			{
				TArray<FLinearColor> s;
				s.Add(FLinearColor(1.f, 1.f, 1.f, 0.34f)); // specular peak
				s.Add(FLinearColor(1.f, 1.f, 1.f, 0.20f));
				s.Add(FLinearColor(1.f, 1.f, 1.f, 0.09f));
				s.Add(FLinearColor(1.f, 1.f, 1.f, 0.07f)); // held -> hard cut-off
				return s;
			};

			// --- Layer 3: lower body. Darkens under the waterline, then lifts again
			// at the very bottom -- that bounce is what stops it reading as flat.
			auto BodyStops = []() -> TArray<FLinearColor>
			{
				TArray<FLinearColor> s;
				s.Add(FLinearColor(0.f, 0.f, 0.f, 0.30f)); // shadow just under the break
				s.Add(FLinearColor(0.f, 0.f, 0.f, 0.16f));
				s.Add(FLinearColor(0.f, 0.f, 0.f, 0.04f));
				s.Add(FLinearColor(1.f, 1.f, 1.f, 0.07f)); // bounce light at the base
				return s;
			};

			// --- Layer 5: scrim behind the label only, fading out to the right.
			auto ScrimStops = []() -> TArray<FLinearColor>
			{
				TArray<FLinearColor> s;
				s.Add(FLinearColor(0.f, 0.f, 0.f, 0.28f));
				s.Add(FLinearColor(0.f, 0.f, 0.f, 0.16f));
				s.Add(FLinearColor(0.f, 0.f, 0.f, 0.f));
				return s;
			};

			// --- Label font: 1px outline instead of a drop shadow (a shadow just
			// smears at this size), plus tracking so the row reads as a header
			// rather than another property label.
			FSlateFontInfo headerFont = IDetailLayoutBuilder::GetDetailFontBold();
			headerFont.LetterSpacing = 55; // 1/1000 em; drop to ~35 if too airy
			headerFont.OutlineSettings.OutlineSize  = 1;
			headerFont.OutlineSettings.OutlineColor = FLinearColor(0.f, 0.f, 0.f, 0.55f);
			// Keeps the outline solid when the fill is translucent -- without this,
			// lowering the text alpha fades the outline with it.
			headerFont.OutlineSettings.bSeparateFillAlpha = true;

			headerContent =
				SNew(SOverlay)
				// Horizontal colour sweep across the full row.
				// Orient_Vertical = vertical bands, i.e. progression runs left->right.
				+ SOverlay::Slot()
				[
					SNew(SComplexGradient)
					.Visibility(EVisibility::HitTestInvisible)
					.GradientColors_Lambda(SweepStops)
					.Orientation(Orient_Vertical)
				]
				// Gloss: two stacked gradients meeting at GlossSplit.
				+ SOverlay::Slot()
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.FillHeight(GlossSplit)
					[
						SNew(SComplexGradient)
						.Visibility(EVisibility::HitTestInvisible)
						.GradientColors_Lambda(GlassStops)
						.Orientation(Orient_Horizontal) // top-to-bottom
					]
					+ SVerticalBox::Slot()
					.FillHeight(1.f - GlossSplit)
					[
						SNew(SComplexGradient)
						.Visibility(EVisibility::HitTestInvisible)
						.GradientColors_Lambda(BodyStops)
						.Orientation(Orient_Horizontal)
					]
				]
				// 1px rim light along the very top edge.
				+ SOverlay::Slot()
				.VAlign(VAlign_Top)
				[
					SNew(SBox)
					.HeightOverride(1.f)
					[
						SNew(SImage)
						.Visibility(EVisibility::HitTestInvisible)
						.Image(FAppStyle::GetBrush("WhiteBrush"))
						.ColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.22f))
					]
				]
				// Left-anchored dark scrim: guarantees label contrast on light
				// palettes without dimming the rest of the gradient.
				+ SOverlay::Slot()
				.HAlign(HAlign_Left)
				[
					SNew(SBox)
					.WidthOverride(ScrimWidth)
					[
						SNew(SComplexGradient)
						.Visibility(EVisibility::HitTestInvisible)
						.GradientColors_Lambda(ScrimStops)
						.Orientation(Orient_Vertical)
					]
				]
				// Label.
				+ SOverlay::Slot()
				.VAlign(VAlign_Center)
				.Padding(FMargin(10.f, 0.f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(FText::FromString(leaf_label))
						.Font(headerFont)
						.ColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.95f))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.HAlign(HAlign_Right)
					.VAlign(VAlign_Center)
					[
						SNullWidget::NullWidget
					]
				];
			break;
		}

		case EStructCategoryHeaderStyle::Border:
		{
			// Strong colored outline drawn as four explicit edges around a
			// transparent interior, so it reads clearly regardless of style set.
			const float T = 2.f; // border thickness

			auto EdgeColor = [category_path]() { return GetSwatchColorForPath(category_path); };

			headerContent =
				SNew(SOverlay)
				// Interior content.
				+ SOverlay::Slot()
				.VAlign(VAlign_Center)
				.Padding(FMargin(6.f, 0.f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					[
						MakeContrastLabel(/*bBold*/ true, /*bContrastToFill*/ false)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.HAlign(HAlign_Right)
					.VAlign(VAlign_Center)
					[
						SNullWidget::NullWidget
					]
				]
				// Top edge.
				+ SOverlay::Slot()
				.VAlign(VAlign_Top)
				[
					SNew(SBox).HeightOverride(T)
					[ SNew(SColorBlock).Color_Lambda(EdgeColor) ]
				]
				// Bottom edge.
				+ SOverlay::Slot()
				.VAlign(VAlign_Bottom)
				[
					SNew(SBox).HeightOverride(T)
					[ SNew(SColorBlock).Color_Lambda(EdgeColor) ]
				]
				// Left edge.
				+ SOverlay::Slot()
				.HAlign(HAlign_Left)
				[
					SNew(SBox).WidthOverride(T)
					[ SNew(SColorBlock).Color_Lambda(EdgeColor) ]
				]
				// Right edge.
				+ SOverlay::Slot()
				.HAlign(HAlign_Right)
				[
					SNew(SBox).WidthOverride(T)
					[ SNew(SColorBlock).Color_Lambda(EdgeColor) ]
				];
			break;
		}

		case EStructCategoryHeaderStyle::LeftBarAndTint:
			{
				// Left accent stripe + a subtle wash of the same color across the row.
				headerContent =
				   SNew(SBorder)
				   .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				   .VAlign(VAlign_Fill)
				   .BorderBackgroundColor_Lambda([category_path]()
				   {
					  const FLinearColor c = GetSwatchColorForPath(category_path);
					  return FLinearColor(c.R, c.G, c.B, 0.18f);
				   })
				   .Padding(FMargin(0.f))
				   [
					  SNew(SHorizontalBox)
					  + SHorizontalBox::Slot()
					  .AutoWidth()
					  .VAlign(VAlign_Fill) // Ensures the slot stretches top-to-bottom
					  [
						 SNew(SBox)
						 .WidthOverride(4.f)
						 [
							SNew(SColorBlock)
							.Color_Lambda([category_path]() { return GetSwatchColorForPath(category_path); })
						 ]
					  ]
					  + SHorizontalBox::Slot()
					  .Padding(FMargin(6.f, 0.f))
					  .VAlign(VAlign_Center)
					  [
						 MakeContrastLabel(/*bBold*/ true, /*bContrastToFill*/ false)
					  ]
					  + SHorizontalBox::Slot()
					  .AutoWidth()
					  .HAlign(HAlign_Right)
					  .VAlign(VAlign_Center)
					  .Padding(FMargin(0.f, 0.f, 4.f, 0.f))
					  [
						 SNullWidget::NullWidget
					  ]
				   ];
				break;
			}

		case EStructCategoryHeaderStyle::SolidFill:
		default:
		{
			// Solid fill, regular-weight label.
			headerContent =
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.VAlign(VAlign_Center)
				.BorderBackgroundColor_Lambda([category_path]() { return GetSwatchColorForPath(category_path); })
				.Padding(FMargin(6.f, 0.f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					[
						MakeContrastLabel(/*bBold*/ false, /*bContrastToFill*/ true)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.HAlign(HAlign_Right)
					.VAlign(VAlign_Center)
					[
						SNullWidget::NullWidget
					]
				];
			break;
		}
		}

		// Pin to the stock category header height so styling never grows the row.
		// Wrap in a right-click zone that opens the palette menu at the cursor.
		return SNew(SBox)
			.HeightOverride(22.f)
			[
				SNew(SStructHeaderRightClickZone)
				.MenuContentBuilder_Lambda([this, category_path]() { return BuildPaletteMenuContent(category_path); })
				.ExtraToolTipText(extra_tooltip)
				[
					headerContent
				]
			];
	}
	else
	{
		// UNASSIGNED (None): keep Unreal's stock header look (default bold label),
		// but wrap it in the right-click zone so the user can still assign a color.
		return SNew(SStructHeaderRightClickZone)
			.MenuContentBuilder_Lambda([this, category_path]() { return BuildPaletteMenuContent(category_path); })
			.ExtraToolTipText(extra_tooltip)
			[
				SNew(SBox)
				.HeightOverride(22.f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(leaf_label))
					.Font(IDetailLayoutBuilder::GetDetailFontBold())
				]
			];
	}
}

void FStructCategoryGroups::ApplyColoredHeader(IDetailGroup& group,
                                               const FString& category_path,
                                               const FString& leaf_label)
{
	group.HeaderRow().WholeRowContent()
	[
		BuildColoredHeaderContent(category_path, leaf_label)
	];
}

IDetailGroup* FStructCategoryGroups::ResolveGroup(IDetailChildrenBuilder& child_builder,
                                                   const TArray<FString>& path,
                                                   TMap<FString, IDetailGroup*>& groups_by_path,
                                                   const TSet<FString>& colored_paths)
{
	IDetailGroup* current = nullptr;
	FString accumulated;

	for (const FString& segment : path)
	{
		accumulated = accumulated.IsEmpty() ? segment : (accumulated + TEXT("|") + segment);

		if (IDetailGroup** existing = groups_by_path.Find(accumulated))
		{
			current = *existing;
			continue;
		}

		IDetailGroup& newGroup = current
			? current->AddGroup(FName(*accumulated), FText::FromString(segment))
			: child_builder.AddGroup(FName(*accumulated), FText::FromString(segment));

		// If this exact path was flagged ColoredCategory, give it a colored header
		// with a palette dropdown.
		if (colored_paths.Contains(accumulated))
		{
			ApplyColoredHeader(newGroup, accumulated, segment);
		}

		groups_by_path.Add(accumulated, &newGroup);
		current = &newGroup;
	}

	return current;
}

void FStructCategoryGroups::CustomizeChildren(TSharedRef<IPropertyHandle> property_handle,
                                              IDetailChildrenBuilder& child_builder,
                                              IPropertyTypeCustomizationUtils& customization_utils)
{
	// Cache so the header color dropdown can force a panel rebuild on change.
	m_PropertyUtilities = customization_utils.GetPropertyUtilities();

	uint32 numChildren = 0;
	property_handle->GetNumChildren(numChildren);

	// Helper: compute the normalized category path for a child (shared by both passes).
	auto ComputePath = [this](const TSharedPtr<IPropertyHandle>& childHandle, TArray<FString>& outPath)
	{
		outPath.Reset();
		FString category = childHandle->GetMetaData(TEXT("Category"));
		if (category.IsEmpty())
		{
			return;
		}

		category.ParseIntoArray(outPath, TEXT("|"), /*InCullEmpty*/ true);
		for (FString& segment : outPath)
		{
			segment.TrimStartAndEndInline();
		}

		if (outPath.Num() > 0 && !m_StructDisplayName.IsEmpty()
			&& outPath[0].Equals(m_StructDisplayName, ESearchCase::IgnoreCase))
		{
			outPath.RemoveAt(0);
		}
	};

	// PASS 1: collect every category path that exists. Every category in a
	// UseCategoryGroups struct is colored automatically .
	TSet<FString> coloredPaths;
	for (uint32 index = 0; index < numChildren; ++index)
	{
		TSharedPtr<IPropertyHandle> childHandle = property_handle->GetChildHandle(index);
		if (!childHandle.IsValid() || !childHandle->GetProperty())
		{
			continue;
		}

		TArray<FString> path;
		ComputePath(childHandle, path);
		if (path.Num() == 0)
		{
			continue; // uncategorized property, nothing to color.
		}

		FString accumulated;
		for (const FString& segment : path)
		{
			accumulated = accumulated.IsEmpty() ? segment : (accumulated + TEXT("|") + segment);
			coloredPaths.Add(accumulated);
		}
	}

	// PASS 2: build groups and place properties.
	TMap<FString, IDetailGroup*> groupsByPath;
	for (uint32 index = 0; index < numChildren; ++index)
	{
		TSharedPtr<IPropertyHandle> childHandle = property_handle->GetChildHandle(index);
		if (!childHandle.IsValid() || !childHandle->GetProperty())
		{
			continue;
		}

		TArray<FString> path;
		ComputePath(childHandle, path);

		if (path.Num() == 0)
		{
			child_builder.AddProperty(childHandle.ToSharedRef());
			continue;
		}

		if (IDetailGroup* group = ResolveGroup(child_builder, path, groupsByPath, coloredPaths))
		{
			group->AddPropertyRow(childHandle.ToSharedRef());
		}
		else
		{
			child_builder.AddProperty(childHandle.ToSharedRef());
		}
	}
}

#undef LOCTEXT_NAMESPACE