// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimTimelines.h"

#include "NameTBD/WidgetAnimation/WidgetAnimPreset.h"
#include "UObject/Package.h"
#include "WidgetBlueprint.h"

UWidgetAnimTimelinesExtension* UWidgetAnimTimelinesExtension::Find(const UWidgetBlueprint* WidgetBlueprint)
{
	return WidgetBlueprint ? UWidgetBlueprintExtension::GetExtension<UWidgetAnimTimelinesExtension>(WidgetBlueprint) : nullptr;
}

UWidgetAnimTimelinesExtension* UWidgetAnimTimelinesExtension::FindOrAdd(UWidgetBlueprint* WidgetBlueprint)
{
	if (!WidgetBlueprint)
	{
		return nullptr;
	}
	if (UWidgetAnimTimelinesExtension* Existing = Find(WidgetBlueprint))
	{
		return Existing;
	}

	// The Widget Blueprint's list of extensions is what changes, so it is what the transaction records: undone, the
	// extension is off it again. The extension itself is made transactional so later edits to it undo too.
	WidgetBlueprint->Modify();
	UWidgetAnimTimelinesExtension* Extension = UWidgetBlueprintExtension::RequestExtension<UWidgetAnimTimelinesExtension>(WidgetBlueprint);
	if (Extension)
	{
		Extension->SetFlags(RF_Transactional);
	}
	return Extension;
}

int32 UWidgetAnimTimelinesExtension::IndexOf(const FString& Name) const
{
	return Timelines.IndexOfByPredicate([&Name](const FWidgetAnimTimeline& Timeline)
	{
		return Timeline.Name.Equals(Name, ESearchCase::IgnoreCase);
	});
}

int32 UWidgetAnimTimelinesExtension::IndexOf(const UWidgetAnimPresetAsset* Animation) const
{
	if (!Animation)
	{
		return INDEX_NONE;
	}
	return Timelines.IndexOfByPredicate([Animation](const FWidgetAnimTimeline& Timeline)
	{
		return Timeline.Animation == Animation;
	});
}

FString UWidgetAnimTimelinesExtension::MakeUniqueName(const FString& Base, int32 Ignore) const
{
	FString Stem = Base.TrimStartAndEnd();
	if (Stem.IsEmpty())
	{
		Stem = TEXT("Timeline");
	}

	auto IsTaken = [this, Ignore](const FString& Candidate)
	{
		const int32 Found = IndexOf(Candidate);
		return Found != INDEX_NONE && Found != Ignore;
	};
	if (!IsTaken(Stem))
	{
		return Stem;
	}

	// "Intro 2" copied again is "Intro 3", not "Intro 2 2": a number already on the end is counted on from.
	int32 Number = 2;
	int32 Space = INDEX_NONE;
	if (Stem.FindLastChar(TEXT(' '), Space) && Space > 0 && Stem.Mid(Space + 1).IsNumeric())
	{
		Number = FMath::Max(FCString::Atoi(*Stem.Mid(Space + 1)) + 1, 2);
		Stem = Stem.Left(Space);
	}
	FString Candidate;
	do
	{
		Candidate = FString::Printf(TEXT("%s %d"), *Stem, Number++);
	}
	while (IsTaken(Candidate));
	return Candidate;
}

FString UWidgetAnimTimelinesExtension::MakeDefaultName() const
{
	// Always numbered, from 1 - a gap left by a deleted one is filled first.
	for (int32 Number = 1;; ++Number)
	{
		const FString Candidate = FString::Printf(TEXT("Timeline %d"), Number);
		if (IndexOf(Candidate) == INDEX_NONE)
		{
			return Candidate;
		}
	}
}

int32 UWidgetAnimTimelinesExtension::AddTimeline(const FString& Name, const UWidgetAnimPresetAsset* CopyFrom)
{
	// Named for what it is rather than after the timeline: renaming a timeline only changes its Name, never the object.
	const FName ObjectName = MakeUniqueObjectName(this, UWidgetAnimPresetAsset::StaticClass(), TEXT("WidgetAnimTimeline"));
	UWidgetAnimPresetAsset* Animation = CopyFrom
		? DuplicateObject<UWidgetAnimPresetAsset>(CopyFrom, this, ObjectName)
		: NewObject<UWidgetAnimPresetAsset>(this, ObjectName);
	if (!Animation)
	{
		return INDEX_NONE;
	}

	// An object inside the Widget Blueprint, not an asset: never public or standalone, so nothing outside can pick it.
	Animation->ClearFlags(RF_Public | RF_Standalone | RF_Transient);
	Animation->SetFlags(RF_Transactional);
	if (!CopyFrom && Animation->LayerGroups.Num() == 0)
	{
		Animation->LayerGroups.AddDefaulted();
	}

	FWidgetAnimTimeline& Timeline = Timelines.AddDefaulted_GetRef();
	Timeline.Name = Name.TrimStartAndEnd().IsEmpty() ? MakeDefaultName() : MakeUniqueName(Name);
	Timeline.Animation = Animation;
	return Timelines.Num() - 1;
}
