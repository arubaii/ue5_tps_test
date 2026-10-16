// Copyright AR GameSystems 2026. All rights reserved.

#include "ButtonPreviewToolbar.h"
#include "WidgetAnimBuilderToolbar.h"

#include "NameTBD/Buttons/ContextMenuButton.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Styling/AppStyle.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Toolkits/AssetEditorToolkitMenuContext.h"
#include "ToolMenus.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintEditor.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ButtonPreviewToolbar"

namespace ButtonPreviewToolbarPrivate
{
	const FName WidgetBlueprintEditorName(TEXT("WidgetBlueprintEditor"));

	const FSlateBrush* PickBrush(std::initializer_list<FName> Candidates)
	{
		const ISlateStyle& Style = FAppStyle::Get();
		for (const FName Candidate : Candidates)
		{
			if (const FSlateBrush* Brush = Style.GetOptionalBrush(Candidate, nullptr, nullptr))
			{
				return Brush;
			}
		}
		return nullptr;
	}

	bool IsWidgetBlueprintEditor(const FAssetEditorToolkit& Toolkit)
	{
		const TArray<UObject*>* EditedObjects = Toolkit.GetObjectsCurrentlyBeingEdited();
		if (!EditedObjects || Toolkit.GetToolkitFName() != WidgetBlueprintEditorName)
		{
			return false;
		}
		return EditedObjects->ContainsByPredicate([](const UObject* Edited) { return Edited && Edited->IsA<UWidgetBlueprint>(); });
	}

	/** What the toolbar did to one button (by name): kept to show it again on a rebuilt preview, and to replay. */
	struct FPreviewChoice
	{
		bool bHasState = false;
		EContextMenuDesignPreview State = EContextMenuDesignPreview::Normal;

		bool bHasValue = false;
		int32 ValueIndex = 0;

		/** The last change, for Replay: a state change from ReplayFromState, or a value step of ReplayStep from ReplayFromValue. */
		bool bLastWasValue = false;
		EContextMenuDesignPreview ReplayFromState = EContextMenuDesignPreview::Normal;
		int32 ReplayFromValue = 0;
		int32 ReplayStep = 0;
		bool bCanReplay = false;
	};

	/** The Context Menu Button selected in one Widget Blueprint editor's designer, and what the toolbar has done to buttons there. */
	class FSelectedButtonPreview
	{
	public:

		explicit FSelectedButtonPreview(TWeakPtr<FWidgetBlueprintEditor> InEditor)
			: Editor(InEditor)
		{
		}

		/** The selected button's designer copy, or null (nothing, several things, or something else selected). */
		UContextMenuButton* GetButton() const
		{
			Refresh();
			return SelectedButton.Get();
		}

		FText GetLabel() const
		{
			const UContextMenuButton* Button = GetButton();
			if (!Button)
			{
				return FText::GetEmpty();
			}
			const FString& Label = Button->GetDisplayLabel();
			return FText::FromString(Label.IsEmpty() ? Button->GetName() : Label);
		}

		EContextMenuDesignPreview GetState() const
		{
			const UContextMenuButton* Button = GetButton();
			return Button ? Button->GetEffectiveDesignPreview() : EContextMenuDesignPreview::Normal;
		}

		bool CanBeEnabled() const
		{
			const UContextMenuButton* Button = GetButton();
			return Button && Button->bButtonCanBeEnabled;
		}

		void PickState(EContextMenuDesignPreview State)
		{
			UContextMenuButton* Button = GetButton();
			if (!Button)
			{
				return;
			}
			const EContextMenuDesignPreview From = Button->GetEffectiveDesignPreview();
			Button->PlayDesignerPreviewState(State, /*bAnimate=*/true);

			FPreviewChoice& Choice = Choices.FindOrAdd(Button->GetFName());
			Choice.bHasState = true;
			Choice.State = State;
			if (From != State)
			{
				Choice.bLastWasValue = false;
				Choice.ReplayFromState = From;
				Choice.bCanReplay = true;
			}
		}

		bool HasValues() const
		{
			const UContextMenuButton* Button = GetButton();
			return Button && Button->GetDesignerPreviewValueCount() > 0;
		}

		FText GetValueText() const
		{
			const UContextMenuButton* Button = GetButton();
			return Button ? Button->GetDesignerPreviewValueText() : FText::GetEmpty();
		}

