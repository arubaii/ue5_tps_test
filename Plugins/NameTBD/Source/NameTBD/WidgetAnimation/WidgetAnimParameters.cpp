// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimParameters.h"

#include "StructUtils/InstancedStruct.h"
#include "StructUtils/PropertyBag.h"
#include "UObject/UnrealType.h"

#include "WidgetAnimLibrary.h"
#include "WidgetAnimPreset.h"

DEFINE_LOG_CATEGORY_STATIC(LogWidgetAnimParameters, Log, All);

// =====================================================================================================
// FWidgetAnimParameter / FWidgetAnimParameterValue
// =====================================================================================================

FName FWidgetAnimParameter::GetDefaultOption() const
{
	if (Options.Contains(DefaultOption))
	{
		return DefaultOption;
	}
	return Options.Num() > 0 ? Options[0] : FName();
}

FWidgetAnimParameterValue FWidgetAnimParameterValue::FromDefault(const FWidgetAnimParameter& Parameter)
{
	FWidgetAnimParameterValue Value;
	Value.Name = Parameter.Name;
	Value.Type = Parameter.Type;
	Value.Float = Parameter.DefaultFloat;
	Value.Integer = Parameter.DefaultInteger;
	Value.bBool = Parameter.bDefaultBool;
	Value.Color = Parameter.DefaultColor;
	Value.Vector2D = Parameter.DefaultVector2D;
	Value.Option = Parameter.GetDefaultOption();
	return Value;
}

FVector4 FWidgetAnimParameterValue::AsVector(const TArray<FName>* ChoiceOptions) const
{
	// A single number is every channel, so it scales a Vector 2D field evenly.
	switch (Type)
	{
	case EWidgetAnimParameterType::Integer:  return FVector4(Integer, Integer, Integer, Integer);
	case EWidgetAnimParameterType::Bool:     return bBool ? FVector4(1.0, 1.0, 1.0, 1.0) : FVector4(0.0, 0.0, 0.0, 0.0);
	case EWidgetAnimParameterType::Color:    return FVector4(Color.R, Color.G, Color.B, Color.A);
	case EWidgetAnimParameterType::Vector2D: return FVector4(Vector2D.X, Vector2D.Y, 0.0, 0.0);
	case EWidgetAnimParameterType::Choice:
	{
		const double Index = ChoiceOptions ? FMath::Max(ChoiceOptions->IndexOfByKey(Option), 0) : 0.0;
		return FVector4(Index, Index, Index, Index);
	}
	default:                                 return FVector4(Float, Float, Float, Float);
	}
}

void FWidgetAnimParameterValue::SetFromVector(const FVector4& Vector, const TArray<FName>* ChoiceOptions)
{
	switch (Type)
	{
	case EWidgetAnimParameterType::Integer:  Integer = FMath::RoundToInt32(Vector.X); break;
	case EWidgetAnimParameterType::Bool:     bBool = Vector.X > 0.5; break;
	case EWidgetAnimParameterType::Color:    Color = FLinearColor(static_cast<float>(Vector.X), static_cast<float>(Vector.Y), static_cast<float>(Vector.Z), static_cast<float>(Vector.W)); break;
	case EWidgetAnimParameterType::Vector2D: Vector2D = FVector2D(Vector.X, Vector.Y); break;
	case EWidgetAnimParameterType::Choice:
	{
		const int32 Index = FMath::RoundToInt32(Vector.X);
		Option = ChoiceOptions && ChoiceOptions->IsValidIndex(Index) ? (*ChoiceOptions)[Index] : Option;
		break;
	}
	default:                                 Float = static_cast<float>(Vector.X); break;
	}
}

bool FWidgetAnimParameterValue::SameValueAs(const FWidgetAnimParameterValue& Other) const
{
	if (Type != Other.Type)
	{
		return false;
	}
	switch (Type)
	{
	case EWidgetAnimParameterType::Integer:  return Integer == Other.Integer;
	case EWidgetAnimParameterType::Bool:     return bBool == Other.bBool;
	case EWidgetAnimParameterType::Color:    return Color.Equals(Other.Color, 1e-4f);
	case EWidgetAnimParameterType::Vector2D: return Vector2D.Equals(Other.Vector2D, 1e-4);
	case EWidgetAnimParameterType::Choice:   return Option == Other.Option;
	default:                                 return FMath::IsNearlyEqual(Float, Other.Float, 1e-4f);
	}
}

