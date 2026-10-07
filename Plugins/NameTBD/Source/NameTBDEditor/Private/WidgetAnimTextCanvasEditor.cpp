// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimTextCanvasEditor.h"

#include "NameTBDColors.h"
#include "NameTBDStyle.h"
#include "NameTBDTooltips.h"
#include "WidgetShapeEditorUI.h"
#include "NameTBD/WidgetAnimation/WidgetAnimRuntimeSettings.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Widget.h"
#include "Engine/DataTable.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Text/TextLayout.h"
#include "Internationalization/Text.h"
#include "Internationalization/TextKey.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "WidgetBlueprint.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "WidgetAnimTextCanvasEditor"

namespace WidgetAnimTips = NameTBD::Tooltips::WidgetAnimEditor;

// =====================================================================================================
// Edit state
// =====================================================================================================

namespace WidgetAnimTextEditing
{
	/** What the text box held at a step of the edit, for undo and redo: its text and selection. */
	struct FSnapshot
	{
		FString Text;
		int32 Start = 0;
		int32 End = 0;
	};

	/**
	 * One widget's edit. Kept here, by the widget's name, rather than in the canvas widgets - which the designer throws away
	 * and builds again whenever it refreshes, a click on the toolbar included - so neither the selection the toolbar acts on
	 * nor the undo history is lost to a refresh.
	 */
	struct FState
	{
		bool bEditing = false;

		/** The selection the toolbar acts on, in characters: the box's, kept while the keyboard is elsewhere. */
		int32 SelectionStart = INDEX_NONE;
		int32 SelectionEnd = INDEX_NONE;

		TArray<FSnapshot> Undo;
		TArray<FSnapshot> Redo;

		/** When typing last changed the text: typing after a pause longer than a second is a new undo step. */
		double LastTypingTime = -1.0;
	};

	/** How many steps back the history keeps. */
	constexpr int32 MaxHistory = 200;

	static FState& GetState(FName WidgetName)
	{
		static TMap<FName, FState> States;
		return States.FindOrAdd(WidgetName);
	}

	bool IsEditing(FName WidgetName)
	{
		return !WidgetName.IsNone() && GetState(WidgetName).bEditing;
	}

	void SetEditing(FName WidgetName, bool bEditing)
	{
		if (WidgetName.IsNone())
		{
			return;
		}
		// Each edit starts afresh: no selection carried in, nothing to undo from the last one.
		FState& State = GetState(WidgetName);
		State = FState();
		State.bEditing = bEditing;
	}

	const TArray<FName>& GetEditableClassNames()
	{
		static const TArray<FName> Names =
		{
			TEXT("WidgetAnimTextBlock"),
			TEXT("WidgetAnimRichTextBlock"),
			TEXT("CommonAnimatedText"),
			TEXT("CommonAnimatedRichText"),
		};
		return Names;
	}

	/** Whether Class is one of the animated texts, or made from one. */
	static bool IsEditableClass(const UClass* Class)
	{
		for (; Class; Class = Class->GetSuperClass())
		{
			if (GetEditableClassNames().Contains(Class->GetFName()))
			{
				return true;
			}
		}
		return false;
	}
}

// =====================================================================================================
// Tags
// =====================================================================================================

namespace WidgetAnimTextEditor
{
	/** One tag in a text: where it is (End one past its '>'), its name, and what kind it is. */
	struct FTag
	{
		int32 Start = 0;
		int32 End = 0;
		FString Name;
		bool bClose = false;

		/** Stands alone - <pause=0.5>, <event=Hit>, <play=Pop>, or anything written <.../> - rather than opening a run of text. */
		bool bMoment = false;
	};

	/** An opening tag and the closing one that ends it: the text between them is what it tags. Close INDEX_NONE: to the end. */
	struct FPair
	{
		int32 Open = INDEX_NONE;
		int32 Close = INDEX_NONE;
	};

