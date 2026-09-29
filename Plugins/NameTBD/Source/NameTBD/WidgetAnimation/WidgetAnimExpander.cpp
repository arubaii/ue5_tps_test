// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimExpander.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ListViewBase.h"
#include "Components/PanelWidget.h"
#include "Components/Widget.h"
#include "Curves/CurveFloat.h"
#include "Math/RandomStream.h"

// =====================================================================================================
// FWidgetAnimExpander
// =====================================================================================================

void FWidgetAnimExpander::ComputeOffsets(int32 NumItems, TArray<float>& OutOffsets) const
{
	OutOffsets.Reset(NumItems);
	OutOffsets.AddZeroed(FMath::Max(NumItems, 0));
	if (NumItems <= 1)
	{
		return;
	}

	// Each item's distance, in items, from where the stagger begins. Measured as a distance rather than handed out as
	// a rank so that items the same distance from the middle start together - both halves of From The Middle move as
	// mirror images - and so Seconds Apart means the same thing in every order: that long per step outward.
	TArray<float, TInlineAllocator<64>> Distance;
	Distance.SetNumUninitialized(NumItems);
	const float Middle = (NumItems - 1) * 0.5f;

	TArray<int32, TInlineAllocator<64>> Shuffled;
	if (Order == EWidgetAnimStaggerOrder::Random)
	{
		// A shuffle of the places, fixed by the seed, so the same animation on the same number of items looks the
		// same every time it plays - random, not restless.
		Shuffled.SetNumUninitialized(NumItems);
		for (int32 Index = 0; Index < NumItems; ++Index)
		{
			Shuffled[Index] = Index;
		}
		FRandomStream Stream(Seed);
		for (int32 Index = NumItems - 1; Index > 0; --Index)
		{
			Shuffled.Swap(Index, Stream.RandRange(0, Index));
		}
	}

	for (int32 Index = 0; Index < NumItems; ++Index)
	{
		switch (Order)
		{
		case EWidgetAnimStaggerOrder::LastToFirst: Distance[Index] = static_cast<float>(NumItems - 1 - Index); break;
		case EWidgetAnimStaggerOrder::FromCenter:  Distance[Index] = FMath::Abs(Index - Middle); break;
		case EWidgetAnimStaggerOrder::FromEdges:   Distance[Index] = Middle - FMath::Abs(Index - Middle); break;
		case EWidgetAnimStaggerOrder::Random:      Distance[Index] = static_cast<float>(Shuffled[Index]); break;
		case EWidgetAnimStaggerOrder::FirstToLast:
		default:                                   Distance[Index] = static_cast<float>(Index); break;
		}
	}

	float Nearest = TNumericLimits<float>::Max();
	float Farthest = -TNumericLimits<float>::Max();
	for (const float Value : Distance)
	{
		Nearest = FMath::Min(Nearest, Value);
		Farthest = FMath::Max(Farthest, Value);
	}

	const float Steps = Farthest - Nearest;
	if (Steps <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	// The whole spread, first start to last: that many steps at Seconds Apart, or Spread Over itself.
	const float Total = Spacing == EWidgetAnimStaggerSpacing::Across
		? FMath::Max(Spread, 0.f)
		: FMath::Max(Interval, 0.f) * Steps;

	for (int32 Index = 0; Index < NumItems; ++Index)
	{
		const float Along = (Distance[Index] - Nearest) / Steps;
		OutOffsets[Index] = FMath::Max(Total * WidgetAnimEasing::Evaluate(SpacingEasing, SpacingCurve, Along), 0.f);
	}
}

float FWidgetAnimExpander::GetFanSeconds(int32 NumItems) const
{
	TArray<float> Offsets;
	ComputeOffsets(NumItems, Offsets);

	float Last = 0.f;
	for (const float Offset : Offsets)
	{
		Last = FMath::Max(Last, Offset);
	}
	return Last;
}

FString FWidgetAnimExpander::DescribeSpacing() const
{
	const FString Timing = Spacing == EWidgetAnimStaggerSpacing::Across
		? FString::Printf(TEXT("spread over %.2f s"), Spread)
		: FString::Printf(TEXT("%.2f s apart"), Interval);

	const UEnum* OrderEnum = StaticEnum<EWidgetAnimStaggerOrder>();
	const FString OrderName = OrderEnum ? OrderEnum->GetDisplayNameTextByValue(static_cast<int64>(Order)).ToString().ToLower() : FString();
	return OrderName.IsEmpty() ? Timing : FString::Printf(TEXT("%s, %s"), *Timing, *OrderName);
}

// =====================================================================================================
// Each Child
// =====================================================================================================

void FWidgetAnimExpander_EachChild::GatherItems(UWidget* Target, TArray<FWidgetAnimExpandedItem>& OutItems) const
{
	OutItems.Reset();
	if (!Target)
	{
		return;
	}

	auto Add = [this, &OutItems](UWidget* Child)
	{
		if (!Child)
		{
			return;
		}

		if (bSkipHidden)
		{
			const ESlateVisibility Visibility = Child->GetVisibility();
			if (Visibility == ESlateVisibility::Collapsed || Visibility == ESlateVisibility::Hidden)
			{
				return;
			}
		}

		FWidgetAnimExpandedItem& Item = OutItems.AddDefaulted_GetRef();
		Item.Widget = Child;
	};

	// A list shows entry widgets it makes and recycles itself, not children it holds: the ones on screen now are the
	// ones to stagger - the rows a list was just filled with, as it opens.
	if (const UListViewBase* List = Cast<UListViewBase>(Target))
	{
		for (UUserWidget* Entry : List->GetDisplayedEntryWidgets())
		{
			Add(Entry);
		}
		return;
	}

	// A User Widget's children are those of the panel it is built on - a card's rows, a menu's buttons.
	UWidget* Container = Target;
	if (const UUserWidget* UserWidget = Cast<UUserWidget>(Target))
	{
		Container = UserWidget->WidgetTree ? UserWidget->WidgetTree->RootWidget.Get() : nullptr;
	}

	if (const UPanelWidget* Panel = Cast<UPanelWidget>(Container))
	{
		for (int32 ChildIndex = 0; ChildIndex < Panel->GetChildrenCount(); ++ChildIndex)
		{
			Add(Panel->GetChildAt(ChildIndex));
		}
	}
}

// =====================================================================================================
// Each Letter, Word or Line
// =====================================================================================================

namespace WidgetAnimExpand
{
	UWidget* FindPartHost(UWidget* Target)
	{
		if (!Target)
		{
			return nullptr;
		}

		if (Target->Implements<UWidgetAnimPartHost>())
		{
			return Target;
		}

		// Into a User Widget's own tree, so a stagger aimed at a whole button reaches the label inside it.
		if (const UUserWidget* UserWidget = Cast<UUserWidget>(Target))
		{
			return UserWidget->WidgetTree ? FindPartHost(UserWidget->WidgetTree->RootWidget) : nullptr;
		}

		if (const UPanelWidget* Panel = Cast<UPanelWidget>(Target))
		{
			for (int32 ChildIndex = 0; ChildIndex < Panel->GetChildrenCount(); ++ChildIndex)
			{
				if (UWidget* Found = FindPartHost(Panel->GetChildAt(ChildIndex)))
				{
					return Found;
				}
			}
		}

		return nullptr;
	}
}

FName FWidgetAnimExpander_EachTextPart::GetPartUnit() const
{
	switch (Unit)
	{
	case EWidgetAnimTextUnit::Word: return WidgetAnimPartUnits::Word;
	case EWidgetAnimTextUnit::Line: return WidgetAnimPartUnits::Line;
	case EWidgetAnimTextUnit::Letter:
	default:                        return WidgetAnimPartUnits::Letter;
	}
}

void FWidgetAnimExpander_EachTextPart::GatherItems(UWidget* Target, TArray<FWidgetAnimExpandedItem>& OutItems) const
{
	OutItems.Reset();

	UWidget* Host = WidgetAnimExpand::FindPartHost(Target);
	const IWidgetAnimPartHost* PartHost = Cast<IWidgetAnimPartHost>(Host);
	if (!PartHost)
	{
		return;
	}

	const FName PartUnit = GetPartUnit();
	const int32 NumParts = PartHost->GetWidgetAnimPartCount(PartUnit);
	OutItems.Reserve(NumParts);
	for (int32 PartIndex = 0; PartIndex < NumParts; ++PartIndex)
	{
		FWidgetAnimExpandedItem& Item = OutItems.AddDefaulted_GetRef();
		Item.Widget = Host;
		Item.Part = FWidgetAnimPartKey(PartUnit, PartIndex);
	}
}

bool FWidgetAnimExpander_EachTextPart::IsReadyToExpand(UWidget* Target) const
{
	const IWidgetAnimPartHost* PartHost = Cast<IWidgetAnimPartHost>(WidgetAnimExpand::FindPartHost(Target));
	return !PartHost || PartHost->AreWidgetAnimPartsReady(GetPartUnit());
}

FString FWidgetAnimExpander_EachTextPart::GetSummary() const
{
	switch (Unit)
	{
	case EWidgetAnimTextUnit::Word: return TEXT("Each Word");
	case EWidgetAnimTextUnit::Line: return TEXT("Each Line");
	case EWidgetAnimTextUnit::Letter:
	default:                        return TEXT("Each Letter");
	}
}
