// Copyright Bilcash 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "StructCategoryGroups_TestStruct.generated.h"

/*
	DEMO FILE
	---------
	These structs exist purely to demonstrate the plugin. Three Blueprint-facing
	structs are provided:

	  * FStructCategoryGroups_WithPlugin    - opted in via meta = (UseCategoryGroups)
	  * 
	  * FStructCategoryGroups_WithoutPlugin - identical shape, but NOT opted in, so
	                                          it shows Unreal's default layout for a
	                                          side-by-side
	  * FStructCategoryGroups_RowWrapper    - wraps the WithPlugin struct as a Data
	                                          Table row (see ShowOnlyInnerProperties).
*/

/* StructCategoryGroups demo struct */
USTRUCT(BlueprintType, meta = (UseCategoryGroups))
struct FStructCategoryGroupsShowcase_WithPlugin
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alpha")
	bool W = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alpha | Beta")
	int X = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alpha | Beta | Gamma")
	FVector2D Y = FVector2D(1.f, 1.f);
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alpha | Beta | Gamma")
	FLinearColor Z = FLinearColor::White;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Omega")
	bool A = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Omega")
	bool B = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Psi")
	bool C = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zeta")
	bool D = false;
};

/* StructCategoryGroups demo struct */
USTRUCT(BlueprintType)
struct FStructCategoryGroupsShowcase_WithoutPlugin
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alpha")
	bool W = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alpha | Beta")
	int X = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alpha | Beta | Gamma")
	FVector2D Y = FVector2D(1.f, 1.f);
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alpha | Beta | Gamma")
	FLinearColor Z = FLinearColor::White;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Omega")
	bool A = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Omega")
	bool B = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Psi")
	bool C = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zeta")
	bool D = false;
};

/* StructCategoryGroups demo row wrapper */
USTRUCT(BlueprintType)
struct FStructCategoryGroups_RowWrapper : public FTableRowBase
{
	GENERATED_BODY()

	/** ShowOnlyInnerProperties flattens the wrapper so the grouped/colored fields
	 *  appear directly in the Data Table row editor. */
	UPROPERTY(EditAnywhere, Category = "Row", meta = (ShowOnlyInnerProperties))
	FStructCategoryGroupsShowcase_WithPlugin Data;
};