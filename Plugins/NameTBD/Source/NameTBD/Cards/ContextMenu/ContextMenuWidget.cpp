// Copyright AR GameSystems 2026. All rights reserved.

#include "ContextMenuWidget.h"

#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/Events.h"

#include "NameTBD/Buttons/ContextMenuButton.h"
#include "NameTBD/Buttons/Actions/ContextMenuInterface.h"
#include "NameTBD/Buttons/Actions/ContextMenuPayload.h"
#include "NameTBD/Cards/InfoCardAnchor.h"
#include "NameTBD/DeveloperSettings/InfoCardDeveloperSettings.h"
#include "NameTBD/Widgets/WidgetSVGImage.h"
#include "ContextMenuAnchor.h"

DEFINE_LOG_CATEGORY_STATIC(LogContextMenu, Log, All);

const FName UContextMenuEntryWidget::SubmenuCardId(TEXT("Submenu"));

namespace ContextMenuWidgetPrivate
{
	/** The native widgets are only built for the C++ classes; a Widget Blueprint made from one owns its tree. */
	bool IsNativeClass(const UObject* Object)
	{
		return Object && !Cast<UWidgetBlueprintGeneratedClass>(Object->GetClass());
	}

	FSlateBrush NoDrawBrush()
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::NoDrawType;
		return Brush;
	}

	/** The first usable texture of a Button Actions entry's icon list, for an entry that has none of its own. */
	FContextMenuIcon IconFromTagChoices(const TArray<FContextButtonImageChoice>& Choices)
	{
		FContextMenuIcon Icon;
		for (const FContextButtonImageChoice& Choice : Choices)
		{
			if (Choice.IconChoice == EImageChoice::Icon && !Choice.Icon.IsNull())
			{
				Icon.ImageSource = EWidgetImageSource::Texture;
				Icon.Texture = Choice.Icon.LoadSynchronous();
				break;
			}
		}
		return Icon;
	}
}

// =================================================================================================
// UContextMenuEntryWidget
// =================================================================================================

void UContextMenuEntryWidget::SetupEntry(const FContextMenuEntry& InEntry, const FContextMenuStyle& InStyle, UContextMenuWidget* InMenu, int32 InIndex,
	bool bReserveIconColumn, const TArray<UButtonAction*>& InActions, const TArray<UButtonAction*>& InTagActions)
{
	Entry = InEntry;
	EntryStyle = InStyle;
	OwningMenu = InMenu;
	EntryIndex = InIndex;
	bEntryChecked = Entry.CheckType != EContextMenuCheckType::None && Entry.bChecked;

	RowActions.Reset();
	RowActions.Append(InActions);
	RowTagActions.Reset();
	RowTagActions.Append(InTagActions);

	if (Entry.EntryType == EContextMenuEntryType::Header && EntryStyle.bUppercaseHeaders)
	{
		Entry.HeaderText = Entry.HeaderText.ToUpper();
	}

	if (WidgetTree && !WidgetTree->RootWidget && ContextMenuWidgetPrivate::IsNativeClass(this))
	{
		BuildNativeTree(bReserveIconColumn);
	}

	FillParts();
	OnMenuEntrySet(Entry, bEntryChecked);
}

void UContextMenuEntryWidget::BuildNativeTree(bool bReserveIconColumn)
{
	switch (Entry.EntryType)
	{
	case EContextMenuEntryType::Button:
	case EContextMenuEntryType::Submenu:
		WidgetTree->RootWidget = BuildButtonRow(bReserveIconColumn);
		break;

	case EContextMenuEntryType::Spacer:
	{
		USpacer* Gap = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass(), TEXT("EntrySpacer"));
		Gap->SetSize(FVector2D(1.f, Entry.SpacerHeight));
		WidgetTree->RootWidget = Gap;
		break;
	}

	case EContextMenuEntryType::Divider:
	case EContextMenuEntryType::Header:
	default:
		WidgetTree->RootWidget = BuildDividerRow();
		break;
	}
}

UWidget* UContextMenuEntryWidget::MakeIconPart(const FContextMenuIcon& Icon, FName PartName, const TCHAR* Glyph, TObjectPtr<UWidget>& OutPart)
{
	USizeBox* IconBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), FName(*(PartName.ToString() + TEXT("Box"))));
	IconBox->SetWidthOverride(EntryStyle.IconSize);
	IconBox->SetHeightOverride(EntryStyle.IconSize);

	UWidget* Part = nullptr;
	if (Icon.IsSet() && Icon.ImageSource == EWidgetImageSource::SVG)
	{
		Part = WidgetTree->ConstructWidget<UWidgetSVGImage>(UWidgetSVGImage::StaticClass(), PartName);
	}
	else if (Icon.IsSet() || !Glyph || !*Glyph)
	{
		Part = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), PartName);
	}
	else
	{
		UTextBlock* GlyphText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), PartName);
		GlyphText->SetText(FText::FromString(Glyph));
		GlyphText->SetFont(EntryStyle.LabelFont);
		GlyphText->SetJustification(ETextJustify::Center);
		Part = GlyphText;
	}

	IconBox->SetContent(Part);
	OutPart = Part;
	return IconBox;
}

UWidget* UContextMenuEntryWidget::MakeLine(FName PartName)
{
	USizeBox* LineBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), FName(*(PartName.ToString() + TEXT("Box"))));
	LineBox->SetHeightOverride(EntryStyle.DividerThickness);

	UImage* Line = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), PartName);
	Line->SetColorAndOpacity(EntryStyle.DividerColor);
	LineBox->SetContent(Line);
	return LineBox;
}

