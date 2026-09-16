// Copyright AR Game Systems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "NameTBD/DeveloperSettings/InfoCardDeveloperSettings.h" 
#include "ContextMenuBuilderBase.generated.h"

class UHorizontalBox;
class UTextBlock;
class UButton;
class UImage;

UCLASS(Abstract, Blueprintable)
class NAMETBD_API UContextMenuBuilderBase : public UUserWidget
{
	GENERATED_BODY()

public:
	// Expose this to Blueprints so you can pipe in the struct data directly
	UFUNCTION(BlueprintCallable, Category = "Context Menu")
	void BuildFromData(const FContextMenuButtonData& InButtonData);

protected:
	void BuildIconBox(UHorizontalBox* TargetBox, const TArray<FContextButtonImageChoice>& Icons);

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Context Menu")
	FVector2D IconSize = FVector2D(32.0, 32.0);

	// --- Layout Containers ---
	
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	UHorizontalBox* LeftIconsBox;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	UHorizontalBox* CenterIconsBox;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	UHorizontalBox* RightIconsBox;

	// --- Bound Controls ---
	
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	UButton* EquipButton;

	// --- Texts ---
	
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	UTextBlock* LeftText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	UTextBlock* CenterText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	UTextBlock* RightText;
};