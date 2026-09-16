#include "NameTBDAssetTypeActions.h"
#include "NameTBD/WidgetAnimation/WidgetAnimPreset.h" 

#define LOCTEXT_NAMESPACE "AssetTypeActions"

FText FNameTBDAssetTypeActions::GetName() const
{
	return LOCTEXT("FNameTBDAssetTypeActionsName", "Widget Animation Preset");
}

FColor FNameTBDAssetTypeActions::GetTypeColor() const
{
	// Stripe color under data asset icon
	return FColor(235, 168, 52); 
}

UClass* FNameTBDAssetTypeActions::GetSupportedClass() const
{
	return UWidgetAnimPresetAsset::StaticClass();
}

uint32 FNameTBDAssetTypeActions::GetCategories()
{
	// Places it in the "User Interface" category in the create menu
	return EAssetTypeCategories::UI; 
}

#undef LOCTEXT_NAMESPACE