// Copyright AR GameSystems 2026. All rights reserved.

#include "ContextMenuWidget.h"

#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/ButtonSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/Events.h"
#include "InputAction.h"
#include "Rendering/SlateRenderer.h"

#include "NameTBD/Buttons/ContextMenuButton.h"
#include "NameTBD/Buttons/Actions/ContextMenuInterface.h"
#include "NameTBD/Buttons/Actions/ContextMenuPayload.h"
#include "NameTBD/Cards/InfoCardAnchor.h"
#include "NameTBD/Cards/TooltipCardAnchor.h"
#include "NameTBD/DeveloperSettings/InfoCardDeveloperSettings.h"
#include "NameTBD/Menu/Input/MenuInputSubsystem.h"
#include "NameTBD/Menu/Input/MenuInputTypes.h"
#include "NameTBD/Menu/Settings/MenuProjectSettings.h"
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

	/** The usable textures of a Button Actions entry's icon list, in order, for an entry that has no icons of its own there. */
	TArray<FContextMenuIcon> IconsFromTagChoices(const TArray<FContextButtonImageChoice>& Choices)
	{
		TArray<FContextMenuIcon> Icons;
		for (const FContextButtonImageChoice& Choice : Choices)
		{
			if (Choice.IconChoice == EImageChoice::Icon && !Choice.Icon.IsNull())
			{
				FContextMenuIcon& Icon = Icons.AddDefaulted_GetRef();
				Icon.ImageSource = EWidgetImageSource::Texture;
				Icon.Texture = Choice.Icon.LoadSynchronous();
			}
		}
		return Icons;
	}

	/** A chord's keys in the order menus write them: Ctrl, Shift, Alt, Cmd, then the key. */
	void ChordKeys(const FInputChord& Chord, TArray<FKey>& OutKeys)
	{
		if (Chord.bCtrl)
		{
			OutKeys.Add(EKeys::LeftControl);
		}
		if (Chord.bShift)
		{
			OutKeys.Add(EKeys::LeftShift);
		}
		if (Chord.bAlt)
		{
			OutKeys.Add(EKeys::LeftAlt);
		}
		if (Chord.bCmd)
		{
			OutKeys.Add(EKeys::LeftCommand);
		}
		if (Chord.Key.IsValid())
		{
			OutKeys.Add(Chord.Key);
		}
	}

	/** A key as a menu writes it: Ctrl, Shift, Alt, Cmd for modifiers, the gamepad family's label ("A" / "Cross"), else its short name. */
	FText KeyName(const FKey& Key, EMenuGamepadFamily Family)
	{
		if (Key == EKeys::LeftControl || Key == EKeys::RightControl)
		{
			return NSLOCTEXT("ContextMenu", "KeyCtrl", "Ctrl");
		}
		if (Key == EKeys::LeftShift || Key == EKeys::RightShift)
		{
			return NSLOCTEXT("ContextMenu", "KeyShift", "Shift");
		}
		if (Key == EKeys::LeftAlt || Key == EKeys::RightAlt)
		{
			return NSLOCTEXT("ContextMenu", "KeyAlt", "Alt");
		}
		if (Key == EKeys::LeftCommand || Key == EKeys::RightCommand)
		{
			return NSLOCTEXT("ContextMenu", "KeyCmd", "Cmd");
		}
		if (Key.IsGamepadKey())
		{
			return MenuInputUtils::GetKeyLabel(Key, Family);
		}
		return Key.GetDisplayName(/*bLongDisplayName=*/false);
	}

	/**
	 * A key's art: the player's Menu Input Subsystem (their device's family, the project's Key Glyph Sets), or - with no local
	 * player, as in the designer - the project's sets read directly (keyboard art, or the Default Gamepad Family's).
	 */
	FMenuKeyGlyph FindKeyGlyph(const UObject* Context, const FKey& Key)
	{
		if (const UMenuInputSubsystem* Input = UMenuInputSubsystem::Get(Context))
		{
			return Input->GetKeyGlyph(Key);
		}

		const UMenuProjectSettings& Settings = UMenuProjectSettings::Get();
		FMenuKeyGlyph Found;
		Found.Key = Key;
		for (const TSoftObjectPtr<UMenuKeyGlyphSet>& SetEntry : Settings.KeyGlyphSets)
		{
			const UMenuKeyGlyphSet* Set = SetEntry.LoadSynchronous();
			const bool bRightSet = Set && (Key.IsGamepadKey() ? (!Set->bForKeyboard && Set->Family == Settings.DefaultGamepadFamily) : Set->bForKeyboard);
			if (const FMenuKeyGlyph* InSet = bRightSet ? Set->Find(Key) : nullptr)
			{
				Found = *InSet;
				break;
			}
		}
		return Found;
	}
}

// =================================================================================================
// UContextMenuEntryWidget
// =================================================================================================

void UContextMenuEntryWidget::SetupEntry(const FContextMenuEntry& InEntry, const FContextMenuStyle& InStyle, UContextMenuWidget* InMenu, int32 InIndex,
	int32 LeftColumnIcons, const TArray<UButtonAction*>& InActions, const TArray<UButtonAction*>& InTagActions)
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
		BuildNativeTree(LeftColumnIcons);
		bBuiltNativeTree = true;
	}

	FillParts();
	OnMenuEntrySet(Entry, bEntryChecked);
}

