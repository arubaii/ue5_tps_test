// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "NameTBD/WidgetAnimation/WidgetAnimHost.h"
#include "NameTBD/WidgetAnimation/WidgetAnimLibrary.h"
#include "NameTBD/WidgetAnimation/WidgetAnimationComponent.h"
#include "WidgetAnimScreenTransitions.h"

#include "WidgetAnimActivatableWidget.generated.h"

/**
 * A Common UI screen that animates with Widget Animations: Anim In as it is pushed, On Activate as it comes back
 * after the screen above it is popped, and Anim Out before it closes - the stack pops it once the exit has played.
 *
 * Use it as the parent class of a Widget Blueprint wherever you would use Common Activatable Widget. Anim In / Anim
 * Out are authored on the screen itself (its built-in Widget Animation Component, shown under Animation), and any
 * Widget Animation Component added inside it joins in through the cascade. Close it with Close Screen, the back
 * action (with Is Back Handler on), or the Pop / Remove Screen Animated nodes on its stack.
 *
 * Leave the component's Play Animations On Visibility Hidden off here: activation and closing already cover what it
 * does, and Common UI changes a screen's visibility as it activates and deactivates.
 *
 * For an activatable base class of your own, embed FWidgetAnimScreenTransitionDriver instead - this class is only
 * that driver plus the forwarding.
 */
UCLASS(Abstract, meta = (DisableNativeTick))
class NAMETBDCOMMONUI_API UWidgetAnimActivatableWidget : public UCommonActivatableWidget, public IWidgetAnimHost, public IWidgetAnimScreen
{
	GENERATED_BODY()

public:

	UWidgetAnimActivatableWidget(const FObjectInitializer& ObjectInitializer);

	/** Anim In / Anim Out, their flags and the designer preview, applied to this screen. */
	UPROPERTY(EditAnywhere, Instanced, NoClear, BlueprintReadOnly, Category = "Animation", meta = (DisplayName = "Widget Animation", ShowOnlyInnerProperties))
	TObjectPtr<UWidgetAnimationComponent> WidgetAnimationComponent;

	/** How this screen animates as it is activated and closed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Screen Transitions", meta = (ShowOnlyInnerProperties))
	FWidgetAnimScreenTransitionSettings ScreenTransitions;

	/**
	 * Plays Anim Out on this screen and everything inside it, then deactivates it, which is what makes its stack pop
	 * it. Does nothing while a close is already playing, or when the screen is not active (to take a covered screen
	 * off its stack, use Remove Screen Animated).
	 */
	UFUNCTION(BlueprintCallable, Category = "Animation|Screen Transitions")
	void CloseScreen();

	/** True while the exit is playing and the screen is waiting to deactivate. */
	UFUNCTION(BlueprintPure, Category = "Animation|Screen Transitions")
	bool IsScreenClosing() const;

	UFUNCTION(BlueprintCallable, Category = "Animation")
	void PlayIn(bool bCascadeToChildren = true);

	UFUNCTION(BlueprintCallable, Category = "Animation")
	void PlayOut(bool bCascadeToChildren = true);

	UFUNCTION(BlueprintPure, Category = "Animation")
	EWidgetAnimPhase GetAnimPhase() const;

	/** Kept alongside the component's own copy in case the Details panel resolves GetOptions on the widget. */
	UFUNCTION()
	TArray<FString> GetAnimTargetWidgetOptions() const;

	// IWidgetAnimHost
	virtual void PlayWidgetAnimationsById(const TArray<FName>& AnimIds, bool bReverse, float PlaybackSpeed, bool bHoldFinalPose, bool bRestartIfPlaying) override;
	virtual void StopWidgetAnimationsById(const TArray<FName>& AnimIds, bool bResetToStart) override;
	virtual bool AreWidgetAnimationsPlayingById(const TArray<FName>& AnimIds) const override;
	virtual bool SetWidgetAnimationValues(FName ValueName, const FVector4& Value, bool bSnap, bool bStartIfNotPlaying) override;
	virtual FVector4 GetWidgetAnimationValues(FName ValueName, bool bTargetValue) const override;

	// IWidgetAnimScreen
	virtual bool RequestAnimatedClose() override;
	virtual bool IsClosingAnimated() const override;

protected:

	// UUserWidget
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void SynchronizeProperties() override;
	virtual void OnWidgetRebuilt() override;

	// UCommonActivatableWidget
	virtual void NativeOnActivated() override;
	virtual void NativeOnDeactivated() override;

	/**
	 * The back action closes the screen with its exit, instead of deactivating it on the spot. A Blueprint that
	 * implements On Handle Back Action still decides for itself (and can call Close Screen). Back presses during
	 * an exit are swallowed.
	 */
	virtual bool NativeOnHandleBackAction() override;

	/** Fired when Close Screen starts an exit animation - not when the screen closes at once for want of one. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Animation|Screen Transitions", meta = (DisplayName = "On Close Started"))
	void BP_OnCloseStarted();

private:

	/** The component, bound to this widget. Binding is lazy because templates must never be bound. */
	UWidgetAnimationComponent& GetAnim() const;

	FWidgetAnimScreenTransitionDriver Transitions;

	/** Set while Super::NativeConstruct runs, which is where an Auto Activate screen activates. */
	bool bInNativeConstruct = false;
};