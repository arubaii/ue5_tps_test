// Copyright AR GameSystems 2026. All rights reserved.

#include "NiagaraUIParameters.h"

#include "Curves/CurveFloat.h"
#include "Curves/CurveLinearColor.h"
#include "Curves/CurveVector.h"
#include "Curves/RichCurve.h"
#include "Engine/Texture.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraComponent.h"
#include "NiagaraDataInterface.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "NiagaraDataInterfaceCurveBase.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "NiagaraUserRedirectionParameterStore.h"

namespace NiagaraUIParameters
{
	namespace
	{
		// ---------------------------------------------------------------------------------------------
		// Data interfaces: curves and arrays
		// ---------------------------------------------------------------------------------------------

		/**
		 * The User parameter data interfaces listed: those that hold a value a UI effect is tuned by. Known by class name, so
		 * none of their headers - nor their exports - are needed. Texture Sample isn't one: it samples on the GPU only, and only
		 * CPU emitters are drawn in UI. Mesh, grid, render target and other scene data interfaces mean nothing in UI.
		 */
		enum class EDataInterfaceKind : uint8
		{
			None,
			Curve,
			ColorCurve,
			VectorCurve,
			Vector2DCurve,
			ArrayFloat,
			ArrayInt32,
			ArrayBool,
			ArrayVector2D,
			ArrayVector,
			ArrayPosition,
			ArrayVector4,
			ArrayColor,
			ArrayQuat
		};

		EDataInterfaceKind GetDataInterfaceKind(const UClass* Class)
		{
			static const TMap<FName, EDataInterfaceKind> Kinds =
			{
				{ TEXT("NiagaraDataInterfaceCurve"), EDataInterfaceKind::Curve },
				{ TEXT("NiagaraDataInterfaceColorCurve"), EDataInterfaceKind::ColorCurve },
				{ TEXT("NiagaraDataInterfaceVectorCurve"), EDataInterfaceKind::VectorCurve },
				{ TEXT("NiagaraDataInterfaceVector2DCurve"), EDataInterfaceKind::Vector2DCurve },
				{ TEXT("NiagaraDataInterfaceArrayFloat"), EDataInterfaceKind::ArrayFloat },
				{ TEXT("NiagaraDataInterfaceArrayInt32"), EDataInterfaceKind::ArrayInt32 },
				{ TEXT("NiagaraDataInterfaceArrayBool"), EDataInterfaceKind::ArrayBool },
				{ TEXT("NiagaraDataInterfaceArrayFloat2"), EDataInterfaceKind::ArrayVector2D },
				{ TEXT("NiagaraDataInterfaceArrayFloat3"), EDataInterfaceKind::ArrayVector },
				{ TEXT("NiagaraDataInterfaceArrayPosition"), EDataInterfaceKind::ArrayPosition },
				{ TEXT("NiagaraDataInterfaceArrayFloat4"), EDataInterfaceKind::ArrayVector4 },
				{ TEXT("NiagaraDataInterfaceArrayColor"), EDataInterfaceKind::ArrayColor },
				{ TEXT("NiagaraDataInterfaceArrayQuat"), EDataInterfaceKind::ArrayQuat },
			};
			const EDataInterfaceKind* Kind = Class ? Kinds.Find(Class->GetFName()) : nullptr;
			return Kind ? *Kind : EDataInterfaceKind::None;
		}

		bool IsCurveKind(EDataInterfaceKind Kind)
		{
			return Kind == EDataInterfaceKind::Curve || Kind == EDataInterfaceKind::ColorCurve || Kind == EDataInterfaceKind::VectorCurve || Kind == EDataInterfaceKind::Vector2DCurve;
		}

		/** A curve data interface's curves, by their property names: what the bag's curve struct holds, channel by channel. */
		TArrayView<const TCHAR* const> GetCurveNames(EDataInterfaceKind Kind)
		{
			static const TCHAR* const Single[] = { TEXT("Curve") };
			static const TCHAR* const Color[] = { TEXT("RedCurve"), TEXT("GreenCurve"), TEXT("BlueCurve"), TEXT("AlphaCurve") };
			static const TCHAR* const Vector[] = { TEXT("XCurve"), TEXT("YCurve"), TEXT("ZCurve") };
			switch (Kind)
			{
			case EDataInterfaceKind::Curve: return Single;
			case EDataInterfaceKind::ColorCurve: return Color;
			case EDataInterfaceKind::VectorCurve: return Vector;
			case EDataInterfaceKind::Vector2DCurve: return TArrayView<const TCHAR* const>(Vector, 2);
			default: return {};
			}
		}

		/** The struct a curve is listed as: Curve for Floats as a float curve, Color Curve as a color curve, the vector ones as a vector curve (a Vector 2D Curve its X and Y). */
		const UScriptStruct* GetCurveStruct(EDataInterfaceKind Kind)
		{
			switch (Kind)
			{
			case EDataInterfaceKind::Curve: return FRuntimeFloatCurve::StaticStruct();
			case EDataInterfaceKind::ColorCurve: return FRuntimeCurveLinearColor::StaticStruct();
			case EDataInterfaceKind::VectorCurve:
			case EDataInterfaceKind::Vector2DCurve: return FRuntimeVectorCurve::StaticStruct();
			default: return nullptr;
			}
		}

