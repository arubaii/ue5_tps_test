// Copyright AR GameSystems 2026. All rights reserved.

#include "SearchBar.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/EditableText.h"
#include "Components/EditableTextBox.h"
#include "Components/ListView.h"
#include "Components/PanelWidget.h"
#include "Components/RichTextBlock.h"
#include "Components/TextBlock.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Kismet/GameplayStatics.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "NameTBD/Menu/Widgets/SMenuImage.h"
#include "NameTBD/Search/SearchableItemInterface.h"
#include "NameTBD/Widgets/WidgetEditorHooks.h"
#include "NameTBD/Widgets/WidgetTreeLookup.h"

#define LOCTEXT_NAMESPACE "SearchBar"

namespace SearchBarPrivate
{
	/** The All entry's key: never a key of the game's own, and reported as None. */
	const FName AllKey(TEXT("__SearchBar_All__"));

	/** List items with this many items or more are only partly read for suggestions (the first ones). */
	constexpr int32 MaxSuggestionSources = 500;

	/** The engine's default UI font as an asset, so a saved style keeps a real font. Constructors only. */
	FSlateFontInfo MakeFontAsset(const TCHAR* Typeface, int32 Size)
	{
		static ConstructorHelpers::FObjectFinder<UFont> DefaultFont(*UWidget::GetDefaultFontName());
		if (DefaultFont.Object)
		{
			return FSlateFontInfo(DefaultFont.Object, Size, FName(Typeface));
		}
		return FCoreStyle::GetDefaultFontStyle(FName(Typeface), Size);
	}

	FSlateBrush MakeRounded(const FLinearColor& Fill, float Radius, const FLinearColor& Outline, float OutlineWidth, bool bPill = false)
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.TintColor = FSlateColor(Fill);
		Brush.OutlineSettings.CornerRadii = FVector4(Radius, Radius, Radius, Radius);
		Brush.OutlineSettings.Color = FSlateColor(Outline);
		Brush.OutlineSettings.Width = OutlineWidth;
		Brush.OutlineSettings.RoundingType = bPill ? ESlateBrushRoundingType::HalfHeightRadius : ESlateBrushRoundingType::FixedRadius;
		return Brush;
	}

	const FSlateBrush* NoBrush()
	{
		static const FSlateNoResource Brush;
		return &Brush;
	}

	/** Rows that draw nothing of their own: the row's highlight is the style's Suggestion Highlight. */
	const FTableRowStyle* PlainRowStyle()
	{
		static const FTableRowStyle Style = []()
		{
			FTableRowStyle Made;
			const FSlateNoResource NoResource;
			Made.SetSelectorFocusedBrush(NoResource)
				.SetActiveHoveredBrush(NoResource)
				.SetActiveBrush(NoResource)
				.SetInactiveHoveredBrush(NoResource)
				.SetInactiveBrush(NoResource)
				.SetActiveHighlightedBrush(NoResource)
				.SetInactiveHighlightedBrush(NoResource)
				.SetEvenRowBackgroundHoveredBrush(NoResource)
				.SetEvenRowBackgroundBrush(NoResource)
				.SetOddRowBackgroundHoveredBrush(NoResource)
				.SetOddRowBackgroundBrush(NoResource);
			return Made;
		}();
		return &Style;
	}

	/** A Rich Text Block's text without its tags. */
	FString StripTags(const FString& Rich)
	{
		FString Out;
		Out.Reserve(Rich.Len());
		bool bInTag = false;
		for (const TCHAR Char : Rich)
		{
			if (Char == TEXT('<'))
			{
				bInTag = true;
			}
			else if (Char == TEXT('>') && bInTag)
			{
				bInTag = false;
			}
			else if (!bInTag)
			{
				Out.AppendChar(Char);
			}
		}
		return Out;
	}

	/** Every piece of text shown in Widget and what it holds (text blocks, rich text, editable text), in tree order. */
	void GatherWidgetTexts(const UWidget* Widget, TArray<FString>& Out, int32 Depth)
	{
		if (!Widget || Depth > 10)
		{
			return;
		}
		if (Depth > 0 && Widget->GetVisibility() == ESlateVisibility::Collapsed)
		{
			return;
		}

		FString Found;
		if (const UTextBlock* TextBlock = Cast<UTextBlock>(Widget))
		{
			Found = TextBlock->GetText().ToString();
		}
		else if (const URichTextBlock* RichText = Cast<URichTextBlock>(Widget))
		{
			Found = StripTags(RichText->GetText().ToString());
		}
		else if (const UEditableText* Editable = Cast<UEditableText>(Widget))
		{
			Found = Editable->GetText().ToString();
		}
		else if (const UEditableTextBox* EditableBox = Cast<UEditableTextBox>(Widget))
		{
			Found = EditableBox->GetText().ToString();
		}
		Found.TrimStartAndEndInline();
		if (!Found.IsEmpty())
		{
			Out.Add(Found);
		}

		if (const UUserWidget* User = Cast<UUserWidget>(Widget))
		{
			if (User->WidgetTree && User->WidgetTree->RootWidget)
			{
				GatherWidgetTexts(User->WidgetTree->RootWidget, Out, Depth + 1);
			}
		}
		else if (const UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		{
			for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
			{
				GatherWidgetTexts(Panel->GetChildAt(Index), Out, Depth + 1);
			}
		}
	}

	/** A list item's name, read from a property most item objects have one of. */
	FString ReadLabelProperty(const UObject* Object)
	{
		static const FName Names[] = { TEXT("DisplayName"), TEXT("Name"), TEXT("ItemName"), TEXT("Title"), TEXT("Label"), TEXT("Text") };
		for (const FName& Name : Names)
		{
			const FProperty* Property = Object->GetClass()->FindPropertyByName(Name);
			if (!Property)
			{
				continue;
			}
			FString Value;
			if (const FTextProperty* TextProperty = CastField<FTextProperty>(Property))
			{
				Value = TextProperty->GetPropertyValue_InContainer(Object).ToString();
			}
			else if (const FStrProperty* StringProperty = CastField<FStrProperty>(Property))
			{
				Value = StringProperty->GetPropertyValue_InContainer(Object);
			}
			else if (const FNameProperty* NameProperty = CastField<FNameProperty>(Property))
			{
				const FName Read = NameProperty->GetPropertyValue_InContainer(Object);
				Value = Read.IsNone() ? FString() : Read.ToString();
			}
			if (!Value.IsEmpty())
			{
				return Value;
			}
		}
		return FString();
	}

	/** The vector icons drawn when the style gives no picture: a magnifier, a cross, a clock (a recent search). */
	enum class EGlyph : uint8
	{
		Search,
		Clear,
		Recent
	};

	class SSearchBarGlyph : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SSearchBarGlyph)
			: _Glyph(EGlyph::Search)
			, _Size(16.f)
		{}
			SLATE_ARGUMENT(EGlyph, Glyph)
			SLATE_ARGUMENT(float, Size)
			SLATE_ATTRIBUTE(FLinearColor, Color)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			Glyph = InArgs._Glyph;
			GlyphSize = InArgs._Size;
			GlyphColor = InArgs._Color;
		}

		virtual FVector2D ComputeDesiredSize(float) const override
		{
			return FVector2D(GlyphSize, GlyphSize);
		}

		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
			FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
		{
			const FVector2f Local(AllottedGeometry.GetLocalSize());
			const float Side = FMath::Min(Local.X, Local.Y);
			if (Side <= 0.f)
			{
				return LayerId;
			}
			const FVector2f Origin = (Local - FVector2f(Side, Side)) * 0.5f;
			auto At = [&Origin, Side](float X, float Y) { return Origin + FVector2f(X, Y) * Side; };
			const FLinearColor Tint = GlyphColor.Get(FLinearColor::White) * InWidgetStyle.GetColorAndOpacityTint();
			const float Thickness = FMath::Max(1.5f, Side * 0.11f);
			const FPaintGeometry PaintGeometry = AllottedGeometry.ToPaintGeometry();
			auto Line = [&](TArray<FVector2f> Points)
			{
				FSlateDrawElement::MakeLines(OutDrawElements, LayerId, PaintGeometry, MoveTemp(Points), ESlateDrawEffect::None, Tint, /*bAntialias=*/true, Thickness);
			};
			auto Circle = [&](const FVector2f& Center, float Radius)
			{
				TArray<FVector2f> Points;
				for (int32 Step = 0; Step <= 28; ++Step)
				{
					const float Angle = UE_TWO_PI * Step / 28.f;
					Points.Add(Center + FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
				}
				Line(MoveTemp(Points));
			};

			switch (Glyph)
			{
			case EGlyph::Search:
			{
				const FVector2f Center = At(0.42f, 0.42f);
				const float Radius = Side * 0.29f;
				Circle(Center, Radius);
				const FVector2f Out = FVector2f(UE_INV_SQRT_2, UE_INV_SQRT_2);
				Line({ Center + Out * Radius, At(0.9f, 0.9f) });
				break;
			}
			case EGlyph::Clear:
				Line({ At(0.25f, 0.25f), At(0.75f, 0.75f) });
				Line({ At(0.75f, 0.25f), At(0.25f, 0.75f) });
				break;
			case EGlyph::Recent:
				Circle(At(0.5f, 0.5f), Side * 0.38f);
				Line({ At(0.5f, 0.28f), At(0.5f, 0.5f), At(0.66f, 0.6f) });
				break;
			}
			return LayerId;
		}

	private:
		EGlyph Glyph = EGlyph::Search;
		float GlyphSize = 16.f;
		TAttribute<FLinearColor> GlyphColor;
	};

	/** The style's picture when it has one, else the vector glyph, Size across, in Color. */
	TSharedRef<SWidget> MakeIcon(const FMenuImage& Image, EGlyph Glyph, float Size, const TAttribute<FLinearColor>& Color)
	{
		if (Image.HasImage())
		{
			return SNew(SMenuImage)
				.Image(Image)
				.ImageSize(FVector2D(Size, Size))
				.Tint_Lambda([Color]() { return FSlateColor(Color.Get(FLinearColor::White)); });
		}
		return SNew(SSearchBarGlyph).Glyph(Glyph).Size(Size).Color(Color);
	}
}

