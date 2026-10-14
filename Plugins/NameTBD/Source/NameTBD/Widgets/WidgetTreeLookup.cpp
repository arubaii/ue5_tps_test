// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetTreeLookup.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "UObject/UnrealType.h"

namespace WidgetTreeLookup
{
	UWidgetTree* FindWidgetTree(const UObject* From)
	{
		static const FName WidgetTreeName(TEXT("WidgetTree"));
		for (const UObject* Outer = From; Outer; Outer = Outer->GetOuter())
		{
			if (const UWidgetTree* Tree = Cast<UWidgetTree>(Outer))
			{
				return const_cast<UWidgetTree*>(Tree);
			}
			if (const UUserWidget* UserWidget = Cast<UUserWidget>(Outer))
			{
				if (UserWidget->WidgetTree)
				{
					return UserWidget->WidgetTree;
				}
			}
			// A Widget Blueprint (editor): its WidgetTree property, read by reflection.
			if (const FObjectProperty* TreeProperty = FindFProperty<FObjectProperty>(Outer->GetClass(), WidgetTreeName))
			{
				if (UWidgetTree* Tree = Cast<UWidgetTree>(TreeProperty->GetObjectPropertyValue_InContainer(Outer)))
				{
					return Tree;
				}
			}
		}
		return nullptr;
	}

	void GetWidgetNames(const UObject* From, TArray<FString>& OutNames, const UWidget* Exclude, bool bWithNone)
	{
		OutNames.Reset();
		if (const UWidgetTree* Tree = FindWidgetTree(From))
		{
			Tree->ForEachWidget([&OutNames, Exclude](UWidget* Widget)
			{
				if (Widget && Widget != Exclude)
				{
					OutNames.Add(Widget->GetName());
				}
			});
		}
		OutNames.Sort();
		if (bWithNone)
		{
			OutNames.Insert(TEXT("None"), 0);
		}
	}

	UWidget* FindWidget(const UObject* From, FName Name)
	{
		if (Name.IsNone())
		{
			return nullptr;
		}
		const UWidgetTree* Tree = FindWidgetTree(From);
		return Tree ? Tree->FindWidget(Name) : nullptr;
	}
}
