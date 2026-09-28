// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimActivatableWidget.h"

UWidgetAnimActivatableWidget::UWidgetAnimActivatableWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	WidgetAnimationComponent = ObjectInitializer.CreateDefaultSubobject<UWidgetAnimationComponent>(this, TEXT("WidgetAnimationComponent"));
}

UWidgetAnimationComponent& UWidgetAnimActivatableWidget::GetAnim() const
{
	check(WidgetAnimationComponent);

	if (!IsTemplate() && WidgetAnimationComponent->GetAnimatedWidget() != this)
	{
		// const_cast: binding is bookkeeping on the component, not a change to this widget.
		WidgetAnimationComponent->BindToWidget(const_cast<UWidgetAnimActivatableWidget*>(this));
	}

	return *WidgetAnimationComponent;
}

// =====================================================================================================
// Screen transitions
// =====================================================================================================

void UWidgetAnimActivatableWidget::CloseScreen()
{
	RequestAnimatedClose();
}

bool UWidgetAnimActivatableWidget::IsScreenClosing() const
{
	return Transitions.IsClosing();
}

bool UWidgetAnimActivatableWidget::RequestAnimatedClose()
{
	const bool bWasClosing = Transitions.IsClosing();

	// Bound before the driver looks for the screen's components, so it finds this screen's own one as the root.
	GetAnim();

	const bool bAnimating = Transitions.RequestClose(*this, ScreenTransitions);
	if (bAnimating && !bWasClosing)
	{
		BP_OnCloseStarted();
	}

	return bAnimating;
}

bool UWidgetAnimActivatableWidget::IsClosingAnimated() const
{
	return Transitions.IsClosing();
}

void UWidgetAnimActivatableWidget::NativeOnActivated()
{
	Super::NativeOnActivated();

	// An Auto Activate screen activates from inside NativeConstruct, before its component has been told it is
	// constructed. That activation is the screen opening, whose entrance is the Anim In the component starts on
	// construct a moment later, so there is nothing for the transition to add.
	if (bInNativeConstruct)
	{
		return;
	}

	GetAnim();
	Transitions.NotifyActivated(*this, ScreenTransitions);
}

void UWidgetAnimActivatableWidget::NativeOnDeactivated()
{
	Transitions.NotifyDeactivated(*this);

	Super::NativeOnDeactivated();
}

bool UWidgetAnimActivatableWidget::NativeOnHandleBackAction()
{
	// Already leaving: a second back press must not deactivate the screen from under its own exit.
	if (Transitions.IsClosing())
	{
		return true;
	}

	// A Blueprint override of On Handle Back Action is the author taking over the back action; the base class then
	// runs it as usual. Looked up by name because the event is protected in Common UI.
	static const FName BlueprintBackHandlerName(TEXT("BP_OnHandleBackAction"));
	if (GetClass()->IsFunctionImplementedInScript(BlueprintBackHandlerName))
	{
		return Super::NativeOnHandleBackAction();
	}

	// This is only reached with Is Back Handler on, which is Common UI's own switch for "back closes this screen".
	RequestAnimatedClose();
	return true;
}

// =====================================================================================================
// Pass-throughs
// =====================================================================================================

void UWidgetAnimActivatableWidget::PlayIn(bool bCascadeToChildren)
{
	GetAnim().PlayIn(bCascadeToChildren);
}

void UWidgetAnimActivatableWidget::PlayOut(bool bCascadeToChildren)
{
	GetAnim().PlayOut(bCascadeToChildren);
}

EWidgetAnimPhase UWidgetAnimActivatableWidget::GetAnimPhase() const
{
	return GetAnim().GetAnimPhase();
}

TArray<FString> UWidgetAnimActivatableWidget::GetAnimTargetWidgetOptions() const
{
	return GetAnim().GetAnimTargetWidgetOptions();
}

void UWidgetAnimActivatableWidget::PlayWidgetAnimationsById(const TArray<FName>& AnimIds, bool bReverse, float PlaybackSpeed, bool bHoldFinalPose, bool bRestartIfPlaying)
{
	GetAnim().PlayWidgetAnimationsById(AnimIds, bReverse, PlaybackSpeed, bHoldFinalPose, bRestartIfPlaying);
}

void UWidgetAnimActivatableWidget::StopWidgetAnimationsById(const TArray<FName>& AnimIds, bool bResetToStart)
{
	GetAnim().StopWidgetAnimationsById(AnimIds, bResetToStart);
}

bool UWidgetAnimActivatableWidget::AreWidgetAnimationsPlayingById(const TArray<FName>& AnimIds) const
{
	return GetAnim().AreWidgetAnimationsPlayingById(AnimIds);
}

bool UWidgetAnimActivatableWidget::SetWidgetAnimationValues(FName ValueName, const FVector4& Value, bool bSnap, bool bStartIfNotPlaying)
{
	return GetAnim().SetWidgetAnimationValues(ValueName, Value, bSnap, bStartIfNotPlaying);
}

FVector4 UWidgetAnimActivatableWidget::GetWidgetAnimationValues(FName ValueName, bool bTargetValue) const
{
	return GetAnim().GetWidgetAnimationValues(ValueName, bTargetValue);
}

// =====================================================================================================
// Lifecycle
// =====================================================================================================

void UWidgetAnimActivatableWidget::NativeConstruct()
{
	GetAnim();

	{
		TGuardValue<bool> Guard(bInNativeConstruct, true);
		Super::NativeConstruct();
	}

	// After Super, so the screen's Anim In starts from a fully constructed widget - and it is this, not the
	// activation, that plays the entrance of a pushed screen (see FWidgetAnimScreenTransitionSettings).
	GetAnim().NotifyOwnerConstructed();
}

void UWidgetAnimActivatableWidget::NativeDestruct()
{
	// First: a close still waiting must not outlive the widget being on screen.
	Transitions.NotifyDestructed(*this);

	if (WidgetAnimationComponent)
	{
		WidgetAnimationComponent->NotifyOwnerDestructed();
	}

	Super::NativeDestruct();
}

void UWidgetAnimActivatableWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	// The component previews itself in the designer; a built-in one just needs telling when that is.
	if (IsDesignTime() && WidgetAnimationComponent && !IsTemplate())
	{
		GetAnim().NotifyOwnerDesignTimeRefresh();
	}
}

void UWidgetAnimActivatableWidget::OnWidgetRebuilt()
{
	Super::OnWidgetRebuilt();

	if (IsDesignTime() && WidgetAnimationComponent && !IsTemplate())
	{
		GetAnim().EnsureDesignerPreviewTimer();
	}
}