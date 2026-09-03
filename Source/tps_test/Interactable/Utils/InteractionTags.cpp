// Copyright arubaii. All rights reserved.

#include "InteractionTags.h"

namespace InteractionTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Interaction_CanInteract, "Interaction.CanInteract",
		"Present on an interactor's owner while it has a valid, available focus target.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Interaction_Interacting, "Interaction.Interacting",
		"Present on an interactor's owner for the duration of a hold interaction.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_State_UI_Menu, "State.UI.Menu",
		"Default interaction suppression tag. Push this while a blocking menu is open.");
}