FString FWidgetAnimParameterValue::ToDisplayString() const
{
	switch (Type)
	{
	case EWidgetAnimParameterType::Integer:  return FString::FromInt(Integer);
	case EWidgetAnimParameterType::Bool:     return bBool ? TEXT("True") : TEXT("False");
	case EWidgetAnimParameterType::Color:    return FString::Printf(TEXT("(%.2f, %.2f, %.2f, %.2f)"), Color.R, Color.G, Color.B, Color.A);
	case EWidgetAnimParameterType::Vector2D: return FString::Printf(TEXT("(%s, %s)"), *FString::SanitizeFloat(Vector2D.X), *FString::SanitizeFloat(Vector2D.Y));
	case EWidgetAnimParameterType::Choice:   return Option.IsNone() ? TEXT("-") : Option.ToString();
	default:                                 return FString::SanitizeFloat(Float);
	}
}

// =====================================================================================================
// FWidgetAnimParameterScope
// =====================================================================================================

namespace WidgetAnimParametersLocal
{
	/** The innermost scope on this thread. Animations are built on the game thread; a thread of its own sees none. */
	thread_local FWidgetAnimParameterScope* TopScope = nullptr;

	/** The resolver the tracks being built now take their values from. */
	thread_local const FWidgetAnimParameterResolver* CurrentResolver = nullptr;
}

FWidgetAnimParameterScope::FWidgetAnimParameterScope(TConstArrayView<FWidgetAnimParameterValue> InValues, const UObject* InAnimation, FName InAnimId, bool bInUnderUse)
	: Values(InValues)
	, Animation(InAnimation)
	, AnimId(InAnimId)
	, bUnderUse(bInUnderUse)
{
	Outer = WidgetAnimParametersLocal::TopScope;
	WidgetAnimParametersLocal::TopScope = this;
}

FWidgetAnimParameterScope::~FWidgetAnimParameterScope()
{
	WidgetAnimParametersLocal::TopScope = Outer;
}

bool FWidgetAnimParameterScope::AppliesTo(const UObject* UseAnimation, FName UseAnimId) const
{
	return (!Animation || Animation == UseAnimation) && (AnimId.IsNone() || AnimId == UseAnimId);
}

void FWidgetAnimParameterScopeStack::Push(TConstArrayView<FWidgetAnimParameterValue> Values, const UObject* Animation, FName AnimId, bool bUnderUse)
{
	Scopes.Add(MakeUnique<FWidgetAnimParameterScope>(Values, Animation, AnimId, bUnderUse));
}

void FWidgetAnimParameterScopeStack::Reset()
{
	// Innermost first, so each hands the thread back the one it was pushed over.
	for (int32 Index = Scopes.Num() - 1; Index >= 0; --Index)
	{
		Scopes[Index].Reset();
	}
	Scopes.Reset();
}

// =====================================================================================================
// FWidgetAnimParameterResolver
// =====================================================================================================

bool FWidgetAnimParameterResolver::Resolve(const UWidgetAnimPresetAsset& Animation, const FWidgetAnimEntryBase* Entry, const FWidgetAnimParameterResolver* Outer, FName UseAnimId)
{
	Values.Reset();
	Declarations.Reset();
	if (Animation.Parameters.Num() == 0)
	{
		return false;
	}

	// The defaults.
	for (const FWidgetAnimParameter& Parameter : Animation.Parameters)
	{
		if (Parameter.Name.IsNone() || Values.Contains(Parameter.Name))
		{
			continue;
		}
		Values.Add(Parameter.Name, FWidgetAnimParameterValue::FromDefault(Parameter));
		Declarations.Add(Parameter.Name, &Parameter);
	}

	// The animation this one plays in, by name.
	if (Outer)
	{
		for (const TPair<FName, FWidgetAnimParameterValue>& Value : Outer->Values)
		{
			Set(Value.Value);
		}
	}

	// The scopes in effect, outermost first, so the innermost wins.
	TArray<const FWidgetAnimParameterScope*, TInlineAllocator<4>> Chain;
	for (const FWidgetAnimParameterScope* Scope = WidgetAnimParametersLocal::TopScope; Scope; Scope = Scope->Outer)
	{
		Chain.Insert(Scope, 0);
	}

	// Those under the use - a component's Parameters - and only what they set.
	for (const FWidgetAnimParameterScope* Scope : Chain)
	{
		if (!Scope->bUnderUse || !Scope->AppliesTo(&Animation, UseAnimId))
		{
			continue;
		}
		for (const FWidgetAnimParameterValue& Value : Scope->Values)
		{
			if (Value.bOverride)
			{
				Set(Value);
			}
		}
	}

	// This use's own.
	if (Entry)
	{
		for (const FWidgetAnimParameterValue& Value : Entry->Parameters)
		{
			if (Value.bOverride)
			{
				Set(Value);
			}
		}
	}

	// What the play was handed.
	for (const FWidgetAnimParameterScope* Scope : Chain)
	{
		if (Scope->bUnderUse || !Scope->AppliesTo(&Animation, UseAnimId))
		{
			continue;
		}
		for (const FWidgetAnimParameterValue& Value : Scope->Values)
		{
			Set(Value);
		}
	}
	return true;
}

