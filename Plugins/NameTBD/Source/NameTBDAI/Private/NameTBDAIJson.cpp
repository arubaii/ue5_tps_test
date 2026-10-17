// Copyright AR GameSystems 2026. All rights reserved.

#include "NameTBDAIJson.h"

#include "Engine/Blueprint.h"
#include "Layout/Margin.h"
#include "Misc/OutputDevice.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateColor.h"
#include "StructUtils/InstancedStruct.h"
#include "StructUtils/PropertyBag.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

namespace NameTBDAIJson
{
	namespace
	{
		/** Gathers what ImportText complains about, to say why a value didn't take. */
		class FErrorCollector : public FOutputDevice
		{
		public:
			FString Text;
			virtual void Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category) override
			{
				Text += Text.IsEmpty() ? FString(Message) : FString(TEXT(" ")) + Message;
			}
		};

		bool IsInstancedStruct(const FProperty* Property)
		{
			const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
			return StructProperty && StructProperty->Struct == FInstancedStruct::StaticStruct();
		}

		bool IsPropertyBag(const FProperty* Property)
		{
			const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
			return StructProperty && StructProperty->Struct == FInstancedPropertyBag::StaticStruct();
		}

		FString DescribeJson(const TSharedPtr<FJsonValue>& Value)
		{
			FString Text = ToString(Value);
			return Text.Len() > 80 ? Text.Left(77) + TEXT("...") : Text;
		}

		/** The text to hand ImportText for a JSON value: strings as they are, everything else as written. */
		FString AsImportText(const TSharedPtr<FJsonValue>& Value)
		{
			if (!Value.IsValid())
			{
				return FString();
			}
			switch (Value->Type)
			{
			case EJson::String: return Value->AsString();
			case EJson::Boolean: return Value->AsBool() ? TEXT("True") : TEXT("False");
			case EJson::Number: return FString::SanitizeFloat(Value->AsNumber());
			case EJson::Null: return TEXT("None");
			default: return ToString(Value);
			}
		}

		const TMap<FString, FLinearColor>& NamedColors()
		{
			static const TMap<FString, FLinearColor> Colors = {
				{ TEXT("white"), FLinearColor::White },
				{ TEXT("black"), FLinearColor::Black },
				{ TEXT("transparent"), FLinearColor::Transparent },
				{ TEXT("clear"), FLinearColor::Transparent },
				{ TEXT("red"), FLinearColor::Red },
				{ TEXT("green"), FLinearColor::Green },
				{ TEXT("blue"), FLinearColor::Blue },
				{ TEXT("yellow"), FLinearColor::Yellow },
				{ TEXT("gray"), FLinearColor::Gray },
				{ TEXT("grey"), FLinearColor::Gray },
			};
			return Colors;
		}

		/** Assigns a JSON array of numbers to a struct's numeric fields in order: [x, y] to X, Y; [r, g, b, a] to R, G, B, A. */
		bool WriteNumbersInOrder(const UScriptStruct* Struct, void* Address, const TArray<TSharedPtr<FJsonValue>>& Numbers, FString& OutError)
		{
			int32 Index = 0;
			for (TFieldIterator<FProperty> It(Struct); It && Index < Numbers.Num(); ++It)
			{
				FNumericProperty* Numeric = CastField<FNumericProperty>(*It);
				if (!Numeric)
				{
					continue;
				}
				double Value = 0.0;
				if (!Numbers[Index]->TryGetNumber(Value))
				{
					OutError = FString::Printf(TEXT("Expected numbers for %s."), *Struct->GetName());
					return false;
				}
				void* FieldAddress = Numeric->ContainerPtrToValuePtr<void>(Address);
				if (Numeric->IsFloatingPoint())
				{
					Numeric->SetFloatingPointPropertyValue(FieldAddress, Value);
				}
				else
				{
					Numeric->SetIntPropertyValue(FieldAddress, static_cast<int64>(FMath::RoundToDouble(Value)));
				}
				++Index;
			}
			if (Index < Numbers.Num())
			{
				OutError = FString::Printf(TEXT("%s takes %d numbers, not %d."), *Struct->GetName(), Index, Numbers.Num());
				return false;
			}
			return true;
		}

		int32 CountNumericFields(const UScriptStruct* Struct)
		{
			int32 Count = 0;
			for (TFieldIterator<FProperty> It(Struct); It; ++It)
			{
				Count += CastField<FNumericProperty>(*It) ? 1 : 0;
			}
			return Count;
		}

		bool StructIsAllNumbers(const UScriptStruct* Struct)
		{
			int32 Total = 0;
			for (TFieldIterator<FProperty> It(Struct); It; ++It)
			{
				++Total;
			}
			return Total > 0 && Total == CountNumericFields(Struct);
		}

		/** One segment of a path: "Keys[2]" -> Name "Keys", Index 2; "Triggers+" -> bAppend. */
		struct FSegment
		{
			FString Name;
			int32 Index = INDEX_NONE;
			bool bAppend = false;
		};

		bool ParseSegment(const FString& Raw, FSegment& Out, FString& OutError)
		{
			FString Text = Raw.TrimStartAndEnd();
			if (Text.EndsWith(TEXT("+")))
			{
				Out.bAppend = true;
				Text.LeftChopInline(1);
			}
			int32 Bracket = INDEX_NONE;
			if (Text.FindChar(TEXT('['), Bracket))
			{
				const FString IndexText = Text.Mid(Bracket + 1).Replace(TEXT("]"), TEXT("")).TrimStartAndEnd();
				if (!IndexText.IsNumeric())
				{
					OutError = FString::Printf(TEXT("'%s': write a list entry as Name[0]."), *Raw);
					return false;
				}
				Out.Index = FCString::Atoi(*IndexText);
				Text = Text.Left(Bracket).TrimStartAndEnd();
			}
			Out.Name = Text;
			return !Out.Name.IsEmpty();
		}

		/** Splits "A.B[1].C" at its dots, keeping brackets together. */
		void SplitPath(const FString& Path, TArray<FString>& Out)
		{
			Path.ParseIntoArray(Out, TEXT("."), /*bCullEmpty=*/true);
		}

