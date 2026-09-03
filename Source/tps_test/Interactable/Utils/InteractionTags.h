// Copyright arubaii. All rights reserved.

#pragma once

#include "NativeGameplayTags.h"

/**
 * Tags the plugin declares natively so they exist without anyone editing an ini.
 *
 * Only two categories are needed. Everything the Blueprint version tracked with tags on
 * the interactable (ItemWidgetState.ShowingInteractable / ShowingAmbient / ShowingStatic)
 * is now per-viewer state inside UInteractorComponent, because that is what it always
 * was — the tags were a way to store it on the wrong object.
 */
namespace InteractionTags
{
	/**
	 * Applied to the interactor's owner while it has a valid, available focus target.
	 * Read it to gate an interact animation, a HUD hint, whatever — it is informational,
	 * the plugin never branches on it.
	 */
	TPS_TEST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Interaction_CanInteract);

	/** Applied to the interactor's owner for the duration of a hold. */
	TPS_TEST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Interaction_Interacting);

	/**
	 * The default suppression tag. Any UI that should hide interaction prompts pushes
	 * this (or a child of it) onto the interactor. Menus are the obvious case, hence the
	 * name, but cutscenes and death screens want the same thing.
	 */
	TPS_TEST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_UI_Menu);
}