float UContextMenuEntryWidget::GetIconColumnWidth(const FContextMenuStyle& InStyle, int32 InIconCount)
{
	if (InIconCount <= 0)
	{
		return 0.f;
	}

	// Between two icons: the gap, or gap - delimiter - gap. Measured, so every row's labels start at the same place.
	float Between = InStyle.IconGap;
	if (!InStyle.IconDelimiter.IsEmpty() && FSlateApplication::IsInitialized() && FSlateApplication::Get().GetRenderer())
	{
		const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		Between = InStyle.IconGap * 2.f + FontMeasure->Measure(InStyle.IconDelimiter, InStyle.ShortcutFont).X;
	}
	return InIconCount * InStyle.IconSize + (InIconCount - 1) * Between;
}

void UContextMenuEntryWidget::BuildNativeTree(int32 LeftColumnIcons)
{
	switch (Entry.EntryType)
	{
	case EContextMenuEntryType::Button:
	case EContextMenuEntryType::Submenu:
		WidgetTree->RootWidget = BuildButtonRow(LeftColumnIcons);
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

UWidget* UContextMenuEntryWidget::MakeIconList(const TArray<FContextMenuIcon>& Icons, const TCHAR* Base, TArray<TObjectPtr<UWidget>>& OutParts)
{
	UHorizontalBox* List = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), FName(*FString::Printf(TEXT("%sList"), Base)));
	const bool bDelimited = !EntryStyle.IconDelimiter.IsEmpty();

	for (const FContextMenuIcon& Icon : Icons)
	{
		if (!Icon.IsSet())
		{
			continue;
		}

		const bool bFirst = OutParts.IsEmpty();
		if (!bFirst && bDelimited)
		{
			UTextBlock* Delimiter = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), MakeUniqueObjectName(WidgetTree, UTextBlock::StaticClass(), TEXT("IconDelimiter")));
			Delimiter->SetText(EntryStyle.IconDelimiter);
			Delimiter->SetFont(EntryStyle.ShortcutFont);
			UHorizontalBoxSlot* DelimiterSlot = List->AddChildToHorizontalBox(Delimiter);
			DelimiterSlot->SetVerticalAlignment(VAlign_Center);
			DelimiterSlot->SetPadding(FMargin(EntryStyle.IconGap, 0.f, 0.f, 0.f));
			IconDelimiterParts.Add(Delimiter);
		}

		// Base, Base_1, Base_2...: the first is the part a Row Button Preset's targets (and Widget Blueprint rows) know.
		const FName PartName = bFirst ? FName(Base) : FName(*FString::Printf(TEXT("%s_%d"), Base, OutParts.Num()));
		TObjectPtr<UWidget> Part;
		UHorizontalBoxSlot* IconSlot = List->AddChildToHorizontalBox(MakeIconPart(Icon, PartName, nullptr, Part));
		IconSlot->SetVerticalAlignment(VAlign_Center);
		IconSlot->SetPadding(FMargin(bFirst ? 0.f : EntryStyle.IconGap, 0.f, 0.f, 0.f));
		OutParts.Add(Part);
	}
	return List;
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