void FWidgetAnimParameterResolver::Set(const FWidgetAnimParameterValue& Value)
{
	FWidgetAnimParameterValue* Existing = Values.Find(Value.Name);
	if (!Existing)
	{
		return;
	}
	if (Existing->Type == Value.Type)
	{
		const FName Name = Existing->Name;
		*Existing = Value;
		Existing->Name = Name;
		Existing->bOverride = true;
		return;
	}

	// A number for a number: a Float for an Integer parameter, a Bool for either.
	auto IsNumber = [](EWidgetAnimParameterType Type)
	{
		return Type == EWidgetAnimParameterType::Float || Type == EWidgetAnimParameterType::Integer || Type == EWidgetAnimParameterType::Bool;
	};
	if (IsNumber(Existing->Type) && IsNumber(Value.Type))
	{
		Existing->SetFromVector(Value.AsVector());
	}
}

bool FWidgetAnimParameterResolver::Holds(const FWidgetAnimParameterCondition& Condition) const
{
	if (!Condition.IsSet())
	{
		return true;
	}
	const FWidgetAnimParameterValue* Value = Values.Find(Condition.Parameter);
	if (!Value)
	{
		return true;
	}
	if (Value->Type == EWidgetAnimParameterType::Choice)
	{
		return Condition.Options.Contains(Value->Option);
	}

	// A Bool (or a number, as a Bool): while true - or while false, when its options say False.
	const bool bTrue = Value->AsVector().X > 0.5;
	const bool bWhileFalse = Condition.Options.Contains(FName(TEXT("False")));
	return bWhileFalse ? !bTrue : bTrue;
}

bool FWidgetAnimParameterResolver::Apply(const TArray<FWidgetAnimLayer>& Layers, TArray<FWidgetAnimLayer>& OutLayers, TSet<int32>& OutMuted) const
{
	const bool bAny = Layers.ContainsByPredicate([](const FWidgetAnimLayer& Layer)
	{
		return Layer.ParameterBindings.Num() > 0 || Layer.PlaysWhen.IsSet();
	});
	if (!bAny)
	{
		return false;
	}

	OutLayers = Layers;
	for (int32 Index = 0; Index < Layers.Num(); ++Index)
	{
		const FWidgetAnimLayer& Authored = Layers[Index];
		FWidgetAnimLayer& Played = OutLayers[Index];
		if (!Holds(Authored.PlaysWhen))
		{
			OutMuted.Add(Index);
		}

		for (const FWidgetAnimParameterBinding& Binding : Authored.ParameterBindings)
		{
			const FWidgetAnimParameterValue* Value = Values.Find(Binding.Parameter);
			if (!Value)
			{
				// Renamed or removed since it was bound: the field's own constant plays. Said once per name.
				static TSet<FName> Warned;
				bool bAlreadyWarned = false;
				Warned.Add(Binding.Parameter, &bAlreadyWarned);
				if (!bAlreadyWarned)
				{
					UE_LOG(LogWidgetAnimParameters, Warning, TEXT("A track's %s is bound to the animation parameter '%s', which the animation doesn't have: the field's own value plays."),
						*Binding.FieldPath, *Binding.Parameter.ToString());
				}
				continue;
			}

			// The binding table and the condition are never fields a parameter writes.
			if (Binding.FieldPath.StartsWith(TEXT("ParameterBindings")) || Binding.FieldPath.StartsWith(TEXT("PlaysWhen")))
			{
				continue;
			}

			const FWidgetAnimParameter* const* Declaration = Declarations.Find(Binding.Parameter);
			WidgetAnimParameters::WriteField(FWidgetAnimLayer::StaticStruct(), &Played, Binding.FieldPath, *Value, Binding.Scale, Binding.Offset,
				Declaration && *Declaration ? &(*Declaration)->Options : nullptr);
		}
	}
	return true;
}