		void StepValue(int32 Step)
		{
			UContextMenuButton* Button = GetButton();
			if (!Button || Button->GetDesignerPreviewValueCount() <= 0)
			{
				return;
			}
			const int32 From = Button->GetDesignerPreviewValueIndex();
			Button->SetDesignerPreviewValueIndex(From + Step, /*bAnimate=*/true);

			FPreviewChoice& Choice = Choices.FindOrAdd(Button->GetFName());
			Choice.bHasValue = true;
			Choice.ValueIndex = Button->GetDesignerPreviewValueIndex();
			Choice.bLastWasValue = true;
			Choice.ReplayFromValue = From;
			Choice.ReplayStep = Step;
			Choice.bCanReplay = true;
		}

		bool HasAnimations() const
		{
			const UContextMenuButton* Button = GetButton();
			return Button && Button->HasButtonAnimations();
		}

		void PlayAnimation(bool bOut)
		{
			if (UContextMenuButton* Button = GetButton())
			{
				Button->PlayDesignerAnimPreview(bOut);
			}
		}

		bool CanReplay() const
		{
			const UContextMenuButton* Button = GetButton();
			const FPreviewChoice* Choice = Button ? Choices.Find(Button->GetFName()) : nullptr;
			return Choice && Choice->bCanReplay;
		}

		/** The last change once more: put back where it started, at once, then played. */
		void Replay()
		{
			UContextMenuButton* Button = GetButton();
			const FPreviewChoice* Choice = Button ? Choices.Find(Button->GetFName()) : nullptr;
			if (!Choice || !Choice->bCanReplay)
			{
				return;
			}
			if (Choice->bLastWasValue)
			{
				Button->SetDesignerPreviewValueIndex(Choice->ReplayFromValue, /*bAnimate=*/false);
				Button->SetDesignerPreviewValueIndex(Choice->ReplayFromValue + Choice->ReplayStep, /*bAnimate=*/true);
			}
			else
			{
				Button->PlayDesignerPreviewState(Choice->ReplayFromState, /*bAnimate=*/false);
				Button->PlayDesignerPreviewState(Choice->State, /*bAnimate=*/true);
			}
		}

	private:

		/**
		 * Follows the selection, and the designer's preview: it is made again on every compile and many edits, and every button
		 * the toolbar has set up is shown again as it was left - at once - in the new one.
		 */
		void Refresh() const
		{
			const TSharedPtr<FWidgetBlueprintEditor> Pinned = Editor.Pin();
			if (!Pinned.IsValid())
			{
				SelectedButton.Reset();
				return;
			}

			UUserWidget* PreviewRoot = Pinned->GetPreview();
			if (PreviewRoot != LastPreviewRoot.Get())
			{
				LastPreviewRoot = PreviewRoot;
				ShowChoicesIn(PreviewRoot);
			}

			UContextMenuButton* Found = nullptr;
			if (Pinned->GetSelectedWidgets().Num() == 1)
			{
				Found = Cast<UContextMenuButton>(Pinned->GetSelectedWidgets().CreateConstIterator()->GetPreview());
			}
			SelectedButton = Found;
		}

		void ShowChoicesIn(UUserWidget* PreviewRoot) const
		{
			if (!PreviewRoot || !PreviewRoot->WidgetTree)
			{
				return;
			}
			for (const TPair<FName, FPreviewChoice>& Entry : Choices)
			{
				UContextMenuButton* Button = Cast<UContextMenuButton>(PreviewRoot->WidgetTree->FindWidget(Entry.Key));
				if (!Button)
				{
					continue;
				}
				if (Entry.Value.bHasValue && Button->GetDesignerPreviewValueCount() > 0)
				{
					Button->SetDesignerPreviewValueIndex(Entry.Value.ValueIndex, /*bAnimate=*/false);
				}
				if (Entry.Value.bHasState)
				{
					Button->PlayDesignerPreviewState(Entry.Value.State, /*bAnimate=*/false);
				}
			}
		}

		TWeakPtr<FWidgetBlueprintEditor> Editor;
		mutable TWeakObjectPtr<UContextMenuButton> SelectedButton;
		mutable TWeakObjectPtr<UUserWidget> LastPreviewRoot;
		TMap<FName, FPreviewChoice> Choices;
	};