UWidget* UContextMenuEntryWidget::BuildButtonRow(bool bReserveIconColumn)
{
	using namespace ContextMenuWidgetPrivate;

	RowButton = WidgetTree->ConstructWidget<UContextMenuButton>(UContextMenuButton::StaticClass(), TEXT("RowButton"));
	RowHighlight = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("RowHighlight"));
	USizeBox* RowSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("RowSize"));
	UHorizontalBox* Parts = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("RowParts"));

	RowButton->SetContent(RowHighlight);
	RowHighlight->SetContent(RowSize);
	RowSize->SetContent(Parts);

	bool bFirstPart = true;
	auto AddPart = [&Parts, &bFirstPart, this](UWidget* Part, bool bFill)
	{
		UHorizontalBoxSlot* PartSlot = Parts->AddChildToHorizontalBox(Part);
		PartSlot->SetVerticalAlignment(VAlign_Center);
		PartSlot->SetPadding(FMargin(bFirstPart ? 0.f : EntryStyle.PartGap, 0.f, 0.f, 0.f));
		if (bFill)
		{
			PartSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}
		bFirstPart = false;
	};

	// The icon column: the left icon, or the check / radio mark of a checkable row. Kept (empty) when the menu reserves it.
	const bool bHasCheck = Entry.CheckType != EContextMenuCheckType::None;
	if (Entry.LeftIcon.IsSet() || bHasCheck || bReserveIconColumn)
	{
		UWidget* Column = nullptr;
		if (bHasCheck)
		{
			const FContextMenuIcon& Mark = Entry.CheckType == EContextMenuCheckType::Radio ? EntryStyle.RadioMark : EntryStyle.CheckMark;
			Column = MakeIconPart(Mark, TEXT("CheckMark"), Entry.CheckType == EContextMenuCheckType::Radio ? TEXT("●") : TEXT("✓"), CheckMark);
		}
		else
		{
			Column = MakeIconPart(Entry.LeftIcon, TEXT("LeftIcon"), nullptr, LeftIcon);
		}
		AddPart(Column, false);
	}

	if (Entry.CenterIcon.IsSet())
	{
		AddPart(MakeIconPart(Entry.CenterIcon, TEXT("CenterIcon"), nullptr, CenterIcon), false);
	}

	LabelText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("LabelText"));
	LabelText->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
	AddPart(LabelText, true);

	RightText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("RightText"));
	AddPart(RightText, false);

	ShortcutText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ShortcutText"));
	AddPart(ShortcutText, false);

	if (Entry.RightIcon.IsSet())
	{
		AddPart(MakeIconPart(Entry.RightIcon, TEXT("RightIcon"), nullptr, RightIcon), false);
	}

	if (Entry.EntryType == EContextMenuEntryType::Submenu)
	{
		AddPart(MakeIconPart(EntryStyle.SubmenuArrow, TEXT("SubmenuArrow"), TEXT("›"), SubmenuArrow), false);

		// The submenu opens from an anchor around the button: hover (after the style's delay), click and Right all go
		// through it, and it brings placement beside the row, flipping at the screen edge and keep-open while the cursor
		// travels to the submenu.
		SubmenuAnchor = WidgetTree->ConstructWidget<UInfoCardAnchor>(UInfoCardAnchor::StaticClass(), TEXT("SubmenuAnchor"));
		SubmenuAnchor->SetContent(RowButton);
		return SubmenuAnchor;
	}

	return RowButton;
}

UWidget* UContextMenuEntryWidget::BuildDividerRow()
{
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("DividerRow"));
	const bool bText = Entry.EntryType == EContextMenuEntryType::Header && !Entry.HeaderText.IsEmpty();
	const bool bLine = Entry.EntryType == EContextMenuEntryType::Divider || Entry.bShowDividerLine || !bText;
	const FMargin RowInset = bText ? EntryStyle.HeaderPadding : EntryStyle.DividerPadding;

	auto AddLine = [this, &Row](FName PartName, const FMargin& LinePadding)
	{
		UHorizontalBoxSlot* LineSlot = Row->AddChildToHorizontalBox(MakeLine(PartName));
		LineSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		LineSlot->SetVerticalAlignment(VAlign_Center);
		LineSlot->SetPadding(LinePadding);
	};

	if (!bText)
	{
		AddLine(TEXT("DividerLine"), FMargin(0.f));
		DividerLine = Row->GetChildAt(0);
	}
	else
	{
		HeaderText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("HeaderText"));
		const EHorizontalAlignment Alignment = Entry.HeaderAlignment.GetValue();
		const float Gap = EntryStyle.PartGap;

		// Category ------   ---- Category ----   ------ Category
		if (bLine && (Alignment == HAlign_Center || Alignment == HAlign_Right))
		{
			AddLine(TEXT("DividerLineBefore"), FMargin(0.f, 0.f, Gap, 0.f));
		}

		UHorizontalBoxSlot* TextSlot = Row->AddChildToHorizontalBox(HeaderText);
		TextSlot->SetVerticalAlignment(VAlign_Center);
		if (!bLine)
		{
			// Text alone: the row is the text, aligned within it.
			TextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			HeaderText->SetJustification(Alignment == HAlign_Center ? ETextJustify::Center : (Alignment == HAlign_Right ? ETextJustify::Right : ETextJustify::Left));
		}

		if (bLine && (Alignment == HAlign_Left || Alignment == HAlign_Fill || Alignment == HAlign_Center))
		{
			AddLine(TEXT("DividerLine"), FMargin(Gap, 0.f, 0.f, 0.f));
		}
		DividerLine = bLine ? Row->GetChildAt(Row->GetChildrenCount() - 1) : nullptr;
	}

	// The padding goes on a border around the row, so the lines stop short of the panel's edges by the same amount.
	UBorder* Inset = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("DividerInset"));
	Inset->SetBrush(ContextMenuWidgetPrivate::NoDrawBrush());
	Inset->SetPadding(RowInset);
	Inset->SetContent(Row);
	return Inset;
}

