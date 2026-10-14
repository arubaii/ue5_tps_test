// Copyright AR GameSystems 2026. All rights reserved.

#include "MediaBoxWithControls.h"

#include "MediaBoxSettings.h"
#include "MediaBoxTransportBar.h"
#include "SMediaBox.h"

#include "Blueprint/UserWidget.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformTime.h"
#include "Input/Reply.h"
#include "InputCoreTypes.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Layout/Children.h"
#include "Layout/ArrangedChildren.h"

#if WITH_EDITOR
#include "Editor/WidgetCompilerLog.h"
#endif

#define LOCTEXT_NAMESPACE "MediaBoxWithControls"

/**
 * Lays the controls bar over the video: scaled evenly so it is Height x the video's height tall (within Min / Max Height),
 * stretched across the video's width (laid out at width / scale, so its own Fill slots - the scrub bar - take the room), along
 * the bottom or top, inside Padding. Asks for no size of its own: the bar's designed size never makes the Media Box bigger. Only
 * the bar takes the pointer; the rest of the area goes through to the video.
 */
class SMediaBoxControlsFrame : public SPanel
{
public:
	SLATE_BEGIN_ARGS(SMediaBoxControlsFrame)
		: _Alignment(VAlign_Bottom)
		, _Height(0.13f)
		, _MinHeight(0.f)
		, _MaxHeight(0.f)
	{
		_Visibility = EVisibility::SelfHitTestInvisible;
	}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_ARGUMENT(EVerticalAlignment, Alignment)
		SLATE_ARGUMENT(float, Height)
		SLATE_ARGUMENT(float, MinHeight)
		SLATE_ARGUMENT(float, MaxHeight)
		SLATE_ARGUMENT(FMargin, Padding)
	SLATE_END_ARGS()

	SMediaBoxControlsFrame()
		: BarChildren(this)
	{
	}

	void Construct(const FArguments& InArgs)
	{
		// Drawn afresh every frame: a cached drawing of the bar made at another scale (the designer as it lays out, zooms, or the
		// box resized) would be shown stretched - blurry text. The bar is small; the video under it repaints every frame anyway.
		ForceVolatile(true);

		BarAlignment = InArgs._Alignment;
		HeightPart = InArgs._Height;
		MinBarHeight = InArgs._MinHeight;
		MaxBarHeight = InArgs._MaxHeight;
		BarPadding = InArgs._Padding;
		BarWidget = InArgs._Content.Widget;

		// The bar sits in a holder that undoes any render scale above the video (see Tick): laid out that much bigger, drawn
		// shrunk back, so its text is rasterized at the size it is shown.
		BarHolder = SNew(SBox)
		[
			BarWidget.ToSharedRef()
		];
		BarHolder->SetRenderTransformPivot(FVector2D::ZeroVector);
		BarChildren.Add(BarHolder.ToSharedRef());
	}

