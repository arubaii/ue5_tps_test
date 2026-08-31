#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "UI/WidgetAnimation/WidgetAnimLibrary.h"
#include "UserWidget_Animatable.generated.h"

UCLASS()
class TPS_TEST_API UUserWidget_Animatable : public UUserWidget
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	bool bPlayOnlyOnConstructAndDestruct = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (EditCondition = "!bPlayOnlyOnConstructAndDestruct"))
	bool bPlayInAnimationsOnConstruct = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (EditCondition = "!bPlayOnlyOnConstructAndDestruct"))
	bool bPlayAnimationsOnVisibilityHidden = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	TArray<FWidgetAnimSettings> AnimIn;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	TArray<FWidgetAnimSettings> AnimOut;

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	FWidgetAnimPlaybackState AnimPlayback;

	UFUNCTION(BlueprintCallable, Category = "Animation")
	void PlayIn(bool bCascadeToChildren = true);

	UFUNCTION(BlueprintCallable, Category = "Animation")
	void PlayOut(bool bCascadeToChildren = true);

	UFUNCTION(BlueprintPure, Category = "Animation")
	bool IsAnimClosing() const;

	UFUNCTION(BlueprintCallable, Category = "Animation")
	void RebuildEffectiveAnimations();

	virtual void SetVisibility(ESlateVisibility InVisibility) override;

	
protected:

	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UFUNCTION(BlueprintCallable, Category = "Animation")
	bool TickAnimationPlayback(float DeltaTime);

	UFUNCTION(BlueprintNativeEvent, Category = "Animation")
	void OnOutAnimationFinished();
	virtual void OnOutAnimationFinished_Implementation();

private:

	UPROPERTY(Transient)
	TArray<FWidgetAnimSettings> EffectiveInAnimations;

	UPROPERTY(Transient)
	TArray<FWidgetAnimSettings> EffectiveOutAnimations;

	UPROPERTY(Transient)
	bool bIsDeferringVisibility = false;

	UPROPERTY(Transient)
	ESlateVisibility DeferredVisibility = ESlateVisibility::Hidden;
	
	void CascadeAnimationToChildren(bool bPlayingIn);
};