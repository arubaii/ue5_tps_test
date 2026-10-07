// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetDesignAnimation.h"

#include "NameTBD/Widgets/Shapes/WidgetShapeAnimation.h"

#include "Algo/Find.h"
#include "Components/Widget.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateColor.h"
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

	/**
	 * One design setting blended from A toward B, Alpha of the way, into Out - which starts as a copy of whichever of the two is
	 * nearer (see WriteBlends). Numbers glide, and so does every number inside a struct, member by member: a brush's tint,
	 * size, margin and outline as well as a color or a font's size. Not a Shape track's blend (WidgetShapeAnimation::LerpValue),
	 * which switches a whole brush halfway, tint and all. What has nothing in between - a brush's image, a font face or
	 * material, a choice from a list - is left as Out holds it, and cross-faded instead: see HasDiscreteDifference.
	 */
	void LerpDesignValue(const FProperty* Property, const void* A, const void* B, void* Out, float Alpha)
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
			for (TFieldIterator<FProperty> It(Struct->Struct); It; ++It)
			{
				const FProperty* Member = *It;
				for (int32 Index = 0; Index < Member->ArrayDim; ++Index)
				{
					LerpDesignValue(Member, Member->ContainerPtrToValuePtr<void>(A, Index), Member->ContainerPtrToValuePtr<void>(B, Index),
						Member->ContainerPtrToValuePtr<void>(Out, Index), Alpha);
				}
			}
		}
		else if (const FArrayProperty* Array = CastField<FArrayProperty>(Property))
		{
			// Element by element while both keys hold as many; otherwise the list is one of the things held until the key.
			FScriptArrayHelper ArrayA(Array, A);
			FScriptArrayHelper ArrayB(Array, B);
			FScriptArrayHelper ArrayOut(Array, Out);
			if (ArrayA.Num() == ArrayB.Num() && ArrayOut.Num() == ArrayA.Num())
			{
				for (int32 Index = 0; Index < ArrayA.Num(); ++Index)
				{
					LerpDesignValue(Array->Inner, ArrayA.GetRawPtr(Index), ArrayB.GetRawPtr(Index), ArrayOut.GetRawPtr(Index), Alpha);
				}
			}
		}
		// Anything else - an asset, a name, a choice from a list, an on/off - is left as Out already holds it.
	}

	/**
	 * Whether A and B differ in anything LerpDesignValue can't glide between - a brush's image or how it is drawn, a font's
	 * face, material or outline material, an on/off. Such a setting is cross-faded: see WriteBlends.
	 */
	bool HasDiscreteDifference(const FProperty* Property, const void* A, const void* B)
	{
		if (Property->IsA<FFloatProperty>() || Property->IsA<FDoubleProperty>() || Property->IsA<FIntProperty>())
		{
			return false;
		}
		if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
		{
			for (TFieldIterator<FProperty> It(Struct->Struct); It; ++It)
			{
				const FProperty* Member = *It;
				for (int32 Index = 0; Index < Member->ArrayDim; ++Index)
				{
					if (HasDiscreteDifference(Member, Member->ContainerPtrToValuePtr<void>(A, Index), Member->ContainerPtrToValuePtr<void>(B, Index)))
					{
						return true;
					}
				}
			}
			return false;
		}
		if (const FArrayProperty* Array = CastField<FArrayProperty>(Property))
		{
			FScriptArrayHelper ArrayA(Array, A);
			FScriptArrayHelper ArrayB(Array, B);
			if (ArrayA.Num() != ArrayB.Num())
			{
				return true;
			}
			for (int32 Index = 0; Index < ArrayA.Num(); ++Index)
			{
				if (HasDiscreteDifference(Array->Inner, ArrayA.GetRawPtr(Index), ArrayB.GetRawPtr(Index)))
				{
					return true;
				}
			}
			return false;
		}
		return !Property->Identical(A, B);
	}

	/** A color's opacity scaled by Fade - an FSlateColor only while it holds a color of its own, not a theme's. */
	void FadeColor(const UScriptStruct* Struct, void* Value, float Fade)
	{
		if (Struct == FSlateColor::StaticStruct())
		{
			FSlateColor& Color = *static_cast<FSlateColor*>(Value);
			if (Color.IsColorSpecified())
			{
				const FLinearColor Specified = Color.GetSpecifiedColor();
				Color = FSlateColor(Specified.CopyWithNewOpacity(Specified.A * Fade));
			}
		}
		else if (Struct == TBaseStructure<FLinearColor>::Get())
		{
			static_cast<FLinearColor*>(Value)->A *= Fade;
		}
	}

	/**
	 * How visible a cross-faded setting is Alpha of the way from one key to the next: fading out from the key being left to
	 * nothing halfway, where it changes over, and back in to the next key - so an image or a font face blends into the next
	 * through transparency, instead of switching in one frame. A widget shows one brush and one font at a time, so the two
	 * can't both be drawn at once.
	 */
	float CrossFadeAt(float Alpha)
	{
		const float Clamped = FMath::Clamp(Alpha, 0.f, 1.f);
		return Clamped < 0.5f ? 1.f - Clamped * 2.f : Clamped * 2.f - 1.f;
	}

	/**
	 * Settings a style of the widget's owns: a Common Text Block with a Text Style set draws its font, colors, shadow and
	 * strike brush from that style - its Details make them read-only, and it puts them back from the style whenever it is
	 * synchronized - so a Design track leaves them alone rather than fight it. Read off the widget by name, so it needs no
	 * Common UI here.
	 */
	bool IsOwnedByStyle(const UWidget* Widget, const FProperty* Property)
	{
		static const FName StyleOwned[] =
		{
			TEXT("Font"), TEXT("ColorAndOpacity"), TEXT("StrikeBrush"), TEXT("ShadowOffset"), TEXT("ShadowColorAndOpacity"),
		};
		if (!Algo::Find(StyleOwned, Property->GetFName()))
		{
			return false;
		}
		const FObjectPropertyBase* Style = FindFProperty<FObjectPropertyBase>(Widget->GetClass(), TEXT("Style"));
		return Style && Style->GetObjectPropertyValue_InContainer(Widget) != nullptr;
	}

	/** Writes one setting's value onto Widget, through its setter where it has one. See WidgetDesignAnimation::WriteBlend. */
	bool WriteValue(UWidget* Widget, const FProperty* Property, const void* Value, bool& bOutNeedsSync)
	{
		if (Property->Identical(Property->ContainerPtrToValuePtr<void>(Widget), Value))
		{
			return false;
		}

		// Through the widget's own setter where it has one - SetBrush, SetColorAndOpacity - which shows the change on screen
		// there and then. Without one, into the widget, to be shown by SynchronizeProperties.
		if (Property->HasSetter())
		{
			Property->SetValue_InContainer(Widget, Value);
		}
		else
		{
			Property->CopyCompleteValue(Property->ContainerPtrToValuePtr<void>(Widget), Value);
			bOutNeedsSync = true;
		}
		return true;
	}
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
		const TArray<const FProperty*> Properties = { Property };
		const TArray<const void*> From = { A };
		const TArray<const void*> To = { B };
		bool bChanged = false;
		WriteBlends(Widget, Properties, From, To, Alpha, &bChanged, &bOutNeedsSync);
		return bChanged;
	}

	void WriteBlends(UWidget* Widget, const TArray<const FProperty*>& Properties, const TArray<const void*>& From, const TArray<const void*>& To, float Alpha, bool* bOutChanged, bool* bOutNeedsSync)
	{
		using namespace WidgetDesignAnimationImpl;

		bool bChanged = false;
		bool bNeedsSync = false;

		// Only ever onto a widget shown - a preview, or one playing - never a Widget Blueprint's template or a class default,
		// which are what UMG's Details edit: written to, they would hold animated values as if designed.
		if (!Widget || Widget->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject) || Properties.Num() != From.Num() || Properties.Num() != To.Num())
		{
			return;
		}

		// Every value first, then every write: a font changing face fades the colors the text is drawn in, which are settings
		// of their own.
		TArray<void*, TInlineAllocator<16>> Blends;
		Blends.SetNumZeroed(Properties.Num());
		float TextFade = 1.f;
		for (int32 Index = 0; Index < Properties.Num(); ++Index)
		{
			const FProperty* Property = Properties[Index];
			const void* A = From[Index] ? From[Index] : To[Index];
			const void* B = To[Index] ? To[Index] : From[Index];
			if (!Property || !A || IsOwnedByStyle(Widget, Property))
			{
				continue;
			}

			void* Blend = FMemory::Malloc(Property->GetSize(), Property->GetMinAlignment());
			Property->InitializeValue(Blend);
			Blends[Index] = Blend;

			// Exactly a key's value at a key.
			if (A == B || Alpha <= 0.f || Alpha >= 1.f)
			{
				Property->CopyCompleteValue(Blend, Alpha >= 1.f ? B : A);
				continue;
			}

			// Between them: whatever has nothing in between is the nearer key's - a brush whole, with its image, so its image
			// and everything that goes with it stay together - and every number glides.
			Property->CopyCompleteValue(Blend, Alpha < 0.5f ? A : B);
			LerpDesignValue(Property, A, B, Blend, Alpha);

			// An image, or anything else of a brush or a font that can't glide, cross-fades through transparency: a brush by
			// its own tint and outline, a font by the colors its text is drawn in.
			const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
			if (StructProperty && HasDiscreteDifference(Property, A, B))
			{
				const float Fade = CrossFadeAt(Alpha);
				if (StructProperty->Struct == FSlateBrush::StaticStruct())
				{
					FSlateBrush& Brush = *static_cast<FSlateBrush*>(Blend);
					FadeColor(FSlateColor::StaticStruct(), &Brush.TintColor, Fade);
					FadeColor(FSlateColor::StaticStruct(), &Brush.OutlineSettings.Color, Fade);
				}
				else if (StructProperty->Struct == FSlateFontInfo::StaticStruct())
				{
					TextFade = FMath::Min(TextFade, Fade);
				}
			}
		}

		if (TextFade < 1.f)
		{
			for (int32 Index = 0; Index < Properties.Num(); ++Index)
			{
				const FStructProperty* StructProperty = CastField<FStructProperty>(Properties[Index]);
				const FName Name = StructProperty ? StructProperty->GetFName() : NAME_None;
				if (Blends[Index] && (Name == TEXT("ColorAndOpacity") || Name == TEXT("ShadowColorAndOpacity")))
				{
					FadeColor(StructProperty->Struct, Blends[Index], TextFade);
				}
			}
		}

		for (int32 Index = 0; Index < Properties.Num(); ++Index)
		{
			if (void* Blend = Blends[Index])
			{
				bChanged |= WriteValue(Widget, Properties[Index], Blend, bNeedsSync);
				Properties[Index]->DestroyValue(Blend);
				FMemory::Free(Blend);
			}
		}

		if (bOutChanged)
		{
			*bOutChanged = bChanged;
		}
		if (bOutNeedsSync)
		{
			*bOutNeedsSync = *bOutNeedsSync || bNeedsSync;
		}
	}
}
