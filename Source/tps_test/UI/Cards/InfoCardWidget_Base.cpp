#include "UI/Cards/InfoCardWidget_Base.h"

#include "Inventory/Components/InventoryComponent.h"
#include "Inventory/UI/W_InventoryDisplayOnly_Base.h" 
#include "Menu/MenuManagerComponent.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/SlateBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Components/CanvasPanelSlot.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/KismetMathLibrary.h"

void UInfoCardWidgetBase::SetupCard(const FInfoCardSpec& Spec, UWidget* InAnchorWidget)
{
	PositionMode          = Spec.PositionMode;
	IconPosition          = Spec.IconPosition;
	CursorPosition        = Spec.CursorPosition;
	ViewportPosition      = Spec.ViewportPosition;
	Offset                = Spec.Offset;
	HoverAnchorPadding    = Spec.HoverAnchorPadding;
	bClampToViewport      = Spec.bClampToViewport;
	UpdatesPositionOnTick = Spec.bUpdatePositionOnTick;

	AnchorWidget    = InAnchorWidget;
	SourceCardType  = Spec.CardType;
	CardId          = Spec.CardId;

	IsFirstFrame          = true;
	ElapsedTime           = 0.f;
	bHasIgnoredSpawnClick = false;

	RefreshAnchorGeometry();

	TArray<UInfoCardPayload*> RawPayloads;
	RawPayloads.Reserve(Spec.Payloads.Num());
	for (const TObjectPtr<UInfoCardPayload>& P : Spec.Payloads)
	{
		RawPayloads.Add(P.Get());
	}
	SetPayloads(RawPayloads);
}

void UInfoCardWidgetBase::SetPayloads(const TArray<UInfoCardPayload*>& NewPayloads)
{
	Payloads.Reset(NewPayloads.Num());
	for (UInfoCardPayload* P : NewPayloads)
	{
		Payloads.Add(P);
	}

	ApplyPayloads(NewPayloads);
}

void UInfoCardWidgetBase::ApplyPayloads_Implementation(const TArray<UInfoCardPayload*>& InPayloads)
{
	// Generic, opt-in behaviour: if this card's WBP has an InventoryDisplay
	// widget bound and one of its payloads is a component-preview payload,
	// wire it up automatically. Cards with neither simply no-op here — this
	// costs nothing to a card that doesn't use it.
	if (!InventoryDisplay)
	{
		return;
	}

	const UInfoCardPayload_InventoryDisplay* DisplayPayload = nullptr;
	for (const UInfoCardPayload* Payload : InPayloads)
	{
		DisplayPayload = Cast<UInfoCardPayload_InventoryDisplay>(Payload);
		if (DisplayPayload)
		{
			break;
		}
	}

	BindInventoryDisplay(DisplayPayload ? DisplayPayload->SourceComponent : nullptr);
}

void UInfoCardWidgetBase::BindInventoryDisplay(UInventoryComponent* Component)
{
	UnbindInventoryDisplay();

	if (!InventoryDisplay)
	{
		return;
	}

	if (!Component)
	{
		InventoryDisplay->ClearDisplay();
		return;
	}

	BoundInventoryComponent = Component;
	Component->OnInventoryChanged.AddDynamic(this, &UInfoCardWidgetBase::HandleBoundInventoryChanged);
	InventoryDisplay->RefreshFromComponent(Component);
}

void UInfoCardWidgetBase::UnbindInventoryDisplay()
{
	if (UInventoryComponent* Component = BoundInventoryComponent.Get())
	{
		Component->OnInventoryChanged.RemoveDynamic(this, &UInfoCardWidgetBase::HandleBoundInventoryChanged);
	}
	BoundInventoryComponent = nullptr;
}

void UInfoCardWidgetBase::HandleBoundInventoryChanged(int32 InventoryIndex)
{
	// Deliberately not trying to figure out whether this particular change is
	// relevant to us (e.g. the item moved to a different component, an
	// unrelated slot changed, etc.) — that's unknowable from here without a
	// cross-component transfer notification the inventory system doesn't
	// currently expose. Closing rather than risking a stale or wrong display
	// is the safe default; the next hover/right-click rebuilds the card fresh
	// from whatever's actually true at that point.
	CloseCard();
}

UInfoCardPayload* UInfoCardWidgetBase::GetPrimaryPayload() const
{
	return Payloads.IsValidIndex(0) ? Payloads[0].Get() : nullptr;
}

UInfoCardPayload* UInfoCardWidgetBase::GetPayloadAtIndex(int32 Index) const
{
	return Payloads.IsValidIndex(Index) ? Payloads[Index].Get() : nullptr;
}

