// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetShapeAnimation.h"

#include "ShapeWidget.h"

#include "Styling/SlateBrush.h"
#include "UObject/ObjectKey.h"
#include "UObject/EnumProperty.h"
#include "UObject/UnrealType.h"

namespace WidgetShapeAnimationImpl
{
	/**
	 * A setting's ID in a bag: the same for the same setting of every kind of shape - a Fill declared once on the base is
	 * one setting wherever it appears - so every key of one kind shares one bag layout, and a layout change keeps what
	 * the two kinds share.
	 */
	FGuid MakeId(const FProperty* Property)
	{
		// Worked out once per setting: layouts are checked on every sync of every key.
		static TMap<TPair<FObjectKey, FName>, FGuid> Known;
		const UStruct* Owner = Property->GetOwnerStruct();
		const TPair<FObjectKey, FName> Key(FObjectKey(Owner), Property->GetFName());
		if (const FGuid* Found = Known.Find(Key))
		{
			return *Found;
		}
		const FString Path = (Owner ? Owner->GetPathName() : FString()) + TEXT(":") + Property->GetName();
		return Known.Add(Key, FGuid::NewDeterministicGuid(Path));
	}

	/** Copies one value between properties that may not be of exactly one type - by text when they aren't. */
	void CopyValue(const FProperty* ToProperty, void* To, const FProperty* FromProperty, const void* From)
	{
		if (ToProperty->SameType(FromProperty))
		{
			ToProperty->CopyCompleteValue(To, From);
			return;
		}
		FString Text;
		FromProperty->ExportTextItem_Direct(Text, From, nullptr, nullptr, PPF_None);
		ToProperty->ImportText_Direct(*Text, To, nullptr, PPF_None);
	}

	/** Blended as a unit: a brush's image can't be in between two images, and the rest of it means nothing without it. */
	bool SwitchesWhole(const UScriptStruct* Struct)
	{
		return Struct == FSlateBrush::StaticStruct();
	}
}

namespace WidgetShapeAnimation
{
	void GetProperties(const UClass* ShapeClass, TArray<const FProperty*>& OutProperties)
	{
		OutProperties.Reset();
		if (!ShapeClass || !ShapeClass->IsChildOf(UShapeWidget::StaticClass()))
		{
			return;
		}
		const FName DesiredSize = GET_MEMBER_NAME_CHECKED(UShapeWidget, DesiredSize);
		for (TFieldIterator<FProperty> It(ShapeClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			const FProperty* Property = *It;
			const UClass* Owner = Property->GetOwnerClass();
			if (!Owner || !Owner->IsChildOf(UShapeWidget::StaticClass()))
			{
				continue;
			}
			if (!Property->HasAnyPropertyFlags(CPF_Edit) || Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated))
			{
				continue;
			}
			if (Property->GetFName() == DesiredSize || UShapeWidget::IsContentProperty(Property))
			{
				continue;
			}
			OutProperties.Add(Property);
		}
	}

	bool IsLaidOutFor(const FInstancedPropertyBag& Bag, const UClass* Kind)
	{
		TArray<const FProperty*> Properties;
		GetProperties(Kind, Properties);
		return IsLaidOutWith(Bag, Properties);
	}

	bool IsLaidOutWith(const FInstancedPropertyBag& Bag, const TArray<const FProperty*>& Properties)
	{
		const UPropertyBag* Layout = Bag.GetPropertyBagStruct();
		const int32 NumHeld = Layout ? Layout->GetPropertyDescs().Num() : 0;
		if (NumHeld != Properties.Num())
		{
			return false;
		}
		for (const FProperty* Property : Properties)
		{
			const FPropertyBagPropertyDesc* Desc = Bag.FindPropertyDescByName(Property->GetFName());
			if (!Desc || Desc->ID != WidgetShapeAnimationImpl::MakeId(Property))
			{
				return false;
			}
		}
		return true;
	}

	bool LayOut(FInstancedPropertyBag& Bag, const UClass* Kind, const UObject* Source)
	{
		using namespace WidgetShapeAnimationImpl;

		if (!Kind)
		{
			const bool bHadAny = Bag.GetPropertyBagStruct() != nullptr;
			Bag.Reset();
			return bHadAny;
		}

		TArray<const FProperty*> Properties;
		GetProperties(Kind, Properties);
		return LayOutWith(Bag, Kind, Source, Properties);
	}