UWidget* UContextMenuEntryWidget::BuildButtonRow(int32 LeftColumnIcons)
{
	using namespace ContextMenuWidgetPrivate;

	RowButton = WidgetTree->ConstructWidget<UContextMenuButton>(UContextMenuButton::StaticClass(), TEXT("RowButton"));
	RowHighlight = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("RowHighlight"));
	USizeBox* RowSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("RowSize"));
	UHorizontalBox* Parts = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("RowParts"));

	// A button slot centres its content by default: the highlight then shrank to the label. It fills the row (the inset
	// between them is the style's Highlight Inset, on the menu's slot for the row); FillParts puts this right again on
	// whichever slot the button builds from.
	RowButton->SetContent(RowHighlight);
	RowHighlight->SetContent(RowSize);
	RowSize->SetContent(Parts);

	const EVerticalAlignment PartsVertical = EntryStyle.RowContentVerticalAlignment.GetValue();
	bool bFirstPart = true;
	auto AddPart = [&Parts, &bFirstPart, PartsVertical, this](UWidget* Part, bool bFill) -> UHorizontalBoxSlot*
	{
		UHorizontalBoxSlot* PartSlot = Parts->AddChildToHorizontalBox(Part);
		PartSlot->SetVerticalAlignment(PartsVertical);
		PartSlot->SetPadding(FMargin(bFirstPart ? 0.f : EntryStyle.PartGap, 0.f, 0.f, 0.f));
		if (bFill)
		{
			PartSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}
		bFirstPart = false;
		return PartSlot;
	};

	// The icon column: the left icons, or the check / radio mark of a checkable row. As wide as the widest row's, so every
	// label in the menu starts at the same place - and further right once a row has more icons.
	const bool bHasCheck = Entry.CheckType != EContextMenuCheckType::None;
	if (LeftColumnIcons > 0 || bHasCheck || Entry.CountLeftIcons() > 0)
	{
		USizeBox* Column = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("LeftIconColumn"));
		Column->SetWidthOverride(GetIconColumnWidth(EntryStyle, FMath::Max3(LeftColumnIcons, bHasCheck ? 1 : 0, Entry.CountLeftIcons())));
		UWidget* ColumnContent = nullptr;
		if (bHasCheck)
		{
			const FContextMenuIcon& Mark = Entry.CheckType == EContextMenuCheckType::Radio ? EntryStyle.RadioMark : EntryStyle.CheckMark;
			ColumnContent = MakeIconPart(Mark, TEXT("CheckMark"), Entry.CheckType == EContextMenuCheckType::Radio ? TEXT("●") : TEXT("✓"), CheckMark);
		}
		else
		{
			ColumnContent = MakeIconList(Entry.LeftIcons, TEXT("LeftIcon"), LeftIconParts);
		}
		if (USizeBoxSlot* ContentSlot = Cast<USizeBoxSlot>(Column->SetContent(ColumnContent)))
		{
			ContentSlot->SetHorizontalAlignment(HAlign_Left);
			ContentSlot->SetVerticalAlignment(VAlign_Center);
		}
		AddPart(Column, false);
	}

	if (Entry.CenterIcons.ContainsByPredicate([](const FContextMenuIcon& Icon) { return Icon.IsSet(); }))
	{
		AddPart(MakeIconList(Entry.CenterIcons, TEXT("CenterIcon"), CenterIconParts), false);
	}

	// Left text (the label), Center Text and Right Text: three texts, one per alignment. Center Text sits in the middle of the
	// room between the label and the right-hand parts: the label and a spacer after Center Text share that room evenly.
	LabelText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("LabelText"));
	LabelText->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
	const bool bHasCenterText = !Entry.CenterText.IsEmpty();
	const EHorizontalAlignment CenterPlacement = EntryStyle.CenterTextAlignment.HorizontalAlignment.GetValue();
	AddPart(LabelText, !bHasCenterText || CenterPlacement != HAlign_Left);

	if (bHasCenterText)
	{
		CenterText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CenterText"));
		CenterText->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
		AddPart(CenterText, false);
		if (CenterPlacement != HAlign_Right)
		{
			AddPart(WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass(), TEXT("CenterTextBalance")), true);
		}
	}

	RightText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("RightText"));
	AddPart(RightText, false);

	// The shortcuts: as text in ShortcutText, or keys built into BindingsBox (art, frames, spaced delimiters).
	ShortcutText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ShortcutText"));
	AddPart(ShortcutText, false);
	UHorizontalBox* Keys = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("BindingsBox"));
	BindingsBox = Keys;
	AddPart(Keys, false);

	if (Entry.RightIcons.ContainsByPredicate([](const FContextMenuIcon& Icon) { return Icon.IsSet(); }))
	{
		AddPart(MakeIconList(Entry.RightIcons, TEXT("RightIcon"), RightIconParts), false);
	}

	if (Entry.EntryType == EContextMenuEntryType::Submenu || Entry.HasAttachedMenu())
	{
		AddPart(MakeIconPart(EntryStyle.SubmenuArrow, TEXT("SubmenuArrow"), TEXT("›"), SubmenuArrow), false);
	}

	// The row's tooltip as an info card (the style's Row Tooltips): a Tooltip Card Anchor around the button.
	UWidget* RowRoot = RowButton;
	if (!Entry.Description.IsEmpty() && EntryStyle.RowTooltips.Mode == EContextMenuTooltipMode::TooltipCard)
	{
		TooltipAnchor = WidgetTree->ConstructWidget<UTooltipCardAnchor>(UTooltipCardAnchor::StaticClass(), TEXT("TooltipAnchor"));
		TooltipAnchor->SetContent(RowRoot);
		RowRoot = TooltipAnchor;
	}

	if (OpensMenuBeside())
	{
		// The submenu (or attached menu) opens from an anchor around the row: hover (after the style's delay), click and
		// Right all go through it, and it brings placement beside the row, flipping at the screen edge and keep-open while
		// the cursor travels to it.
		SubmenuAnchor = WidgetTree->ConstructWidget<UInfoCardAnchor>(UInfoCardAnchor::StaticClass(), TEXT("SubmenuAnchor"));
		SubmenuAnchor->SetContent(RowRoot);
		return SubmenuAnchor;
	}

	return RowRoot;
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

		// The entry's own alignment (its justification follows it), or the style's Text Divider Alignment.
		const FContextMenuTextAlignment& StyleAlignment = EntryStyle.TextDividerAlignment;
		EHorizontalAlignment Alignment = StyleAlignment.HorizontalAlignment.GetValue() == HAlign_Fill ? HAlign_Left : StyleAlignment.HorizontalAlignment.GetValue();
		ETextJustify::Type Justification = StyleAlignment.Justification.GetValue();
		switch (Entry.TextDividerAlignment)
		{
		case EContextMenuTextDividerAlignment::Left:
			Alignment = HAlign_Left;
			Justification = ETextJustify::Left;
			break;
		case EContextMenuTextDividerAlignment::Center:
			Alignment = HAlign_Center;
			Justification = ETextJustify::Center;
			break;
		case EContextMenuTextDividerAlignment::Right:
			Alignment = HAlign_Right;
			Justification = ETextJustify::Right;
			break;
		case EContextMenuTextDividerAlignment::StyleDefault:
		default:
			break;
		}
		HeaderText->SetJustification(Justification);
		const float Gap = EntryStyle.PartGap;

		// Category ------   ---- Category ----   ------ Category
		if (bLine && (Alignment == HAlign_Center || Alignment == HAlign_Right))
		{
			AddLine(TEXT("DividerLineBefore"), FMargin(0.f, 0.f, Gap, 0.f));
		}

		UHorizontalBoxSlot* TextSlot = Row->AddChildToHorizontalBox(HeaderText);
		TextSlot->SetVerticalAlignment(StyleAlignment.VerticalAlignment);
		if (!bLine)
		{
			// Text alone: the row is the text's room, and the text sits in it by the alignment.
			TextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			TextSlot->SetHorizontalAlignment(Alignment);
		}

		if (bLine && (Alignment == HAlign_Left || Alignment == HAlign_Center))
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
	using namespace ContextMenuWidgetPrivate;

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
		RowLook.Normal = NoDrawBrush();
		RowLook.Hovered = NoDrawBrush();
		RowLook.Pressed = NoDrawBrush();
		RowLook.Disabled = NoDrawBrush();
		RowLook.NormalPadding = FMargin(0.f);
		RowLook.PressedPadding = FMargin(0.f);
		RowButton->SetStyle(RowLook);
	}

	// After the preset, on the slot the button really builds from (the content slot, or the one kept for parts hidden from
	// the Hierarchy): the row's content fills the button, so the highlight is as wide as the row. A Widget Blueprint row's
	// own slot is its author's.
	if (bBuiltNativeTree)
	{
		if (UButtonSlot* PartsSlot = Cast<UButtonSlot>(RowButton->GetPartsSlot()))
		{
			PartsSlot->SetHorizontalAlignment(HAlign_Fill);
			PartsSlot->SetVerticalAlignment(VAlign_Fill);
			PartsSlot->SetPadding(FMargin(0.f));
		}
	}

	RowButton->bShouldDestroyCard = false;
	RowButton->bButtonCanBeEnabled = false;
	RowButton->SetIsEnabled(Entry.bEnabled);
	RowButton->OnContextClicked.AddUniqueDynamic(this, &UContextMenuEntryWidget::HandleRowClicked);

	if (RowHighlight)
	{
		RowHighlight->SetPadding(EntryStyle.RowPadding);
		RowHighlight->SetBrush(NoDrawBrush());
		if (bBuiltNativeTree)
		{
			// Fill: the label takes the room left and the shortcuts line up on the right; otherwise the parts huddle.
			RowHighlight->SetHorizontalAlignment(EntryStyle.RowContentHorizontalAlignment);
			RowHighlight->SetVerticalAlignment(VAlign_Fill);
		}
	}

	if (USizeBox* RowSize = Cast<USizeBox>(RowHighlight ? RowHighlight->GetContent() : nullptr))
	{
		RowSize->SetMinDesiredHeight(EntryStyle.RowHeight);
	}

	// Each text by the style's alignment for it (native rows: a Widget Blueprint row lays its texts out itself).
	auto AlignText = [this](UTextBlock* Text, const FContextMenuTextAlignment& Alignment)
	{
		if (!Text || !bBuiltNativeTree)
		{
			return;
		}
		Text->SetJustification(Alignment.Justification);
		if (UHorizontalBoxSlot* TextSlot = Cast<UHorizontalBoxSlot>(Text->Slot))
		{
			TextSlot->SetHorizontalAlignment(Alignment.HorizontalAlignment);
			TextSlot->SetVerticalAlignment(Alignment.VerticalAlignment);
		}
	};

	if (LabelText)
	{
		LabelText->SetText(Entry.Label);
		LabelText->SetFont(EntryStyle.LabelFont);
		LabelText->SetVisibility(Entry.Label.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
		AlignText(LabelText, EntryStyle.LabelAlignment);
	}

	if (CenterText)
	{
		CenterText->SetText(Entry.CenterText);
		CenterText->SetFont(EntryStyle.LabelFont);
		CenterText->SetVisibility(Entry.CenterText.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
		AlignText(CenterText, EntryStyle.CenterTextAlignment);
	}

	if (RightText)
	{
		RightText->SetText(Entry.RightText);
		RightText->SetFont(EntryStyle.ShortcutFont);
		RightText->SetVisibility(Entry.RightText.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
		AlignText(RightText, EntryStyle.RightTextAlignment);
	}

	AlignText(ShortcutText, EntryStyle.ShortcutAlignment);
	FillBindings();
	FillIconParts();

	if (SubmenuArrow)
	{
		SubmenuArrow->SetVisibility(OpensMenuBeside() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	if (OpensMenuBeside())
	{
		SetupSubmenuAnchor();
	}
	SetupTooltip();

	ApplyStateLook();
}

void UContextMenuEntryWidget::FillIconParts()
{
	// A Widget Blueprint row: its LeftIcon, LeftIcon_1, LeftIcon_2... in order (the native row made its lists itself).
	auto Collect = [this](UWidget* First, const TCHAR* Base, TArray<TObjectPtr<UWidget>>& OutParts)
	{
		if (!OutParts.IsEmpty() || !First || !WidgetTree)
		{
			return;
		}
		OutParts.Add(First);
		for (int32 Index = 1; Index < 16; ++Index)
		{
			UWidget* Next = WidgetTree->FindWidget(FName(*FString::Printf(TEXT("%s_%d"), Base, Index)));
			if (!Next)
			{
				break;
			}
			OutParts.Add(Next);
		}
	};
	Collect(LeftIcon, TEXT("LeftIcon"), LeftIconParts);
	Collect(CenterIcon, TEXT("CenterIcon"), CenterIconParts);
	Collect(RightIcon, TEXT("RightIcon"), RightIconParts);

	// The first part of each list is the one a Row Button Preset's targets and Blueprint reach by name.
	LeftIcon = LeftIconParts.IsEmpty() ? LeftIcon.Get() : LeftIconParts[0].Get();
	CenterIcon = CenterIconParts.IsEmpty() ? CenterIcon.Get() : CenterIconParts[0].Get();
	RightIcon = RightIconParts.IsEmpty() ? RightIcon.Get() : RightIconParts[0].Get();

	// Parts with no icon of the entry's to show (a Widget Blueprint row with more slots than icons) are collapsed.
	auto ShowFor = [](const TArray<TObjectPtr<UWidget>>& PartList, const TArray<FContextMenuIcon>& Icons)
	{
		int32 SetCount = 0;
		for (const FContextMenuIcon& Icon : Icons)
		{
			SetCount += Icon.IsSet() ? 1 : 0;
		}
		for (int32 Index = 0; Index < PartList.Num(); ++Index)
		{
			if (PartList[Index])
			{
				PartList[Index]->SetVisibility(Index < SetCount ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
			}
		}
	};
	if (Entry.CheckType == EContextMenuCheckType::None || !bBuiltNativeTree)
	{
		ShowFor(LeftIconParts, Entry.LeftIcons);
	}
	ShowFor(CenterIconParts, Entry.CenterIcons);
	ShowFor(RightIconParts, Entry.RightIcons);

	for (UTextBlock* Delimiter : IconDelimiterParts)
	{
		if (Delimiter)
		{
			Delimiter->SetText(EntryStyle.IconDelimiter);
			Delimiter->SetFont(EntryStyle.ShortcutFont);
		}
	}
}

void UContextMenuEntryWidget::GatherBindings(TArray<TArray<FKey>>& OutBindings, bool bForDisplay) const
{
	using namespace ContextMenuWidgetPrivate;
	OutBindings.Reset();
	if (Entry.EntryType != EContextMenuEntryType::Button)
	{
		return;
	}

	const FContextMenuBindingStyle& Look = EntryStyle.Bindings;
	const UMenuInputSubsystem* Input = UMenuInputSubsystem::Get(this);
	const bool bOnGamepad = Input && Input->IsUsingGamepad();
	const bool bKeyboard = !bForDisplay || !(bOnGamepad && Look.bHideKeyboardBindingsOnGamepad);
	const bool bGamepad = !bForDisplay || bOnGamepad || !Look.bGamepadBindingsOnlyOnGamepad;

	auto AddKeys = [&OutBindings](TArray<FKey>&& Keys)
	{
		if (Keys.Num() > 0 && !OutBindings.Contains(Keys))
		{
			OutBindings.Add(MoveTemp(Keys));
		}
	};
	auto AddChord = [&AddKeys](const FInputChord& Chord)
	{
		if (Chord.IsValidChord())
		{
			TArray<FKey> Keys;
			ChordKeys(Chord, Keys);
			AddKeys(MoveTemp(Keys));
		}
	};
	auto AddKey = [&AddKeys](const FKey& Key)
	{
		if (Key.IsValid())
		{
			AddKeys(TArray<FKey>{ Key });
		}
	};

	if (bKeyboard)
	{
		AddChord(Entry.Shortcut);
		for (const FInputChord& More : Entry.MoreShortcuts)
		{
			AddChord(More);
		}
		if (Input && Entry.ShortcutAction)
		{
			AddKey(Input->GetKeyForAction(Entry.ShortcutAction, EMenuInputDevice::KeyboardMouse));
		}
	}
	if (bGamepad)
	{
		AddKey(Entry.GamepadShortcut);
		if (Input && Entry.ShortcutAction)
		{
			AddKey(Input->GetKeyForAction(Entry.ShortcutAction, EMenuInputDevice::Gamepad));
		}
	}

	if (bForDisplay && Look.MaxBindings > 0 && OutBindings.Num() > Look.MaxBindings)
	{
		OutBindings.SetNum(Look.MaxBindings);
	}
}

bool UContextMenuEntryWidget::MatchesShortcut(const FInputChord& Pressed) const
{
	TArray<TArray<FKey>> Bindings;
	GatherBindings(Bindings, /*bForDisplay=*/false);

	TArray<FKey> PressedKeys;
	ContextMenuWidgetPrivate::ChordKeys(Pressed, PressedKeys);
	return Bindings.Contains(PressedKeys);
}

UTextBlock* UContextMenuEntryWidget::MakeBindingText(const FText& InText)
{
	const FContextMenuBindingStyle& Look = EntryStyle.Bindings;
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), MakeUniqueObjectName(WidgetTree, UTextBlock::StaticClass(), TEXT("BindingText")));
	Text->SetText(InText);
	Text->SetFont(Look.bOverrideFont ? Look.Font : EntryStyle.ShortcutFont);
	BindingTextParts.Add(Text);
	return Text;
}

UWidget* UContextMenuEntryWidget::MakeGlyphImage(const FMenuImage& Art)
{
	const float Height = EntryStyle.Bindings.GlyphHeight;
	if (Art.ImageSource == EWidgetImageSource::SVG && Art.SVG)
	{
		UWidgetSVGImage* Vector = WidgetTree->ConstructWidget<UWidgetSVGImage>(UWidgetSVGImage::StaticClass(), MakeUniqueObjectName(WidgetTree, UWidgetSVGImage::StaticClass(), TEXT("BindingGlyph")));
		Vector->SetSVG(Art.SVG);
		const FVector2D ArtSize = Art.SVG->GetSVGSize();
		Vector->SetImageSize(FVector2D(ArtSize.Y > 0.f ? Height * ArtSize.X / ArtSize.Y : Height, Height));
		return Vector;
	}

	UImage* Picture = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), MakeUniqueObjectName(WidgetTree, UImage::StaticClass(), TEXT("BindingGlyph")));
	Picture->SetBrushResourceObject(Art.Image);
	float Width = Height;
	if (const UTexture2D* Texture = Cast<UTexture2D>(Art.Image))
	{
		// Wide art (Space, a d-pad) keeps its proportions.
		Width = Texture->GetSizeY() > 0 ? Height * Texture->GetSizeX() / Texture->GetSizeY() : Height;
	}
	Picture->SetDesiredSizeOverride(FVector2D(Width, Height));
	return Picture;
}

void UContextMenuEntryWidget::FillBindings()
{
	using namespace ContextMenuWidgetPrivate;

	TArray<TArray<FKey>> Bindings;
	GatherBindings(Bindings, /*bForDisplay=*/true);

	const FContextMenuBindingStyle& Look = EntryStyle.Bindings;
	const bool bAsArt = Entry.ShortcutDisplay == EContextMenuEntryBindingDisplay::Image
		|| (Entry.ShortcutDisplay == EContextMenuEntryBindingDisplay::StyleDefault && Look.Display == EContextMenuBindingDisplay::Image);
	const UMenuInputSubsystem* Input = UMenuInputSubsystem::Get(this);
	const EMenuGamepadFamily Family = Input ? Input->GetGamepadFamily() : UMenuProjectSettings::Get().DefaultGamepadFamily;

	// Text first: Ctrl+C, Ctrl+Insert. ShortcutText shows it when nothing needs building (and in Widget Blueprint rows with
	// no BindingsBox).
	TArray<FString> BindingStrings;
	for (const TArray<FKey>& Keys : Bindings)
	{
		TArray<FString> Names;
		for (const FKey& Key : Keys)
		{
			Names.Add(KeyName(Key, Family).ToString());
		}
		BindingStrings.Add(FString::Join(Names, *Look.KeyDelimiter.ToString()));
	}
	const bool bPlainText = !bAsArt && Look.Frame == EContextMenuBindingFrame::None && Look.KeyGap <= 0.f;
	const bool bBuild = BindingsBox && !bPlainText && Bindings.Num() > 0;

	if (ShortcutText)
	{
		ShortcutText->SetText(FText::FromString(FString::Join(BindingStrings, *Look.BindingSeparator.ToString())));
		ShortcutText->SetFont(Look.bOverrideFont ? Look.Font : EntryStyle.ShortcutFont);
		ShortcutText->SetVisibility(Bindings.Num() > 0 && !bBuild ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	BindingTextParts.Reset();
	BindingImageParts.Reset();
	BindingImageTints.Reset();
	if (!BindingsBox)
	{
		return;
	}
	BindingsBox->ClearChildren();
	BindingsBox->SetVisibility(bBuild ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (!bBuild || !WidgetTree)
	{
		return;
	}

	auto Add = [](UPanelWidget* Container, UWidget* Child, float LeftGap)
	{
		UPanelSlot* ChildSlot = Container->AddChild(Child);
		if (UHorizontalBoxSlot* AsHorizontal = Cast<UHorizontalBoxSlot>(ChildSlot))
		{
			AsHorizontal->SetVerticalAlignment(VAlign_Center);
			AsHorizontal->SetPadding(FMargin(LeftGap, 0.f, 0.f, 0.f));
		}
	};
	auto MakeFrame = [this, &Look](UWidget* Content) -> UBorder*
	{
		UBorder* Frame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), MakeUniqueObjectName(WidgetTree, UBorder::StaticClass(), TEXT("BindingFrame")));
		Frame->SetBrush(Look.FrameBrush);
		Frame->SetPadding(Look.FramePadding);
		Frame->SetHorizontalAlignment(HAlign_Center);
		Frame->SetVerticalAlignment(VAlign_Center);
		Frame->SetContent(Content);
		return Frame;
	};

	for (int32 BindingIndex = 0; BindingIndex < Bindings.Num(); ++BindingIndex)
	{
		if (BindingIndex > 0)
		{
			Add(BindingsBox, MakeBindingText(Look.BindingSeparator), Look.KeyGap);
		}

		// Whole Binding: one frame, the keys and their delimiters inside it. Otherwise straight into the box.
		UPanelWidget* Container = BindingsBox;
		float LeadGap = BindingIndex > 0 ? Look.KeyGap : 0.f;
		if (Look.Frame == EContextMenuBindingFrame::WholeBinding)
		{
			UHorizontalBox* Inner = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), MakeUniqueObjectName(WidgetTree, UHorizontalBox::StaticClass(), TEXT("BindingKeys")));
			Add(BindingsBox, MakeFrame(Inner), LeadGap);
			Container = Inner;
			LeadGap = 0.f;
		}

		const TArray<FKey>& Keys = Bindings[BindingIndex];
		for (int32 KeyIndex = 0; KeyIndex < Keys.Num(); ++KeyIndex)
		{
			if (KeyIndex > 0)
			{
				Add(Container, MakeBindingText(Look.KeyDelimiter), Look.KeyGap);
			}

			// The key's art from the project's Key Glyph Sets, else its name.
			UWidget* KeyPart = nullptr;
			if (bAsArt)
			{
				const FMenuKeyGlyph Glyph = FindKeyGlyph(this, Keys[KeyIndex]);
				if (Glyph.Image.HasImage())
				{
					KeyPart = MakeGlyphImage(Glyph.Image);
					BindingImageParts.Add(KeyPart);
					BindingImageTints.Add(Glyph.Image.Tint);
				}
			}
			if (!KeyPart)
			{
				KeyPart = MakeBindingText(KeyName(Keys[KeyIndex], Family));
			}

			if (Look.Frame == EContextMenuBindingFrame::EachKey)
			{
				KeyPart = MakeFrame(KeyPart);
			}
			Add(Container, KeyPart, KeyIndex > 0 ? Look.KeyGap : LeadGap);
		}
	}
}

