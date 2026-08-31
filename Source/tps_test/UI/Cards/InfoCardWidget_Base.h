#pragma once

#include "CoreMinimal.h"
#include "UI/WidgetAnimation/UserWidget_Animatable.h"
#include "GameplayTagContainer.h"
#include "UI/Cards/InfoCardTypes.h"
#include "InfoCardWidget_Base.generated.h"

class UWidget;
class UMenuManagerComponent;

UCLASS()
class TPS_TEST_API UInfoCardWidgetBase : public UUserWidget_Animatable
{
	GENERATED_BODY()

public:

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "InfoCard")
	EInfoCardPosition PositionMode = EInfoCardPosition::ToIcon;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "InfoCard", meta = (EditCondition = "PositionMode == EInfoCardPosition::ToIcon"))
	EDirection2D IconPosition = EDirection2D::Up;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "InfoCard", meta = (EditCondition = "PositionMode == EInfoCardPosition::ToCursor"))
	EInfoCardCursorPosition CursorPosition = EInfoCardCursorPosition::UpRight;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "InfoCard")
	FVector2D Offset = FVector2D(0.0f, 0.0f);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "InfoCard")
	bool UpdatesPositionOnTick = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "InfoCard")
	float HoverAnchorPadding = 8.f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "InfoCard")
	bool bClampToViewport = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "InfoCard")
	bool bCloseOnMenuStateChanged = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "InfoCard")
	bool bCloseOnClickOutside = true;

	/** Now visible in the BP Details Panel so you can see/edit the trigger type per card */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "InfoCard")
	ECardType SourceCardType = ECardType::Hover;

	UPROPERTY(BlueprintReadWrite, Transient, Category = "InfoCard")
	FVector2D IconPositon = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Transient, Category = "InfoCard")
	FVector2D IconFrameSize = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Transient, Category = "InfoCard")
	bool IsFirstFrame = true;

	UPROPERTY(BlueprintReadWrite, Transient, Category = "InfoCard")
	float ElapsedTime = 0.f;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "InfoCard")
	TArray<TObjectPtr<UInfoCardPayload>> Payloads;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "InfoCard")
	FName CardId = NAME_None;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "InfoCard")
	TWeakObjectPtr<UWidget> AnchorWidget;

	UFUNCTION(BlueprintCallable, Category = "InfoCard")
	void SetupCard(const FInfoCardSpec& Spec, UWidget* InAnchorWidget);

	UFUNCTION(BlueprintCallable, Category = "InfoCard")
	void SetPayloads(const TArray<UInfoCardPayload*>& NewPayloads);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "InfoCard")
	void ApplyPayloads(const TArray<UInfoCardPayload*>& InPayloads);
	virtual void ApplyPayloads_Implementation(const TArray<UInfoCardPayload*>& InPayloads);

	UFUNCTION(BlueprintPure, Category = "InfoCard")
	UInfoCardPayload* GetPrimaryPayload() const;

	UFUNCTION(BlueprintPure, Category = "InfoCard")
	UInfoCardPayload* GetPayloadAtIndex(int32 Index) const;

	UFUNCTION(BlueprintCallable, Category = "InfoCard")
	void CloseCard();

	UFUNCTION(BlueprintPure, Category = "InfoCard")
	bool IsClosing() const;

	UFUNCTION(BlueprintCallable, Category = "InfoCard|Position")
	void RefreshAnchorGeometry();

	UFUNCTION(BlueprintCallable, Category = "InfoCard|Position")
	void UpdatePosition();

	UFUNCTION(BlueprintPure, Category = "InfoCard|Position")
	FVector2D GetTargetPosition() const;

	UFUNCTION(BlueprintPure, Category = "InfoCard|Position")
	FVector2D PositionToIcon(EDirection2D TryPosition, float OffsetX, float OffsetY) const;

	UFUNCTION(BlueprintPure, Category = "InfoCard|Position")
	FVector2D PositionToCursor(EInfoCardCursorPosition Position, float OffsetX, float OffsetY) const;

protected:

	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	virtual void OnOutAnimationFinished_Implementation() override;

	FVector2D GetCardDesiredSize() const;
	FVector2D ClampToViewport(const FVector2D& InPosition, const FVector2D& CardSize) const;
	FVector2D GetViewportSizeInViewportUnits() const;

	UFUNCTION(BlueprintPure, Category = "InfoCard")
	UMenuManagerComponent* ResolveMenuManagerComponent() const;

	UFUNCTION()
	void HandleMenuStateChanged(FGameplayTag ScreenTag, bool bIsOpen);

	UPROPERTY(Transient)
	TWeakObjectPtr<UMenuManagerComponent> BoundMenuManager;

	void TickClickOutsideToClose();

	UPROPERTY(Transient)
	bool bHasIgnoredSpawnClick = false;
};