// =====================================================================================================
// Style
// =====================================================================================================

FSearchBarStyle::FSearchBarStyle()
{
	FillLook(*this, ESearchBarLook::Dark, FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 12));
}

FSearchBarStyle FSearchBarStyle::MakeLook(ESearchBarLook InLook, const FSlateFontInfo& InFont)
{
	FSearchBarStyle Made;
	FillLook(Made, InLook, InFont);
	return Made;
}

void FSearchBarStyle::FillLook(FSearchBarStyle& Out, ESearchBarLook InLook, const FSlateFontInfo& InFont)
{
	using namespace SearchBarPrivate;

	Out.Font = InFont;
	Out.SecondaryFont = InFont;
	Out.SecondaryFont.Size = FMath::Max(InFont.Size - 2.f, 6.f);

	const FLinearColor Accent(0.35f, 0.6f, 1.f, 1.f);
	Out.PopupBackground = MakeRounded(FLinearColor(0.028f, 0.03f, 0.038f, 0.98f), 6.f, FLinearColor(1.f, 1.f, 1.f, 0.1f), 1.f);
	Out.SuggestionHighlight = MakeRounded(FLinearColor(0.24f, 0.42f, 0.92f, 0.35f), 4.f, FLinearColor::Transparent, 0.f);

	switch (InLook)
	{
	case ESearchBarLook::Light:
	{
		const FLinearColor LightAccent(0.15f, 0.4f, 0.95f, 1.f);
		Out.Background = MakeRounded(FLinearColor(0.96f, 0.96f, 0.97f, 1.f), 6.f, FLinearColor(0.f, 0.f, 0.f, 0.12f), 1.f);
		Out.HoveredBackground = MakeRounded(FLinearColor(0.98f, 0.98f, 0.99f, 1.f), 6.f, FLinearColor(0.f, 0.f, 0.f, 0.25f), 1.f);
		Out.FocusedBackground = MakeRounded(FLinearColor::White, 6.f, LightAccent, 1.5f);
		Out.TextColor = FLinearColor(0.08f, 0.08f, 0.1f, 1.f);
		Out.HintColor = FLinearColor(0.f, 0.f, 0.f, 0.45f);
		Out.IconColor = FLinearColor(0.f, 0.f, 0.f, 0.5f);
		Out.FocusedIconColor = LightAccent;
		Out.ClearColor = FLinearColor(0.f, 0.f, 0.f, 0.45f);
		Out.ClearHoveredColor = FLinearColor(0.f, 0.f, 0.f, 0.9f);
		Out.SecondaryColor = FLinearColor(0.f, 0.f, 0.f, 0.5f);
		Out.PopupBackground = MakeRounded(FLinearColor::White, 6.f, FLinearColor(0.f, 0.f, 0.f, 0.12f), 1.f);
		Out.SuggestionHighlight = MakeRounded(FLinearColor(0.15f, 0.4f, 0.95f, 0.14f), 4.f, FLinearColor::Transparent, 0.f);
		Out.SuggestionColor = FLinearColor(0.1f, 0.1f, 0.12f, 1.f);
		Out.MatchColor = FLinearColor(0.1f, 0.35f, 0.9f, 1.f);
		break;
	}

	case ESearchBarLook::Pill:
		Out.Padding = FMargin(16.f, 8.f);
		Out.Background = MakeRounded(FLinearColor(0.02f, 0.022f, 0.028f, 0.92f), 0.f, FLinearColor(1.f, 1.f, 1.f, 0.1f), 1.f, /*bPill=*/true);
		Out.HoveredBackground = MakeRounded(FLinearColor(0.035f, 0.037f, 0.045f, 0.94f), 0.f, FLinearColor(1.f, 1.f, 1.f, 0.22f), 1.f, true);
		Out.FocusedBackground = MakeRounded(FLinearColor(0.015f, 0.017f, 0.022f, 0.96f), 0.f, Accent, 1.5f, true);
		break;

	case ESearchBarLook::Minimal:
		Out.Padding = FMargin(6.f, 6.f);
		Out.Background = MakeRounded(FLinearColor::Transparent, 4.f, FLinearColor::Transparent, 0.f);
		Out.HoveredBackground = MakeRounded(FLinearColor(1.f, 1.f, 1.f, 0.04f), 4.f, FLinearColor(1.f, 1.f, 1.f, 0.12f), 1.f);
		Out.FocusedBackground = MakeRounded(FLinearColor(1.f, 1.f, 1.f, 0.05f), 4.f, FLinearColor(1.f, 1.f, 1.f, 0.3f), 1.f);
		break;

	case ESearchBarLook::Dark:
	case ESearchBarLook::Custom:
	default:
		Out.Background = MakeRounded(FLinearColor(0.02f, 0.022f, 0.028f, 0.92f), 6.f, FLinearColor(1.f, 1.f, 1.f, 0.1f), 1.f);
		Out.HoveredBackground = MakeRounded(FLinearColor(0.035f, 0.037f, 0.045f, 0.94f), 6.f, FLinearColor(1.f, 1.f, 1.f, 0.22f), 1.f);
		Out.FocusedBackground = MakeRounded(FLinearColor(0.015f, 0.017f, 0.022f, 0.96f), 6.f, Accent, 1.5f);
		break;
	}
}

