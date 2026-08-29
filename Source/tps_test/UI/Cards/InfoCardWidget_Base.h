
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/WidgetAnimation/WidgetAnimLibrary.h"
#include "InfoCardWidget_Base.generated.h"


/** Cardinal direction used to place the info card relative to an icon. */
UENUM(BlueprintType)
enum class EDirection2D : uint8
{
    Up,
    Down,
    Left,
    Right
};

/** Corner/edge used to place the info card relative to the mouse cursor. */
UENUM(BlueprintType)
enum class EInfoCardCursorPosition : uint8
{
    UpRight,
    DownRight,
    UpLeft,
    DownLeft,
    UpCenter,
    DownCenter,
    LeftCenter,
    RightCenter
};

/** Whether the card anchors itself to an icon or to a fixed cursor-relative position. */
UENUM(BlueprintType)
enum class EInfoCardPosition : uint8
{
    ToIcon,
    ToCursor
};

/**
 * Base class for W_InfoCard. Provides the two positioning helpers that are
 * painful to author as Blueprint graphs: positioning the card relative to
 * an icon (PositionToIcon) and positioning it relative to the mouse cursor
 * (PositionToCursor).
 */
UCLASS()
class TPS_TEST_API UInfoCardWidgetBase : public UUserWidget
{
    GENERATED_BODY()

public:
    
    // =============================================================================================================
    // Config
    // =============================================================================================================
    
    /** Determines whether the card anchors to an icon or to a cursor-relative position. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "InfoCard")
    EInfoCardPosition PositionMode = EInfoCardPosition::ToIcon;

    /** Direction used when PositionMode is ToIcon. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "InfoCard", meta = (EditCondition = "PositionMode == EInfoCardPosition::ToIcon"))
    EDirection2D IconPosition = EDirection2D::Up;

    /** Corner/edge used when PositionMode is ToPosition. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "InfoCard", meta = (EditCondition = "PositionMode == EInfoCardPosition::ToCursor"))
    EInfoCardCursorPosition CursorPosition = EInfoCardCursorPosition::UpRight;

    /** Positional offset from info card */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "InfoCard")
    FVector2D Offset = FVector2D(0.0f, 0.0f);
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "InfoCard")
    bool UpdatesPositionOnTick = true; 
    
    /** Add/edit entries here in the Details panel to layer fade/scale/slide/rotate/pulse effects. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InfoCard | Animation")
    TArray<FWidgetAnimSettings> Animations;

    /** Runtime-only playback state, index-parallel with Animations. Don't edit by hand. */
    UPROPERTY(BlueprintReadWrite, Category = "InfoCard | Animation")
    TArray<FWidgetAnimState> AnimStates;
    
    // =============================================================================================================
    // =============================================================================================================
    
    
    /** Top-left viewport position of the icon this card is attached to. */
    UPROPERTY(BlueprintReadWrite, Category = "InfoCard")
    FVector2D IconPositon = FVector2D::ZeroVector;

    /** Size (in viewport pixels) of the icon's frame/box. */
    UPROPERTY(BlueprintReadWrite, Category = "InfoCard")
    FVector2D IconFrameSize = FVector2D::ZeroVector;

    /** True on the first Tick after the card becomes visible/constructed. */
    UPROPERTY(BlueprintReadWrite, Category = "InfoCard")
    bool IsFirstFrame = true;

    /** Accumulated time since the card started showing (driven from Tick). */
    UPROPERTY(BlueprintReadWrite, Category = "InfoCard")
    float ElapsedTime = 0.f;

    /**
     * Gap, in pixels, kept between the card and the icon/cursor it is
     * anchored to. Exposed so designers can tweak it without touching code.
     */
    UPROPERTY(BlueprintReadWrite, Category = "InfoCard")
    float AnchorPadding = 8.f;

    /**
     * If true, PositionToIcon will clamp the result so the card stays fully
     * within the viewport instead of running off the edge of the screen.
     */
    UPROPERTY(BlueprintReadWrite, Category = "InfoCard")
    bool bClampToViewport = true;

    // ------------------------------------------------------------------
    // Positional functions
    // ------------------------------------------------------------------

    /**
     * Computes the viewport-space position this card should be placed at so
     * that it sits directly Up/Down/Left/Right of the icon (IconPositon +
     * IconFrameSIze), using this widget's own desired size so it doesn't
     * overlap the icon. Optionally clamped to stay on-screen.
     */
    UFUNCTION(BlueprintPure, Category = "InfoCard|Position")
    FVector2D PositionToIcon(EDirection2D TryPosition, float OffsetX, float OffsetY) const;

    /**
     * Computes the viewport-space position this card should be placed at so
     * that the given corner/edge of the card sits Offset pixels away from
     * the current mouse cursor position.
     */
    UFUNCTION(BlueprintPure, Category = "InfoCard|Position")
    FVector2D PositionToCursor(EInfoCardCursorPosition Position, float OffsetX, float OffsetY) const;

protected:

    /** Returns this widget's current desired (rendered) size in pixels. */
    FVector2D GetCardDesiredSize() const;
};