void UContextMenuEntryWidget::FillParts()
{
	if (HeaderText)
	{
		HeaderText->SetText(Entry.HeaderText);
		HeaderText->SetFont(EntryStyle.HeaderFont);
		HeaderText->SetColorAndOpacity(EntryStyle.HeaderColor);
	}

	if (DividerLine)
	{
		if (UImage* LineImage = Cast<UImage>(DividerLine))
		{
			LineImage->SetColorAndOpacity(EntryStyle.DividerColor);
		}
	}

	if (!RowButton)
	{
		return;
	}

	// The row's button: its clicks are the menu's, its focus is the keyboard's. Never closes a card itself (the menu decides
	// that, for the whole chain) and never toggles an enabled state of its own (the check mark shows a row's state).
	const UContextMenuButtonPresetAsset* Preset = Entry.RowButtonPreset ? Entry.RowButtonPreset.Get() : EntryStyle.RowButtonPreset.Get();
	bUsesRowPreset = Preset != nullptr;
	if (Preset)
	{
		RowButton->ApplyButtonPreset(const_cast<UContextMenuButtonPresetAsset*>(Preset));
	}
	else
	{
		FButtonStyle RowLook = RowButton->GetStyle();
		RowLook.Normal = ContextMenuWidgetPrivate::NoDrawBrush();
		RowLook.Hovered = ContextMenuWidgetPrivate::NoDrawBrush();
		RowLook.Pressed = ContextMenuWidgetPrivate::NoDrawBrush();
		RowLook.Disabled = ContextMenuWidgetPrivate::NoDrawBrush();
		RowLook.NormalPadding = FMargin(0.f);
		RowLook.PressedPadding = FMargin(0.f);
		RowButton->SetStyle(RowLook);
	}
	RowButton->bShouldDestroyCard = false;
	RowButton->bButtonCanBeEnabled = false;
	RowButton->SetIsEnabled(Entry.bEnabled);
	RowButton->SetToolTipText(Entry.Description);
	RowButton->OnContextClicked.AddUniqueDynamic(this, &UContextMenuEntryWidget::HandleRowClicked);

	if (RowHighlight)
	{
		RowHighlight->SetPadding(EntryStyle.RowPadding);
		RowHighlight->SetBrush(ContextMenuWidgetPrivate::NoDrawBrush());
	}

	if (USizeBox* RowSize = Cast<USizeBox>(RowHighlight ? RowHighlight->GetContent() : nullptr))
	{
		RowSize->SetMinDesiredHeight(EntryStyle.RowHeight);
	}

	if (LabelText)
	{
		LabelText->SetText(Entry.Label);
		LabelText->SetFont(EntryStyle.LabelFont);
		LabelText->SetVisibility(Entry.Label.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}

	if (RightText)
	{
		RightText->SetText(Entry.RightText);
		RightText->SetFont(EntryStyle.ShortcutFont);
		RightText->SetVisibility(Entry.RightText.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}

	if (ShortcutText)
	{
		const bool bHasShortcut = Entry.Shortcut.IsValidChord();
		ShortcutText->SetText(bHasShortcut ? Entry.Shortcut.GetInputText(/*bLongDisplayName=*/false) : FText::GetEmpty());
		ShortcutText->SetFont(EntryStyle.ShortcutFont);
		ShortcutText->SetVisibility(bHasShortcut ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	if (SubmenuArrow)
	{
		SubmenuArrow->SetVisibility(Entry.EntryType == EContextMenuEntryType::Submenu ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	if (Entry.EntryType == EContextMenuEntryType::Submenu)
	{
		SetupSubmenuAnchor();
	}

	ApplyStateLook();
}

void UContextMenuEntryWidget::SetupSubmenuAnchor()
{
	UContextMenuWidget* Menu = OwningMenu.Get();
	if (!SubmenuAnchor || !Menu || !Entry.Submenu)
	{
		return;
	}

	// The submenu looks like the menu it came from, whatever its own asset says, and reports to the same anchor.
	UInfoCardPayload_ContextMenu* SubPayload = NewObject<UInfoCardPayload_ContextMenu>(SubmenuAnchor);
	SubPayload->Menu = Entry.Submenu;
	SubPayload->StyleOverride = Menu->GetMenuStyleAsset();
	SubPayload->MenuAnchor = Menu->GetMenuAnchor();

	FInfoCardSpec Spec;
	Spec.CardId = SubmenuCardId;
	Spec.CardClass = EntryStyle.MenuWidgetClass ? TSubclassOf<UInfoCardWidget>(EntryStyle.MenuWidgetClass) : TSubclassOf<UInfoCardWidget>(Menu->GetClass());
	Spec.CardType = ECardType::Hover;
	Spec.HoverDelay.Source = EInfoCardValueSource::Custom;
	Spec.HoverDelay.CustomValue = EntryStyle.SubmenuDelay;
	Spec.bTriggerOnMouseHover = true;
	Spec.bTriggerOnFocus = false;
	Spec.bKeepOpenUntilCursorLeaves = true;
	Spec.PositionMode = EInfoCardPosition::ToIcon;
	Spec.IconPosition = EDirection2D::Right;
	Spec.IconAnchorPadding = EntryStyle.PanelPadding.Right;
	Spec.FlipToAvoidClipping.Source = EInfoCardValueSource::Custom;
	Spec.FlipToAvoidClipping.CustomValue = true;
	Spec.CloseOtherCardsScope = ECardCloseOthersScope::SameSourceWidgetClass;
	Spec.RemoveCardOnPressing = ECardRemoveOnPressZone::OutsideAnchorAndCard;
	Spec.ZOrder = 2000;
	Spec.Payloads.Add(SubPayload);
	for (UContextButtonPayload* ActionPayload : Menu->GetActionPayloads())
	{
		Spec.Payloads.Add(ActionPayload);
	}

	SubmenuAnchor->SourceMode = EInfoCardSourceMode::Custom;
	SubmenuAnchor->FocusTriggerMode = EInfoCardFocusTriggerMode::Disabled;
	SubmenuAnchor->CustomInfoCards = { Spec };
}

bool UContextMenuEntryWidget::OpenSubmenu(bool bFocusFirstEntry)
{
	if (!SubmenuAnchor || !IsSelectable())
	{
		return false;
	}

	SubmenuAnchor->ShowCardById(SubmenuCardId);
	UContextMenuWidget* Submenu = GetOpenSubmenu();
	if (Submenu && bFocusFirstEntry)
	{
		Submenu->FocusFirstEntry();
	}
	return Submenu != nullptr;
}

UContextMenuWidget* UContextMenuEntryWidget::GetOpenSubmenu() const
{
	return SubmenuAnchor ? Cast<UContextMenuWidget>(SubmenuAnchor->GetActiveCardById(SubmenuCardId)) : nullptr;
}

void UContextMenuEntryWidget::ApplyIcon(UWidget* Part, const FContextMenuIcon& Icon, const FLinearColor& StateTint) const
{
	if (!Part)
	{
		return;
	}

	if (UImage* AsImage = Cast<UImage>(Part))
	{
		if (Icon.ImageSource == EWidgetImageSource::Texture && Icon.Texture)
		{
			AsImage->SetBrushFromTexture(Icon.Texture, /*bMatchSize=*/false);
			AsImage->SetDesiredSizeOverride(FVector2D(EntryStyle.IconSize));
		}
	}
	else if (UWidgetSVGImage* AsSVG = Cast<UWidgetSVGImage>(Part))
	{
		if (Icon.ImageSource == EWidgetImageSource::SVG && Icon.SVG)
		{
			AsSVG->SetSVG(Icon.SVG);
			AsSVG->SetImageSize(FVector2D(EntryStyle.IconSize));
		}
	}

	TintIcon(Part, Icon, StateTint);
}

void UContextMenuEntryWidget::TintIcon(UWidget* Part, const FContextMenuIcon& Icon, const FLinearColor& StateTint) const
{
	const FLinearColor Tint = Icon.bOverrideTint ? Icon.Tint * FLinearColor(1.f, 1.f, 1.f, StateTint.A) : StateTint;
	if (UImage* AsImage = Cast<UImage>(Part))
	{
		AsImage->SetColorAndOpacity(Tint);
	}
	else if (UWidgetSVGImage* AsSVG = Cast<UWidgetSVGImage>(Part))
	{
		AsSVG->SetColorAndOpacity(Tint);
	}
	else if (UTextBlock* AsText = Cast<UTextBlock>(Part))
	{
		AsText->SetColorAndOpacity(FSlateColor(Tint));
	}
}

void UContextMenuEntryWidget::ApplyStateLook()
{
	if (!RowButton)
	{
		return;
	}

	const bool bEnabled = Entry.bEnabled;

	if (RowHighlight)
	{
		// A Row Button Preset draws the states itself; the native highlight would paint over it.
		if (bUsesRowPreset || !bEnabled)
		{
			RowHighlight->SetBrush(ContextMenuWidgetPrivate::NoDrawBrush());
		}
		else
		{
			RowHighlight->SetBrush(bPressedLook ? EntryStyle.RowPressed : (bHighlighted ? EntryStyle.RowHighlight : ContextMenuWidgetPrivate::NoDrawBrush()));
		}
	}

	const bool bShowHighlightColors = bHighlighted && bEnabled && !bUsesRowPreset;
	FSlateColor LabelColor = EntryStyle.LabelColor;
	if (!bEnabled)
	{
		LabelColor = EntryStyle.DisabledLabelColor;
	}
	else if (bShowHighlightColors)
	{
		LabelColor = EntryStyle.HighlightedLabelColor;
	}
	else if (Entry.Emphasis == EContextMenuEntryEmphasis::Destructive)
	{
		LabelColor = EntryStyle.DestructiveLabelColor;
	}

	if (LabelText)
	{
		LabelText->SetColorAndOpacity(LabelColor);
	}

	const FSlateColor ShortcutColor = !bEnabled ? EntryStyle.DisabledLabelColor : (bShowHighlightColors ? EntryStyle.HighlightedLabelColor : EntryStyle.ShortcutColor);
	if (RightText)
	{
		RightText->SetColorAndOpacity(ShortcutColor);
	}
	if (ShortcutText)
	{
		ShortcutText->SetColorAndOpacity(ShortcutColor);
	}

	const FLinearColor IconTint = !bEnabled ? EntryStyle.DisabledIconTint : (bShowHighlightColors ? EntryStyle.HighlightedIconTint : EntryStyle.IconTint);
	ApplyIcon(LeftIcon, Entry.LeftIcon, IconTint);
	ApplyIcon(CenterIcon, Entry.CenterIcon, IconTint);
	ApplyIcon(RightIcon, Entry.RightIcon, IconTint);
	ApplyIcon(SubmenuArrow, EntryStyle.SubmenuArrow, IconTint);

	if (CheckMark)
	{
		const FContextMenuIcon& Mark = Entry.CheckType == EContextMenuCheckType::Radio ? EntryStyle.RadioMark : EntryStyle.CheckMark;
		ApplyIcon(CheckMark, Mark, IconTint);
		CheckMark->SetVisibility(bEntryChecked ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}
}

void UContextMenuEntryWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!RowButton)
	{
		return;
	}

	// The cursor resting on a row moves keyboard focus to it, so the keyboard carries on from wherever the mouse was and
	// only one row is ever highlighted.
	if (RowButton->IsHovered() && !RowButton->HasKeyboardFocus() && IsSelectable())
	{
		const FVector2D CursorDelta = FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetCursorPos() - FSlateApplication::Get().GetLastCursorPos() : FVector2D::ZeroVector;
		if (!CursorDelta.IsNearlyZero())
		{
			RowButton->SetKeyboardFocus();
		}
	}

	const bool bNowHighlighted = IsSelectable() && (RowButton->IsHovered() || RowButton->HasKeyboardFocus() || GetOpenSubmenu() != nullptr);
	const bool bNowPressed = IsSelectable() && RowButton->IsPressed();
	if (bNowHighlighted == bHighlighted && bNowPressed == bPressedLook)
	{
		return;
	}

	const bool bHighlightChanged = bNowHighlighted != bHighlighted;
	bHighlighted = bNowHighlighted;
	bPressedLook = bNowPressed;
	ApplyStateLook();

	if (bHighlightChanged)
	{
		OnHighlightChanged(bHighlighted);
	}
}

void UContextMenuEntryWidget::HandleRowClicked(bool bShouldCloseCard, bool bEnabling)
{
	UContextMenuWidget* Menu = OwningMenu.Get();
	if (!Menu)
	{
		return;
	}

	// A click from the keyboard (Enter, a gamepad's confirm) is what focus means here: the button had it.
	Menu->HandleEntryChosen(this, RowButton && RowButton->HasKeyboardFocus() && !RowButton->IsHovered());
}

void UContextMenuEntryWidget::ChooseEntry()
{
	if (UContextMenuWidget* Menu = OwningMenu.Get())
	{
		Menu->HandleEntryChosen(this, /*bFromKeyboard=*/IsEntryFocused());
	}
}

void UContextMenuEntryWidget::SetEntryChecked(bool bNewChecked)
{
	bEntryChecked = Entry.CheckType != EContextMenuCheckType::None && bNewChecked;
	ApplyStateLook();
}

void UContextMenuEntryWidget::FocusEntry()
{
	if (RowButton && IsSelectable())
	{
		RowButton->SetKeyboardFocus();
	}
}

bool UContextMenuEntryWidget::IsEntryFocused() const
{
	return RowButton && RowButton->HasKeyboardFocus();
}

// =================================================================================================
// UContextMenuWidget
// =================================================================================================

UContextMenuWidget::UContextMenuWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Escape, the arrows and type-to-select reach the menu through its own focus.
	SetIsFocusable(true);
}

bool UContextMenuWidget::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}

	if (WidgetTree && !WidgetTree->RootWidget && ContextMenuWidgetPrivate::IsNativeClass(this))
	{
		BuildNativeFrame();
	}
	return true;
}

void UContextMenuWidget::BuildNativeFrame()
{
	MenuPanel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("MenuPanel"));
	MenuSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("MenuSizeBox"));
	MenuScroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("MenuScroll"));
	UVerticalBox* Rows = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("EntriesBox"));
	EntriesBox = Rows;

	WidgetTree->RootWidget = MenuPanel;
	MenuPanel->SetContent(MenuSizeBox);
	MenuSizeBox->SetContent(MenuScroll);
	MenuScroll->AddChild(Rows);
	MenuScroll->SetAlwaysShowScrollbar(false);
}

