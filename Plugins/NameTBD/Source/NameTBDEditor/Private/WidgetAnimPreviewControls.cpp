// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimPreviewControls.h"

#include "NameTBD/WidgetAnimation/WidgetAnimBuilder.h"

#include "IUMGDesigner.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "WidgetAnimPreviewControls"

DEFINE_LOG_CATEGORY_STATIC(LogWidgetAnimPreviewControls, Log, All);

namespace
{
	// ---------------------------------------------------------------------------------------------
	// Deliberately loud, so the row can be found before it is made to look right.
	//
	// Zero alignment pins the row's top-left corner exactly at the selected widget's top-centre with
	// no lift, ASCII labels avoid the font fallback that U+23F8 triggers, and a solid red background
	// is findable anywhere on the canvas. Once it is on screen: alignment (0.5, 1.0), offset (0, -6),
	// glyphs or icon brushes, and a dark translucent background.
	// ---------------------------------------------------------------------------------------------
	const FVector2D ControlsAlignment(0.f, 0.f);
	const FVector2D ControlsOffset(0.f, 0.f);

	const TCHAR* PlayGlyph = TEXT("PLAY");
	const TCHAR* PauseGlyph = TEXT("PAUSE");
	const TCHAR* RestartGlyph = TEXT("RESTART");

	const FLinearColor ControlsBackground(1.f, 0.f, 0.f, 1.f);
}

FWidgetAnimPreviewControls::FWidgetAnimPreviewControls()
{
	ExtensionId = FName(TEXT("WidgetAnimPreviewControls"));

	UE_LOG(LogWidgetAnimPreviewControls, Log, TEXT("Preview controls extension created for a designer."));
}

UWidgetAnimBuilder* FWidgetAnimPreviewControls::ResolveBuilder(const FWidgetReference& Reference)
{
	if (!Reference.IsValid())
	{
		return nullptr;
	}

	// The preview, not the template. The template is the Blueprint's copy and has no clock running, and
	// this is deliberately re-read rather than remembered: the preview is thrown away and rebuilt often.
	UWidget* PreviewWidget = Reference.GetPreview();
	if (!PreviewWidget)
	{
		return nullptr;
	}

	// A builder placed in some other Widget Blueprint is selected as itself.
	if (UWidgetAnimBuilder* AsBuilder = Cast<UWidgetAnimBuilder>(PreviewWidget))
	{
		return AsBuilder;
	}

	// Inside the builder's own Widget Blueprint the only thing selectable is PreviewImage, whose outer
	// chain runs back up through the widget tree to the builder instance the designer built.
	return PreviewWidget->GetTypedOuter<UWidgetAnimBuilder>();
}

UWidgetAnimBuilder* FWidgetAnimPreviewControls::GetBuilder() const
{
	if (UWidgetAnimBuilder* FromSelection = ResolveBuilder(ExtendedWidget))
	{
		return FromSelection;
	}

	// The reference can name a widget the designer has since replaced wholesale - a compile, a rebuilt
	// tree - in which case the designer's current selection is the same widget under a new instance.
	if (Designer)
	{
		return ResolveBuilder(Designer->GetSelectedWidget());
	}

	return nullptr;
}

bool FWidgetAnimPreviewControls::CanExtendSelection(const TArray<FWidgetReference>& Selection) const
{
	// One widget only: two selected builders would need two rows, and there is no single sensible answer.
	const bool bCanExtend = Selection.Num() == 1 && ResolveBuilder(Selection[0]) != nullptr;

	UE_LOG(LogWidgetAnimPreviewControls, Log, TEXT("CanExtendSelection: %d selected, extending %s"),
		Selection.Num(), bCanExtend ? TEXT("yes") : TEXT("no"));

	return bCanExtend;
}

