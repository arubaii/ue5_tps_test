// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "WidgetAnimLibrary.h"
#include "WidgetAnimPreset.h"
#include "WidgetAnimHost.h"
#include "WidgetAnimationComponent.h"
#include "UserWidget_Animatable.generated.h"

/**
 * A User Widget that animates ITSELF through a built-in UWidgetAnimationComponent.
 *
 * New widgets don't need this: select any widget in the designer and use Add Component > Widget
 * Animation Component instead. This class stays so widgets already built on it keep their authored
 * animations and their Blueprint graphs, and so a widget that wants the whole User Widget animated (rather
 * than something inside it) has a one-step way to get that.
 */
UCLASS()
class NAMETBD_API UUserWidget_Animatable : public UUserWidget, public IWidgetAnimHost
{
	GENERATED_BODY()

public:

	UUserWidget_Animatable(const FObjectInitializer& ObjectInitializer);

	/** Anim In / Anim Out, their flags and the designer preview, applied to this widget. */
	UPROPERTY(EditAnywhere, Instanced, NoClear, BlueprintReadOnly, Category = "Animation", meta = (DisplayName = "Widget Animation", ShowOnlyInnerProperties))
	TObjectPtr<UWidgetAnimationComponent> WidgetAnimationComponent;

	UFUNCTION(BlueprintCallable, Category = "Animation")
	void PlayIn(bool bCascadeToChildren = true);

	UFUNCTION(BlueprintCallable, Category = "Animation")
	void PlayOut(bool bCascadeToChildren = true);

	UFUNCTION(BlueprintPure, Category = "Animation")
	bool IsAnimClosing() const;

	UFUNCTION(BlueprintPure, Category = "Animation")
	EWidgetAnimPhase GetAnimPhase() const;

	UFUNCTION(BlueprintCallable, Category = "Animation")
	void RebuildEffectiveAnimations();

	/** See UWidgetAnimationComponent::SetExtraAnimSettings. */
	UFUNCTION(BlueprintCallable, Category = "Animation")
	void SetExtraAnimSettings(const TArray<FWidgetAnimSettings>& InExtraAnimIn, const TArray<FWidgetAnimSettings>& InExtraAnimOut);

	/** Kept alongside the component's own copy in case the Details panel resolves GetOptions on the widget. */
	UFUNCTION()
	TArray<FString> GetAnimTargetWidgetOptions() const;

	// IWidgetAnimHost
	virtual void PlayWidgetAnimationsById(const TArray<FName>& AnimIds, bool bReverse, float PlaybackSpeed, bool bHoldFinalPose, bool bRestartIfPlaying) override;
	virtual void StopWidgetAnimationsById(const TArray<FName>& AnimIds, bool bResetToStart) override;
	virtual bool AreWidgetAnimationsPlayingById(const TArray<FName>& AnimIds) const override;
	virtual bool SetWidgetAnimationValues(FName ValueName, const FVector4& Value, bool bSnap, bool bStartIfNotPlaying) override;
	virtual FVector4 GetWidgetAnimationValues(FName ValueName, bool bTargetValue) const override;

	/** Routed into the component, so hiding this widget still plays Anim Out first. */
	virtual void SetVisibility(ESlateVisibility InVisibility) override;

	virtual void Serialize(FArchive& Ar) override;
	virtual void PostLoad() override;

protected:

	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void SynchronizeProperties() override;
	virtual void OnWidgetRebuilt() override;

	UFUNCTION(BlueprintCallable, Category = "Animation")
	bool TickAnimationPlayback(float DeltaTime);

	/** Fired when the exit animation finishes. Any visibility deferred by a hide has already been applied. */
	UFUNCTION(BlueprintNativeEvent, Category = "Animation")
	void OnOutAnimationFinished();
	virtual void OnOutAnimationFinished_Implementation();

	/** The widget an entry's Target Widget name refers to, or null to mean "this widget". */
	UWidget* ResolveAnimTarget(FName TargetWidgetName) const;

private:

	/** The component, bound to this widget. Binding is lazy because templates must never be bound. */
	UWidgetAnimationComponent& GetAnim() const;

	void HandleComponentOutFinished(UWidgetAnimationComponent* Component);

	FDelegateHandle OutFinishedHandle;

#if WITH_EDITORONLY_DATA
	// Where these settings lived before they moved into the component. Loaded from old packages through
	// CoreRedirects, read once in PostLoad, never saved again (the _DEPRECATED suffix makes them CPF_Deprecated).
	UPROPERTY()
	bool bPlayOnlyOnConstructAndDestruct_DEPRECATED = false;

	UPROPERTY()
	bool bPlayInAnimationsOnConstruct_DEPRECATED = true;

	UPROPERTY()
	bool bPlayAnimationsOnVisibilityHidden_DEPRECATED = false;

	UPROPERTY()
	TArray<FWidgetAnimEntry> AnimIn_DEPRECATED;

	UPROPERTY()
	TArray<FWidgetAnimEntry> AnimOut_DEPRECATED;

	UPROPERTY()
	bool bPreserveAuthoredTransforms_DEPRECATED = false;

	UPROPERTY()
	FWidgetAnimPreviewSettings DesignerPreview_DEPRECATED;

	FWidgetAnimationLegacySettings GatherLegacyWidgetAnimationSettings() const;
#endif
};