void UContextMenuWidget::ApplyFrameStyle()
{
	if (MenuPanel)
	{
		MenuPanel->SetBrush(MenuStyle.PanelBackground);
		MenuPanel->SetPadding(MenuStyle.PanelPadding);
	}

	if (MenuSizeBox)
	{
		MenuSizeBox->SetMinDesiredWidth(MenuStyle.MinWidth);
		if (MenuStyle.MaxWidth > 0.f)
		{
			MenuSizeBox->SetMaxDesiredWidth(MenuStyle.MaxWidth);
		}
		else
		{
			MenuSizeBox->ClearMaxDesiredWidth();
		}

		if (MenuStyle.MaxHeight > 0.f)
		{
			MenuSizeBox->SetMaxDesiredHeight(MenuStyle.MaxHeight);
		}
		else
		{
			MenuSizeBox->ClearMaxDesiredHeight();
		}
	}
}

void UContextMenuWidget::ApplyPayloads_Implementation(const TArray<UNameTBD_Payload*>& InPayloads)
{
	Super::ApplyPayloads_Implementation(InPayloads);

	MenuPayload = nullptr;
	for (UNameTBD_Payload* Payload : InPayloads)
	{
		if (UInfoCardPayload_ContextMenu* AsMenu = Cast<UInfoCardPayload_ContextMenu>(Payload))
		{
			MenuPayload = AsMenu;
			break;
		}
	}

	// The older tag-list payload (Tag Payload: Button Tags, or a Context Menu Preset of them) builds the same menu: a row per
	// tag, labelled, iconned and run from Project Settings > Info Card > Button Actions.
	if (!MenuPayload)
	{
		for (UNameTBD_Payload* Payload : InPayloads)
		{
			const UContextMenuPayload* TagList = Cast<UContextMenuPayload>(Payload);
			if (!TagList)
			{
				continue;
			}

			MenuPayload = NewObject<UInfoCardPayload_ContextMenu>(this);
			const TArray<FGameplayTag>& Tags = TagList->ContextMenuPreset ? TagList->ContextMenuPreset->Buttons : TagList->ContextButtons;
			for (const FGameplayTag& Tag : Tags)
			{
				if (!UContextMenuButton::FindButtonData(Tag))
				{
					continue; // "a tag not implemented in the developer settings is skipped", as the payload says
				}
				FContextMenuExtraEntry& Row = MenuPayload->ExtraEntries.AddDefaulted_GetRef();
				Row.Entry.EntryId = Tag.GetTagName();
				Row.Entry.ButtonTag = Tag;
			}
			break;
		}
	}

	const UContextMenuAsset* MenuAsset = MenuPayload ? MenuPayload->Menu.Get() : nullptr;
	MenuStyleAsset = const_cast<UContextMenuStyleAsset*>(UContextMenuStyleAsset::ResolveAsset(MenuPayload ? MenuPayload->StyleOverride.Get() : nullptr, MenuAsset));
	MenuStyle = UContextMenuStyleAsset::Resolve(MenuPayload ? MenuPayload->StyleOverride.Get() : nullptr, MenuAsset);

	// The style's entrance and sounds, unless the card's entry brought its own. Once: a payload re-delivered while the menu
	// is open (Refresh Card) must not restart them.
	if (!bAppliedOpenLook)
	{
		bAppliedOpenLook = true;
		if (AdditiveAnimations.IsEmpty())
		{
			AdditiveAnimations = MenuStyle.OpenAnimations;
			ApplyAdditiveAnimations();
		}
		if (Sounds.IsEmpty())
		{
			Sounds = MenuStyle.MenuSounds;
		}
	}

	ApplyFrameStyle();
	BuildRows();
}

