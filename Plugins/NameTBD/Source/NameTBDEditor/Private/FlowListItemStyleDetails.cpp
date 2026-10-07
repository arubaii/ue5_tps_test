// Copyright AR GameSystems 2026. All rights reserved.

#include "FlowListItemStyleDetails.h"

#include "Containers/Ticker.h"
#include "DetailLayoutBuilder.h"
#include "IPropertyUtilities.h"
#include "NameTBD/Widgets/FlowList/FlowListItemStyle.h"

TSharedRef<IDetailCustomization> FFlowListItemStyleDetails::MakeInstance()
{
	return MakeShared<FFlowListItemStyleDetails>();
}

void FFlowListItemStyleDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);

	bool bLaidOut = false;
	for (const TWeakObjectPtr<UObject>& Object : Objects)
	{
		if (UFlowListItemStyle* Style = Cast<UFlowListItemStyle>(Object.Get()))
		{
			if (Style->SyncLooksToOwner())
			{
				Style->MarkPackageDirty();
				bLaidOut = true;
			}
		}
	}

	// The rows were made from the looks as they were: shown again, next frame, from the looks as they are now.
	if (bLaidOut)
	{
		const TWeakPtr<IPropertyUtilities> WeakUtilities = DetailBuilder.GetPropertyUtilities();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakUtilities](float)
		{
			if (const TSharedPtr<IPropertyUtilities> Utilities = WeakUtilities.Pin())
			{
				Utilities->ForceRefresh();
			}
			return false;
		}));
	}
}