const FWidgetAnimParameterResolver* FWidgetAnimParameterResolver::GetCurrent()
{
	return WidgetAnimParametersLocal::CurrentResolver;
}

FWidgetAnimParameterResolver::FCurrentScope::FCurrentScope(const FWidgetAnimParameterResolver* Resolver)
	: Previous(WidgetAnimParametersLocal::CurrentResolver)
{
	WidgetAnimParametersLocal::CurrentResolver = Resolver;
}

FWidgetAnimParameterResolver::FCurrentScope::~FCurrentScope()
{
	WidgetAnimParametersLocal::CurrentResolver = Previous;
}

// =====================================================================================================
// Fields
// =====================================================================================================

namespace WidgetAnimParametersLocal
{
	struct FPathStep
	{
		FName Name;
		int32 Index = INDEX_NONE;
	};

	bool ParsePath(const FString& Path, TArray<FPathStep, TInlineAllocator<8>>& OutSteps)
	{
		TArray<FString> Parts;
		Path.ParseIntoArray(Parts, TEXT("."));
		for (const FString& Part : Parts)
		{
			FPathStep Step;
			int32 Bracket = INDEX_NONE;
			if (Part.FindChar(TEXT('['), Bracket))
			{
				const int32 Close = Part.Find(TEXT("]"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Bracket);
				if (Close == INDEX_NONE)
				{
					return false;
				}
				Step.Index = FCString::Atoi(*Part.Mid(Bracket + 1, Close - Bracket - 1));
				Step.Name = FName(*Part.Left(Bracket));
			}
			else
			{
				Step.Name = FName(*Part);
			}
			OutSteps.Add(Step);
		}
		return OutSteps.Num() > 0;
	}

	/**
	 * The struct a property's value is, and its memory - an instanced struct's contents (a registered type's Type Params), a
	 * property bag's (a Particles track's User Parameters), or the struct itself. With no memory, only a plain struct can be
	 * stepped into: what an instanced struct holds is only known from its value.
	 */
	bool StepInto(const FProperty* Property, void* ValuePtr, const UScriptStruct*& OutStruct, void*& OutMemory)
	{
		const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
		if (!StructProperty)
		{
			return false;
		}
		if (StructProperty->Struct == FInstancedStruct::StaticStruct())
		{
			if (!ValuePtr)
			{
				return false;
			}
			FInstancedStruct* Instanced = static_cast<FInstancedStruct*>(ValuePtr);
			OutStruct = Instanced->GetScriptStruct();
			OutMemory = Instanced->GetMutableMemory();
			return OutStruct && OutMemory;
		}
		if (StructProperty->Struct == FInstancedPropertyBag::StaticStruct())
		{
			if (!ValuePtr)
			{
				return false;
			}
			FInstancedPropertyBag* Bag = static_cast<FInstancedPropertyBag*>(ValuePtr);
			FStructView View = Bag->GetMutableValue();
			OutStruct = View.GetScriptStruct();
			OutMemory = View.GetMemory();
			return OutStruct && OutMemory;
		}
		OutStruct = StructProperty->Struct;
		OutMemory = ValuePtr;
		return OutStruct != nullptr;
	}

	/** Walks FieldPath from Struct: the field's property, and where its value is (null without Memory). */
	const FProperty* Walk(const UScriptStruct* Struct, void* Memory, const FString& FieldPath, void*& OutValue)
	{
		OutValue = nullptr;
		TArray<FPathStep, TInlineAllocator<8>> Steps;
		if (!Struct || !ParsePath(FieldPath, Steps))
		{
			return nullptr;
		}

		const UStruct* Current = Struct;
		void* CurrentMemory = Memory;
		const FProperty* Property = nullptr;
		void* Value = nullptr;
		for (int32 StepIndex = 0; StepIndex < Steps.Num(); ++StepIndex)
		{
			if (StepIndex > 0)
			{
				const UScriptStruct* Inner = nullptr;
				void* InnerMemory = nullptr;
				if (!StepInto(Property, Value, Inner, InnerMemory))
				{
					return nullptr;
				}
				Current = Inner;
				CurrentMemory = InnerMemory;
			}

			Property = FindFProperty<FProperty>(Current, Steps[StepIndex].Name);
			if (!Property)
			{
				return nullptr;
			}
			Value = CurrentMemory ? Property->ContainerPtrToValuePtr<void>(CurrentMemory) : nullptr;

			const int32 Index = Steps[StepIndex].Index;
			if (Index == INDEX_NONE)
			{
				continue;
			}
			if (const FArrayProperty* Array = CastField<FArrayProperty>(Property))
			{
				if (Value)
				{
					FScriptArrayHelper Helper(Array, Value);
					if (!Helper.IsValidIndex(Index))
					{
						return nullptr;
					}
					Value = Helper.GetRawPtr(Index);
				}
				Property = Array->Inner;
			}
			else if (Index >= 0 && Index < Property->ArrayDim)
			{
				Value = Value ? static_cast<uint8*>(Value) + Index * Property->GetElementSize() : nullptr;
			}
			else
			{
				return nullptr;
			}
		}
		OutValue = Value;
		return Property;
	}

	const UEnum* GetFieldEnum(const FProperty* Field)
	{
		if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Field))
		{
			return EnumProperty->GetEnum();
		}
		if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Field))
		{
			return ByteProperty->Enum;
		}
		return nullptr;
	}

	/** The enum value a Choice's option names - by its name ("Up") or its display name - or an Integer's index. */
	bool FindEnumValue(const UEnum* Enum, const FWidgetAnimParameterValue& Value, int64& OutValue)
	{
		const int32 NumEntries = Enum->NumEnums() - (Enum->ContainsExistingMax() ? 1 : 0);
		if (Value.Type == EWidgetAnimParameterType::Integer)
		{
			if (Value.Integer < 0 || Value.Integer >= NumEntries)
			{
				return false;
			}
			OutValue = Enum->GetValueByIndex(Value.Integer);
			return true;
		}
		const FString Wanted = Value.Type == EWidgetAnimParameterType::Choice ? Value.Option.ToString() : Value.ToDisplayString();
		for (int32 Index = 0; Index < NumEntries; ++Index)
		{
			if (Enum->GetNameStringByIndex(Index).Equals(Wanted, ESearchCase::IgnoreCase)
				|| Enum->GetDisplayNameTextByIndex(Index).ToString().Equals(Wanted, ESearchCase::IgnoreCase))
			{
				OutValue = Enum->GetValueByIndex(Index);
				return true;
			}
		}
		return false;
	}

	bool WriteLeaf(const FProperty* Field, void* ValuePtr, const FWidgetAnimParameterValue& Value, float Scale, float Offset, const TArray<FName>* ChoiceOptions)
	{
		const FVector4 Vector = Value.AsVector(ChoiceOptions);
		auto Number = [Scale, Offset](double In) { return In * Scale + Offset; };

		if (const UEnum* Enum = GetFieldEnum(Field))
		{
			int64 EnumValue = 0;
			if (!FindEnumValue(Enum, Value, EnumValue))
			{
				return false;
			}
			if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Field))
			{
				EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, EnumValue);
			}
			else
			{
				CastFieldChecked<FByteProperty>(Field)->SetIntPropertyValue(ValuePtr, static_cast<uint64>(EnumValue));
			}
			return true;
		}
		if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Field))
		{
			BoolProperty->SetPropertyValue(ValuePtr, Vector.X > 0.5);
			return true;
		}
		if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Field))
		{
			if (NumericProperty->IsFloatingPoint())
			{
				NumericProperty->SetFloatingPointPropertyValue(ValuePtr, Number(Vector.X));
			}
			else
			{
				NumericProperty->SetIntPropertyValue(ValuePtr, static_cast<int64>(FMath::RoundToDouble(Number(Vector.X))));
			}
			return true;
		}
		if (const FNameProperty* NameProperty = CastField<FNameProperty>(Field))
		{
			NameProperty->SetPropertyValue(ValuePtr, Value.Type == EWidgetAnimParameterType::Choice ? Value.Option : FName(*Value.ToDisplayString()));
			return true;
		}
		if (const FStrProperty* StringProperty = CastField<FStrProperty>(Field))
		{
			StringProperty->SetPropertyValue(ValuePtr, Value.ToDisplayString());
			return true;
		}
		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Field))
		{
			const UScriptStruct* Struct = StructProperty->Struct;
			if (Struct == TBaseStructure<FLinearColor>::Get())
			{
				*static_cast<FLinearColor*>(ValuePtr) = FLinearColor(static_cast<float>(Vector.X), static_cast<float>(Vector.Y), static_cast<float>(Vector.Z), static_cast<float>(Vector.W));
				return true;
			}
			if (Struct == TBaseStructure<FColor>::Get())
			{
				*static_cast<FColor*>(ValuePtr) = FLinearColor(static_cast<float>(Vector.X), static_cast<float>(Vector.Y), static_cast<float>(Vector.Z), static_cast<float>(Vector.W)).ToFColor(/*bSRGB=*/true);
				return true;
			}
			if (Struct == TBaseStructure<FVector2D>::Get())
			{
				*static_cast<FVector2D*>(ValuePtr) = FVector2D(Number(Vector.X), Number(Vector.Y));
				return true;
			}
			if (Struct == TVariantStructure<FVector2f>::Get())
			{
				*static_cast<FVector2f*>(ValuePtr) = FVector2f(static_cast<float>(Number(Vector.X)), static_cast<float>(Number(Vector.Y)));
				return true;
			}
			if (Struct == TBaseStructure<FVector>::Get())
			{
				// X and Y; Z too from a single number.
				FVector& Out = *static_cast<FVector*>(ValuePtr);
				Out.X = Number(Vector.X);
				Out.Y = Number(Vector.Y);
				if (Value.Type != EWidgetAnimParameterType::Vector2D)
				{
					Out.Z = Number(Vector.Z);
				}
				return true;
			}
		}
		return false;
	}
}