		/** Channel Index of a bag's curve struct at Value (of StructType) - its external curve's when it names one. */
		FRichCurve* GetBagCurve(const UScriptStruct* StructType, void* Value, int32 Index)
		{
			if (StructType == FRuntimeFloatCurve::StaticStruct())
			{
				FRuntimeFloatCurve& Curve = *static_cast<FRuntimeFloatCurve*>(Value);
				return Index == 0 ? (Curve.ExternalCurve ? &Curve.ExternalCurve->FloatCurve : &Curve.EditorCurveData) : nullptr;
			}
			if (StructType == FRuntimeCurveLinearColor::StaticStruct())
			{
				FRuntimeCurveLinearColor& Curve = *static_cast<FRuntimeCurveLinearColor*>(Value);
				return Index >= 0 && Index < 4 ? (Curve.ExternalCurve ? &Curve.ExternalCurve->FloatCurves[Index] : &Curve.ColorCurves[Index]) : nullptr;
			}
			if (StructType == FRuntimeVectorCurve::StaticStruct())
			{
				FRuntimeVectorCurve& Curve = *static_cast<FRuntimeVectorCurve*>(Value);
				return Index >= 0 && Index < 3 ? (Curve.ExternalCurve ? &Curve.ExternalCurve->FloatCurves[Index] : &Curve.VectorCurves[Index]) : nullptr;
			}
			return nullptr;
		}

		/** Curve Name of a curve data interface. */
		FRichCurve* GetDataInterfaceCurve(UNiagaraDataInterface* DataInterface, const TCHAR* Name)
		{
			const FStructProperty* Property = DataInterface ? FindFProperty<FStructProperty>(DataInterface->GetClass(), Name) : nullptr;
			return Property && Property->Struct == FRichCurve::StaticStruct() ? Property->ContainerPtrToValuePtr<FRichCurve>(DataInterface) : nullptr;
		}

		/** Where parameter Desc of Bag keeps its value, to write. */
		void* GetMutableBagValuePtr(FInstancedPropertyBag& Bag, const FPropertyBagPropertyDesc& Desc)
		{
			uint8* Memory = Bag.GetMutableValue().GetMemory();
			return Memory && Desc.CachedProperty ? Desc.CachedProperty->ContainerPtrToValuePtr<void>(Memory) : nullptr;
		}

		const void* GetBagValuePtr(const FInstancedPropertyBag& Bag, const FPropertyBagPropertyDesc& Desc)
		{
			const uint8* Memory = Bag.GetValue().GetMemory();
			return Memory && Desc.CachedProperty ? Desc.CachedProperty->ContainerPtrToValuePtr<void>(Memory) : nullptr;
		}

		/** An array parameter of a bag, as a typed array - T matching the bag's element type. */
		template <typename T>
		TArray<T> ReadBagArray(const FInstancedPropertyBag& Bag, const FPropertyBagPropertyDesc& Desc)
		{
			TArray<T> Result;
			const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Desc.CachedProperty);
			const void* Value = GetBagValuePtr(Bag, Desc);
			if (!ArrayProperty || !Value || ArrayProperty->Inner->GetElementSize() != sizeof(T))
			{
				return Result;
			}
			FScriptArrayHelper Helper(ArrayProperty, Value);
			Result.Reserve(Helper.Num());
			for (int32 Index = 0; Index < Helper.Num(); ++Index)
			{
				Result.Add(*reinterpret_cast<const T*>(Helper.GetRawPtr(Index)));
			}
			return Result;
		}