USearchBarStyleAsset::USearchBarStyleAsset()
{
	Style = FSearchBarStyle::MakeLook(ESearchBarLook::Dark, SearchBarPrivate::MakeFontAsset(TEXT("Regular"), 12));
}

// =====================================================================================================
// Slate host
// =====================================================================================================

void SSearchBarHost::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		InArgs._Content.Widget
	];
}

void SSearchBarHost::OnFocusChanging(const FWeakWidgetPath& PreviousFocusPath, const FWidgetPath& NewWidgetPath, const FFocusEvent& InFocusEvent)
{
	SCompoundWidget::OnFocusChanging(PreviousFocusPath, NewWidgetPath, InFocusEvent);

	bool bInside = NewWidgetPath.ContainsWidget(this);
	for (const TWeakPtr<SWidget>& Weak : AlsoInside)
	{
		const TSharedPtr<SWidget> Pinned = Weak.Pin();
		bInside |= Pinned.IsValid() && NewWidgetPath.ContainsWidget(Pinned.Get());
	}
	if (bInside != bHadFocus)
	{
		bHadFocus = bInside;
		if (OnFocusMoved)
		{
			OnFocusMoved(bInside);
		}
	}
}

// =====================================================================================================
// Search Bar
// =====================================================================================================

USearchBar::USearchBar(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	HintText = LOCTEXT("Hint", "Search");
	AllFilterLabel = LOCTEXT("AllFilter", "All");
	ResultCountFormat = LOCTEXT("ResultCount", "{0} {0}|plural(one=result,other=results)");
	SearchStyle = FSearchBarStyle::MakeLook(ESearchBarLook::Dark, SearchBarPrivate::MakeFontAsset(TEXT("Regular"), 12));
}

#if WITH_EDITOR
const FText USearchBar::GetPaletteCategory()
{
	return LOCTEXT("PaletteCategory", "NameTBD");
}

void USearchBar::PreEditChange(FProperty* PropertyAboutToChange)
{
	if (PropertyAboutToChange && PropertyAboutToChange->GetFName() == GET_MEMBER_NAME_CHECKED(USearchBar, Look))
	{
		LookBeforeEdit = Look;
	}
	Super::PreEditChange(PropertyAboutToChange);
}

void USearchBar::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// Custom starts from the look it was, so one colour can be changed without building a style from nothing.
	if (PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(USearchBar, Look)
		&& Look == ESearchBarLook::Custom && LookBeforeEdit != ESearchBarLook::Custom)
	{
		SearchStyle = FSearchBarStyle::MakeLook(LookBeforeEdit, SearchStyle.Font);

		// The Details panel edits the designer's preview copy and hands only Look on to the Widget Blueprint's own widget.
		if (USearchBar* Template = Cast<USearchBar>(WidgetEditorHooks::FindDesignerTemplate(this)))
		{
			Template->Modify();
			Template->SearchStyle = SearchStyle;
			Template->MarkPackageDirty();
		}
	}
	LookStyleMadeFor = ESearchBarLook::Custom;
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

const FSearchBarStyle& USearchBar::GetActiveStyle() const
{
	if (StyleAsset)
	{
		return StyleAsset->Style;
	}
	if (Look == ESearchBarLook::Custom)
	{
		return SearchStyle;
	}
	if (LookStyleMadeFor != Look)
	{
		LookStyle = FSearchBarStyle::MakeLook(Look, SearchStyle.Font);
		LookStyleMadeFor = Look;
	}
	return LookStyle;
}

