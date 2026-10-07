// Copyright AR GameSystems 2026. All rights reserved.

#include "FlowListScrollBar.h"

#include "FlowList.h"
#include "Rendering/DrawElements.h"

#define LOCTEXT_NAMESPACE "FlowListScrollBar"

namespace FlowListScrollBarPrivate
{
	/** A pill: a rounded box whose ends are half circles, whichever way it is long. */
	void MakePill(FSlateBrush& Brush, const FLinearColor& Color)
	{
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.TintColor = FSlateColor(Color);
		Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::HalfHeightRadius;
		Brush.OutlineSettings.Width = 0.f;
	}

	/** How often a bar with no list looks for one, in seconds - the list may be added later. */
	constexpr double SearchInterval = 0.5;
}

// =====================================================================================================
// Slate
// =====================================================================================================

void SFlowListScrollBar::Construct(const FArguments& InArgs)
{
	Owner = InArgs._Owner;
	SetCanTick(false);
	LastActivityTime = FPlatformTime::Seconds();
}

void SFlowListScrollBar::NotifyActivity()
{
	LastActivityTime = FPlatformTime::Seconds();
	Invalidate(EInvalidateWidgetReason::Paint);
	WakeUp();
}

bool SFlowListScrollBar::IsVertical() const
{
	const UFlowListScrollBar* Bar = Owner.Get();
	if (!Bar)
	{
		return true;
	}
	if (Bar->bMatchListOrientation)
	{
		if (const UFlowList* List = Bar->GetFlowList())
		{
			return List->ListLayout.Orientation == Orient_Vertical;
		}
	}
	return Bar->Orientation == Orient_Vertical;
}

float SFlowListScrollBar::GetShownOpacity() const
{
	const UFlowListScrollBar* Bar = Owner.Get();
	if (!Bar || !Bar->bAutoHide || bDragging || IsHovered() || Bar->IsDesignTime())
	{
		return 1.f;
	}
	const double Since = FPlatformTime::Seconds() - LastActivityTime;
	if (Since <= Bar->HideDelay)
	{
		return 1.f;
	}
	return Bar->FadeTime > 0.f ? FMath::Clamp(1.f - static_cast<float>((Since - Bar->HideDelay) / Bar->FadeTime), 0.f, 1.f) : 0.f;
}

SFlowListScrollBar::FBarLayout SFlowListScrollBar::MakeBarLayout(const FVector2f& Size) const
{
	FBarLayout Layout;
	const UFlowListScrollBar* Bar = Owner.Get();
	const UFlowList* List = Bar ? Bar->GetFlowList() : nullptr;
	Layout.bVertical = IsVertical();

	const float Length = Layout.bVertical ? Size.Y : Size.X;
	const float Cross = Layout.bVertical ? Size.X : Size.Y;
	Layout.Thickness = Bar ? FMath::Lerp(Bar->Thickness, Bar->HoverThickness, HoverAmount) : 5.f;
	Layout.Thickness = FMath::Min(Layout.Thickness, Cross);
	Layout.CrossStart = (Cross - Layout.Thickness) * 0.5f;
	Layout.TrackStart = 0.f;
	Layout.TrackLength = Length;

	const float View = List ? List->GetViewFraction() : 1.f;
	const float Fraction = List ? List->GetScrollFraction() : 0.f;
	const float MinThumb = Bar ? Bar->MinThumbLength : 20.f;
	Layout.ThumbLength = FMath::Clamp(FMath::Max(Length * View, MinThumb), 0.f, Length);
	Layout.ThumbStart = (Length - Layout.ThumbLength) * FMath::Clamp(Fraction, 0.f, 1.f);
	return Layout;
}

FVector2D SFlowListScrollBar::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	const UFlowListScrollBar* Bar = Owner.Get();
	if (!Bar)
	{
		return FVector2D(8.0, 64.0);
	}
	const bool bVertical = IsVertical();
	if (Bar->Style == EFlowListScrollBarStyle::Dots)
	{
		const UFlowList* List = Bar->GetFlowList();
		const int32 Count = FMath::Min(List ? List->GetItemCount() : 3, Bar->MaxDots);
		const double Along = Bar->ActiveDotSize.X + (Bar->DotSize.X + Bar->DotSpacing) * FMath::Max(Count - 1, 0);
		const double Across = FMath::Max(Bar->DotSize.Y, Bar->ActiveDotSize.Y);
		return bVertical ? FVector2D(Across, Along) : FVector2D(Along, Across);
	}
	const double Across = FMath::Max(Bar->Thickness, Bar->HoverThickness);
	return bVertical ? FVector2D(Across, 64.0) : FVector2D(64.0, Across);
}

