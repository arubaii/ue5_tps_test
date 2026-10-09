// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Text/STextBlock.h"

/**
 * A name shown as text, renamed by double-clicking it: the text gives way to a text box holding the name, all of it
 * selected, sized to what is typed - Enter or clicking away commits, Escape puts it back. What the Widget Animations panel's
 * timeline and builder names and the State Machine editor's own machine name are renamed with.
 *
 * Not an SInlineEditableTextBlock: that one's text box is held to the width of the text it replaces, so a name was cut off
 * at the right while it was edited.
 */
class SWidgetAnimNameField : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SWidgetAnimNameField)
		: _Font(FAppStyle::GetFontStyle("NormalFontBold"))
		, _TextColor(FSlateColor::UseForeground())
		, _IsReadOnly(false)
	{}
		/** The name as it is. */
		SLATE_ATTRIBUTE(FText, Text)
		SLATE_ARGUMENT(FSlateFontInfo, Font)
		SLATE_ATTRIBUTE(FSlateColor, TextColor)
		/** True: double-clicking does nothing. */
		SLATE_ATTRIBUTE(bool, IsReadOnly)
		/** The name typed - Enter, or clicking away. Not called on Escape. */
		SLATE_EVENT(FOnTextCommitted, OnTextCommitted)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		NameText = InArgs._Text;
		bReadOnly = InArgs._IsReadOnly;
		OnCommitted = InArgs._OnTextCommitted;

		ChildSlot
		[
			SAssignNew(Switcher, SWidgetSwitcher)
			.WidgetIndex(0)

			+ SWidgetSwitcher::Slot()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(InArgs._Font)
				.Text(NameText)
				.ColorAndOpacity(InArgs._TextColor)
			]

			+ SWidgetSwitcher::Slot()
			.VAlign(VAlign_Center)
			[
				SAssignNew(Box, SEditableTextBox)
				.Font(InArgs._Font)
				.MinDesiredWidth(60.f)
				.SelectAllTextWhenFocused(true)
				.RevertTextOnEscape(true)
				.ClearKeyboardFocusOnCommit(true)
				.OnTextCommitted(this, &SWidgetAnimNameField::HandleCommitted)
			]
		];
	}

	/** Into the text box, the name selected - as a double-click does. */
	void EnterEditingMode()
	{
		if (bReadOnly.Get() || !Box.IsValid() || IsEditing())
		{
			return;
		}
		Box->SetText(NameText.Get());
		Switcher->SetActiveWidgetIndex(1);
		FSlateApplication::Get().SetKeyboardFocus(Box, EFocusCause::SetDirectly);
	}

	bool IsEditing() const
	{
		return Switcher.IsValid() && Switcher->GetActiveWidgetIndex() == 1;
	}

	virtual FReply OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent) override
	{
		if (InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || bReadOnly.Get() || IsEditing())
		{
			return FReply::Unhandled();
		}
		EnterEditingMode();
		return FReply::Handled();
	}

private:

	void HandleCommitted(const FText& NewText, ETextCommit::Type CommitType)
	{
		if (!IsEditing())
		{
			return;
		}
		Switcher->SetActiveWidgetIndex(0);
		// Escape: put back, nothing renamed.
		if (CommitType != ETextCommit::OnCleared)
		{
			OnCommitted.ExecuteIfBound(NewText, CommitType);
		}
	}

	TAttribute<FText> NameText;
	TAttribute<bool> bReadOnly;
	FOnTextCommitted OnCommitted;
	TSharedPtr<SWidgetSwitcher> Switcher;
	TSharedPtr<SEditableTextBox> Box;
};