TSharedRef<SWidget> USearchBar::RebuildWidget()
{
	using namespace SearchBarPrivate;

	const TWeakObjectPtr<USearchBar> WeakBar(this);
	CurrentQuery = Text.ToString();
	RebuildFilterItems();

	// The suggestions, under the bar, as wide as it.
	SlatePopup = SNew(SBorder)
		.BorderImage_Lambda([WeakBar]() { const USearchBar* Bar = WeakBar.Get(); return Bar ? &Bar->GetActiveStyle().PopupBackground : NoBrush(); })
		.Padding(FMargin(4.f))
		[
			SNew(SBox)
			.MaxDesiredHeight(360.f)
			[
				SAssignNew(SlateSuggestions, SListView<TSharedPtr<FSearchBarSuggestion>>)
				.ListItemsSource(&SuggestionItems)
				.SelectionMode(ESelectionMode::Single)
				.IsFocusable(false)
				.OnGenerateRow_UObject(this, &USearchBar::MakeSuggestionRow)
				.OnMouseButtonClick_Lambda([WeakBar](TSharedPtr<FSearchBarSuggestion> Item)
				{
					if (USearchBar* Bar = WeakBar.Get())
					{
						Bar->PickSuggestion(Item);
					}
				})
			]
		];

	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox)

		// The magnifier.
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(TAttribute<FMargin>::CreateLambda([WeakBar]()
		{
			const USearchBar* Bar = WeakBar.Get();
			return FMargin(0.f, 0.f, Bar ? Bar->GetActiveStyle().Spacing : 8.f, 0.f);
		}))
		[
			SAssignNew(SlateSearchIcon, SBox)
		]

		// The field, its hint over it while it is empty.
		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		.VAlign(VAlign_Center)
		[
			SNew(SOverlay)

			+ SOverlay::Slot()
			.VAlign(VAlign_Center)
			[
				SAssignNew(SlateText, SEditableText)
				.Text(Text)
				.Font_Lambda([WeakBar]() { const USearchBar* Bar = WeakBar.Get(); return Bar ? Bar->GetActiveStyle().Font : FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 12); })
				.ColorAndOpacity_Lambda([WeakBar]() { const USearchBar* Bar = WeakBar.Get(); return FSlateColor(Bar ? Bar->GetActiveStyle().TextColor : FLinearColor::White); })
				.SelectAllTextWhenFocused_Lambda([WeakBar]() { const USearchBar* Bar = WeakBar.Get(); return Bar && Bar->bSelectAllOnFocus; })
				.ClearKeyboardFocusOnCommit(false)
				.RevertTextOnEscape(false)
				.OnTextChanged_UObject(this, &USearchBar::HandleTextChanged)
				.OnTextCommitted_UObject(this, &USearchBar::HandleTextCommitted)
				.OnKeyDownHandler_UObject(this, &USearchBar::HandleKeyDown)
			]

			+ SOverlay::Slot()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text_Lambda([WeakBar]() { const USearchBar* Bar = WeakBar.Get(); return Bar ? Bar->HintText : FText::GetEmpty(); })
				.Font_Lambda([WeakBar]() { const USearchBar* Bar = WeakBar.Get(); return Bar ? Bar->GetActiveStyle().Font : FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 12); })
				.ColorAndOpacity_Lambda([WeakBar]() { const USearchBar* Bar = WeakBar.Get(); return FSlateColor(Bar ? Bar->GetActiveStyle().HintColor : FLinearColor::Gray); })
				.Visibility_Lambda([WeakBar]()
				{
					const USearchBar* Bar = WeakBar.Get();
					return Bar && Bar->CurrentQuery.IsEmpty() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
				})
			]
		]

		// How many match.
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(FMargin(8.f, 0.f, 0.f, 0.f))
		[
			SNew(STextBlock)
			.Text_Lambda([WeakBar]() { const USearchBar* Bar = WeakBar.Get(); return Bar ? Bar->GetResultCountText() : FText::GetEmpty(); })
			.Font_Lambda([WeakBar]() { const USearchBar* Bar = WeakBar.Get(); return Bar ? Bar->GetActiveStyle().SecondaryFont : FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 10); })
			.ColorAndOpacity_Lambda([WeakBar]() { const USearchBar* Bar = WeakBar.Get(); return FSlateColor(Bar ? Bar->GetActiveStyle().SecondaryColor : FLinearColor::Gray); })
			.Visibility_Lambda([WeakBar]()
			{
				const USearchBar* Bar = WeakBar.Get();
				return Bar && Bar->bShowResultCount && Bar->ResultCount >= 0 && Bar->IsSearching() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
			})
		]

		// Clear, while something is typed.
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(FMargin(6.f, 0.f, 0.f, 0.f))
		[
			SNew(SButton)
			.ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
			.ContentPadding(FMargin(2.f))
			.IsFocusable(false)
			.ToolTipText(LOCTEXT("ClearTooltip", "Clear"))
			.OnClicked_Lambda([WeakBar]()
			{
				if (USearchBar* Bar = WeakBar.Get())
				{
					Bar->ClearSearch();
					Bar->FocusSearch();
				}
				return FReply::Handled();
			})
			.Visibility_Lambda([WeakBar]()
			{
				const USearchBar* Bar = WeakBar.Get();
				return Bar && Bar->bShowClearButton && !Bar->CurrentQuery.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed;
			})
			[
				SAssignNew(SlateClearIcon, SBox)
			]
		]

		// The filter, when there are categories.
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(FMargin(8.f, 0.f, 0.f, 0.f))
		[
			SAssignNew(SlateFilter, SComboBox<TSharedPtr<FSearchFilterOption>>)
			.OptionsSource(&FilterItems)
			.Method(EPopupMethod::UseCurrentWindow)
			.OnGenerateWidget_Lambda([WeakBar](TSharedPtr<FSearchFilterOption> Option) -> TSharedRef<SWidget>
			{
				const USearchBar* Bar = WeakBar.Get();
				return Bar ? Bar->MakeFilterRow(Option) : SNullWidget::NullWidget;
			})
			.OnSelectionChanged_Lambda([WeakBar](TSharedPtr<FSearchFilterOption> Option, ESelectInfo::Type SelectInfo)
			{
				if (USearchBar* Bar = WeakBar.Get())
				{
					Bar->HandleFilterPicked(Option, SelectInfo);
				}
			})
			.Visibility_Lambda([WeakBar]()
			{
				const USearchBar* Bar = WeakBar.Get();
				return Bar && Bar->FilterItems.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed;
			})
			[
				SNew(STextBlock)
				.Text_Lambda([WeakBar]() { const USearchBar* Bar = WeakBar.Get(); return Bar ? Bar->GetActiveFilterLabel() : FText::GetEmpty(); })
				.Font_Lambda([WeakBar]() { const USearchBar* Bar = WeakBar.Get(); return Bar ? Bar->GetActiveStyle().SecondaryFont : FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 10); })
			]
		];

	SAssignNew(SlateHost, SSearchBarHost)
	[
		SAssignNew(SlateAnchor, SMenuAnchor)
		.Placement(MenuPlacement_ComboBox)
		.Method(EPopupMethod::UseCurrentWindow)
		.MenuContent(SlatePopup)
		[
			SNew(SBorder)
			.BorderImage_Lambda([WeakBar]() -> const FSlateBrush*
			{
				const USearchBar* Bar = WeakBar.Get();
				if (!Bar)
				{
					return NoBrush();
				}
				const FSearchBarStyle& Active = Bar->GetActiveStyle();
				if (Bar->bFocused)
				{
					return &Active.FocusedBackground;
				}
				return Bar->SlateHost.IsValid() && Bar->SlateHost->IsHovered() ? &Active.HoveredBackground : &Active.Background;
			})
			.Padding(TAttribute<FMargin>::CreateLambda([WeakBar]()
			{
				const USearchBar* Bar = WeakBar.Get();
				return Bar ? Bar->GetActiveStyle().Padding : FMargin(12.f, 8.f);
			}))
			[
				Row
			]
		]
	];

	SlateHost->AlsoInside.Add(SlatePopup);
	SlateHost->OnFocusMoved = [WeakBar](bool bHasFocus)
	{
		if (USearchBar* Bar = WeakBar.Get())
		{
			Bar->HandleFocusMoved(bHasFocus);
		}
	};
	return SlateHost.ToSharedRef();
}

void USearchBar::SynchronizeProperties()
{
	using namespace SearchBarPrivate;

	Super::SynchronizeProperties();
	if (!SlateHost.IsValid())
	{
		return;
	}

	LookStyleMadeFor = ESearchBarLook::Custom;
	const FSearchBarStyle& Active = GetActiveStyle();
	const TWeakObjectPtr<USearchBar> WeakBar(this);
	if (SlateSearchIcon.IsValid())
	{
		SlateSearchIcon->SetContent(MakeIcon(Active.SearchIcon, EGlyph::Search, Active.IconSize, TAttribute<FLinearColor>::CreateLambda([WeakBar]()
		{
			const USearchBar* Bar = WeakBar.Get();
			return Bar ? (Bar->bFocused ? Bar->GetActiveStyle().FocusedIconColor : Bar->GetActiveStyle().IconColor) : FLinearColor::White;
		})));
	}
	if (SlateClearIcon.IsValid())
	{
		const TWeakPtr<SBox> WeakIcon = SlateClearIcon;
		SlateClearIcon->SetContent(MakeIcon(Active.ClearIcon, EGlyph::Clear, Active.IconSize * 0.75f, TAttribute<FLinearColor>::CreateLambda([WeakBar, WeakIcon]()
		{
			const USearchBar* Bar = WeakBar.Get();
			const TSharedPtr<SBox> Icon = WeakIcon.Pin();
			const bool bHovered = Icon.IsValid() && Icon->IsHovered();
			return Bar ? (bHovered ? Bar->GetActiveStyle().ClearHoveredColor : Bar->GetActiveStyle().ClearColor) : FLinearColor::White;
		})));
	}

	if (SlateText.IsValid() && !SlateText->GetText().EqualTo(Text))
	{
		TGuardValue<bool> Guard(bSettingText, true);
		SlateText->SetText(Text);
	}
	CurrentQuery = Text.ToString();
	RebuildFilterItems();

	if (!IsDesignTime())
	{
		LoadRecentSearches();

		// What is typed from the start (Text set in the designer, or before the bar was shown) filters at once.
		const int32 Count = ApplyToResults();
		UpdateNoResults(Count);
		ResultCount = Count;
	}
}

