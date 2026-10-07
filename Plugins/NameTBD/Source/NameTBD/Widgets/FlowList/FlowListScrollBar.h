// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SLeafWidget.h"

#include "FlowListScrollBar.generated.h"

class UFlowList;
class UFlowListScrollBar;

/** How a Flow List Scroll Bar shows where its list is. */
UENUM(BlueprintType)
enum class EFlowListScrollBarStyle : uint8
{
	Bar UMETA(ToolTip = "A track with a thumb: its length how much of the list is in view, its place where the list is. Drag the thumb, or click the track to go there."),
	Dots UMETA(ToolTip = "Page dots, one per item: the current one grows and lights as the list reaches it, gliding between dots as it moves. Click a dot to go to its item. A long list shows Max Dots at a time, the ones at the edges smaller.")
};

/**
 * Draws a Flow List Scroll Bar: reads its UMG widget's settings and its list live on every paint (as SWidgetShape does), and
 * turns drags and clicks into scrolling.
 */
class NAMETBD_API SFlowListScrollBar : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SFlowListScrollBar) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UFlowListScrollBar>, Owner)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** The list moved or changed: shown again if hidden, and repainted. */
	void NotifyActivity();

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;

private:
	struct FBarLayout
	{
		bool bVertical = true;
		float TrackStart = 0.f;
		float TrackLength = 0.f;
		float ThumbStart = 0.f;
		float ThumbLength = 0.f;
		float CrossStart = 0.f;
		float Thickness = 0.f;
	};

	bool IsVertical() const;
	FBarLayout MakeBarLayout(const FVector2f& Size) const;
	float GetShownOpacity() const;
	float FractionAt(const FGeometry& Geometry, const FVector2f& Absolute) const;
	void WakeUp();
	EActiveTimerReturnType OnAnimate(double InCurrentTime, float InDeltaTime);

	TWeakObjectPtr<UFlowListScrollBar> Owner;

	/** When the list last moved, for Auto Hide. */
	double LastActivityTime = -1.0e9;
	float HoverAmount = 0.f;
	bool bDragging = false;

	/** Where on the thumb a drag took hold of it, along the bar. */
	float GrabOffset = 0.f;

	/** The dots as last painted, item by item, for clicks. */
	mutable TArray<TPair<int32, FSlateRect>> DotRects;

	TSharedPtr<FActiveTimerHandle> ActiveTimer;
};

/**
 * A scroll bar or page dots for a Flow List - as Rive's Scroll Bar constraint follows a scrolling layout: drop it next to a
 * list and it follows the nearest one, or the one named Flow List Name. Styled with brushes, so a thumb can be an image, a
 * material or a rounded box; it can thicken under the pointer and hide while the list rests.
 */
UCLASS(meta = (DisplayName = "Flow List Scroll Bar"))
class NAMETBD_API UFlowListScrollBar : public UWidget
{
	GENERATED_BODY()

public:
	UFlowListScrollBar();