int32 SFlowListScrollBar::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const UFlowListScrollBar* Bar = Owner.Get();
	if (!Bar)
	{
		return LayerId;
	}
	const float Opacity = GetShownOpacity();
	if (Opacity <= 0.f)
	{
		return LayerId;
	}

	const FVector2f Size(AllottedGeometry.GetLocalSize());
	const FLinearColor StyleTint = InWidgetStyle.GetColorAndOpacityTint();
	auto DrawBox = [&](const FSlateBrush& Brush, const FVector2f& Position, const FVector2f& BoxSize, const FLinearColor& Color, int32 Layer)
	{
		FLinearColor Tint = Color * StyleTint;
		Tint.A *= Opacity;
		FSlateDrawElement::MakeBox(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(BoxSize, FSlateLayoutTransform(Position)),
			&Brush, ESlateDrawEffect::None, Tint);
	};

	const UFlowList* List = Bar->GetFlowList();

	if (Bar->Style == EFlowListScrollBarStyle::Bar)
	{
		const FBarLayout Layout = MakeBarLayout(Size);
		auto Rect = [&Layout](float Start, float Length)
		{
			return Layout.bVertical
				? TPair<FVector2f, FVector2f>(FVector2f(Layout.CrossStart, Start), FVector2f(Layout.Thickness, Length))
				: TPair<FVector2f, FVector2f>(FVector2f(Start, Layout.CrossStart), FVector2f(Length, Layout.Thickness));
		};
		const TPair<FVector2f, FVector2f> Track = Rect(Layout.TrackStart, Layout.TrackLength);
		DrawBox(Bar->TrackBrush, Track.Key, Track.Value, Bar->TrackBrush.GetTint(InWidgetStyle), LayerId);
		if (List && List->GetItemCount() > 0)
		{
			const TPair<FVector2f, FVector2f> Thumb = Rect(Layout.ThumbStart, Layout.ThumbLength);
			DrawBox(Bar->ThumbBrush, Thumb.Key, Thumb.Value, Bar->ThumbBrush.GetTint(InWidgetStyle), LayerId + 1);
		}
		return LayerId + 1;
	}

	// Dots: each grows and lights as the list nears its item, gliding from one to the next between items.
	DotRects.Reset();
	const int32 Count = List ? List->GetItemCount() : (Bar->IsDesignTime() ? 5 : 0);
	if (Count <= 0)
	{
		return LayerId;
	}
	const bool bVertical = IsVertical();
	const bool bLoop = List && List->Scrolling.bLoop && Count > 1;
	const float Scroll = List ? List->GetScrollPosition() : 0.f;
	auto DistanceTo = [Count, bLoop, Scroll](int32 Index)
	{
		float Distance = static_cast<float>(Index) - Scroll;
		if (bLoop)
		{
			Distance = FMath::Fmod(Distance, static_cast<float>(Count));
			if (Distance < -Count * 0.5f)
			{
				Distance += Count;
			}
			else if (Distance >= Count * 0.5f)
			{
				Distance -= Count;
			}
		}
		return Distance;
	};

	// A long list shows the dots round the current item; those at the window's edges shrink to say there are more.
	const int32 Shown = FMath::Min(Count, FMath::Max(Bar->MaxDots, 3));
	int32 First = 0;
	if (Count > Shown)
	{
		int32 Center = FMath::RoundToInt(Scroll);
		Center = ((Center % Count) + Count) % Count;
		First = FMath::Clamp(Center - Shown / 2, 0, Count - Shown);
	}
	const int32 Last = First + Shown - 1;

	struct FDot
	{
		int32 Index;
		FVector2f Size;
		FLinearColor Color;
	};
	TArray<FDot, TInlineAllocator<32>> Dots;
	float Total = 0.f;
	for (int32 Index = First; Index <= Last; ++Index)
	{
		const float Active = FMath::Clamp(1.f - FMath::Abs(DistanceTo(Index)), 0.f, 1.f);
		float Edge = 1.f;
		if (Count > Shown)
		{
			const bool bMoreBefore = First > 0;
			const bool bMoreAfter = Last < Count - 1;
			if ((Index == First && bMoreBefore) || (Index == Last && bMoreAfter))
			{
				Edge = 0.5f;
			}
			else if ((Index == First + 1 && bMoreBefore) || (Index == Last - 1 && bMoreAfter))
			{
				Edge = 0.75f;
			}
		}
		FVector2f DotSize = FMath::Lerp(FVector2f(Bar->DotSize), FVector2f(Bar->ActiveDotSize), Active) * Edge;
		if (bVertical)
		{
			DotSize = FVector2f(DotSize.Y, DotSize.X);
		}
		const FLinearColor Color = FMath::Lerp(Bar->DotColor, Bar->ActiveDotColor, Active) * Bar->DotBrush.GetTint(InWidgetStyle);
		Dots.Add({ Index, DotSize, Color });
		Total += bVertical ? DotSize.Y : DotSize.X;
	}
	Total += Bar->DotSpacing * static_cast<float>(FMath::Max(Dots.Num() - 1, 0));

	const float Length = bVertical ? Size.Y : Size.X;
	const float Cross = bVertical ? Size.X : Size.Y;
	float Along = (Length - Total) * 0.5f;
	for (const FDot& Dot : Dots)
	{
		const float DotAlong = bVertical ? Dot.Size.Y : Dot.Size.X;
		const float DotCross = bVertical ? Dot.Size.X : Dot.Size.Y;
		const FVector2f Position = bVertical
			? FVector2f((Cross - DotCross) * 0.5f, Along)
			: FVector2f(Along, (Cross - DotCross) * 0.5f);
		DrawBox(Bar->DotBrush, Position, Dot.Size, Dot.Color, LayerId);

		// A little more than the dot itself, so small dots are easy to hit.
		const FVector2f Pad(4.f, 4.f);
		DotRects.Emplace(Dot.Index, FSlateRect(Position - Pad, Position + Dot.Size + Pad));
		Along += DotAlong + Bar->DotSpacing;
	}
	return LayerId;
}

