// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetDataValues.h"

#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "INotifyFieldValueChanged.h"
#include "UObject/EnumProperty.h"
#include "UObject/TextProperty.h"
#include "UObject/UnrealType.h"

// =====================================================================================================
// FWidgetDataPath
// =====================================================================================================

void FWidgetDataPath::Set(const FString& InPath)
{
	PathText = InPath;
	PathText.TrimStartAndEndInline();

	Segments.Reset();

	TArray<FString> Parts;
	PathText.ParseIntoArray(Parts, TEXT("."), /*bCullEmpty=*/true);
	for (FString& Part : Parts)
	{
		Part.TrimStartAndEndInline();
		if (!Part.IsEmpty())
		{
			Segments.Add(FName(*Part));
		}
	}

	Cache.Reset();
	Cache.SetNum(Segments.Num());
}

// =====================================================================================================
// FWidgetDataValue
// =====================================================================================================

namespace WidgetDataValuesPrivate
{
	/**
	 * Components that aren't reached through a property: ones a C++ actor created without a UPROPERTY, or
	 * added at runtime. Blueprint-added components are also properties, and those are found first.
	 */
	static UActorComponent* FindComponentByName(AActor* Actor, FName ComponentName)
	{
		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (Component && Component->GetFName() == ComponentName)
			{
				return Component;
			}
		}

		return nullptr;
	}

	static void* AllocateAligned(int32 Size, uint32 Alignment)
	{
		void* Memory = FMemory::Malloc(FMath::Max(Size, 1), FMath::Max<uint32>(Alignment, 1));
		FMemory::Memzero(Memory, FMath::Max(Size, 1));
		return Memory;
	}
}

