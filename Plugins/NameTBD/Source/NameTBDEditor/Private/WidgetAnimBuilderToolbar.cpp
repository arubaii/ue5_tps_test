// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimBuilderToolbar.h"

#include "NameTBD/WidgetAnimation/WidgetAnimBuilder.h"
#include "NameTBDColors.h"
#include "Styling/AppStyle.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Toolkits/AssetEditorToolkitMenuContext.h"
#include "ToolMenus.h"
#include "WidgetBlueprint.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "WidgetAnimBuilderToolbar"

namespace
{
	// ---------------------------------------------------------------------------------------------
	// The two colours the Play/Pause button is tinted with. Change these and nothing else.
	// ---------------------------------------------------------------------------------------------

	/** Shown while the preview is running, on the button that would freeze it. */
	const FLinearColor PauseTint(NameTBD::Colors::AnimationOrange_Disabled);   // amber

	/** Shown while the preview is frozen, on the button that would let it go. */
	const FLinearColor PlayTint(NameTBD::Colors::AnimationOrange_Normal);              // animation preset color

	/**
	 * The first of Candidates the app style actually has a brush for.
	 *
	 * Icon names move between engine versions - Icons.Pause is absent in 5.7 and logs as missing, which is
	 * what left the button blank - and GetBrush on a name that isn't there draws nothing. GetOptionalBrush
	 * with a null default answers the question without the warning, and the answer is settled once.
	 */
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

	const FSlateBrush* GetPauseBrush()
	{
		static const FSlateBrush* Brush = PickBrush({ "Icons.Pause", "PlayWorld.PausePlaySession", "GenericPause", "Sequencer.Transport.Pause" });
		return Brush;
	}

	const FSlateBrush* GetPlayBrush()
	{
		static const FSlateBrush* Brush = PickBrush({ "Icons.Play", "PlayWorld.ResumePlaySession", "PlayWorld.PlayInViewport", "GenericPlay" });
		return Brush;
	}

	FName GetResetIconName()
	{
		static const FName IconName =
			PickBrush({ "Icons.Refresh" }) ? FName("Icons.Refresh") :
			PickBrush({ "Icons.Convert" }) ? FName("Icons.Convert") : NAME_None;

		return IconName;
	}

	/** The builder class this toolbar belongs to, or null for every other asset editor. */
	UClass* GetBuilderClass(const FToolMenuSection& Section)
	{
		const UAssetEditorToolkitMenuContext* Context = Section.FindContext<UAssetEditorToolkitMenuContext>();
		if (!Context)
		{
			return nullptr;
		}

		const TSharedPtr<FAssetEditorToolkit> Toolkit = Context->Toolkit.Pin();
		if (!Toolkit.IsValid())
		{
			return nullptr;
		}

		const TArray<UObject*>* EditedObjects = Toolkit->GetObjectsCurrentlyBeingEdited();
		if (!EditedObjects)
		{
			return nullptr;
		}

		for (UObject* EditedObject : *EditedObjects)
		{
			const UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(EditedObject);
			if (!WidgetBlueprint || !WidgetBlueprint->GeneratedClass)
			{
				continue;
			}

			if (WidgetBlueprint->GeneratedClass->IsChildOf(UWidgetAnimBuilder::StaticClass()))
			{
				return WidgetBlueprint->GeneratedClass;
			}
		}

		return nullptr;
	}

	void TogglePaused(TWeakObjectPtr<UClass> WeakBuilderClass)
	{
		UWidgetAnimBuilder::ForEachLivePreview(WeakBuilderClass.Get(), [](UWidgetAnimBuilder& Builder)
		{
			Builder.TogglePreviewPaused();
		});
	}

	void ResetPreview(TWeakObjectPtr<UClass> WeakBuilderClass)
	{
		UWidgetAnimBuilder::ForEachLivePreview(WeakBuilderClass.Get(), [](UWidgetAnimBuilder& Builder)
		{
			// Reset shows the widget exactly as authored, with no animation applied, and leaves it
			// frozen there until Play is pressed again.
			Builder.SetPreviewPaused(true);
			Builder.ResetPreview();
		});
	}

	/** Nothing to drive until the designer has actually built a preview - a Graph-only session has none. */
	bool HasPreview(TWeakObjectPtr<UClass> WeakBuilderClass)
	{
		return UWidgetAnimBuilder::HasLivePreview(WeakBuilderClass.Get());
	}

	bool IsPaused(TWeakObjectPtr<UClass> WeakBuilderClass)
	{
		return UWidgetAnimBuilder::IsLivePreviewPaused(WeakBuilderClass.Get());
	}