	bool IsMomentTag(const FString& Name)
	{
		return Name.Equals(TEXT("pause"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("event"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("play"), ESearchCase::IgnoreCase);
	}

	/** Every tag in Text, in order, as the text widgets read them: < up to the next >. */
	void FindTags(const FString& Text, TArray<FTag>& OutTags)
	{
		OutTags.Reset();
		for (int32 Index = 0; Index < Text.Len(); ++Index)
		{
			if (Text[Index] != TEXT('<'))
			{
				continue;
			}
			const int32 CloseAngle = Text.Find(TEXT(">"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Index + 1);
			if (CloseAngle == INDEX_NONE)
			{
				break;
			}

			FTag& Tag = OutTags.AddDefaulted_GetRef();
			Tag.Start = Index;
			Tag.End = CloseAngle + 1;
			FString Inside = Text.Mid(Index + 1, CloseAngle - Index - 1).TrimStartAndEnd();
			Tag.bClose = Inside.StartsWith(TEXT("/"));
			const bool bSelfClosing = !Tag.bClose && Inside.EndsWith(TEXT("/"));
			if (Tag.bClose)
			{
				Inside.RightChopInline(1);
			}
			int32 NameEnd = 0;
			while (NameEnd < Inside.Len() && !FChar::IsWhitespace(Inside[NameEnd]) && Inside[NameEnd] != TEXT('=') && Inside[NameEnd] != TEXT('/'))
			{
				++NameEnd;
			}
			Tag.Name = Inside.Left(NameEnd);
			Tag.bMoment = !Tag.bClose && (bSelfClosing || IsMomentTag(Tag.Name));
			Index = CloseAngle;
		}
	}

	/** Which tags open and close each run: </> closes the one opened last, </name> the last of that name. */
	void PairTags(const TArray<FTag>& Tags, TArray<FPair>& OutPairs)
	{
		OutPairs.Reset();
		TArray<int32> Open;
		for (int32 Index = 0; Index < Tags.Num(); ++Index)
		{
			const FTag& Tag = Tags[Index];
			if (Tag.bMoment)
			{
				continue;
			}
			if (!Tag.bClose)
			{
				Open.Add(Index);
				continue;
			}
			int32 Match = INDEX_NONE;
			for (int32 Stack = Open.Num() - 1; Stack >= 0; --Stack)
			{
				if (Tag.Name.IsEmpty() || Tags[Open[Stack]].Name.Equals(Tag.Name, ESearchCase::IgnoreCase))
				{
					Match = Stack;
					break;
				}
			}
			if (Match != INDEX_NONE)
			{
				OutPairs.Add({ Open[Match], Index });
				Open.RemoveAt(Match);
			}
		}
		for (const int32 Unclosed : Open)
		{
			OutPairs.Add({ Unclosed, INDEX_NONE });
		}
	}

	/** Where each line of Text starts, as the text box breaks it: at \r\n, \r or \n. */
	void GetLineStarts(const FString& Text, TArray<int32>& OutStarts)
	{
		OutStarts.Reset();
		OutStarts.Add(0);
		for (int32 Index = 0; Index < Text.Len(); ++Index)
		{
			if (Text[Index] == TEXT('\r'))
			{
				if (Index + 1 < Text.Len() && Text[Index + 1] == TEXT('\n'))
				{
					++Index;
				}
				OutStarts.Add(Index + 1);
			}
			else if (Text[Index] == TEXT('\n'))
			{
				OutStarts.Add(Index + 1);
			}
		}
	}

	int32 ToIndex(const FString& Text, const FTextLocation& Location)
	{
		TArray<int32> Starts;
		GetLineStarts(Text, Starts);
		const int32 Line = FMath::Clamp(Location.GetLineIndex(), 0, Starts.Num() - 1);
		return FMath::Clamp(Starts[Line] + Location.GetOffset(), 0, Text.Len());
	}

	FTextLocation ToLocation(const FString& Text, int32 Index)
	{
		TArray<int32> Starts;
		GetLineStarts(Text, Starts);
		int32 Line = 0;
		while (Line + 1 < Starts.Num() && Starts[Line + 1] <= Index)
		{
			++Line;
		}
		return FTextLocation(Line, Index - Starts[Line]);
	}

	/** A color as the color tag reads it: #RRGGBB. */
	FString ToHex(const FLinearColor& Color)
	{
		const FColor SRGB = Color.ToFColor(/*bSRGB=*/true);
		return FString::Printf(TEXT("#%02X%02X%02X"), SRGB.R, SRGB.G, SRGB.B);
	}
}

// =====================================================================================================
// The editor on the canvas
// =====================================================================================================

namespace WidgetAnimTextCanvasEditorImpl
{
	namespace Palette = NameTBD::Colors::ShapeEditor;

	/** How far above the widget the toolbar's bottom edge sits - as the Custom Shape's does. */
	constexpr float ToolbarLift = 6.f;

	const FSlateBrush* ToolbarBrush()
	{
		static const FSlateRoundedBoxBrush Brush(Palette::ToolbarBackground, 5.f, Palette::ToolbarOutline, 1.f);
		return &Brush;
	}

	const FSlateBrush* TextFrameBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor(0.f, 0.f, 0.f, 0.55f), 4.f, FLinearColor(1.f, 1.f, 1.f, 0.12f), 1.f);
		return &Brush;
	}

	const FSlateBrush* DividerBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor(1.f, 1.f, 1.f, 0.15f), 0.f);
		return &Brush;
	}

	FTextProperty* FindTextProperty(const UWidget* Widget)
	{
		return Widget ? FindFProperty<FTextProperty>(Widget->GetClass(), TEXT("Text")) : nullptr;
	}

	FText ReadText(const UWidget* Widget)
	{
		const FTextProperty* Property = FindTextProperty(Widget);
		return Property ? *Property->ContainerPtrToValuePtr<FText>(Widget) : FText::GetEmpty();
	}

	/** Straight into the field - a setter may do more than a template wants - then out to the widget's Slate, if it has one. */
	void WriteText(UWidget* Widget, const FText& Value)
	{
		const FTextProperty* Property = FindTextProperty(Widget);
		if (!Property)
		{
			return;
		}
		*Property->ContainerPtrToValuePtr<FText>(Widget) = Value;
		if (Widget->GetCachedWidget().IsValid())
		{
			Widget->SynchronizeProperties();
		}
	}

	/** NewString as a text with Old's localization key, so a text already gathered for translation stays the same text. */
	FText KeepKey(const FText& Old, const FString& NewString)
	{
		const TOptional<FString> Namespace = FTextInspector::GetNamespace(Old);
		const TOptional<FString> Key = FTextInspector::GetKey(Old);
		return Namespace.IsSet() && Key.IsSet() && !Old.IsCultureInvariant()
			? FText::ChangeKey(FTextKey(*Namespace.GetValue()), FTextKey(*Key.GetValue()), FText::FromString(NewString))
			: FText::FromString(NewString);
	}
}

/**
 * The toolbar above an animated text on the designer's canvas: Edit Text, or - while editing - a card with the text and the
 * tag buttons over it. See WidgetAnimTextCanvasEditor.h.
 */
