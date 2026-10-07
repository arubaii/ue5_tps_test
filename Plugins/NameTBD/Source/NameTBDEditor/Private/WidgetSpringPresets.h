// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "IPropertyTypeCustomization.h"

#include "NameTBD/WidgetAnimation/WidgetAnimSpring.h"

#include "WidgetSpringPresets.generated.h"

class IPropertyHandle;

/** A spring saved under a name, offered on every spring's Preset menu. */
USTRUCT()
struct FWidgetSpringPreset
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Spring Preset")
	FString Name;

	/** Shown on the menu as the preset's tooltip: what it is for. */
	UPROPERTY(EditAnywhere, Category = "Spring Preset")
	FString Description;

	UPROPERTY(EditAnywhere, Category = "Spring Preset")
	FWidgetSpringParams Spring;
};

/**
 * Springs of your own, offered on every spring's Preset menu above the built-in ones (Smooth, Quick, Snappy, Gentle, Bouncy,
 * Wobbly, Jelly, Interactive, Slow, Heavy): Project Settings > Plugins > Spring Presets. Saved to the project's DefaultEditor.ini, so a team shares them.
 * Add one from any spring's Preset menu (Save As Preset), or here.
 */
UCLASS(config = Editor, defaultconfig, meta = (DisplayName = "Spring Presets"))
class UWidgetSpringPresetSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	UPROPERTY(config, EditAnywhere, Category = "Spring Presets", meta = (TitleProperty = "Name"))
	TArray<FWidgetSpringPreset> Presets;

	static UWidgetSpringPresetSettings& Get();

	/** Adds a preset - or replaces the one already under its name - and saves. */
	void AddPreset(const FWidgetSpringPreset& Preset);

	void RemovePreset(int32 Index);

private:
	void Save();
};

namespace WidgetSpringPresets
{
	/** A ready-made spring: what it feels like, by name. */
	struct FBuiltIn
	{
		FText Name;
		FText Description;
		FWidgetSpringParams Spring;
	};

	const TArray<FBuiltIn>& GetBuiltIns();

	/** Whether two springs move the same - the values their mode uses, compared closely. */
	bool IsSameSpring(const FWidgetSpringParams& A, const FWidgetSpringParams& B);
}

/**
 * Every spring's Details (Flow List Scroll Spring and Reorder Spring, an Item Style's Transition, a scroll bar's Thumb
 * Spring, a track's spring, a state machine blend's): its fields as usual, and a Preset menu on its header - the built-in
 * feels, the project's own, and Save As Preset for the one being edited - with what it currently matches and a one-line
 * summary of how it moves.
 */
class FWidgetSpringParamsCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& Utils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& Utils) override;

private:
	TSharedRef<SWidget> MakePresetButton();
	TSharedRef<SWidget> MakePresetMenu();

	/** The spring edited - the first one, with several selected - and whether they all agree. */
	bool ReadSpring(FWidgetSpringParams& OutSpring, bool& bOutMixed) const;

	void ApplySpring(const FWidgetSpringParams& Spring, const FText& Name);
	void SaveAsPreset(const FString& Name);

	FText GetMatchText() const;
	FText GetSummaryText() const;

	TSharedPtr<IPropertyHandle> Handle;
};
