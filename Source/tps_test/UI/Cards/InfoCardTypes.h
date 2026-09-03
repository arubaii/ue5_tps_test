#pragma once

#include "CoreMinimal.h"
#include "Inventory/Structs/Instance.h"
#include "UObject/Object.h"
#include "Templates/SubclassOf.h"
#include "InfoCardTypes.generated.h"

class UInfoCardWidgetBase;
class UInventoryComponent;

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

/** Whether the card anchors itself to an icon, a fixed cursor-relative position, or a fixed viewport-relative position. */
UENUM(BlueprintType)
enum class EInfoCardPosition : uint8
{
	ToIcon,
	ToCursor,
	ToViewport
};

/** Fixed corner/edge of the viewport used to place the info card when PositionMode is ToViewport. */
UENUM(BlueprintType)
enum class EViewportPosition : uint8
{
	TopLeft,
	TopCenter,
	TopRight,
	LeftCenter,
	RightCenter,
	BottomLeft,
	BottomCenter,
	BottomRight
};

/** How the card is triggered by the widget it is attached to. */
UENUM(BlueprintType)
enum class ECardType : uint8
{
	Hover,
	RightClick,
	LeftClick
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

/**
 * Drives any card's optional InventoryDisplay child (see UInfoCardWidgetBase)
 * from a live UInventoryComponent. UInfoCardWidgetBase handles the refresh and
 * the "close if the source changes" safety net generically from this payload
 * alone — no per-card C++/Blueprint work needed. Attach this to any InfoCards
 * entry whose card is meant to preview a component's slots (attachments, a
 * recipe's owned-resources view, a shop's stock, etc.).
 */
UCLASS(DisplayName = "Info Card Data (Inventory Display)")
class TPS_TEST_API UInfoCardPayload_InventoryDisplay : public UInfoCardPayload
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory Display")
	TObjectPtr<UInventoryComponent> SourceComponent = nullptr;
};

/**
 * Carries the row name a card should look up its own display data from —
 * separate from UInfoCardPayload_InventoryDisplay, since a card can show
 * "this item's own name/icon/description" (this payload) and "this item's
 * attachments/contents" (the display payload) independently, or both at
 * once in the same Payloads array.
 */
UCLASS(DisplayName = "Info Card Data (Item Row)")
class TPS_TEST_API UInfoCardPayload_ItemRow : public UInfoCardPayload
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Row")
	FName ItemRowName = NAME_None;
};

UCLASS(DisplayName = "Info Card Data (Item Instance)")
class TPS_TEST_API UInfoCardPayload_ItemInstance : public UInfoCardPayload
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	FItemInstance ItemInstance;
};

UCLASS(DisplayName = "Info Card Data (Item Definition)")
class TPS_TEST_API UInfoCardPayload_ItemDefinition : public UInfoCardPayload
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	FItemInstance ItemInstance;
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card", meta = (EditCondition = "CardType == ECardType::LeftClick"))
	bool bToggleOnLeftClick = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card")
	ECardExclusivityScope ExclusivityScope = ECardExclusivityScope::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card|Placement")
	EInfoCardPosition PositionMode = EInfoCardPosition::ToIcon;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card|Placement", meta = (EditCondition = "PositionMode == EInfoCardPosition::ToIcon"))
	EDirection2D IconPosition = EDirection2D::Up;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card|Placement", meta = (EditCondition = "PositionMode == EInfoCardPosition::ToCursor"))
	EInfoCardCursorPosition CursorPosition = EInfoCardCursorPosition::UpRight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card|Placement", meta = (EditCondition = "PositionMode == EInfoCardPosition::ToViewport"))
	EViewportPosition ViewportPosition = EViewportPosition::TopRight;

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