void USearchBar::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DebounceTimer);
	}
	SlateHost.Reset();
	SlateText.Reset();
	SlateAnchor.Reset();
	SlatePopup.Reset();
	SlateSuggestions.Reset();
	SlateFilter.Reset();
	SlateSearchIcon.Reset();
	SlateClearIcon.Reset();
	bFocused = false;
}

// ---------------------------------------------------------------------------------------------
// Typing
// ---------------------------------------------------------------------------------------------

void USearchBar::HandleTextChanged(const FText& NewText)
{
	if (bSettingText)
	{
		return;
	}
	Text = NewText;
	CurrentQuery = NewText.ToString();
	UpdateSuggestions();
	ScheduleSearch();
}

void USearchBar::HandleTextCommitted(const FText& NewText, ETextCommit::Type CommitMethod)
{
	if (bSettingText || CommitMethod != ETextCommit::OnEnter)
	{
		return;
	}
	Text = NewText;
	CurrentQuery = NewText.ToString();
	AddRecentSearch(CurrentQuery);
	OpenSuggestions(false);
	SearchNow();
	OnSearchCommitted.Broadcast(CurrentQuery);
}

FReply USearchBar::HandleKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent)
{
	const FKey Key = KeyEvent.GetKey();
	const bool bOpen = SlateAnchor.IsValid() && SlateAnchor->IsOpen() && SuggestionItems.Num() > 0;

	if (bOpen && (Key == EKeys::Down || Key == EKeys::Gamepad_DPad_Down))
	{
		MoveSuggestionHighlight(1);
		return FReply::Handled();
	}
	if (bOpen && (Key == EKeys::Up || Key == EKeys::Gamepad_DPad_Up))
	{
		MoveSuggestionHighlight(-1);
		return FReply::Handled();
	}
	if (bOpen && (Key == EKeys::Enter || Key == EKeys::Tab) && SuggestionItems.IsValidIndex(HighlightedSuggestion))
	{
		PickSuggestion(SuggestionItems[HighlightedSuggestion]);
		return FReply::Handled();
	}
	if (Key == EKeys::Escape)
	{
		if (bOpen)
		{
			OpenSuggestions(false);
			return FReply::Handled();
		}
		if (bClearOnEscape && !CurrentQuery.IsEmpty())
		{
			ClearSearch();
			return FReply::Handled();
		}
	}

	// Escape on an empty bar, and everything else: the field's, or the screen's (its Back).
	return FReply::Unhandled();
}

void USearchBar::HandleFocusMoved(bool bHasFocus)
{
	bFocused = bHasFocus;
	if (bHasFocus)
	{
		// Recent searches while nothing is typed yet, matches otherwise.
		UpdateSuggestions();
	}
	else
	{
		OpenSuggestions(false);
	}
	OnSearchFocusChanged.Broadcast(bHasFocus);
}

void USearchBar::ScheduleSearch()
{
	UWorld* World = GetWorld();
	if (!World || IsDesignTime() || DebounceSeconds <= 0.f)
	{
		SearchNow();
		return;
	}

	// Restarted by every key: a burst of typing searches once, at its end.
	World->GetTimerManager().SetTimer(DebounceTimer, this, &USearchBar::SearchNow, DebounceSeconds, false);
}

void USearchBar::SearchNow()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DebounceTimer);
	}
	const int32 Count = ApplyToResults();
	UpdateNoResults(Count);
	if (Count != ResultCount)
	{
		ResultCount = Count;
		OnResultsChanged.Broadcast(Count);
	}
	OnSearchChanged.Broadcast(CurrentQuery, ActiveFilter);
}

bool USearchBar::IsQueryActive() const
{
	FString Compact = CurrentQuery;
	Compact.ReplaceInline(TEXT(" "), TEXT(""));
	return Compact.Len() > 0 && Compact.Len() >= MatchOptions.MinQueryLength;
}

bool USearchBar::IsSearching() const
{
	return IsQueryActive() || ActiveFilter != NAME_None;
}

bool USearchBar::PassesFilter(FName Category) const
{
	return ActiveFilter == NAME_None || Category == ActiveFilter;
}

// ---------------------------------------------------------------------------------------------
// Results
// ---------------------------------------------------------------------------------------------

UWidget* USearchBar::FindResultsWidget() const
{
	if (UWidget* OverrideWidget = ResultsOverride.Get())
	{
		return OverrideWidget;
	}
	return ResultsWidget.IsNone() ? nullptr : WidgetTreeLookup::FindWidget(this, ResultsWidget);
}

void USearchBar::GetItemTexts(UObject* Item, TArray<FString>& OutTexts, FName& OutCategory, bool bLabelOnly) const
{
	OutTexts.Reset();
	OutCategory = NAME_None;
	if (!Item)
	{
		return;
	}
	if (Item->Implements<USearchableItemInterface>())
	{
		OutTexts.Add(ISearchableItemInterface::Execute_GetSearchText(Item));
		OutCategory = ISearchableItemInterface::Execute_GetSearchFilterCategory(Item);
		if (!bLabelOnly && MatchOptions.bSearchKeywords)
		{
			OutTexts.Append(ISearchableItemInterface::Execute_GetSearchKeywords(Item));
		}
		return;
	}
	if (const UWidget* Widget = Cast<UWidget>(Item))
	{
		if (!bSearchInsideWidgets)
		{
			return;
		}
		TArray<FString> Found;
		SearchBarPrivate::GatherWidgetTexts(Widget, Found, 0);
		if (Found.Num() > 0)
		{
			// All of it as one text, so "red pot" finds a slot whose name and rarity are two text blocks. The first is its label.
			OutTexts.Add(bLabelOnly ? Found[0] : FString::Join(Found, TEXT(" ")));
		}
		return;
	}
	const FString Label = SearchBarPrivate::ReadLabelProperty(Item);
	if (!Label.IsEmpty())
	{
		OutTexts.Add(Label);
	}
}

int32 USearchBar::ApplyToResults()
{
	if (IsDesignTime())
	{
		return -1;
	}
	UWidget* Target = FindResultsWidget();
	if (UListView* List = Cast<UListView>(Target))
	{
		return FilterList(*List);
	}
	if (UPanelWidget* Panel = Cast<UPanelWidget>(Target))
	{
		return FilterPanel(*Panel);
	}
	RestoreResults();
	return -1;
}

