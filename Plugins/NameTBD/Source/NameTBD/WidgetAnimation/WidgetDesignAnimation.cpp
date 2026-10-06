// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetDesignAnimation.h"

#include "NameTBD/Widgets/Shapes/WidgetShapeAnimation.h"

#include "Components/Widget.h"
#include "UObject/UnrealType.h"

namespace WidgetDesignAnimationImpl
{
	/**
	 * The visual settings a Design track keys, by name, wherever a widget's class has them: what most widgets are designed
	 * with. Layout - sizes, alignment, slots - and content - text, children - are left to their own tracks and to the
	 * designer; opacity and the render transform are Fade's and Transform's.
	 */
	const FName DesignPropertyNames[] =
	{
		TEXT("Brush"),                  // Image
		TEXT("Background"),             // Border
		TEXT("ColorAndOpacity"),        // Image, Text Block, Button, User Widget
		TEXT("BrushColor"),             // Border
		TEXT("ContentColorAndOpacity"), // Border
		TEXT("BackgroundColor"),        // Button
		TEXT("Font"),                   // Text Block
		TEXT("ShadowColorAndOpacity"),  // Text Block
		TEXT("ShadowOffset"),           // Text Block
		TEXT("StrikeBrush"),            // Text Block
		TEXT("FillColorAndOpacity"),    // Progress Bar
		TEXT("Percent"),                // Progress Bar
		TEXT("Padding"),                // Border, Button
	};
}

namespace WidgetDesignAnimation
{
	UClass* GetDesignKind(const UClass* WidgetClass)
	{
		UClass* Kind = const_cast<UClass*>(WidgetClass);
		while (Kind && !Kind->HasAnyClassFlags(CLASS_Native))
		{
			Kind = Kind->GetSuperClass();
		}
		return Kind && Kind->IsChildOf(UWidget::StaticClass()) ? Kind : nullptr;
	}

	void GetProperties(const UClass* WidgetClass, TArray<const FProperty*>& OutProperties)
	{
		OutProperties.Reset();
		if (!WidgetClass || !WidgetClass->IsChildOf(UWidget::StaticClass()))
		{
			return;
		}
		for (const FName Name : WidgetDesignAnimationImpl::DesignPropertyNames)
		{
			const FProperty* Property = FindFProperty<FProperty>(WidgetClass, Name);
			if (Property && Property->HasAnyPropertyFlags(CPF_Edit) && !Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated | CPF_EditConst))
			{
				OutProperties.Add(Property);
			}
		}
	}

	bool LayOut(FInstancedPropertyBag& Bag, const UClass* Kind, const UObject* Source)
	{
		TArray<const FProperty*> Properties;
		GetProperties(Kind, Properties);
		return WidgetShapeAnimation::LayOutWith(Bag, Kind, Source, Properties);
	}

	bool IsLaidOutFor(const FInstancedPropertyBag& Bag, const UClass* Kind)
	{
		if (!Kind)
		{
			return Bag.GetPropertyBagStruct() == nullptr;
		}
		TArray<const FProperty*> Properties;
		GetProperties(Kind, Properties);
		return Bag.GetPropertyBagStruct() != nullptr && WidgetShapeAnimation::IsLaidOutWith(Bag, Properties);
	}

	bool WriteBlend(UWidget* Widget, const FProperty* Property, const void* A, const void* B, float Alpha, bool& bOutNeedsSync)
	{
		if (!Widget || !Property || (!A && !B))
		{
			return false;
		}
		const void* From = A ? A : B;
		const void* To = B ? B : A;

		// The value to land on: exactly a key's at a key, else the blend between the two.
		void* Blend = FMemory::Malloc(Property->GetSize(), Property->GetMinAlignment());
		Property->InitializeValue(Blend);
		if (From == To || Alpha == 0.f || Alpha == 1.f)
		{
			Property->CopyCompleteValue(Blend, Alpha == 1.f ? To : From);
		}
		else
		{
			Property->CopyCompleteValue(Blend, From);
			WidgetShapeAnimation::LerpValue(Property, From, To, Blend, Alpha);
		}

		bool bChanged = !Property->Identical(Property->ContainerPtrToValuePtr<void>(Widget), Blend);
		if (bChanged)
		{
			// Through the widget's own setter where it has one - SetBrush, SetColorAndOpacity - which shows the change on
			// screen there and then. Without one, into the widget, to be shown by SynchronizeProperties.
			if (Property->HasSetter())
			{
				Property->SetValue_InContainer(Widget, Blend);
			}
			else
			{
				Property->CopyCompleteValue(Property->ContainerPtrToValuePtr<void>(Widget), Blend);
				bOutNeedsSync = true;
			}
		}

		Property->DestroyValue(Blend);
		FMemory::Free(Blend);
		return bChanged;
	}
}