class SWidgetAnimTextCanvasEditor : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SWidgetAnimTextCanvasEditor) {}
		SLATE_ARGUMENT(FWidgetReference, Widget)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		using namespace WidgetAnimTextCanvasEditorImpl;
		using WidgetShapeEditorUI::FChip;
		using WidgetShapeEditorUI::MakeChip;

		Reference = InArgs._Widget;
		if (const UWidget* Template = Reference.GetTemplate())
		{
			WidgetName = Template->GetFName();
			bRich = FindFProperty<FObjectPropertyBase>(Template->GetClass(), TEXT("TextStyleSet")) != nullptr;
		}

		const TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
		auto Add = [&Row](TSharedRef<SWidget> Item, float Gap = 2.f)
		{
			Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(Gap, 0.f, 0.f, 0.f))[Item];
		};
		auto Divider = []()
		{
			return SNew(SBox).WidthOverride(1.f).HeightOverride(16.f)[SNew(SBorder).BorderImage(DividerBrush())];
		};

		{
			FChip Chip;
			Chip.Icon = FNameTBDStyle::ShapeDoneIcon;
			Chip.Label = LOCTEXT("Done", "Done");
			Chip.ToolTip = WidgetAnimTips::TextEditorDoneTooltip();
			Chip.IsActive = []() { return true; };
			Chip.bPressOnly = true;
			Chip.OnClick = Do([](SWidgetAnimTextCanvasEditor& Editor) { Editor.FinishEditing(); });
			Add(MakeChip(Chip), 0.f);
		}

		// Look: the project's own styles and effects first, then the built-in look tags.
		Add(Divider(), 6.f);
		if (bRich)
		{
			Add(MakeMenuChip(LOCTEXT("StyleButton", "Style"), WidgetAnimTips::TextEditorStyleTooltip(), &SWidgetAnimTextCanvasEditor::MakeStyleMenu), 6.f);
		}
		Add(MakeMenuChip(LOCTEXT("EffectButton", "Effect"), WidgetAnimTips::TextEditorEffectTooltip(), &SWidgetAnimTextCanvasEditor::MakeEffectMenu), bRich ? 2.f : 6.f);
		Add(Divider(), 6.f);
		Add(MakeMenuChip(LOCTEXT("ColorButton", "Color"), WidgetAnimTips::TextEditorColorTooltip(), &SWidgetAnimTextCanvasEditor::MakeColorMenu), 6.f);
		Add(MakeMenuChip(LOCTEXT("SizeButton", "Size"), WidgetAnimTips::TextEditorSizeTooltip(), &SWidgetAnimTextCanvasEditor::MakeSizeMenu));
		Add(MakeActionChip(LOCTEXT("GlowButton", "Glow"), WidgetAnimTips::TextEditorGlowTooltip(), [](SWidgetAnimTextCanvasEditor& Editor) { Editor.Wrap(TEXT("<glow>"), TEXT("</>")); }));
		Add(Divider(), 6.f);
		Add(MakeActionChip(LOCTEXT("ClearButton", "Clear"), WidgetAnimTips::TextEditorClearTooltip(), [](SWidgetAnimTextCanvasEditor& Editor) { Editor.ClearTags(); }), 6.f);

		FChip EditChip;
		EditChip.Icon = FNameTBDStyle::ShapeEditIcon;
		EditChip.Label = LOCTEXT("EditText", "Edit Text");
		EditChip.ToolTip = WidgetAnimTips::TextEditorEditTooltip();
		EditChip.OnClick = Do([](SWidgetAnimTextCanvasEditor& Editor) { Editor.StartEditing(); });

		const TSharedRef<SScrollBar> ScrollBar = SNew(SScrollBar).Orientation(Orient_Vertical).AlwaysShowScrollbar(false);
		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 10);

		auto ShownWhileEditing = [this]() { return IsEditing() ? EVisibility::Visible : EVisibility::Collapsed; };

		ChildSlot
		[
			SNew(SBorder)
			.BorderImage(ToolbarBrush())
			.Padding(FMargin(4.f, 3.f))
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBox)
					.Visibility_Lambda([this]() { return IsEditing() ? EVisibility::Collapsed : EVisibility::Visible; })
					[
						MakeChip(EditChip)
					]
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBox)
					.Visibility_Lambda(ShownWhileEditing)
					[
						Row
					]
				]

				// The text, tags and all - no wider than the toolbar over it, so a long text wraps rather than widening the card.
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.f, 4.f, 0.f, 0.f))
				[
					SNew(SBox)
					.Visibility_Lambda(ShownWhileEditing)
					.MaxDesiredWidth_Lambda([Row]() { return FOptionalSize(FMath::Max(360.f, static_cast<float>(Row->GetDesiredSize().X))); })
					[
						SNew(SBorder)
						.BorderImage(TextFrameBrush())
						.ForegroundColor(FSlateColor(Palette::ToolText))
						.Padding(FMargin(6.f, 4.f))
						[
							SNew(SBox)
							.MinDesiredHeight(64.f)
							.MaxDesiredHeight(220.f)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot()
								.FillWidth(1.f)
								[
									SAssignNew(Box, SMultiLineEditableText)
									.Font(Font)
									.AutoWrapText(true)
									.SelectAllTextWhenFocused(false)
									.ClearTextSelectionOnFocusLoss(false)
									.VScrollBar(ScrollBar)
									.ToolTipText(WidgetAnimTips::TextEditorBoxTooltip())
									.OnTextChanged_Lambda([this](const FText& NewText) { OnTyped(NewText.ToString()); })
									.OnTextCommitted_Lambda([this](const FText& NewText, ETextCommit::Type) { Commit(NewText.ToString(), INDEX_NONE, INDEX_NONE, /*bRecord=*/false); })
								]
								+ SHorizontalBox::Slot()
								.AutoWidth()
								[
									ScrollBar
								]
							]
						]
					]
				]

				// Where the cursor is: the tags around it, innermost last - as a word processor shows the styles in force.
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(2.f, 3.f, 0.f, 1.f))
				[
					SNew(STextBlock)
					.Visibility_Lambda(ShownWhileEditing)
					.Font(FCoreStyle::GetDefaultFontStyle("Italic", 9))
					.ColorAndOpacity(FSlateColor(Palette::ToolText.CopyWithNewOpacity(0.6f)))
					.Text_Lambda([this]() { return GetCursorTagsText(); })
				]
			]
		];

		SetBoxText(ReadTemplate());
		bFocusBox = IsEditing();
	}

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
	{
		SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
		if (!Box.IsValid())
		{
			return;
		}

		// The text changed elsewhere - the Details panel, an undo - is taken in, unless it is being typed in here.
		if (!Box->HasKeyboardFocus())
		{
			// Compared with every line break alike: the box may give its lines back joined differently from how they went in.
			const FString Template = ReadTemplate();
			if (Template.Replace(TEXT("\r\n"), TEXT("\n")) != Box->GetText().ToString().Replace(TEXT("\r\n"), TEXT("\n")))
			{
				SetBoxText(Template);
			}
		}
		else
		{
			// The selection, as it is now, for the toolbar - which may be clicked after the keyboard has gone elsewhere, or
			// after the designer has built this editor again.
			int32 Start = 0;
			int32 End = 0;
			GetSelectionRange(Start, End);
			State().SelectionStart = Start;
			State().SelectionEnd = End;
		}

		// Into the box as editing starts - or as this editor is built again in the middle of one - once it is shown and can
		// take the keyboard, with the selection it had.
		if (bFocusBox && IsEditing())
		{
			bFocusBox = false;
			FSlateApplication::Get().SetKeyboardFocus(Box, EFocusCause::SetDirectly);
			const WidgetAnimTextEditing::FState& Edit = State();
			if (Edit.SelectionStart != INDEX_NONE)
			{
				const FString Text = GetCurrentText();
				const int32 Start = FMath::Clamp(Edit.SelectionStart, 0, Text.Len());
				const int32 End = FMath::Clamp(Edit.SelectionEnd, Start, Text.Len());
				Box->SelectText(WidgetAnimTextEditor::ToLocation(Text, Start), WidgetAnimTextEditor::ToLocation(Text, End));
			}
		}
	}

	virtual FReply OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override
	{
		// Ctrl / Cmd + Z, and Ctrl / Cmd + Y or Ctrl / Cmd + Shift + Z: this edit's own undo and redo - typing and tags alike -
		// ahead of the box's, which knows nothing of the tags the toolbar wrote.
		const FKey Key = InKeyEvent.GetKey();
		if (IsEditing() && (Key == EKeys::Z || Key == EKeys::Y) && (InKeyEvent.IsControlDown() || InKeyEvent.IsCommandDown()) && !InKeyEvent.IsAltDown())
		{
			StepHistory(/*bRedo=*/Key == EKeys::Y || InKeyEvent.IsShiftDown());
			return FReply::Handled();
		}
		return SCompoundWidget::OnPreviewKeyDown(MyGeometry, InKeyEvent);
	}

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override
	{
		// What the box leaves: Escape with nothing selected finishes, as it does a Custom Shape's edit.
		if (IsEditing() && InKeyEvent.GetKey() == EKeys::Escape)
		{
			FinishEditing();
			return FReply::Handled();
		}
		return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
	}