int32 USearchBar::FilterList(UListView& List)
{
	if (FilteredList.Get() != &List || FilteredPanel.IsValid())
	{
		RestoreResults();
		FilteredList = &List;
	}

	if (!IsSearching())
	{
		// Everything back, as it was before the search began.
		if (bHasSourceItems)
		{
			TArray<UObject*> Everything;
			Everything.Reserve(SourceItems.Num());
			for (const TObjectPtr<UObject>& Item : SourceItems)
			{
				Everything.Add(Item.Get());
			}
			List.SetListItems(Everything);
			SourceItems.Reset();
			bHasSourceItems = false;
		}
		return List.GetNumItems();
	}

	if (!bHasSourceItems)
	{
		SourceItems.Reset();
		for (UObject* Item : List.GetListItems())
		{
			SourceItems.Add(Item);
		}
		bHasSourceItems = true;
	}

	const bool bQuery = IsQueryActive();
	TArray<TPair<UObject*, int32>> Scored;
	TArray<FString> Texts;
	for (const TObjectPtr<UObject>& Item : SourceItems)
	{
		UObject* Object = Item.Get();
		FName Category;
		GetItemTexts(Object, Texts, Category, /*bLabelOnly=*/false);
		if (!Object || !PassesFilter(Category))
		{
			continue;
		}
		int32 Score = 0;
		if (bQuery && !USearchLibrary::MatchAny(CurrentQuery, Texts, MatchOptions, Score))
		{
			continue;
		}
		Scored.Emplace(Object, Score);
	}
	if (bBestMatchFirst && bQuery)
	{
		Scored.StableSort([](const TPair<UObject*, int32>& A, const TPair<UObject*, int32>& B) { return A.Value > B.Value; });
	}
	TArray<UObject*> Shown;
	Shown.Reserve(Scored.Num());
	for (const TPair<UObject*, int32>& Entry : Scored)
	{
		Shown.Add(Entry.Key);
	}
	List.SetListItems(Shown);
	return Shown.Num();
}

