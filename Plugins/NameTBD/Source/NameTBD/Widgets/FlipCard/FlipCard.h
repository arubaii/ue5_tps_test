// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/NamedSlotInterface.h"
#include "Components/Widget.h"
#include "Layout/Children.h"
#include "Widgets/SPanel.h"
#include "NameTBD/Widgets/ShapeBox.h"
#include "NameTBD/Widgets/WidgetMotion.h"
#include "NameTBD/Widgets/Shapes/WidgetShapeGeometry.h"

#include "FlipCard.generated.h"

/** How a Flip Card goes from one face to the other. Each moves differently - not the same turn at another speed. */
UENUM(BlueprintType)
enum class EFlipCardMotion : uint8
{
	Flip UMETA(ToolTip = "Turns over sideways on its upright axis, in perspective: the near edge grows, the far one shrinks, the back comes round."),
	FlipVertical UMETA(DisplayName = "Flip Vertical", ToolTip = "Tips over top to bottom on its level axis, in perspective."),
	SpringFlip UMETA(DisplayName = "Spring Flip", ToolTip = "Turns over on a spring: overshoots past the back, swings back and forth and settles."),
	LiftAndFlip UMETA(DisplayName = "Lift & Flip", ToolTip = "Like a card picked up off a table: rises toward you (bigger, its shadow spreading and softening), turns over at the top, settles back down."),
	Swing UMETA(ToolTip = "Like a door: the front swings away on its left edge, then the back swings in on its right."),
	CoinSpin UMETA(DisplayName = "Coin Spin", ToolTip = "Spins one and a half turns, fast then slowing, landing on the other face."),
	FadeThrough UMETA(DisplayName = "Fade Through", ToolTip = "No turn: the front fades and shrinks away, then the back fades in and grows into place."),
	Shuffle UMETA(ToolTip = "Like a deck: the front slides out to the side and tucks underneath, leaving the back on top.")
};

/** What flips a Flip Card on its own. */
UENUM(BlueprintType)
enum class EFlipCardTrigger : uint8
{
	None UMETA(DisplayName = "Blueprint Only", ToolTip = "Only Flip, Flip To Back, Flip To Front and Set Show Back."),
	Click UMETA(ToolTip = "A click (or tap) on the card turns it over. A button on a face takes its own clicks."),
	Hover UMETA(ToolTip = "The back shows while the pointer is over the card - a product or info card.")
};

/** A Flip Card's settings, as plain data for Slate. */
struct FFlipCardSettings
{
	EFlipCardMotion Motion = EFlipCardMotion::Flip;
	float EnterDuration = 0.6f;
	float ExitDuration = 0.6f;
	bool bReverseOnReturn = true;
	float Perspective = 0.35f;
	float Lift = 0.08f;
	float Shading = 0.35f;
	float Glare = 0.2f;
	float CornerRadius = 12.f;
	EFlipCardTrigger Trigger = EFlipCardTrigger::Click;
	bool bTiltOnHover = false;
	float MaxTilt = 10.f;
	float TiltGlare = 0.25f;
	bool bCastShadow = true;
	FLinearColor ShadowColor = FLinearColor(0.f, 0.f, 0.f, 0.3f);
	float ShadowBlur = 24.f;
	FVector2D ShadowOffset = FVector2D(0.0, 10.0);
};

DECLARE_DELEGATE_OneParam(FOnFlipCardRequest, bool /*bToBack*/);

/**
 * The Slate side of a Flip Card: the two faces, each in a Shape Box that turns it in perspective (drawn offscreen only while
 * it is turned; at rest it is drawn as it is), with the turn's shading, glare and shadow drawn on and under them.
 */
class NAMETBD_API SFlipCard : public SPanel
{
public:
	SLATE_BEGIN_ARGS(SFlipCard) {}
		SLATE_EVENT(FOnFlipCardRequest, OnRequestFlip)
		SLATE_EVENT(FOnFlipCardRequest, OnFlipFinished)
	SLATE_END_ARGS()

	SFlipCard();

	void Construct(const FArguments& InArgs);