		/** The struct and memory one step into a value: a struct's own fields, what an instanced struct holds, a bag's values, an object's. */
		bool StepInto(FProperty* Property, void* Address, const UStruct*& OutStruct, void*& OutContainer, UObject*& InOutOwner, FString& OutError)
		{
			if (IsInstancedStruct(Property))
			{
				FInstancedStruct* Instanced = static_cast<FInstancedStruct*>(Address);
				if (!Instanced->IsValid())
				{
					OutError = FString::Printf(TEXT("'%s' holds nothing yet: set it to {\"$type\": \"<struct>\"} first."), *Property->GetName());
					return false;
				}
				OutStruct = Instanced->GetScriptStruct();
				OutContainer = Instanced->GetMutableMemory();
				return true;
			}
			if (IsPropertyBag(Property))
			{
				FInstancedPropertyBag* Bag = static_cast<FInstancedPropertyBag*>(Address);
				const UPropertyBag* BagStruct = Bag->GetPropertyBagStruct();
				if (!BagStruct)
				{
					OutError = FString::Printf(TEXT("'%s' has no fields yet."), *Property->GetName());
					return false;
				}
				OutStruct = BagStruct;
				OutContainer = Bag->GetMutableValue().GetMemory();
				return true;
			}
			if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				OutStruct = StructProperty->Struct;
				OutContainer = Address;
				return true;
			}
			if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
			{
				UObject* Inner = ObjectProperty->GetObjectPropertyValue(Address);
				if (!Inner)
				{
					OutError = FString::Printf(TEXT("'%s' is None."), *Property->GetName());
					return false;
				}
				Inner->Modify();
				OutStruct = Inner->GetClass();
				OutContainer = Inner;
				InOutOwner = Inner;
				return true;
			}
			OutError = FString::Printf(TEXT("'%s' has no fields inside it."), *Property->GetName());
			return false;
		}

		void ListFieldNames(const UStruct* Struct, FString& Out)
		{
			TArray<FString> Names;
			for (TFieldIterator<FProperty> It(Struct); It; ++It)
			{
				if (!It->HasAnyPropertyFlags(CPF_Deprecated | CPF_Transient) && (It->HasAnyPropertyFlags(CPF_Edit) || !Struct->IsA<UClass>()))
				{
					Names.Add(It->GetAuthoredName());
				}
				if (Names.Num() >= 40)
				{
					Names.Add(TEXT("..."));
					break;
				}
			}
			Out = FString::Join(Names, TEXT(", "));
		}
	}

	// =================================================================================================================
	// Names
	// =================================================================================================================

	FString Normalize(const FString& Name)
	{
		FString Out;
		Out.Reserve(Name.Len());
		for (const TCHAR Char : Name)
		{
			if (Char == TEXT(' ') || Char == TEXT('_') || Char == TEXT('-') || Char == TEXT('.'))
			{
				continue;
			}
			Out.AppendChar(FChar::ToLower(Char));
		}
		return Out;
	}

	FProperty* FindField(const UStruct* Struct, const FString& Name, FString* OutError)
	{
		if (!Struct)
		{
			return nullptr;
		}
		const FString Key = Normalize(Name);
		TArray<FProperty*> Starts;
		TArray<FProperty*> Contains;

		// Exact matches by rank, whatever order the fields come in: the property's own name, then its display name, then a
		// bool without its b. A shape's "Fill" is its Fill paint, not bFill (whose display name is also "Fill").
		FProperty* ByDisplay = nullptr;
		FProperty* ByBareBool = nullptr;
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			FProperty* Property = *It;
			if (Property->HasAnyPropertyFlags(CPF_Deprecated))
			{
				continue;
			}
			const FString Own = Normalize(Property->GetAuthoredName());
			FString Display = Normalize(Property->GetMetaData(TEXT("DisplayName")));
			const bool bBool = CastField<FBoolProperty>(Property) != nullptr;
			if (Own == Key)
			{
				return Property;
			}
			if (!ByDisplay && !Display.IsEmpty() && Display == Key)
			{
				ByDisplay = Property;
			}
			if (!ByBareBool && bBool && Own.StartsWith(TEXT("b")) && Own.Mid(1) == Key)
			{
				ByBareBool = Property;
			}
			const FString Bare = bBool && Own.StartsWith(TEXT("b")) ? Own.Mid(1) : Own;
			if (Key.Len() >= 3 && (Bare.StartsWith(Key) || (!Display.IsEmpty() && Display.StartsWith(Key))))
			{
				Starts.Add(Property);
			}
			else if (Key.Len() >= 4 && (Bare.Contains(Key) || (!Display.IsEmpty() && Display.Contains(Key))))
			{
				Contains.Add(Property);
			}
		}
		if (ByDisplay || ByBareBool)
		{
			return ByDisplay ? ByDisplay : ByBareBool;
		}
		if (Starts.Num() == 1)
		{
			return Starts[0];
		}
		if (Starts.Num() == 0 && Contains.Num() == 1)
		{
			return Contains[0];
		}
		if (OutError)
		{
			const TArray<FProperty*>& Candidates = Starts.Num() > 1 ? Starts : Contains;
			if (Candidates.Num() > 1)
			{
				TArray<FString> Names;
				for (const FProperty* Candidate : Candidates)
				{
					Names.Add(Candidate->GetAuthoredName());
				}
				*OutError = FString::Printf(TEXT("'%s' could be: %s."), *Name, *FString::Join(Names, TEXT(", ")));
			}
			else
			{
				FString Fields;
				ListFieldNames(Struct, Fields);
				*OutError = FString::Printf(TEXT("No field '%s' on %s. Fields: %s."), *Name, *Struct->GetName(), *Fields);
			}
		}
		return nullptr;
	}

	// =================================================================================================================
	// Colors and classes
	// =================================================================================================================

	bool ParseColor(const TSharedPtr<FJsonValue>& Value, FLinearColor& OutColor)
	{
		if (!Value.IsValid())
		{
			return false;
		}
		if (Value->Type == EJson::String)
		{
			FString Text = Value->AsString().TrimStartAndEnd();
			if (const FLinearColor* Named = NamedColors().Find(Text.ToLower()))
			{
				OutColor = *Named;
				return true;
			}
			if (Text.StartsWith(TEXT("#")))
			{
				Text.RightChopInline(1);
				if (Text.Len() == 3 || Text.Len() == 4)
				{
					FString Long;
					for (const TCHAR Char : Text)
					{
						Long.AppendChar(Char);
						Long.AppendChar(Char);
					}
					Text = Long;
				}
				if (Text.Len() != 6 && Text.Len() != 8)
				{
					return false;
				}
				for (const TCHAR Char : Text)
				{
					if (!FChar::IsHexDigit(Char))
					{
						return false;
					}
				}
				// FColor::FromHex reads RRGGBB or RRGGBBAA; the color is sRGB, as pickers show it.
				OutColor = FLinearColor(FColor::FromHex(Text));
				return true;
			}
			if (Text.StartsWith(TEXT("(")))
			{
				return OutColor.InitFromString(Text);
			}
			return false;
		}
		if (Value->Type == EJson::Array)
		{
			const TArray<TSharedPtr<FJsonValue>>& Parts = Value->AsArray();
			if (Parts.Num() < 3 || Parts.Num() > 4)
			{
				return false;
			}
			double Channels[4] = { 0.0, 0.0, 0.0, 1.0 };
			for (int32 Index = 0; Index < Parts.Num(); ++Index)
			{
				if (!Parts[Index]->TryGetNumber(Channels[Index]))
				{
					return false;
				}
			}
			OutColor = FLinearColor(Channels[0], Channels[1], Channels[2], Channels[3]);
			return true;
		}
		if (Value->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			double R = 0, G = 0, B = 0, A = 1;
			bool bAny = false;
			for (const auto& Pair : Object->Values)
			{
				const FString Key = Normalize(Pair.Key);
				double Number = 0.0;
				if (!Pair.Value->TryGetNumber(Number))
				{
					continue;
				}
				if (Key == TEXT("r")) { R = Number; bAny = true; }
				else if (Key == TEXT("g")) { G = Number; bAny = true; }
				else if (Key == TEXT("b")) { B = Number; bAny = true; }
				else if (Key == TEXT("a")) { A = Number; }
			}
			if (bAny)
			{
				OutColor = FLinearColor(R, G, B, A);
			}
			return bAny;
		}
		return false;
	}

	FString ColorToHex(const FLinearColor& Color)
	{
		const FColor Srgb = Color.ToFColor(/*bSRGB=*/true);
		return Srgb.A == 255
			? FString::Printf(TEXT("#%02X%02X%02X"), Srgb.R, Srgb.G, Srgb.B)
			: FString::Printf(TEXT("#%02X%02X%02X%02X"), Srgb.R, Srgb.G, Srgb.B, Srgb.A);
	}

	UClass* FindClass(const FString& InName, const UClass* Base, FString* OutError)
	{
		FString Name = InName.TrimStartAndEnd();
		if (Name.IsEmpty())
		{
			if (OutError) *OutError = TEXT("No class was named.");
			return nullptr;
		}

		UClass* Found = nullptr;
		if (Name.StartsWith(TEXT("/")))
		{
			// A class path, or a Blueprint asset whose generated class is wanted.
			Found = LoadObject<UClass>(nullptr, *Name, nullptr, LOAD_Quiet | LOAD_NoWarn);
			if (!Found)
			{
				if (UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *Name, nullptr, LOAD_Quiet | LOAD_NoWarn))
				{
					Found = Blueprint->GeneratedClass;
				}
			}
		}
		else
		{
			const FString Key = Normalize(Name);
			const FString KeyNoPrefix = Key.Len() > 1 && (Key.StartsWith(TEXT("u")) || Key.StartsWith(TEXT("a"))) ? Key.Mid(1) : Key;
			for (TObjectIterator<UClass> It; It; ++It)
			{
				UClass* Class = *It;
				if (Base && !Class->IsChildOf(Base))
				{
					continue;
				}
				if (Class->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists) || Class->GetName().StartsWith(TEXT("SKEL_"))
					|| Class->GetName().StartsWith(TEXT("REINST_")))
				{
					continue;
				}
				const FString Own = Normalize(Class->GetName());
				const FString Display = Normalize(Class->GetMetaData(TEXT("DisplayName")));
				if (Own == Key || Own == KeyNoPrefix || (!Display.IsEmpty() && Display == Key)
					|| Normalize(Class->GetName().Replace(TEXT("_C"), TEXT(""))) == Key)
				{
					// Prefer native classes over Blueprint ones of the same name.
					if (!Found || (Found->ClassGeneratedBy && !Class->ClassGeneratedBy))
					{
						Found = Class;
					}
				}
			}
		}

		if (!Found)
		{
			if (OutError) *OutError = FString::Printf(TEXT("No class '%s'%s."), *Name, Base ? *FString::Printf(TEXT(" of type %s"), *Base->GetName()) : TEXT(""));
			return nullptr;
		}
		if (Base && !Found->IsChildOf(Base))
		{
			if (OutError) *OutError = FString::Printf(TEXT("'%s' isn't a %s."), *Name, *Base->GetName());
			return nullptr;
		}
		return Found;
	}

	// =================================================================================================================
	// Writing
	// =================================================================================================================

	bool WriteValue(FProperty* Property, void* Address, const TSharedPtr<FJsonValue>& Value, UObject* Owner, FString& OutError)
	{
		if (!Property || !Address || !Value.IsValid())
		{
			OutError = TEXT("Nothing to write.");
			return false;
		}
		const FString Field = Property->GetAuthoredName();

		// ---- Plain values ----
		if (FBoolProperty* Bool = CastField<FBoolProperty>(Property))
		{
			bool bValue = false;
			if (Value->TryGetBool(bValue))
			{
				Bool->SetPropertyValue(Address, bValue);
				return true;
			}
			const FString Text = Normalize(AsImportText(Value));
			if (Text == TEXT("true") || Text == TEXT("1") || Text == TEXT("yes") || Text == TEXT("on"))
			{
				Bool->SetPropertyValue(Address, true);
				return true;
			}
			if (Text == TEXT("false") || Text == TEXT("0") || Text == TEXT("no") || Text == TEXT("off"))
			{
				Bool->SetPropertyValue(Address, false);
				return true;
			}
			OutError = FString::Printf(TEXT("%s takes true or false."), *Field);
			return false;
		}

		const UEnum* Enum = nullptr;
		FNumericProperty* EnumStorage = nullptr;
		if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			Enum = EnumProperty->GetEnum();
			EnumStorage = EnumProperty->GetUnderlyingProperty();
		}
		else if (FByteProperty* Byte = CastField<FByteProperty>(Property); Byte && Byte->Enum)
		{
			Enum = Byte->Enum;
			EnumStorage = Byte;
		}
		if (Enum && EnumStorage)
		{
			double Number = 0.0;
			if (Value->Type == EJson::Number && Value->TryGetNumber(Number))
			{
				EnumStorage->SetIntPropertyValue(Address, static_cast<int64>(Number));
				return true;
			}
			const FString Key = Normalize(AsImportText(Value));
			TArray<FString> Options;
			for (int32 Index = 0; Index < Enum->NumEnums() - (Enum->ContainsExistingMax() ? 1 : 0); ++Index)
			{
				if (Enum->HasMetaData(TEXT("Hidden"), Index))
				{
					continue;
				}
				const FString Short = Enum->GetNameStringByIndex(Index);
				const FString Display = Enum->GetDisplayNameTextByIndex(Index).ToString();
				Options.Add(Short);
				FString ShortKey = Normalize(Short);
				// "HAlign_Center" also answers to "Center".
				int32 Underscore = INDEX_NONE;
				const bool bPrefixed = Short.FindChar(TEXT('_'), Underscore);
				if (ShortKey == Key || Normalize(Display) == Key || (bPrefixed && Normalize(Short.Mid(Underscore + 1)) == Key))
				{
					EnumStorage->SetIntPropertyValue(Address, Enum->GetValueByIndex(Index));
					return true;
				}
			}
			OutError = FString::Printf(TEXT("%s: '%s' isn't one of %s."), *Field, *AsImportText(Value), *FString::Join(Options, TEXT(", ")));
			return false;
		}

		if (FNumericProperty* Numeric = CastField<FNumericProperty>(Property))
		{
			double Number = 0.0;
			if (!Value->TryGetNumber(Number) && !LexTryParseString(Number, *AsImportText(Value).TrimStartAndEnd()))
			{
				OutError = FString::Printf(TEXT("%s takes a number, not %s."), *Field, *DescribeJson(Value));
				return false;
			}
			if (Property->HasMetaData(TEXT("ClampMin")))
			{
				Number = FMath::Max(Number, FCString::Atod(*Property->GetMetaData(TEXT("ClampMin"))));
			}
			if (Property->HasMetaData(TEXT("ClampMax")))
			{
				Number = FMath::Min(Number, FCString::Atod(*Property->GetMetaData(TEXT("ClampMax"))));
			}
			if (Numeric->IsFloatingPoint())
			{
				Numeric->SetFloatingPointPropertyValue(Address, Number);
			}
			else
			{
				Numeric->SetIntPropertyValue(Address, static_cast<int64>(FMath::RoundToDouble(Number)));
			}
			return true;
		}

		if (CastField<FStrProperty>(Property))
		{
			*static_cast<FString*>(Address) = AsImportText(Value);
			return true;
		}
		if (CastField<FNameProperty>(Property))
		{
			const FString Text = AsImportText(Value).TrimStartAndEnd();
			*static_cast<FName*>(Address) = Text.IsEmpty() || Text == TEXT("None") ? NAME_None : FName(*Text);
			return true;
		}
		if (CastField<FTextProperty>(Property))
		{
			*static_cast<FText*>(Address) = FText::FromString(AsImportText(Value));
			return true;
		}

		// ---- References ----
		if (FSoftClassProperty* SoftClass = CastField<FSoftClassProperty>(Property))
		{
			const FString Text = AsImportText(Value).TrimStartAndEnd();
			if (Text.IsEmpty() || Text == TEXT("None"))
			{
				*static_cast<FSoftObjectPtr*>(Address) = FSoftObjectPtr();
				return true;
			}
			UClass* Class = FindClass(Text, SoftClass->MetaClass, &OutError);
			if (!Class)
			{
				return false;
			}
			*static_cast<FSoftObjectPtr*>(Address) = FSoftObjectPtr(Class);
			return true;
		}
		if (FSoftObjectProperty* SoftObject = CastField<FSoftObjectProperty>(Property))
		{
			const FString Text = AsImportText(Value).TrimStartAndEnd();
			*static_cast<FSoftObjectPtr*>(Address) = Text.IsEmpty() || Text == TEXT("None") ? FSoftObjectPtr() : FSoftObjectPtr(FSoftObjectPath(Text));
			return true;
		}
		if (FClassProperty* ClassProperty = CastField<FClassProperty>(Property))
		{
			const FString Text = AsImportText(Value).TrimStartAndEnd();
			if (Text.IsEmpty() || Text == TEXT("None"))
			{
				ClassProperty->SetObjectPropertyValue(Address, nullptr);
				return true;
			}
			UClass* Class = FindClass(Text, ClassProperty->MetaClass, &OutError);
			if (!Class)
			{
				return false;
			}
			ClassProperty->SetObjectPropertyValue(Address, Class);
			return true;
		}
		if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
		{
			// An instanced object written as {"$class": ..., fields}: made new, or the one there written into.
			if (Value->Type == EJson::Object)
			{
				const TSharedPtr<FJsonObject> Fields = Value->AsObject();
				UObject* Existing = ObjectProperty->GetObjectPropertyValue(Address);
				FString ClassName;
				Fields->TryGetStringField(TEXT("$class"), ClassName);
				UObject* Target = Existing;
				if (!ClassName.IsEmpty() || !Target)
				{
					UClass* Class = ClassName.IsEmpty() ? ObjectProperty->PropertyClass.Get() : FindClass(ClassName, ObjectProperty->PropertyClass, &OutError);
					if (!Class)
					{
						return false;
					}
					if (Class->HasAnyClassFlags(CLASS_Abstract))
					{
						OutError = FString::Printf(TEXT("%s: %s is abstract - name a class with \"$class\"."), *Field, *Class->GetName());
						return false;
					}
					if (!Existing || Existing->GetClass() != Class)
					{
						Target = NewObject<UObject>(Owner ? Owner : GetTransientPackage(), Class, NAME_None, RF_Transactional | (Owner ? Owner->GetMaskedFlags(RF_PropagateToSubObjects) : RF_NoFlags));
						ObjectProperty->SetObjectPropertyValue(Address, Target);
					}
				}
				TSharedPtr<FJsonObject> Rest = MakeShared<FJsonObject>();
				Rest->Values = Fields->Values;
				Rest->RemoveField(TEXT("$class"));
				TArray<FString> Errors;
				Target->Modify();
				WriteObject(Target->GetClass(), Target, Rest, Target, Errors);
				OutError = FString::Join(Errors, TEXT(" "));
				return Errors.Num() == 0;
			}

			const FString Text = AsImportText(Value).TrimStartAndEnd();
			if (Text.IsEmpty() || Text == TEXT("None"))
			{
				ObjectProperty->SetObjectPropertyValue(Address, nullptr);
				return true;
			}
			UObject* Found = StaticLoadObject(ObjectProperty->PropertyClass, nullptr, *Text, nullptr, LOAD_Quiet | LOAD_NoWarn);
			if (!Found && !Text.Contains(TEXT(".")) && Text.StartsWith(TEXT("/")))
			{
				// "/Game/Folder/Asset" without ".Asset".
				const FString Full = Text + TEXT(".") + FPaths::GetBaseFilename(Text);
				Found = StaticLoadObject(ObjectProperty->PropertyClass, nullptr, *Full, nullptr, LOAD_Quiet | LOAD_NoWarn);
			}
			if (!Found || !Found->IsA(ObjectProperty->PropertyClass))
			{
				OutError = FString::Printf(TEXT("%s: no %s at '%s'."), *Field, *ObjectProperty->PropertyClass->GetName(), *Text);
				return false;
			}
			ObjectProperty->SetObjectPropertyValue(Address, Found);
			return true;
		}

		// ---- Structs ----
		if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			UScriptStruct* Struct = StructProperty->Struct;
			const FName StructName = Struct->GetFName();

			if (Struct == TBaseStructure<FLinearColor>::Get() || Struct == TBaseStructure<FColor>::Get() || Struct == FSlateColor::StaticStruct())
			{
				FLinearColor Color;
				if (Value->Type != EJson::Object || !Value->AsObject()->HasField(TEXT("SpecifiedColor")))
				{
					if (!ParseColor(Value, Color))
					{
						OutError = FString::Printf(TEXT("%s takes a color: \"#RRGGBB\", \"#RRGGBBAA\" or [r, g, b, a]."), *Field);
						return false;
					}
					if (Struct == TBaseStructure<FLinearColor>::Get())
					{
						*static_cast<FLinearColor*>(Address) = Color;
					}
					else if (Struct == TBaseStructure<FColor>::Get())
					{
						*static_cast<FColor*>(Address) = Color.ToFColor(true);
					}
					else
					{
						*static_cast<FSlateColor*>(Address) = FSlateColor(Color);
					}
					return true;
				}
			}

			if (Struct == FInstancedStruct::StaticStruct())
			{
				FInstancedStruct* Instanced = static_cast<FInstancedStruct*>(Address);
				if (Value->Type == EJson::Null)
				{
					Instanced->Reset();
					return true;
				}
				TSharedPtr<FJsonObject> Fields = Value->Type == EJson::Object ? Value->AsObject() : nullptr;
				FString TypeName = Value->Type == EJson::String ? Value->AsString() : FString();
				if (Fields.IsValid())
				{
					Fields->TryGetStringField(TEXT("$type"), TypeName);
				}
				if (!TypeName.IsEmpty())
				{
					const FString BasePath = Property->GetMetaData(TEXT("BaseStruct"));
					const UScriptStruct* Base = BasePath.IsEmpty() ? nullptr : FindObject<UScriptStruct>(nullptr, *BasePath);
					const FString Key = Normalize(TypeName);
					const UScriptStruct* Picked = nullptr;
					for (TObjectIterator<UScriptStruct> It; It; ++It)
					{
						if ((!Base || It->IsChildOf(Base)) && (Normalize(It->GetName()) == Key || Normalize(It->GetName()).EndsWith(Key)
							|| Normalize(It->GetDisplayNameText().ToString()) == Key))
						{
							Picked = *It;
							if (Normalize(It->GetName()) == Key)
							{
								break;
							}
						}
					}
					if (!Picked)
					{
						OutError = FString::Printf(TEXT("%s: no struct '%s'%s."), *Field, *TypeName, Base ? *FString::Printf(TEXT(" deriving from %s"), *Base->GetName()) : TEXT(""));
						return false;
					}
					if (Instanced->GetScriptStruct() != Picked)
					{
						Instanced->InitializeAs(Picked);
					}
				}
				if (!Instanced->IsValid())
				{
					OutError = FString::Printf(TEXT("%s: say which struct it holds with \"$type\"."), *Field);
					return false;
				}
				if (Fields.IsValid())
				{
					TSharedPtr<FJsonObject> Rest = MakeShared<FJsonObject>();
				Rest->Values = Fields->Values;
					Rest->RemoveField(TEXT("$type"));
					TArray<FString> Errors;
					WriteObject(Instanced->GetScriptStruct(), Instanced->GetMutableMemory(), Rest, Owner, Errors);
					OutError = FString::Join(Errors, TEXT(" "));
					return Errors.Num() == 0;
				}
				return true;
			}

			if (Struct == FInstancedPropertyBag::StaticStruct())
			{
				FInstancedPropertyBag* Bag = static_cast<FInstancedPropertyBag*>(Address);
				if (Value->Type != EJson::Object || !Bag->GetPropertyBagStruct())
				{
					OutError = FString::Printf(TEXT("%s takes an object of its fields' values%s."), *Field, Bag->GetPropertyBagStruct() ? TEXT("") : TEXT(" (it has none yet)"));
					return false;
				}
				TArray<FString> Errors;
				WriteObject(Bag->GetPropertyBagStruct(), Bag->GetMutableValue().GetMemory(), Value->AsObject(), Owner, Errors);
				OutError = FString::Join(Errors, TEXT(" "));
				return Errors.Num() == 0;
			}

			if (Struct == FMargin::StaticStruct())
			{
				double Uniform = 0.0;
				if (Value->Type == EJson::Number && Value->TryGetNumber(Uniform))
				{
					*static_cast<FMargin*>(Address) = FMargin(static_cast<float>(Uniform));
					return true;
				}
				if (Value->Type == EJson::Array && Value->AsArray().Num() == 2)
				{
					double Horizontal = 0.0, Vertical = 0.0;
					Value->AsArray()[0]->TryGetNumber(Horizontal);
					Value->AsArray()[1]->TryGetNumber(Vertical);
					*static_cast<FMargin*>(Address) = FMargin(static_cast<float>(Horizontal), static_cast<float>(Vertical));
					return true;
				}
			}

			if (Struct == FSlateBrush::StaticStruct() && Value->Type == EJson::String)
			{
				// A brush from a texture or material path.
				const FString Text = Value->AsString().TrimStartAndEnd();
				FSlateBrush* Brush = static_cast<FSlateBrush*>(Address);
				if (Text.IsEmpty() || Text == TEXT("None"))
				{
					Brush->SetResourceObject(nullptr);
					return true;
				}
				UObject* Resource = StaticLoadObject(UObject::StaticClass(), nullptr, *Text, nullptr, LOAD_Quiet | LOAD_NoWarn);
				if (!Resource)
				{
					OutError = FString::Printf(TEXT("%s: no texture or material at '%s'."), *Field, *Text);
					return false;
				}
				Brush->SetResourceObject(Resource);
				return true;
			}

			// [x, y], [r, g, b, a]... into structs of numbers; a single number for all of them.
			if (StructIsAllNumbers(Struct))
			{
				if (Value->Type == EJson::Array)
				{
					return WriteNumbersInOrder(Struct, Address, Value->AsArray(), OutError);
				}
				double Uniform = 0.0;
				if (Value->Type == EJson::Number && Value->TryGetNumber(Uniform))
				{
					TArray<TSharedPtr<FJsonValue>> Repeated;
					for (int32 Index = 0; Index < CountNumericFields(Struct); ++Index)
					{
						Repeated.Add(MakeShared<FJsonValueNumber>(Uniform));
					}
					return WriteNumbersInOrder(Struct, Address, Repeated, OutError);
				}
			}

			if (Value->Type == EJson::Object)
			{
				TArray<FString> Errors;
				WriteObject(Struct, Address, Value->AsObject(), Owner, Errors);
				OutError = FString::Join(Errors, TEXT(" "));
				return Errors.Num() == 0;
			}
			// Fall through to Unreal's text format: "(X=1,Y=2)".
			(void)StructName;
		}

		// ---- Lists and maps ----
		if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			if (Value->Type == EJson::Array)
			{
				const TArray<TSharedPtr<FJsonValue>>& Items = Value->AsArray();
				FScriptArrayHelper Helper(ArrayProperty, Address);
				Helper.EmptyValues();
				Helper.AddValues(Items.Num());
				TArray<FString> Errors;
				for (int32 Index = 0; Index < Items.Num(); ++Index)
				{
					FString ItemError;
					if (!WriteValue(ArrayProperty->Inner, Helper.GetRawPtr(Index), Items[Index], Owner, ItemError))
					{
						Errors.Add(FString::Printf(TEXT("%s[%d]: %s"), *Field, Index, *ItemError));
					}
				}
				OutError = FString::Join(Errors, TEXT(" "));
				return Errors.Num() == 0;
			}
		}
		if (FMapProperty* MapProperty = CastField<FMapProperty>(Property); MapProperty && Value->Type == EJson::Object)
		{
			FScriptMapHelper Helper(MapProperty, Address);
			Helper.EmptyValues();
			TArray<FString> Errors;
			for (const auto& Pair : Value->AsObject()->Values)
			{
				const int32 Index = Helper.AddDefaultValue_Invalid_NeedsRehash();
				FString KeyError;
				if (!WriteValue(MapProperty->KeyProp, Helper.GetKeyPtr(Index), MakeShared<FJsonValueString>(Pair.Key), Owner, KeyError))
				{
					Errors.Add(KeyError);
				}
				FString ValueError;
				if (!WriteValue(MapProperty->ValueProp, Helper.GetValuePtr(Index), Pair.Value, Owner, ValueError))
				{
					Errors.Add(ValueError);
				}
			}
			Helper.Rehash();
			OutError = FString::Join(Errors, TEXT(" "));
			return Errors.Num() == 0;
		}

		// ---- Anything else: Unreal's own text format ----
		const FString Text = AsImportText(Value);
		FErrorCollector Errors;
		const TCHAR* End = Property->ImportText_Direct(*Text, Address, Owner, PPF_None, &Errors);
		if (!End || !Errors.Text.IsEmpty())
		{
			OutError = FString::Printf(TEXT("%s: couldn't read %s%s%s"), *Field, *DescribeJson(Value), Errors.Text.IsEmpty() ? TEXT(".") : TEXT(": "), *Errors.Text);
			return false;
		}
		return true;
	}

	namespace
	{
		/** Writes one key (a path) of an object into Container. */
		bool WriteKey(const UStruct* Struct, void* Container, const FString& Key, const TSharedPtr<FJsonValue>& Value, UObject* Owner,
			FString& OutError, FProperty** OutTopField)
		{
			TArray<FString> Segments;
			SplitPath(Key, Segments);
			if (Segments.Num() == 0)
			{
				OutError = TEXT("Empty key.");
				return false;
			}

			const UStruct* CurrentStruct = Struct;
			void* CurrentContainer = Container;
			UObject* CurrentOwner = Owner;

			for (int32 SegmentIndex = 0; SegmentIndex < Segments.Num(); ++SegmentIndex)
			{
				FSegment Segment;
				if (!ParseSegment(Segments[SegmentIndex], Segment, OutError))
				{
					return false;
				}
				FProperty* Property = FindField(CurrentStruct, Segment.Name, &OutError);
				if (!Property)
				{
					return false;
				}
				if (SegmentIndex == 0 && OutTopField)
				{
					*OutTopField = Property;
				}

				void* Address = Property->ContainerPtrToValuePtr<void>(CurrentContainer);
				FProperty* ValueProperty = Property;
				const bool bLast = SegmentIndex == Segments.Num() - 1;

				if (Segment.bAppend)
				{
					FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property);
					if (!ArrayProperty || !bLast)
					{
						OutError = FString::Printf(TEXT("'%s+' appends to a list: %s isn't one."), *Segment.Name, *Property->GetAuthoredName());
						return false;
					}
					FScriptArrayHelper Helper(ArrayProperty, Address);
					// A list of items appends each; a single item appends one.
					const bool bMany = Value->Type == EJson::Array && !CastField<FArrayProperty>(ArrayProperty->Inner)
						&& !(CastField<FStructProperty>(ArrayProperty->Inner) && StructIsAllNumbers(CastField<FStructProperty>(ArrayProperty->Inner)->Struct)
							&& Value->AsArray().Num() > 0 && Value->AsArray()[0]->Type == EJson::Number);
					TArray<TSharedPtr<FJsonValue>> Items = bMany ? Value->AsArray() : TArray<TSharedPtr<FJsonValue>>{ Value };
					TArray<FString> Errors;
					for (const TSharedPtr<FJsonValue>& Item : Items)
					{
						const int32 NewIndex = Helper.AddValue();
						FString ItemError;
						if (!WriteValue(ArrayProperty->Inner, Helper.GetRawPtr(NewIndex), Item, CurrentOwner, ItemError))
						{
							Errors.Add(ItemError);
						}
					}
					OutError = FString::Join(Errors, TEXT(" "));
					return Errors.Num() == 0;
				}

				if (Segment.Index != INDEX_NONE)
				{
					FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property);
					if (!ArrayProperty)
					{
						OutError = FString::Printf(TEXT("'%s' isn't a list."), *Segment.Name);
						return false;
					}
					FScriptArrayHelper Helper(ArrayProperty, Address);
					if (Segment.Index == Helper.Num())
					{
						Helper.AddValue();
					}
					if (!Helper.IsValidIndex(Segment.Index))
					{
						OutError = FString::Printf(TEXT("'%s' has %d entries: there is no [%d]."), *Segment.Name, Helper.Num(), Segment.Index);
						return false;
					}
					Address = Helper.GetRawPtr(Segment.Index);
					ValueProperty = ArrayProperty->Inner;
				}

				if (bLast)
				{
					return WriteValue(ValueProperty, Address, Value, CurrentOwner, OutError);
				}

				if (!StepInto(ValueProperty, Address, CurrentStruct, CurrentContainer, CurrentOwner, OutError))
				{
					return false;
				}
			}
			return false;
		}
	}

	void WriteObject(const UStruct* Struct, void* Container, const TSharedPtr<FJsonObject>& Object, UObject* Owner,
		TArray<FString>& OutErrors, TArray<FProperty*>* OutTopFields)
	{
		if (!Struct || !Container || !Object.IsValid())
		{
			return;
		}
		for (const auto& Pair : Object->Values)
		{
			if (Pair.Key.StartsWith(TEXT("$")))
			{
				continue;
			}
			FString Error;
			FProperty* Top = nullptr;
			if (!WriteKey(Struct, Container, Pair.Key, Pair.Value, Owner, Error, &Top))
			{
				OutErrors.Add(Error.IsEmpty() ? FString::Printf(TEXT("%s didn't take."), *Pair.Key) : Error);
			}
			if (Top && OutTopFields)
			{
				OutTopFields->AddUnique(Top);
			}
		}
	}

	bool ApplyToObject(UObject* Object, const TSharedPtr<FJsonObject>& Props, TArray<FString>& OutErrors)
	{
		if (!Object || !Props.IsValid() || Props->Values.Num() == 0)
		{
			return true;
		}
		const int32 ErrorsBefore = OutErrors.Num();
		Object->Modify();

		// Each top-level field: PreEditChange, written, PostEditChangeProperty - as one edit in the Details panel. Assets and
		// classes go first: what they pick can lay other fields out (a Niagara System lays out User Parameters).
		struct FKey
		{
			const FString* Key = nullptr;
			const TSharedPtr<FJsonValue>* Value = nullptr;
			FProperty* Top = nullptr;
		};
		TArray<FKey> Keys;
		for (const auto& Pair : Props->Values)
		{
			if (Pair.Key.StartsWith(TEXT("$")))
			{
				continue;
			}
			TArray<FString> Segments;
			SplitPath(Pair.Key, Segments);
			FSegment First;
			FString Error;
			FProperty* Top = Segments.Num() > 0 && ParseSegment(Segments[0], First, Error) ? FindField(Object->GetClass(), First.Name, &Error) : nullptr;
			if (!Top)
			{
				OutErrors.Add(Error.IsEmpty() ? FString::Printf(TEXT("No field '%s' on %s."), *Pair.Key, *Object->GetClass()->GetName()) : Error);
				continue;
			}
			Keys.Add({ &Pair.Key, &Pair.Value, Top });
		}
		Keys.StableSort([](const FKey& A, const FKey& B)
		{
			const bool bAssetA = CastField<FObjectPropertyBase>(A.Top) != nullptr && !A.Top->HasAnyPropertyFlags(CPF_InstancedReference);
			const bool bAssetB = CastField<FObjectPropertyBase>(B.Top) != nullptr && !B.Top->HasAnyPropertyFlags(CPF_InstancedReference);
			return bAssetA && !bAssetB;
		});

		for (const FKey& Each : Keys)
		{
			FProperty* Top = Each.Top;
			Object->PreEditChange(Top);
			TSharedPtr<FJsonObject> One = MakeShared<FJsonObject>();
			One->SetField(*Each.Key, *Each.Value);
			WriteObject(Object->GetClass(), Object, One, Object, OutErrors);
			FPropertyChangedEvent Event(Top, EPropertyChangeType::ValueSet);
			Object->PostEditChangeProperty(Event);
		}
		return OutErrors.Num() == ErrorsBefore;
	}

	// =================================================================================================================
	// Reading
	// =================================================================================================================

	namespace
	{
		struct FStructDefaults
		{
			explicit FStructDefaults(const UScriptStruct* InStruct)
				: Struct(InStruct)
			{
				Memory.SetNumZeroed(FMath::Max(Struct->GetStructureSize(), 1));
				Struct->InitializeStruct(Memory.GetData());
			}
			~FStructDefaults()
			{
				Struct->DestroyStruct(Memory.GetData());
			}
			const UScriptStruct* Struct;
			TArray<uint8> Memory;
		};

		bool ShouldList(const FProperty* Property, bool bEditableOnly)
		{
			if (Property->HasAnyPropertyFlags(CPF_Deprecated | CPF_Transient))
			{
				return false;
			}
			return !bEditableOnly || Property->HasAnyPropertyFlags(CPF_Edit);
		}
	}

	TSharedPtr<FJsonValue> ReadValue(const FProperty* Property, const void* Address, int32 Depth)
	{
		if (!Property || !Address)
		{
			return MakeShared<FJsonValueNull>();
		}
		if (const FBoolProperty* Bool = CastField<FBoolProperty>(Property))
		{
			return MakeShared<FJsonValueBoolean>(Bool->GetPropertyValue(Address));
		}
		if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			const int64 Value = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(Address);
			return MakeShared<FJsonValueString>(EnumProperty->GetEnum()->GetNameStringByValue(Value));
		}
		if (const FByteProperty* Byte = CastField<FByteProperty>(Property); Byte && Byte->Enum)
		{
			return MakeShared<FJsonValueString>(Byte->Enum->GetNameStringByValue(Byte->GetPropertyValue(Address)));
		}
		if (const FNumericProperty* Numeric = CastField<FNumericProperty>(Property))
		{
			const double Value = Numeric->IsFloatingPoint() ? Numeric->GetFloatingPointPropertyValue(Address) : static_cast<double>(Numeric->GetSignedIntPropertyValue(Address));
			return MakeShared<FJsonValueNumber>(FMath::RoundToDouble(Value * 1000.0) / 1000.0);
		}
		if (CastField<FStrProperty>(Property))
		{
			const FString& Text = *static_cast<const FString*>(Address);
			return MakeShared<FJsonValueString>(Text.Len() > 300 ? Text.Left(300) + TEXT("...") : Text);
		}
		if (CastField<FNameProperty>(Property))
		{
			return MakeShared<FJsonValueString>(static_cast<const FName*>(Address)->ToString());
		}
		if (CastField<FTextProperty>(Property))
		{
			const FString Text = static_cast<const FText*>(Address)->ToString();
			return MakeShared<FJsonValueString>(Text.Len() > 300 ? Text.Left(300) + TEXT("...") : Text);
		}
		if (CastField<FSoftObjectProperty>(Property) || CastField<FSoftClassProperty>(Property))
		{
			const FSoftObjectPtr& Soft = *static_cast<const FSoftObjectPtr*>(Address);
			return Soft.IsNull() ? StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueNull>()) : StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueString>(Soft.ToString()));
		}
		if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
		{
			const UObject* Object = ObjectProperty->GetObjectPropertyValue(Address);
			if (!Object)
			{
				return MakeShared<FJsonValueNull>();
			}
			if (const UClass* Class = Cast<UClass>(Object))
			{
				return MakeShared<FJsonValueString>(Class->ClassGeneratedBy ? Class->GetPathName() : Class->GetName());
			}
			if (Property->HasAnyPropertyFlags(CPF_InstancedReference | CPF_PersistentInstance) && Depth > 0)
			{
				TSharedPtr<FJsonObject> Inner = ReadObjectNonDefault(Object, Depth - 1);
				Inner->SetStringField(TEXT("$class"), Object->GetClass()->GetName());
				return MakeShared<FJsonValueObject>(Inner);
			}
			return MakeShared<FJsonValueString>(Object->IsAsset() || Object->GetOuter() == Object->GetOutermost() ? Object->GetPathName() : Object->GetName());
		}
		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			const UScriptStruct* Struct = StructProperty->Struct;
			if (Struct == TBaseStructure<FLinearColor>::Get())
			{
				const FLinearColor& Color = *static_cast<const FLinearColor*>(Address);
				const bool bInRange = Color.R <= 1.f && Color.G <= 1.f && Color.B <= 1.f && Color.R >= 0.f && Color.G >= 0.f && Color.B >= 0.f;
				if (bInRange)
				{
					return MakeShared<FJsonValueString>(ColorToHex(Color));
				}
				TArray<TSharedPtr<FJsonValue>> Channels = { Num(Color.R), Num(Color.G), Num(Color.B), Num(Color.A) };
				return MakeShared<FJsonValueArray>(Channels);
			}
			if (Struct == TBaseStructure<FColor>::Get())
			{
				return MakeShared<FJsonValueString>(ColorToHex(FLinearColor(*static_cast<const FColor*>(Address))));
			}
			if (Struct == FSlateColor::StaticStruct())
			{
				const FSlateColor& Color = *static_cast<const FSlateColor*>(Address);
				return MakeShared<FJsonValueString>(Color.IsColorSpecified() ? ColorToHex(Color.GetSpecifiedColor()) : FString(TEXT("(foreground)")));
			}
			if (Struct == FInstancedStruct::StaticStruct())
			{
				const FInstancedStruct& Instanced = *static_cast<const FInstancedStruct*>(Address);
				if (!Instanced.IsValid())
				{
					return MakeShared<FJsonValueNull>();
				}
				TSharedPtr<FJsonObject> Inner = Depth > 0 ? ReadNonDefault(Instanced.GetScriptStruct(), Instanced.GetMemory(), nullptr, Depth - 1) : MakeShared<FJsonObject>();
				Inner->SetStringField(TEXT("$type"), Instanced.GetScriptStruct()->GetName());
				return MakeShared<FJsonValueObject>(Inner);
			}
			if (Struct == FInstancedPropertyBag::StaticStruct())
			{
				const FInstancedPropertyBag& Bag = *static_cast<const FInstancedPropertyBag*>(Address);
				TSharedPtr<FJsonObject> Inner = MakeShared<FJsonObject>();
				if (const UPropertyBag* BagStruct = Bag.GetPropertyBagStruct())
				{
					const uint8* Memory = Bag.GetValue().GetMemory();
					for (TFieldIterator<FProperty> It(BagStruct); It; ++It)
					{
						Inner->SetField(It->GetAuthoredName(), ReadValue(*It, It->ContainerPtrToValuePtr<void>(Memory), Depth - 1));
					}
				}
				return MakeShared<FJsonValueObject>(Inner);
			}
			if (StructIsAllNumbers(Struct) && CountNumericFields(Struct) <= 4)
			{
				TArray<TSharedPtr<FJsonValue>> Numbers;
				for (TFieldIterator<FProperty> It(Struct); It; ++It)
				{
					Numbers.Add(ReadValue(*It, It->ContainerPtrToValuePtr<void>(Address), 0));
				}
				return MakeShared<FJsonValueArray>(Numbers);
			}
			if (Depth <= 0)
			{
				FString Text;
				Property->ExportText_Direct(Text, Address, Address, nullptr, PPF_None);
				return MakeShared<FJsonValueString>(Text.Len() > 200 ? Text.Left(200) + TEXT("...") : Text);
			}
			return MakeShared<FJsonValueObject>(ReadNonDefault(Struct, Address, nullptr, Depth - 1));
		}
		if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			FScriptArrayHelper Helper(ArrayProperty, Address);
			TArray<TSharedPtr<FJsonValue>> Items;
			const int32 Shown = FMath::Min(Helper.Num(), 24);
			for (int32 Index = 0; Index < Shown; ++Index)
			{
				Items.Add(ReadValue(ArrayProperty->Inner, Helper.GetRawPtr(Index), Depth - 1));
			}
			if (Helper.Num() > Shown)
			{
				Items.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("... %d more"), Helper.Num() - Shown)));
			}
			return MakeShared<FJsonValueArray>(Items);
		}
		if (const FMapProperty* MapProperty = CastField<FMapProperty>(Property))
		{
			FScriptMapHelper Helper(MapProperty, Address);
			TSharedPtr<FJsonObject> Inner = MakeShared<FJsonObject>();
			for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index)
			{
				if (!Helper.IsValidIndex(Index))
				{
					continue;
				}
				FString Key;
				MapProperty->KeyProp->ExportText_Direct(Key, Helper.GetKeyPtr(Index), nullptr, nullptr, PPF_None);
				Inner->SetField(Key, ReadValue(MapProperty->ValueProp, Helper.GetValuePtr(Index), Depth - 1));
			}
			return MakeShared<FJsonValueObject>(Inner);
		}

		FString Text;
		Property->ExportText_Direct(Text, Address, Address, nullptr, PPF_None);
		return MakeShared<FJsonValueString>(Text.Len() > 200 ? Text.Left(200) + TEXT("...") : Text);
	}

	TSharedPtr<FJsonObject> ReadNonDefault(const UStruct* Struct, const void* Container, const void* Defaults, int32 Depth, int32 MaxFields)
	{
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		if (!Struct || !Container)
		{
			return Out;
		}

		TUniquePtr<FStructDefaults> OwnDefaults;
		if (!Defaults)
		{
			if (const UScriptStruct* ScriptStruct = Cast<UScriptStruct>(Struct))
			{
				OwnDefaults = MakeUnique<FStructDefaults>(ScriptStruct);
				Defaults = OwnDefaults->Memory.GetData();
			}
		}

		// Struct fields with no Edit flag (plain UPROPERTY()) are often the point of a struct (tracks' keys); a class's aren't.
		const bool bEditableOnly = Struct->IsA<UClass>();
		int32 Count = 0;
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			const FProperty* Property = *It;
			if (!ShouldList(Property, bEditableOnly))
			{
				continue;
			}
			if (Defaults && Property->Identical_InContainer(Container, Defaults, 0, PPF_None))
			{
				continue;
			}
			if (++Count > MaxFields)
			{
				Out->SetStringField(TEXT("$more"), TEXT("more fields differ - ask for one by path"));
				break;
			}
			Out->SetField(Property->GetAuthoredName(), ReadValue(Property, Property->ContainerPtrToValuePtr<void>(Container), Depth));
		}
		return Out;
	}

	TSharedPtr<FJsonObject> ReadObjectNonDefault(const UObject* Object, int32 Depth)
	{
		if (!Object)
		{
			return MakeShared<FJsonObject>();
		}
		const UObject* Defaults = Object->GetClass()->GetDefaultObject(false);
		return ReadNonDefault(Object->GetClass(), Object, Defaults, Depth, 60);
	}

	TSharedPtr<FJsonValue> ReadPath(const UStruct* Struct, const void* Container, const FString& Path, FString& OutError)
	{
		TArray<FString> Segments;
		SplitPath(Path, Segments);
		const UStruct* CurrentStruct = Struct;
		void* CurrentContainer = const_cast<void*>(Container);
		UObject* Owner = nullptr;
		for (int32 SegmentIndex = 0; SegmentIndex < Segments.Num(); ++SegmentIndex)
		{
			FSegment Segment;
			if (!ParseSegment(Segments[SegmentIndex], Segment, OutError))
			{
				return nullptr;
			}
			FProperty* Property = FindField(CurrentStruct, Segment.Name, &OutError);
			if (!Property)
			{
				return nullptr;
			}
			void* Address = Property->ContainerPtrToValuePtr<void>(CurrentContainer);
			FProperty* ValueProperty = Property;
			if (Segment.Index != INDEX_NONE)
			{
				FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property);
				if (!ArrayProperty)
				{
					OutError = FString::Printf(TEXT("'%s' isn't a list."), *Segment.Name);
					return nullptr;
				}
				FScriptArrayHelper Helper(ArrayProperty, Address);
				if (!Helper.IsValidIndex(Segment.Index))
				{
					OutError = FString::Printf(TEXT("'%s' has %d entries."), *Segment.Name, Helper.Num());
					return nullptr;
				}
				Address = Helper.GetRawPtr(Segment.Index);
				ValueProperty = ArrayProperty->Inner;
			}
			if (SegmentIndex == Segments.Num() - 1)
			{
				return ReadValue(ValueProperty, Address, 4);
			}
			// Reading never modifies: step without Modify.
			if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(ValueProperty))
			{
				UObject* Inner = ObjectProperty->GetObjectPropertyValue(Address);
				if (!Inner)
				{
					OutError = FString::Printf(TEXT("'%s' is None."), *Segment.Name);
					return nullptr;
				}
				CurrentStruct = Inner->GetClass();
				CurrentContainer = Inner;
				continue;
			}
			if (!StepInto(ValueProperty, Address, CurrentStruct, CurrentContainer, Owner, OutError))
			{
				return nullptr;
			}
		}
		OutError = TEXT("No path given.");
		return nullptr;
	}

	// =================================================================================================================
	// JSON text and arguments
	// =================================================================================================================

	FString ToString(const TSharedPtr<FJsonObject>& Object, bool bPretty)
	{
		FString Out;
		if (!Object.IsValid())
		{
			return TEXT("{}");
		}
		if (bPretty)
		{
			const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
			FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
		}
		else
		{
			const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
			FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
		}
		return Out;
	}

	FString ToString(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid())
		{
			return TEXT("null");
		}
		FString Out;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Value, FString(), Writer);
		return Out;
	}

	TSharedPtr<FJsonObject> ParseObject(const FString& Text)
	{
		TSharedPtr<FJsonObject> Object;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Object))
		{
			return nullptr;
		}
		return Object;
	}

	TSharedPtr<FJsonValue> ParseValue(const FString& Text)
	{
		TSharedPtr<FJsonValue> Value;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Value))
		{
			return nullptr;
		}
		return Value;
	}

	FString GetString(const TSharedPtr<FJsonObject>& Args, const FString& Key, const FString& Default)
	{
		FString Value;
		if (Args.IsValid() && Args->TryGetStringField(Key, Value))
		{
			return Value;
		}
		double Number = 0.0;
		if (Args.IsValid() && Args->TryGetNumberField(Key, Number))
		{
			return FString::SanitizeFloat(Number);
		}
		return Default;
	}

	double GetNumber(const TSharedPtr<FJsonObject>& Args, const FString& Key, double Default)
	{
		double Value = Default;
		if (Args.IsValid() && !Args->TryGetNumberField(Key, Value))
		{
			FString Text;
			if (Args->TryGetStringField(Key, Text) && LexTryParseString(Value, *Text))
			{
				return Value;
			}
			return Default;
		}
		return Value;
	}

	bool GetBool(const TSharedPtr<FJsonObject>& Args, const FString& Key, bool bDefault)
	{
		bool bValue = bDefault;
		if (Args.IsValid() && Args->TryGetBoolField(Key, bValue))
		{
			return bValue;
		}
		return bDefault;
	}

	TSharedPtr<FJsonObject> GetObject(const TSharedPtr<FJsonObject>& Args, const FString& Key)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		if (Args.IsValid() && Args->TryGetObjectField(Key, Object) && Object)
		{
			return *Object;
		}
		return nullptr;
	}

	TArray<TSharedPtr<FJsonValue>> GetArray(const TSharedPtr<FJsonObject>& Args, const FString& Key)
	{
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (Args.IsValid() && Args->TryGetArrayField(Key, Array) && Array)
		{
			return *Array;
		}
		return {};
	}

	TArray<FString> GetStringArray(const TSharedPtr<FJsonObject>& Args, const FString& Key)
	{
		TArray<FString> Out;
		for (const TSharedPtr<FJsonValue>& Value : GetArray(Args, Key))
		{
			FString Text;
			if (Value.IsValid() && Value->TryGetString(Text))
			{
				Out.Add(Text);
			}
		}
		if (Out.Num() == 0)
		{
			const FString Single = GetString(Args, Key);
			if (!Single.IsEmpty())
			{
				Out.Add(Single);
			}
		}
		return Out;
	}

	bool Has(const TSharedPtr<FJsonObject>& Args, const FString& Key)
	{
		const TSharedPtr<FJsonValue> Value = Args.IsValid() ? Args->TryGetField(Key) : nullptr;
		return Value.IsValid() && !Value->IsNull();
	}

	TSharedPtr<FJsonValue> Str(const FString& Value) { return MakeShared<FJsonValueString>(Value); }
	TSharedPtr<FJsonValue> Num(double Value) { return MakeShared<FJsonValueNumber>(Value); }
	TSharedPtr<FJsonValue> Bool(bool bValue) { return MakeShared<FJsonValueBoolean>(bValue); }
	TSharedPtr<FJsonValue> Obj(const TSharedPtr<FJsonObject>& Object) { return MakeShared<FJsonValueObject>(Object); }

	TArray<TSharedPtr<FJsonValue>> ListOf(const TSharedPtr<FJsonObject>& Object)
	{
		TArray<TSharedPtr<FJsonValue>> List;
		List.Add(MakeShared<FJsonValueObject>(Object));
		return List;
	}
	TSharedPtr<FJsonValue> Arr(const TArray<FString>& Values)
	{
		TArray<TSharedPtr<FJsonValue>> Items;
		for (const FString& Value : Values)
		{
			Items.Add(MakeShared<FJsonValueString>(Value));
		}
		return MakeShared<FJsonValueArray>(Items);
	}
}