namespace WidgetAnimParameters
{
	bool WriteField(const UScriptStruct* Struct, void* Memory, const FString& FieldPath, const FWidgetAnimParameterValue& Value, float Scale, float Offset, const TArray<FName>* ChoiceOptions)
	{
		void* ValuePtr = nullptr;
		const FProperty* Field = WidgetAnimParametersLocal::Walk(Struct, Memory, FieldPath, ValuePtr);
		return Field && ValuePtr && WidgetAnimParametersLocal::WriteLeaf(Field, ValuePtr, Value, Scale, Offset, ChoiceOptions);
	}

	const FProperty* FindField(const UScriptStruct* Struct, const FString& FieldPath, const void* Memory)
	{
		void* Unused = nullptr;
		return WidgetAnimParametersLocal::Walk(Struct, const_cast<void*>(Memory), FieldPath, Unused);
	}

	bool ReadField(const UScriptStruct* Struct, const void* Memory, const FString& FieldPath, FWidgetAnimParameterValue& InOutValue)
	{
		void* ValuePtr = nullptr;
		const FProperty* Field = WidgetAnimParametersLocal::Walk(Struct, const_cast<void*>(Memory), FieldPath, ValuePtr);
		if (!Field || !ValuePtr)
		{
			return false;
		}

		if (const UEnum* Enum = WidgetAnimParametersLocal::GetFieldEnum(Field))
		{
			int64 EnumValue = 0;
			if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Field))
			{
				EnumValue = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(ValuePtr);
			}
			else
			{
				EnumValue = static_cast<int64>(*static_cast<const uint8*>(ValuePtr));
			}
			const int32 Index = Enum->GetIndexByValue(EnumValue);
			InOutValue.Option = Index != INDEX_NONE ? FName(*Enum->GetNameStringByIndex(Index)) : FName();
			InOutValue.Integer = FMath::Max(Index, 0);
			return true;
		}
		if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Field))
		{
			InOutValue.bBool = BoolProperty->GetPropertyValue(ValuePtr);
			return true;
		}
		if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Field))
		{
			const double Number = NumericProperty->IsFloatingPoint()
				? NumericProperty->GetFloatingPointPropertyValue(ValuePtr)
				: static_cast<double>(NumericProperty->GetSignedIntPropertyValue(ValuePtr));
			InOutValue.SetFromVector(FVector4(Number, Number, Number, Number));
			return true;
		}
		if (const FNameProperty* NameProperty = CastField<FNameProperty>(Field))
		{
			InOutValue.Option = NameProperty->GetPropertyValue(ValuePtr);
			return true;
		}
		if (const FStrProperty* StringProperty = CastField<FStrProperty>(Field))
		{
			InOutValue.Option = FName(*StringProperty->GetPropertyValue(ValuePtr));
			return true;
		}
		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Field))
		{
			const UScriptStruct* FieldStruct = StructProperty->Struct;
			FVector4 Vector;
			if (FieldStruct == TBaseStructure<FLinearColor>::Get())
			{
				const FLinearColor& Color = *static_cast<const FLinearColor*>(ValuePtr);
				Vector = FVector4(Color.R, Color.G, Color.B, Color.A);
			}
			else if (FieldStruct == TBaseStructure<FColor>::Get())
			{
				const FLinearColor Color = FLinearColor(*static_cast<const FColor*>(ValuePtr));
				Vector = FVector4(Color.R, Color.G, Color.B, Color.A);
			}
			else if (FieldStruct == TBaseStructure<FVector2D>::Get())
			{
				const FVector2D& Vector2D = *static_cast<const FVector2D*>(ValuePtr);
				Vector = FVector4(Vector2D.X, Vector2D.Y, 0.0, 0.0);
			}
			else if (FieldStruct == TVariantStructure<FVector2f>::Get())
			{
				const FVector2f& Vector2f = *static_cast<const FVector2f*>(ValuePtr);
				Vector = FVector4(Vector2f.X, Vector2f.Y, 0.0, 0.0);
			}
			else if (FieldStruct == TBaseStructure<FVector>::Get())
			{
				const FVector& Vector3 = *static_cast<const FVector*>(ValuePtr);
				Vector = FVector4(Vector3.X, Vector3.Y, Vector3.Z, 0.0);
			}
			else
			{
				return false;
			}
			InOutValue.SetFromVector(Vector);
			return true;
		}
		return false;
	}

	bool GetNaturalType(const FProperty* Field, EWidgetAnimParameterType& OutType)
	{
		if (!Field)
		{
			return false;
		}
		if (WidgetAnimParametersLocal::GetFieldEnum(Field) || CastField<FNameProperty>(Field) || CastField<FStrProperty>(Field))
		{
			OutType = EWidgetAnimParameterType::Choice;
			return true;
		}
		if (CastField<FBoolProperty>(Field))
		{
			OutType = EWidgetAnimParameterType::Bool;
			return true;
		}
		if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Field))
		{
			OutType = NumericProperty->IsFloatingPoint() ? EWidgetAnimParameterType::Float : EWidgetAnimParameterType::Integer;
			return true;
		}
		const FStructProperty* StructProperty = CastField<FStructProperty>(Field);
		const UScriptStruct* Struct = StructProperty ? StructProperty->Struct : nullptr;
		if (Struct == TBaseStructure<FLinearColor>::Get() || Struct == TBaseStructure<FColor>::Get())
		{
			OutType = EWidgetAnimParameterType::Color;
			return true;
		}
		if (Struct == TBaseStructure<FVector2D>::Get() || Struct == TVariantStructure<FVector2f>::Get() || Struct == TBaseStructure<FVector>::Get())
		{
			OutType = EWidgetAnimParameterType::Vector2D;
			return true;
		}
		return false;
	}

	bool CanBind(const FProperty* Field, EWidgetAnimParameterType Type)
	{
		if (!Field)
		{
			return false;
		}
		const UEnum* Enum = WidgetAnimParametersLocal::GetFieldEnum(Field);
		const bool bNumber = !Enum && CastField<FNumericProperty>(Field) != nullptr;
		const bool bBool = CastField<FBoolProperty>(Field) != nullptr;
		const FStructProperty* StructProperty = CastField<FStructProperty>(Field);
		const UScriptStruct* Struct = StructProperty ? StructProperty->Struct : nullptr;
		const bool bColor = Struct == TBaseStructure<FLinearColor>::Get() || Struct == TBaseStructure<FColor>::Get();
		const bool bVector = Struct == TBaseStructure<FVector2D>::Get() || Struct == TVariantStructure<FVector2f>::Get() || Struct == TBaseStructure<FVector>::Get();
		const bool bText = CastField<FNameProperty>(Field) != nullptr || CastField<FStrProperty>(Field) != nullptr;

		switch (Type)
		{
		case EWidgetAnimParameterType::Float:    return bNumber || bVector;
		case EWidgetAnimParameterType::Integer:  return bNumber || bVector || Enum != nullptr;
		case EWidgetAnimParameterType::Bool:     return bBool || bNumber;
		case EWidgetAnimParameterType::Color:    return bColor;
		case EWidgetAnimParameterType::Vector2D: return bVector;
		case EWidgetAnimParameterType::Choice:   return Enum != nullptr || bText;
		default:                                 return false;
		}
	}

	bool IsNameFree(const TArray<FWidgetAnimParameter>& Parameters, FName Name, int32 Ignore)
	{
		if (Name.IsNone())
		{
			return false;
		}
		for (int32 Index = 0; Index < Parameters.Num(); ++Index)
		{
			if (Index != Ignore && Parameters[Index].Name == Name)
			{
				return false;
			}
		}
		return true;
	}

	FName MakeUniqueName(const TArray<FWidgetAnimParameter>& Parameters, const FString& Base, int32 Ignore)
	{
		const FString Trimmed = Base.TrimStartAndEnd().IsEmpty() ? FString(TEXT("Parameter")) : Base.TrimStartAndEnd();
		FName Candidate(*Trimmed);
		for (int32 Number = 2; !IsNameFree(Parameters, Candidate, Ignore); ++Number)
		{
			Candidate = FName(*FString::Printf(TEXT("%s %d"), *Trimmed, Number));
		}
		return Candidate;
	}
}