	/**
	 * The Play/Pause button, built by hand rather than through InitToolBarButton.
	 *
	 * A toolbar entry takes an FSlateIcon, and an FSlateIcon carries no colour - the icon is drawn in
	 * whatever the toolbar style says. Tinting it means owning the widget, so this is an SButton with an
	 * SImage inside it that is tinted per state, wearing the editor's flat button style where that exists.
	 */
	TSharedRef<SWidget> MakePlayPauseWidget(TWeakObjectPtr<UClass> WeakBuilderClass)
	{
		const ISlateStyle& Style = FAppStyle::Get();

		TSharedRef<SButton> Button =
			SNew(SButton)
			.ContentPadding(FMargin(6.f, 4.f))
			.IsEnabled_Lambda([WeakBuilderClass]() { return HasPreview(WeakBuilderClass); })
			.ToolTipText_Lambda([WeakBuilderClass]()
			{
				return IsPaused(WeakBuilderClass)
					? LOCTEXT("PlayTooltip", "Let the preview run on from where it is")
					: LOCTEXT("PauseTooltip", "Freeze the preview on this frame");
			})
			.OnClicked_Lambda([WeakBuilderClass]()
			{
				TogglePaused(WeakBuilderClass);
				return FReply::Handled();
			})
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SImage)
					.DesiredSizeOverride(FVector2D(16.f, 16.f))
					.Image_Lambda([WeakBuilderClass]()
					{
						return IsPaused(WeakBuilderClass) ? GetPlayBrush() : GetPauseBrush();
					})
					.ColorAndOpacity_Lambda([WeakBuilderClass]()
					{
						// The button offers the action, not the state: frozen offers Play, in Play's colour.
						return FSlateColor(IsPaused(WeakBuilderClass) ? PlayTint : PauseTint);
					})
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4.f, 0.f, 0.f, 0.f)
				[
					SNew(STextBlock)
					.Text_Lambda([WeakBuilderClass]()
					{
						return IsPaused(WeakBuilderClass)
							? LOCTEXT("PlayLabel", "Play Animation")
							: LOCTEXT("PauseLabel", "Pause Animation");
					})
				]
			];

		// Left at the default look if this style set has no SimpleButton, which is the flat, borderless
		// one the rest of the toolbar uses.
		if (Style.HasWidgetStyle<FButtonStyle>("SimpleButton"))
		{
			Button->SetButtonStyle(&Style.GetWidgetStyle<FButtonStyle>("SimpleButton"));
		}

		return Button;
	}

	const FNumberFormattingOptions& GetLengthNumberFormat()
	{
		static const FNumberFormattingOptions Format = []
		{
			FNumberFormattingOptions Options;
			Options.MinimumFractionalDigits = 2;
			Options.MaximumFractionalDigits = 2;
			return Options;
		}();

		return Format;
	}

	/**
	 * Elapsed, total, and loop info off whichever live preview is found first.
	 *
	 * Several designer tabs can each have their own live preview; the toolbar can only show one set of
	 * numbers, so this settles for the first one found, same as IsPaused and HasPreview already
	 * effectively do by OR-ing across every live instance.
	 */
	void GetPreviewLength(
		TWeakObjectPtr<UClass> WeakBuilderClass,
		float& OutElapsedSeconds,
		float& OutTotalSeconds,
		bool& OutLoops,
		bool& OutPingPong,
		float& OutLoopSeconds)
	{
		OutElapsedSeconds = 0.f;
		OutTotalSeconds = 0.f;
		OutLoops = false;
		OutPingPong = false;
		OutLoopSeconds = 0.f;

		bool bFound = false;
		UWidgetAnimBuilder::ForEachLivePreview(WeakBuilderClass.Get(), [&](UWidgetAnimBuilder& Builder)
		{
			if (bFound)
			{
				return;
			}

			bFound = true;
			OutElapsedSeconds = Builder.GetPreviewElapsedSeconds();
			OutTotalSeconds = Builder.GetPreviewTotalSeconds();
			OutLoops = Builder.GetPreviewLoopInfo(OutPingPong, OutLoopSeconds);
		});
	}

	FText FormatPreviewLength(float ElapsedSeconds, float TotalSeconds, bool bLoops, bool bPingPong, float LoopSeconds)
	{
		if (TotalSeconds == 0.f)
		{
			// No layers and no Preview Animation - there is nothing to time yet.
			return LOCTEXT("LengthEmpty", "--");
		}

		const FText ElapsedText = FText::AsNumber(ElapsedSeconds, &GetLengthNumberFormat());

		const FText BaseText = TotalSeconds < 0.f
			// A layer somewhere in the stack loops forever, so there is no total to reach.
			? FText::Format(LOCTEXT("LengthEndless", "{0}s / \u221E"), ElapsedText)
			: FText::Format(LOCTEXT("Length", "{0}s / {1}s"), ElapsedText, FText::AsNumber(TotalSeconds, &GetLengthNumberFormat()));

		if (!bLoops)
		{
			return BaseText;
		}

		const FText LoopSecondsText = FText::AsNumber(LoopSeconds, &GetLengthNumberFormat());
		const FText LoopSuffix = bPingPong
			? FText::Format(LOCTEXT("LoopDurationPingPong", "(Loop Duration w/ Ping Pong = {0}s)"), LoopSecondsText)
			: FText::Format(LOCTEXT("LoopDuration", "(Loop Duration = {0}s)"), LoopSecondsText);

		return FText::Format(LOCTEXT("LengthWithLoop", "{0} {1}"), BaseText, LoopSuffix);
	}

	/** The read-only "elapsed / total" label that sits beside Reset. */
	TSharedRef<SWidget> MakeLengthWidget(TWeakObjectPtr<UClass> WeakBuilderClass)
	{
		return SNew(SBox)
			.Padding(FMargin(8.f, 0.f, 4.f, 0.f))
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.ToolTipText(LOCTEXT("LengthTooltip", "How far into the animation currently showing the preview is."))
				.Text_Lambda([WeakBuilderClass]()
				{
					float ElapsedSeconds = 0.f;
					float TotalSeconds = 0.f;
					bool bLoops = false;
					bool bPingPong = false;
					float LoopSeconds = 0.f;
					GetPreviewLength(WeakBuilderClass, ElapsedSeconds, TotalSeconds, bLoops, bPingPong, LoopSeconds);
					return FormatPreviewLength(ElapsedSeconds, TotalSeconds, bLoops, bPingPong, LoopSeconds);
				})
			];
	}

	void BuildEntries(FToolMenuSection& Section)
	{
		UClass* BuilderClass = GetBuilderClass(Section);
		if (!BuilderClass)
		{
			// Not a Widget Anim Builder's editor. Every other asset editor's toolbar is left untouched.
			return;
		}

		const TWeakObjectPtr<UClass> WeakBuilderClass(BuilderClass);

		FToolMenuEntry PlayPause = FToolMenuEntry::InitWidget(
			"WidgetAnimPlayPause",
			MakePlayPauseWidget(WeakBuilderClass),
			FText::GetEmpty(),
			/*bNoIndent=*/true);

		// Sit just ahead of the Widget Reflector button, which is the last thing on this toolbar. An
		// insert naming an entry that isn't there is ignored rather than failing, so the worst case is
		// that these land at the end of the section anyway.
		PlayPause.InsertPosition = FToolMenuInsert("WidgetReflector", EToolMenuInsertType::Before);

		FToolMenuEntry Reset = FToolMenuEntry::InitToolBarButton(
			"WidgetAnimReset",
			FUIAction(
				FExecuteAction::CreateStatic(&ResetPreview, WeakBuilderClass),
				FCanExecuteAction::CreateStatic(&HasPreview, WeakBuilderClass)),
			LOCTEXT("ResetLabel", "Reset"),
			LOCTEXT("ResetTooltip", "Reset the widget to resting state."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), GetResetIconName()));

		Reset.InsertPosition = FToolMenuInsert("WidgetReflector", EToolMenuInsertType::Before);

		FToolMenuEntry Length = FToolMenuEntry::InitWidget(
			"WidgetAnimLength",
			MakeLengthWidget(WeakBuilderClass),
			FText::GetEmpty(),
			/*bNoIndent=*/true);

		Length.InsertPosition = FToolMenuInsert("WidgetAnimReset", EToolMenuInsertType::After);

		Section.AddEntry(PlayPause);
		Section.AddEntry(Reset);
		Section.AddEntry(Length);
	}
}

void FWidgetAnimBuilderToolbar::Register()
{
	UToolMenus* ToolMenus = UToolMenus::Get();
	if (!ToolMenus)
	{
		return;
	}

	// The Widget Blueprint editor's own toolbar, which is where Compile, Diff and Widget Reflector live.
	// AssetEditorToolbar.CommonActions - the save and browse buttons at the far left - is the fallback,
	// since it is the one menu every asset editor is guaranteed to build from.
	UToolMenu* Menu = ToolMenus->ExtendMenu("AssetEditor.WidgetBlueprintEditor.ToolBar");
	if (!Menu)
	{
		Menu = ToolMenus->ExtendMenu("AssetEditorToolbar.CommonActions");
	}

	if (!Menu)
	{
		return;
	}

	FToolMenuSection& Section = Menu->FindOrAddSection("WidgetAnimPreview");

	// Dynamic, because the section is filled once per asset editor window and the context that says which
	// asset that window is editing only exists at that point.
	Section.AddDynamicEntry("WidgetAnimPreviewControls", FNewToolMenuSectionDelegate::CreateStatic(&BuildEntries));
}

#undef LOCTEXT_NAMESPACE