int32 USearchBar::FilterPanel(UPanelWidget& Panel)
{
	if (FilteredPanel.Get() != &Panel || FilteredList.IsValid())
	{
		RestoreResults();
		FilteredPanel = &Panel;
	}

	const UWidget* NoResults = NoResultsWidget.IsNone() ? nullptr : WidgetTreeLookup::FindWidget(this, NoResultsWidget);
	const bool bSearching = IsSearching();
	const bool bQuery = IsQueryActive();
	int32 Count = 0;
	TArray<FString> Texts;
	for (int32 Index = 0; Index < Panel.GetChildrenCount(); ++Index)
	{
		UWidget* Child = Panel.GetChildAt(Index);
		if (!Child || Child == NoResults || Child == this)
		{
			continue;
		}
		bool bShow = true;
		if (bSearching)
		{
			FName Category;
			GetItemTexts(Child, Texts, Category, /*bLabelOnly=*/false);
			int32 Score = 0;
			bShow = PassesFilter(Category) && (!bQuery || USearchLibrary::MatchAny(CurrentQuery, Texts, MatchOptions, Score));
		}

		const TWeakObjectPtr<UWidget> Key(Child);
		if (bShow)
		{
			if (const ESlateVisibility* Was = HiddenChildren.Find(Key))
			{
				Child->SetVisibility(*Was);
				HiddenChildren.Remove(Key);
			}
			Count += Child->IsVisible() ? 1 : 0;
		}
		else if (!HiddenChildren.Contains(Key) && Child->IsVisible())
		{
			// Hidden by the search, its own visibility kept to give back. A child the game hid itself is left alone.
			HiddenChildren.Add(Key, Child->GetVisibility());
			Child->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
	return Count;
}

void USearchBar::RestoreResults()
{
	for (const TPair<TWeakObjectPtr<UWidget>, ESlateVisibility>& Hidden : HiddenChildren)
	{
		if (UWidget* Child = Hidden.Key.Get())
		{
			Child->SetVisibility(Hidden.Value);
		}
	}
	HiddenChildren.Reset();
	FilteredPanel.Reset();

	if (UListView* List = FilteredList.Get())
	{
		if (bHasSourceItems)
		{
			TArray<UObject*> Everything;
			for (const TObjectPtr<UObject>& Item : SourceItems)
			{
				Everything.Add(Item.Get());
			}
			List->SetListItems(Everything);
		}
	}
	FilteredList.Reset();
	SourceItems.Reset();
	bHasSourceItems = false;
}

void USearchBar::UpdateNoResults(int32 Count)
{
	if (IsDesignTime() || NoResultsWidget.IsNone())
	{
		return;
	}
	if (UWidget* NoResults = WidgetTreeLookup::FindWidget(this, NoResultsWidget))
	{
		NoResults->SetVisibility(Count == 0 && IsSearching() ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void USearchBar::SetResultsWidgetObject(UWidget* InWidget)
{
	RestoreResults();
	ResultsOverride = InWidget;
	SearchNow();
}

void USearchBar::SetSearchSourceItems(const TArray<UObject*>& InItems)
{
	SourceItems.Reset();
	for (UObject* Item : InItems)
	{
		SourceItems.Add(Item);
	}
	bHasSourceItems = true;
	if (UListView* List = Cast<UListView>(FindResultsWidget()))
	{
		FilteredList = List;
	}
	SearchNow();
}

void USearchBar::RefreshResults()
{
	SearchNow();
}

bool USearchBar::MatchesQuery(const FString& Candidate, int32& OutScore) const
{
	return USearchLibrary::MatchWithOptions(CurrentQuery, Candidate, MatchOptions, OutScore);
}

FString USearchBar::HighlightQuery(const FString& Candidate, const FString& StyleName) const
{
	return USearchLibrary::HighlightMatches(CurrentQuery, Candidate, MatchOptions, StyleName);
}

FText USearchBar::GetResultCountText() const
{
	return ResultCount >= 0 ? FText::Format(ResultCountFormat, FText::AsNumber(ResultCount)) : FText::GetEmpty();
}

// ---------------------------------------------------------------------------------------------
// Blueprint
// ---------------------------------------------------------------------------------------------

void USearchBar::SetText(const FText& InText, bool bSearch)
{
	Text = InText;
	CurrentQuery = InText.ToString();
	if (SlateText.IsValid() && !SlateText->GetText().EqualTo(InText))
	{
		TGuardValue<bool> Guard(bSettingText, true);
		SlateText->SetText(InText);
	}
	if (bSearch)
	{
		SearchNow();
	}
}

void USearchBar::ClearSearch()
{
	ActiveFilter = NAME_None;
	if (SlateFilter.IsValid() && FilterItems.Num() > 0)
	{
		SlateFilter->SetSelectedItem(FilterItems[0]);
	}
	SetText(FText::GetEmpty(), /*bSearch=*/false);
	OpenSuggestions(false);

	// At once, not debounced: clearing is one deliberate act, and the full list is wanted back now.
	SearchNow();
	OnSearchCleared.Broadcast();
}

void USearchBar::FocusSearch()
{
	if (SlateText.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetKeyboardFocus(SlateText, EFocusCause::SetDirectly);
	}
}

bool USearchBar::HasSearchFocus() const
{
	return bFocused;
}

void USearchBar::SetFilterOptions(const TArray<FSearchFilterOption>& InOptions)
{
	const FName Before = ActiveFilter;
	FilterOptions = InOptions;
	RebuildFilterItems();
	if (ActiveFilter != Before)
	{
		SearchNow();
	}
}

void USearchBar::SetActiveFilter(FName FilterKey)
{
	const bool bKnown = FilterOptions.ContainsByPredicate([FilterKey](const FSearchFilterOption& Option) { return Option.Key == FilterKey; });
	ActiveFilter = bKnown ? FilterKey : NAME_None;
	RebuildFilterItems();
	SearchNow();
}

void USearchBar::SetSuggestions(const TArray<FText>& InSuggestions)
{
	Suggestions = InSuggestions;
	if (bFocused)
	{
		UpdateSuggestions();
	}
}

void USearchBar::SetLook(ESearchBarLook InLook)
{
	Look = InLook;
	LookStyleMadeFor = ESearchBarLook::Custom;
	SynchronizeProperties();
}

void USearchBar::SetStyleAsset(USearchBarStyleAsset* InStyleAsset)
{
	StyleAsset = InStyleAsset;
	SynchronizeProperties();
}

TArray<FString> USearchBar::GetWidgetOptions() const
{
	TArray<FString> Names;
	WidgetTreeLookup::GetWidgetNames(this, Names, this);
	return Names;
}

// ---------------------------------------------------------------------------------------------
// Filter dropdown
// ---------------------------------------------------------------------------------------------

void USearchBar::RebuildFilterItems()
{
	FilterItems.Reset();
	if (FilterOptions.Num() > 0)
	{
		FSearchFilterOption All;
		All.Key = SearchBarPrivate::AllKey;
		All.DisplayText = AllFilterLabel;
		FilterItems.Add(MakeShared<FSearchFilterOption>(All));
		for (const FSearchFilterOption& Option : FilterOptions)
		{
			FilterItems.Add(MakeShared<FSearchFilterOption>(Option));
		}
	}

	// A filter that is no longer offered is let go.
	const TSharedPtr<FSearchFilterOption>* Picked = FilterItems.FindByPredicate([this](const TSharedPtr<FSearchFilterOption>& Item)
	{
		return Item->Key == ActiveFilter;
	});
	if (!Picked)
	{
		ActiveFilter = NAME_None;
	}

	if (SlateFilter.IsValid())
	{
		SlateFilter->RefreshOptions();
		if (FilterItems.Num() > 0)
		{
			SlateFilter->SetSelectedItem(Picked ? *Picked : FilterItems[0]);
		}
	}
}

TSharedRef<SWidget> USearchBar::MakeFilterRow(TSharedPtr<FSearchFilterOption> Option) const
{
	const FSearchBarStyle& Active = GetActiveStyle();
	return SNew(SBox)
		.Padding(FMargin(8.f, 4.f))
		[
			SNew(STextBlock)
			.Text(Option.IsValid() ? Option->DisplayText : FText::GetEmpty())
			.Font(Active.SecondaryFont)
		];
}

void USearchBar::HandleFilterPicked(TSharedPtr<FSearchFilterOption> Option, ESelectInfo::Type SelectInfo)
{
	// Direct: set from code (rebuilt, cleared) - not the player choosing.
	if (SelectInfo == ESelectInfo::Direct)
	{
		return;
	}
	ActiveFilter = (!Option.IsValid() || Option->Key == SearchBarPrivate::AllKey) ? NAME_None : Option->Key;
	SearchNow();
}

FText USearchBar::GetActiveFilterLabel() const
{
	for (const FSearchFilterOption& Option : FilterOptions)
	{
		if (Option.Key == ActiveFilter && ActiveFilter != NAME_None)
		{
			return Option.DisplayText;
		}
	}
	return AllFilterLabel;
}

// ---------------------------------------------------------------------------------------------
// Suggestions
// ---------------------------------------------------------------------------------------------

void USearchBar::UpdateSuggestions()
{
	SuggestionItems.Reset();
	HighlightedSuggestion = INDEX_NONE;
	if (!bShowSuggestions || !bFocused || IsDesignTime())
	{
		OpenSuggestions(false);
		return;
	}
	LoadRecentSearches();

	const FString Typed = CurrentQuery.TrimStartAndEnd();
	TSet<FString> Seen;
	auto Add = [this, &Seen, &Typed](const FString& Value, bool bRecent, int32 Score)
	{
		const FString Key = Value.ToLower();
		if (Value.IsEmpty() || Seen.Contains(Key) || Key == Typed.ToLower() || SuggestionItems.Num() >= MaxSuggestions)
		{
			return;
		}
		Seen.Add(Key);
		TSharedPtr<FSearchBarSuggestion> Made = MakeShared<FSearchBarSuggestion>();
		Made->Text = Value;
		Made->bRecent = bRecent;
		Made->Score = Score;
		SuggestionItems.Add(Made);
	};

	if (Typed.IsEmpty())
	{
		if (bSuggestRecentSearches)
		{
			for (const FString& Recent : RecentSearches)
			{
				Add(Recent, /*bRecent=*/true, 0);
			}
		}
	}
	else if (IsQueryActive())
	{
		struct FCandidate
		{
			FString Value;
			bool bRecent = false;
			int32 Score = 0;
		};
		TArray<FCandidate> Matched;
		auto Consider = [this, &Matched, &Typed](const FString& Value, bool bRecent)
		{
			int32 Score = 0;
			if (!Value.IsEmpty() && USearchLibrary::MatchWithOptions(Typed, Value, MatchOptions, Score))
			{
				// What was searched before is a little more likely to be wanted again.
				Matched.Add({ Value, bRecent, Score + (bRecent ? 3 : 0) });
			}
		};

		if (bSuggestRecentSearches)
		{
			for (const FString& Recent : RecentSearches)
			{
				Consider(Recent, true);
			}
		}
		for (const FText& Suggestion : Suggestions)
		{
			Consider(Suggestion.ToString(), false);
		}
		if (bSuggestResults)
		{
			TArray<FString> Texts;
			FName Category;
			UWidget* Target = FindResultsWidget();
			if (UListView* List = Cast<UListView>(Target))
			{
				TArray<UObject*> Items;
				if (bHasSourceItems)
				{
					for (const TObjectPtr<UObject>& Item : SourceItems)
					{
						Items.Add(Item.Get());
					}
				}
				else
				{
					for (UObject* Item : List->GetListItems())
					{
						Items.Add(Item);
					}
				}
				for (int32 Index = 0; Index < Items.Num() && Index < SearchBarPrivate::MaxSuggestionSources; ++Index)
				{
					GetItemTexts(Items[Index], Texts, Category, /*bLabelOnly=*/true);
					if (Texts.Num() > 0 && PassesFilter(Category))
					{
						Consider(Texts[0], false);
					}
				}
			}
			else if (UPanelWidget* Panel = Cast<UPanelWidget>(Target))
			{
				for (int32 Index = 0; Index < Panel->GetChildrenCount() && Index < SearchBarPrivate::MaxSuggestionSources; ++Index)
				{
					GetItemTexts(Panel->GetChildAt(Index), Texts, Category, /*bLabelOnly=*/true);
					if (Texts.Num() > 0 && PassesFilter(Category))
					{
						Consider(Texts[0], false);
					}
				}
			}
		}

		Matched.StableSort([](const FCandidate& A, const FCandidate& B) { return A.Score > B.Score; });
		for (const FCandidate& Candidate : Matched)
		{
			Add(Candidate.Value, Candidate.bRecent, Candidate.Score);
		}
	}

	if (SlateSuggestions.IsValid())
	{
		SlateSuggestions->RequestListRefresh();
	}
	OpenSuggestions(SuggestionItems.Num() > 0);
}

void USearchBar::OpenSuggestions(bool bOpen)
{
	if (!SlateAnchor.IsValid())
	{
		return;
	}
	if (SlateAnchor->IsOpen() != bOpen)
	{
		// Opened without taking the focus: typing carries on in the field.
		SlateAnchor->SetIsOpen(bOpen, /*bFocusMenu=*/false);
	}
	if (!bOpen)
	{
		HighlightedSuggestion = INDEX_NONE;
	}
}

void USearchBar::MoveSuggestionHighlight(int32 Direction)
{
	const int32 Count = SuggestionItems.Num();
	if (Count == 0)
	{
		return;
	}
	HighlightedSuggestion = HighlightedSuggestion == INDEX_NONE
		? (Direction > 0 ? 0 : Count - 1)
		: (HighlightedSuggestion + Direction + Count) % Count;
	if (SlateSuggestions.IsValid())
	{
		SlateSuggestions->RequestScrollIntoView(SuggestionItems[HighlightedSuggestion]);
	}
}

void USearchBar::PickSuggestion(const TSharedPtr<FSearchBarSuggestion>& Suggestion)
{
	if (!Suggestion.IsValid())
	{
		return;
	}
	const FString Picked = Suggestion->Text;
	SetText(FText::FromString(Picked), /*bSearch=*/false);
	if (SlateText.IsValid())
	{
		SlateText->GoTo(ETextLocation::EndOfDocument);
	}
	AddRecentSearch(Picked);
	OpenSuggestions(false);
	SearchNow();
	OnSuggestionPicked.Broadcast(Picked);
	OnSearchCommitted.Broadcast(Picked);
}

TSharedRef<ITableRow> USearchBar::MakeSuggestionRow(TSharedPtr<FSearchBarSuggestion> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	const TWeakObjectPtr<USearchBar> WeakBar(this);
	const FSearchBarStyle& Active = GetActiveStyle();
	TSharedRef<STableRow<TSharedPtr<FSearchBarSuggestion>>> TableRow = SNew(STableRow<TSharedPtr<FSearchBarSuggestion>>, OwnerTable)
		.Style(SearchBarPrivate::PlainRowStyle())
		.Padding(FMargin(0.f));

	const TWeakPtr<STableRow<TSharedPtr<FSearchBarSuggestion>>> WeakRow = TableRow;
	TableRow->SetContent(
		SNew(SBorder)
		.BorderImage_Lambda([WeakBar, WeakRow, Item]() -> const FSlateBrush*
		{
			const USearchBar* Bar = WeakBar.Get();
			const TSharedPtr<STableRow<TSharedPtr<FSearchBarSuggestion>>> PinnedRow = WeakRow.Pin();
			if (!Bar)
			{
				return SearchBarPrivate::NoBrush();
			}
			const bool bPicked = Bar->SuggestionItems.IsValidIndex(Bar->HighlightedSuggestion) && Bar->SuggestionItems[Bar->HighlightedSuggestion] == Item;
			const bool bHovered = PinnedRow.IsValid() && PinnedRow->IsHovered();
			return bPicked || bHovered ? &Bar->GetActiveStyle().SuggestionHighlight : SearchBarPrivate::NoBrush();
		})
		.Padding(Active.SuggestionPadding)
		[
			MakeHighlightedText(Item.IsValid() ? Item->Text : FString(), Item.IsValid() && Item->bRecent)
		]);
	return TableRow;
}

TSharedRef<SWidget> USearchBar::MakeHighlightedText(const FString& Value, bool bRecent) const
{
	using namespace SearchBarPrivate;

	const FSearchBarStyle& Active = GetActiveStyle();
	TSharedRef<SHorizontalBox> Runs = SNew(SHorizontalBox);
	if (bRecent)
	{
		Runs->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(0.f, 0.f, Active.Spacing, 0.f))
			[
				SNew(SSearchBarGlyph).Glyph(EGlyph::Recent).Size(Active.IconSize * 0.8f).Color(Active.IconColor)
			];
	}

	// The letters that match what is typed in Match Color, the rest in Suggestion Color.
	auto AddRun = [&Runs, &Active](const FString& Piece, bool bMatch)
	{
		if (Piece.IsEmpty())
		{
			return;
		}
		Runs->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Piece))
				.Font(Active.Font)
				.ColorAndOpacity(FSlateColor(bMatch ? Active.MatchColor : Active.SuggestionColor))
			];
	};
	const TArray<FIntPoint> Ranges = USearchLibrary::GetMatchRanges(CurrentQuery, Value, MatchOptions);
	int32 Next = 0;
	for (const FIntPoint& Range : Ranges)
	{
		const int32 Start = FMath::Clamp(Range.X, Next, Value.Len());
		const int32 End = FMath::Clamp(Range.X + Range.Y, Start, Value.Len());
		AddRun(Value.Mid(Next, Start - Next), false);
		AddRun(Value.Mid(Start, End - Start), true);
		Next = End;
	}
	AddRun(Value.Mid(Next), false);
	return Runs;
}

// ---------------------------------------------------------------------------------------------
// Recent searches
// ---------------------------------------------------------------------------------------------

void USearchBar::AddRecentSearch(const FString& InQuery)
{
	const FString Trimmed = InQuery.TrimStartAndEnd();
	if (Trimmed.IsEmpty() || !bRememberRecentSearches)
	{
		return;
	}
	LoadRecentSearches();
	RecentSearches.RemoveAll([&Trimmed](const FString& Recent) { return Recent.Equals(Trimmed, ESearchCase::IgnoreCase); });
	RecentSearches.Insert(Trimmed, 0);
	if (RecentSearches.Num() > MaxRecentSearches)
	{
		RecentSearches.SetNum(FMath::Max(MaxRecentSearches, 0));
	}
	SaveRecentSearches();
}

void USearchBar::ClearRecentSearches()
{
	RecentSearches.Reset();
	SaveRecentSearches();
	if (bFocused)
	{
		UpdateSuggestions();
	}
}

void USearchBar::LoadRecentSearches()
{
	if (bRecentLoaded || IsDesignTime())
	{
		return;
	}
	bRecentLoaded = true;
	if (RecentSearchesSaveSlot.IsEmpty() || !UGameplayStatics::DoesSaveGameExist(RecentSearchesSaveSlot, 0))
	{
		return;
	}
	if (const USearchBarHistorySave* Saved = Cast<USearchBarHistorySave>(UGameplayStatics::LoadGameFromSlot(RecentSearchesSaveSlot, 0)))
	{
		RecentSearches = Saved->RecentSearches;
	}
}

void USearchBar::SaveRecentSearches() const
{
	if (RecentSearchesSaveSlot.IsEmpty() || IsDesignTime())
	{
		return;
	}
	if (USearchBarHistorySave* Save = Cast<USearchBarHistorySave>(UGameplayStatics::CreateSaveGameObject(USearchBarHistorySave::StaticClass())))
	{
		Save->RecentSearches = RecentSearches;

		// Written on a worker: a search committed mid-menu never waits on the disk.
		UGameplayStatics::AsyncSaveGameToSlot(Save, RecentSearchesSaveSlot, 0);
	}
}

#undef LOCTEXT_NAMESPACE
