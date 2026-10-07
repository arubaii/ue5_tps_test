// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"

class SWidget;

/** The shape editors' buttons: the Custom Shape toolbar on the designer's canvas and the shape Details rows. */
namespace WidgetShapeEditorUI
{
	/**
	 * A "chip": a small rounded button with an icon, a label or both, centered in it. It draws its own background - so a
	 * chip that is the active tool shows the dimmed animation orange it is given, which a tinted editor button can't - and
	 * brightens under the mouse. Every part but OnClick is optional.
	 */
	struct FChip
	{
		FText Label;
		FName Icon;
		FText ToolTip;

		/** Called each frame, for a chip whose label or icon follows something (Open Path / Close Path). */
		TFunction<FText()> GetLabel;
		TFunction<FName()> GetIcon;
		TFunction<FText()> GetToolTip;

		TFunction<bool()> IsActive;
		TFunction<bool()> IsEnabled;
		TFunction<void()> OnClick;

		/**
		 * Acts as the mouse goes down, and never takes the keyboard focus - for a chip that works on what another widget
		 * has selected (the text editor's), which a click that moved the focus, or waited for the mouse to come up, could
		 * lose.
		 */
		bool bPressOnly = false;
	};

	TSharedRef<SWidget> MakeChip(const FChip& Chip);
}
