// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * The Widget Blueprint editor toolbar's button preview: shown while a Context Menu Button is selected in the designer, and
 * driving that button's designer copy only (nothing authored changes, nothing is saved).
 *
 *   ButtonName   Idle  Hover  Press  On  Disabled   < High >   Replay
 *
 *   - A state: the button blends there from the one shown, with its own speeds, curves and delays - the transition exactly as
 *     the game plays it (UContextMenuButton::PlayDesignerPreviewState). Pick another mid-blend and it turns from where it is.
 *     On shows only for a button that can be enabled.
 *   - The stepper: for a button that steps through values (a Choice Button's options or numbers, a Segmented Choice's
 *     choices) - the value changes as at runtime: the text kicks in, the highlight slides (SetDesignerPreviewValueIndex).
 *   - Replay: plays the button's last change again, from where it started.
 *
 * What was picked is remembered per button for the editor's session, and shown again (at once) whenever the designer rebuilds
 * its preview. It replaces the Show Preview dropdown in the Details panel, which stays as the state shown until the toolbar
 * picks one.
 */
class FButtonPreviewToolbar
{
public:

	/** Call from the module's UToolMenus startup callback. */
	static void Register();
};