float SFlowListScrollBar::FractionAt(const FGeometry& Geometry, const FVector2f& Absolute) const
{
	const FVector2f Local(Geometry.AbsoluteToLocal(Absolute));
	const FBarLayout Layout = MakeBarLayout(FVector2f(Geometry.GetLocalSize()));
	const float Along = (Layout.bVertical ? Local.Y : Local.X) - GrabOffset;
	const float Room = Layout.TrackLength - Layout.ThumbLength;
	return Room > 0.f ? FMath::Clamp((Along - Layout.TrackStart) / Room, 0.f, 1.f) : 0.f;
}

FReply SFlowListScrollBar::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	UFlowListScrollBar* Bar = Owner.Get();
	UFlowList* List = Bar ? Bar->GetFlowList() : nullptr;
	if (!Bar || !List || !Bar->bInteractive || Bar->IsDesignTime() || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}

	const FVector2f Local(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()));
	if (Bar->Style == EFlowListScrollBarStyle::Dots)
	{
		for (const TPair<int32, FSlateRect>& Dot : DotRects)
		{
			if (Dot.Value.ContainsPoint(FVector2D(Local)))
			{
				List->ScrollToIndex(Dot.Key, true);
				NotifyActivity();
				return FReply::Handled();
			}
		}
		return FReply::Unhandled();
	}

	// On the thumb: held where it was taken. On the track: the thumb jumps under the pointer and is held by its middle.
	const FBarLayout Layout = MakeBarLayout(FVector2f(MyGeometry.GetLocalSize()));
	const float Along = Layout.bVertical ? Local.Y : Local.X;
	const bool bOnThumb = Along >= Layout.ThumbStart && Along <= Layout.ThumbStart + Layout.ThumbLength;
	GrabOffset = bOnThumb ? Along - Layout.ThumbStart : Layout.ThumbLength * 0.5f;
	bDragging = true;
	List->SetScrollFraction(FractionAt(MyGeometry, FVector2f(MouseEvent.GetScreenSpacePosition())), /*bAnimate=*/!bOnThumb);
	NotifyActivity();
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SFlowListScrollBar::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bDragging)
	{
		return FReply::Unhandled();
	}
	UFlowListScrollBar* Bar = Owner.Get();
	if (UFlowList* List = Bar ? Bar->GetFlowList() : nullptr)
	{
		List->SetScrollFraction(FractionAt(MyGeometry, FVector2f(MouseEvent.GetScreenSpacePosition())), /*bAnimate=*/false);
	}
	NotifyActivity();
	return FReply::Handled();
}

FReply SFlowListScrollBar::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bDragging)
	{
		return FReply::Unhandled();
	}
	bDragging = false;

	// Let go between items: a snapping list settles on the nearest.
	UFlowListScrollBar* Bar = Owner.Get();
	if (UFlowList* List = Bar ? Bar->GetFlowList() : nullptr)
	{
		List->SnapToNearestItem();
	}
	NotifyActivity();
	return FReply::Handled().ReleaseMouseCapture();
}