	bool LayOutWith(FInstancedPropertyBag& Bag, const UClass* Kind, const UObject* Source, const TArray<const FProperty*>& Properties)
	{
		using namespace WidgetShapeAnimationImpl;

		if (!Kind)
		{
			const bool bHadAny = Bag.GetPropertyBagStruct() != nullptr;
			Bag.Reset();
			return bHadAny;
		}
		if (IsLaidOutWith(Bag, Properties))
		{
			return false;
		}

		TArray<FPropertyBagPropertyDesc> Descs;
		Descs.Reserve(Properties.Num());
		for (const FProperty* Property : Properties)
		{
			FPropertyBagPropertyDesc& Desc = Descs.Emplace_GetRef(Property->GetFName(), Property);
			Desc.ID = MakeId(Property);
#if WITH_EDITORONLY_DATA
			// The setting's own limits, units, conditions and tooltip, so a key's fields read as the shape's own do.
			Desc.MetaData.Reset();
			if (const TMap<FName, FString>* MetaData = Property->GetMetaDataMap())
			{
				for (const TPair<FName, FString>& Pair : *MetaData)
				{
					Desc.MetaData.Add(FPropertyBagPropertyDescMetaData(Pair.Key, Pair.Value));
				}
			}
#endif
		}

		FInstancedPropertyBag Fresh;
		Fresh.InitializeFromBagStruct(UPropertyBag::GetOrCreateFromDescs(Descs));

		// The values: what the shape holds now (or the kind's defaults), then whatever the bag already held of them.
		const UObject* From = Source && Source->IsA(Kind) ? Source : Kind->GetDefaultObject();
		Capture(Fresh, From);

		const FConstStructView Old = Bag.GetValue();
		const UPropertyBag* OldLayout = Bag.GetPropertyBagStruct();
		const FStructView New = Fresh.GetMutableValue();
		if (OldLayout && Old.GetMemory())
		{
			for (const FPropertyBagPropertyDesc& OldDesc : OldLayout->GetPropertyDescs())
			{
				const FPropertyBagPropertyDesc* NewDesc = Fresh.FindPropertyDescByName(OldDesc.Name);
				if (NewDesc && NewDesc->CachedProperty && OldDesc.CachedProperty)
				{
					CopyValue(NewDesc->CachedProperty, NewDesc->CachedProperty->ContainerPtrToValuePtr<void>(New.GetMemory()),
						OldDesc.CachedProperty, OldDesc.CachedProperty->ContainerPtrToValuePtr<void>(Old.GetMemory()));
				}
			}
		}

		Bag = MoveTemp(Fresh);
		return true;
	}

	void Capture(FInstancedPropertyBag& Bag, const UObject* Source)
	{
		const UPropertyBag* Layout = Bag.GetPropertyBagStruct();
		const FStructView Values = Bag.GetMutableValue();
		if (!Source || !Layout || !Values.GetMemory())
		{
			return;
		}
		for (const FPropertyBagPropertyDesc& Desc : Layout->GetPropertyDescs())
		{
			const FProperty* SourceProperty = FindFProperty<FProperty>(Source->GetClass(), Desc.Name);
			if (Desc.CachedProperty && SourceProperty)
			{
				WidgetShapeAnimationImpl::CopyValue(Desc.CachedProperty, Desc.CachedProperty->ContainerPtrToValuePtr<void>(Values.GetMemory()),
					SourceProperty, SourceProperty->ContainerPtrToValuePtr<void>(Source));
			}
		}
	}