private:

	FWidgetReference Reference;
	FName WidgetName;
	TSharedPtr<SMultiLineEditableText> Box;
	bool bRich = false;
	bool bFocusBox = false;

	/** What the box showed before the latest change typed into it - the step undo goes back to. */
	FString ShownText;

	/** Set while the box is given text from here, which is not typing. */
	bool bSettingText = false;

	UWidget* GetTemplate() const { return Reference.GetTemplate(); }
	UWidget* GetPreview() const { return Reference.GetPreview(); }
	bool IsEditing() const { return WidgetAnimTextEditing::IsEditing(WidgetName); }
	WidgetAnimTextEditing::FState& State() const { return WidgetAnimTextEditing::GetState(WidgetName); }

	void SetBoxText(const FString& Text)
	{
		ShownText = Text;
		if (Box.IsValid())
		{
			TGuardValue<bool> Guard(bSettingText, true);
			Box->SetText(FText::FromString(Text));
		}
	}

	/**
	 * The range the toolbar acts on: the box's selection while it has the keyboard, else the one it last had - which a
	 * chip, a menu or the color picker taking the keyboard, or the designer building this editor again, doesn't lose. With
	 * none, the end of the text.
	 */
	void GetActionRange(int32& OutStart, int32& OutEnd) const
	{
		const FString Text = GetCurrentText();
		if (Box.IsValid() && Box->HasKeyboardFocus())
		{
			GetSelectionRange(OutStart, OutEnd);
			return;
		}
		const WidgetAnimTextEditing::FState& Edit = State();
		if (Edit.SelectionStart == INDEX_NONE)
		{
			OutStart = OutEnd = Text.Len();
			return;
		}
		OutStart = FMath::Clamp(Edit.SelectionStart, 0, Text.Len());
		OutEnd = FMath::Clamp(Edit.SelectionEnd, OutStart, Text.Len());
	}

	void PushHistory(TArray<WidgetAnimTextEditing::FSnapshot>& History, const FString& Text, int32 Start, int32 End)
	{
		if (History.Num() >= WidgetAnimTextEditing::MaxHistory)
		{
			History.RemoveAt(0);
		}
		History.Add({ Text, Start, End });
	}

	/** Typing: the preview follows at once, and a run of it - until a pause, or a tag - is one undo step. */
	void OnTyped(const FString& NewString)
	{
		if (!bSettingText && NewString != ShownText)
		{
			WidgetAnimTextEditing::FState& Edit = State();
			const double Now = FPlatformTime::Seconds();
			if (Edit.LastTypingTime < 0.0 || Now - Edit.LastTypingTime > 1.0)
			{
				const int32 Start = Edit.SelectionStart != INDEX_NONE ? FMath::Clamp(Edit.SelectionStart, 0, ShownText.Len()) : ShownText.Len();
				const int32 End = Edit.SelectionStart != INDEX_NONE ? FMath::Clamp(Edit.SelectionEnd, Start, ShownText.Len()) : Start;
				PushHistory(Edit.Undo, ShownText, Start, End);
				Edit.Redo.Reset();
			}
			Edit.LastTypingTime = Now;
		}
		ShownText = NewString;
		PreviewText(NewString);
	}

	/** One step back - or forward again - through this edit: the text and the selection it had. */
	void StepHistory(bool bRedo)
	{
		WidgetAnimTextEditing::FState& Edit = State();
		TArray<WidgetAnimTextEditing::FSnapshot>& From = bRedo ? Edit.Redo : Edit.Undo;
		TArray<WidgetAnimTextEditing::FSnapshot>& To = bRedo ? Edit.Undo : Edit.Redo;
		if (From.Num() == 0)
		{
			return;
		}
		const WidgetAnimTextEditing::FSnapshot Step = From.Pop();
		int32 Start = 0;
		int32 End = 0;
		GetActionRange(Start, End);
		PushHistory(To, GetCurrentText(), Start, End);
		Edit.LastTypingTime = -1.0;
		Commit(Step.Text, Step.Start, Step.End, /*bRecord=*/false);
	}

	FString ReadTemplate() const
	{
		return WidgetAnimTextCanvasEditorImpl::ReadText(GetTemplate()).ToString();
	}

	/** The text as typed so far, which the toolbar works on - not yet committed, it is still the newest. */
	FString GetCurrentText() const
	{
		return Box.IsValid() ? Box->GetText().ToString() : ReadTemplate();
	}

	void StartEditing()
	{
		WidgetAnimTextEditing::SetEditing(WidgetName, true);
		bFocusBox = true;
	}

	void FinishEditing()
	{
		if (Box.IsValid())
		{
			Commit(Box->GetText().ToString(), INDEX_NONE, INDEX_NONE, /*bRecord=*/false);
		}
		WidgetAnimTextEditing::SetEditing(WidgetName, false);
		FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::Cleared);
	}

	/** What is being typed, shown on the canvas at once: on the preview only - the Widget Blueprint's widget gets it on commit. */
	void PreviewText(const FString& NewString)
	{
		UWidget* Preview = GetPreview();
		if (Preview && Preview != GetTemplate() && WidgetAnimTextCanvasEditorImpl::ReadText(Preview).ToString() != NewString)
		{
			WidgetAnimTextCanvasEditorImpl::WriteText(Preview, FText::FromString(NewString));
		}
	}

	/**
	 * The new text onto the Widget Blueprint's widget, as one undo step keeping its localization key, and onto the designer's
	 * preview - written straight in, as the Custom Shape's edits are, so the designer doesn't rebuild under the box - then into
	 * the box, with SelectFrom - SelectTo selected.
	 */
	void Commit(const FString& NewString, int32 SelectFrom = INDEX_NONE, int32 SelectTo = INDEX_NONE, bool bRecord = true)
	{
		using namespace WidgetAnimTextCanvasEditorImpl;

		UWidget* Template = GetTemplate();
		if (!Template || !FindTextProperty(Template))
		{
			return;
		}

		// A toolbar change is a step of its own in the edit's history, ending any run of typing before it.
		const FString Before = GetCurrentText();
		if (bRecord && Before != NewString)
		{
			int32 Start = 0;
			int32 End = 0;
			GetActionRange(Start, End);
			WidgetAnimTextEditing::FState& Edit = State();
			PushHistory(Edit.Undo, Before, Start, End);
			Edit.Redo.Reset();
			Edit.LastTypingTime = -1.0;
		}

		const FText Old = ReadText(Template);
		if (Old.ToString() != NewString)
		{
			const FText NewText = KeepKey(Old, NewString);
			{
				const FScopedTransaction Transaction(LOCTEXT("EditTextTransaction", "Edit Text"));
				Template->Modify();
				WriteText(Template, NewText);
			}
			Template->MarkPackageDirty();
			if (UWidgetBlueprint* Blueprint = Template->GetTypedOuter<UWidgetBlueprint>())
			{
				Blueprint->Status = BS_Dirty;
			}
			UWidget* Preview = GetPreview();
			if (Preview && Preview != Template)
			{
				WriteText(Preview, NewText);
			}
		}

		if (Box.IsValid())
		{
			if (Box->GetText().ToString() != NewString)
			{
				SetBoxText(NewString);
			}
			if (SelectFrom != INDEX_NONE)
			{
				// Back into the box - a menu or the picker took the keyboard - with the same words selected, ready for the next tag.
				State().SelectionStart = SelectFrom;
				State().SelectionEnd = SelectTo;
				FSlateApplication::Get().SetKeyboardFocus(Box, EFocusCause::SetDirectly);
				Box->SelectText(WidgetAnimTextEditor::ToLocation(NewString, SelectFrom), WidgetAnimTextEditor::ToLocation(NewString, SelectTo));
			}
		}
	}

	/** The selection, as character indices into the current text: Start == End is the cursor alone. */
	void GetSelectionRange(int32& OutStart, int32& OutEnd) const
	{
		const FString Text = GetCurrentText();
		if (!Box.IsValid())
		{
			OutStart = OutEnd = Text.Len();
			return;
		}
		// The cursor is one end of the selection; the selected text says how far the other is, and on which side.
		const int32 CursorIndex = WidgetAnimTextEditor::ToIndex(Text, Box->GetCursorLocation());
		const FString Selected = Box->GetSelectedText().ToString();
		const int32 Length = Selected.Len();
		OutStart = OutEnd = CursorIndex;
		if (Length == 0)
		{
			return;
		}
		const bool bBefore = CursorIndex - Length >= 0 && Text.Mid(CursorIndex - Length, Length) == Selected;
		const bool bAfter = CursorIndex + Length <= Text.Len() && Text.Mid(CursorIndex, Length) == Selected;
		if (bBefore)
		{
			OutStart = CursorIndex - Length;
		}
		else if (bAfter)
		{
			OutEnd = CursorIndex + Length;
		}
	}


	/** Open and Close around the selection, the selection kept on the same words - or an empty pair at the cursor, the cursor inside. */
	void Wrap(const FString& Open, const FString& Close)
	{
		int32 Start = 0;
		int32 End = 0;
		GetActionRange(Start, End);
		const FString Text = GetCurrentText();
		const FString New = Text.Left(Start) + Open + Text.Mid(Start, End - Start) + Close + Text.Mid(End);
		Commit(New, Start + Open.Len(), End + Open.Len());
	}

	/**
	 * The tags off the selection: every run whose tagged words it touches loses its opening and closing tags, and every tag
	 * for a moment inside it goes. With nothing selected, the runs around the cursor.
	 */
	void ClearTags()
	{
		using namespace WidgetAnimTextEditor;

		int32 Start = 0;
		int32 End = 0;
		GetActionRange(Start, End);
		const FString Text = GetCurrentText();

		TArray<FTag> Tags;
		FindTags(Text, Tags);
		TArray<FPair> Pairs;
		PairTags(Tags, Pairs);

		TArray<int32> Remove;
		for (const FPair& Pair : Pairs)
		{
			const int32 ContentStart = Tags[Pair.Open].End;
			const int32 ContentEnd = Pair.Close != INDEX_NONE ? Tags[Pair.Close].Start : Text.Len();
			const bool bTouches = Start == End
				? Start >= Tags[Pair.Open].Start && Start <= (Pair.Close != INDEX_NONE ? Tags[Pair.Close].End : Text.Len())
				: ContentStart < End && ContentEnd > Start;
			if (bTouches)
			{
				Remove.Add(Pair.Open);
				if (Pair.Close != INDEX_NONE)
				{
					Remove.Add(Pair.Close);
				}
			}
		}
		for (int32 Index = 0; Index < Tags.Num(); ++Index)
		{
			if (Tags[Index].bMoment && Tags[Index].Start >= Start && Tags[Index].End <= FMath::Max(End, Start))
			{
				Remove.Add(Index);
			}
		}
		if (Remove.Num() == 0)
		{
			return;
		}

		// Last first, so the places of the rest stay put; the selection follows the text it was on.
		Remove.Sort([](int32 A, int32 B) { return A > B; });
		FString New = Text;
		int32 NewStart = Start;
		int32 NewEnd = End;
		for (const int32 Index : Remove)
		{
			const FTag& Removed = Tags[Index];
			const int32 Length = Removed.End - Removed.Start;
			New.RemoveAt(Removed.Start, Length);
			NewStart -= Removed.End <= Start ? Length : 0;
			NewEnd -= Removed.End <= End ? Length : 0;
		}
		Commit(New, FMath::Max(NewStart, 0), FMath::Max(NewEnd, 0));
	}

	/** "In: Bold › shake" - the runs the cursor is in, outermost first. Empty outside any. */
	FText GetCursorTagsText() const
	{
		using namespace WidgetAnimTextEditor;

		int32 Start = 0;
		int32 End = 0;
		GetActionRange(Start, End);
		const FString Text = GetCurrentText();
		TArray<FTag> Tags;
		FindTags(Text, Tags);
		TArray<FPair> Pairs;
		PairTags(Tags, Pairs);
		Pairs.Sort([&Tags](const FPair& A, const FPair& B) { return Tags[A.Open].Start < Tags[B.Open].Start; });

		TArray<FString> Names;
		for (const FPair& Pair : Pairs)
		{
			const int32 ContentStart = Tags[Pair.Open].End;
			const int32 ContentEnd = Pair.Close != INDEX_NONE ? Tags[Pair.Close].Start : Text.Len();
			if (Start >= ContentStart && Start <= ContentEnd)
			{
				Names.Add(Text.Mid(Tags[Pair.Open].Start + 1, Tags[Pair.Open].End - Tags[Pair.Open].Start - 2));
			}
		}
		return Names.Num() > 0
			? FText::Format(LOCTEXT("CursorTags", "In: {0}"), FText::FromString(FString::Join(Names, TEXT("  ›  "))))
			: WidgetAnimTips::TextEditorHint();
	}

	// ---------------------------------------------------------------------------------------------
	// Toolbar
	// ---------------------------------------------------------------------------------------------

	/** An action on this editor, reached through a weak pointer: a chip outlives nothing, but a menu or picker might. */
	TFunction<void()> Do(TFunction<void(SWidgetAnimTextCanvasEditor&)> Action)
	{
		const TWeakPtr<SWidgetAnimTextCanvasEditor> WeakEditor = SharedThis(this);
		return [WeakEditor, Action]()
		{
			if (const TSharedPtr<SWidgetAnimTextCanvasEditor> Pinned = WeakEditor.Pin())
			{
				Action(*Pinned);
			}
		};
	}

	TSharedRef<SWidget> MakeActionChip(const FText& Label, const FText& Tip, TFunction<void(SWidgetAnimTextCanvasEditor&)> Action)
	{
		WidgetShapeEditorUI::FChip Chip;
		Chip.Label = Label;
		Chip.ToolTip = Tip;
		Chip.bPressOnly = true;
		Chip.OnClick = Do(MoveTemp(Action));
		return WidgetShapeEditorUI::MakeChip(Chip);
	}

	/** A chip that drops a menu down under itself, lit while the menu is open. */
	TSharedRef<SWidget> MakeMenuChip(const FText& Label, const FText& Tip, TSharedRef<SWidget>(SWidgetAnimTextCanvasEditor::* MakeMenu)())
	{
		const TSharedRef<TWeakPtr<SMenuAnchor>> Anchor = MakeShared<TWeakPtr<SMenuAnchor>>();

		WidgetShapeEditorUI::FChip Chip;
		Chip.Label = Label;
		Chip.ToolTip = Tip;
		Chip.bPressOnly = true;
		Chip.IsActive = [Anchor]() { const TSharedPtr<SMenuAnchor> Menu = Anchor->Pin(); return Menu.IsValid() && Menu->IsOpen(); };
		Chip.OnClick = [Anchor]()
		{
			if (const TSharedPtr<SMenuAnchor> Menu = Anchor->Pin())
			{
				Menu->SetIsOpen(!Menu->IsOpen());
			}
		};

		const TSharedRef<SMenuAnchor> MenuAnchor = SNew(SMenuAnchor)
			.Placement(MenuPlacement_BelowAnchor)
			.OnGetMenuContent(FOnGetContent::CreateSP(this, MakeMenu))
			[
				WidgetShapeEditorUI::MakeChip(Chip)
			];
		*Anchor = MenuAnchor;
		return MenuAnchor;
	}

	/** A menu entry that wraps the selection in Open ... Close. */
	void AddWrapEntry(FMenuBuilder& Menu, const FText& Label, const FText& Tip, const FString& Open, const FString& Close)
	{
		Menu.AddMenuEntry(Label, Tip, FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this, Open, Close]() { Wrap(Open, Close); })));
	}

	/** A value typed into a menu, then put in by Apply on Enter: a color's hex code or name. */
	void AddNameEntry(FMenuBuilder& Menu, const FText& Hint, TFunction<void(const FString&)> Apply)
	{
		Menu.AddWidget(
			SNew(SBox)
			.WidthOverride(200.f)
			.Padding(FMargin(8.f, 2.f))
			[
				SNew(SEditableTextBox)
				.HintText(Hint)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.OnTextCommitted_Lambda([Apply](const FText& Text, ETextCommit::Type Type)
				{
					const FString Name = Text.ToString().TrimStartAndEnd();
					if (Type == ETextCommit::OnEnter && !Name.IsEmpty())
					{
						Apply(Name.Replace(TEXT(" "), TEXT("")));
						FSlateApplication::Get().DismissAllMenus();
					}
				})
			],
			FText::GetEmpty(), /*bNoIndent=*/true);
	}

	void AddNote(FMenuBuilder& Menu, const FText& Note)
	{
		Menu.AddWidget(
			SNew(SBox)
			.MaxDesiredWidth(280.f)
			.Padding(FMargin(8.f, 4.f))
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Font(FCoreStyle::GetDefaultFontStyle("Italic", 9))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text(Note)
			],
			FText::GetEmpty(), /*bNoIndent=*/true);
	}

	/** The rows of the widget's Text Style Set - its own rich text styles - but its Default, which is all untagged text. */
	TSharedRef<SWidget> MakeStyleMenu()
	{
		FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);
		Menu.BeginSection("Styles", LOCTEXT("StylesHeading", "Text Style Set"));

		const UObject* Widget = GetTemplate();
		const FObjectPropertyBase* StyleSetProperty = Widget ? FindFProperty<FObjectPropertyBase>(Widget->GetClass(), TEXT("TextStyleSet")) : nullptr;
		const UDataTable* StyleSet = StyleSetProperty ? Cast<UDataTable>(StyleSetProperty->GetObjectPropertyValue_InContainer(Widget)) : nullptr;
		int32 NumRows = 0;
		if (StyleSet)
		{
			for (const FName& Row : StyleSet->GetRowNames())
			{
				if (Row.ToString().Equals(TEXT("Default"), ESearchCase::IgnoreCase))
				{
					continue;
				}
				const FString Name = Row.ToString();
				AddWrapEntry(Menu, FText::FromName(Row), FText::Format(WidgetAnimTips::TextEditorStyleRowTooltip(), FText::FromString(Name)), FString::Printf(TEXT("<%s>"), *Name), TEXT("</>"));
				++NumRows;
			}
		}
		if (NumRows == 0)
		{
			AddNote(Menu, StyleSet ? WidgetAnimTips::TextEditorNoStyleRowsNote() : WidgetAnimTips::TextEditorNoStyleSetNote());
		}
		Menu.EndSection();
		return Menu.MakeWidget();
	}

	/** The project's Text Tags, by name. */
	TSharedRef<SWidget> MakeEffectMenu()
	{
		FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);
		Menu.BeginSection("Effects", LOCTEXT("EffectsHeading", "Text Tags"));
		const TArray<FWidgetAnimTextTag>& Tags = UWidgetAnimRuntimeSettings::Get().TextTags;
		for (const FWidgetAnimTextTag& TextTag : Tags)
		{
			if (TextTag.Tag.IsNone())
			{
				continue;
			}
			const FString Name = TextTag.Tag.ToString();
			AddWrapEntry(Menu, FText::FromName(TextTag.Tag), FText::Format(WidgetAnimTips::TextEditorEffectRowTooltip(), FText::FromString(Name)), FString::Printf(TEXT("<%s>"), *Name), TEXT("</>"));
		}
		if (Tags.Num() == 0)
		{
			AddNote(Menu, WidgetAnimTips::TextEditorNoEffectsNote());
		}
		Menu.EndSection();
		return Menu.MakeWidget();
	}

	/** A palette of named colors, the editor's color picker for any other, and a hex code typed in. */
	TSharedRef<SWidget> MakeColorMenu()
	{
		FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);
		Menu.BeginSection("Colors", LOCTEXT("ColorsHeading", "Color"));

		struct FNamedColor { const TCHAR* Name; FLinearColor Color; };
		static const FNamedColor Named[] =
		{
			{ TEXT("Red"),    FLinearColor(FColor(0xE5, 0x3E, 0x3E)) },
			{ TEXT("Orange"), FLinearColor(FColor(0xF2, 0x8C, 0x28)) },
			{ TEXT("Yellow"), FLinearColor(FColor(0xF5, 0xD0, 0x3B)) },
			{ TEXT("Green"),  FLinearColor(FColor(0x3F, 0xC1, 0x5A)) },
			{ TEXT("Cyan"),   FLinearColor(FColor(0x3B, 0xC9, 0xDB)) },
			{ TEXT("Blue"),   FLinearColor(FColor(0x3D, 0x7E, 0xF0)) },
			{ TEXT("Purple"), FLinearColor(FColor(0x9B, 0x5D, 0xE5)) },
			{ TEXT("Pink"),   FLinearColor(FColor(0xF0, 0x6E, 0xB4)) },
			{ TEXT("White"),  FLinearColor::White },
			{ TEXT("Gray"),   FLinearColor(FColor(0x9A, 0x9A, 0x9A)) },
		};
		for (const FNamedColor& Entry : Named)
		{
			const FString Open = FString::Printf(TEXT("<color=%s>"), *WidgetAnimTextEditor::ToHex(Entry.Color));
			const FLinearColor Swatch = Entry.Color;
			Menu.AddMenuEntry(
				FUIAction(FExecuteAction::CreateLambda([this, Open]() { Wrap(Open, TEXT("</>")); })),
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(0.f, 0.f, 6.f, 0.f))
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
					.BorderBackgroundColor(Swatch)
					[
						SNew(SBox).WidthOverride(12.f).HeightOverride(12.f)
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(FText::FromString(Entry.Name))
				]);
		}
		Menu.EndSection();

		Menu.BeginSection("OtherColor", LOCTEXT("OtherColorHeading", "Other"));
		{
			// The selection is read now: the picker takes the keyboard, and with it what the box would say is selected.
			int32 Start = 0;
			int32 End = 0;
			GetActionRange(Start, End);
			const TWeakPtr<SWidgetAnimTextCanvasEditor> WeakEditor = SharedThis(this);
			Menu.AddMenuEntry(
				LOCTEXT("PickColor", "Pick a Color..."),
				WidgetAnimTips::TextEditorPickColorTooltip(),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([WeakEditor, Start, End]()
				{
					const TSharedRef<FLinearColor> Picked = MakeShared<FLinearColor>(FLinearColor::White);
					const TSharedRef<bool> bCancelled = MakeShared<bool>(false);
					FColorPickerArgs Args;
					Args.bUseAlpha = false;
					Args.bIsModal = false;
					Args.InitialColor = FLinearColor::White;
					Args.OnColorCommitted = FOnLinearColorValueChanged::CreateLambda([Picked](FLinearColor Color) { *Picked = Color; });
					Args.OnColorPickerCancelled = FOnColorPickerCancelled::CreateLambda([bCancelled](FLinearColor) { *bCancelled = true; });
					Args.OnColorPickerWindowClosed = FOnWindowClosed::CreateLambda([WeakEditor, Picked, bCancelled, Start, End](const TSharedRef<SWindow>&)
					{
						const TSharedPtr<SWidgetAnimTextCanvasEditor> Pinned = WeakEditor.Pin();
						if (Pinned && !*bCancelled)
						{
							Pinned->WrapRange(Start, End, FString::Printf(TEXT("<color=%s>"), *WidgetAnimTextEditor::ToHex(*Picked)), TEXT("</>"));
						}
					});
					OpenColorPicker(Args);
				})));
			AddNameEntry(Menu, LOCTEXT("HexHint", "#RRGGBB or a color name"), [WeakEditor, Start, End](const FString& Value)
			{
				if (const TSharedPtr<SWidgetAnimTextCanvasEditor> Pinned = WeakEditor.Pin())
				{
					Pinned->WrapRange(Start, End, FString::Printf(TEXT("<color=%s>"), *Value), TEXT("</>"));
				}
			});
		}
		Menu.EndSection();
		return Menu.MakeWidget();
	}

	/** Wrap, on a range read before the selection could be lost. */
	void WrapRange(int32 Start, int32 End, const FString& Open, const FString& Close)
	{
		const FString Text = GetCurrentText();
		Start = FMath::Clamp(Start, 0, Text.Len());
		End = FMath::Clamp(End, Start, Text.Len());
		const FString New = Text.Left(Start) + Open + Text.Mid(Start, End - Start) + Close + Text.Mid(End);
		Commit(New, Start + Open.Len(), End + Open.Len());
	}

	TSharedRef<SWidget> MakeSizeMenu()
	{
		FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);
		Menu.BeginSection("Sizes", LOCTEXT("SizesHeading", "Size"));
		for (const TCHAR* Size : { TEXT("0.75"), TEXT("1.25"), TEXT("1.5"), TEXT("2") })
		{
			AddWrapEntry(Menu, FText::Format(LOCTEXT("SizeEntry", "{0}×"), FText::FromString(Size)), WidgetAnimTips::TextEditorSizeTooltip(), FString::Printf(TEXT("<size=%s>"), Size), TEXT("</>"));
		}
		Menu.EndSection();
		return Menu.MakeWidget();
	}
};