void FWidgetAnimPreviewControls::ExtendSelection(const TArray<FWidgetReference>& Selection, TArray<TSharedRef<FDesignerSurfaceElement>>& SurfaceElements)
{
	if (Selection.Num() != 1)
	{
		return;
	}

	ExtendedWidget = Selection[0];

	if (!ResolveBuilder(ExtendedWidget))
	{
		return;
	}

	UE_LOG(LogWidgetAnimPreviewControls, Log, TEXT("Adding preview controls for %s."),
		*GetNameSafe(ExtendedWidget.GetTemplate()));

	// Border and button are left at their defaults. Every named style this used to ask for - SimpleButton,
	// ToolPanel.GroupBorder - comes from a style set that may not be the one FAppStyle resolves to here.
	TSharedRef<SWidget> Controls =
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("Border"))
		.BorderBackgroundColor(ControlsBackground)
		.Padding(FMargin(3.f))
		.Visibility(this, &FWidgetAnimPreviewControls::GetControlsVisibility)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(1.f, 0.f)
			[
				SNew(SButton)
				.ContentPadding(FMargin(8.f, 2.f))
				.ToolTipText(this, &FWidgetAnimPreviewControls::GetPlayPauseTooltip)
				.OnClicked(this, &FWidgetAnimPreviewControls::OnPlayPauseClicked)
				[
					SNew(STextBlock)
					.Text(this, &FWidgetAnimPreviewControls::GetPlayPauseLabel)
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(1.f, 0.f)
			[
				SNew(SButton)
				.ContentPadding(FMargin(8.f, 2.f))
				.ToolTipText(LOCTEXT("RestartTooltip", "Restart the preview from the beginning"))
				.OnClicked(this, &FWidgetAnimPreviewControls::OnRestartClicked)
				[
					SNew(STextBlock)
					.Text(FText::FromString(RestartGlyph))
				]
			]
		];

	SurfaceElements.Add(MakeShared<FDesignerSurfaceElement>(
		Controls,
		EExtensionLayoutLocation::TopCenter,
		ControlsAlignment,
		ControlsOffset));
}

EVisibility FWidgetAnimPreviewControls::GetControlsVisibility() const
{
	// Hidden rather than collapsed for the frame or two after a rebuild where nothing resolves: collapsing
	// would drop the row out of the canvas's layout, and the row is what the designer is positioning.
	return GetBuilder() ? EVisibility::Visible : EVisibility::Hidden;
}

FText FWidgetAnimPreviewControls::GetPlayPauseLabel() const
{
	const UWidgetAnimBuilder* LiveBuilder = GetBuilder();
	const bool bPaused = LiveBuilder && LiveBuilder->IsPreviewPaused();

	// The button offers the action, not the state: paused offers Play.
	return FText::FromString(bPaused ? PlayGlyph : PauseGlyph);
}

FText FWidgetAnimPreviewControls::GetPlayPauseTooltip() const
{
	const UWidgetAnimBuilder* LiveBuilder = GetBuilder();
	const bool bPaused = LiveBuilder && LiveBuilder->IsPreviewPaused();

	return bPaused
		? LOCTEXT("PlayTooltip", "Let the preview run on from where it is")
		: LOCTEXT("PauseTooltip", "Freeze the preview on this frame");
}

FReply FWidgetAnimPreviewControls::OnPlayPauseClicked()
{
	if (UWidgetAnimBuilder* LiveBuilder = GetBuilder())
	{
		LiveBuilder->TogglePreviewPaused();
	}

	return FReply::Handled();
}

FReply FWidgetAnimPreviewControls::OnRestartClicked()
{
	if (UWidgetAnimBuilder* LiveBuilder = GetBuilder())
	{
		// Restarting while frozen would look like nothing happened, so it always plays from the top.
		LiveBuilder->SetPreviewPaused(true);
		LiveBuilder->ResetPreview();
	}

	return FReply::Handled();
}

TSharedRef<FDesignerExtension> FWidgetAnimPreviewControlsFactory::CreateDesignerExtension() const
{
	return MakeShared<FWidgetAnimPreviewControls>();
}

#undef LOCTEXT_NAMESPACE