	virtual void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override
	{
		if (!BarWidget.IsValid())
		{
			LastBarHeight = 0.f;
			return;
		}
		// Kept as our own pointer: in this const function the child list only hands out const widgets.
		const TSharedRef<SWidget> Bar = BarWidget.ToSharedRef();
		if (!ArrangedChildren.Accepts(Bar->GetVisibility()) || !BarHolder.IsValid())
		{
			return;
		}

		const FVector2f Area = FVector2f(FVector2D(AllottedGeometry.GetLocalSize()));
		const FVector2f Inner(FMath::Max(1.f, Area.X - static_cast<float>(BarPadding.GetTotalSpaceAlong<Orient_Horizontal>())),
			FMath::Max(1.f, Area.Y - static_cast<float>(BarPadding.GetTotalSpaceAlong<Orient_Vertical>())));
		const FVector2f Designed = FVector2f(FVector2D(Bar->GetDesiredSize()));

		// The scale: the bar to Height x the video's height, within the limits - and no wider than fits, so nothing in it is
		// squeezed. Unscaled when Height is 0, or the bar has no height of its own (a Canvas Panel root).
		float Scale = 1.f;
		if (HeightPart > 0.f && Designed.Y > KINDA_SMALL_NUMBER)
		{
			float Target = Area.Y * HeightPart;
			if (MinBarHeight > 0.f)
			{
				Target = FMath::Max(Target, MinBarHeight);
			}
			if (MaxBarHeight > 0.f)
			{
				Target = FMath::Min(Target, MaxBarHeight);
			}
			Scale = Target / Designed.Y;
			if (Designed.X > KINDA_SMALL_NUMBER)
			{
				Scale = FMath::Min(Scale, Inner.X / Designed.X);
			}
			Scale = FMath::Max(Scale, KINDA_SMALL_NUMBER);
		}

		// Laid out at the video's width in the bar's own units, its designed height; drawn scaled.
		const float BarHeight = Designed.Y > KINDA_SMALL_NUMBER ? Designed.Y : Inner.Y * HeightPart;
		const FVector2f LocalSize(Inner.X / Scale, FMath::Min(BarHeight, Inner.Y / Scale));
		const float ShownHeight = LocalSize.Y * Scale;
		const float Top = BarAlignment == VAlign_Top ? static_cast<float>(BarPadding.Top)
			: BarAlignment == VAlign_Center ? static_cast<float>(BarPadding.Top) + (Inner.Y - ShownHeight) * 0.5f
			: Area.Y - static_cast<float>(BarPadding.Bottom) - ShownHeight;

		// Laid out HolderScale times bigger (the render scale above), the holder's render transform shrinking it back: drawn the
		// same size, its text sharp.
		ArrangedChildren.AddWidget(AllottedGeometry.MakeChild(BarHolder.ToSharedRef(), LocalSize, FSlateLayoutTransform(Scale * HolderScale, FVector2f(static_cast<float>(BarPadding.Left), Top))));
		LastBarHeight = ShownHeight;
		ArrangedScale = Scale * HolderScale * AllottedGeometry.Scale;
	}

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override
	{
		return FVector2D::ZeroVector;
	}

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
	{
		SPanel::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

		// A render scale above the video (the Widget Gizmo, an animation) - not seen by layout - is undone around the bar: its text
		// was stretched (blurry) under it, while a size change through layout (the Canvas Panel's handles) stayed sharp.
		const float CrispScale = SMediaBox::GetCrispScale(AllottedGeometry);
		if (BarHolder.IsValid() && !FMath::IsNearlyEqual(CrispScale, HolderScale))
		{
			HolderScale = CrispScale;
			BarHolder->SetRenderTransform(HolderScale != 1.f ? TOptional<FSlateRenderTransform>(FSlateRenderTransform(1.f / HolderScale)) : TOptional<FSlateRenderTransform>());
			Invalidate(EInvalidateWidgetReason::Layout);
		}

		// The bar drawn at a new scale: everything in it laid out and painted again, its text rasterized at that scale.
		if (BarWidget.IsValid() && !FMath::IsNearlyEqual(ArrangedScale, PaintedScale, 0.001f))
		{
			PaintedScale = ArrangedScale;
			BarWidget->Invalidate(EInvalidateWidgetReason::Layout | EInvalidateWidgetReason::Paint);
			Invalidate(EInvalidateWidgetReason::Layout);
		}
	}

	virtual FChildren* GetChildren() override
	{
		return &BarChildren;
	}

	/** The bar's height as drawn, in the video's units - for lifting the subtitles above it. */
	float GetShownBarHeight() const
	{
		return LastBarHeight;
	}

private:
	TSlotlessChildren<SWidget> BarChildren;
	TSharedPtr<SWidget> BarWidget;
	TSharedPtr<SBox> BarHolder;
	float HolderScale = 1.f;
	EVerticalAlignment BarAlignment = VAlign_Bottom;
	float HeightPart = 0.13f;
	float MinBarHeight = 0.f;
	float MaxBarHeight = 0.f;
	FMargin BarPadding;
	mutable float LastBarHeight = 0.f;
	mutable float ArrangedScale = 1.f;
	float PaintedScale = 1.f;
};

/**
 * What a Media Box With Controls is in Slate: the video (SMediaBox) with the controls bar over it (in SMediaBoxControlsFrame).
 * Takes the click on the video (not on the controls) for Click To Play / Pause; everything else is read by the widget each tick.
 */
