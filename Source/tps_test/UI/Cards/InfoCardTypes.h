#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Templates/SubclassOf.h"
#include "InfoCardTypes.generated.h"

class UInfoCardWidgetBase;

/**
 * Cardinal direction used to place the info card relative to an icon.
 */
UENUM(BlueprintType)
enum class EDirection2D : uint8
{
	Up,
	Down,
	Left,
	Right,
	UpLeft,
	UpRight,
	DownLeft,
	DownRight
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

/** How the card is triggered by the widget it is attached to. */
UENUM(BlueprintType)
enum class ECardType : uint8
{
	Hover,
	RightClick
};

/** What happens to other active info cards already on screen when this one is shown. */
UENUM(BlueprintType)
enum class ECardExclusivityScope : uint8
{
	None,
	SameSourceWidgetClass,
	AllCards
};


UCLASS(Abstract, Blueprintable, BlueprintType, EditInlineNew, DefaultToInstanced, CollapseCategories)
class TPS_TEST_API UInfoCardPayload : public UObject
{
	GENERATED_BODY()

public:
	virtual UWorld* GetWorld() const override;
};

UCLASS(DisplayName = "Info Card Data (Text)")
class TPS_TEST_API UInfoCardPayload_Text : public UInfoCardPayload
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text")
	FText Title;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text", meta = (MultiLine = true))
	FText Body;
};

USTRUCT(BlueprintType)
struct TPS_TEST_API FInfoCardSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card")
	FName CardId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card")
	TSubclassOf<UInfoCardWidgetBase> CardClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card")
	ECardType CardType = ECardType::Hover;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card", meta = (EditCondition = "CardType == ECardType::Hover", ClampMin = "0.0", UIMin = "0.0", UIMax = "2.0"))
	float HoverDelay = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card", meta = (EditCondition = "CardType == ECardType::RightClick"))
	bool bToggleOnRightClick = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card")
	ECardExclusivityScope ExclusivityScope = ECardExclusivityScope::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card|Placement")
	EInfoCardPosition PositionMode = EInfoCardPosition::ToIcon;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card|Placement", meta = (EditCondition = "PositionMode == EInfoCardPosition::ToIcon"))
	EDirection2D IconPosition = EDirection2D::Up;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card|Placement", meta = (EditCondition = "PositionMode == EInfoCardPosition::ToCursor"))
	EInfoCardCursorPosition CursorPosition = EInfoCardCursorPosition::UpRight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card|Placement")
	FVector2D Offset = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card|Placement")
	float HoverAnchorPadding = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card|Placement")
	bool bClampToViewport = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card|Placement")
	bool bUpdatePositionOnTick = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card|Placement")
	int32 ZOrder = 1000;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = "Info Card|Data")
	TArray<TObjectPtr<UInfoCardPayload>> Payloads;
};