	void LerpValue(const FProperty* Property, const void* A, const void* B, void* Out, float Alpha)
	{
		if (const FFloatProperty* Float = CastField<FFloatProperty>(Property))
		{
			Float->SetPropertyValue(Out, FMath::Lerp(Float->GetPropertyValue(A), Float->GetPropertyValue(B), Alpha));
		}
		else if (const FDoubleProperty* Double = CastField<FDoubleProperty>(Property))
		{
			Double->SetPropertyValue(Out, FMath::Lerp(Double->GetPropertyValue(A), Double->GetPropertyValue(B), static_cast<double>(Alpha)));
		}
		else if (const FIntProperty* Int = CastField<FIntProperty>(Property))
		{
			const double Blended = FMath::Lerp(static_cast<double>(Int->GetPropertyValue(A)), static_cast<double>(Int->GetPropertyValue(B)), static_cast<double>(Alpha));
			Int->SetPropertyValue(Out, static_cast<int32>(FMath::RoundToDouble(Blended)));
		}
		else if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
		{
			if (WidgetShapeAnimationImpl::SwitchesWhole(Struct->Struct))
			{
				Property->CopySingleValue(Out, Alpha < 0.5f ? A : B);
				return;
			}
			for (TFieldIterator<FProperty> It(Struct->Struct); It; ++It)
			{
				const FProperty* Member = *It;
				for (int32 Index = 0; Index < Member->ArrayDim; ++Index)
				{
					LerpValue(Member, Member->ContainerPtrToValuePtr<void>(A, Index), Member->ContainerPtrToValuePtr<void>(B, Index),
						Member->ContainerPtrToValuePtr<void>(Out, Index), Alpha);
				}
			}
		}
		else if (const FArrayProperty* Array = CastField<FArrayProperty>(Property))
		{
			FScriptArrayHelper ArrayA(Array, A);
			FScriptArrayHelper ArrayB(Array, B);
			if (ArrayA.Num() != ArrayB.Num())
			{
				Property->CopySingleValue(Out, Alpha < 0.5f ? A : B);
				return;
			}
			FScriptArrayHelper ArrayOut(Array, Out);
			if (ArrayOut.Num() != ArrayA.Num())
			{
				Property->CopySingleValue(Out, A);
			}
			for (int32 Index = 0; Index < ArrayA.Num(); ++Index)
			{
				LerpValue(Array->Inner, ArrayA.GetRawPtr(Index), ArrayB.GetRawPtr(Index), ArrayOut.GetRawPtr(Index), Alpha);
			}
		}
		else
		{
			// On or off, a choice from a list, an asset: nothing in between to pass through, so it changes halfway.
			Property->CopySingleValue(Out, Alpha < 0.5f ? A : B);
		}
	}

	FInstancedPropertyBag LerpBags(const FInstancedPropertyBag& A, const FInstancedPropertyBag& B, float Alpha)
	{
		const UPropertyBag* Layout = A.GetPropertyBagStruct();
		if (!Layout || Layout != B.GetPropertyBagStruct())
		{
			return Alpha < 0.5f ? A : B;
		}

		FInstancedPropertyBag Out = A;
		const FConstStructView ValuesA = A.GetValue();
		const FConstStructView ValuesB = B.GetValue();
		const FStructView ValuesOut = Out.GetMutableValue();
		for (const FPropertyBagPropertyDesc& Desc : Layout->GetPropertyDescs())
		{
			if (const FProperty* Property = Desc.CachedProperty)
			{
				LerpValue(Property, Property->ContainerPtrToValuePtr<void>(ValuesA.GetMemory()), Property->ContainerPtrToValuePtr<void>(ValuesB.GetMemory()),
					Property->ContainerPtrToValuePtr<void>(ValuesOut.GetMemory()), Alpha);
			}
		}
		return Out;
	}

	const void* FindValue(const FInstancedPropertyBag& Bag, const FProperty* Property)
	{
		const FPropertyBagPropertyDesc* Desc = Property ? Bag.FindPropertyDescByName(Property->GetFName()) : nullptr;
		const FConstStructView Values = Bag.GetValue();
		if (!Desc || !Desc->CachedProperty || !Values.GetMemory() || !Desc->CachedProperty->SameType(Property))
		{
			return nullptr;
		}
		return Desc->CachedProperty->ContainerPtrToValuePtr<void>(Values.GetMemory());
	}

	bool WriteBlend(UObject* Shape, const FProperty* Property, const void* A, const void* B, float Alpha)
	{
		if (!Shape || !Property || (!A && !B))
		{
			return false;
		}
		const void* From = A ? A : B;
		const void* To = B ? B : A;
		void* Target = Property->ContainerPtrToValuePtr<void>(Shape);

		// Exactly a key's value at a key - the common case of a track resting on one - with nothing to blend.
		if (From == To || Alpha == 0.f || Alpha == 1.f)
		{
			const void* Exact = Alpha == 1.f ? To : From;
			if (Property->Identical(Target, Exact))
			{
				return false;
			}
			Property->CopyCompleteValue(Target, Exact);
			return true;
		}

		void* Blend = FMemory::Malloc(Property->GetSize(), Property->GetMinAlignment());
		Property->InitializeValue(Blend);
		Property->CopyCompleteValue(Blend, From);
		LerpValue(Property, From, To, Blend, Alpha);

		const bool bChanged = !Property->Identical(Target, Blend);
		if (bChanged)
		{
			Property->CopyCompleteValue(Target, Blend);
		}

		Property->DestroyValue(Blend);
		FMemory::Free(Blend);
		return bChanged;
	}
}
