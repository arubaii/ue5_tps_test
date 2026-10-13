// Copyright AR GameSystems 2026. All rights reserved.

#include "MediaBoxLibrary.h"

#include "MediaBoxClip.h"
#include "MediaBoxLog.h"
#include "MediaBoxPool.h"

#include "Components/Widget.h"

void UMediaBoxLibrary::AddToMediaWarmGroup(UWidget* Widget, FName Group, UMediaBoxClip* Clip)
{
	if (UMediaBoxPool* Pool = Widget ? UMediaBoxPool::Get(Widget) : nullptr)
	{
		Pool->AddToGroup(Group, Widget, Clip);
	}
}

void UMediaBoxLibrary::RemoveFromMediaWarmGroup(UWidget* Widget, FName Group)
{
	if (UMediaBoxPool* Pool = Widget ? UMediaBoxPool::Get(Widget) : nullptr)
	{
		Pool->RemoveFromGroup(Group, Widget);
	}
}

void UMediaBoxLibrary::ClearMediaWarmGroup(UObject* WorldContextObject, FName Group)
{
	if (UMediaBoxPool* Pool = UMediaBoxPool::Get(WorldContextObject))
	{
		Pool->ClearGroup(Group);
	}
}

void UMediaBoxLibrary::SetMediaWarmGroupFocus(UWidget* Widget, FName Group)
{
	if (UMediaBoxPool* Pool = Widget ? UMediaBoxPool::Get(Widget) : nullptr)
	{
		Pool->SetGroupFocus(Group, Widget);
	}
}

void UMediaBoxLibrary::KeepMediaClipWarm(UObject* WorldContextObject, UMediaBoxClip* Clip, bool bKeepWarm)
{
	if (UMediaBoxPool* Pool = UMediaBoxPool::Get(WorldContextObject))
	{
		Pool->PinClip(Clip, bKeepWarm);
	}
}

void UMediaBoxLibrary::SetMediaBoxSubtitlesEnabled(bool bEnabled)
{
	MediaBoxRuntime::SetSubtitlesEnabled(bEnabled);
}

bool UMediaBoxLibrary::AreMediaBoxSubtitlesEnabled()
{
	return MediaBoxRuntime::AreSubtitlesEnabled();
}

void UMediaBoxLibrary::GetMediaBoxPoolStats(UObject* WorldContextObject, int32& WarmPlayers, int32& PlayingPlayers, int32& WarmStarts, int32& ColdStarts)
{
	WarmPlayers = PlayingPlayers = WarmStarts = ColdStarts = 0;
	if (const UMediaBoxPool* Pool = UMediaBoxPool::Get(WorldContextObject))
	{
		WarmPlayers = Pool->GetNumWarm();
		PlayingPlayers = Pool->GetNumLeased();
		WarmStarts = Pool->WarmStarts;
		ColdStarts = Pool->ColdStarts;
	}
}
