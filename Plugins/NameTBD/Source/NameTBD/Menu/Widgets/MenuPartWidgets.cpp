// Copyright AR GameSystems 2026. All rights reserved.

#include "MenuPartWidgets.h"

#include "Components/EditableText.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"

namespace MenuPartWidgetsLocal
{
	/** Text into a part, collapsed while empty. */
	void FillLine(UTextBlock* Block, const FText& Text)
	{
		if (Block)
		{
			Block->SetText(Text);
			Block->SetVisibility(Text.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
		}
	}
}

// =====================================================================================================
//  Base
// =====================================================================================================

FMenuStyle UMenuPartWidget::GetMenuStyle() const
{
	return StyleHolder->Style;
}

FMenuStylePalette UMenuPartWidget::GetMenuPalette() const
{
	return StyleHolder->Palette();
}

void UMenuPartWidget::SetStyleHolder(const FMenuStyleRef& InHolder)
{
	StyleHolder = InHolder;
	OnMenuStyleChanged();
}

// =====================================================================================================
//  Tab
// =====================================================================================================

void UMenuTabWidget::AssignTab(FName InPageId, const FText& InPageName, const FMenuImage& InPageIcon, int32 InTabIndex, TFunction<void()> InSelect)
{
	PageId = InPageId;
	PageName = InPageName;
	PageIcon = InPageIcon;
	TabIndex = InTabIndex;
	SelectFunction = MoveTemp(InSelect);
	if (LabelText)
	{
		LabelText->SetText(StyleHolder->Style.bUppercaseTabs ? InPageName.ToUpper() : InPageName);
	}
	OnTabAssigned(InPageId, InPageName, InPageIcon, InTabIndex);
}

void UMenuTabWidget::SetTabState(bool bInSelected, bool bInHovered, bool bInPressed)
{
	if (bStateSent && bSelected == bInSelected && bLastHovered == bInHovered && bLastPressed == bInPressed)
	{
		return;
	}
	bStateSent = true;
	bSelected = bInSelected;
	bLastHovered = bInHovered;
	bLastPressed = bInPressed;
	OnTabStateChanged(bInSelected, bInHovered, bInPressed);
}

void UMenuTabWidget::SelectTab()
{
	if (SelectFunction)
	{
		SelectFunction();
	}
}

// =====================================================================================================
//  Heading
// =====================================================================================================

void UMenuHeadingWidget::AssignHeading(const FText& InHeadingText, bool bInCategory)
{
	HeadingText = InHeadingText;
	bCategory = bInCategory;
	if (LabelText)
	{
		LabelText->SetText(!bInCategory && StyleHolder->Style.bUppercaseSections ? InHeadingText.ToUpper() : InHeadingText);
	}
	OnHeadingAssigned(InHeadingText, bInCategory);
}

// =====================================================================================================
//  Footer prompt
// =====================================================================================================

void UMenuFooterPromptWidget::AssignPrompt(EMenuFooterAction InAction, const FText& InLabel, TFunction<void()> InPress)
{
	Action = InAction;
	PromptLabel = InLabel;
	PressFunction = MoveTemp(InPress);
	MenuPartWidgetsLocal::FillLine(LabelText, StyleHolder->Style.bUppercaseButtons ? InLabel.ToUpper() : InLabel);
	OnPromptAssigned(InAction, InLabel);
}

void UMenuFooterPromptWidget::SetGlyph(const FMenuKeyGlyph& InGlyph)
{
	Glyph = InGlyph;
	if (KeyText)
	{
		const FText KeyLabel = !InGlyph.Label.IsEmpty() ? InGlyph.Label : (InGlyph.Key.IsValid() ? InGlyph.Key.GetDisplayName(false) : FText::GetEmpty());
		MenuPartWidgetsLocal::FillLine(KeyText, KeyLabel);
	}
	OnGlyphChanged(InGlyph);
}

void UMenuFooterPromptWidget::SetPromptState(bool bInEnabled, bool bInHovered, bool bInPressed)
{
	if (bStateSent && bLastEnabled == bInEnabled && bLastHovered == bInHovered && bLastPressed == bInPressed)
	{
		return;
	}
	bStateSent = true;
	bLastEnabled = bInEnabled;
	bLastHovered = bInHovered;
	bLastPressed = bInPressed;
	OnPromptStateChanged(bInEnabled, bInHovered, bInPressed);
}

void UMenuFooterPromptWidget::Press()
{
	if (PressFunction && bLastEnabled)
	{
		PressFunction();
	}
}

// =====================================================================================================
//  Description
// =====================================================================================================

void UMenuDescriptionWidget::Describe(UMenuSetting* InSetting, const FMenuSettingDescription& InDescription)
{
	using namespace MenuPartWidgetsLocal;
	DescribedSetting = InSetting;
	CurrentDescription = InDescription;
	FillLine(TitleText, InDescription.Title);
	FillLine(ValueLineText, InDescription.ValueLine);
	FillLine(BodyText, InDescription.Body);
	FillLine(OptionText, InDescription.OptionText);
	FillLine(WarningText, InDescription.Warning);
	FillLine(NoteText, InDescription.Note);
	OnDescriptionChanged(InSetting, InDescription);
}

// =====================================================================================================
//  Search
// =====================================================================================================

void UMenuSearchWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (SearchText)
	{
		SearchText->OnTextChanged.AddUniqueDynamic(this, &UMenuSearchWidget::HandleTextChanged);
	}
	if (SearchTextBox)
	{
		SearchTextBox->OnTextChanged.AddUniqueDynamic(this, &UMenuSearchWidget::HandleTextChanged);
	}
}

