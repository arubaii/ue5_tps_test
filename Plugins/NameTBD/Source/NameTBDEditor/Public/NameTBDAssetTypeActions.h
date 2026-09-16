#pragma once

#include "CoreMinimal.h"
#include "AssetTypeActions_Base.h"

class FNameTBDAssetTypeActions : public FAssetTypeActions_Base
{
public:
	// Returns the text displayed at the bottom of the thumbnail
	virtual FText GetName() const override;
    
	// Returns the color of the stripe under the thumbnail
	virtual FColor GetTypeColor() const override;
    
	// Specifies which C++ class these rules apply to
	virtual UClass* GetSupportedClass() const override;
    
	// Defines which category it appears under in the Right-Click > Create menu
	virtual uint32 GetCategories() override;
};