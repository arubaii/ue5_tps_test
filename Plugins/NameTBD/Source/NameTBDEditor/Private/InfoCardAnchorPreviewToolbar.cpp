// Copyright AR GameSystems 2026. All rights reserved.

#include "InfoCardAnchorPreviewToolbar.h"
#include "WidgetAnimBuilderToolbar.h"

#include "NameTBD/Cards/InfoCardAnchor.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Toolkits/AssetEditorToolkitMenuContext.h"
#include "ToolMenus.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintEditor.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "InfoCardAnchorPreviewToolbar"

namespace InfoCardAnchorPreviewToolbarPrivate
{
	const FName WidgetBlueprintEditorName(TEXT("WidgetBlueprintEditor"));

	bool IsWidgetBlueprintEditor(const FAssetEditorToolkit& Toolkit)
	{
		const TArray<UObject*>* EditedObjects = Toolkit.GetObjectsCurrentlyBeingEdited();
		if (!EditedObjects || Toolkit.GetToolkitFName() != WidgetBlueprintEditorName)
		{
			return false;
		}
		return EditedObjects->ContainsByPredicate([](const UObject* Edited) { return Edited && Edited->IsA<UWidgetBlueprint>(); });
	}

	/** The Info Card Anchor selected in one Widget Blueprint editor's designer, and the cards the toolbar picked for anchors there. */
	class FSelectedAnchorPreview
	{
	public:

		explicit FSelectedAnchorPreview(TWeakPtr<FWidgetBlueprintEditor> InEditor)
			: Editor(InEditor)
		{
		}

		UInfoCardAnchor* GetAnchor() const
		{
			Refresh();
			return SelectedAnchor.Get();
		}

		FText GetLabel() const
		{
			const UInfoCardAnchor* Anchor = GetAnchor();
			if (!Anchor)
			{
				return FText::GetEmpty();
			}
			const FString& Label = Anchor->GetDisplayLabel();
			return FText::FromString(Label.IsEmpty() ? Anchor->GetName() : Label);
		}

		FText GetCardText() const
		{
			const UInfoCardAnchor* Anchor = GetAnchor();
			const FName Shown = Anchor ? Anchor->GetDesignerPreviewCard() : NAME_None;
			return Shown.IsNone() ? LOCTEXT("NoCard", "None") : FText::FromName(Shown);
		}

		TArray<FName> GetCardIds() const
		{
			const UInfoCardAnchor* Anchor = GetAnchor();
			return Anchor ? Anchor->GetInfoCardIds() : TArray<FName>();
		}

		void PickCard(FName CardId)
		{
			if (UInfoCardAnchor* Anchor = GetAnchor())
			{
				Anchor->SetDesignerPreviewCardPick(CardId);
				Picks.Add(Anchor->GetFName(), CardId);
			}
		}

		bool HasAnimations() const
		{
			const UInfoCardAnchor* Anchor = GetAnchor();
			return Anchor && Anchor->HasDesignerPreviewAnimations();
		}

		void PlayAnimation(bool bOut)
		{
			if (UInfoCardAnchor* Anchor = GetAnchor())
			{
				Anchor->PlayDesignerCardAnimation(bOut);
			}
		}

	private:

		/** Follows the selection, and the designer's preview, which is made again on every compile and many edits. */
		void Refresh() const
		{
			const TSharedPtr<FWidgetBlueprintEditor> Pinned = Editor.Pin();
			if (!Pinned.IsValid())
			{
				SelectedAnchor.Reset();
				return;
			}

			UUserWidget* PreviewRoot = Pinned->GetPreview();
			if (PreviewRoot != LastPreviewRoot.Get())
			{
				LastPreviewRoot = PreviewRoot;
				ShowPicksIn(PreviewRoot);
			}

			UInfoCardAnchor* Found = nullptr;
			if (Pinned->GetSelectedWidgets().Num() == 1)
			{
				Found = Cast<UInfoCardAnchor>(Pinned->GetSelectedWidgets().CreateConstIterator()->GetPreview());
			}
			SelectedAnchor = Found;
		}

		void ShowPicksIn(UUserWidget* PreviewRoot) const
		{
			if (!PreviewRoot || !PreviewRoot->WidgetTree)
			{
				return;
			}
			for (const TPair<FName, FName>& Pick : Picks)
			{
				if (UInfoCardAnchor* Anchor = Cast<UInfoCardAnchor>(PreviewRoot->WidgetTree->FindWidget(Pick.Key)))
				{
					Anchor->SetDesignerPreviewCardPick(Pick.Value);
				}
			}
		}

		TWeakPtr<FWidgetBlueprintEditor> Editor;
		mutable TWeakObjectPtr<UInfoCardAnchor> SelectedAnchor;
		mutable TWeakObjectPtr<UUserWidget> LastPreviewRoot;
		TMap<FName, FName> Picks;
	};