void UMenuSearchWidget::AssignSearch(const FText& InHint, TFunction<void(const FText&)> InSetText)
{
	SearchHint = InHint;
	SetTextFunction = MoveTemp(InSetText);
	if (SearchText)
	{
		SearchText->SetHintText(InHint);
	}
	if (SearchTextBox)
	{
		SearchTextBox->SetHintText(InHint);
	}
}

void UMenuSearchWidget::ShowSearchText(const FText& Text)
{
	// Shown without searching again.
	bShowing = true;
	if (SearchText && !SearchText->GetText().EqualTo(Text))
	{
		SearchText->SetText(Text);
	}
	if (SearchTextBox && !SearchTextBox->GetText().EqualTo(Text))
	{
		SearchTextBox->SetText(Text);
	}
	bShowing = false;
	OnMenuSearchTextChanged(Text);
}

void UMenuSearchWidget::SetMenuSearchText(const FText& Text)
{
	if (SetTextFunction)
	{
		SetTextFunction(Text);
	}
}

void UMenuSearchWidget::HandleTextChanged(const FText& Text)
{
	if (!bShowing)
	{
		SetMenuSearchText(Text);
	}
}

// =====================================================================================================
//  Dialog
// =====================================================================================================

UMenuDialogWidget::UMenuDialogWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(true);
}

void UMenuDialogWidget::AssignDialog(const FText& InTitle, const FText& InBody, const TArray<FText>& InLabels, const TArray<int32>& InKinds, int32 InDefault,
	int32 InCancel, TFunction<void(int32)> InChoose)
{
	DialogTitle = InTitle;
	DialogBody = InBody;
	ButtonLabels = InLabels;
	ButtonKinds = InKinds;
	DefaultButton = InDefault;
	CancelButton = InCancel;
	ChooseFunction = MoveTemp(InChoose);
	MenuPartWidgetsLocal::FillLine(TitleText, InTitle);
	MenuPartWidgetsLocal::FillLine(BodyText, InBody);
	OnDialogShown(InTitle, InBody, InLabels, InDefault, InCancel);
}

void UMenuDialogWidget::SetBody(const FText& InBody)
{
	if (InBody.EqualTo(DialogBody))
	{
		return;
	}
	DialogBody = InBody;
	MenuPartWidgetsLocal::FillLine(BodyText, InBody);
	OnDialogBodyChanged(InBody);
}

UWidget* UMenuDialogWidget::GetDialogFocusWidget_Implementation()
{
	return nullptr;
}

void UMenuDialogWidget::Choose(int32 ButtonIndex)
{
	if (ChooseFunction)
	{
		// Once: the screen closes the dialog next frame.
		TFunction<void(int32)> Pending = MoveTemp(ChooseFunction);
		ChooseFunction = nullptr;
		Pending(ButtonIndex);
	}
}

void UMenuDialogWidget::CancelDialog()
{
	if (CancelButton != INDEX_NONE)
	{
		Choose(CancelButton);
	}
}

// =====================================================================================================
//  Option list
// =====================================================================================================

UMenuOptionListWidget::UMenuOptionListWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(true);
}

void UMenuOptionListWidget::AssignList(const FText& InTitle, const TArray<FText>& InLabels, int32 InCurrent, TFunction<void(int32)> InPick)
{
	ListTitle = InTitle;
	OptionLabels = InLabels;
	CurrentOption = InCurrent;
	PickFunction = MoveTemp(InPick);
	MenuPartWidgetsLocal::FillLine(TitleText, InTitle);
	OnOptionListShown(InTitle, InLabels, InCurrent);
}

UWidget* UMenuOptionListWidget::GetListFocusWidget_Implementation()
{
	return nullptr;
}

void UMenuOptionListWidget::PickOption(int32 OptionIndex)
{
	if (PickFunction)
	{
		TFunction<void(int32)> Pending = MoveTemp(PickFunction);
		PickFunction = nullptr;
		Pending(OptionIndex);
	}
}

void UMenuOptionListWidget::CancelList()
{
	PickOption(INDEX_NONE);
}