class SMediaBoxControlsHost : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMediaBoxControlsHost)
		: _ControlsAlignment(VAlign_Bottom)
		, _ControlsHeight(0.13f)
		, _ControlsMinHeight(0.f)
		, _ControlsMaxHeight(0.f)
	{}
		SLATE_ARGUMENT(UMediaBoxWithControls*, OwnerBox)
		SLATE_ARGUMENT(TSharedPtr<SWidget>, Video)
		SLATE_ARGUMENT(TSharedPtr<SWidget>, Controls)
		SLATE_ARGUMENT(EVerticalAlignment, ControlsAlignment)
		SLATE_ARGUMENT(float, ControlsHeight)
		SLATE_ARGUMENT(float, ControlsMinHeight)
		SLATE_ARGUMENT(float, ControlsMaxHeight)
		SLATE_ARGUMENT(FMargin, ControlsPadding)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		WeakOwnerBox = InArgs._OwnerBox;
		ControlsContent = InArgs._Controls;

		TSharedRef<SWidget> ControlsLayer = SNullWidget::NullWidget;
		if (ControlsContent.IsValid())
		{
			ControlsLayer = SAssignNew(ControlsFrame, SMediaBoxControlsFrame)
				.Alignment(InArgs._ControlsAlignment)
				.Height(InArgs._ControlsHeight)
				.MinHeight(InArgs._ControlsMinHeight)
				.MaxHeight(InArgs._ControlsMaxHeight)
				.Padding(InArgs._ControlsPadding)
				[
					ControlsContent.ToSharedRef()
				];
		}

		ChildSlot
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				InArgs._Video.IsValid() ? InArgs._Video.ToSharedRef() : SNullWidget::NullWidget
			]
			+ SOverlay::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			[
				ControlsLayer
			]
		];
	}

	/** The bar's area in desktop space, for telling a click on the video from one on the bar. */
	bool IsOverControls(const FVector2D& ScreenPosition) const
	{
		return ControlsContent.IsValid() && ControlsContent->GetVisibility().IsVisible()
			&& ControlsContent->GetTickSpaceGeometry().IsUnderLocation(ScreenPosition);
	}

	/** The bar's height as drawn, in this widget's units, for lifting the subtitles. */
	float GetControlsHeight() const
	{
		return ControlsFrame.IsValid() ? ControlsFrame->GetShownBarHeight() : 0.f;
	}

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		UMediaBoxWithControls* OwnerBox = WeakOwnerBox.Get();
		if (OwnerBox && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && !IsOverControls(MouseEvent.GetScreenSpacePosition())
			&& OwnerBox->HandleVideoClicked())
		{
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

private:
	TWeakObjectPtr<UMediaBoxWithControls> WeakOwnerBox;
	TSharedPtr<SWidget> ControlsContent;
	TSharedPtr<SMediaBoxControlsFrame> ControlsFrame;
};

UMediaBoxWithControls::UMediaBoxWithControls()
{
	// A player takes the pointer: hover shows the controls, a click plays / pauses, the bar's buttons are clicked.
	SetVisibilityInternal(ESlateVisibility::Visible);

	// The bar (and anything in it, a Background Blur too) never draws outside the video.
	SetClipping(EWidgetClipping::ClipToBounds);
	LetterboxColor = FLinearColor::Black;
	DesiredSize = FVector2D(640.0, 360.0);

	// A player waiting for its play button: warm while shown, so the first play starts at once.
	bKeepWarmWhileShown = true;
}

FMediaBoxControlsBehavior UMediaBoxWithControls::GetResolvedControlsBehavior() const
{
	return (bOverrideControlsBehavior ? ControlsBehavior : UMediaBoxSettings::Get().DefaultControlsBehavior).Resolve();
}

TSubclassOf<UMediaBoxTransportBar> UMediaBoxWithControls::ResolveControlsClass() const
{
	if (ControlsClass)
	{
		return ControlsClass;
	}
	return UMediaBoxSettings::Get().DefaultControlsClass.LoadSynchronous();
}

