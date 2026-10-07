// Copyright AR GameSystems 2026. All rights reserved.

#include "FlowListInput.h"

#include "FlowList.h"
#include "NameTBD/WidgetAnimation/WidgetAnimDriver.h"

bool FWidgetAnimInput_FlowList::Sample(const FWidgetAnimInputContext& Context, FVector4& OutValue)
{
	UWidget* Measured = Context.MeasuredWidget;
	if (!Measured)
	{
		return false;
	}

	// The Item measures read the list the widget is an item of, every frame: items move between indices as the list changes.
	const bool bItemMeasure = Measure == EFlowListMeasure::ItemIndex || Measure == EFlowListMeasure::ItemDistance
		|| Measure == EFlowListMeasure::ItemFocus || Measure == EFlowListMeasure::IsItemSelected;
	if (bItemMeasure)
	{
		UWidget* Item = nullptr;
		int32 Index = INDEX_NONE;
		const UFlowList* Owner = UFlowList::FindFlowListOfItem(Measured, Item, Index);
		if (!Owner || Index == INDEX_NONE)
		{
			return false;
		}
		float Value = 0.f;
		switch (Measure)
		{
		case EFlowListMeasure::ItemIndex:
			Value = static_cast<float>(Index);
			break;
		case EFlowListMeasure::ItemDistance:
			Value = Owner->GetItemDistance(Item);
			break;
		case EFlowListMeasure::ItemFocus:
			Value = FMath::Clamp(1.f - FMath::Abs(Owner->GetItemDistance(Item)), 0.f, 1.f);
			break;
		case EFlowListMeasure::IsItemSelected:
			Value = Index == Owner->GetSelectedIndex() ? 1.f : 0.f;
			break;
		default:
			break;
		}
		OutValue = WidgetAnimValue::FromFloat(Value);
		return true;
	}

	// The others read one list, found once - again only if the widget measured on, or the name, changes.
	// A list found that has since gone is looked for again; a widget with none near isn't searched every frame.
	if (!bSearched || SearchedFrom.Get() != Measured || SearchedName != ListName || (List.IsStale() && !List.IsValid()))
	{
		List = UFlowList::FindNearestFlowList(Measured, ListName);
		SearchedFrom = Measured;
		SearchedName = ListName;
		bSearched = true;
	}
	const UFlowList* Read = List.Get();
	if (!Read)
	{
		return false;
	}

	float Value = 0.f;
	switch (Measure)
	{
	case EFlowListMeasure::SelectedIndex:
		Value = static_cast<float>(Read->GetSelectedIndex());
		break;
	case EFlowListMeasure::SelectedValue:
		Value = static_cast<float>(Read->GetItemValue(Read->GetSelectedIndex()));
		break;
	case EFlowListMeasure::ScrollPosition:
		Value = Read->GetScrollPosition();
		break;
	case EFlowListMeasure::ScrollProgress:
		Value = Read->GetScrollFraction();
		break;
	case EFlowListMeasure::ScrollSpeed:
	{
		const float Scroll = Read->GetScrollPosition();
		Value = bHasLastScroll && Context.DeltaTime > 0.f ? (Scroll - LastScroll) / Context.DeltaTime : 0.f;
		LastScroll = Scroll;
		bHasLastScroll = true;
		break;
	}
	case EFlowListMeasure::IsScrolling:
		Value = Read->IsScrolling() ? 1.f : 0.f;
		break;
	case EFlowListMeasure::ItemCount:
		Value = static_cast<float>(Read->GetItemCount());
		break;
	default:
		return false;
	}
	OutValue = WidgetAnimValue::FromFloat(Value);
	return true;
}

FString FWidgetAnimInput_FlowList::GetSummary() const
{
	const UEnum* Enum = StaticEnum<EFlowListMeasure>();
	const FString MeasureName = Enum ? Enum->GetDisplayNameTextByValue(static_cast<int64>(Measure)).ToString() : FString();
	return ListName.IsNone()
		? FString::Printf(TEXT("Flow List %s"), *MeasureName)
		: FString::Printf(TEXT("%s %s"), *ListName.ToString(), *MeasureName);
}