void UContextMenuWidget::RebuildMenu()
{
	ApplyFrameStyle();
	BuildRows();
}

void UContextMenuWidget::BuildRows()
{
	if (!EntriesBox)
	{
		if (!IsDesignTime())
		{
			UE_LOG(LogContextMenu, Warning, TEXT("Context Menu Widget '%s' has no panel named EntriesBox to put its rows in."), *GetClass()->GetName());
		}
		return;
	}

	EntriesBox->ClearChildren();
	EntryWidgets.Reset();

	TArray<FContextMenuEntry> Entries;
	if (MenuPayload)
	{
		// Set Entry Enabled / Hidden / Label / Checked on the anchor, read live: they apply in submenus too, whose payloads
		// are copies made when the row was built.
		if (const UContextMenuAnchor* RootAnchor = Cast<UContextMenuAnchor>(GetMenuAnchor()))
		{
			MenuPayload->RuntimeStates = RootAnchor->GetEntryRuntimeStates();
		}
		MenuPayload->BuildEntries(Entries);
	}

	// Each open menu gets its own copies of the entries' actions, outered here so they have a world, and so Is Action
	// Visible / Enabled can run before the rows exist.
	struct FPreparedRow
	{
		FContextMenuEntry Entry;
		TArray<UButtonAction*> Actions;
		TArray<UButtonAction*> TagActions;
	};
	TArray<FPreparedRow> Prepared;
	Prepared.Reserve(Entries.Num());

	for (FContextMenuEntry& Source : Entries)
	{
		FPreparedRow& Row = Prepared.AddDefaulted_GetRef();
		Row.Entry = MoveTemp(Source);

		for (const TObjectPtr<UButtonAction>& Authored : Row.Entry.Actions)
		{
			if (Authored)
			{
				Row.Actions.Add(DuplicateObject<UButtonAction>(Authored, this));
			}
		}

		// A Button Tag: its actions, and its text and icons where the entry left those empty.
		if (const FContextMenuButtonData* TagData = UContextMenuButton::FindButtonData(Row.Entry.ButtonTag))
		{
			for (const TSubclassOf<UButtonAction>& ActionClass : TagData->Actions)
			{
				if (ActionClass)
				{
					Row.TagActions.Add(NewObject<UButtonAction>(this, ActionClass));
				}
			}

			if (Row.Entry.Label.IsEmpty())
			{
				Row.Entry.Label = !TagData->ButtonText_Center.IsEmpty() ? TagData->ButtonText_Center
					: (!TagData->ButtonText_Left.IsEmpty() ? TagData->ButtonText_Left : TagData->ButtonText_Right);
			}
			if (Row.Entry.RightText.IsEmpty() && !TagData->ButtonText_Right.IsEmpty() && !TagData->ButtonText_Center.IsEmpty())
			{
				Row.Entry.RightText = TagData->ButtonText_Right;
			}
			if (!Row.Entry.LeftIcon.IsSet())
			{
				Row.Entry.LeftIcon = ContextMenuWidgetPrivate::IconFromTagChoices(TagData->LeftIcons);
			}
			if (!Row.Entry.CenterIcon.IsSet())
			{
				Row.Entry.CenterIcon = ContextMenuWidgetPrivate::IconFromTagChoices(TagData->CenterIcons);
			}
			if (!Row.Entry.RightIcon.IsSet())
			{
				Row.Entry.RightIcon = ContextMenuWidgetPrivate::IconFromTagChoices(TagData->RightIcons);
			}
		}
	}

	// The actions' own say: hidden rows go, disabled ones grey out. Runs every time the menu opens, so it follows the game.
	if (!IsDesignTime())
	{
		for (int32 Index = Prepared.Num() - 1; Index >= 0; --Index)
		{
			FPreparedRow& Row = Prepared[Index];
			const FContextMenuActionContext Context = MakeActionContext(Row.Entry.EntryId, nullptr);

			bool bVisible = true;
			bool bEnabled = true;
			for (UButtonAction* Action : Row.Actions)
			{
				bVisible &= Action->IsActionVisible(Context);
				bEnabled &= Action->IsActionEnabled(Context);
			}
			for (UButtonAction* Action : Row.TagActions)
			{
				bVisible &= Action->IsActionVisible(Context);
				bEnabled &= Action->IsActionEnabled(Context);
			}

			if (!bVisible)
			{
				Prepared.RemoveAt(Index);
				continue;
			}
			Row.Entry.bEnabled &= bEnabled;
		}
	}

	const bool bReserveIconColumn = MenuStyle.bAlwaysReserveIconColumn || Prepared.ContainsByPredicate([](const FPreparedRow& Row)
	{
		return Row.Entry.LeftIcon.IsSet() || Row.Entry.CheckType != EContextMenuCheckType::None;
	});

	for (int32 Index = 0; Index < Prepared.Num(); ++Index)
	{
		const FPreparedRow& Row = Prepared[Index];
		const bool bIsDivider = Row.Entry.EntryType == EContextMenuEntryType::Divider || Row.Entry.EntryType == EContextMenuEntryType::Header;
		TSubclassOf<UContextMenuEntryWidget> RowClass = (bIsDivider && MenuStyle.DividerWidgetClass) ? MenuStyle.DividerWidgetClass : MenuStyle.EntryWidgetClass;
		if (!RowClass)
		{
			RowClass = UContextMenuEntryWidget::StaticClass();
		}

		UContextMenuEntryWidget* RowWidget = CreateWidget<UContextMenuEntryWidget>(this, RowClass);
		if (!RowWidget)
		{
			continue;
		}

#if WITH_EDITOR
		if (IsDesignTime())
		{
			RowWidget->SetDesignerFlags(GetDesignerFlags());
		}
#endif

		RowWidget->SetupEntry(Row.Entry, MenuStyle, this, EntryWidgets.Num(), bReserveIconColumn, Row.Actions, Row.TagActions);

		UPanelSlot* RowSlot = EntriesBox->AddChild(RowWidget);
		if (UVerticalBoxSlot* AsVerticalSlot = Cast<UVerticalBoxSlot>(RowSlot))
		{
			AsVerticalSlot->SetHorizontalAlignment(HAlign_Fill);
		}
		EntryWidgets.Add(RowWidget);
	}
}