void UInfoCardWidgetBase::CloseCard()
{
	if (IsClosing())
	{
		return;
	}

	if (UMenuManagerComponent* MenuManager = BoundMenuManager.Get())
	{
		MenuManager->OnMenuStateChanged.RemoveDynamic(this, &UInfoCardWidgetBase::HandleMenuStateChanged);
	}
	BoundMenuManager = nullptr;

	PlayOut();
}

bool UInfoCardWidgetBase::IsClosing() const
{
	return IsAnimClosing();
}

void UInfoCardWidgetBase::NativeConstruct()
{
	Super::NativeConstruct();

	IsFirstFrame = true;
	ElapsedTime  = 0.f;

	if (bCloseOnMenuStateChanged)
	{
		if (UMenuManagerComponent* MenuManager = ResolveMenuManagerComponent())
		{
			MenuManager->OnMenuStateChanged.AddDynamic(this, &UInfoCardWidgetBase::HandleMenuStateChanged);
			BoundMenuManager = MenuManager;
		}
	}
}

void UInfoCardWidgetBase::NativeDestruct()
{
	if (UMenuManagerComponent* MenuManager = BoundMenuManager.Get())
	{
		MenuManager->OnMenuStateChanged.RemoveDynamic(this, &UInfoCardWidgetBase::HandleMenuStateChanged);
	}
	BoundMenuManager = nullptr;

	UnbindInventoryDisplay();

	Super::NativeDestruct();
}

void UInfoCardWidgetBase::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	UUserWidget::NativeTick(MyGeometry, InDeltaTime);

	ElapsedTime += InDeltaTime;

	if (UpdatesPositionOnTick || IsFirstFrame)
	{
		UpdatePosition();
	}

	IsFirstFrame = false;

	TickClickOutsideToClose();
	TickAnimationPlayback(InDeltaTime);
}

void UInfoCardWidgetBase::OnOutAnimationFinished_Implementation()
{
    Super::OnOutAnimationFinished_Implementation();
	RemoveFromParent();
}

UMenuManagerComponent* UInfoCardWidgetBase::ResolveMenuManagerComponent() const
{
	APlayerController* PC = GetOwningPlayer();
	if (!PC)
	{
		return nullptr;
	}

	if (APawn* Pawn = PC->GetPawn())
	{
		if (UMenuManagerComponent* FromPawn = Pawn->FindComponentByClass<UMenuManagerComponent>())
		{
			return FromPawn;
		}
	}

	return PC->FindComponentByClass<UMenuManagerComponent>();
}

void UInfoCardWidgetBase::HandleMenuStateChanged(FGameplayTag ScreenTag, bool bIsOpen)
{
	CloseCard();
}

void UInfoCardWidgetBase::TickClickOutsideToClose()
{
	if (!bCloseOnClickOutside || IsClosing())
	{
		return;
	}

	APlayerController* PC = GetOwningPlayer();
	if (!PC)
	{
		return;
	}

	const bool bRightClicked  = PC->WasInputKeyJustPressed(EKeys::RightMouseButton);
	const bool bLeftClicked   = PC->WasInputKeyJustPressed(EKeys::LeftMouseButton);
	const bool bMiddleClicked = PC->WasInputKeyJustPressed(EKeys::MiddleMouseButton);

	// The click that spawned a right/left-click card is also, in the same
	// frame, a click "outside" the card (it hasn't been laid out under the
	// cursor yet) — without this, the card would close itself the instant
	// it opens. Only the matching button for this card's own trigger type
	// is ever ignored, and only once.
	const bool bSpawnClickForThisCardType =
		(bRightClicked && SourceCardType == ECardType::RightClick) ||
		(bLeftClicked && SourceCardType == ECardType::LeftClick);

	if (bSpawnClickForThisCardType && !bHasIgnoredSpawnClick)
	{
		bHasIgnoredSpawnClick = true;
		return;
	}

	if (!bRightClicked && !bLeftClicked && !bMiddleClicked)
	{
		return;
	}

	if (!GetCachedGeometry().IsUnderLocation(UWidgetLayoutLibrary::GetMousePositionOnPlatform()))
	{
		CloseCard();
	}
}

FVector2D UInfoCardWidgetBase::GetCardDesiredSize() const
{
	return GetDesiredSize();
}

FVector2D UInfoCardWidgetBase::GetViewportSizeInViewportUnits() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return FVector2D::ZeroVector;
	}

	const float ViewportScale = FMath::Max(UWidgetLayoutLibrary::GetViewportScale(World), KINDA_SMALL_NUMBER);
	return UWidgetLayoutLibrary::GetViewportSize(World) / ViewportScale;
}

