// Copyright arubaii. All rights reserved.

#include "CategorizedStructCustomization.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailGroup.h"
#include "PropertyHandle.h"

#define LOCTEXT_NAMESPACE "CategorizedStructCustomization"

TSharedRef<IPropertyTypeCustomization> FCategorizedStructCustomization::MakeInstance()
{
	return MakeShareable(new FCategorizedStructCustomization());
}

void FCategorizedStructCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle,
                                                      FDetailWidgetRow& HeaderRow,
                                                      IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// Default header: the property's own name on the left, nothing on the right. The
	// struct stays expandable exactly as before; only the children are reorganised.
	HeaderRow
		.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget()
		];

	// Cache the struct's name so a redundant leading category segment can be stripped
	// (e.g. "Recoil|Jitter" inside FWeaponRecoil should just read "Jitter").
	StructDisplayName.Reset();
	if (const FStructProperty* StructProperty = CastField<FStructProperty>(PropertyHandle->GetProperty()))
	{
		if (StructProperty->Struct)
		{
			StructDisplayName = StructProperty->Struct->GetName(); // already has no F prefix
		}
	}
}

IDetailGroup* FCategorizedStructCustomization::ResolveGroup(IDetailChildrenBuilder& ChildBuilder,
                                                            const TArray<FString>& Path,
                                                            TMap<FString, IDetailGroup*>& GroupsByPath)
{
	IDetailGroup* Current = nullptr;
	FString Accumulated;

	for (const FString& Segment : Path)
	{
		Accumulated = Accumulated.IsEmpty() ? Segment : (Accumulated + TEXT("|") + Segment);

		if (IDetailGroup** Existing = GroupsByPath.Find(Accumulated))
		{
			Current = *Existing;
			continue;
		}

		// Top-level groups hang off the struct; deeper ones nest inside their parent.
		IDetailGroup& NewGroup = Current
			? Current->AddGroup(FName(*Accumulated), FText::FromString(Segment))
			: ChildBuilder.AddGroup(FName(*Accumulated), FText::FromString(Segment));

		GroupsByPath.Add(Accumulated, &NewGroup);
		Current = &NewGroup;
	}

	return Current;
}

void FCategorizedStructCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle,
                                                        IDetailChildrenBuilder& ChildBuilder,
                                                        IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	uint32 NumChildren = 0;
	PropertyHandle->GetNumChildren(NumChildren);

	// Keyed by full path ("Jitter", "Jitter|Advanced") so intermediate groups are reused
	// rather than recreated for every child that shares a prefix.
	TMap<FString, IDetailGroup*> GroupsByPath;

	for (uint32 Index = 0; Index < NumChildren; ++Index)
	{
		TSharedPtr<IPropertyHandle> ChildHandle = PropertyHandle->GetChildHandle(Index);
		if (!ChildHandle.IsValid() || !ChildHandle->GetProperty())
		{
			continue;
		}

		// Category metadata is stripped from cooked/shipping builds, so guard on WITH_EDITOR
		// semantics implicitly: this whole class is editor-only, and GetMetaData returns an
		// empty string when the key is absent.
		FString Category = ChildHandle->GetMetaData(TEXT("Category"));

		TArray<FString> Path;
		if (!Category.IsEmpty())
		{
			Category.ParseIntoArray(Path, TEXT("|"), /*InCullEmpty*/ true);

			// Drop a leading segment that merely repeats the struct name.
			if (Path.Num() > 0 && !StructDisplayName.IsEmpty()
				&& Path[0].Equals(StructDisplayName, ESearchCase::IgnoreCase))
			{
				Path.RemoveAt(0);
			}
		}

		if (Path.Num() == 0)
		{
			// Uncategorized (or category == struct name): keep it visible at the top level.
			ChildBuilder.AddProperty(ChildHandle.ToSharedRef());
			continue;
		}

		if (IDetailGroup* Group = ResolveGroup(ChildBuilder, Path, GroupsByPath))
		{
			Group->AddPropertyRow(ChildHandle.ToSharedRef());
		}
		else
		{
			ChildBuilder.AddProperty(ChildHandle.ToSharedRef());
		}
	}
}

#undef LOCTEXT_NAMESPACE