	void SetFrontContent(const TSharedRef<SWidget>& InContent);
	void SetBackContent(const TSharedRef<SWidget>& InContent);
	void SetSettings(const FFlipCardSettings& InSettings);

	/** Turns to the back (or the front). bAnimate false shows it at once. */
	void SetShowBack(bool bBack, bool bAnimate);
	bool IsShowingBack() const { return bShowingBack; }

	/** Where it is between the faces: 0 the front, 1 the back. */
	float GetProgress() const;

	virtual FChildren* GetChildren() override { return &Children; }
	virtual void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;

protected:
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

private:
	/** How one face is shown this frame. */
	struct FFacePose
	{
		bool bVisible = true;
		float Yaw = 0.f;
		float Pitch = 0.f;
		FVector2D Translation = FVector2D::ZeroVector;
		float Scale = 1.f;
		float Roll = 0.f;
		float Opacity = 1.f;
		float Shade = 0.f;
		float Glare = 0.f;
		bool bGlareFromRight = true;
		float Lift = 0.f;
	};

	void ApplyPose();
	void Wake();
	EActiveTimerReturnType Animate(double InCurrentTime, float InDeltaTime);
	bool IsTurnMotion() const;
	EWidgetMotion GetCurve() const;
	FShapeBoxShape MakeFaceShape(const FFacePose& Pose) const;
	void PaintFace(int32 FaceIndex, const FArrangedWidget& Face, const FPaintArgs& Args, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32& InOutLayer, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const;
	void PaintShadow(const FArrangedWidget& Face, const FFacePose& Pose, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle) const;

	TSlotlessChildren<SWidget> Children;
	TSharedPtr<SShapeBox> FrontFace;
	TSharedPtr<SShapeBox> BackFace;
	FOnFlipCardRequest OnRequestFlip;
	FOnFlipCardRequest OnFlipFinished;

	FFlipCardSettings Settings;
	bool bShowingBack = false;
	bool bPressedHere = false;

	/** The turn, in half turns (0 the front, 1 the back, 2 the front again) - or 0..1 for the motions that don't turn. */
	FWidgetMotionValue Turn;
	float TurnFrom = 0.f;
	FWidgetMotionValue TiltX;
	FWidgetMotionValue TiltY;
	FWidgetMotionValue HoverAmount;
	FVector2f PointerUV = FVector2f(0.5f, 0.5f);
	TSharedPtr<FActiveTimerHandle> AnimateTimer;

	FFacePose Poses[2];
	bool bBackOnTop = false;
	mutable FVector2D LastSize = FVector2D(1.0, 1.0);
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFlipCardFlipped, bool, bShowingBack);

/**
 * Flip Card: a card with a front and a back (named slots - drop any widgets in), turned over with a choice of motions that
 * each look different: a perspective flip sideways or over the top, a springy flip, a lift-and-turn, a door swing, a coin
 * spin, a fade-through and a deck shuffle. Shading, glare and a shadow follow the turn; it can tilt toward the pointer with a
 * moving highlight. Flips on click, on hover, or from Blueprint.
 */
UCLASS(meta = (DisplayName = "Flip Card"))
class NAMETBD_API UFlipCard : public UWidget, public INamedSlotInterface
{
	GENERATED_BODY()

public:
	UFlipCard(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Showing the back. Also what the designer shows. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flip Card", meta = (DisplayName = "Show Back"))
	bool bShowBack = false;

	/** How it turns over. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flip Card")
	EFlipCardMotion FlipMotion = EFlipCardMotion::Flip;

	/** Seconds to turn to the back. 0: the motion's own pace. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flip Card", meta = (ClampMin = "0", UIMax = "3", Units = "s"))
	float EnterDuration = 0.6f;

	/** Seconds to turn back to the front. 0: the motion's own pace. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flip Card", meta = (ClampMin = "0", UIMax = "3", Units = "s"))
	float ExitDuration = 0.6f;

	/** Turns back the way it came. Off, a turning motion carries on round the same way, as a card turned over twice does. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flip Card")
	bool bReverseOnReturn = true;

	/** What flips it on its own. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flip Card")
	EFlipCardTrigger Trigger = EFlipCardTrigger::Click;

	/** How strongly the turn is foreshortened: 0 flat, 1 strong. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look", meta = (ClampMin = "0", ClampMax = "1"))
	float Perspective = 0.35f;

	/** Lift & Flip: how much bigger it gets at the top of the turn (0.08 is 8%). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look", meta = (EditCondition = "FlipMotion == EFlipCardMotion::LiftAndFlip", EditConditionHides, ClampMin = "0", ClampMax = "0.5"))
	float Lift = 0.08f;

	/** How dark a face gets as it turns away edge-on. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look", meta = (ClampMin = "0", ClampMax = "1"))
	float Shading = 0.35f;

	/** A light sweeping over the face as it turns. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look", meta = (ClampMin = "0", ClampMax = "1"))
	float Glare = 0.2f;

	/** The faces' corner rounding - what the shading, glare and shadow are shaped to (round the faces' own backgrounds to match). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look", meta = (ClampMin = "0", UIMax = "48", ForceUnits = "px"))
	float CornerRadius = 12.f;

	/** Tilts toward the pointer while hovered, a highlight following it - a holographic trading card. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tilt")
	bool bTiltOnHover = false;

	/** How far it tilts at the card's edge, in degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tilt", meta = (EditCondition = "bTiltOnHover", EditConditionHides, ClampMin = "0", ClampMax = "30", ForceUnits = "Degrees"))
	float MaxTilt = 10.f;

	/** The highlight under the pointer while tilting. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tilt", meta = (EditCondition = "bTiltOnHover", EditConditionHides, ClampMin = "0", ClampMax = "1"))
	float TiltGlare = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shadow")
	bool bCastShadow = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shadow", meta = (EditCondition = "bCastShadow", EditConditionHides))
	FLinearColor ShadowColor = FLinearColor(0.f, 0.f, 0.f, 0.3f);

	/** How soft (a CSS blur radius), in Slate units. It spreads as the card lifts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shadow", meta = (EditCondition = "bCastShadow", EditConditionHides, ClampMin = "0", UIMax = "96", ForceUnits = "px"))
	float ShadowBlur = 24.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shadow", meta = (EditCondition = "bCastShadow", EditConditionHides))
	FVector2D ShadowOffset = FVector2D(0.0, 10.0);

	/** It started turning: to the back (true) or the front. */
	UPROPERTY(BlueprintAssignable, Category = "Flip Card")
	FOnFlipCardFlipped OnFlipStarted;

	/** It finished turning: showing the back (true) or the front. */
	UPROPERTY(BlueprintAssignable, Category = "Flip Card")
	FOnFlipCardFlipped OnFlipFinished;

	/** Turns it over to the other face. */
	UFUNCTION(BlueprintCallable, Category = "Flip Card")
	void Flip(bool bAnimate = true);

	UFUNCTION(BlueprintCallable, Category = "Flip Card")
	void FlipToBack(bool bAnimate = true);

	UFUNCTION(BlueprintCallable, Category = "Flip Card")
	void FlipToFront(bool bAnimate = true);

	UFUNCTION(BlueprintCallable, Category = "Flip Card")
	void SetShowBack(bool bBack, bool bAnimate = true);

	/** Showing (or turning to) the back. */
	UFUNCTION(BlueprintPure, Category = "Flip Card")
	bool IsShowingBack() const { return bShowBack; }

	/** Where it is between the faces now: 0 the front, 1 the back. */
	UFUNCTION(BlueprintPure, Category = "Flip Card")
	float GetFlipProgress() const;

	// INamedSlotInterface
	virtual void GetSlotNames(TArray<FName>& SlotNames) const override;
	virtual UWidget* GetContentForSlot(FName SlotName) const override;
	virtual void SetContentForSlot(FName SlotName, UWidget* Content) override;

	// UWidget
	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif

protected:
	UPROPERTY()
	TObjectPtr<UWidget> FrontContent;

	UPROPERTY()
	TObjectPtr<UWidget> BackContent;

	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	void HandleRequestFlip(bool bToBack);
	void HandleFlipFinished(bool bBack);
	FFlipCardSettings MakeSettings() const;

	TSharedPtr<SFlipCard> MyCard;
};