void UMediaBoxWithControls::EnsureControlsWidget()
{
	const TSubclassOf<UMediaBoxTransportBar> BarClass = ResolveControlsClass();
	if (ControlsWidget && ControlsWidget->GetClass() == BarClass.Get())
	{
		return;
	}

	ControlsWidget = nullptr;
	if (!BarClass || BarClass->HasAnyClassFlags(CLASS_Abstract))
	{
		return;
	}

	// Made by this widget, not put in the Widget Blueprint's tree: it is the player's own part.
	ControlsWidget = CreateWidget<UMediaBoxTransportBar>(this, BarClass);
	if (ControlsWidget)
	{
#if WITH_EDITOR
		if (IsDesignTime())
		{
			ControlsWidget->SetDesignerFlags(GetDesignerFlags());
		}
#endif
		ControlsWidget->SetMediaBox(this);
		ControlsShownVisibility = ControlsWidget->GetVisibility();
		if (ControlsShownVisibility == ESlateVisibility::Hidden || ControlsShownVisibility == ESlateVisibility::Collapsed)
		{
			ControlsShownVisibility = ESlateVisibility::SelfHitTestInvisible;
		}
	}
}

TSharedRef<SWidget> UMediaBoxWithControls::BuildBoxContent(const TSharedRef<SWidget>& Video)
{
	EnsureControlsWidget();
	TSharedPtr<SWidget> Controls = ControlsWidget ? ControlsWidget->TakeWidget() : TSharedPtr<SWidget>();

	MyControlsHost = SNew(SMediaBoxControlsHost)
		.OwnerBox(this)
		.Video(Video)
		.Controls(Controls)
		.ControlsAlignment(ControlsAlignment.GetValue())
		.ControlsHeight(ControlsHeight)
		.ControlsMinHeight(ControlsMinHeight)
		.ControlsMaxHeight(ControlsMaxHeight)
		.ControlsPadding(ControlsPadding);

	// Shown as it first appears (YouTube shows its bar until the video plays).
	ControlsAlpha = 1.f;
	LastActivityTime = FPlatformTime::Seconds();
	ApplyControlsAlpha();
	return MyControlsHost.ToSharedRef();
}

void UMediaBoxWithControls::SynchronizeProperties()
{
	// Another Controls Class takes effect when the widget is next built (the designer rebuilds its preview on every change).
	Super::SynchronizeProperties();
	ApplyControlsAlpha();
}

void UMediaBoxWithControls::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	if (ControlsWidget && !IsUnreachable())
	{
		ControlsWidget->ReleaseSlateResources(bReleaseChildren);
	}
	MyControlsHost.Reset();
	bPointerHidden = false;
}

#if WITH_EDITOR
void UMediaBoxWithControls::ValidateCompiledDefaults(IWidgetCompilerLog& CompileLog) const
{
	Super::ValidateCompiledDefaults(CompileLog);
	if (!ResolveControlsClass())
	{
		CompileLog.Note(FText::Format(LOCTEXT("NoControlsNote", "{0}: no Controls Class, and no Default Controls Class in Project Settings > Plugins > Media Box - it shows no controls bar."), FText::FromString(GetName())));
	}
}
#endif

void UMediaBoxWithControls::ShowControls()
{
	LastActivityTime = FPlatformTime::Seconds();
}

bool UMediaBoxWithControls::HandleVideoClicked()
{
	if (!GetResolvedControlsBehavior().bClickToPlayPause)
	{
		return false;
	}
	TogglePlayPause();
	ShowControls();
	return true;
}