	/** The Flow List to follow, by its name in the Widget Blueprint. None: the nearest one (see UFlowList::FindNearestFlowList). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar", meta = (DisplayName = "Flow List Name"))
	FName FlowListName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar")
	EFlowListScrollBarStyle Style = EFlowListScrollBarStyle::Bar;

	/** Runs the way its list does. Off: Orientation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar", meta = (DisplayName = "Match List Orientation"))
	bool bMatchListOrientation = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar", meta = (EditCondition = "!bMatchListOrientation"))
	TEnumAsByte<EOrientation> Orientation = Orient_Vertical;

	/** Dragging the thumb, clicking the track or a dot scrolls the list. Off: it only shows where the list is. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar", meta = (DisplayName = "Interactive"))
	bool bInteractive = true;

	/** Fades out once the list has rested for Hide Delay, and back in as soon as it moves or the pointer comes over it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar", meta = (DisplayName = "Auto Hide"))
	bool bAutoHide = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar", meta = (ClampMin = "0", UIMax = "5", Units = "s", EditCondition = "bAutoHide"))
	float HideDelay = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar", meta = (ClampMin = "0", UIMax = "1", Units = "s", EditCondition = "bAutoHide"))
	float FadeTime = 0.25f;

	// Bar

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar|Bar", meta = (EditCondition = "Style == EFlowListScrollBarStyle::Bar", EditConditionHides))
	FSlateBrush TrackBrush;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar|Bar", meta = (EditCondition = "Style == EFlowListScrollBarStyle::Bar", EditConditionHides))
	FSlateBrush ThumbBrush;

	/** How thick the track and thumb are, across the bar, in Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar|Bar", meta = (ClampMin = "1", UIMax = "40", EditCondition = "Style == EFlowListScrollBarStyle::Bar", EditConditionHides))
	float Thickness = 5.f;

	/** How thick they grow while the pointer is over the bar or the thumb is dragged - easier to grab. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar|Bar", meta = (ClampMin = "1", UIMax = "40", EditCondition = "Style == EFlowListScrollBarStyle::Bar", EditConditionHides))
	float HoverThickness = 9.f;

	/** The shortest the thumb gets however long the list, so it can always be grabbed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar|Bar", meta = (ClampMin = "4", UIMax = "200", EditCondition = "Style == EFlowListScrollBarStyle::Bar", EditConditionHides))
	float MinThumbLength = 28.f;

	// Dots

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar|Dots", meta = (EditCondition = "Style == EFlowListScrollBarStyle::Dots", EditConditionHides))
	FSlateBrush DotBrush;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar|Dots", meta = (EditCondition = "Style == EFlowListScrollBarStyle::Dots", EditConditionHides))
	FVector2D DotSize = FVector2D(8.0, 8.0);

	/** The current item's dot: wider makes the pill most page indicators use. Dots grow into it as the list reaches them. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar|Dots", meta = (EditCondition = "Style == EFlowListScrollBarStyle::Dots", EditConditionHides))
	FVector2D ActiveDotSize = FVector2D(22.0, 8.0);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar|Dots", meta = (EditCondition = "Style == EFlowListScrollBarStyle::Dots", EditConditionHides))
	FLinearColor DotColor = FLinearColor(1.f, 1.f, 1.f, 0.35f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar|Dots", meta = (EditCondition = "Style == EFlowListScrollBarStyle::Dots", EditConditionHides))
	FLinearColor ActiveDotColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar|Dots", meta = (ClampMin = "0", UIMax = "40", EditCondition = "Style == EFlowListScrollBarStyle::Dots", EditConditionHides))
	float DotSpacing = 8.f;

	/** The most dots shown at once. A longer list shows the ones around the current item, shrinking those at the edges. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar|Dots", meta = (ClampMin = "3", UIMax = "30", EditCondition = "Style == EFlowListScrollBarStyle::Dots", EditConditionHides))
	int32 MaxDots = 9;

	/** Follows List instead of looking one up by name. Null goes back to looking. */
	UFUNCTION(BlueprintCallable, Category = "Scroll Bar")
	void SetFlowList(UFlowList* List);

	/** The list followed, found if it hasn't been yet. */
	UFUNCTION(BlueprintPure, Category = "Scroll Bar")
	UFlowList* GetFlowList() const;

	// UWidget
	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	void Bind(UFlowList* List) const;
	void Unbind() const;
	void HandleListUpdated() const;

	TSharedPtr<SFlowListScrollBar> MyBar;

	/** Set with Set Flow List. */
	TWeakObjectPtr<UFlowList> ExplicitList;

	/** The list followed and its OnListUpdated binding. Found lazily: a list placed after the bar isn't built yet when the bar is. */
	mutable TWeakObjectPtr<UFlowList> BoundList;
	mutable FDelegateHandle UpdatedHandle;
	mutable double LastSearchTime = -1.0e9;
};