void UContextMenuEntryWidget::SetupSubmenuAnchor()
{
	UContextMenuWidget* Menu = OwningMenu.Get();
	const bool bAttached = Entry.HasAttachedMenu();
	UContextMenuAsset* Opens = bAttached ? Entry.AttachedMenu.Get() : Entry.Submenu.Get();
	if (!SubmenuAnchor || !Menu || !Opens)
	{
		return;
	}

	// A submenu looks like the menu it came from, whatever its own asset says; an attached menu may pick a style of its own.
	// Both report to the same anchor.
	UContextMenuStyleAsset* OwnStyle = bAttached ? Entry.AttachedMenuStyle.Get() : nullptr;
	UInfoCardPayload_ContextMenu* SubPayload = NewObject<UInfoCardPayload_ContextMenu>(SubmenuAnchor);
	SubPayload->Menu = Opens;
	SubPayload->StyleOverride = OwnStyle ? OwnStyle : Menu->GetMenuStyleAsset();
	SubPayload->MenuAnchor = Menu->GetMenuAnchor();

	const TSubclassOf<UContextMenuWidget> MenuClass = OwnStyle ? OwnStyle->Style.MenuWidgetClass : EntryStyle.MenuWidgetClass;

	FInfoCardSpec Spec;
	Spec.CardId = SubmenuCardId;
	Spec.CardClass = MenuClass ? TSubclassOf<UInfoCardWidget>(MenuClass) : TSubclassOf<UInfoCardWidget>(Menu->GetClass());
	Spec.CardType = ECardType::Hover;
	Spec.HoverDelay.Source = EInfoCardValueSource::Custom;
	Spec.HoverDelay.CustomValue = EntryStyle.SubmenuDelay;
	// An attached menu set to open on press waits for the click (HandleEntryChosen opens it by ID).
	Spec.bTriggerOnMouseHover = !bAttached || Entry.AttachedMenuOpenOn == EContextMenuAttachedMenuTrigger::Hover;
	Spec.bTriggerOnFocus = false;
	Spec.bKeepOpenUntilCursorLeaves = true;

	// Beside the row on its right, its panel lined up with this one's (gap = panel padding); an attached menu as it says.
	const FInfoCardPositioning Beside = bAttached ? Entry.AttachedMenuPositioning : FInfoCardPositioning::MakeBeside(EDirection2D::Right, EntryStyle.PanelPadding.Right);
	Beside.ApplyTo(Spec);

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

void UContextMenuEntryWidget::SetupTooltip()
{
	const FContextMenuTooltipStyle& Tips = EntryStyle.RowTooltips;
	const bool bAsCard = TooltipAnchor && Tips.Mode == EContextMenuTooltipMode::TooltipCard;

	// The engine's plain tooltip, or none on the button when the card shows the Description.
	RowButton->SetToolTipText(bAsCard ? FText::GetEmpty() : Entry.Description);
	if (!TooltipAnchor)
	{
		return;
	}

	TooltipAnchor->TooltipTemplate = Tips.Template;
	TooltipAnchor->TemplateCardId = Tips.TemplateCardId;
	TooltipAnchor->bOverrideTooltipTextStyle = Tips.bOverrideTextStyle;
	TooltipAnchor->TooltipTextStyle = Tips.TextStyle;
	TooltipAnchor->bOverrideTooltipDelay = Tips.bOverrideDelay;
	TooltipAnchor->TooltipDelay = Tips.Delay;
	TooltipAnchor->bOverrideTooltipPosition = Tips.bOverridePositioning;
	TooltipAnchor->TooltipPositioning = Tips.Positioning;
	// A row's tooltip must never close the menu it is in (a template may say Close Other Cards).
	TooltipAnchor->SetKeepsOtherCardsOpen(true);
	TooltipAnchor->SetTooltipCardText(bAsCard ? Entry.Description : FText::GetEmpty(), FText::GetEmpty());
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
	if (CenterText)
	{
		CenterText->SetColorAndOpacity(LabelColor);
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
	for (UTextBlock* KeyText : BindingTextParts)
	{
		if (KeyText)
		{
			KeyText->SetColorAndOpacity(ShortcutColor);
		}
	}
	for (UTextBlock* Delimiter : IconDelimiterParts)
	{
		if (Delimiter)
		{
			Delimiter->SetColorAndOpacity(ShortcutColor);
		}
	}

	const FLinearColor IconTint = !bEnabled ? EntryStyle.DisabledIconTint : (bShowHighlightColors ? EntryStyle.HighlightedIconTint : EntryStyle.IconTint);

	// Each list's parts take its set icons in order (unset elements were skipped when the parts were made).
	auto ApplyIconList = [this, &IconTint](const TArray<TObjectPtr<UWidget>>& PartList, const TArray<FContextMenuIcon>& Icons)
	{
		int32 PartIndex = 0;
		for (const FContextMenuIcon& Icon : Icons)
		{
			if (Icon.IsSet() && PartList.IsValidIndex(PartIndex))
			{
				ApplyIcon(PartList[PartIndex++], Icon, IconTint);
			}
		}
	};
	if (Entry.CheckType == EContextMenuCheckType::None || !bBuiltNativeTree)
	{
		ApplyIconList(LeftIconParts, Entry.LeftIcons);
	}
	ApplyIconList(CenterIconParts, Entry.CenterIcons);
	ApplyIconList(RightIconParts, Entry.RightIcons);
	ApplyIcon(SubmenuArrow, EntryStyle.SubmenuArrow, IconTint);

	// Key art keeps its own colours (its glyph's tint); a disabled row fades it like its icons.
	for (int32 Index = 0; Index < BindingImageParts.Num(); ++Index)
	{
		const FLinearColor ArtTint = BindingImageTints.IsValidIndex(Index) ? BindingImageTints[Index] : FLinearColor::White;
		const FLinearColor Faded = ArtTint * FLinearColor(1.f, 1.f, 1.f, bEnabled ? 1.f : EntryStyle.DisabledIconTint.A);
		if (UImage* AsImage = Cast<UImage>(BindingImageParts[Index]))
		{
			AsImage->SetColorAndOpacity(Faded);
		}
		else if (UWidgetSVGImage* AsSVG = Cast<UWidgetSVGImage>(BindingImageParts[Index]))
		{
			AsSVG->SetColorAndOpacity(Faded);
		}
	}

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
			if (Row.Entry.LeftIcons.IsEmpty())
			{
				Row.Entry.LeftIcons = ContextMenuWidgetPrivate::IconsFromTagChoices(TagData->LeftIcons);
			}
			if (Row.Entry.CenterIcons.IsEmpty())
			{
				Row.Entry.CenterIcons = ContextMenuWidgetPrivate::IconsFromTagChoices(TagData->CenterIcons);
			}
			if (Row.Entry.RightIcons.IsEmpty())
			{
				Row.Entry.RightIcons = ContextMenuWidgetPrivate::IconsFromTagChoices(TagData->RightIcons);
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

	// The left icon column is as many icons wide as the row with the most (a check mark counts as one), so every label starts
	// at the same place and moves along for all of them once a row has more.
	int32 LeftColumnIcons = MenuStyle.bAlwaysReserveIconColumn ? 1 : 0;
	for (const FPreparedRow& Row : Prepared)
	{
		const bool bButtonLike = Row.Entry.EntryType == EContextMenuEntryType::Button || Row.Entry.EntryType == EContextMenuEntryType::Submenu;
		if (bButtonLike)
		{
			LeftColumnIcons = FMath::Max(LeftColumnIcons, Row.Entry.CheckType != EContextMenuCheckType::None ? 1 : Row.Entry.CountLeftIcons());
		}
	}

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

		RowWidget->SetupEntry(Row.Entry, MenuStyle, this, EntryWidgets.Num(), LeftColumnIcons, Row.Actions, Row.TagActions);

		UPanelSlot* RowSlot = EntriesBox->AddChild(RowWidget);
		if (UVerticalBoxSlot* AsVerticalSlot = Cast<UVerticalBoxSlot>(RowSlot))
		{
			AsVerticalSlot->SetHorizontalAlignment(HAlign_Fill);
			// Highlight Inset: the button rows' highlights stop short of the panel (Panel Padding) by this much more.
			if (!bIsDivider && Row.Entry.EntryType != EContextMenuEntryType::Spacer)
			{
				AsVerticalSlot->SetPadding(MenuStyle.RowHighlightInset);
			}
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

	// An attached menu that opens on press: the row has run, its menu opens beside it, and this menu stays for it.
	if (ChosenEntry.HasAttachedMenu() && ChosenEntry.AttachedMenuOpenOn == EContextMenuAttachedMenuTrigger::Press)
	{
		if (WeakMenu.IsValid() && WeakRow.IsValid())
		{
			WeakRow->OpenSubmenu(bFromKeyboard);
		}
		return;
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
		if (EntryWidgets.IsValidIndex(Focused) && EntryWidgets[Focused]->OpensMenuBeside())
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
			if (Row && Row->IsSelectable() && Row->MatchesShortcut(Pressed))
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
