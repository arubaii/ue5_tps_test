// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * The first option of the editor's widget pickers - a Track Group's or a track's Target Widget, the state machine preview's
 * Plays On and its track groups' rows: the widget the animation plays on itself, stored as None. Listed as "(this widget)",
 * never as None, which read as "no widget at all". Widget names can't hold spaces or brackets, so no path is ever taken for it.
 */
namespace WidgetAnimTargetOption
{
	/** The option itself. */
	inline const FString& ThisWidget()
	{
		static const FString Label(TEXT("(this widget)"));
		return Label;
	}

	inline bool IsThisWidget(const FString& Option)
	{
		return Option == ThisWidget();
	}

	/** The option a stored target reads as. */
	inline FString FromPath(FName Path)
	{
		return Path.IsNone() ? ThisWidget() : Path.ToString();
	}

	/** The target an option stores: None for (this widget). */
	inline FName ToPath(const FString& Option)
	{
		return IsThisWidget(Option) ? FName(NAME_None) : FName(*Option);
	}
}