void UInfoCardWidgetBase::RefreshAnchorGeometry()
{
	UWidget* Anchor = AnchorWidget.Get();
	if (!Anchor || !Anchor->GetCachedWidget().IsValid())
	{
		return;
	}

	const FGeometry& AnchorGeometry = Anchor->GetCachedGeometry();

	FVector2D PixelScratch  = FVector2D::ZeroVector;
	FVector2D ViewportTL    = FVector2D::ZeroVector;
	FVector2D ViewportBR    = FVector2D::ZeroVector;

	USlateBlueprintLibrary::LocalToViewport(this, AnchorGeometry, FVector2D::ZeroVector, PixelScratch, ViewportTL);
	USlateBlueprintLibrary::LocalToViewport(this, AnchorGeometry, AnchorGeometry.GetLocalSize(), PixelScratch, ViewportBR);

	IconPositon   = ViewportTL;
	IconFrameSize = ViewportBR - ViewportTL;
}

void UInfoCardWidgetBase::UpdatePosition()
{
	if (PositionMode == EInfoCardPosition::ToIcon)
	{
		RefreshAnchorGeometry();
	}

	ForceLayoutPrepass();

	SetPositionInViewport(GetTargetPosition(), /*bRemoveDPIScale=*/false);

	if (IsFirstFrame)
	{
		if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
		{
			CanvasSlot->SetAutoSize(true);
		}
	}
}

FVector2D UInfoCardWidgetBase::GetTargetPosition() const
{
	switch (PositionMode)
	{
	case EInfoCardPosition::ToIcon:
		return PositionToIcon(IconPosition, Offset.X, Offset.Y);

	case EInfoCardPosition::ToCursor:
		return PositionToCursor(CursorPosition, Offset.X, Offset.Y);

	case EInfoCardPosition::ToViewport:
		return PositionToViewport(ViewportPosition, Offset.X, Offset.Y);

	default:
		return FVector2D::ZeroVector;
	}
}

FVector2D UInfoCardWidgetBase::ClampToViewport(const FVector2D& InPosition, const FVector2D& CardSize) const
{
	if (!bClampToViewport)
	{
		return InPosition;
	}

	const FVector2D ViewportSize = GetViewportSizeInViewportUnits();
	if (ViewportSize.IsNearlyZero())
	{
		return InPosition;
	}

	FVector2D Result = InPosition;
	Result.X = FMath::Clamp(Result.X, 0.0, FMath::Max(0.0, ViewportSize.X - CardSize.X));
	Result.Y = FMath::Clamp(Result.Y, 0.0, FMath::Max(0.0, ViewportSize.Y - CardSize.Y));
	return Result;
}

FVector2D UInfoCardWidgetBase::PositionToIcon(EDirection2D TryPosition, float OffsetX, float OffsetY) const
{
	const FVector2D CardSize   = GetCardDesiredSize();
	const FVector2D IconCenter = IconPositon + (IconFrameSize * 0.5f);

	FVector2D Result = FVector2D::ZeroVector;

	switch (TryPosition)
	{
	case EDirection2D::Up:
		Result.X = IconCenter.X - (CardSize.X * 0.5f) + OffsetX;
		Result.Y = IconPositon.Y - CardSize.Y - HoverAnchorPadding - OffsetY;
		break;

	case EDirection2D::Down:
		Result.X = IconCenter.X - (CardSize.X * 0.5f) + OffsetX;
		Result.Y = IconPositon.Y + IconFrameSize.Y + HoverAnchorPadding + OffsetY;
		break;

	case EDirection2D::Left:
		Result.X = IconPositon.X - CardSize.X - HoverAnchorPadding - OffsetX;
		Result.Y = IconCenter.Y - (CardSize.Y * 0.5f) + OffsetY;
		break;

	case EDirection2D::Right:
		Result.X = IconPositon.X + IconFrameSize.X + HoverAnchorPadding + OffsetX;
		Result.Y = IconCenter.Y - (CardSize.Y * 0.5f) + OffsetY;
		break;

	case EDirection2D::UpLeft:
		Result.X = IconPositon.X - (CardSize.X * 0.5f) - HoverAnchorPadding - OffsetX;
		Result.Y = IconPositon.Y - (CardSize.Y * 0.5f) - HoverAnchorPadding - OffsetY;
		break;

	case EDirection2D::UpRight:
		Result.X = IconPositon.X + IconFrameSize.X - (CardSize.X * 0.5f) + HoverAnchorPadding + OffsetX;
		Result.Y = IconPositon.Y - (CardSize.Y * 0.5f) - HoverAnchorPadding - OffsetY;
		break;

	case EDirection2D::DownLeft:
		Result.X = IconPositon.X - (CardSize.X * 0.5f) - HoverAnchorPadding - OffsetX;
		Result.Y = IconPositon.Y + IconFrameSize.Y - (CardSize.Y * 0.5f) + HoverAnchorPadding + OffsetY;
		break;

	case EDirection2D::DownRight:
		Result.X = IconPositon.X + IconFrameSize.X - (CardSize.X * 0.5f) + HoverAnchorPadding + OffsetX;
		Result.Y = IconPositon.Y + IconFrameSize.Y - (CardSize.Y * 0.5f) + HoverAnchorPadding + OffsetY;
		break;
	}

	return ClampToViewport(Result, CardSize);
}