void SFlowListScrollBar::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	SLeafWidget::OnMouseCaptureLost(CaptureLostEvent);
	if (bDragging)
	{
		bDragging = false;
		UFlowListScrollBar* Bar = Owner.Get();
		if (UFlowList* List = Bar ? Bar->GetFlowList() : nullptr)
		{
			List->SnapToNearestItem();
		}
		NotifyActivity();
	}
}

void SFlowListScrollBar::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SLeafWidget::OnMouseEnter(MyGeometry, MouseEvent);
	WakeUp();
}

void SFlowListScrollBar::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SLeafWidget::OnMouseLeave(MouseEvent);
	WakeUp();
}

void SFlowListScrollBar::WakeUp()
{
	if (!ActiveTimer.IsValid())
	{
		ActiveTimer = RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SFlowListScrollBar::OnAnimate));
	}
}

EActiveTimerReturnType SFlowListScrollBar::OnAnimate(double InCurrentTime, float InDeltaTime)
{
	// The bar thickens under the pointer and while held, and - with Auto Hide - fades once the list rests.
	const bool bThick = IsHovered() || bDragging;
	const float Goal = bThick ? 1.f : 0.f;
	const float Step = FMath::Clamp(InDeltaTime / 0.12f, 0.f, 1.f);
	HoverAmount = HoverAmount < Goal ? FMath::Min(HoverAmount + Step, Goal) : FMath::Max(HoverAmount - Step, Goal);

	const UFlowListScrollBar* Bar = Owner.Get();
	const float Opacity = GetShownOpacity();
	const bool bFading = Bar && Bar->bAutoHide && Opacity > 0.f && !bThick;
	Invalidate(EInvalidateWidgetReason::Paint);

	if (HoverAmount != Goal || bFading)
	{
		return EActiveTimerReturnType::Continue;
	}
	ActiveTimer.Reset();
	return EActiveTimerReturnType::Stop;
}

// =====================================================================================================
// UMG
// =====================================================================================================

UFlowListScrollBar::UFlowListScrollBar()
{
	FlowListScrollBarPrivate::MakePill(TrackBrush, FLinearColor(1.f, 1.f, 1.f, 0.08f));
	FlowListScrollBarPrivate::MakePill(ThumbBrush, FLinearColor(1.f, 1.f, 1.f, 0.6f));
	FlowListScrollBarPrivate::MakePill(DotBrush, FLinearColor::White);
}

TSharedRef<SWidget> UFlowListScrollBar::RebuildWidget()
{
	MyBar = SNew(SFlowListScrollBar).Owner(this);
	return MyBar.ToSharedRef();
}

void UFlowListScrollBar::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	// The name may have changed: look again.
	Unbind();
	LastSearchTime = -1.0e9;
	if (MyBar.IsValid())
	{
		MyBar->NotifyActivity();
	}
}

void UFlowListScrollBar::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	Unbind();
	MyBar.Reset();
}

void UFlowListScrollBar::SetFlowList(UFlowList* List)
{
	ExplicitList = List;
	Unbind();
	LastSearchTime = -1.0e9;
	if (MyBar.IsValid())
	{
		MyBar->NotifyActivity();
	}
}

UFlowList* UFlowListScrollBar::GetFlowList() const
{
	if (UFlowList* Bound = BoundList.Get())
	{
		return Bound;
	}

	UFlowList* Found = ExplicitList.Get();
	if (!Found)
	{
		// Not too often: a bar with no list near would otherwise search the tree every paint.
		const double Now = FPlatformTime::Seconds();
		if (Now - LastSearchTime < FlowListScrollBarPrivate::SearchInterval)
		{
			return nullptr;
		}
		LastSearchTime = Now;
		Found = UFlowList::FindNearestFlowList(this, FlowListName);
	}
	if (Found)
	{
		Bind(Found);
	}
	return Found;
}

void UFlowListScrollBar::Bind(UFlowList* List) const
{
	Unbind();
	BoundList = List;
	UpdatedHandle = List->OnListUpdated.AddUObject(this, &UFlowListScrollBar::HandleListUpdated);
}

void UFlowListScrollBar::Unbind() const
{
	if (UFlowList* List = BoundList.Get())
	{
		List->OnListUpdated.Remove(UpdatedHandle);
	}
	BoundList.Reset();
	UpdatedHandle.Reset();
}

void UFlowListScrollBar::HandleListUpdated() const
{
	if (MyBar.IsValid())
	{
		MyBar->NotifyActivity();
	}
}

#if WITH_EDITOR
const FText UFlowListScrollBar::GetPaletteCategory()
{
	return LOCTEXT("PaletteCategory", "NameTBD");
}
#endif

#undef LOCTEXT_NAMESPACE