void FWidgetDataValue::Release()
{
	for (FBuffer& Buffer : Buffers)
	{
		if (!Buffer.Memory)
		{
			continue;
		}

		if (Buffer.Function)
		{
			for (TFieldIterator<FProperty> It(Buffer.Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
			{
				It->DestroyValue_InContainer(Buffer.Memory);
			}
		}
		else if (Buffer.CopiedProperty)
		{
			Buffer.CopiedProperty->DestroyValue(Buffer.Memory);
		}

		FMemory::Free(Buffer.Memory);
	}

	Buffers.Reset();
	Property = nullptr;
	ValuePtr = nullptr;
	OwnerObject = nullptr;
	bDirectStorage = false;
}

bool FWidgetDataValue::Resolve(UObject* Root, const FWidgetDataPath& Path, FWidgetDataValue& Out, FString* OutError, bool bCallGetters)
{
	Out.Release();

	auto Fail = [OutError](FString&& Message)
	{
		if (OutError)
		{
			*OutError = MoveTemp(Message);
		}
		return false;
	};

	if (!::IsValid(Root))
	{
		return Fail(TEXT("the object to read from is missing"));
	}

	if (Path.Segments.IsEmpty())
	{
		return Fail(TEXT("the path is empty"));
	}

	if (Path.Cache.Num() != Path.Segments.Num())
	{
		Path.Cache.SetNum(Path.Segments.Num());
	}

	// Exactly one of these describes where the next segment is looked up: an object, or a struct value
	// (StructType + StructMemory) reached through a struct property.
	UObject* CurrentObject = Root;
	const UStruct* StructType = nullptr;
	uint8* StructMemory = nullptr;

	for (int32 Index = 0; Index < Path.Segments.Num(); ++Index)
	{
		const FName Segment = Path.Segments[Index];
		const bool bLastSegment = Index == Path.Segments.Num() - 1;

		const UStruct* Scope = CurrentObject ? CurrentObject->GetClass() : StructType;
		void* Container = CurrentObject ? static_cast<void*>(CurrentObject) : static_cast<void*>(StructMemory);

		FWidgetDataPath::FSegmentCache& Cached = Path.Cache[Index];
		if (Cached.Scope.Get() != Scope)
		{
			Cached = FWidgetDataPath::FSegmentCache();
			Cached.Scope = Scope;
			Cached.Property = FindFProperty<FProperty>(Scope, Segment);

			if (!Cached.Property)
			{
				if (const UClass* ScopeClass = Cast<UClass>(Scope))
				{
					UFunction* Function = ScopeClass->FindFunctionByName(Segment);
					if (WidgetDataValues::IsGetterFunction(Function))
					{
						Cached.Getter = Function;
					}
				}
			}
		}

		const FProperty* SegmentProperty = nullptr;
		void* SegmentValue = nullptr;
		bool bSegmentDirect = false;

		if (FProperty* Found = Cached.Property)
		{
			SegmentProperty = Found;

			if (Found->HasGetter())
			{
				// A native getter can compute the value, so there may be no storage to point at. Read a copy.
				FBuffer& Buffer = Out.Buffers.AddDefaulted_GetRef();
				Buffer.CopiedProperty = Found;
				Buffer.Memory = static_cast<uint8*>(WidgetDataValuesPrivate::AllocateAligned(Found->GetSize(), Found->GetMinAlignment()));
				Found->InitializeValue(Buffer.Memory);
				Found->GetValue_InContainer(Container, Buffer.Memory);
				SegmentValue = Buffer.Memory;
			}
			else
			{
				SegmentValue = Found->ContainerPtrToValuePtr<void>(Container);
				bSegmentDirect = true;
			}
		}
		else if (UFunction* Getter = Cached.Getter.Get(); Getter && CurrentObject && bCallGetters)
		{
			// Same parameter setup the engine uses when it calls a function by name: zeroed, every parameter
			// initialised, then destroyed again when this value is released.
			FBuffer& Buffer = Out.Buffers.AddDefaulted_GetRef();
			Buffer.Function = Getter;
			Buffer.Memory = static_cast<uint8*>(WidgetDataValuesPrivate::AllocateAligned(Getter->ParmsSize, Getter->GetMinAlignment()));
			for (TFieldIterator<FProperty> It(Getter); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
			{
				It->InitializeValue_InContainer(Buffer.Memory);
			}

			CurrentObject->ProcessEvent(Getter, Buffer.Memory);

			SegmentProperty = WidgetDataValues::GetGetterOutput(Getter);
			SegmentValue = SegmentProperty ? SegmentProperty->ContainerPtrToValuePtr<void>(Buffer.Memory) : nullptr;
		}
		else if (AActor* Actor = Cast<AActor>(CurrentObject); Actor && !bLastSegment)
		{
			if (UActorComponent* Component = WidgetDataValuesPrivate::FindComponentByName(Actor, Segment))
			{
				CurrentObject = Component;
				continue;
			}
		}

		if (!SegmentProperty || !SegmentValue)
		{
			return Fail(FString::Printf(TEXT("'%s' has nothing called '%s'"), *GetNameSafe(Scope), *Segment.ToString()));
		}

		if (bLastSegment)
		{
			Out.Property = SegmentProperty;
			Out.ValuePtr = SegmentValue;
			Out.OwnerObject = CurrentObject;
			Out.bDirectStorage = bSegmentDirect;
			return true;
		}

		// Step inside the value for the next segment.
		if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(SegmentProperty))
		{
			UObject* Next = ObjectProperty->GetObjectPropertyValue(SegmentValue);
			if (!::IsValid(Next))
			{
				return Fail(FString::Printf(TEXT("'%s' is empty"), *Segment.ToString()));
			}

			CurrentObject = Next;
			StructType = nullptr;
			StructMemory = nullptr;
		}
		else if (const FStructProperty* StructProperty = CastField<FStructProperty>(SegmentProperty))
		{
			CurrentObject = nullptr;
			StructType = StructProperty->Struct;
			StructMemory = static_cast<uint8*>(SegmentValue);
		}
		else
		{
			return Fail(FString::Printf(TEXT("'%s' is a value with nothing inside it"), *Segment.ToString()));
		}
	}

	return Fail(TEXT("the path is empty"));
}

// =====================================================================================================
// FWidgetDataScratch
// =====================================================================================================

FWidgetDataScratch::FWidgetDataScratch(const FProperty* InProperty)
	: ScratchProperty(InProperty)
{
	if (ScratchProperty)
	{
		Memory = WidgetDataValuesPrivate::AllocateAligned(ScratchProperty->GetSize(), ScratchProperty->GetMinAlignment());
		ScratchProperty->InitializeValue(Memory);
	}
}

FWidgetDataScratch::~FWidgetDataScratch()
{
	if (Memory)
	{
		ScratchProperty->DestroyValue(Memory);
		FMemory::Free(Memory);
	}
}

// =====================================================================================================
// Value helpers
// =====================================================================================================

bool WidgetDataValues::IsNumeric(const FProperty* Property)
{
	return CastField<FNumericProperty>(Property) || CastField<FBoolProperty>(Property) || CastField<FEnumProperty>(Property);
}

bool WidgetDataValues::ReadNumber(const FProperty* Property, const void* Value, double& OutNumber)
{
	if (!Property || !Value)
	{
		return false;
	}

	if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
	{
		OutNumber = BoolProperty->GetPropertyValue(Value) ? 1.0 : 0.0;
		return true;
	}

	if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
	{
		OutNumber = static_cast<double>(EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(Value));
		return true;
	}

	if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
	{
		OutNumber = NumericProperty->IsFloatingPoint()
			? NumericProperty->GetFloatingPointPropertyValue(Value)
			: static_cast<double>(NumericProperty->GetSignedIntPropertyValue(Value));
		return true;
	}

	return false;
}

bool WidgetDataValues::WriteNumber(const FProperty* Property, void* Value, double Number)
{
	if (!Property || !Value)
	{
		return false;
	}

	if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
	{
		BoolProperty->SetPropertyValue(Value, Number != 0.0);
		return true;
	}

	if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
	{
		EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(Value, static_cast<int64>(FMath::RoundHalfFromZero(Number)));
		return true;
	}

	if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
	{
		if (NumericProperty->IsFloatingPoint())
		{
			NumericProperty->SetFloatingPointPropertyValue(Value, Number);
		}
		else
		{
			NumericProperty->SetIntPropertyValue(Value, static_cast<int64>(FMath::RoundHalfFromZero(Number)));
		}
		return true;
	}

	return false;
}

bool WidgetDataValues::IsTruthy(const FProperty* Property, const void* Value)
{
	if (!Property || !Value)
	{
		return false;
	}

	double Number = 0.0;
	if (ReadNumber(Property, Value, Number))
	{
		return Number != 0.0;
	}

	if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
	{
		return IsValid(ObjectProperty->GetObjectPropertyValue(Value));
	}

	if (const FStrProperty* StringProperty = CastField<FStrProperty>(Property))
	{
		return !StringProperty->GetPropertyValue(Value).IsEmpty();
	}

	if (const FTextProperty* TextProperty = CastField<FTextProperty>(Property))
	{
		return !TextProperty->GetPropertyValue(Value).IsEmpty();
	}

	if (const FNameProperty* NameProperty = CastField<FNameProperty>(Property))
	{
		return !NameProperty->GetPropertyValue(Value).IsNone();
	}

	return false;
}

namespace WidgetDataValuesPrivate
{
	static bool HasTruthValue(const FProperty* Property)
	{
		return WidgetDataValues::IsNumeric(Property)
			|| CastField<FObjectPropertyBase>(Property)
			|| CastField<FStrProperty>(Property)
			|| CastField<FTextProperty>(Property)
			|| CastField<FNameProperty>(Property);
	}
}

bool WidgetDataValues::CanConvert(const FProperty* From, const FProperty* To)
{
	if (!From || !To)
	{
		return false;
	}

	if (To->SameType(From))
	{
		return true;
	}

	if (IsNumeric(From) && IsNumeric(To))
	{
		return true;
	}

	return CastField<FBoolProperty>(To) && WidgetDataValuesPrivate::HasTruthValue(From);
}

bool WidgetDataValues::Convert(const FProperty* From, const void* FromValue, const FProperty* To, void* ToValue)
{
	if (!From || !FromValue || !To || !ToValue)
	{
		return false;
	}

	if (To->SameType(From))
	{
		To->CopyCompleteValue(ToValue, FromValue);
		return true;
	}

	double Number = 0.0;
	if (IsNumeric(To) && ReadNumber(From, FromValue, Number))
	{
		return WriteNumber(To, ToValue, Number);
	}

	if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(To))
	{
		if (WidgetDataValuesPrivate::HasTruthValue(From))
		{
			BoolProperty->SetPropertyValue(ToValue, IsTruthy(From, FromValue));
			return true;
		}
	}

	return false;
}

bool WidgetDataValues::WriteField(UObject* Target, const FProperty* Field, const void* NewValue)
{
	if (!IsValid(Target) || !Field || !NewValue)
	{
		return false;
	}

	bool bUnchanged = false;
	if (Field->HasGetter())
	{
		FWidgetDataScratch Current(Field);
		Field->GetValue_InContainer(Target, Current.Get());
		bUnchanged = Field->Identical(Current.Get(), NewValue);
	}
	else
	{
		bUnchanged = Field->Identical(Field->ContainerPtrToValuePtr<void>(Target), NewValue);
	}

	if (bUnchanged)
	{
		return false;
	}

	if (Field->HasSetter())
	{
		Field->SetValue_InContainer(Target, NewValue);
	}
	else
	{
		Field->CopyCompleteValue(Field->ContainerPtrToValuePtr<void>(Target), NewValue);
	}

	// Always broadcast, even after a native setter that may already have: a setter that doesn't broadcast would
	// otherwise leave View Bindings and triggers silently stale, and a second broadcast of a real change costs
	// one extra binding update.
	BroadcastField(Target, Field->GetFName());
	return true;
}

void WidgetDataValues::BroadcastField(UObject* Target, FName FieldName)
{
	INotifyFieldValueChanged* Notifier = Cast<INotifyFieldValueChanged>(Target);
	if (!Notifier)
	{
		return;
	}

	const UE::FieldNotification::FFieldId FieldId = Notifier->GetFieldNotificationDescriptor().GetField(Target->GetClass(), FieldName);
	if (FieldId.IsValid())
	{
		Notifier->BroadcastFieldValueChanged(FieldId);
	}
}

const FProperty* WidgetDataValues::GetGetterOutput(const UFunction* Function)
{
	if (!Function)
	{
		return nullptr;
	}

	// A C++ getter returns its value; a Blueprint function has no return value and hands it out through its
	// output pin instead, which is an out parameter. Either counts, as long as there is exactly one output and
	// no inputs. By-reference inputs are out parameters too, and are told apart by CPF_ReferenceParm.
	const FProperty* Output = nullptr;
	for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
	{
		const bool bIsOutput = It->HasAnyPropertyFlags(CPF_ReturnParm)
			|| (It->HasAnyPropertyFlags(CPF_OutParm) && !It->HasAnyPropertyFlags(CPF_ReferenceParm));

		if (!bIsOutput || Output)
		{
			return nullptr;
		}

		Output = *It;
	}

	return Output;
}

bool WidgetDataValues::IsGetterFunction(const UFunction* Function)
{
	return GetGetterOutput(Function) != nullptr;
}