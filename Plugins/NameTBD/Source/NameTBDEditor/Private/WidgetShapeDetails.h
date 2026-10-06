// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class IDetailLayoutBuilder;
class UClass;

/**
 * The Details panel of every shape widget (Ellipse, Triangle, Star, N-gon, Custom Shape).
 *
 * Adds, at the top of the Shape category:
 *   Preset   - presets of this kind of shape (see WidgetShapePresets.h): pick one to give the selected shapes exactly its
 *              settings, or only its outline; name the current settings to save them as a new one.
 *   Outline  - a Custom Shape's own tools: Edit Shape (the path editor on the designer's canvas), Start From (an outline
 *              from a primitive, or from another shape widget of this Widget Blueprint exactly as it is), Fit To Widget
 *              and the flips.
 *
 * Every change goes through the panel's own property handles, so it is one undo step, reaches every selected shape, and
 * is copied from the designer's preview to the Widget Blueprint as any Details edit is.
 */
class FWidgetShapeDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual ~FWidgetShapeDetails() override;

	virtual void CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder) override;
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	TSharedRef<SWidget> MakePresetMenu();
	TSharedRef<SWidget> MakeStartFromMenu();

	void ApplyPreset(const FString& Name, const TArray<TPair<FName, FString>>& Values, bool bOutlineOnly);
	void SavePreset(const FString& Name);

	/** Sets a property on every selected shape, through the panel. False when it isn't shown. */
	bool SetProperty(FName PropertyName, const UClass* Owner, const FString& Value);

	/** Gives every selected Custom Shape this outline. */
	void SetPath(const struct FWidgetShapePath& Path, const FText& Description);

	/** Copies another shape widget onto the selected Custom Shapes: its outline as it is drawn, and its look. */
	void CopyFromShape(const class UShapeWidget* Source);

	/** Lays the panel out again on the next tick, never inside the event that asked. */
	void RefreshSoon();

	TWeakPtr<IDetailLayoutBuilder> WeakBuilder;
	TArray<TWeakObjectPtr<UObject>> Objects;

	/** The kind of shape every selected object is - null for a mix, which presets can't serve. */
	const UClass* ShapeClass = nullptr;

	FDelegateHandle PathEditedHandle;
	FString NewPresetName;
};
