// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "GenericPlatform/ICursor.h"

/**
 * A cursor the UMG designer shows over its own while set - the Widget Gizmo's, while it grabs, drags or has a handle under the
 * mouse. Slate asks the widgets under the mouse for a cursor whenever it likes (after every move it makes up itself, as the
 * grabbed widget moves under the mouse), and the designer answered with its arrow, so a cursor set only on the platform
 * flickered between the two. The designer's answer is FMotionPathLayer::GetDesignerCursor (WidgetAnimMotionPath.cpp), which
 * asks this first.
 */
namespace NameTBDDesignerCursor
{
	inline TOptional<EMouseCursor::Type>& Override()
	{
		static TOptional<EMouseCursor::Type> Cursor;
		return Cursor;
	}
}