void UMediaBoxWithControls::TickMediaBox(float DeltaTime)
{
	Super::TickMediaBox(DeltaTime);

	if (!MyControlsHost.IsValid())
	{
		return;
	}

	// The bar follows the box from here too: in the designer a User Widget's own tick may not run, and the bar should still show
	// the time, the scrub position and the rest as the preview plays (it updates once a frame, whoever asks first).
	if (ControlsWidget)
	{
		ControlsWidget->RefreshFromMediaBox(DeltaTime);
	}

	const FMediaBoxControlsBehavior Behavior = GetResolvedControlsBehavior();
	const double Now = FPlatformTime::Seconds();

	// The pointer: over the video, moving.
	const bool bHovered = MyControlsHost->IsHovered();
	if (FSlateApplication::IsInitialized())
	{
		const FVector2f Pointer = FVector2f(FVector2D(FSlateApplication::Get().GetCursorPos()));
		if (bHovered && !Pointer.Equals(LastPointerPosition, 0.5f))
		{
			LastActivityTime = Now;
		}
		LastPointerPosition = Pointer;
	}
	if (bWasHovered && !bHovered)
	{
		LastUnhoverTime = Now;
	}
	if (!bWasHovered && bHovered)
	{
		LastActivityTime = Now;
	}
	bWasHovered = bHovered;

	// Started, paused, played on: shown for a moment so the change is seen.
	const EMediaBoxStatus CurrentStatus = GetStatus();
	if (CurrentStatus != LastSeenStatus)
	{
		if (Behavior.bShowOnPlaybackChange && LastSeenStatus != EMediaBoxStatus::Empty)
		{
			LastActivityTime = Now;
		}
		LastSeenStatus = CurrentStatus;
	}

	bool bShow = true;
	if (Behavior.bAutoHide && !IsDesignTime())
	{
		const bool bPlaying = CurrentStatus == EMediaBoxStatus::Playing;
		if (!bPlaying)
		{
			bShow = CurrentStatus == EMediaBoxStatus::Paused ? Behavior.bShowWhilePaused : Behavior.bShowWhileStopped;
		}
		else
		{
			const bool bOverControls = Behavior.bShowWhileOverControls && ControlsWidget && ControlsWidget->IsHovered();
			const bool bFocused = Behavior.bShowWhileFocused && ControlsWidget && (ControlsWidget->HasAnyUserFocus() || ControlsWidget->HasFocusedDescendants());
			const bool bRecent = Now - LastActivityTime < Behavior.HideAfterIdle;
			if (bOverControls || bFocused)
			{
				bShow = true;
			}
			else if (bHovered)
			{
				bShow = Behavior.HideAfterIdle <= 0.f || bRecent;
			}
			else
			{
				// Left: hidden Hide After Unhovered after leaving - or sooner, once it would have hidden for being idle.
				bShow = Now - LastUnhoverTime < Behavior.HideAfterUnhovered && (Behavior.HideAfterIdle <= 0.f || bRecent);
			}
		}
	}

	// Faded toward shown or hidden.
	const float PreviousAlpha = ControlsAlpha;
	if (bShow)
	{
		ControlsAlpha = Behavior.FadeInTime > 0.f ? FMath::Min(1.f, ControlsAlpha + DeltaTime / Behavior.FadeInTime) : 1.f;
	}
	else
	{
		ControlsAlpha = Behavior.FadeOutTime > 0.f ? FMath::Max(0.f, ControlsAlpha - DeltaTime / Behavior.FadeOutTime) : 0.f;
	}

	// The subtitles kept above the bar while it shows.
	const float NewInset = Behavior.bLiftSubtitles && ControlsWidget && ControlsAlignment == VAlign_Bottom
		? (MyControlsHost->GetControlsHeight() + static_cast<float>(ControlsPadding.Bottom)) * ControlsAlpha
		: 0.f;
	if (!FMath::IsNearlyEqual(NewInset, SubtitleBottomInset))
	{
		SubtitleBottomInset = NewInset;
		InvalidateLayoutAndVolatility();
	}

	// The pointer hidden with the controls, while over the video.
	const bool bHidePointer = Behavior.bHidePointer && bHovered && ControlsAlpha <= 0.f && !IsDesignTime();
	if (bHidePointer != bPointerHidden)
	{
		bPointerHidden = bHidePointer;
		MyControlsHost->SetCursor(bHidePointer ? TOptional<EMouseCursor::Type>(EMouseCursor::None) : TOptional<EMouseCursor::Type>());
	}

	if (ControlsAlpha != PreviousAlpha)
	{
		ApplyControlsAlpha();
	}
}

void UMediaBoxWithControls::ApplyControlsAlpha()
{
	if (!ControlsWidget)
	{
		return;
	}
	ControlsWidget->SetRenderOpacity(ControlsAlpha);

	// Hidden once faded out, so it takes no clicks; its own visibility when shown.
	const ESlateVisibility Wanted = ControlsAlpha > 0.f ? ControlsShownVisibility : ESlateVisibility::Hidden;
	if (ControlsWidget->GetVisibility() != Wanted)
	{
		ControlsWidget->SetVisibility(Wanted);
	}
}

#undef LOCTEXT_NAMESPACE