// =====================================================================================================
// UWidgetAnimParameterLibrary
// =====================================================================================================

FWidgetAnimParameterValue UWidgetAnimParameterLibrary::MakeAnimationParameterFloat(FName Name, float Value)
{
	FWidgetAnimParameterValue Made;
	Made.Name = Name;
	Made.Type = EWidgetAnimParameterType::Float;
	Made.Float = Value;
	return Made;
}

FWidgetAnimParameterValue UWidgetAnimParameterLibrary::MakeAnimationParameterInteger(FName Name, int32 Value)
{
	FWidgetAnimParameterValue Made;
	Made.Name = Name;
	Made.Type = EWidgetAnimParameterType::Integer;
	Made.Integer = Value;
	return Made;
}

FWidgetAnimParameterValue UWidgetAnimParameterLibrary::MakeAnimationParameterBool(FName Name, bool bValue)
{
	FWidgetAnimParameterValue Made;
	Made.Name = Name;
	Made.Type = EWidgetAnimParameterType::Bool;
	Made.bBool = bValue;
	return Made;
}

FWidgetAnimParameterValue UWidgetAnimParameterLibrary::MakeAnimationParameterColor(FName Name, FLinearColor Value)
{
	FWidgetAnimParameterValue Made;
	Made.Name = Name;
	Made.Type = EWidgetAnimParameterType::Color;
	Made.Color = Value;
	return Made;
}