		template <typename T>
		void WriteBagArray(FInstancedPropertyBag& Bag, const FPropertyBagPropertyDesc& Desc, const TArray<T>& Items)
		{
			const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Desc.CachedProperty);
			void* Value = GetMutableBagValuePtr(Bag, Desc);
			if (!ArrayProperty || !Value || ArrayProperty->Inner->GetElementSize() != sizeof(T))
			{
				return;
			}
			FScriptArrayHelper Helper(ArrayProperty, Value);
			Helper.Resize(Items.Num());
			for (int32 Index = 0; Index < Items.Num(); ++Index)
			{
				*reinterpret_cast<T*>(Helper.GetRawPtr(Index)) = Items[Index];
			}
		}

		/**
		 * The values an array data interface holds, NumFloats floats each (or one int / bool), read through its one array
		 * property - by reflection, so its member's name doesn't matter.
		 */
		const FArrayProperty* FindDataInterfaceArray(const UNiagaraDataInterface* DataInterface)
		{
			if (!DataInterface)
			{
				return nullptr;
			}
			for (TFieldIterator<FArrayProperty> It(DataInterface->GetClass()); It; ++It)
			{
				return *It;
			}
			return nullptr;
		}

		TArray<float> ReadDataInterfaceFloats(const UNiagaraDataInterface* DataInterface, int32 NumFloats)
		{
			TArray<float> Flat;
			const FArrayProperty* ArrayProperty = FindDataInterfaceArray(DataInterface);
			if (!ArrayProperty || ArrayProperty->Inner->GetElementSize() != NumFloats * static_cast<int32>(sizeof(float)))
			{
				return Flat;
			}
			FScriptArrayHelper Helper(ArrayProperty, ArrayProperty->ContainerPtrToValuePtr<void>(DataInterface));
			Flat.Reserve(Helper.Num() * NumFloats);
			for (int32 Index = 0; Index < Helper.Num(); ++Index)
			{
				const float* Element = reinterpret_cast<const float*>(Helper.GetRawPtr(Index));
				Flat.Append(Element, NumFloats);
			}
			return Flat;
		}

		template <typename T>
		TArray<T> ReadDataInterfaceValues(const UNiagaraDataInterface* DataInterface)
		{
			TArray<T> Values;
			const FArrayProperty* ArrayProperty = FindDataInterfaceArray(DataInterface);
			if (!ArrayProperty || ArrayProperty->Inner->GetElementSize() != sizeof(T))
			{
				return Values;
			}
			FScriptArrayHelper Helper(ArrayProperty, ArrayProperty->ContainerPtrToValuePtr<void>(DataInterface));
			Values.Reserve(Helper.Num());
			for (int32 Index = 0; Index < Helper.Num(); ++Index)
			{
				Values.Add(*reinterpret_cast<const T*>(Helper.GetRawPtr(Index)));
			}
			return Values;
		}

		/** What a data interface parameter is in the bag. False for one it doesn't list. */
		bool MakeDataInterfaceDesc(FName Name, const UClass* Class, FPropertyBagPropertyDesc& OutDesc)
		{
			const EDataInterfaceKind Kind = GetDataInterfaceKind(Class);
			if (IsCurveKind(Kind))
			{
				OutDesc = FPropertyBagPropertyDesc(Name, EPropertyBagPropertyType::Struct, GetCurveStruct(Kind));
				return true;
			}
			const auto MakeArray = [&OutDesc, Name](EPropertyBagPropertyType ValueType, const UObject* ValueTypeObject = nullptr)
			{
				OutDesc = FPropertyBagPropertyDesc(Name, EPropertyBagContainerType::Array, ValueType, ValueTypeObject);
				return true;
			};
			switch (Kind)
			{
			case EDataInterfaceKind::ArrayFloat: return MakeArray(EPropertyBagPropertyType::Float);
			case EDataInterfaceKind::ArrayInt32: return MakeArray(EPropertyBagPropertyType::Int32);
			case EDataInterfaceKind::ArrayBool: return MakeArray(EPropertyBagPropertyType::Bool);
			case EDataInterfaceKind::ArrayVector2D: return MakeArray(EPropertyBagPropertyType::Struct, TBaseStructure<FVector2D>::Get());
			case EDataInterfaceKind::ArrayVector:
			case EDataInterfaceKind::ArrayPosition: return MakeArray(EPropertyBagPropertyType::Struct, TBaseStructure<FVector>::Get());
			case EDataInterfaceKind::ArrayVector4: return MakeArray(EPropertyBagPropertyType::Struct, TBaseStructure<FVector4>::Get());
			case EDataInterfaceKind::ArrayColor: return MakeArray(EPropertyBagPropertyType::Struct, TBaseStructure<FLinearColor>::Get());
			case EDataInterfaceKind::ArrayQuat: return MakeArray(EPropertyBagPropertyType::Struct, TBaseStructure<FQuat>::Get());
			default: return false;
			}
		}

		/** A data interface's value (the system's), into Bag's parameter Name. */
		void ReadDataInterfaceValue(UNiagaraDataInterface* DataInterface, FName Name, FInstancedPropertyBag& Bag)
		{
			const FPropertyBagPropertyDesc* Desc = DataInterface ? Bag.FindPropertyDescByName(Name) : nullptr;
			if (!Desc)
			{
				return;
			}
			const EDataInterfaceKind Kind = GetDataInterfaceKind(DataInterface->GetClass());

			if (IsCurveKind(Kind))
			{
				void* Value = GetMutableBagValuePtr(Bag, *Desc);
				const UScriptStruct* StructType = Cast<UScriptStruct>(Desc->ValueTypeObject);
				const TArrayView<const TCHAR* const> Names = GetCurveNames(Kind);
				for (int32 Index = 0; Value && Index < Names.Num(); ++Index)
				{
					FRichCurve* Into = GetBagCurve(StructType, Value, Index);
					const FRichCurve* From = GetDataInterfaceCurve(DataInterface, Names[Index]);
					if (Into && From)
					{
						*Into = *From;
					}
				}
				return;
			}

			// Each element as the bag holds it: the data interface's floats widened to doubles where the bag's type is.
			const auto ReadFloats = [DataInterface](int32 NumFloats) { return ReadDataInterfaceFloats(DataInterface, NumFloats); };
			switch (Kind)
			{
			case EDataInterfaceKind::ArrayFloat:
				WriteBagArray(Bag, *Desc, ReadFloats(1));
				break;
			case EDataInterfaceKind::ArrayInt32:
				WriteBagArray(Bag, *Desc, ReadDataInterfaceValues<int32>(DataInterface));
				break;
			case EDataInterfaceKind::ArrayBool:
				WriteBagArray(Bag, *Desc, ReadDataInterfaceValues<bool>(DataInterface));
				break;
			case EDataInterfaceKind::ArrayVector2D:
			{
				const TArray<float> Flat = ReadFloats(2);
				TArray<FVector2D> Items;
				for (int32 Index = 0; Index + 1 < Flat.Num(); Index += 2)
				{
					Items.Emplace(Flat[Index], Flat[Index + 1]);
				}
				WriteBagArray(Bag, *Desc, Items);
				break;
			}
			case EDataInterfaceKind::ArrayVector:
			case EDataInterfaceKind::ArrayPosition:
			{
				const TArray<float> Flat = ReadFloats(3);
				TArray<FVector> Items;
				for (int32 Index = 0; Index + 2 < Flat.Num(); Index += 3)
				{
					Items.Emplace(Flat[Index], Flat[Index + 1], Flat[Index + 2]);
				}
				WriteBagArray(Bag, *Desc, Items);
				break;
			}
			case EDataInterfaceKind::ArrayVector4:
			case EDataInterfaceKind::ArrayColor:
			case EDataInterfaceKind::ArrayQuat:
			{
				const TArray<float> Flat = ReadFloats(4);
				TArray<FVector4> Vectors;
				TArray<FLinearColor> Colors;
				TArray<FQuat> Quats;
				for (int32 Index = 0; Index + 3 < Flat.Num(); Index += 4)
				{
					Vectors.Emplace(Flat[Index], Flat[Index + 1], Flat[Index + 2], Flat[Index + 3]);
					Colors.Emplace(Flat[Index], Flat[Index + 1], Flat[Index + 2], Flat[Index + 3]);
					Quats.Emplace(Flat[Index], Flat[Index + 1], Flat[Index + 2], Flat[Index + 3]);
				}
				if (Kind == EDataInterfaceKind::ArrayVector4)
				{
					WriteBagArray(Bag, *Desc, Vectors);
				}
				else if (Kind == EDataInterfaceKind::ArrayColor)
				{
					WriteBagArray(Bag, *Desc, Colors);
				}
				else
				{
					WriteBagArray(Bag, *Desc, Quats);
				}
				break;
			}
			default:
				break;
			}
		}

		/** The system's User parameter named Name (short, as the bag lists it), as Component plays it. */
		bool FindUserVariable(UNiagaraComponent* Component, FName Name, FNiagaraVariable& OutVariable)
		{
			UNiagaraSystem* System = Component ? Component->GetAsset() : nullptr;
			if (!System)
			{
				return false;
			}
			TArray<FNiagaraVariable> UserVariables;
			System->GetExposedParameters().GetUserParameters(UserVariables);
			for (const FNiagaraVariable& Variable : UserVariables)
			{
				if (GetShortName(Variable.GetName()) == Name)
				{
					OutVariable = Variable;
					return true;
				}
			}
			return false;
		}

		/** Whether a bag parameter can only be a data interface's: an array, or a curve struct. */
		bool IsDataInterfaceDesc(const FPropertyBagPropertyDesc& Desc)
		{
			if (CastField<FArrayProperty>(Desc.CachedProperty))
			{
				return true;
			}
			const UObject* StructType = Desc.ValueType == EPropertyBagPropertyType::Struct ? Desc.ValueTypeObject.Get() : nullptr;
			return StructType && (StructType == FRuntimeFloatCurve::StaticStruct() || StructType == FRuntimeCurveLinearColor::StaticStruct() || StructType == FRuntimeVectorCurve::StaticStruct());
		}

		/** Hands a data interface parameter Desc of Bag to Component. False when Component's system has no such data interface. */
		bool ApplyDataInterface(UNiagaraComponent* Component, const FInstancedPropertyBag& Bag, const FPropertyBagPropertyDesc& Desc)
		{
			FNiagaraVariable Variable;
			if (!FindUserVariable(Component, Desc.Name, Variable) || !Variable.GetType().IsDataInterface())
			{
				return false;
			}
			const EDataInterfaceKind Kind = GetDataInterfaceKind(Variable.GetType().GetClass());
			const FName FullName = Variable.GetName();

			if (IsCurveKind(Kind))
			{
				FNiagaraUserRedirectionParameterStore& Store = Component->GetOverrideParameters();
				UNiagaraDataInterface* Current = Store.GetDataInterface(Variable);
				const UScriptStruct* StructType = Cast<UScriptStruct>(Desc.ValueTypeObject);
				void* Value = const_cast<void*>(GetBagValuePtr(Bag, Desc));
				if (!Current || !Value || StructType != GetCurveStruct(Kind))
				{
					return false;
				}

				// Already these curves: nothing to do - the usual case, as most starts hand the same values on again.
				const TArrayView<const TCHAR* const> Names = GetCurveNames(Kind);
				bool bSame = true;
				for (int32 Index = 0; bSame && Index < Names.Num(); ++Index)
				{
					const FRichCurve* Have = GetDataInterfaceCurve(Current, Names[Index]);
					const FRichCurve* Want = GetBagCurve(StructType, Value, Index);
					bSame = !Have || !Want || FRichCurve::StaticStruct()->CompareScriptStruct(Have, Want, 0);
				}
				if (bSame)
				{
					return true;
				}

				// A fresh copy with the new curves, swapped in - never the one a running simulation may be reading on another
				// thread, nor (should the component ever share it) the system's own.
				UNiagaraDataInterface* Copy = DuplicateObject<UNiagaraDataInterface>(Current, Component);
				Copy->SetFlags(RF_Transient);
				for (int32 Index = 0; Index < Names.Num(); ++Index)
				{
					FRichCurve* Into = GetDataInterfaceCurve(Copy, Names[Index]);
					const FRichCurve* From = GetBagCurve(StructType, Value, Index);
					if (Into && From)
					{
						*Into = *From;
					}
				}

				// The lookup table the simulation samples is made from the curves: made again from the new ones.
				static const UClass* CurveBaseClass = FindObject<UClass>(nullptr, TEXT("/Script/Niagara.NiagaraDataInterfaceCurveBase"));
				if (CurveBaseClass && Copy->IsA(CurveBaseClass))
				{
					UNiagaraDataInterfaceCurveBase* CurveInterface = static_cast<UNiagaraDataInterfaceCurveBase*>(Copy);
					CurveInterface->UpdateTimeRanges();
					CurveInterface->UpdateLUT();
				}

				// Bound as the simulation is made: made again (a playing one starts over - only when a curve really changed).
				Store.SetDataInterface(Copy, Variable);
				Component->ReinitializeSystem();
				return true;
			}

			switch (Kind)
			{
			case EDataInterfaceKind::ArrayFloat:
				UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayFloat(Component, FullName, ReadBagArray<float>(Bag, Desc));
				return true;
			case EDataInterfaceKind::ArrayInt32:
				UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayInt32(Component, FullName, ReadBagArray<int32>(Bag, Desc));
				return true;
			case EDataInterfaceKind::ArrayBool:
				UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayBool(Component, FullName, ReadBagArray<bool>(Bag, Desc));
				return true;
			case EDataInterfaceKind::ArrayVector2D:
				UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector2D(Component, FullName, ReadBagArray<FVector2D>(Bag, Desc));
				return true;
			case EDataInterfaceKind::ArrayVector:
				UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(Component, FullName, ReadBagArray<FVector>(Bag, Desc));
				return true;
			case EDataInterfaceKind::ArrayPosition:
				UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayPosition(Component, FullName, ReadBagArray<FVector>(Bag, Desc));
				return true;
			case EDataInterfaceKind::ArrayVector4:
				UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector4(Component, FullName, ReadBagArray<FVector4>(Bag, Desc));
				return true;
			case EDataInterfaceKind::ArrayColor:
				UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayColor(Component, FullName, ReadBagArray<FLinearColor>(Bag, Desc));
				return true;
			case EDataInterfaceKind::ArrayQuat:
				UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayQuat(Component, FullName, ReadBagArray<FQuat>(Bag, Desc));
				return true;
			default:
				return false;
			}
		}

		/**
		 * What one of the system's User parameters is in the bag - false for a type it can't hold (a data interface other
		 * than a curve or an array).
		 * Positions are vectors there, and said so (bOutPosition), so they are handed back as positions.
		 */
		bool MakeDesc(const FNiagaraVariable& Variable, FPropertyBagPropertyDesc& OutDesc, bool& bOutPosition)
		{
			const FNiagaraTypeDefinition& Type = Variable.GetType();
			const FName Name = GetShortName(Variable.GetName());
			bOutPosition = false;
			if (Type == FNiagaraTypeDefinition::GetFloatDef())
			{
				OutDesc = FPropertyBagPropertyDesc(Name, EPropertyBagPropertyType::Float);
			}
			else if (Type == FNiagaraTypeDefinition::GetIntDef())
			{
				OutDesc = FPropertyBagPropertyDesc(Name, EPropertyBagPropertyType::Int32);
			}
			else if (Type == FNiagaraTypeDefinition::GetBoolDef())
			{
				OutDesc = FPropertyBagPropertyDesc(Name, EPropertyBagPropertyType::Bool);
			}
			else if (Type == FNiagaraTypeDefinition::GetColorDef())
			{
				OutDesc = FPropertyBagPropertyDesc(Name, EPropertyBagPropertyType::Struct, TBaseStructure<FLinearColor>::Get());
			}
			else if (Type == FNiagaraTypeDefinition::GetVec2Def())
			{
				OutDesc = FPropertyBagPropertyDesc(Name, EPropertyBagPropertyType::Struct, TBaseStructure<FVector2D>::Get());
			}
			else if (Type == FNiagaraTypeDefinition::GetVec3Def() || Type == FNiagaraTypeDefinition::GetPositionDef())
			{
				OutDesc = FPropertyBagPropertyDesc(Name, EPropertyBagPropertyType::Struct, TBaseStructure<FVector>::Get());
				bOutPosition = Type == FNiagaraTypeDefinition::GetPositionDef();
			}
			else if (Type == FNiagaraTypeDefinition::GetVec4Def())
			{
				OutDesc = FPropertyBagPropertyDesc(Name, EPropertyBagPropertyType::Struct, TBaseStructure<FVector4>::Get());
			}
			else if (Type == FNiagaraTypeDefinition::GetQuatDef())
			{
				OutDesc = FPropertyBagPropertyDesc(Name, EPropertyBagPropertyType::Struct, TBaseStructure<FQuat>::Get());
			}
			else if (Type.IsEnum() && Type.GetEnum())
			{
				OutDesc = FPropertyBagPropertyDesc(Name, EPropertyBagPropertyType::Enum, Type.GetEnum());
			}
			else if (Type.IsDataInterface())
			{
				// Curves and arrays - see EDataInterfaceKind. Any other data interface isn't listed.
				return MakeDataInterfaceDesc(Name, Type.GetClass(), OutDesc);
			}
			else if (Type.IsUObject() && Type.GetClass())
			{
				OutDesc = FPropertyBagPropertyDesc(Name, EPropertyBagPropertyType::Object, Type.GetClass());
			}
			else
			{
				return false;
			}
			return true;
		}

		/** The system's own value of Variable, into Bag's parameter of the same name. */
		void ReadSystemValue(const FNiagaraParameterStore& Store, const FNiagaraVariable& Variable, FInstancedPropertyBag& Bag)
		{
			const FNiagaraTypeDefinition& Type = Variable.GetType();
			const FName Name = GetShortName(Variable.GetName());
			if (Type == FNiagaraTypeDefinition::GetFloatDef())
			{
				Bag.SetValueFloat(Name, Store.GetParameterValue<float>(Variable));
			}
			else if (Type == FNiagaraTypeDefinition::GetIntDef())
			{
				Bag.SetValueInt32(Name, Store.GetParameterValue<int32>(Variable));
			}
			else if (Type == FNiagaraTypeDefinition::GetBoolDef())
			{
				Bag.SetValueBool(Name, Store.GetParameterValue<FNiagaraBool>(Variable).GetValue());
			}
			else if (Type == FNiagaraTypeDefinition::GetColorDef())
			{
				const FLinearColor Value = Store.GetParameterValue<FLinearColor>(Variable);
				Bag.SetValueStruct(Name, FConstStructView::Make(Value));
			}
			else if (Type == FNiagaraTypeDefinition::GetVec2Def())
			{
				const FVector2D Value(Store.GetParameterValue<FVector2f>(Variable));
				Bag.SetValueStruct(Name, FConstStructView::Make(Value));
			}
			else if (Type == FNiagaraTypeDefinition::GetVec3Def() || Type == FNiagaraTypeDefinition::GetPositionDef())
			{
				const FVector Value(Store.GetParameterValue<FVector3f>(Variable));
				Bag.SetValueStruct(Name, FConstStructView::Make(Value));
			}
			else if (Type == FNiagaraTypeDefinition::GetVec4Def())
			{
				const FVector4 Value(Store.GetParameterValue<FVector4f>(Variable));
				Bag.SetValueStruct(Name, FConstStructView::Make(Value));
			}
			else if (Type == FNiagaraTypeDefinition::GetQuatDef())
			{
				const FQuat Value(Store.GetParameterValue<FQuat4f>(Variable));
				Bag.SetValueStruct(Name, FConstStructView::Make(Value));
			}
			else if (Type.IsEnum() && Type.GetEnum())
			{
				Bag.SetValueEnum(Name, static_cast<uint8>(Store.GetParameterValue<int32>(Variable)), Type.GetEnum());
			}
			else if (Type.IsDataInterface())
			{
				ReadDataInterfaceValue(Store.GetDataInterface(Variable), Name, Bag);
			}
			else if (Type.IsUObject())
			{
				Bag.SetValueObject(Name, Store.GetUObject(Variable));
			}
		}

		/** Where parameter Desc of Bag keeps its value. */
		const void* GetValuePtr(const FInstancedPropertyBag& Bag, const FPropertyBagPropertyDesc& Desc)
		{
			const uint8* Memory = Bag.GetValue().GetMemory();
			return Memory && Desc.CachedProperty ? Desc.CachedProperty->ContainerPtrToValuePtr<void>(Memory) : nullptr;
		}

		bool IsSameType(const FPropertyBagPropertyDesc& A, const FPropertyBagPropertyDesc& B)
		{
			return A.ValueType == B.ValueType && A.ValueTypeObject == B.ValueTypeObject;
		}

		/** Whether two bags hold the same parameters, of the same types, with the same values. */
		bool AreIdentical(const FInstancedPropertyBag& A, const FInstancedPropertyBag& B)
		{
			const UPropertyBag* StructA = A.GetPropertyBagStruct();
			const UPropertyBag* StructB = B.GetPropertyBagStruct();
			if (StructA != StructB)
			{
				return false;
			}
			return !StructA || StructA->CompareScriptStruct(A.GetValue().GetMemory(), B.GetValue().GetMemory(), 0);
		}

		/** Whether two bags list the same parameters, of the same types - the rows the Details panel shows. */
		bool HaveSameLayout(const FInstancedPropertyBag& A, const FInstancedPropertyBag& B)
		{
			return A.GetPropertyBagStruct() == B.GetPropertyBagStruct();
		}

		/** Parameter Desc of Values as the system has it in SystemValues: false when the system doesn't have it, as that type. */
		const FPropertyBagPropertyDesc* FindSystemDesc(const FInstancedPropertyBag& SystemValues, const FPropertyBagPropertyDesc& Desc)
		{
			const FPropertyBagPropertyDesc* SystemDesc = SystemValues.FindPropertyDescByName(Desc.Name);
			return SystemDesc && IsSameType(Desc, *SystemDesc) && SystemDesc->CachedProperty ? SystemDesc : nullptr;
		}
	}

	FName GetShortName(FName Name)
	{
		FString Text = Name.ToString();
		Text.RemoveFromStart(TEXT("User."));
		return FName(*Text);
	}

	bool IsChanged(const FInstancedPropertyBag& Values, const FInstancedPropertyBag& SystemValues, const FPropertyBagPropertyDesc& Desc)
	{
		const FPropertyBagPropertyDesc* SystemDesc = FindSystemDesc(SystemValues, Desc);
		if (!SystemDesc || !Desc.CachedProperty)
		{
			return true;
		}
		const void* Value = GetValuePtr(Values, Desc);
		const void* SystemValue = GetValuePtr(SystemValues, *SystemDesc);
		return !Value || !SystemValue || !Desc.CachedProperty->Identical(Value, SystemValue);
	}

	bool HasChanges(const FInstancedPropertyBag& Values, const FInstancedPropertyBag& SystemValues)
	{
		if (const UPropertyBag* BagStruct = Values.GetPropertyBagStruct())
		{
			for (const FPropertyBagPropertyDesc& Desc : BagStruct->GetPropertyDescs())
			{
				if (IsChanged(Values, SystemValues, Desc))
				{
					return true;
				}
			}
		}
		return false;
	}

	bool LayOut(UNiagaraSystem* System, FInstancedPropertyBag& Values, FInstancedPropertyBag& SystemValues, TArray<FName>& PositionNames, bool* bOutChanged)
	{
		if (bOutChanged)
		{
			*bOutChanged = false;
		}

		// The system's User parameters as they are now, and its values of them.
		TArray<FPropertyBagPropertyDesc> Descs;
		TArray<FNiagaraVariable> Variables;
		TArray<FName> Positions;
		if (System)
		{
			TArray<FNiagaraVariable> UserVariables;
			System->GetExposedParameters().GetUserParameters(UserVariables);
			for (const FNiagaraVariable& Variable : UserVariables)
			{
				FPropertyBagPropertyDesc Desc;
				bool bPosition = false;
				if (!MakeDesc(Variable, Desc, bPosition) || Descs.ContainsByPredicate([&Desc](const FPropertyBagPropertyDesc& Other) { return Other.Name == Desc.Name; }))
				{
					continue;
				}
				Descs.Add(Desc);
				Variables.Add(Variable);
				if (bPosition)
				{
					Positions.Add(Desc.Name);
				}
			}
		}

		FInstancedPropertyBag NewSystemValues;
		if (Descs.Num() > 0)
		{
			NewSystemValues.AddProperties(Descs);
			for (const FNiagaraVariable& Variable : Variables)
			{
				ReadSystemValue(System->GetExposedParameters(), Variable, NewSystemValues);
			}
		}

		// Nothing new in the system since it was last laid out: nothing to do (asked often).
		if (AreIdentical(NewSystemValues, SystemValues) && HaveSameLayout(NewSystemValues, Values) && PositionNames == Positions)
		{
			return false;
		}

		// Each at the system's value - but one that was changed keeps its own, as long as it is still there, of the same type.
		FInstancedPropertyBag NewValues = NewSystemValues;
		if (const UPropertyBag* NewStruct = NewValues.GetPropertyBagStruct())
		{
			for (const FPropertyBagPropertyDesc& NewDesc : NewStruct->GetPropertyDescs())
			{
				const FPropertyBagPropertyDesc* OldDesc = Values.FindPropertyDescByName(NewDesc.Name);
				if (!OldDesc || !IsSameType(*OldDesc, NewDesc) || !OldDesc->CachedProperty || !NewDesc.CachedProperty || !IsChanged(Values, SystemValues, *OldDesc))
				{
					continue;
				}
				const void* OldValue = GetValuePtr(Values, *OldDesc);
				uint8* NewMemory = NewValues.GetMutableValue().GetMemory();
				if (OldValue && NewMemory)
				{
					NewDesc.CachedProperty->CopyCompleteValue(NewDesc.CachedProperty->ContainerPtrToValuePtr<void>(NewMemory), OldValue);
				}
			}
		}

		const bool bRowsChanged = !HaveSameLayout(NewValues, Values);
		Values = MoveTemp(NewValues);
		SystemValues = MoveTemp(NewSystemValues);
		PositionNames = MoveTemp(Positions);
		if (bOutChanged)
		{
			*bOutChanged = true;
		}
		return bRowsChanged;
	}

	void Apply(UNiagaraComponent* Component, const FInstancedPropertyBag& Bag, const FPropertyBagPropertyDesc& Desc, const TArray<FName>& PositionNames)
	{
		// A curve or an array: the data interface's, set on the component's own copy of it. Never handed on as a value.
		if (IsDataInterfaceDesc(Desc))
		{
			ApplyDataInterface(Component, Bag, Desc);
			return;
		}

		const FName Name = Desc.Name;
		switch (Desc.ValueType)
		{
		case EPropertyBagPropertyType::Float:
			if (const auto Value = Bag.GetValueFloat(Name); Value.HasValue())
			{
				Component->SetVariableFloat(Name, Value.GetValue());
			}
			break;
		case EPropertyBagPropertyType::Int32:
			if (const auto Value = Bag.GetValueInt32(Name); Value.HasValue())
			{
				Component->SetVariableInt(Name, Value.GetValue());
			}
			break;
		case EPropertyBagPropertyType::Bool:
			if (const auto Value = Bag.GetValueBool(Name); Value.HasValue())
			{
				Component->SetVariableBool(Name, Value.GetValue());
			}
			break;
		case EPropertyBagPropertyType::Enum:
			if (const auto Value = Bag.GetValueEnum(Name, Cast<UEnum>(Desc.ValueTypeObject)); Value.HasValue())
			{
				Component->SetVariableInt(Name, Value.GetValue());
			}
			break;
		case EPropertyBagPropertyType::Object:
			if (const auto Value = Bag.GetValueObject(Name); Value.HasValue())
			{
				ApplyObject(Component, Name, Value.GetValue());
			}
			break;
		case EPropertyBagPropertyType::Struct:
		{
			const UScriptStruct* StructType = Cast<UScriptStruct>(Desc.ValueTypeObject);
			const auto Value = Bag.GetValueStruct(Name, StructType);
			if (!Value.HasValue() || !StructType)
			{
				break;
			}
			const FStructView View = Value.GetValue();
			if (StructType == TBaseStructure<FLinearColor>::Get())
			{
				Component->SetVariableLinearColor(Name, View.Get<FLinearColor>());
			}
			else if (StructType == TBaseStructure<FVector2D>::Get())
			{
				Component->SetVariableVec2(Name, View.Get<FVector2D>());
			}
			else if (StructType == TBaseStructure<FVector>::Get())
			{
				if (PositionNames.Contains(Name))
				{
					Component->SetVariablePosition(Name, View.Get<FVector>());
				}
				else
				{
					Component->SetVariableVec3(Name, View.Get<FVector>());
				}
			}
			else if (StructType == TBaseStructure<FVector4>::Get())
			{
				Component->SetVariableVec4(Name, View.Get<FVector4>());
			}
			else if (StructType == TBaseStructure<FQuat>::Get())
			{
				Component->SetVariableQuat(Name, View.Get<FQuat>());
			}
			break;
		}
		default:
			break;
		}
	}

	void ApplyChanged(UNiagaraComponent* Component, const FInstancedPropertyBag& Values, const FInstancedPropertyBag& SystemValues, const TArray<FName>& PositionNames)
	{
		const UPropertyBag* BagStruct = Values.GetPropertyBagStruct();
		if (!Component || !BagStruct)
		{
			return;
		}
		for (const FPropertyBagPropertyDesc& Desc : BagStruct->GetPropertyDescs())
		{
			if (IsChanged(Values, SystemValues, Desc))
			{
				Apply(Component, Values, Desc, PositionNames);
			}
		}
	}

	void RestoreChanged(UNiagaraComponent* Component, const FInstancedPropertyBag& Values, const FInstancedPropertyBag& SystemValues, const TArray<FName>& PositionNames)
	{
		const UPropertyBag* BagStruct = Values.GetPropertyBagStruct();
		if (!Component || !BagStruct)
		{
			return;
		}
		for (const FPropertyBagPropertyDesc& Desc : BagStruct->GetPropertyDescs())
		{
			// One the system no longer has is left alone: there is no value of its to put back.
			const FPropertyBagPropertyDesc* SystemDesc = FindSystemDesc(SystemValues, Desc);
			if (SystemDesc && IsChanged(Values, SystemValues, Desc))
			{
				Apply(Component, SystemValues, *SystemDesc, PositionNames);
			}
		}
	}

	void ApplyObject(UNiagaraComponent* Component, FName Name, UObject* Value)
	{
		if (UMaterialInterface* Material = Cast<UMaterialInterface>(Value))
		{
			Component->SetVariableMaterial(Name, Material);
		}
		else if (UTexture* Texture = Cast<UTexture>(Value))
		{
			Component->SetVariableTexture(Name, Texture);
		}
		else
		{
			Component->SetVariableObject(Name, Value);
		}
	}
}