	TSharedRef<SWidget> MakeSmallButton(const FText& Label, const FText& ToolTip, TFunction<void()> OnClicked, TAttribute<bool> IsButtonEnabled)
	{
		return SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(FMargin(3.f, 2.f))
			.VAlign(VAlign_Center)
			.ToolTipText(ToolTip)
			.IsEnabled(IsButtonEnabled)
			.OnClicked_Lambda([OnClicked]() { OnClicked(); return FReply::Handled(); })
			[
				SNew(STextBlock).Text(Label)
			];
	}

	TSharedRef<SWidget> MakeCardMenu(const TSharedRef<FSelectedAnchorPreview>& Preview)
	{
		FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("PickNone", "None"),
			LOCTEXT("PickNoneTooltip", "Show no card beside this anchor in the designer."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([Preview]() { Preview->PickCard(NAME_None); })));

		for (const FName CardId : Preview->GetCardIds())
		{
			MenuBuilder.AddMenuEntry(
				FText::FromName(CardId),
				FText::Format(LOCTEXT("PickCardTooltip", "Show the {0} card beside this anchor, as the game will draw it."), FText::FromName(CardId)),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([Preview, CardId]() { Preview->PickCard(CardId); })));
		}

		return MenuBuilder.MakeWidget();
	}

	/** The controls, collapsed away unless an Info Card Anchor is selected. */
	TSharedRef<SWidget> MakeControls(const TSharedRef<FSelectedAnchorPreview>& Preview)
	{
		return SNew(SHorizontalBox)
			.Visibility_Lambda([Preview]() { return Preview->GetAnchor() ? EVisibility::Visible : EVisibility::Collapsed; })

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
					.ToolTipText(LOCTEXT("AnchorTooltip", "The selected anchor's card preview: pick a card to see it beside the anchor, then play its entrance and exit."))
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SComboButton)
				.ComboButtonStyle(FAppStyle::Get(), "SimpleComboButton")
				.ToolTipText(LOCTEXT("CardTooltip", "Which card the designer shows beside this anchor (this designer copy only; the anchor's Preview Card is what is saved)."))
				.OnGetMenuContent_Lambda([Preview]() { return MakeCardMenu(Preview); })
				.ButtonContent()
				[
					SNew(STextBlock)
					.Text_Lambda([Preview]() { return FText::Format(LOCTEXT("CardLabel", "Card: {0}"), Preview->GetCardText()); })
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(6.f, 0.f, 2.f, 0.f))
			[
				MakeSmallButton(LOCTEXT("AnimIn", "In"), LOCTEXT("AnimInTooltip", "Play the shown card's entrance once: its class's Anim In plus this anchor's additive animations."),
					[Preview]() { Preview->PlayAnimation(/*bOut=*/false); },
					TAttribute<bool>::CreateLambda([Preview]() { return Preview->HasAnimations(); }))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				MakeSmallButton(LOCTEXT("AnimOut", "Out"), LOCTEXT("AnimOutTooltip", "Play the shown card's exit once."),
					[Preview]() { Preview->PlayAnimation(/*bOut=*/true); },
					TAttribute<bool>::CreateLambda([Preview]() { return Preview->HasAnimations(); }))
			];
	}

	void BuildEntries(FToolMenuSection& Section)
	{
		const UAssetEditorToolkitMenuContext* Context = Section.FindContext<UAssetEditorToolkitMenuContext>();
		const TSharedPtr<FAssetEditorToolkit> Toolkit = Context ? Context->Toolkit.Pin() : nullptr;
		if (!Toolkit.IsValid() || !IsWidgetBlueprintEditor(*Toolkit))
		{
			return;
		}
		const TSharedRef<FSelectedAnchorPreview> Preview = MakeShared<FSelectedAnchorPreview>(StaticCastSharedPtr<FWidgetBlueprintEditor>(Toolkit));
		Section.AddEntry(FToolMenuEntry::InitWidget("NameTBDInfoCardPreview", MakeControls(Preview), FText::GetEmpty(), /*bNoIndent=*/true));
	}
}

void FInfoCardAnchorPreviewToolbar::Register()
{
	UToolMenus* ToolMenus = UToolMenus::Get();
	if (!ToolMenus)
	{
		return;
	}

	UToolMenu* Menu = ToolMenus->ExtendMenu("AssetEditor.WidgetBlueprintEditor.ToolBar");
	if (!Menu)
	{
		Menu = ToolMenus->ExtendMenu("AssetEditorToolbar.CommonActions");
	}
	if (!Menu)
	{
		return;
	}

	// The selected widget's section, so the toolbar draws one divider for it (see FButtonPreviewToolbar::Register).
	FToolMenuSection& Section = Menu->FindOrAddSection(FWidgetAnimBuilderToolbar::SelectedWidgetSectionName);
	Section.AddDynamicEntry("NameTBDInfoCardPreviewControls", FNewToolMenuSectionDelegate::CreateStatic(&InfoCardAnchorPreviewToolbarPrivate::BuildEntries));
}

#undef LOCTEXT_NAMESPACE