	TSharedRef<SWidget> MakeIcon(const FSlateBrush* Brush, const FText& Fallback)
	{
		if (Brush)
		{
			return SNew(SBox)
				.WidthOverride(14.f)
				.HeightOverride(14.f)
				[
					SNew(SImage)
					.Image(Brush)
					.ColorAndOpacity(FSlateColor::UseForeground())
				];
		}
		return SNew(STextBlock).Text(Fallback);
	}

	TSharedRef<SWidget> MakeStateToggle(const TSharedRef<FSelectedButtonPreview>& Preview, EContextMenuDesignPreview State, const FText& Label,
		const FText& ToolTip, TAttribute<EVisibility> ToggleVisibility = EVisibility::Visible)
	{
		return SNew(SCheckBox)
			.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
			.Padding(FMargin(6.f, 1.f))
			.ToolTipText(ToolTip)
			.Visibility(ToggleVisibility)
			.IsChecked_Lambda([Preview, State]() { return Preview->GetState() == State ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([Preview, State](ECheckBoxState) { Preview->PickState(State); })
			[
				SNew(STextBlock).Text(Label)
			];
	}

	TSharedRef<SWidget> MakeSmallButton(const TSharedRef<SWidget>& Content, const FText& ToolTip, TFunction<void()> OnClicked, TAttribute<bool> IsButtonEnabled = true)
	{
		return SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(FMargin(3.f, 2.f))
			.VAlign(VAlign_Center)
			.ToolTipText(ToolTip)
			.IsEnabled(IsButtonEnabled)
			.OnClicked_Lambda([OnClicked]() { OnClicked(); return FReply::Handled(); })
			[
				Content
			];
	}

	/** The controls, collapsed away unless a Context Menu Button is selected. Kept narrow: about 430 px, 550 with the stepper. */
	TSharedRef<SWidget> MakeControls(const TSharedRef<FSelectedButtonPreview>& Preview)
	{
		const FSlateBrush* LeftBrush = PickBrush({ "Icons.ChevronLeft", "Icons.ArrowLeft" });
		const FSlateBrush* RightBrush = PickBrush({ "Icons.ChevronRight", "Icons.ArrowRight" });
		const FSlateBrush* ReplayBrush = PickBrush({ "Icons.Refresh", "GenericCommands.Undo" });

		return SNew(SHorizontalBox)
			.Visibility_Lambda([Preview]() { return Preview->GetButton() ? EVisibility::Visible : EVisibility::Collapsed; })

			// Which button.
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(6.f, 0.f, 6.f, 0.f))
			[
				SNew(SBox)
				.MaxDesiredWidth(110.f)
				[
					SNew(STextBlock)
					.Font(FAppStyle::GetFontStyle("NormalFontBold"))
					.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
					.Text_Lambda([Preview]() { return Preview->GetLabel(); })
					.ToolTipText(LOCTEXT("ButtonTooltip", "The selected button's preview: pick a state or a value to see the button change to it as it does in game."))
				]
			]

			// Its states.
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[
					MakeStateToggle(Preview, EContextMenuDesignPreview::Normal, LOCTEXT("Normal", "Normal"),
						LOCTEXT("NormalTooltip", "Normal: the button at rest. It blends here as it does in game."))
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					MakeStateToggle(Preview, EContextMenuDesignPreview::Hovered, LOCTEXT("Hover", "Hover"),
						LOCTEXT("HoverTooltip", "Hovered - or focused by keyboard or gamepad."))
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					MakeStateToggle(Preview, EContextMenuDesignPreview::Pressed, LOCTEXT("Press", "Press"),
						LOCTEXT("PressTooltip", "Pressed (a button that can be enabled goes straight to its click's result in game)."))
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					MakeStateToggle(Preview, EContextMenuDesignPreview::Enabled, LOCTEXT("Enabled", "Enabled"),
						LOCTEXT("EnabledTooltip", "Enabled: switched on, checked, chosen (buttons that can be enabled)."),
						TAttribute<EVisibility>::CreateLambda([Preview]() { return Preview->CanBeEnabled() ? EVisibility::Visible : EVisibility::Collapsed; }))
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					MakeStateToggle(Preview, EContextMenuDesignPreview::Disabled, LOCTEXT("Disabled", "Disabled"),
						LOCTEXT("DisabledTooltip", "Disabled: Is Enabled off."))
				]
			]

			// Its values, for a button that steps through some.
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(6.f, 0.f, 0.f, 0.f))
			[
				SNew(SHorizontalBox)
				.Visibility_Lambda([Preview]() { return Preview->HasValues() ? EVisibility::Visible : EVisibility::Collapsed; })
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					MakeSmallButton(MakeIcon(LeftBrush, LOCTEXT("PreviousGlyph", "<")), LOCTEXT("PreviousTooltip", "Show the previous value, as the button steps back in game."),
						[Preview]() { Preview->StepValue(-1); })
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SBox)
					.MinDesiredWidth(48.f)
					.MaxDesiredWidth(96.f)
					.HAlign(HAlign_Center)
					[
						SNew(STextBlock)
						.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
						.Text_Lambda([Preview]() { return Preview->GetValueText(); })
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					MakeSmallButton(MakeIcon(RightBrush, LOCTEXT("NextGlyph", ">")), LOCTEXT("NextTooltip", "Show the next value, as the button steps on in game."),
						[Preview]() { Preview->StepValue(1); })
				]
			]

			// Its Anim In / Anim Out, for a button that has them: played once on the designer's copy, as at runtime.
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(6.f, 0.f, 0.f, 0.f))
			[
				SNew(SHorizontalBox)
				.Visibility_Lambda([Preview]() { return Preview->HasAnimations() ? EVisibility::Visible : EVisibility::Collapsed; })
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					MakeSmallButton(SNew(STextBlock).Text(LOCTEXT("AnimIn", "In")), LOCTEXT("AnimInTooltip", "Play the button's Anim In once."),
						[Preview]() { Preview->PlayAnimation(/*bOut=*/false); })
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					MakeSmallButton(SNew(STextBlock).Text(LOCTEXT("AnimOut", "Out")), LOCTEXT("AnimOutTooltip", "Play the button's Anim Out once, from Anim In's end."),
						[Preview]() { Preview->PlayAnimation(/*bOut=*/true); })
				]
			]

			// The last change again.
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(4.f, 0.f, 2.f, 0.f))
			[
				MakeSmallButton(MakeIcon(ReplayBrush, LOCTEXT("ReplayGlyph", "Replay")), LOCTEXT("ReplayTooltip", "Play the last change again, from where it started."),
					[Preview]() { Preview->Replay(); },
					TAttribute<bool>::CreateLambda([Preview]() { return Preview->CanReplay(); }))
			];
	}

	/** Runs once per asset editor window: a Widget Blueprint editor gets the controls, every other editor nothing. */
	void BuildEntries(FToolMenuSection& Section)
	{
		const UAssetEditorToolkitMenuContext* Context = Section.FindContext<UAssetEditorToolkitMenuContext>();
		const TSharedPtr<FAssetEditorToolkit> Toolkit = Context ? Context->Toolkit.Pin() : nullptr;
		if (!Toolkit.IsValid() || !IsWidgetBlueprintEditor(*Toolkit))
		{
			return;
		}
		const TSharedRef<FSelectedButtonPreview> Preview = MakeShared<FSelectedButtonPreview>(StaticCastSharedPtr<FWidgetBlueprintEditor>(Toolkit));
		Section.AddEntry(FToolMenuEntry::InitWidget("NameTBDButtonPreview", MakeControls(Preview), FText::GetEmpty(), /*bNoIndent=*/true));
	}
}

void FButtonPreviewToolbar::Register()
{
	UToolMenus* ToolMenus = UToolMenus::Get();
	if (!ToolMenus)
	{
		return;
	}

	// The Widget Blueprint editor's own toolbar, like the selected widget's animation controls beside it.
	UToolMenu* Menu = ToolMenus->ExtendMenu("AssetEditor.WidgetBlueprintEditor.ToolBar");
	if (!Menu)
	{
		Menu = ToolMenus->ExtendMenu("AssetEditorToolbar.CommonActions");
	}
	if (!Menu)
	{
		return;
	}

	// The same section as the selected widget's animation controls (FWidgetAnimBuilderToolbar, registered first): a toolbar draws
	// a divider at the start of every section, whether or not its controls are collapsed, so a section of its own drew a second
	// divider with an empty gap before it. One section, one divider.
	FToolMenuSection& Section = Menu->FindOrAddSection(FWidgetAnimBuilderToolbar::SelectedWidgetSectionName);
	Section.AddDynamicEntry("NameTBDButtonPreviewControls", FNewToolMenuSectionDelegate::CreateStatic(&ButtonPreviewToolbarPrivate::BuildEntries));
}

#undef LOCTEXT_NAMESPACE
