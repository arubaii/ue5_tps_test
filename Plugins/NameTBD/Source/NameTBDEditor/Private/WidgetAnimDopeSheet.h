// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "NameTBD/WidgetAnimation/WidgetAnimPreset.h"

/**
 * Dope Sheet timelines (Key Mode): an animation made as a Dope Sheet is edited as keys only - every row of the Track Editor is
 * one kind of key on one widget, its keys diamonds in that kind's color, with no bars, no start and end to drag, and no names
 * or presets. Chosen when the timeline is made and kept for its life; saved and played as any other animation (the rows are
 * tracks, their keys the tracks' keys), each row holding its first key until it gets there. See UWidgetAnimPresetAsset::bDopeSheet.
 */
namespace WidgetAnimDopeSheet
{
	/** Whether Animation was made as a Dope Sheet. */
	inline bool IsDopeSheet(const UWidgetAnimPresetAsset* Animation)
	{
		return Animation && Animation->bDopeSheet;
	}
}
