// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * The Widget Blueprint editor toolbar's info card preview: shown while an Info Card Anchor (a Tooltip Card Anchor, a Context
 * Menu Anchor) is selected in the designer, driving that anchor's designer copy only (nothing authored changes, nothing is
 * saved).
 *
 *   AnchorName   Card: [Tooltip v]   In  Out
 *
 *   - Card: which of the anchor's cards the designer shows beside it (UInfoCardAnchor::SetDesignerPreviewCardPick) - the real
 *     card, with its payloads, where the game puts it. Default: the anchor's Preview Card. None hides it.
 *   - In / Out: the shown card's entrance / exit once (UInfoCardAnchor::PlayDesignerCardAnimation) - the card class's Anim In /
 *     Anim Out plus the anchor's resolved additive animations. They replace the old Designer Preview mode in the Details.
 *
 * Shares the selected widget's animation section, like the button preview. Picks are remembered per anchor for the editor's
 * session and shown again whenever the designer rebuilds its preview.
 */
class FInfoCardAnchorPreviewToolbar
{
public:

	/** Call from the module's UToolMenus startup callback, after FButtonPreviewToolbar::Register. */
	static void Register();
};
