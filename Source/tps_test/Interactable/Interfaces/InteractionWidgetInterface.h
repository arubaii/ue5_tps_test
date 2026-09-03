// Copyright arubaii. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Interactable/Types/InteractionTypes.h"
#include "InteractionWidgetInterface.generated.h"

UINTERFACE(MinimalAPI, Blueprintable, meta = (DisplayName = "Interaction Widget"))
class UInteractionWidgetInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * Optional contract for a prompt widget.
 *
 * Every function has a working default, so a widget that implements none of it still
 * works: it will pop in, pop out and never animate. Implement only the parts you care
 * about.
 *
 * This one interface replaces four Blueprint ones (widget animations, configurables,
 * interaction type, progress). They were always called on the same objects at the same
 * points in the same order; splitting them bought nothing and cost four asset lookups
 * per widget per frame.
 */
class TPS_TEST_API IInteractionWidgetInterface
{
	GENERATED_BODY()

public:
	/**
	 * The widget became visible.
	 *
	 * Return how long your intro animation runs. The interactor uses it only to avoid
	 * tearing the widget down mid-animation; return 0 if you don't animate.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction|Widget")
	float OnInteractionWidgetShown();
	virtual float OnInteractionWidgetShown_Implementation() { return 0.f; }

	/**
	 * The widget is about to be hidden.
	 *
	 * Return your outro length. The interactor defers the actual collapse by that much,
	 * which is the only reason it needs the number.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction|Widget")
	float OnInteractionWidgetHidden();
	virtual float OnInteractionWidgetHidden_Implementation() { return 0.f; }

	/**
	 * Hold progress, 0..1. Called every frame of a hold and once with 0 when it ends.
	 * A press-to-interact prompt can ignore this entirely.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction|Widget")
	void SetInteractionProgress(float Progress);
	virtual void SetInteractionProgress_Implementation(float Progress) {}

	/**
	 * Told once, when the widget is built, how this interactable is committed. Use it to
	 * swap the key glyph between a tap and a hold ring.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction|Widget")
	void SetInteractionMethod(EInteractionMethod Method);
	virtual void SetInteractionMethod_Implementation(EInteractionMethod Method) {}

	/**
	 * Distance fade, 0..1. Default routes to SetRenderOpacity, so most widgets get the
	 * behaviour without implementing anything.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction|Widget")
	void SetInteractionOpacity(float Opacity);
	virtual void SetInteractionOpacity_Implementation(float Opacity);

	/**
	 * Free-form payload from the interactable, forwarded verbatim at build time. This is
	 * the hook for "show the item name and icon" without the plugin knowing what an item
	 * is — see UInteractableComponent::GetWidgetPayload.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction|Widget")
	void SetInteractionPayload(UObject* Payload);
	virtual void SetInteractionPayload_Implementation(UObject* Payload) {}
	
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction")
	FVector2D GetInteractionWidgetSize() const;
};
