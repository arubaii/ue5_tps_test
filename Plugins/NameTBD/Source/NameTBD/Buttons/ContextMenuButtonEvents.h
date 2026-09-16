// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "ContextMenuButtonEvents.generated.h"

class UContextMenuButton;

UINTERFACE(BlueprintType, meta = (DisplayName = "Context Menu Button Event Receiver"))
class NAMETBD_API UContextMenuButtonEventReceiver : public UInterface
{
	GENERATED_BODY()
};

/**
 * Lets any widget ABOVE a button handle that button's events, without holding a reference to it.
 *
 * The binding problem this solves: OnContextClicked and friends are BlueprintAssignable, so a Widget
 * Blueprint can bind them on a button in its OWN tree. What it cannot do is bind a button that lives
 * inside a widget it merely PLACES - UMG never exposes a child widget's internal widget tree to whatever
 * drops it on a canvas. So the moment a button is wrapped in a reusable widget, every placement of that
 * widget has to have its behaviour hard-coded into the wrapper, or the wrapper has to re-expose a
 * pass-through delegate for every event, forever.
 *
 * Instead, the button walks its outer chain when something happens and calls these on every widget above
 * it that implements this interface. A placement four levels up handles the click directly, in its own
 * graph, and the wrapper in between needs to know nothing about it. Implement the interface in Class
 * Settings, then the events appear under Interfaces in the My Blueprint panel.
 *
 * Button ID is how a receiver tells several buttons apart - see UContextMenuButton::ButtonId. Handling a
 * receiver's events never stops the button's own OnContextClicked from firing as well; the two are
 * parallel routes to the same event, not alternatives.
 */
class NAMETBD_API IContextMenuButtonEventReceiver
{
	GENERATED_BODY()

public:

	/**
	 * @param Button              The button that was clicked.
	 * @param ButtonId            That button's Button Id, or None if it hasn't been given one.
	 * @param bShouldDestroyCard  Whatever the button's own OnContextClicked reports.
	 * @param bEnabling           The enabled state the button is BECOMING because of this click.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Context Menu Button")
	void OnContextMenuButtonClicked(UContextMenuButton* Button, FName ButtonId, bool bShouldDestroyCard, bool bEnabling);
	virtual void OnContextMenuButtonClicked_Implementation(UContextMenuButton* Button, FName ButtonId, bool bShouldDestroyCard, bool bEnabling) {}

	/** Fires whenever the button's enabled state actually changes, whatever caused it. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Context Menu Button")
	void OnContextMenuButtonEnabledStateChanged(UContextMenuButton* Button, FName ButtonId, bool bIsEnabled);
	virtual void OnContextMenuButtonEnabledStateChanged_Implementation(UContextMenuButton* Button, FName ButtonId, bool bIsEnabled) {}

	/** bHovered is true on enter, false on leave. Navigation focus counts as hover, same as it does for the button's own visuals. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Context Menu Button")
	void OnContextMenuButtonHoverChanged(UContextMenuButton* Button, FName ButtonId, bool bHovered);
	virtual void OnContextMenuButtonHoverChanged_Implementation(UContextMenuButton* Button, FName ButtonId, bool bHovered) {}

	/** A hold-to-confirm button finished its press duration. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Context Menu Button")
	void OnContextMenuButtonPressDurationCompleted(UContextMenuButton* Button, FName ButtonId);
	virtual void OnContextMenuButtonPressDurationCompleted_Implementation(UContextMenuButton* Button, FName ButtonId) {}
};