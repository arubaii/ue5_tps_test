// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MenuSetting.h"
#include "MenuSettingTypes.h"

#include "MenuSettingsSchema.generated.h"

/** A group of settings under a heading on a page ("Display", "Quality", "Keyboard & Mouse"). */
USTRUCT(BlueprintType)
struct NAMETBD_API FMenuSettingsSection
{
	GENERATED_BODY()

	/** For merging: a schema's section with the same page and Id adds to (or replaces settings in) this one. Empty: the Title. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Section")
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Section")
	FText Title;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = "Section")
	TArray<TObjectPtr<UMenuSetting>> Settings;

	/**
	 * Lists the player's key bindings here, one row per Enhanced Input player mappable key (needs Enhanced Input's Enable User
	 * Settings, and the mapping contexts registered - Project Settings > Plugins > Menus > Key Binding Contexts).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Key Bindings")
	bool bKeyBindings = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Key Bindings", meta = (EditCondition = "bKeyBindings"))
	EMenuKeyBindingDevice KeyBindingDevice = EMenuKeyBindingDevice::KeyboardMouse;

	/** Only mappings whose Display Category is one of these. Empty: all. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Key Bindings", meta = (EditCondition = "bKeyBindings"))
	TArray<FString> KeyBindingCategories;

	/** Listed only while all of these hold. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Section")
	TArray<FMenuSettingCondition> ShownWhen;

	FName GetMergeId() const { return Id.IsNone() ? FName(*Title.ToString()) : Id; }
};

/** A page (a tab) of settings: Video, Audio, Controls, Accessibility, Gameplay - or your own. */
USTRUCT(BlueprintType)
struct NAMETBD_API FMenuSettingsPage
{
	GENERATED_BODY()

	/** "Video", "Audio", "Controls", "Accessibility", "Gameplay" for the built-in pages. A schema page with the same Id adds to it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Page")
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Page")
	FText DisplayName;

	/** Shown on its tab before the name (or alone, with the style's Tab Icons Only). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Page")
	FMenuImage Icon;

	/** Lower comes first. The built-in pages are 100 (Gameplay), 200 (Video), 300 (Audio), 400 (Controls), 500 (Accessibility). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Page")
	int32 Order = 1000;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Page")
	TArray<FMenuSettingsSection> Sections;

	/** Only on these platforms ("Windows", "Desktop", "Console"...). Empty: everywhere. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Page")
	TArray<FString> Platforms;
};

/**
 * Settings pages, sections and settings, as data. Projects list theirs in Project Settings > Plugins > Menus > Schemas (or a
 * Settings Menu widget's Extra Schemas, or Register Schema at run time); every schema is merged over the built-in pages:
 *  - a page with an Id already there adds its sections to that page (a section with the same Id adds its settings);
 *  - a setting with an Id already there replaces it where it was.
 * So "a Render Distance slider on the Video page" is one schema with a Video page, a Graphics section and one Number setting.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Settings Schema"))
class NAMETBD_API UMenuSettingsSchema : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings")
	TArray<FMenuSettingsPage> Pages;

	/** Merged in this order: lower first, so a higher one's settings win. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings")
	int32 Priority = 0;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** Fired when a schema is edited in the editor (open settings screens rebuild). */
	static FSimpleMulticastDelegate& OnAnySchemaChanged();
};

/** What merging leaves out (Project Settings > Plugins > Menus). */
struct FMenuSettingsMergeRules
{
	TSet<FName> HiddenPages;
	TSet<FName> HiddenSettings;
};

namespace MenuSettingsSchemaUtils
{
	/** Merges Schemas (in order: each over the ones before) into OutPages, sorted by Order. */
	NAMETBD_API void Merge(const TArray<const UMenuSettingsSchema*>& Schemas, const FMenuSettingsMergeRules& Rules, TArray<FMenuSettingsPage>& OutPages);

	/** The schemas a player's settings are made of: the built-in pages (if on), the project's, then Extra. Loads them. */
	NAMETBD_API void GatherSchemas(UMenuSettingsSchema* BuiltIn, const TArray<UMenuSettingsSchema*>& Extra, TArray<const UMenuSettingsSchema*>& OutSchemas);

	NAMETBD_API FMenuSettingsMergeRules MakeProjectRules();
}
