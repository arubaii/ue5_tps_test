// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"

class UContextMenuButton;

/**
 * Keeps the ready-made buttons (UStyledButtonBase) right with the Widget Blueprint they are placed in.
 *
 * A ready-made button is made of child widgets. The Widget Blueprint compiler wants a GUID in WidgetVariableNameToGuidMap
 * for every widget made in the blueprint's tree - listed in the hierarchy or not - and no GUID for a widget that is gone; each
 * mismatch is an ensure with a full callstack (which froze the editor). The designer gives one only to the widget dropped from
 * the Palette, and makes the dragged widget again for every drop preview. So:
 *
 *   - A dropped button only marks itself pending (UContextMenuButton::OnCreationFromPalette). Its parts are built here,
 *     once it has really been placed, in one undoable transaction, every part given its GUID.
 *   - Before every compile, and before every structural change made here, the GUID map is made to match the tree's widgets
 *     (SyncGuids) - both ways.
 *   - Once built, a ready-made button's parts are hidden from the Hierarchy: moved into a widget tree of the button's own
 *     (UContextMenuButton::SetPartsHidden), which the Widget Blueprint neither lists nor compiles nor wants GUIDs for. Show /
 *     Hide Parts In Hierarchy in its Details moves them back and forth (ToggleParts). Ready-made buttons placed before that
 *     hide theirs once; a pasted copy whose hidden parts are still the original's builds its own (RestoreHiddenParts).
 *   - Parts nothing holds any more (discarded drop previews, deleted buttons, parts left by fix6-fix7) are taken out of the
 *     tree, outside the compile, on the next tick.
 */
namespace StyledButtonsEditor
{
	void Register();
	void Unregister();

	/** Called as a Context Menu Button's Details are shown: builds any pending ready-made content, next tick. */
	void OnButtonsShown(const TArray<TWeakObjectPtr<UContextMenuButton>>& Buttons);

	/** Whether the buttons' parts are listed in the Hierarchy - read from the Widget Blueprint's own copies. */
	bool ArePartsShown(const TArray<TWeakObjectPtr<UContextMenuButton>>& Buttons);

	/** Whether any of the buttons has parts to show or hide. */
	bool CanToggleParts(const TArray<TWeakObjectPtr<UContextMenuButton>>& Buttons);

	/**
	 * Lists the buttons' parts in the Hierarchy, or hides them in the button's own tree - on the Widget Blueprint's own copies,
	 * in one undoable step, GUIDs following. The same widgets either way: nothing changed on them is lost.
	 */
	void ToggleParts(const TArray<TWeakObjectPtr<UContextMenuButton>>& Buttons);
}