FWidgetAnimParameterValue UWidgetAnimParameterLibrary::MakeAnimationParameterVector2D(FName Name, FVector2D Value)
{
	FWidgetAnimParameterValue Made;
	Made.Name = Name;
	Made.Type = EWidgetAnimParameterType::Vector2D;
	Made.Vector2D = Value;
	return Made;
}

FWidgetAnimParameterValue UWidgetAnimParameterLibrary::MakeAnimationParameterChoice(FName Name, FName Option)
{
	FWidgetAnimParameterValue Made;
	Made.Name = Name;
	Made.Type = EWidgetAnimParameterType::Choice;
	Made.Option = Option;
	return Made;
}

TArray<FName> UWidgetAnimParameterLibrary::GetAnimationParameterNames(const UWidgetAnimPresetAsset* Animation)
{
	TArray<FName> Names;
	if (Animation)
	{
		for (const FWidgetAnimParameter& Parameter : Animation->Parameters)
		{
			Names.Add(Parameter.Name);
		}
	}
	return Names;
}

bool UWidgetAnimParameterLibrary::GetAnimationParameterDefault(const UWidgetAnimPresetAsset* Animation, FName Name, FWidgetAnimParameterValue& OutValue)
{
	const FWidgetAnimParameter* Found = Animation ? Animation->FindParameter(Name) : nullptr;
	if (!Found)
	{
		return false;
	}
	OutValue = FWidgetAnimParameterValue::FromDefault(*Found);
	return true;
}
