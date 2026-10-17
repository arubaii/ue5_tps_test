// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"

#include "MenuKeyGlyphSetFactory.generated.h"

/**
 * Makes a Key Glyph Set - Content Browser > Add > User Interface > Key Glyph Set, or Create New in any Key Glyph Set picker
 * (Project Settings > Menus > Key Glyph Sets). Without a factory of its own a Data Asset type has no Create New there: the
 * generic Data Asset factory isn't offered for a specific class.
 */
UCLASS()
class UMenuKeyGlyphSetFactory : public UFactory
{
	GENERATED_BODY()

public:

	UMenuKeyGlyphSetFactory();

	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual FText GetDisplayName() const override;
	virtual FText GetToolTip() const override;
	virtual uint32 GetMenuCategories() const override;
	virtual FString GetDefaultNewAssetName() const override;
	virtual bool ShouldShowInNewMenu() const override { return true; }
};