// =====================================================================================================
// Extension
// =====================================================================================================

FWidgetAnimTextEditorExtension::FWidgetAnimTextEditorExtension()
{
	ExtensionId = FName(TEXT("WidgetAnimTextEditor"));
}

bool FWidgetAnimTextEditorExtension::CanExtendSelection(const TArray<FWidgetReference>& Selection) const
{
	const UWidget* Template = Selection.Num() == 1 ? Selection[0].GetTemplate() : nullptr;
	return Template && WidgetAnimTextEditing::IsEditableClass(Template->GetClass()) && WidgetAnimTextCanvasEditorImpl::FindTextProperty(Template);
}

void FWidgetAnimTextEditorExtension::ExtendSelection(const TArray<FWidgetReference>& Selection, TArray<TSharedRef<FDesignerSurfaceElement>>& SurfaceElements)
{
	if (Selection.Num() != 1)
	{
		return;
	}

	// Centered above the text, as the Custom Shape's toolbar is: its bottom middle (alignment 0.5, 1) a few units over the
	// widget's top middle - so the card grows upward, off the text it edits.
	SurfaceElements.Add(MakeShared<FDesignerSurfaceElement>(
		SNew(SWidgetAnimTextCanvasEditor).Widget(Selection[0]),
		EExtensionLayoutLocation::TopCenter,
		FVector2D(0.0, -WidgetAnimTextCanvasEditorImpl::ToolbarLift),
		FVector2D(0.5, 1.0)));
}

TSharedRef<FDesignerExtension> FWidgetAnimTextEditorFactory::CreateDesignerExtension() const
{
	return MakeShared<FWidgetAnimTextEditorExtension>();
}

#undef LOCTEXT_NAMESPACE