TArray<UContextMenuEntryWidget*> UContextMenuWidget::GetEntryWidgets() const
{
	TArray<UContextMenuEntryWidget*> Out;
	Out.Reserve(EntryWidgets.Num());
	for (const TObjectPtr<UContextMenuEntryWidget>& Row : EntryWidgets)
	{
		Out.Add(Row.Get());
	}
	return Out;
}

UContextMenuEntryWidget* UContextMenuWidget::FindEntryWidget(FName EntryId) const
{
	for (const TObjectPtr<UContextMenuEntryWidget>& Row : EntryWidgets)
	{
		if (Row && Row->GetEntry().EntryId == EntryId)
		{
			return Row;
		}
	}
	return nullptr;
}

UInfoCardAnchor* UContextMenuWidget::GetMenuAnchor() const
{
	if (MenuPayload && MenuPayload->MenuAnchor.IsValid())
	{
		return MenuPayload->MenuAnchor.Get();
	}
	return Cast<UInfoCardAnchor>(AnchorWidget.Get());
}

UContextMenuAsset* UContextMenuWidget::GetMenuAsset() const
{
	return MenuPayload ? MenuPayload->Menu.Get() : nullptr;
}

TArray<UContextButtonPayload*> UContextMenuWidget::GetActionPayloads() const
{
	TArray<UContextButtonPayload*> Out;
	for (const TObjectPtr<UNameTBD_Payload>& Payload : Payloads)
	{
		if (UContextButtonPayload* AsAction = Cast<UContextButtonPayload>(Payload.Get()))
		{
			Out.Add(AsAction);
		}
	}
	return Out;
}