FVector2D UInfoCardWidgetBase::PositionToCursor(EInfoCardCursorPosition Position, float OffsetX, float OffsetY) const
{
	const FVector2D CardSize = GetCardDesiredSize();

	FVector2D CursorPos = FVector2D::ZeroVector;
	if (UWorld* World = GetWorld())
	{
		CursorPos = UWidgetLayoutLibrary::GetMousePositionOnViewport(World);
	}

	FVector2D Result = CursorPos;

	switch (Position)
	{
	case EInfoCardCursorPosition::UpRight:
		Result.X = CursorPos.X + OffsetX;
		Result.Y = CursorPos.Y - CardSize.Y - OffsetY;
		break;

	case EInfoCardCursorPosition::UpLeft:
		Result.X = CursorPos.X - CardSize.X - OffsetX;
		Result.Y = CursorPos.Y - CardSize.Y - OffsetY;
		break;

	case EInfoCardCursorPosition::DownRight:
		Result.X = CursorPos.X + OffsetX;
		Result.Y = CursorPos.Y + OffsetY;
		break;

	case EInfoCardCursorPosition::DownLeft:
		Result.X = CursorPos.X - CardSize.X - OffsetX;
		Result.Y = CursorPos.Y + OffsetY;
		break;

	case EInfoCardCursorPosition::UpCenter:
		Result.X = CursorPos.X - (CardSize.X * 0.5f) + OffsetX;
		Result.Y = CursorPos.Y - CardSize.Y - OffsetY;
		break;

	case EInfoCardCursorPosition::DownCenter:
		Result.X = CursorPos.X - (CardSize.X * 0.5f) + OffsetX;
		Result.Y = CursorPos.Y + OffsetY;
		break;

	case EInfoCardCursorPosition::LeftCenter:
		Result.X = CursorPos.X - CardSize.X - OffsetX;
		Result.Y = CursorPos.Y - (CardSize.Y * 0.5f) + OffsetY;
		break;

	case EInfoCardCursorPosition::RightCenter:
		Result.X = CursorPos.X + OffsetX;
		Result.Y = CursorPos.Y - (CardSize.Y * 0.5f) + OffsetY;
		break;
	}

	return ClampToViewport(Result, CardSize);
}

FVector2D UInfoCardWidgetBase::PositionToViewport(EViewportPosition Position, float OffsetX, float OffsetY) const
{
	const FVector2D CardSize     = GetCardDesiredSize();
	const FVector2D ViewportSize = GetViewportSizeInViewportUnits();

	FVector2D Result = FVector2D::ZeroVector;

	switch (Position)
	{
	case EViewportPosition::TopLeft:
		Result.X = OffsetX;
		Result.Y = OffsetY;
		break;

	case EViewportPosition::TopCenter:
		Result.X = (ViewportSize.X * 0.5f) - (CardSize.X * 0.5f) + OffsetX;
		Result.Y = OffsetY;
		break;

	case EViewportPosition::TopRight:
		Result.X = ViewportSize.X - CardSize.X - OffsetX;
		Result.Y = OffsetY;
		break;

	case EViewportPosition::LeftCenter:
		Result.X = OffsetX;
		Result.Y = (ViewportSize.Y * 0.5f) - (CardSize.Y * 0.5f) + OffsetY;
		break;

	case EViewportPosition::RightCenter:
		Result.X = ViewportSize.X - CardSize.X - OffsetX;
		Result.Y = (ViewportSize.Y * 0.5f) - (CardSize.Y * 0.5f) + OffsetY;
		break;

	case EViewportPosition::BottomLeft:
		Result.X = OffsetX;
		Result.Y = ViewportSize.Y - CardSize.Y - OffsetY;
		break;

	case EViewportPosition::BottomCenter:
		Result.X = (ViewportSize.X * 0.5f) - (CardSize.X * 0.5f) + OffsetX;
		Result.Y = ViewportSize.Y - CardSize.Y - OffsetY;
		break;

	case EViewportPosition::BottomRight:
		Result.X = ViewportSize.X - CardSize.X - OffsetX;
		Result.Y = ViewportSize.Y - CardSize.Y - OffsetY;
		break;
	}

	// Viewport-relative positions are already computed to fit within the
	// viewport bounds, but ClampToViewport is still applied for consistency
	// with PositionToIcon/PositionToCursor and to guard against a card
	// larger than the viewport itself.
	return ClampToViewport(Result, CardSize);
}