FContextMenuActionContext UContextMenuWidget::MakeActionContext(FName EntryId, UContextMenuButton* Button) const
{
	FContextMenuActionContext Context;
	Context.EntryId = EntryId;
	Context.Menu = const_cast<UContextMenuWidget*>(this);
	Context.Button = Button;
	Context.Anchor = GetMenuAnchor();
	Context.SourceWidget = Context.Anchor ? Context.Anchor->GetCardSourceWidget() : nullptr;
	for (UContextButtonPayload* Payload : GetActionPayloads())
	{
		Context.Payloads.Add(Payload);
	}
	return Context;
}

UContextMenuWidget* UContextMenuWidget::GetRootMenu() const
{
	const UContextMenuWidget* Current = this;
	for (int32 Depth = 0; Depth < 32; ++Depth)
	{
		const UContextMenuWidget* Parent = Cast<UContextMenuWidget>(Current->GetParentCard());
		if (!Parent)
		{
			break;
		}
		Current = Parent;
	}
	return const_cast<UContextMenuWidget*>(Current);
}

bool UContextMenuWidget::IsSubmenu() const
{
	return Cast<UContextMenuWidget>(GetParentCard()) != nullptr;
}

void UContextMenuWidget::CloseWholeMenu()
{
	// The first menu takes every submenu with it: their anchors live in its rows.
	if (UContextMenuWidget* Root = GetRootMenu())
	{
		Root->CloseCard();
	}
}

void UContextMenuWidget::HandleEntryChosen(UContextMenuEntryWidget* Row, bool bFromKeyboard)
{
	if (!Row || !Row->IsSelectable() || IsClosing())
	{
		return;
	}

	const FContextMenuEntry ChosenEntry = Row->GetEntry();

	if (ChosenEntry.EntryType == EContextMenuEntryType::Submenu)
	{
		Row->OpenSubmenu(bFromKeyboard);
		return;
	}

	UContextMenuAnchor* MenuAnchor = Cast<UContextMenuAnchor>(GetMenuAnchor());

	// Checks first, so actions and listeners see the state the click produced.
	bool bNowChecked = false;
	if (ChosenEntry.CheckType == EContextMenuCheckType::CheckBox)
	{
		bNowChecked = !Row->IsEntryChecked();
		Row->SetEntryChecked(bNowChecked);
	}
	else if (ChosenEntry.CheckType == EContextMenuCheckType::Radio)
	{
		bNowChecked = true;
		for (const TObjectPtr<UContextMenuEntryWidget>& Other : EntryWidgets)
		{
			if (Other && Other != Row && Other->GetEntry().CheckType == EContextMenuCheckType::Radio
				&& Other->GetEntry().RadioGroup == ChosenEntry.RadioGroup && Other->IsEntryChecked())
			{
				Other->SetEntryChecked(false);
				if (MenuAnchor)
				{
					MenuAnchor->RememberEntryChecked(Other->GetEntry().EntryId, false);
				}
			}
		}
		Row->SetEntryChecked(true);
	}

	if (MenuAnchor && ChosenEntry.CheckType != EContextMenuCheckType::None)
	{
		MenuAnchor->RememberEntryChecked(ChosenEntry.EntryId, bNowChecked);
	}

	// Actions can close the menu, or tear it down outright (travel, a widget removed): everything below re-checks.
	TWeakObjectPtr<UContextMenuWidget> WeakMenu(this);
	TWeakObjectPtr<UContextMenuEntryWidget> WeakRow(Row);
	const FContextMenuActionContext Context = MakeActionContext(ChosenEntry.EntryId, Row->GetRowButton());

	TArray<UButtonAction*> Inline;
	for (const TObjectPtr<UButtonAction>& Action : Row->GetRowActions())
	{
		Inline.Add(Action.Get());
	}
	TArray<UButtonAction*> Tagged;
	for (const TObjectPtr<UButtonAction>& Action : Row->GetRowTagActions())
	{
		Tagged.Add(Action.Get());
	}

	for (UButtonAction* Action : Inline)
	{
		if (Action)
		{
			Action->ExecuteMenuAction(Context);
		}
	}

	if (Tagged.Num() > 0)
	{
		// A tag's actions are handed exactly the payloads its entry expects, index for index (a missing one is null), as a
		// button carrying the tag does.
		FContextMenuActionContext TagContext = Context;
		TagContext.Payloads.Reset();
		if (const FContextMenuButtonData* TagData = UContextMenuButton::FindButtonData(ChosenEntry.ButtonTag))
		{
			for (const TSubclassOf<UContextButtonPayload>& Expected : TagData->ExpectedPayloads)
			{
				TagContext.Payloads.Add(UButtonAction::FindActionPayload(GetActionPayloads(), Expected ? Expected : TSubclassOf<UContextButtonPayload>(UContextButtonPayload::StaticClass())));
			}
		}
		for (UButtonAction* Action : Tagged)
		{
			if (Action && WeakMenu.IsValid())
			{
				Action->ExecuteMenuAction(TagContext);
			}
		}
	}

	if (!WeakMenu.IsValid())
	{
		return;
	}

	OnEntryChosen.Broadcast(ChosenEntry.EntryId, bNowChecked);
	if (MenuAnchor)
	{
		MenuAnchor->NotifyMenuEntryChosen(ChosenEntry.EntryId, bNowChecked);
	}

	if (WeakMenu.IsValid() && ChosenEntry.bCloseMenuOnClick)
	{
		CloseWholeMenu();
	}
}

void UContextMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (!IsDesignTime())
	{
		bRestoreFocusOnClose = true;
		// Focus goes to the first row for keyboard / gamepad openings, else to the menu itself (so Escape and the arrows
		// work without highlighting a row the mouse isn't on). Next tick: the menu isn't in a window yet.
		bPendingInitialFocus = true;
	}
}

void UContextMenuWidget::NativeDestruct()
{
	// The player was in the menu: give focus back to what opened it, as a desktop menu does. Only when the menu still had it.
	if (bRestoreFocusOnClose && HasFocusedDescendants())
	{
		if (UInfoCardAnchor* MenuAnchor = GetMenuAnchor())
		{
			UWidget* AnchorContent = MenuAnchor->GetContent();
			if (AnchorContent)
			{
				AnchorContent->SetKeyboardFocus();
			}
		}
	}

	Super::NativeDestruct();
}

void UContextMenuWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!bPendingInitialFocus || IsDesignTime())
	{
		return;
	}
	bPendingInitialFocus = false;

	const bool bFocusRow = MenuPayload && MenuPayload->bFocusFirstEntry;
	if (!bFocusRow || !FocusFirstEntry())
	{
		SetKeyboardFocus();
	}
}

bool UContextMenuWidget::FocusFirstEntry()
{
	for (const TObjectPtr<UContextMenuEntryWidget>& Row : EntryWidgets)
	{
		if (Row && Row->IsSelectable())
		{
			Row->FocusEntry();
			return true;
		}
	}
	return false;
}

int32 UContextMenuWidget::GetFocusedRowIndex() const
{
	for (int32 Index = 0; Index < EntryWidgets.Num(); ++Index)
	{
		if (EntryWidgets[Index] && EntryWidgets[Index]->IsEntryFocused())
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

bool UContextMenuWidget::MoveFocus(int32 Step)
{
	const int32 Count = EntryWidgets.Num();
	if (Count == 0 || Step == 0)
	{
		return false;
	}

	const int32 From = GetFocusedRowIndex();
	int32 Index = From == INDEX_NONE ? (Step > 0 ? -1 : Count) : From;
	for (int32 Tries = 0; Tries < Count; ++Tries)
	{
		Index += Step;
		if (Index < 0 || Index >= Count)
		{
			if (!MenuStyle.bWrapNavigation && From != INDEX_NONE)
			{
				return true; // at the end: stay, but the key is still the menu's
			}
			Index = (Index + Count) % Count;
		}

		if (EntryWidgets[Index] && EntryWidgets[Index]->IsSelectable())
		{
			EntryWidgets[Index]->FocusEntry();
			return true;
		}
	}
	return false;
}

void UContextMenuWidget::CloseSubmenuLevel()
{
	if (const UInfoCardAnchor* OpenedFrom = Cast<UInfoCardAnchor>(AnchorWidget.Get()))
	{
		if (UContextMenuEntryWidget* ParentRow = OpenedFrom->GetTypedOuter<UContextMenuEntryWidget>())
		{
			ParentRow->FocusEntry();
		}
	}
	bRestoreFocusOnClose = false;
	CloseCard();
}

FReply UContextMenuWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();

	if (Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right)
	{
		if (IsSubmenu())
		{
			CloseSubmenuLevel();
		}
		else
		{
			CloseCard();
		}
		return FReply::Handled();
	}

	if (Key == EKeys::Down || Key == EKeys::Gamepad_DPad_Down)
	{
		return MoveFocus(1) ? FReply::Handled() : FReply::Unhandled();
	}

	if (Key == EKeys::Up || Key == EKeys::Gamepad_DPad_Up)
	{
		return MoveFocus(-1) ? FReply::Handled() : FReply::Unhandled();
	}

	if (Key == EKeys::Home || Key == EKeys::End)
	{
		const int32 From = Key == EKeys::Home ? -1 : EntryWidgets.Num();
		for (int32 Index = From + (Key == EKeys::Home ? 1 : -1); EntryWidgets.IsValidIndex(Index); Index += (Key == EKeys::Home ? 1 : -1))
		{
			if (EntryWidgets[Index] && EntryWidgets[Index]->IsSelectable())
			{
				EntryWidgets[Index]->FocusEntry();
				break;
			}
		}
		return FReply::Handled();
	}

	if (Key == EKeys::Right || Key == EKeys::Gamepad_DPad_Right)
	{
		const int32 Focused = GetFocusedRowIndex();
		if (EntryWidgets.IsValidIndex(Focused) && EntryWidgets[Focused]->GetEntry().EntryType == EContextMenuEntryType::Submenu)
		{
			EntryWidgets[Focused]->OpenSubmenu(/*bFocusFirstEntry=*/true);
		}
		return FReply::Handled();
	}

	if (Key == EKeys::Left || Key == EKeys::Gamepad_DPad_Left)
	{
		if (IsSubmenu())
		{
			CloseSubmenuLevel();
		}
		return FReply::Handled();
	}

	// An entry's shortcut chooses it while the menu is open.
	const UContextMenuAsset* MenuAsset = GetMenuAsset();
	if ((!MenuAsset || MenuAsset->bShortcutsWorkWhileOpen) && !Key.IsModifierKey())
	{
		const FInputChord Pressed(Key, InKeyEvent.IsShiftDown(), InKeyEvent.IsControlDown(), InKeyEvent.IsAltDown(), InKeyEvent.IsCommandDown());
		for (const TObjectPtr<UContextMenuEntryWidget>& Row : EntryWidgets)
		{
			if (Row && Row->IsSelectable() && Row->GetEntry().Shortcut.IsValidChord() && Row->GetEntry().Shortcut == Pressed)
			{
				HandleEntryChosen(Row, /*bFromKeyboard=*/true);
				return FReply::Handled();
			}
		}
	}

	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

FReply UContextMenuWidget::NativeOnKeyChar(const FGeometry& InGeometry, const FCharacterEvent& InCharEvent)
{
	const TCHAR Typed = FChar::ToLower(InCharEvent.GetCharacter());
	if (!MenuStyle.bTypeToSelect || !FChar::IsAlnum(Typed) || EntryWidgets.Num() == 0)
	{
		return Super::NativeOnKeyChar(InGeometry, InCharEvent);
	}

	// The next row after the focused one whose label starts with the letter, going round: pressing it again cycles.
	const int32 Count = EntryWidgets.Num();
	const int32 From = GetFocusedRowIndex();
	for (int32 Step = 1; Step <= Count; ++Step)
	{
		const int32 Index = ((From == INDEX_NONE ? -1 : From) + Step + Count) % Count;
		const UContextMenuEntryWidget* Row = EntryWidgets[Index];
		if (!Row || !Row->IsSelectable())
		{
			continue;
		}

		const FString Label = Row->GetEntry().Label.ToString();
		if (!Label.IsEmpty() && FChar::ToLower(Label[0]) == Typed)
		{
			EntryWidgets[Index]->FocusEntry();
			return FReply::Handled();
		}
	}
	return FReply::Handled();
}
