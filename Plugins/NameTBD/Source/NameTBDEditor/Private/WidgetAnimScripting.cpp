// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimScripting.h"

#include "SWidgetAnimTimeline.h"
#include "WidgetAnimEditorSettings.h"
#include "WidgetAnimPanelSource.h"

#include "NameTBD/WidgetAnimation/WidgetAnimBuilder.h"
#include "NameTBD/WidgetAnimation/WidgetAnimEvaluator.h"
#include "NameTBD/WidgetAnimation/WidgetAnimExpander.h"
#include "NameTBD/WidgetAnimation/WidgetAnimPreset.h"
#include "NameTBD/WidgetAnimation/WidgetAnimTransform.h"
#include "NameTBD/WidgetAnimation/Evaluators/WidgetAnimBlueprintCue.h"
#include "NameTBD/WidgetAnimation/Evaluators/WidgetAnimBlueprintType.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "CoreGlobals.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelSlot.h"
#include "Components/PanelWidget.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/Kismet2NameValidators.h"
#include "Misc/FileHelper.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectIterator.h"
#include "WidgetBlueprint.h"

#define LOCTEXT_NAMESPACE "WidgetAnimScripting"

// =====================================================================================================
// Names, matched loosely
//
// Scripts are typed by hand: "ease_out", "Ease Out" and "EaseOut" all mean the same easing, "duration" the Duration
// field and "invert_track" bInvertTrack. Everything a script names - fields, enum values, structs, track types - is
// compared with case, spaces, underscores and punctuation ignored.
// =====================================================================================================

namespace WidgetAnimScriptNames
{
	FString Normalize(const FString& In)
	{
		FString Out;
		Out.Reserve(In.Len());
		for (const TCHAR Char : In)
		{
			if (FChar::IsAlnum(Char))
			{
				Out.AppendChar(FChar::ToLower(Char));
			}
		}
		return Out;
	}

	/** A field of Struct by its name, a bool's name without its b, its Blueprint-authored name, or its display name. */
	FProperty* FindProperty(const UStruct* Struct, const FString& Name)
	{
		if (!Struct || Name.IsEmpty())
		{
			return nullptr;
		}

		const FName Exact(*Name, FNAME_Find);
		if (!Exact.IsNone())
		{
			if (FProperty* Found = FindFProperty<FProperty>(Struct, Exact))
			{
				return Found;
			}
		}

		const FString Wanted = Normalize(Name);
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			FProperty* Property = *It;

			// The authored name: a Blueprint struct's fields are stored as "Speed_4_8A3F...", written as "Speed".
			const FString Plain = Normalize(Property->GetAuthoredName());
			if (Plain == Wanted)
			{
				return Property;
			}
			if (CastField<FBoolProperty>(Property) && Plain.Len() > 1 && Plain[0] == TEXT('b') && Plain.Mid(1) == Wanted)
			{
				return Property;
			}
			if (Property->HasMetaData(TEXT("DisplayName")) && Normalize(Property->GetMetaData(TEXT("DisplayName"))) == Wanted)
			{
				return Property;
			}
		}
		return nullptr;
	}

	/** The enum's value whose name or display name is Name, or INDEX_NONE. */
	int64 FindEnumValue(const UEnum* Enum, const FString& Name)
	{
		if (!Enum)
		{
			return INDEX_NONE;
		}

		// The text may be the full "EWidgetAnimEasing::EaseOut", as Unreal writes it out.
		FString Short = Name;
		int32 Colon = INDEX_NONE;
		if (Short.FindLastChar(TEXT(':'), Colon))
		{
			Short = Short.Mid(Colon + 1);
		}
		const FString Wanted = Normalize(Short);

		const int32 Num = Enum->NumEnums() - (Enum->ContainsExistingMax() ? 1 : 0);
		for (int32 Index = 0; Index < Num; ++Index)
		{
			if (Normalize(Enum->GetNameStringByIndex(Index)) == Wanted
				|| Normalize(Enum->GetDisplayNameTextByIndex(Index).ToString()) == Wanted)
			{
				return Enum->GetValueByIndex(Index);
			}
		}
		return INDEX_NONE;
	}

	/** The enum's values, by name, for saying what a value could have been. */
	FString ListEnumNames(const UEnum* Enum)
	{
		TArray<FString> Names;
		const int32 Num = Enum ? Enum->NumEnums() - (Enum->ContainsExistingMax() ? 1 : 0) : 0;
		for (int32 Index = 0; Index < Num; ++Index)
		{
			if (!Enum->HasMetaData(TEXT("Hidden"), Index))
			{
				Names.Add(Enum->GetNameStringByIndex(Index));
			}
		}
		return FString::Join(Names, TEXT(", "));
	}

	/**
	 * A struct's short name, as a script writes it: past the family prefix - "EachChild" for WidgetAnimExpander_EachChild,
	 * "PointerPosition" for WidgetAnimInput_PointerPosition.
	 */
	FString ShortStructName(const UScriptStruct* Struct)
	{
		if (!Struct)
		{
			return FString();
		}
		const FString Name = Struct->GetName();
		int32 Underscore = INDEX_NONE;
		return Name.FindChar(TEXT('_'), Underscore) ? Name.Mid(Underscore + 1) : Name;
	}

	bool StructMatches(const UScriptStruct* Struct, const FString& Wanted)
	{
		const FString Key = Normalize(Wanted);
		return Normalize(Struct->GetName()) == Key
			|| Normalize(ShortStructName(Struct)) == Key
			|| Normalize(Struct->GetDisplayNameText().ToString()) == Key;
	}
}

// =====================================================================================================
// Fields of a track, by path
// =====================================================================================================

namespace WidgetAnimScriptFields
{
	using namespace WidgetAnimScriptNames;

	/** Where a path led: the field, its value's memory, and the track's own field it started from. */
	struct FResolved
	{
		FProperty* Property = nullptr;
		void* Address = nullptr;

		/** The first field of the path, when it is one of the track's own - not a registered type's setting. */
		FName TrackField;

		/** The first field of the path, whatever it is on - what a widget or slot is told changed. */
		FProperty* TopProperty = nullptr;
	};

	/** Gathers the errors ImportText writes, to say why a value didn't take. */
	class FErrorCollector : public FOutputDevice
	{
	public:
		FString Text;

		virtual void Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			if (!Text.IsEmpty())
			{
				Text += TEXT(" ");
			}
			Text += Message;
		}
	};

	bool IsInstancedStruct(const FProperty* Property)
	{
		const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
		return StructProperty && StructProperty->Struct == FInstancedStruct::StaticStruct();
	}

	/**
	 * Follows Path - "Spring.Bounce", "ValueKeys.Keys[0].Value", "Expander.Interval" - from Layer. A first name the track
	 * doesn't have is looked for among a registered type's settings, so count.get("EndValue") works.
	 */
	/**
	 * Follows Path from Container, a RootStruct - a track, a widget, a slot. Fallback, when given, is where a first name the
	 * root doesn't have is looked for: a track's registered type settings.
	 */
	bool ResolveIn(const UStruct* RootStruct, void* RootContainer, const FString& RootWhere, FInstancedStruct* Fallback, const FString& Path, FResolved& Out, FString& OutError)
	{
		TArray<FString> Segments;
		Path.ParseIntoArray(Segments, TEXT("."), /*bCullEmpty=*/true);
		if (Segments.Num() == 0)
		{
			OutError = TEXT("No field was named.");
			return false;
		}

		const UStruct* Struct = RootStruct;
		void* Container = RootContainer;
		FString Where = RootWhere;

		for (int32 SegmentIndex = 0; SegmentIndex < Segments.Num(); ++SegmentIndex)
		{
			FString Segment = Segments[SegmentIndex].TrimStartAndEnd();

			// "Keys[2]": one entry of a list.
			int32 ArrayIndex = INDEX_NONE;
			int32 Bracket = INDEX_NONE;
			if (Segment.FindChar(TEXT('['), Bracket))
			{
				const FString IndexText = Segment.Mid(Bracket + 1).Replace(TEXT("]"), TEXT("")).TrimStartAndEnd();
				if (!IndexText.IsNumeric())
				{
					OutError = FString::Printf(TEXT("'%s' isn't a list entry: write Name[0]."), *Segments[SegmentIndex]);
					return false;
				}
				ArrayIndex = FCString::Atoi(*IndexText);
				Segment = Segment.Left(Bracket).TrimStartAndEnd();
			}

			FProperty* Property = FindProperty(Struct, Segment);
			bool bOnTrack = SegmentIndex == 0 && Property != nullptr;
			if (!Property && SegmentIndex == 0 && Fallback && Fallback->IsValid())
			{
				Struct = Fallback->GetScriptStruct();
				Container = Fallback->GetMutableMemory();
				Property = FindProperty(Struct, Segment);
				Where = Fallback->GetScriptStruct()->GetDisplayNameText().ToString();
			}
			if (!Property)
			{
				OutError = FString::Printf(TEXT("No field called '%s' on %s."), *Segment, *Where);
				return false;
			}
			if (bOnTrack)
			{
				Out.TrackField = Property->GetFName();
			}
			if (SegmentIndex == 0)
			{
				Out.TopProperty = Property;
			}

			void* Value = Property->ContainerPtrToValuePtr<void>(Container);
			if (ArrayIndex != INDEX_NONE)
			{
				FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property);
				if (!ArrayProperty)
				{
					OutError = FString::Printf(TEXT("'%s' isn't a list."), *Segment);
					return false;
				}
				FScriptArrayHelper Helper(ArrayProperty, Value);
				if (!Helper.IsValidIndex(ArrayIndex))
				{
					OutError = FString::Printf(TEXT("'%s' has %d entries: there is no [%d]."), *Segment, Helper.Num(), ArrayIndex);
					return false;
				}
				Value = Helper.GetRawPtr(ArrayIndex);
				Property = ArrayProperty->Inner;
			}

			if (SegmentIndex == Segments.Num() - 1)
			{
				Out.Property = Property;
				Out.Address = Value;
				return true;
			}

			// Further in: a struct's fields, or what an instanced struct holds.
			const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
			if (!StructProperty)
			{
				OutError = FString::Printf(TEXT("'%s' has no fields inside it."), *Segment);
				return false;
			}
			if (StructProperty->Struct == FInstancedStruct::StaticStruct())
			{
				FInstancedStruct* Instanced = static_cast<FInstancedStruct*>(Value);
				if (!Instanced->IsValid())
				{
					OutError = FString::Printf(TEXT("'%s' holds nothing yet: pick what it holds first (set_stagger, set_input)."), *Segment);
					return false;
				}
				Struct = Instanced->GetScriptStruct();
				Container = Instanced->GetMutableMemory();
			}
			else
			{
				Struct = StructProperty->Struct;
				Container = Value;
			}
			Where = FString::Printf(TEXT("'%s'"), *Segment);
		}
		return false;
	}

	/** A track's field - or, by a name the track doesn't have, its registered type's setting. */
	bool Resolve(FWidgetAnimLayer& Layer, const FString& Path, FResolved& Out, FString& OutError)
	{
		return ResolveIn(FWidgetAnimLayer::StaticStruct(), &Layer, TEXT("this track"), &Layer.TypeParams, Path, Out, OutError);
	}

	/** What kind of value a field holds - see UWidgetAnimScriptAnimation::GetTrackPropertyKind. */
	FString DescribeKind(const FProperty* Property, const void* Address)
	{
		if (CastField<FBoolProperty>(Property))
		{
			return TEXT("bool");
		}
		if (CastField<FEnumProperty>(Property))
		{
			return TEXT("enum");
		}
		if (const FByteProperty* Byte = CastField<FByteProperty>(Property); Byte && Byte->Enum)
		{
			return TEXT("enum");
		}
		if (const FNumericProperty* Numeric = CastField<FNumericProperty>(Property))
		{
			return Numeric->IsFloatingPoint() ? TEXT("float") : TEXT("int");
		}
		if (CastField<FStrProperty>(Property))
		{
			return TEXT("string");
		}
		if (CastField<FNameProperty>(Property))
		{
			return TEXT("name");
		}
		if (CastField<FTextProperty>(Property))
		{
			return TEXT("text");
		}
		if (CastField<FObjectPropertyBase>(Property))
		{
			return TEXT("object");
		}
		if (CastField<FArrayProperty>(Property))
		{
			return TEXT("array");
		}
		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			if (StructProperty->Struct == FInstancedStruct::StaticStruct())
			{
				const FInstancedStruct* Instanced = static_cast<const FInstancedStruct*>(Address);
				return TEXT("instanced:") + (Instanced && Instanced->IsValid() ? ShortStructName(Instanced->GetScriptStruct()) : FString());
			}
			return TEXT("struct:") + StructProperty->Struct->GetName();
		}
		return TEXT("other");
	}

	FString Export(const FProperty* Property, const void* Address)
	{
		if (CastField<FStrProperty>(Property))
		{
			return *static_cast<const FString*>(Address);
		}
		if (CastField<FNameProperty>(Property))
		{
			return static_cast<const FName*>(Address)->ToString();
		}
		if (CastField<FTextProperty>(Property))
		{
			return static_cast<const FText*>(Address)->ToString();
		}
		if (const FBoolProperty* Bool = CastField<FBoolProperty>(Property))
		{
			return Bool->GetPropertyValue(Address) ? TEXT("True") : TEXT("False");
		}
		if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			const int64 Value = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(Address);
			return EnumProperty->GetEnum()->GetNameStringByValue(Value);
		}
		if (const FByteProperty* Byte = CastField<FByteProperty>(Property); Byte && Byte->Enum)
		{
			return Byte->Enum->GetNameStringByValue(Byte->GetPropertyValue(Address));
		}

		FString Text;
		Property->ExportText_Direct(Text, Address, Address, nullptr, PPF_None);
		return Text;
	}

	/** Keeps a number within its field's ClampMin and ClampMax, as the Details panel would. */
	double Clamp(const FProperty* Property, double Value)
	{
		if (Property->HasMetaData(TEXT("ClampMin")))
		{
			Value = FMath::Max(Value, FCString::Atod(*Property->GetMetaData(TEXT("ClampMin"))));
		}
		if (Property->HasMetaData(TEXT("ClampMax")))
		{
			Value = FMath::Min(Value, FCString::Atod(*Property->GetMetaData(TEXT("ClampMax"))));
		}
		return Value;
	}

	bool Import(FProperty* Property, void* Address, const FString& Text, FString& OutError)
	{
		const FString Trimmed = Text.TrimStartAndEnd();

		if (CastField<FStrProperty>(Property))
		{
			*static_cast<FString*>(Address) = Text;
			return true;
		}
		if (CastField<FNameProperty>(Property))
		{
			*static_cast<FName*>(Address) = Trimmed.IsEmpty() ? NAME_None : FName(*Trimmed);
			return true;
		}
		if (CastField<FTextProperty>(Property))
		{
			*static_cast<FText*>(Address) = FText::FromString(Text);
			return true;
		}
		if (FBoolProperty* Bool = CastField<FBoolProperty>(Property))
		{
			const FString Key = Normalize(Trimmed);
			if (Key == TEXT("true") || Key == TEXT("1") || Key == TEXT("yes") || Key == TEXT("on"))
			{
				Bool->SetPropertyValue(Address, true);
				return true;
			}
			if (Key == TEXT("false") || Key == TEXT("0") || Key == TEXT("no") || Key == TEXT("off"))
			{
				Bool->SetPropertyValue(Address, false);
				return true;
			}
			OutError = FString::Printf(TEXT("'%s' isn't True or False."), *Text);
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
			const int64 Value = FindEnumValue(Enum, Trimmed);
			if (Value == INDEX_NONE)
			{
				OutError = FString::Printf(TEXT("'%s' isn't one of: %s."), *Text, *ListEnumNames(Enum));
				return false;
			}
			EnumStorage->SetIntPropertyValue(Address, Value);
			return true;
		}

		if (FNumericProperty* Numeric = CastField<FNumericProperty>(Property))
		{
			double Value = 0.0;
			if (!LexTryParseString(Value, *Trimmed))
			{
				OutError = FString::Printf(TEXT("'%s' isn't a number."), *Text);
				return false;
			}
			Value = Clamp(Property, Value);
			if (Numeric->IsFloatingPoint())
			{
				Numeric->SetFloatingPointPropertyValue(Address, Value);
			}
			else
			{
				Numeric->SetIntPropertyValue(Address, static_cast<int64>(FMath::RoundToDouble(Value)));
			}
			return true;
		}

		if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
		{
			if (Trimmed.IsEmpty() || Trimmed.Equals(TEXT("None"), ESearchCase::IgnoreCase))
			{
				ObjectProperty->SetObjectPropertyValue(Address, nullptr);
				return true;
			}
			UObject* Found = StaticLoadObject(ObjectProperty->PropertyClass, nullptr, *Trimmed);
			if (!Found || !Found->IsA(ObjectProperty->PropertyClass))
			{
				OutError = FString::Printf(TEXT("No %s at '%s'."), *ObjectProperty->PropertyClass->GetName(), *Text);
				return false;
			}
			ObjectProperty->SetObjectPropertyValue(Address, Found);
			return true;
		}

		if (IsInstancedStruct(Property))
		{
			OutError = TEXT("This field holds a struct of a type to pick: pick it with set_stagger / set_input (SetTrackStruct), then set its fields.");
			return false;
		}

		// Structs and lists: Unreal's own text format - "(R=1,G=0,B=0,A=1)", "((Time=0.5,Value=2),(Time=0.8,Value=1))".
		FErrorCollector Errors;
		const TCHAR* End = Property->ImportText_Direct(*Trimmed, Address, nullptr, PPF_None, &Errors);
		if (!End || !Errors.Text.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Couldn't read '%s' as this field's value%s%s"), *Text, Errors.Text.IsEmpty() ? TEXT(".") : TEXT(": "), *Errors.Text);
			return false;
		}
		return true;
	}

	/** The fields a script may name on Struct, by their authored names. */
	void ListFields(const UStruct* Struct, TArray<FString>& OutNames)
	{
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			if (It->HasAnyPropertyFlags(CPF_Transient))
			{
				continue;
			}
			OutNames.Add(It->GetAuthoredName());
		}
	}

	/** The struct an instanced-struct field may hold the children of - its BaseStruct - or null when it names none. */
	const UScriptStruct* GetBaseStruct(const FProperty* Property)
	{
		const FString BasePath = Property ? Property->GetMetaData(TEXT("BaseStruct")) : FString();
		FTopLevelAssetPath Path;
		if (BasePath.IsEmpty() || !Path.TrySetPath(BasePath))
		{
			return nullptr;
		}
		return FindObject<UScriptStruct>(Path);
	}

	/** Every struct an instanced-struct field may hold: its BaseStruct's children, not the base, nothing marked Hidden. */
	void GetStructOptions(const FProperty* Property, TArray<const UScriptStruct*>& OutStructs)
	{
		const UScriptStruct* Base = GetBaseStruct(Property);
		if (!Base)
		{
			return;
		}
		for (TObjectIterator<UScriptStruct> It; It; ++It)
		{
			const UScriptStruct* Struct = *It;
			if (Struct != Base && Struct->IsChildOf(Base) && !Struct->HasMetaData(TEXT("Hidden"))
				&& !Struct->GetName().StartsWith(TEXT("STRUCT_REINST_")))
			{
				OutStructs.Add(Struct);
			}
		}
		OutStructs.Sort([](const UScriptStruct& A, const UScriptStruct& B) { return A.GetName() < B.GetName(); });
	}

	/** Track fields only the panel uses, which a locked track still takes - as the panel lets them change on one. */
	bool IsEditorOnlyFlag(FName Field)
	{
		static const FName Locked(TEXT("bLockedInEditor"));
		static const FName Hidden(TEXT("bHiddenInEditor"));
		static const FName Pinned(TEXT("bPinnedInEditor"));
		static const FName Collapsed(TEXT("CollapsedTrack"));
		return Field == Locked || Field == Hidden || Field == Pinned || Field == Collapsed;
	}

	/** Track fields that say where it starts: setting one places the track by it, rather than holding it where it was. */
	bool IsPlacementField(FName Field)
	{
		static const FName Start(TEXT("Start"));
		static const FName Delay(TEXT("Delay"));
		return Field == Start || Field == Delay;
	}
}

// =====================================================================================================
// Track types
// =====================================================================================================

namespace WidgetAnimScriptTypes
{
	using namespace WidgetAnimScriptNames;

	/** Every Blueprint class deriving from BaseClass in the project, by the name a script gives it, as the Add Track menu finds them. */
	void ForEachBlueprintClass(const UClass* BaseClass, TFunctionRef<FText(const UObject&)> GetName, TFunctionRef<void(const FString&, const FTopLevelAssetPath&)> Visit)
	{
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

		TSet<FTopLevelAssetPath> DerivedPaths;
		AssetRegistry.GetDerivedClassNames({ BaseClass->GetClassPathName() }, TSet<FTopLevelAssetPath>(), DerivedPaths);

		for (const FTopLevelAssetPath& ClassPath : DerivedPaths)
		{
			const FString ClassName = ClassPath.GetAssetName().ToString();
			if (ClassName.StartsWith(TEXT("SKEL_")) || ClassName.StartsWith(TEXT("REINST_")))
			{
				continue;
			}

			FString Name = ClassName.EndsWith(TEXT("_C")) ? ClassName.LeftChop(2) : ClassName;
			if (const UClass* LoadedClass = FindObject<UClass>(ClassPath))
			{
				if (LoadedClass->HasAnyClassFlags(CLASS_Abstract))
				{
					continue;
				}
				Name = GetName(*LoadedClass->GetDefaultObject()).ToString();
			}
			Visit(Name, ClassPath);
		}
	}

	void ForEachBlueprintType(TFunctionRef<void(const FString&, const FTopLevelAssetPath&)> Visit)
	{
		ForEachBlueprintClass(UWidgetAnimBlueprintType::StaticClass(),
			[](const UObject& Default) { return CastChecked<UWidgetAnimBlueprintType>(&Default)->GetTypeDisplayName(); },
			Visit);
	}

	void ForEachBlueprintCue(TFunctionRef<void(const FString&, const FTopLevelAssetPath&)> Visit)
	{
		ForEachBlueprintClass(UWidgetAnimBlueprintCue::StaticClass(),
			[](const UObject& Default) { return CastChecked<UWidgetAnimBlueprintCue>(&Default)->GetCueDisplayName(); },
			Visit);
	}

	/** The built-in types a script can add, in menu order: every one but Registered Type, which is a type by its settings. */
	void ForEachBuiltInType(TFunctionRef<void(EWidgetAnimType, const FString&)> Visit)
	{
		const UEnum* TypeEnum = StaticEnum<EWidgetAnimType>();
		const int32 Num = TypeEnum ? TypeEnum->NumEnums() - 1 : 0;
		for (int32 Index = 0; Index < Num; ++Index)
		{
			const EWidgetAnimType AnimType = static_cast<EWidgetAnimType>(TypeEnum->GetValueByIndex(Index));
			if (AnimType != EWidgetAnimType::Custom && !TypeEnum->HasMetaData(TEXT("Hidden"), Index))
			{
				Visit(AnimType, TypeEnum->GetNameStringByIndex(Index));
			}
		}
	}

	void GetRegisteredEvaluators(TArray<const FWidgetAnimEvaluator*>& OutEvaluators)
	{
		TArray<const FWidgetAnimEvaluator*> All;
		FWidgetAnimEvaluatorRegistry::Get().GetAll(All);
		for (const FWidgetAnimEvaluator* Evaluator : All)
		{
			if (Evaluator->IsListedInAddMenu())
			{
				OutEvaluators.Add(Evaluator);
			}
		}
	}

	/** Names a registered type answers to: its display name, and its settings struct's name with and without the family's words. */
	bool EvaluatorMatches(const FWidgetAnimEvaluator& Evaluator, const FString& Wanted)
	{
		const FString Key = Normalize(Wanted);
		if (Normalize(Evaluator.GetDisplayName().ToString()) == Key)
		{
			return true;
		}
		const UScriptStruct* Params = Evaluator.GetParamsStruct();
		if (!Params)
		{
			return false;
		}
		FString Name = Normalize(Params->GetName());
		if (Name == Key)
		{
			return true;
		}
		Name.RemoveFromStart(TEXT("widgetanim"));
		Name.RemoveFromEnd(TEXT("params"));
		return Name == Key;
	}

	/** A new track of the type a script names, at its defaults - as the Add Track menu makes one. False, with the reason, if no type is called that. */
	bool MakeLayer(const FString& Type, FWidgetAnimLayer& OutLayer, FString& OutError)
	{
		const FString Key = Normalize(Type);
		if (Key.IsEmpty())
		{
			OutError = TEXT("No track type was given.");
			return false;
		}

		bool bFound = false;
		ForEachBuiltInType([&](EWidgetAnimType AnimType, const FString& Name)
		{
			const UEnum* TypeEnum = StaticEnum<EWidgetAnimType>();
			if (!bFound && (Normalize(Name) == Key || Normalize(TypeEnum->GetDisplayNameTextByValue(static_cast<int64>(AnimType)).ToString()) == Key))
			{
				OutLayer.AnimType = AnimType;
				bFound = true;
			}
		});
		if (bFound)
		{
			return true;
		}

		TArray<const FWidgetAnimEvaluator*> Evaluators;
		GetRegisteredEvaluators(Evaluators);
		for (const FWidgetAnimEvaluator* Evaluator : Evaluators)
		{
			if (EvaluatorMatches(*Evaluator, Type))
			{
				OutLayer.AnimType = EWidgetAnimType::Custom;
				OutLayer.TypeParams = FInstancedStruct(Evaluator->GetParamsStruct());

				// A type whose value is a steady pace - a text typed out - starts without an ease, as from the Add Track menu.
				if (Evaluator->PrefersLinearEasing())
				{
					OutLayer.Easing = EWidgetAnimEasing::Linear;
				}
				return true;
			}
		}

		// Blueprint types and cues, by name - loaded only when picked, as from the menu.
		FTopLevelAssetPath Picked;
		bool bCue = false;
		ForEachBlueprintType([&](const FString& Name, const FTopLevelAssetPath& ClassPath)
		{
			if (Picked.IsNull() && (Normalize(Name) == Key || Normalize(ClassPath.GetAssetName().ToString()) == Key))
			{
				Picked = ClassPath;
			}
		});
		if (Picked.IsNull())
		{
			ForEachBlueprintCue([&](const FString& Name, const FTopLevelAssetPath& ClassPath)
			{
				if (Picked.IsNull() && (Normalize(Name) == Key || Normalize(ClassPath.GetAssetName().ToString()) == Key))
				{
					Picked = ClassPath;
					bCue = true;
				}
			});
		}
		if (!Picked.IsNull())
		{
			UClass* Class = LoadObject<UClass>(nullptr, *Picked.ToString());
			if (Class && !bCue && Class->IsChildOf(UWidgetAnimBlueprintType::StaticClass()))
			{
				OutLayer.AnimType = EWidgetAnimType::Custom;
				OutLayer.TypeParams = FWidgetAnimBlueprintTypeParams::MakeFor(Class);
				return true;
			}
			if (Class && bCue && Class->IsChildOf(UWidgetAnimBlueprintCue::StaticClass()))
			{
				OutLayer.AnimType = EWidgetAnimType::Custom;
				OutLayer.TypeParams = FWidgetAnimBlueprintCueParams::MakeFor(Class);
				return true;
			}
			OutError = FString::Printf(TEXT("The Blueprint type '%s' couldn't be loaded."), *Type);
			return false;
		}

		OutError = FString::Printf(TEXT("No track type is called '%s'. anim.track_types lists them."), *Type);
		return false;
	}
}

// =====================================================================================================
// UWidgetAnimScriptAnimation
// =====================================================================================================

/**
 * One change to the animation: refused unless it can be edited, inside a transaction - its own, or the open BeginEdit's -
 * with the edited object Modify()'d first, and told to the panel once it is made (or, inside BeginEdit, when that ends).
 */
struct FWidgetAnimScriptEdit
{
	FWidgetAnimScriptEdit(UWidgetAnimScriptAnimation& InOwner, const FText& Description)
		: Owner(InOwner)
	{
		UObject* Edited = Owner.GetEditedObject();
		if (!Owner.IsEditable() || !Edited)
		{
			Owner.Fail(Owner.Source.IsValid() && Owner.Source.Pin() && Owner.Source.Pin()->IsReadOnly()
				? TEXT("The panel is locked: unlock it to edit the animation.")
				: TEXT("There is no animation that can be edited here."));
			return;
		}

		if (Owner.EditDepth == 0)
		{
			Transaction.Emplace(Description);
		}
		Edited->Modify();
		bOpen = true;
	}

	~FWidgetAnimScriptEdit()
	{
		if (!bOpen || !bChanged)
		{
			return;
		}
		if (Owner.EditDepth > 0)
		{
			Owner.bChangedInEdit = true;
		}
		else
		{
			// Told before the transaction closes, as the panel's own edits are.
			Owner.Notify(ChangeType);
		}
	}

	bool IsOpen() const { return bOpen; }

	void Changed(EPropertyChangeType::Type InChangeType)
	{
		bChanged = true;
		ChangeType = InChangeType;
	}

private:

	UWidgetAnimScriptAnimation& Owner;
	TOptional<FScopedTransaction> Transaction;
	bool bOpen = false;
	bool bChanged = false;
	EPropertyChangeType::Type ChangeType = EPropertyChangeType::ValueSet;
};

/**
 * One change to the Widget Blueprint's designer tree: refused where there is none, inside a transaction - its own, or the
 * open BeginEdit's - with the Blueprint and its tree Modify()'d first. The Blueprint is marked changed once it is made, or
 * when the BeginEdit ends.
 */
struct FWidgetAnimScriptWidgetEdit
{
	FWidgetAnimScriptWidgetEdit(UWidgetAnimScriptAnimation& InOwner, const FText& Description)
		: Owner(InOwner)
	{
		Blueprint = Owner.GetWidgetBlueprint();
		if (!Blueprint || !Blueprint->WidgetTree)
		{
			Owner.Fail(TEXT("There is no Widget Blueprint to change here - a Widget Animation asset edited on its own has no widgets."));
			return;
		}
		const TSharedPtr<IWidgetAnimPanelSource> Pinned = Owner.Source.Pin();
		if (Pinned && Pinned->IsReadOnly())
		{
			Owner.Fail(TEXT("The panel is locked: unlock it to make changes."));
			return;
		}

		if (Owner.EditDepth == 0)
		{
			Transaction.Emplace(Description);
		}
		Blueprint->Modify();
		Blueprint->WidgetTree->Modify();
		bOpen = true;
	}

	~FWidgetAnimScriptWidgetEdit()
	{
		if (!bOpen || !bChanged)
		{
			return;
		}
		if (Owner.EditDepth > 0)
		{
			Owner.bWidgetsChangedInEdit = true;
		}
		else
		{
			Owner.NotifyWidgetsChanged();
		}
	}

	bool IsOpen() const { return bOpen; }
	void Changed() { bChanged = true; }

	UWidgetBlueprint* Blueprint = nullptr;

private:

	UWidgetAnimScriptAnimation& Owner;
	TOptional<FScopedTransaction> Transaction;
	bool bOpen = false;
	bool bChanged = false;
};

void UWidgetAnimScriptAnimation::InitForPanel(const TSharedPtr<IWidgetAnimPanelSource>& InSource, const TSharedPtr<SWidgetAnimTimeline>& InPanel)
{
	Source = InSource;
	Panel = InPanel;
	Asset = nullptr;
}

void UWidgetAnimScriptAnimation::InitForAsset(UWidgetAnimPresetAsset* InAsset)
{
	Source.Reset();
	Panel.Reset();
	Asset = InAsset;
}

TArray<FWidgetAnimLayerGroup>* UWidgetAnimScriptAnimation::GetGroupsArray() const
{
	if (const TSharedPtr<IWidgetAnimPanelSource> Pinned = Source.Pin())
	{
		return Pinned->GetGroups();
	}
	return Asset ? &Asset->LayerGroups : nullptr;
}

TArray<FWidgetAnimLayer>* UWidgetAnimScriptAnimation::GetLayersArray() const
{
	if (const TSharedPtr<IWidgetAnimPanelSource> Pinned = Source.Pin())
	{
		return Pinned->GetLayers();
	}
	return Asset ? &Asset->Layers : nullptr;
}

UObject* UWidgetAnimScriptAnimation::GetEditedObject() const
{
	if (const TSharedPtr<IWidgetAnimPanelSource> Pinned = Source.Pin())
	{
		return Pinned->GetEditedObject();
	}
	return Asset;
}

int32 UWidgetAnimScriptAnimation::CountTracks() const
{
	const TArray<FWidgetAnimLayer>* Layers = GetLayersArray();
	return Layers ? Layers->Num() : 0;
}

int32 UWidgetAnimScriptAnimation::CountGroups() const
{
	const TArray<FWidgetAnimLayerGroup>* Groups = GetGroupsArray();
	return Groups ? Groups->Num() : 0;
}

bool UWidgetAnimScriptAnimation::Fail(const FString& Error) const
{
	LastError = Error;
	return false;
}

bool UWidgetAnimScriptAnimation::CheckTrack(int32 Track) const
{
	const TArray<FWidgetAnimLayer>* Layers = GetLayersArray();
	if (!Layers)
	{
		return Fail(TEXT("There is no animation open."));
	}
	if (!Layers->IsValidIndex(Track))
	{
		return Fail(FString::Printf(TEXT("There is no track %d: the animation has %d."), Track, Layers->Num()));
	}
	return true;
}

bool UWidgetAnimScriptAnimation::CheckGroup(int32 Group) const
{
	const TArray<FWidgetAnimLayerGroup>* Groups = GetGroupsArray();
	if (!Groups)
	{
		return Fail(TEXT("There is no animation open."));
	}
	if (!SupportsGroups())
	{
		return Fail(TEXT("This list of tracks has no Layer Groups: each track names its own widget (set its target)."));
	}
	if (!Groups->IsValidIndex(Group))
	{
		return Fail(FString::Printf(TEXT("There is no group %d: the animation has %d."), Group, Groups->Num()));
	}
	return true;
}

bool UWidgetAnimScriptAnimation::CheckTrackEditable(int32 Track) const
{
	if (!CheckTrack(Track))
	{
		return false;
	}
	if ((*GetLayersArray())[Track].bLockedInEditor)
	{
		return Fail(FString::Printf(TEXT("Track %d is locked: unlock it first (track.locked = False)."), Track));
	}
	return true;
}

void UWidgetAnimScriptAnimation::Notify(EPropertyChangeType::Type ChangeType)
{
	if (const TSharedPtr<IWidgetAnimPanelSource> Pinned = Source.Pin())
	{
		Pinned->NotifyGroupsChanged(ChangeType);
		return;
	}

	// An asset edited directly: told as the panel's sources tell it (see NotifyLayerGroupsChanged in WidgetAnimPanelSource.cpp).
	if (Asset)
	{
		UWidgetAnimTransformLibrary::SyncAllTransformAnims(Asset);
		UWidgetAnimLibrary::AssignDefaultValueNames(Asset->Layers);
		FProperty* LayersProperty = FindFProperty<FProperty>(UWidgetAnimPresetAsset::StaticClass(), TEXT("Layers"));
		FPropertyChangedEvent Event(LayersProperty, ChangeType);
		Asset->PostEditChangeProperty(Event);
	}
}

// ---------------------------------------------------------------------------------------------
// About it
// ---------------------------------------------------------------------------------------------

bool UWidgetAnimScriptAnimation::IsEditable() const
{
	if (const TSharedPtr<IWidgetAnimPanelSource> Pinned = Source.Pin())
	{
		return Pinned->GetEditedObject() && Pinned->CanEdit() && !Pinned->IsReadOnly() && Pinned->GetGroups() && Pinned->GetLayers();
	}
	return Asset != nullptr;
}

FString UWidgetAnimScriptAnimation::GetAnimationName() const
{
	if (Asset)
	{
		return Asset->GetName();
	}
	const TSharedPtr<IWidgetAnimPanelSource> Pinned = Source.Pin();
	if (!Pinned)
	{
		return FString();
	}
	if (const UWidgetAnimPresetAsset* Edited = Pinned->GetAsset())
	{
		return Edited->GetName();
	}
	if (const UWidgetAnimBuilder* Builder = Pinned->GetBuilder())
	{
		return Builder->AnimationName.IsEmpty() ? TEXT("New Animation") : Builder->AnimationName;
	}
	if (!Pinned->SupportsLayerGroups())
	{
		const UObject* Owner = Pinned->GetEditedObject();
		return FString::Printf(TEXT("%s %s"), Owner ? *Owner->GetName() : TEXT(""), *Pinned->GetLayersPropertyName().ToString()).TrimStartAndEnd();
	}
	return TEXT("New Animation");
}

bool UWidgetAnimScriptAnimation::SupportsGroups() const
{
	const TSharedPtr<IWidgetAnimPanelSource> Pinned = Source.Pin();
	return Pinned ? Pinned->SupportsLayerGroups() : true;
}

float UWidgetAnimScriptAnimation::GetSpeedMultiplier() const
{
	if (const TSharedPtr<IWidgetAnimPanelSource> Pinned = Source.Pin())
	{
		return Pinned->GetAnimationSpeedMultiplier();
	}
	return Asset ? FMath::Max(Asset->AnimationSpeedMultiplier, 0.01f) : 1.f;
}

float UWidgetAnimScriptAnimation::GetLength() const
{
	const TArray<FWidgetAnimLayer>* Layers = GetLayersArray();
	float Length = 0.f;
	if (Layers)
	{
		TArray<float> Starts;
		GetStarts(Starts);
		for (int32 Index = 0; Index < Layers->Num(); ++Index)
		{
			Length = FMath::Max(Length, Starts[Index] + FMath::Max((*Layers)[Index].GetRunLength() + GetPlacementExtra(Index), 0.f));
		}
	}

	float MinimumLength = 0.f;
	if (const TSharedPtr<IWidgetAnimPanelSource> Pinned = Source.Pin())
	{
		MinimumLength = Pinned->GetMinimumLength();
	}
	else if (Asset)
	{
		MinimumLength = FMath::Max(Asset->MinimumLength, 0.f);
	}
	return FMath::Max(Length, MinimumLength);
}

bool UWidgetAnimScriptAnimation::BeginEdit(const FString& Description)
{
	if (EditDepth == 0)
	{
		if (!IsEditable() || !GetEditedObject())
		{
			return Fail(Source.Pin() && Source.Pin()->IsReadOnly()
				? TEXT("The panel is locked: unlock it to edit the animation.")
				: TEXT("There is no animation that can be edited here."));
		}
		EditTransaction = MakeUnique<FScopedTransaction>(FText::FromString(Description.IsEmpty() ? TEXT("Edit Widget Animation") : Description));
		bChangedInEdit = false;
		bWidgetsChangedInEdit = false;
	}
	++EditDepth;
	return true;
}

void UWidgetAnimScriptAnimation::EndEdit()
{
	if (EditDepth == 0)
	{
		return;
	}
	if (--EditDepth > 0)
	{
		return;
	}

	// Everything made inside it told once, then the one transaction closed.
	if (bChangedInEdit)
	{
		Notify(EPropertyChangeType::ValueSet);
	}
	if (bWidgetsChangedInEdit)
	{
		NotifyWidgetsChanged();
	}
	bChangedInEdit = false;
	bWidgetsChangedInEdit = false;
	EditTransaction.Reset();
}

// ---------------------------------------------------------------------------------------------
// Layer Groups
// ---------------------------------------------------------------------------------------------

int32 UWidgetAnimScriptAnimation::GetNumGroups() const
{
	return SupportsGroups() ? CountGroups() : 0;
}

FString UWidgetAnimScriptAnimation::GetGroupName(int32 Group) const
{
	if (!CheckGroup(Group))
	{
		return FString();
	}
	return (*GetGroupsArray())[Group].GetDisplayName(Group);
}

bool UWidgetAnimScriptAnimation::SetGroupName(int32 Group, const FString& Name)
{
	if (!CheckGroup(Group))
	{
		return false;
	}
	FWidgetAnimScriptEdit Edit(*this, LOCTEXT("RenameGroup", "Rename Layer Group"));
	if (!Edit.IsOpen())
	{
		return false;
	}
	(*GetGroupsArray())[Group].GroupName = Name.TrimStartAndEnd();
	Edit.Changed(EPropertyChangeType::ValueSet);
	return true;
}

FString UWidgetAnimScriptAnimation::GetGroupTarget(int32 Group) const
{
	if (!CheckGroup(Group))
	{
		return FString();
	}
	const FName Target = (*GetGroupsArray())[Group].PreviewTarget;
	return Target.IsNone() ? FString() : Target.ToString();
}

bool UWidgetAnimScriptAnimation::SetGroupTarget(int32 Group, const FString& Target)
{
	FString Path;
	if (!CheckGroup(Group) || !ResolveTarget(Target, Path))
	{
		return false;
	}
	FWidgetAnimScriptEdit Edit(*this, LOCTEXT("SetGroupTarget", "Set Preview Target"));
	if (!Edit.IsOpen())
	{
		return false;
	}
	(*GetGroupsArray())[Group].PreviewTarget = Path.IsEmpty() ? NAME_None : FName(*Path);
	Edit.Changed(EPropertyChangeType::ValueSet);
	return true;
}

FString UWidgetAnimScriptAnimation::GetGroupFolder(int32 Group) const
{
	if (!CheckGroup(Group))
	{
		return FString();
	}
	return (*GetGroupsArray())[Group].EditorFolder;
}

bool UWidgetAnimScriptAnimation::SetGroupFolder(int32 Group, const FString& Folder)
{
	if (!CheckGroup(Group))
	{
		return false;
	}
	FWidgetAnimScriptEdit Edit(*this, LOCTEXT("SetGroupFolder", "Put Layer Group in List Folder"));
	if (!Edit.IsOpen())
	{
		return false;
	}
	(*GetGroupsArray())[Group].EditorFolder = Folder.TrimStartAndEnd();
	Edit.Changed(EPropertyChangeType::ValueSet);
	return true;
}

int32 UWidgetAnimScriptAnimation::AddGroup(const FString& Name, const FString& Target)
{
	if (!SupportsGroups())
	{
		Fail(TEXT("This list of tracks has no Layer Groups: each track names its own widget (set its target)."));
		return INDEX_NONE;
	}
	FString Path;
	if (!ResolveTarget(Target, Path))
	{
		return INDEX_NONE;
	}
	FWidgetAnimScriptEdit Edit(*this, LOCTEXT("AddGroup", "Add Layer Group"));
	if (!Edit.IsOpen())
	{
		return INDEX_NONE;
	}

	TArray<FWidgetAnimLayerGroup>& Groups = *GetGroupsArray();
	FWidgetAnimLayerGroup& NewGroup = Groups.AddDefaulted_GetRef();
	NewGroup.GroupName = Name.TrimStartAndEnd();
	NewGroup.PreviewTarget = Path.IsEmpty() ? NAME_None : FName(*Path);
	Edit.Changed(EPropertyChangeType::ArrayAdd);
	return Groups.Num() - 1;
}

bool UWidgetAnimScriptAnimation::RemoveGroup(int32 Group)
{
	if (!CheckGroup(Group))
	{
		return false;
	}
	FWidgetAnimScriptEdit Edit(*this, LOCTEXT("RemoveGroup", "Delete Layer Group"));
	if (!Edit.IsOpen())
	{
		return false;
	}

	TArray<FWidgetAnimLayerGroup>& Groups = *GetGroupsArray();
	TArray<FWidgetAnimLayer>& Layers = *GetLayersArray();
	const int32 NumGroups = Groups.Num();

	// The group's tracks go with it - every other track staying where it is in time - and every later group's tracks
	// follow their group down one place, as the panel's Delete Layer Group does.
	TArray<float> OldStarts;
	TArray<float> OldExtras;
	GetStarts(OldStarts);
	GetExtras(OldExtras);

	TArray<int32> OldIndexOf;
	TArray<FWidgetAnimLayer> Kept;
	TArray<FPlacementInfo> KeptInfo;
	for (int32 LayerIndex = 0; LayerIndex < Layers.Num(); ++LayerIndex)
	{
		if (UWidgetAnimLibrary::GetLayerGroupIndex(Layers[LayerIndex], NumGroups) != Group)
		{
			Kept.Add(Layers[LayerIndex]);
			KeptInfo.Add(PlacementInfo.IsValidIndex(LayerIndex) ? PlacementInfo[LayerIndex] : FPlacementInfo());
			OldIndexOf.Add(LayerIndex);
		}
	}
	Layers = MoveTemp(Kept);
	PlacementInfo = MoveTemp(KeptInfo);
	PinStarts(OldStarts, OldExtras, OldIndexOf);

	for (FWidgetAnimLayer& Layer : Layers)
	{
		const int32 InGroup = UWidgetAnimLibrary::GetLayerGroupIndex(Layer, NumGroups);
		Layer.LayerGroup = InGroup > Group ? InGroup - 1 : InGroup;
		for (int32& InsertedGroup : Layer.InsertedGroups)
		{
			InsertedGroup = InsertedGroup == Group ? INDEX_NONE : (InsertedGroup > Group ? InsertedGroup - 1 : InsertedGroup);
		}
	}
	Groups.RemoveAt(Group);

	Edit.Changed(EPropertyChangeType::ArrayRemove);
	return true;
}

// ---------------------------------------------------------------------------------------------
// Tracks
// ---------------------------------------------------------------------------------------------

int32 UWidgetAnimScriptAnimation::GetNumTracks() const
{
	return CountTracks();
}

TArray<FString> UWidgetAnimScriptAnimation::GetTrackTypes() const
{
	TArray<FString> Types;
	WidgetAnimScriptTypes::ForEachBuiltInType([&Types](EWidgetAnimType, const FString& Name) { Types.Add(Name); });

	TArray<const FWidgetAnimEvaluator*> Evaluators;
	WidgetAnimScriptTypes::GetRegisteredEvaluators(Evaluators);
	for (const FWidgetAnimEvaluator* Evaluator : Evaluators)
	{
		Types.Add(Evaluator->GetDisplayName().ToString());
	}

	TArray<FString> BlueprintTypes;
	WidgetAnimScriptTypes::ForEachBlueprintType([&BlueprintTypes](const FString& Name, const FTopLevelAssetPath&) { BlueprintTypes.Add(Name); });
	WidgetAnimScriptTypes::ForEachBlueprintCue([&BlueprintTypes](const FString& Name, const FTopLevelAssetPath&) { BlueprintTypes.Add(Name); });
	BlueprintTypes.Sort();
	Types.Append(BlueprintTypes);
	return Types;
}

int32 UWidgetAnimScriptAnimation::AddTrack(const FString& Type, int32 Group)
{
	FWidgetAnimLayer NewLayer;
	FString Error;
	if (!WidgetAnimScriptTypes::MakeLayer(Type, NewLayer, Error))
	{
		Fail(Error);
		return INDEX_NONE;
	}
	if (SupportsGroups() && Group != INDEX_NONE && !CheckGroup(Group))
	{
		return INDEX_NONE;
	}

	FWidgetAnimScriptEdit Edit(*this, LOCTEXT("AddTrack", "Add Animation Track"));
	if (!Edit.IsOpen())
	{
		return INDEX_NONE;
	}

	TArray<FWidgetAnimLayerGroup>& Groups = *GetGroupsArray();
	TArray<FWidgetAnimLayer>& Layers = *GetLayersArray();

	// A track always lands in a group: the one asked for, else the last, else a new first one - as the panel adds them.
	if (Groups.Num() == 0)
	{
		Groups.AddDefaulted();
	}
	NewLayer.LayerGroup = !SupportsGroups() ? 0 : (Group == INDEX_NONE ? Groups.Num() - 1 : Group);

	// As long as the editor preferences' Default Track Length, following the track before it: a run of add_track calls
	// builds a sequence, and a start set afterwards puts each where the script wants it.
	NewLayer.Duration = FMath::Max(UWidgetAnimEditorSettings::Get().DefaultTrackLength, SWidgetAnimTimeline::MinLayerSeconds);
	if (Layers.Num() > 0)
	{
		NewLayer.Start = EWidgetAnimLayerStart::AfterPrevious;
	}

	TArray<float> OldStarts;
	TArray<float> OldExtras;
	GetStarts(OldStarts);
	GetExtras(OldExtras);

	TArray<int32> OldIndexOf;
	for (int32 LayerIndex = 0; LayerIndex < Layers.Num(); ++LayerIndex)
	{
		OldIndexOf.Add(LayerIndex);
	}
	OldIndexOf.Add(INDEX_NONE);

	Layers.Add(NewLayer);
	PlacementInfo.Add(FPlacementInfo());
	PinStarts(OldStarts, OldExtras, OldIndexOf);

	Edit.Changed(EPropertyChangeType::ArrayAdd);
	return Layers.Num() - 1;
}

int32 UWidgetAnimScriptAnimation::DuplicateTrack(int32 Track)
{
	if (!CheckTrack(Track))
	{
		return INDEX_NONE;
	}
	FWidgetAnimScriptEdit Edit(*this, LOCTEXT("DuplicateTrack", "Duplicate Animation Track"));
	if (!Edit.IsOpen())
	{
		return INDEX_NONE;
	}

	TArray<FWidgetAnimLayer>& Layers = *GetLayersArray();
	TArray<float> OldStarts;
	TArray<float> OldExtras;
	GetStarts(OldStarts);
	GetExtras(OldExtras);

	// A copy is made to be changed, so it never starts out locked, hidden or pinned - as the panel's Duplicate makes it.
	FWidgetAnimLayer Copy = Layers[Track];
	Copy.bLockedInEditor = false;
	Copy.bHiddenInEditor = false;
	Copy.bHiddenByFocus = false;
	Copy.bPinnedInEditor = false;

	const FPlacementInfo CopiedInfo = PlacementInfo.IsValidIndex(Track) ? PlacementInfo[Track] : FPlacementInfo();
	Layers.Insert(Copy, Track + 1);
	SWidgetAnimTimeline::EnsureUniqueTrackName(Layers, Track + 1);
	PlacementInfo.Insert(CopiedInfo, Track + 1);

	// It starts exactly with the track it copies; nothing else moves.
	TArray<int32> OldIndexOf;
	for (int32 LayerIndex = 0; LayerIndex < Layers.Num(); ++LayerIndex)
	{
		OldIndexOf.Add(LayerIndex <= Track ? LayerIndex : (LayerIndex == Track + 1 ? Track : LayerIndex - 1));
	}
	PinStarts(OldStarts, OldExtras, OldIndexOf);

	Edit.Changed(EPropertyChangeType::Duplicate);
	return Track + 1;
}

bool UWidgetAnimScriptAnimation::RemoveTracks(const TArray<int32>& Tracks)
{
	TArray<int32> ToRemove;
	for (const int32 Track : Tracks)
	{
		if (!CheckTrackEditable(Track))
		{
			return false;
		}
		ToRemove.AddUnique(Track);
	}
	if (ToRemove.Num() == 0)
	{
		return true;
	}

	FWidgetAnimScriptEdit Edit(*this, ToRemove.Num() == 1 ? LOCTEXT("RemoveTrack", "Delete Animation Track") : LOCTEXT("RemoveTracks", "Delete Animation Tracks"));
	if (!Edit.IsOpen())
	{
		return false;
	}

	TArray<FWidgetAnimLayer>& Layers = *GetLayersArray();
	TArray<float> OldStarts;
	TArray<float> OldExtras;
	GetStarts(OldStarts);
	GetExtras(OldExtras);

	// From the end, so each removal leaves the indices still to go where they were. Every track left stays where it is in time.
	ToRemove.Sort(TGreater<int32>());
	const int32 OldNum = Layers.Num();
	for (const int32 Track : ToRemove)
	{
		Layers.RemoveAt(Track);
		if (PlacementInfo.IsValidIndex(Track))
		{
			PlacementInfo.RemoveAt(Track);
		}
	}

	TArray<int32> OldIndexOf;
	for (int32 OldIndex = 0; OldIndex < OldNum; ++OldIndex)
	{
		if (!ToRemove.Contains(OldIndex))
		{
			OldIndexOf.Add(OldIndex);
		}
	}
	PinStarts(OldStarts, OldExtras, OldIndexOf);

	Edit.Changed(EPropertyChangeType::ArrayRemove);
	return true;
}

FString UWidgetAnimScriptAnimation::GetTrackType(int32 Track) const
{
	if (!CheckTrack(Track))
	{
		return FString();
	}
	return SWidgetAnimTimeline::GetLayerTypeName((*GetLayersArray())[Track]).ToString();
}

FString UWidgetAnimScriptAnimation::GetTrackName(int32 Track) const
{
	if (!CheckTrack(Track))
	{
		return FString();
	}
	return (*GetLayersArray())[Track].TrackName;
}

FString UWidgetAnimScriptAnimation::SetTrackName(int32 Track, const FString& Name)
{
	if (!CheckTrackEditable(Track))
	{
		return FString();
	}
	FWidgetAnimScriptEdit Edit(*this, LOCTEXT("RenameTrack", "Rename Track"));
	if (!Edit.IsOpen())
	{
		return FString();
	}
	TArray<FWidgetAnimLayer>& Layers = *GetLayersArray();
	Layers[Track].TrackName = SWidgetAnimTimeline::MakeUniqueTrackName(Layers, Name, Track);
	Edit.Changed(EPropertyChangeType::ValueSet);
	return Layers[Track].TrackName;
}

int32 UWidgetAnimScriptAnimation::GetTrackGroup(int32 Track) const
{
	if (!CheckTrack(Track))
	{
		return INDEX_NONE;
	}
	return UWidgetAnimLibrary::GetLayerGroupIndex((*GetLayersArray())[Track], CountGroups());
}

bool UWidgetAnimScriptAnimation::SetTrackGroup(int32 Track, int32 Group)
{
	if (!CheckTrackEditable(Track) || !CheckGroup(Group))
	{
		return false;
	}
	FWidgetAnimScriptEdit Edit(*this, LOCTEXT("SetTrackGroup", "Move to Layer Group"));
	if (!Edit.IsOpen())
	{
		return false;
	}

	// Membership only: where the track sits in time is its place in the one list, which doesn't change.
	FWidgetAnimLayer& Layer = (*GetLayersArray())[Track];
	Layer.LayerGroup = Group;
	Layer.EditorLine = INDEX_NONE;
	Edit.Changed(EPropertyChangeType::ValueSet);
	return true;
}

FString UWidgetAnimScriptAnimation::GetTrackTarget(int32 Track) const
{
	if (!CheckTrack(Track))
	{
		return FString();
	}
	const FWidgetAnimLayer& Layer = (*GetLayersArray())[Track];
	if (!SupportsGroups())
	{
		return Layer.TargetWidget.IsNone() ? FString() : Layer.TargetWidget.ToString();
	}
	const int32 Group = GetTrackGroup(Track);
	return Group == INDEX_NONE ? FString() : GetGroupTarget(Group);
}

bool UWidgetAnimScriptAnimation::SetTrackTarget(int32 Track, const FString& Target)
{
	FString Path;
	if (!CheckTrackEditable(Track) || !ResolveTarget(Target, Path))
	{
		return false;
	}

	if (!SupportsGroups())
	{
		FWidgetAnimScriptEdit Edit(*this, LOCTEXT("SetTrackTarget", "Set Target Widget"));
		if (!Edit.IsOpen())
		{
			return false;
		}
		(*GetLayersArray())[Track].TargetWidget = Path.IsEmpty() ? NAME_None : FName(*Path);
		Edit.Changed(EPropertyChangeType::ValueSet);
		return true;
	}

	// In an animation the group says which widget a track moves: the first group moving this one, or a new one for it.
	const TArray<FWidgetAnimLayerGroup>& Groups = *GetGroupsArray();
	int32 Group = INDEX_NONE;
	for (int32 GroupIndex = 0; GroupIndex < Groups.Num(); ++GroupIndex)
	{
		const FString GroupTarget = Groups[GroupIndex].PreviewTarget.IsNone() ? FString() : Groups[GroupIndex].PreviewTarget.ToString();
		if (GroupTarget.Equals(Path, ESearchCase::IgnoreCase))
		{
			Group = GroupIndex;
			break;
		}
	}

	FWidgetAnimScriptEdit Edit(*this, LOCTEXT("SetTrackTarget", "Set Target Widget"));
	if (!Edit.IsOpen())
	{
		return false;
	}
	if (Group == INDEX_NONE)
	{
		FString Name = Path;
		int32 Slash = INDEX_NONE;
		if (Name.FindLastChar(TEXT('/'), Slash))
		{
			Name = Name.Mid(Slash + 1);
		}
		FWidgetAnimLayerGroup& NewGroup = GetGroupsArray()->AddDefaulted_GetRef();
		NewGroup.GroupName = Name;
		NewGroup.PreviewTarget = Path.IsEmpty() ? NAME_None : FName(*Path);
		Group = GetGroupsArray()->Num() - 1;
	}

	FWidgetAnimLayer& Layer = (*GetLayersArray())[Track];
	if (UWidgetAnimLibrary::GetLayerGroupIndex(Layer, GetGroupsArray()->Num()) != Group)
	{
		Layer.LayerGroup = Group;
		Layer.EditorLine = INDEX_NONE;
	}
	Edit.Changed(EPropertyChangeType::ValueSet);
	return true;
}

float UWidgetAnimScriptAnimation::GetTrackStart(int32 Track) const
{
	if (!CheckTrack(Track))
	{
		return 0.f;
	}
	TArray<float> Starts;
	GetStarts(Starts);
	return Starts.IsValidIndex(Track) ? Starts[Track] : 0.f;
}

bool UWidgetAnimScriptAnimation::SetTrackStart(int32 Track, float Seconds)
{
	if (!CheckTrackEditable(Track))
	{
		return false;
	}
	FWidgetAnimScriptEdit Edit(*this, LOCTEXT("SetTrackStart", "Move Animation Track"));
	if (!Edit.IsOpen())
	{
		return false;
	}

	// Every track where it was but this one, which goes where it was asked to: its Delay - and every Delay after it - rewritten to hold them there.
	TArray<float> OldStarts;
	TArray<float> OldExtras;
	GetStarts(OldStarts);
	GetExtras(OldExtras);
	OldStarts[Track] = FMath::Max(Seconds, 0.f);

	TArray<int32> OldIndexOf;
	for (int32 LayerIndex = 0; LayerIndex < OldStarts.Num(); ++LayerIndex)
	{
		OldIndexOf.Add(LayerIndex);
	}
	PinStarts(OldStarts, OldExtras, OldIndexOf);

	Edit.Changed(EPropertyChangeType::ValueSet);
	return true;
}

float UWidgetAnimScriptAnimation::GetTrackLength(int32 Track) const
{
	if (!CheckTrack(Track))
	{
		return 0.f;
	}
	EnsurePlacementInfo();
	return FMath::Max((*GetLayersArray())[Track].GetRunLength() + GetPlacementExtra(Track), 0.f);
}

FString UWidgetAnimScriptAnimation::GetTrackProperty(int32 Track, const FString& Path) const
{
	if (!CheckTrack(Track))
	{
		return FString();
	}
	WidgetAnimScriptFields::FResolved Resolved;
	FString Error;
	if (!WidgetAnimScriptFields::Resolve((*GetLayersArray())[Track], Path, Resolved, Error))
	{
		Fail(Error);
		return FString();
	}
	return WidgetAnimScriptFields::Export(Resolved.Property, Resolved.Address);
}

FString UWidgetAnimScriptAnimation::GetTrackPropertyKind(int32 Track, const FString& Path) const
{
	if (!CheckTrack(Track))
	{
		return FString();
	}
	WidgetAnimScriptFields::FResolved Resolved;
	FString Error;
	if (!WidgetAnimScriptFields::Resolve((*GetLayersArray())[Track], Path, Resolved, Error))
	{
		Fail(Error);
		return FString();
	}
	return WidgetAnimScriptFields::DescribeKind(Resolved.Property, Resolved.Address);
}

bool UWidgetAnimScriptAnimation::SetTrackProperty(int32 Track, const FString& Path, const FString& Value)
{
	if (!CheckTrack(Track))
	{
		return false;
	}

	// Found first, on the track as it is, to know whether a locked track takes it.
	WidgetAnimScriptFields::FResolved Resolved;
	FString Error;
	if (!WidgetAnimScriptFields::Resolve((*GetLayersArray())[Track], Path, Resolved, Error))
	{
		return Fail(Error);
	}
	if (!WidgetAnimScriptFields::IsEditorOnlyFlag(Resolved.TrackField) && !CheckTrackEditable(Track))
	{
		return false;
	}
	if (WidgetAnimScriptFields::IsInstancedStruct(Resolved.Property))
	{
		return Fail(FString::Printf(TEXT("'%s' holds a struct of a type to pick: pick it with set_stagger / set_input (SetTrackStruct), then set its fields."), *Path));
	}

	FWidgetAnimScriptEdit Edit(*this, LOCTEXT("SetTrackField", "Edit Animation Track"));
	if (!Edit.IsOpen())
	{
		return false;
	}

	TArray<float> OldStarts;
	TArray<float> OldExtras;
	GetStarts(OldStarts);
	GetExtras(OldExtras);

	// Modify() moves nothing, so the address found above is still the field's.
	if (!WidgetAnimScriptFields::Import(Resolved.Property, Resolved.Address, Value, Error))
	{
		return Fail(FString::Printf(TEXT("%s: %s"), *Path, *Error));
	}

	// A new Start or Delay places the track by it; any other change holds it - and every other track - where it was.
	TArray<int32> OldIndexOf;
	for (int32 LayerIndex = 0; LayerIndex < OldStarts.Num(); ++LayerIndex)
	{
		OldIndexOf.Add(LayerIndex == Track && WidgetAnimScriptFields::IsPlacementField(Resolved.TrackField) ? INDEX_NONE : LayerIndex);
	}
	PinStarts(OldStarts, OldExtras, OldIndexOf);

	Edit.Changed(EPropertyChangeType::ValueSet);
	return true;
}

bool UWidgetAnimScriptAnimation::SetTrackStruct(int32 Track, const FString& Path, const FString& StructType)
{
	if (!CheckTrackEditable(Track))
	{
		return false;
	}

	WidgetAnimScriptFields::FResolved Resolved;
	FString Error;
	if (!WidgetAnimScriptFields::Resolve((*GetLayersArray())[Track], Path, Resolved, Error))
	{
		return Fail(Error);
	}
	if (!WidgetAnimScriptFields::IsInstancedStruct(Resolved.Property))
	{
		return Fail(FString::Printf(TEXT("'%s' isn't a field that holds a struct of a type to pick."), *Path));
	}

	const UScriptStruct* Picked = nullptr;
	if (!StructType.TrimStartAndEnd().IsEmpty() && !StructType.TrimStartAndEnd().Equals(TEXT("None"), ESearchCase::IgnoreCase))
	{
		TArray<const UScriptStruct*> Options;
		WidgetAnimScriptFields::GetStructOptions(Resolved.Property, Options);
		for (const UScriptStruct* Option : Options)
		{
			if (WidgetAnimScriptNames::StructMatches(Option, StructType))
			{
				Picked = Option;
				break;
			}
		}
		if (!Picked)
		{
			TArray<FString> Names;
			for (const UScriptStruct* Option : Options)
			{
				Names.Add(WidgetAnimScriptNames::ShortStructName(Option));
			}
			return Fail(FString::Printf(TEXT("'%s' can't hold '%s'. It takes: %s."), *Path, *StructType, Names.Num() ? *FString::Join(Names, TEXT(", ")) : TEXT("nothing")));
		}
	}

	FWidgetAnimScriptEdit Edit(*this, LOCTEXT("SetTrackStruct", "Edit Animation Track"));
	if (!Edit.IsOpen())
	{
		return false;
	}

	TArray<float> OldStarts;
	TArray<float> OldExtras;
	GetStarts(OldStarts);
	GetExtras(OldExtras);

	FInstancedStruct& Instanced = *static_cast<FInstancedStruct*>(Resolved.Address);
	if (Picked)
	{
		Instanced.InitializeAs(Picked);
	}
	else
	{
		Instanced.Reset();
	}

	// A registered type's settings are its type: a track given some plays as that type.
	static const FName TypeParamsName(TEXT("TypeParams"));
	if (Picked && Resolved.TrackField == TypeParamsName)
	{
		(*GetLayersArray())[Track].AnimType = EWidgetAnimType::Custom;
	}

	TArray<int32> OldIndexOf;
	for (int32 LayerIndex = 0; LayerIndex < OldStarts.Num(); ++LayerIndex)
	{
		OldIndexOf.Add(LayerIndex);
	}
	PinStarts(OldStarts, OldExtras, OldIndexOf);

	Edit.Changed(EPropertyChangeType::ValueSet);
	return true;
}

TArray<FString> UWidgetAnimScriptAnimation::GetTrackStructOptions(const FString& Path) const
{
	TArray<FString> Names;
	const FProperty* Property = WidgetAnimScriptNames::FindProperty(FWidgetAnimLayer::StaticStruct(), Path);
	if (!WidgetAnimScriptFields::IsInstancedStruct(Property))
	{
		Fail(FString::Printf(TEXT("'%s' isn't a field of a track that holds a struct of a type to pick."), *Path));
		return Names;
	}
	TArray<const UScriptStruct*> Options;
	WidgetAnimScriptFields::GetStructOptions(Property, Options);
	for (const UScriptStruct* Option : Options)
	{
		Names.Add(WidgetAnimScriptNames::ShortStructName(Option));
	}
	return Names;
}

TArray<FString> UWidgetAnimScriptAnimation::GetTrackPropertyNames(int32 Track, const FString& Path) const
{
	TArray<FString> Names;
	if (!CheckTrack(Track))
	{
		return Names;
	}
	if (Path.TrimStartAndEnd().IsEmpty())
	{
		WidgetAnimScriptFields::ListFields(FWidgetAnimLayer::StaticStruct(), Names);
		return Names;
	}

	WidgetAnimScriptFields::FResolved Resolved;
	FString Error;
	if (!WidgetAnimScriptFields::Resolve((*GetLayersArray())[Track], Path, Resolved, Error))
	{
		Fail(Error);
		return Names;
	}
	if (const FStructProperty* StructProperty = CastField<FStructProperty>(Resolved.Property))
	{
		if (StructProperty->Struct == FInstancedStruct::StaticStruct())
		{
			const FInstancedStruct* Instanced = static_cast<const FInstancedStruct*>(Resolved.Address);
			if (Instanced->IsValid())
			{
				WidgetAnimScriptFields::ListFields(Instanced->GetScriptStruct(), Names);
			}
		}
		else
		{
			WidgetAnimScriptFields::ListFields(StructProperty->Struct, Names);
		}
	}
	return Names;
}

// ---------------------------------------------------------------------------------------------
// Collapsed tracks
// ---------------------------------------------------------------------------------------------

FString UWidgetAnimScriptAnimation::CollapseTracks(const TArray<int32>& Tracks, const FString& Name)
{
	for (const int32 Track : Tracks)
	{
		if (!CheckTrack(Track))
		{
			return FString();
		}
	}
	if (Tracks.Num() == 0)
	{
		Fail(TEXT("No tracks were given to collapse."));
		return FString();
	}

	FWidgetAnimScriptEdit Edit(*this, LOCTEXT("CollapseTracks", "Collapse Tracks"));
	if (!Edit.IsOpen())
	{
		return FString();
	}

	// Its own name, numbered if another collapsed track has it - as the panel's Collapse names one.
	TArray<FWidgetAnimLayer>& Layers = *GetLayersArray();
	FString Base = Name.TrimStartAndEnd();
	if (Base.IsEmpty())
	{
		Base = TEXT("Collapsed Tracks");
	}
	auto IsTaken = [&Layers, &Tracks](FName Candidate)
	{
		for (int32 LayerIndex = 0; LayerIndex < Layers.Num(); ++LayerIndex)
		{
			if (Layers[LayerIndex].CollapsedTrack == Candidate && !Tracks.Contains(LayerIndex))
			{
				return true;
			}
		}
		return false;
	};
	FName Unique(*Base);
	for (int32 Suffix = 2; IsTaken(Unique); ++Suffix)
	{
		Unique = FName(*FString::Printf(TEXT("%s %d"), *Base, Suffix));
	}

	for (const int32 Track : Tracks)
	{
		Layers[Track].CollapsedTrack = Unique;
	}
	Edit.Changed(EPropertyChangeType::ValueSet);
	return Unique.ToString();
}

bool UWidgetAnimScriptAnimation::ExpandCollapsed(const FString& Name)
{
	const TArray<int32> Members = GetCollapsedTracks(Name);
	if (Members.Num() == 0)
	{
		return Fail(FString::Printf(TEXT("No collapsed track is called '%s'."), *Name));
	}
	FWidgetAnimScriptEdit Edit(*this, LOCTEXT("ExpandCollapsed", "Expand Collapsed Track"));
	if (!Edit.IsOpen())
	{
		return false;
	}
	TArray<FWidgetAnimLayer>& Layers = *GetLayersArray();
	for (const int32 Track : Members)
	{
		Layers[Track].CollapsedTrack = NAME_None;
	}
	Edit.Changed(EPropertyChangeType::ValueSet);
	return true;
}

TArray<FString> UWidgetAnimScriptAnimation::GetCollapsedNames() const
{
	TArray<FString> Names;
	if (const TArray<FWidgetAnimLayer>* Layers = GetLayersArray())
	{
		for (const FWidgetAnimLayer& Layer : *Layers)
		{
			if (!Layer.CollapsedTrack.IsNone())
			{
				Names.AddUnique(Layer.CollapsedTrack.ToString());
			}
		}
	}
	return Names;
}

TArray<int32> UWidgetAnimScriptAnimation::GetCollapsedTracks(const FString& Name) const
{
	TArray<int32> Members;
	const TArray<FWidgetAnimLayer>* Layers = GetLayersArray();
	const FString Wanted = Name.TrimStartAndEnd();
	if (Layers && !Wanted.IsEmpty())
	{
		for (int32 LayerIndex = 0; LayerIndex < Layers->Num(); ++LayerIndex)
		{
			const FName Collapsed = (*Layers)[LayerIndex].CollapsedTrack;
			if (!Collapsed.IsNone() && Collapsed.ToString().Equals(Wanted, ESearchCase::IgnoreCase))
			{
				Members.Add(LayerIndex);
			}
		}
	}
	return Members;
}

// ---------------------------------------------------------------------------------------------
// Placement
// ---------------------------------------------------------------------------------------------

void UWidgetAnimScriptAnimation::EnsurePlacementInfo() const
{
	const TArray<FWidgetAnimLayer>* Layers = GetLayersArray();
	const int32 Num = Layers ? Layers->Num() : 0;
	if (PlacementFrame == GFrameCounter && PlacementInfo.Num() == Num)
	{
		return;
	}

	PlacementFrame = GFrameCounter;
	PlacementInfo.Reset();
	PlacementInfo.SetNum(Num);

	const TSharedPtr<IWidgetAnimPanelSource> Pinned = Source.Pin();
	if (!Pinned || !Layers)
	{
		return;
	}
	for (int32 LayerIndex = 0; LayerIndex < Num; ++LayerIndex)
	{
		const FWidgetAnimLayer& Layer = (*Layers)[LayerIndex];
		if (Layer.Expander.IsValid())
		{
			PlacementInfo[LayerIndex].Count = Pinned->GetExpandedCount(LayerIndex);
		}
		if (SWidgetAnimTimeline::HasDerivedLength(Layer))
		{
			PlacementInfo[LayerIndex].PreparedRun = Pinned->GetPreparedRun(LayerIndex);
		}
	}
}

float UWidgetAnimScriptAnimation::GetPlacementExtra(int32 Track) const
{
	const TArray<FWidgetAnimLayer>* Layers = GetLayersArray();
	if (!Layers || !Layers->IsValidIndex(Track))
	{
		return 0.f;
	}
	const FWidgetAnimLayer& Layer = (*Layers)[Track];
	const FPlacementInfo Info = PlacementInfo.IsValidIndex(Track) ? PlacementInfo[Track] : FPlacementInfo();

	// A Stagger reaches past its first item - as far as the preview fans it out, or with no count, as far as a Spread Over
	// reaches. See WidgetAnimStaggerFans::GetExtra in SWidgetAnimTimeline.cpp, which this mirrors.
	float Extra = 0.f;
	const FWidgetAnimExpander* Expander = Layer.Expander.GetPtr<FWidgetAnimExpander>();
	if (Expander && !Layer.IsValueDriven() && !(Expander->GathersParts() && !UWidgetAnimLibrary::TypeTakesParts(Layer.AnimType, Layer.TypeParams)))
	{
		Extra = Info.Count == INDEX_NONE
			? (Expander->Spacing == EWidgetAnimStaggerSpacing::Across ? FMath::Max(Expander->Spread, 0.f) : 0.f)
			: Expander->GetFanSeconds(Info.Count);
	}

	// A length worked out on the preview, in place of its Duration.
	if (Info.PreparedRun >= 0.f && SWidgetAnimTimeline::HasDerivedLength(Layer))
	{
		Extra += Info.PreparedRun - Layer.GetRunLength();
	}
	return Extra;
}

void UWidgetAnimScriptAnimation::GetExtras(TArray<float>& OutExtras) const
{
	EnsurePlacementInfo();
	OutExtras.Reset();
	const int32 Num = CountTracks();
	for (int32 Index = 0; Index < Num; ++Index)
	{
		OutExtras.Add(GetPlacementExtra(Index));
	}
}

void UWidgetAnimScriptAnimation::GetStarts(TArray<float>& OutStarts) const
{
	OutStarts.Reset();
	const TArray<FWidgetAnimLayer>* Layers = GetLayersArray();
	if (!Layers)
	{
		return;
	}
	TArray<float> Extras;
	GetExtras(Extras);
	UWidgetAnimLibrary::PlaceLayerStarts(*Layers, Extras, OutStarts);
}

void UWidgetAnimScriptAnimation::PinStarts(const TArray<float>& OldStarts, const TArray<float>& OldExtras, const TArray<int32>& OldIndexOf)
{
	TArray<FWidgetAnimLayer>* Layers = GetLayersArray();
	if (!Layers)
	{
		return;
	}

	// Walked in sequence, as playback places them: each track measures from the one now above it.
	float PreviousStart = 0.f;
	float PreviousEnd = 0.f;
	for (int32 LayerIndex = 0; LayerIndex < Layers->Num(); ++LayerIndex)
	{
		FWidgetAnimLayer& Layer = (*Layers)[LayerIndex];
		const float Anchor = Layer.Start == EWidgetAnimLayerStart::AfterPrevious ? PreviousEnd : PreviousStart;
		const int32 OldIndex = OldIndexOf.IsValidIndex(LayerIndex) ? OldIndexOf[LayerIndex] : INDEX_NONE;

		float LayerStart = 0.f;
		if (OldStarts.IsValidIndex(OldIndex))
		{
			LayerStart = OldStarts[OldIndex];
			const float Delay = LayerStart - Anchor;
			if (!FMath::IsNearlyEqual(Layer.Delay, Delay, 1.e-5f))
			{
				Layer.Delay = Delay;
			}
		}
		else
		{
			LayerStart = FMath::Max(Anchor + Layer.Delay, 0.f);
		}

		const float Extra = OldExtras.IsValidIndex(OldIndex) ? OldExtras[OldIndex] : GetPlacementExtra(LayerIndex);
		PreviousStart = LayerStart;
		PreviousEnd = LayerStart + FMath::Max(Layer.GetRunLength() + Extra, 0.f);
	}
}

// ---------------------------------------------------------------------------------------------
// Widgets
// ---------------------------------------------------------------------------------------------

UWidget* UWidgetAnimScriptAnimation::GetTreeRoot() const
{
	const TSharedPtr<IWidgetAnimPanelSource> Pinned = Source.Pin();
	if (!Pinned)
	{
		return nullptr;
	}

	// The Widget Blueprint's own tree - what a script changes, so a widget added a line ago is already there - and without
	// one, the designer's preview. The preview has the same names, and the insides of nested User Widgets; see GatherWidgets.
	if (UWidgetBlueprint* Blueprint = GetWidgetBlueprint())
	{
		if (Blueprint->WidgetTree && Blueprint->WidgetTree->RootWidget)
		{
			return Blueprint->WidgetTree->RootWidget;
		}
	}
	if (UUserWidget* Preview = Pinned->GetPreviewRoot())
	{
		if (Preview->WidgetTree && Preview->WidgetTree->RootWidget)
		{
			return Preview->WidgetTree->RootWidget;
		}
	}
	return nullptr;
}

void UWidgetAnimScriptAnimation::GatherWidgets(TArray<FWidgetEntry>& OutWidgets) const
{
	OutWidgets.Reset();

	// Paths as the panel's targets name them (see WidgetAnimTree::Gather): a widget by its name, one inside a nested User
	// Widget as "Nested/Inner".
	// A nested User Widget in the Widget Blueprint's tree has no insides of its own at design time: they are read from the
	// same widget in the designer's preview.
	const TSharedPtr<IWidgetAnimPanelSource> Pinned = Source.Pin();
	UUserWidget* Preview = Pinned ? Pinned->GetPreviewRoot() : nullptr;

	// The preview's User Widgets by path, gathered once, the first time one is needed.
	TMap<FString, UUserWidget*> PreviewUserWidgets;
	bool bPreviewGathered = false;
	auto FindInPreview = [&PreviewUserWidgets, &bPreviewGathered, Preview](const FString& Path) -> UUserWidget*
	{
		if (!bPreviewGathered && Preview)
		{
			bPreviewGathered = true;
			TFunction<void(UWidget*, const FString&, int32)> Walk;
			Walk = [&PreviewUserWidgets, &Walk](UWidget* Widget, const FString& Prefix, int32 Depth)
			{
				if (!Widget || Depth > 128)
				{
					return;
				}
				const FString WidgetPath = Prefix.IsEmpty() ? Widget->GetName() : Prefix + TEXT("/") + Widget->GetName();
				if (const UPanelWidget* PanelWidget = Cast<UPanelWidget>(Widget))
				{
					for (int32 Index = 0; Index < PanelWidget->GetChildrenCount(); ++Index)
					{
						Walk(PanelWidget->GetChildAt(Index), Prefix, Depth + 1);
					}
				}
				else if (UUserWidget* Nested = Cast<UUserWidget>(Widget))
				{
					PreviewUserWidgets.Add(WidgetPath, Nested);
					if (Nested->WidgetTree)
					{
						Walk(Nested->WidgetTree->RootWidget, WidgetPath, Depth + 1);
					}
				}
			};
			if (Preview->WidgetTree)
			{
				Walk(Preview->WidgetTree->RootWidget, FString(), 0);
			}
		}
		UUserWidget* const* Found = PreviewUserWidgets.Find(Path);
		return Found ? *Found : nullptr;
	};

	TFunction<void(UWidget*, const FString&, const FString&, int32)> Gather;
	Gather = [&OutWidgets, &Gather, &FindInPreview, Preview](UWidget* Widget, const FString& TreePrefix, const FString& ParentPath, int32 Depth)
	{
		static constexpr int32 MaxDepth = 128;
		if (!Widget || Depth > MaxDepth)
		{
			return;
		}

		const FString Path = TreePrefix.IsEmpty() ? Widget->GetName() : (TreePrefix + TEXT("/") + Widget->GetName());
		OutWidgets.Add(FWidgetEntry{ Path, ParentPath, Widget });

		if (const UPanelWidget* PanelWidget = Cast<UPanelWidget>(Widget))
		{
			for (int32 Index = 0; Index < PanelWidget->GetChildrenCount(); ++Index)
			{
				Gather(PanelWidget->GetChildAt(Index), TreePrefix, Path, Depth + 1);
			}
		}
		else if (const UUserWidget* Nested = Cast<UUserWidget>(Widget))
		{
			const UUserWidget* Inside = Nested;
			if ((!Inside->WidgetTree || !Inside->WidgetTree->RootWidget) && Preview && Nested != Preview)
			{
				Inside = FindInPreview(Path);
			}
			if (Inside && Inside->WidgetTree && Inside->WidgetTree->RootWidget)
			{
				Gather(Inside->WidgetTree->RootWidget, Path, Path, Depth + 1);
			}
		}
	};
	Gather(GetTreeRoot(), FString(), FString(), 0);
}

bool UWidgetAnimScriptAnimation::ResolveTarget(const FString& Target, FString& OutPath) const
{
	const FString Trimmed = Target.TrimStartAndEnd();
	if (Trimmed.IsEmpty() || Trimmed.Equals(TEXT("None"), ESearchCase::IgnoreCase))
	{
		OutPath.Reset();
		return true;
	}

	// A plain list's tracks name widgets the component's way; an animation's groups by the paths gathered here, which
	// include widgets a script has just added - the panel's own options catch up only once the designer has.
	TArray<FString> Options;
	if (const TSharedPtr<IWidgetAnimPanelSource> Pinned = Source.Pin())
	{
		if (Pinned->SupportsLayerTargets())
		{
			Options = Pinned->GetLayerTargetOptions();
		}
		else
		{
			Options = GetWidgetPaths();
			if (Options.Num() == 0)
			{
				Options = Pinned->GetTargetOptions();
			}
		}
	}
	Options.RemoveAll([](const FString& Option) { return Option.Equals(TEXT("None"), ESearchCase::IgnoreCase); });

	// Nothing to check it against - an asset edited directly, a designer not open: taken as it is.
	if (Options.Num() == 0)
	{
		OutPath = Trimmed;
		return true;
	}

	for (const FString& Option : Options)
	{
		if (Option.Equals(Trimmed, ESearchCase::IgnoreCase))
		{
			OutPath = Option;
			return true;
		}
	}

	// By its name alone: a widget of the Widget Blueprint's own tree before one inside a nested User Widget.
	for (const bool bNested : { false, true })
	{
		for (const FString& Option : Options)
		{
			int32 Slash = INDEX_NONE;
			const bool bIsNested = Option.FindLastChar(TEXT('/'), Slash);
			if (bIsNested == bNested && Option.Mid(Slash + 1).Equals(Trimmed, ESearchCase::IgnoreCase))
			{
				OutPath = Option;
				return true;
			}
		}
	}
	return Fail(FString::Printf(TEXT("No widget called '%s' here."), *Trimmed));
}

TArray<FString> UWidgetAnimScriptAnimation::GetWidgetPaths() const
{
	TArray<FWidgetEntry> Widgets;
	GatherWidgets(Widgets);
	TArray<FString> Paths;
	for (const FWidgetEntry& Entry : Widgets)
	{
		Paths.AddUnique(Entry.Path);
	}
	return Paths;
}

FString UWidgetAnimScriptAnimation::GetRootWidget() const
{
	const UWidget* Root = GetTreeRoot();
	return Root ? Root->GetName() : FString();
}

FString UWidgetAnimScriptAnimation::FindWidget(const FString& NameOrPath) const
{
	const FString Wanted = NameOrPath.TrimStartAndEnd();
	if (Wanted.IsEmpty())
	{
		return FString();
	}
	TArray<FWidgetEntry> Widgets;
	GatherWidgets(Widgets);
	for (const FWidgetEntry& Entry : Widgets)
	{
		if (Entry.Path.Equals(Wanted, ESearchCase::IgnoreCase))
		{
			return Entry.Path;
		}
	}
	for (const bool bNested : { false, true })
	{
		for (const FWidgetEntry& Entry : Widgets)
		{
			int32 Slash = INDEX_NONE;
			const bool bIsNested = Entry.Path.FindLastChar(TEXT('/'), Slash);
			if (bIsNested == bNested && Entry.Path.Mid(Slash + 1).Equals(Wanted, ESearchCase::IgnoreCase))
			{
				return Entry.Path;
			}
		}
	}
	return FString();
}

FString UWidgetAnimScriptAnimation::GetWidgetClass(const FString& Path) const
{
	TArray<FWidgetEntry> Widgets;
	GatherWidgets(Widgets);
	for (const FWidgetEntry& Entry : Widgets)
	{
		if (Entry.Path.Equals(Path, ESearchCase::IgnoreCase) && Entry.Widget)
		{
			// A Blueprint class without its _C: what the Palette calls it.
			FString Name = Entry.Widget->GetClass()->GetName();
			Name.RemoveFromEnd(TEXT("_C"));
			return Name;
		}
	}
	return FString();
}

FString UWidgetAnimScriptAnimation::GetWidgetParent(const FString& Path) const
{
	TArray<FWidgetEntry> Widgets;
	GatherWidgets(Widgets);
	for (const FWidgetEntry& Entry : Widgets)
	{
		if (Entry.Path.Equals(Path, ESearchCase::IgnoreCase))
		{
			return Entry.Parent;
		}
	}
	return FString();
}

TArray<FString> UWidgetAnimScriptAnimation::GetWidgetChildren(const FString& Path) const
{
	TArray<FWidgetEntry> Widgets;
	GatherWidgets(Widgets);
	TArray<FString> Children;
	for (const FWidgetEntry& Entry : Widgets)
	{
		if (Entry.Parent.Equals(Path, ESearchCase::IgnoreCase))
		{
			Children.Add(Entry.Path);
		}
	}
	return Children;
}

int32 UWidgetAnimScriptAnimation::GetWidgetIndex(const FString& Path) const
{
	TArray<FWidgetEntry> Widgets;
	GatherWidgets(Widgets);
	FString Parent;
	bool bFound = false;
	for (const FWidgetEntry& Entry : Widgets)
	{
		if (Entry.Path.Equals(Path, ESearchCase::IgnoreCase))
		{
			Parent = Entry.Parent;
			bFound = true;
			break;
		}
	}
	if (!bFound || Parent.IsEmpty())
	{
		return INDEX_NONE;
	}
	int32 Index = 0;
	for (const FWidgetEntry& Entry : Widgets)
	{
		if (Entry.Parent.Equals(Parent, ESearchCase::IgnoreCase))
		{
			if (Entry.Path.Equals(Path, ESearchCase::IgnoreCase))
			{
				return Index;
			}
			++Index;
		}
	}
	return INDEX_NONE;
}

// ---------------------------------------------------------------------------------------------
// Widgets - changing the Widget Blueprint's own designer tree
// ---------------------------------------------------------------------------------------------

UWidgetBlueprint* UWidgetAnimScriptAnimation::GetWidgetBlueprint() const
{
	const TSharedPtr<IWidgetAnimPanelSource> Pinned = Source.Pin();
	if (!Pinned)
	{
		return nullptr;
	}
	if (UWidgetBlueprint* Blueprint = Pinned->GetWidgetBlueprint())
	{
		return Blueprint;
	}

	// A plain list of tracks - a component's - is part of the Widget Blueprint its component was added in.
	const UObject* Edited = Pinned->GetEditedObject();
	return Edited ? Edited->GetTypedOuter<UWidgetBlueprint>() : nullptr;
}

bool UWidgetAnimScriptAnimation::CanEditWidgets() const
{
	const UWidgetBlueprint* Blueprint = GetWidgetBlueprint();
	return Blueprint && Blueprint->WidgetTree;
}

UWidget* UWidgetAnimScriptAnimation::FindEditableWidget(const FString& PathOrName) const
{
	UWidgetBlueprint* Blueprint = GetWidgetBlueprint();
	if (!Blueprint || !Blueprint->WidgetTree)
	{
		Fail(TEXT("There is no Widget Blueprint to change here - a Widget Animation asset edited on its own has no widgets."));
		return nullptr;
	}

	// Its own name, found straight away - the usual case, and a script may ask thousands of times.
	const FString Trimmed = PathOrName.TrimStartAndEnd();
	if (!Trimmed.Contains(TEXT("/")))
	{
		if (UWidget* Direct = Blueprint->WidgetTree->FindWidget(FName(*Trimmed)))
		{
			return Direct;
		}
	}

	const FString Path = FindWidget(Trimmed);
	if (Path.IsEmpty())
	{
		Fail(FString::Printf(TEXT("No widget called '%s' in this Widget Blueprint."), *Trimmed));
		return nullptr;
	}
	if (Path.Contains(TEXT("/")))
	{
		Fail(FString::Printf(TEXT("'%s' is inside a nested User Widget: change it in that Widget Blueprint."), *Path));
		return nullptr;
	}
	UWidget* Widget = Blueprint->WidgetTree->FindWidget(FName(*Path));
	if (!Widget)
	{
		Fail(FString::Printf(TEXT("'%s' isn't one of this Widget Blueprint's own widgets."), *Path));
	}
	return Widget;
}

UObject* UWidgetAnimScriptAnimation::GetPropertyOwner(const FString& Widget, bool bSlot) const
{
	UWidget* Found = FindEditableWidget(Widget);
	if (!Found || !bSlot)
	{
		return Found;
	}
	if (!Found->Slot)
	{
		Fail(FString::Printf(TEXT("'%s' is the top widget: it has no slot."), *Found->GetName()));
		return nullptr;
	}
	return Found->Slot;
}

UClass* UWidgetAnimScriptAnimation::FindWidgetClass(const FString& Name) const
{
	using namespace WidgetAnimScriptNames;

	const FString Trimmed = Name.TrimStartAndEnd();
	UClass* Found = nullptr;

	if (Trimmed.Contains(TEXT("/")))
	{
		// A path: a class, or a Widget Blueprint whose class it is.
		UObject* Loaded = StaticLoadObject(UObject::StaticClass(), nullptr, *Trimmed);
		if (!Loaded && !Trimmed.Contains(TEXT(".")))
		{
			const FString AssetName = FPaths::GetBaseFilename(Trimmed);
			Loaded = StaticLoadObject(UObject::StaticClass(), nullptr, *(Trimmed + TEXT(".") + AssetName));
		}
		if (const UBlueprint* LoadedBlueprint = Cast<UBlueprint>(Loaded))
		{
			Found = LoadedBlueprint->GeneratedClass;
		}
		else
		{
			Found = Cast<UClass>(Loaded);
		}
	}
	else
	{
		// By name: a widget class already loaded - every native one, and Widget Blueprints in use...
		const FString Key = Normalize(Trimmed);
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Class = *It;
			if (!Class->IsChildOf(UWidget::StaticClass()) || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
			{
				continue;
			}
			FString ClassName = Class->GetName();
			if (ClassName.StartsWith(TEXT("SKEL_")) || ClassName.StartsWith(TEXT("REINST_")))
			{
				continue;
			}
			ClassName.RemoveFromEnd(TEXT("_C"));
			if (Normalize(ClassName) == Key || Normalize(Class->GetDisplayNameText().ToString()) == Key)
			{
				Found = Class;
				break;
			}
		}

		// ...or a Widget Blueprint not loaded yet, found by its asset's name.
		if (!Found)
		{
			IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
			TArray<FAssetData> Assets;
			AssetRegistry.GetAssetsByClass(UWidgetBlueprint::StaticClass()->GetClassPathName(), Assets, /*bSearchSubClasses=*/true);
			for (const FAssetData& a : Assets)
			{
				if (a.AssetName.ToString().Equals(Trimmed, ESearchCase::IgnoreCase))
				{
					if (const UBlueprint* LoadedBlueprint = Cast<UBlueprint>(a.GetAsset()))
					{
						Found = LoadedBlueprint->GeneratedClass;
					}
					break;
				}
			}
		}
	}

	if (!Found || !Found->IsChildOf(UWidget::StaticClass()))
	{
		Fail(FString::Printf(TEXT("No widget class or Widget Blueprint is called '%s'. Try Border, Image, TextBlock, UniformGridPanel, HorizontalBox, VerticalBox, Overlay, SizeBox, or a Widget Blueprint's name."), *Trimmed));
		return nullptr;
	}
	if (Found->HasAnyClassFlags(CLASS_Abstract))
	{
		Fail(FString::Printf(TEXT("'%s' is a base for other widgets, not one that can be placed."), *Trimmed));
		return nullptr;
	}

	// Not inside itself.
	const UWidgetBlueprint* Blueprint = GetWidgetBlueprint();
	if (Blueprint && Blueprint->GeneratedClass && Found->IsChildOf(Blueprint->GeneratedClass))
	{
		Fail(FString::Printf(TEXT("'%s' can't go inside itself."), *Trimmed));
		return nullptr;
	}
	return Found;
}

FName UWidgetAnimScriptAnimation::MakeWidgetName(const FString& Base, bool bTryBase) const
{
	UWidgetBlueprint* Blueprint = GetWidgetBlueprint();
	check(Blueprint && Blueprint->WidgetTree);

	// Names as UMG gives them: what a name ends in after an underscore, numbered from there on.
	FString Stem = Base.TrimStartAndEnd().Replace(TEXT(" "), TEXT("_"));
	if (Stem.IsEmpty())
	{
		Stem = TEXT("Widget");
	}
	int32 Underscore = INDEX_NONE;
	if (!bTryBase && Stem.FindLastChar(TEXT('_'), Underscore) && Stem.Mid(Underscore + 1).IsNumeric())
	{
		Stem = Stem.Left(Underscore);
	}

	FKismetNameValidator Validator(Blueprint);
	auto IsFree = [Blueprint, &Validator](const FName Candidate)
	{
		return !Blueprint->WidgetTree->FindWidget(Candidate)
			&& !StaticFindObjectFast(UObject::StaticClass(), Blueprint->WidgetTree, Candidate)
			&& Validator.IsValid(Candidate) == EValidatorResult::Ok;
	};

	if (bTryBase && IsFree(FName(*Stem)))
	{
		return FName(*Stem);
	}
	for (int32 Number = 1; ; ++Number)
	{
		const FName Candidate(*FString::Printf(TEXT("%s_%d"), *Stem, Number));
		if (IsFree(Candidate))
		{
			return Candidate;
		}
	}
}

UWidget* UWidgetAnimScriptAnimation::CloneWidget(UWidget* SourceWidget, const FString& BaseName, bool bTryBase)
{
	UWidgetBlueprint* Blueprint = GetWidgetBlueprint();
	if (!SourceWidget || !Blueprint || !Blueprint->WidgetTree)
	{
		return nullptr;
	}

	// The widget and its slots in it (a panel's - their outer is the panel, so they are copied with it). The children those
	// slots hold are the original's, their outer being the tree: each is copied in turn, and put in its copied slot.
	UWidget* Copy = DuplicateObject<UWidget>(SourceWidget, Blueprint->WidgetTree, MakeWidgetName(BaseName, bTryBase));
	if (!Copy)
	{
		return nullptr;
	}
	Copy->SetFlags(RF_Transactional);
	Copy->Slot = nullptr;
	Blueprint->OnVariableAdded(Copy->GetFName());

	if (UPanelWidget* PanelCopy = Cast<UPanelWidget>(Copy))
	{
		for (UPanelSlot* PanelSlot : PanelCopy->GetSlots())
		{
			if (!PanelSlot)
			{
				continue;
			}
			PanelSlot->SetFlags(RF_Transactional);
			PanelSlot->Parent = PanelCopy;
			if (UWidget* OriginalChild = PanelSlot->Content)
			{
				UWidget* ChildCopy = CloneWidget(OriginalChild, OriginalChild->GetName(), /*bTryBase=*/false);
				PanelSlot->Content = ChildCopy;
				if (ChildCopy)
				{
					ChildCopy->Slot = PanelSlot;
				}
			}
		}
	}
	return Copy;
}

bool UWidgetAnimScriptAnimation::PlaceWidget(UWidget* Widget, UPanelWidget* Parent, int32 Index)
{
	UWidgetBlueprint* Blueprint = GetWidgetBlueprint();
	if (!Parent)
	{
		if (Blueprint->WidgetTree->RootWidget)
		{
			return Fail(TEXT("Say where it goes: this Widget Blueprint already has a top widget - add it into a panel."));
		}
		Blueprint->WidgetTree->RootWidget = Widget;
		return true;
	}

	if (!Parent->CanAddMoreChildren())
	{
		return Fail(FString::Printf(TEXT("'%s' can't take another child%s."), *Parent->GetName(), Parent->CanHaveMultipleChildren() ? TEXT("") : TEXT(": it holds one, which it has")));
	}
	Parent->Modify();
	UPanelSlot* NewSlot = Parent->AddChild(Widget);
	if (!NewSlot)
	{
		return Fail(FString::Printf(TEXT("'%s' wouldn't take '%s'."), *Parent->GetName(), *Widget->GetName()));
	}
	NewSlot->SetFlags(RF_Transactional);
	if (Index >= 0 && Index < Parent->GetChildrenCount() - 1)
	{
		Parent->ShiftChild(Index, Widget);
	}
	return true;
}

void UWidgetAnimScriptAnimation::NotifyWidgetsChanged()
{
	if (UWidgetBlueprint* Blueprint = GetWidgetBlueprint())
	{
		// As the designer does after adding or removing widgets: the Blueprint's variables follow, and its preview is rebuilt.
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	}
}

FString UWidgetAnimScriptAnimation::AddWidget(const FString& WidgetClass, const FString& Parent, const FString& Name, int32 Index)
{
	UClass* Class = FindWidgetClass(WidgetClass);
	if (!Class)
	{
		return FString();
	}
	UPanelWidget* ParentPanel = nullptr;
	if (!Parent.TrimStartAndEnd().IsEmpty())
	{
		UWidget* ParentWidget = FindEditableWidget(Parent);
		if (!ParentWidget)
		{
			return FString();
		}
		ParentPanel = Cast<UPanelWidget>(ParentWidget);
		if (!ParentPanel)
		{
			Fail(FString::Printf(TEXT("'%s' can't hold other widgets."), *ParentWidget->GetName()));
			return FString();
		}
	}

	FWidgetAnimScriptWidgetEdit Edit(*this, LOCTEXT("AddWidget", "Add Widget"));
	if (!Edit.IsOpen())
	{
		return FString();
	}

	FString Base = Name.TrimStartAndEnd();
	if (Base.IsEmpty())
	{
		Base = Class->GetName();
		Base.RemoveFromEnd(TEXT("_C"));
	}
	const FName NewName = MakeWidgetName(Base, /*bTryBase=*/true);
	UWidget* Widget = Edit.Blueprint->WidgetTree->ConstructWidget<UWidget>(Class, NewName);
	if (!Widget)
	{
		Fail(FString::Printf(TEXT("A %s couldn't be made."), *Class->GetName()));
		return FString();
	}
	Widget->SetFlags(RF_Transactional);
	Edit.Blueprint->OnVariableAdded(Widget->GetFName());

	if (!PlaceWidget(Widget, ParentPanel, Index))
	{
		Edit.Blueprint->OnVariableRemoved(Widget->GetFName());
		Widget->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors);
		return FString();
	}
	Edit.Changed();
	return Widget->GetName();
}

TArray<FString> UWidgetAnimScriptAnimation::CopyWidget(const FString& SourcePath, int32 Count, const FString& Parent, int32 Index, const FString& Name)
{
	TArray<FString> Paths;
	UWidget* SourceWidget = FindEditableWidget(SourcePath);
	if (!SourceWidget)
	{
		return Paths;
	}
	if (Count < 1)
	{
		Fail(TEXT("Ask for at least one copy."));
		return Paths;
	}

	UPanelWidget* ParentPanel = nullptr;
	if (Parent.TrimStartAndEnd().IsEmpty())
	{
		ParentPanel = SourceWidget->GetParent();
		if (!ParentPanel)
		{
			Fail(FString::Printf(TEXT("'%s' is the top widget: say which panel the copies go in."), *SourceWidget->GetName()));
			return Paths;
		}
	}
	else
	{
		UWidget* ParentWidget = FindEditableWidget(Parent);
		if (!ParentWidget)
		{
			return Paths;
		}
		ParentPanel = Cast<UPanelWidget>(ParentWidget);
		if (!ParentPanel)
		{
			Fail(FString::Printf(TEXT("'%s' can't hold other widgets."), *ParentWidget->GetName()));
			return Paths;
		}
	}

	FWidgetAnimScriptWidgetEdit Edit(*this, LOCTEXT("CopyWidget", "Copy Widget"));
	if (!Edit.IsOpen())
	{
		return Paths;
	}

	const FString Base = Name.TrimStartAndEnd().IsEmpty() ? SourceWidget->GetName() : Name.TrimStartAndEnd();
	const UPanelSlot* SourceSlot = SourceWidget->Slot;
	for (int32 CopyIndex = 0; CopyIndex < Count; ++CopyIndex)
	{
		UWidget* Copy = CloneWidget(SourceWidget, Base, /*bTryBase=*/!Name.TrimStartAndEnd().IsEmpty() && CopyIndex == 0);
		if (!Copy)
		{
			Fail(FString::Printf(TEXT("'%s' couldn't be copied."), *SourceWidget->GetName()));
			break;
		}
		if (!PlaceWidget(Copy, ParentPanel, Index >= 0 ? Index + CopyIndex : INDEX_NONE))
		{
			Edit.Blueprint->OnVariableRemoved(Copy->GetFName());
			Copy->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors);
			break;
		}

		// Its slot set as the original's is, where it is the same kind of slot - padding, alignment, a grid cell.
		if (SourceSlot && Copy->Slot && SourceSlot->GetClass() == Copy->Slot->GetClass())
		{
			for (TFieldIterator<FProperty> It(Copy->Slot->GetClass()); It; ++It)
			{
				if (It->HasAnyPropertyFlags(CPF_Edit) && !It->HasAnyPropertyFlags(CPF_Transient))
				{
					It->CopyCompleteValue_InContainer(Copy->Slot, SourceSlot);
				}
			}
			Copy->Slot->SynchronizeProperties();
		}
		Paths.Add(Copy->GetName());
	}
	if (Paths.Num() > 0)
	{
		Edit.Changed();
	}
	return Paths;
}

bool UWidgetAnimScriptAnimation::RemoveWidgets(const TArray<FString>& Widgets)
{
	TArray<UWidget*> ToRemove;
	for (const FString& Path : Widgets)
	{
		UWidget* Widget = FindEditableWidget(Path);
		if (!Widget)
		{
			return false;
		}
		ToRemove.AddUnique(Widget);
	}
	if (ToRemove.Num() == 0)
	{
		return true;
	}

	FWidgetAnimScriptWidgetEdit Edit(*this, ToRemove.Num() == 1 ? LOCTEXT("RemoveWidget", "Delete Widget") : LOCTEXT("RemoveWidgets", "Delete Widgets"));
	if (!Edit.IsOpen())
	{
		return false;
	}

	// As the designer deletes: out of the tree, and each - with everything inside it - out of the Blueprint's variables and
	// into the transient package, so its name is free again.
	for (UWidget* Widget : ToRemove)
	{
		if (!IsValid(Widget) || Widget->GetOuter() != Edit.Blueprint->WidgetTree)
		{
			continue; // inside one removed before it
		}
		TArray<UWidget*> Gone;
		Gone.Add(Widget);
		UWidgetTree::GetChildWidgets(Widget, Gone);

		if (UPanelWidget* Parent = Widget->GetParent())
		{
			Parent->Modify();
		}
		Widget->Modify();
		Edit.Blueprint->WidgetTree->RemoveWidget(Widget);

		for (UWidget* Removed : Gone)
		{
			Removed->Modify();
			Edit.Blueprint->OnVariableRemoved(Removed->GetFName());
			Removed->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors);
		}
	}
	Edit.Changed();
	return true;
}

bool UWidgetAnimScriptAnimation::MoveWidget(const FString& WidgetPath, const FString& Parent, int32 Index)
{
	UWidget* Widget = FindEditableWidget(WidgetPath);
	if (!Widget)
	{
		return false;
	}
	UPanelWidget* OldParent = Widget->GetParent();
	UPanelWidget* NewParent = OldParent;
	if (!Parent.TrimStartAndEnd().IsEmpty())
	{
		UWidget* ParentWidget = FindEditableWidget(Parent);
		if (!ParentWidget)
		{
			return false;
		}
		NewParent = Cast<UPanelWidget>(ParentWidget);
		if (!NewParent)
		{
			return Fail(FString::Printf(TEXT("'%s' can't hold other widgets."), *ParentWidget->GetName()));
		}
	}
	if (!NewParent)
	{
		return Fail(FString::Printf(TEXT("'%s' is the top widget: say which panel it moves into."), *Widget->GetName()));
	}
	for (const UWidget* Ancestor = NewParent; Ancestor; Ancestor = Ancestor->GetParent())
	{
		if (Ancestor == Widget)
		{
			return Fail(FString::Printf(TEXT("'%s' can't go inside itself."), *Widget->GetName()));
		}
	}

	FWidgetAnimScriptWidgetEdit Edit(*this, LOCTEXT("MoveWidget", "Move Widget"));
	if (!Edit.IsOpen())
	{
		return false;
	}

	if (NewParent == OldParent)
	{
		const int32 Last = OldParent->GetChildrenCount() - 1;
		OldParent->Modify();
		OldParent->ShiftChild(Index < 0 ? Last : FMath::Min(Index, Last), Widget);
	}
	else
	{
		if (!NewParent->CanAddMoreChildren())
		{
			return Fail(FString::Printf(TEXT("'%s' can't take another child."), *NewParent->GetName()));
		}
		if (OldParent)
		{
			OldParent->Modify();
		}
		Widget->Modify();
		Widget->RemoveFromParent();
		if (!PlaceWidget(Widget, NewParent, Index))
		{
			return false;
		}
	}
	Edit.Changed();
	return true;
}

FString UWidgetAnimScriptAnimation::GetWidgetProperty(const FString& Widget, const FString& Path, bool bSlot) const
{
	UObject* Owner = GetPropertyOwner(Widget, bSlot);
	if (!Owner)
	{
		return FString();
	}
	WidgetAnimScriptFields::FResolved Resolved;
	FString Error;
	if (!WidgetAnimScriptFields::ResolveIn(Owner->GetClass(), Owner, FString::Printf(TEXT("'%s'"), *Owner->GetName()), nullptr, Path, Resolved, Error))
	{
		Fail(Error);
		return FString();
	}
	return WidgetAnimScriptFields::Export(Resolved.Property, Resolved.Address);
}

FString UWidgetAnimScriptAnimation::GetWidgetPropertyKind(const FString& Widget, const FString& Path, bool bSlot) const
{
	UObject* Owner = GetPropertyOwner(Widget, bSlot);
	if (!Owner)
	{
		return FString();
	}
	WidgetAnimScriptFields::FResolved Resolved;
	FString Error;
	if (!WidgetAnimScriptFields::ResolveIn(Owner->GetClass(), Owner, FString::Printf(TEXT("'%s'"), *Owner->GetName()), nullptr, Path, Resolved, Error))
	{
		Fail(Error);
		return FString();
	}
	return WidgetAnimScriptFields::DescribeKind(Resolved.Property, Resolved.Address);
}

bool UWidgetAnimScriptAnimation::SetWidgetProperty(const FString& Widget, const FString& Path, const FString& Value, bool bSlot)
{
	UObject* Owner = GetPropertyOwner(Widget, bSlot);
	if (!Owner)
	{
		return false;
	}
	WidgetAnimScriptFields::FResolved Resolved;
	FString Error;
	if (!WidgetAnimScriptFields::ResolveIn(Owner->GetClass(), Owner, FString::Printf(TEXT("'%s'"), *Owner->GetName()), nullptr, Path, Resolved, Error))
	{
		return Fail(Error);
	}
	const bool bInstancedObject = CastField<FObjectPropertyBase>(Resolved.Property) && Resolved.Property->HasAnyPropertyFlags(CPF_InstancedReference);
	if (WidgetAnimScriptFields::IsInstancedStruct(Resolved.Property) || bInstancedObject)
	{
		return Fail(FString::Printf(TEXT("'%s' can't be set from a script."), *Path));
	}

	FWidgetAnimScriptWidgetEdit Edit(*this, LOCTEXT("SetWidgetProperty", "Edit Widget"));
	if (!Edit.IsOpen())
	{
		return false;
	}
	Owner->Modify();
	if (!WidgetAnimScriptFields::Import(Resolved.Property, Resolved.Address, Value, Error))
	{
		return Fail(FString::Printf(TEXT("%s: %s"), *Path, *Error));
	}

	// Told as a Details edit would tell it, so the widget - or slot - takes it up.
	FPropertyChangedEvent Event(Resolved.TopProperty, EPropertyChangeType::ValueSet);
	Owner->PostEditChangeProperty(Event);
	if (UPanelSlot* PanelSlot = Cast<UPanelSlot>(Owner))
	{
		PanelSlot->SynchronizeProperties();
	}
	Edit.Changed();
	return true;
}

TArray<FString> UWidgetAnimScriptAnimation::GetWidgetPropertyNames(const FString& Widget, const FString& Path, bool bSlot) const
{
	TArray<FString> Names;
	UObject* Owner = GetPropertyOwner(Widget, bSlot);
	if (!Owner)
	{
		return Names;
	}

	// What the Details panel shows: the editable properties.
	auto List = [&Names](const UStruct* Struct)
	{
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			if (It->HasAnyPropertyFlags(CPF_Edit) && !It->HasAnyPropertyFlags(CPF_Deprecated))
			{
				Names.Add(It->GetAuthoredName());
			}
		}
	};
	if (Path.TrimStartAndEnd().IsEmpty())
	{
		List(Owner->GetClass());
		return Names;
	}

	WidgetAnimScriptFields::FResolved Resolved;
	FString Error;
	if (!WidgetAnimScriptFields::ResolveIn(Owner->GetClass(), Owner, FString::Printf(TEXT("'%s'"), *Owner->GetName()), nullptr, Path, Resolved, Error))
	{
		Fail(Error);
		return Names;
	}
	if (const FStructProperty* StructProperty = CastField<FStructProperty>(Resolved.Property))
	{
		List(StructProperty->Struct);
	}
	return Names;
}

FString UWidgetAnimScriptAnimation::GetSlotClass(const FString& Widget) const
{
	// Read from whichever tree has it - a nested User Widget's insides too, read only.
	const FString Path = FindWidget(Widget);
	TArray<FWidgetEntry> Widgets;
	GatherWidgets(Widgets);
	for (const FWidgetEntry& Entry : Widgets)
	{
		if (Entry.Path == Path)
		{
			return Entry.Widget && Entry.Widget->Slot ? Entry.Widget->Slot->GetClass()->GetName() : FString();
		}
	}
	return FString();
}

// ---------------------------------------------------------------------------------------------
// The panel's selection
// ---------------------------------------------------------------------------------------------

TArray<int32> UWidgetAnimScriptAnimation::GetSelectedTracks() const
{
	const TSharedPtr<SWidgetAnimTimeline> Pinned = Panel.Pin();
	TArray<int32> Tracks = Pinned ? Pinned->GetSelectedLayers() : TArray<int32>();
	const int32 Num = CountTracks();
	Tracks.RemoveAll([Num](int32 Track) { return Track < 0 || Track >= Num; });
	return Tracks;
}

int32 UWidgetAnimScriptAnimation::GetSelectedGroup() const
{
	const TSharedPtr<SWidgetAnimTimeline> Pinned = Panel.Pin();
	if (!Pinned || !SupportsGroups())
	{
		return INDEX_NONE;
	}
	const SWidgetAnimTimeline::FSelection& Selection = Pinned->GetSelection();
	if (Selection.Layer != INDEX_NONE && Selection.Layer < CountTracks())
	{
		return GetTrackGroup(Selection.Layer);
	}
	return Selection.Group >= 0 && Selection.Group < CountGroups() ? Selection.Group : INDEX_NONE;
}

// =====================================================================================================
// The panel side: which panel was used last, and running a script
// =====================================================================================================

namespace WidgetAnimScripting
{
	namespace
	{
		/** Panels in the order they were last worked in, the last one most recent. */
		TArray<TWeakPtr<SWidgetAnimTimeline>>& GetUsedPanels()
		{
			static TArray<TWeakPtr<SWidgetAnimTimeline>> Panels;
			return Panels;
		}

		/** What the panel's Run hands to the Python half, and what comes back. */
		struct FPanelRunState
		{
			FString Code;
			TWeakObjectPtr<UWidgetAnimScriptAnimation> Animation;
			bool bFailed = false;
			FString Error;
			int32 ErrorLine = 0;
			FString ErrorFile;
		};

		FPanelRunState& GetPanelRun()
		{
			static FPanelRunState State;
			return State;
		}
	}

	void NotePanelUsed(const TSharedRef<SWidgetAnimTimeline>& Panel)
	{
		TArray<TWeakPtr<SWidgetAnimTimeline>>& Panels = GetUsedPanels();
		const SWidgetAnimTimeline* Used = &Panel.Get();
		if (Panels.Num() > 0 && Panels.Last().Pin().Get() == Used)
		{
			return;
		}
		Panels.RemoveAll([Used](const TWeakPtr<SWidgetAnimTimeline>& Weak) { return !Weak.IsValid() || Weak.Pin().Get() == Used; });
		Panels.Add(TWeakPtr<SWidgetAnimTimeline>(Panel));
	}

	TSharedPtr<SWidgetAnimTimeline> FindLastUsedPanelFor(const IWidgetAnimPanelSource* Source)
	{
		TArray<TWeakPtr<SWidgetAnimTimeline>>& Panels = GetUsedPanels();
		for (int32 Index = Panels.Num() - 1; Index >= 0; --Index)
		{
			if (const TSharedPtr<SWidgetAnimTimeline> Pinned = Panels[Index].Pin())
			{
				if (Source && Pinned->GetSource() == Source)
				{
					return Pinned;
				}
			}
		}
		return nullptr;
	}

	TSharedPtr<SWidgetAnimTimeline> FindLastUsedPanel()
	{
		TArray<TWeakPtr<SWidgetAnimTimeline>>& Panels = GetUsedPanels();
		for (int32 Index = Panels.Num() - 1; Index >= 0; --Index)
		{
			if (const TSharedPtr<SWidgetAnimTimeline> Pinned = Panels[Index].Pin())
			{
				if (Pinned->GetSource())
				{
					return Pinned;
				}
			}
		}
		return nullptr;
	}

	// ---------------------------------------------------------------------------------------------
	// Python, reached through reflection
	//
	// The Python Editor Script Plugin is optional: a project without it must still build and load this plugin. So nothing
	// here links to it or includes its headers - its Blueprint library (UPythonScriptLibrary) is found by name and its
	// functions called through reflection. With the plugin off, the class isn't there, and the Script area says so.
	// ---------------------------------------------------------------------------------------------

	namespace
	{
		UClass* GetPythonLibrary()
		{
			if (!FModuleManager::Get().IsModuleLoaded(TEXT("PythonScriptPlugin")))
			{
				return nullptr;
			}
			return FindObject<UClass>(FTopLevelAssetPath(TEXT("/Script/PythonScriptPlugin"), TEXT("PythonScriptLibrary")));
		}

		/** Calls a static function of Class by reflection: Fill sets its parameters, Read reads them back after. False if there is no such function. */
		bool CallStatic(UClass* Class, FName FunctionName, TFunctionRef<void(UFunction*, uint8*)> Fill, TFunctionRef<void(UFunction*, uint8*)> Read)
		{
			UFunction* Function = Class ? Class->FindFunctionByName(FunctionName) : nullptr;
			if (!Function)
			{
				return false;
			}

			uint8* Params = static_cast<uint8*>(FMemory::Malloc(FMath::Max<int32>(Function->ParmsSize, 1), Function->GetMinAlignment()));
			FMemory::Memzero(Params, FMath::Max<int32>(Function->ParmsSize, 1));
			for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
			{
				It->InitializeValue_InContainer(Params);
			}

			Fill(Function, Params);
			Class->GetDefaultObject()->ProcessEvent(Function, Params);
			Read(Function, Params);

			for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
			{
				It->DestroyValue_InContainer(Params);
			}
			FMemory::Free(Params);
			return true;
		}

		bool ReadReturnBool(UFunction* Function, uint8* Params)
		{
			for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
			{
				if (It->HasAnyPropertyFlags(CPF_ReturnParm))
				{
					if (const FBoolProperty* Bool = CastField<FBoolProperty>(*It))
					{
						return Bool->GetPropertyValue_InContainer(Params);
					}
				}
			}
			return false;
		}

		/** The first string parameter that isn't the return value: the command. */
		void FillCommand(UFunction* Function, uint8* Params, const FString& Command)
		{
			for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
			{
				if (const FStrProperty* String = CastField<FStrProperty>(*It); String && !It->HasAnyPropertyFlags(CPF_ReturnParm))
				{
					String->SetPropertyValue_InContainer(Params, Command);
					return;
				}
			}
		}

		/** The LogOutput list of ExecutePythonCommandEx - entries of a type (Info, Warning, Error) and a line of output. */
		void ReadLogOutput(UFunction* Function, uint8* Params, TArray<FOutputLine>& OutOutput)
		{
			for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
			{
				const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(*It);
				const FStructProperty* Inner = ArrayProperty ? CastField<FStructProperty>(ArrayProperty->Inner) : nullptr;
				if (!Inner)
				{
					continue;
				}

				const FStrProperty* TextProperty = nullptr;
				const FProperty* TypeProperty = nullptr;
				for (TFieldIterator<FProperty> Field(Inner->Struct); Field; ++Field)
				{
					if (!TextProperty && CastField<FStrProperty>(*Field))
					{
						TextProperty = CastField<FStrProperty>(*Field);
					}
					else if (!TypeProperty && (CastField<FEnumProperty>(*Field) || CastField<FByteProperty>(*Field)))
					{
						TypeProperty = *Field;
					}
				}
				if (!TextProperty)
				{
					continue;
				}

				FScriptArrayHelper Helper(ArrayProperty, ArrayProperty->ContainerPtrToValuePtr<void>(Params));
				for (int32 Index = 0; Index < Helper.Num(); ++Index)
				{
					const uint8* Entry = Helper.GetRawPtr(Index);
					uint64 Type = 0;
					if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(TypeProperty))
					{
						Type = EnumProperty->GetUnderlyingProperty()->GetUnsignedIntPropertyValue(EnumProperty->ContainerPtrToValuePtr<void>(Entry));
					}
					else if (const FByteProperty* Byte = CastField<FByteProperty>(TypeProperty))
					{
						Type = Byte->GetPropertyValue_InContainer(Entry);
					}

					// EPythonLogOutputType: Info, Warning, Error.
					FOutputLine Line;
					Line.Kind = Type == 2 ? FOutputLine::EKind::Error : (Type == 1 ? FOutputLine::EKind::Warning : FOutputLine::EKind::Info);
					Line.Text = TextProperty->GetPropertyValue_InContainer(Entry);
					OutOutput.Add(MoveTemp(Line));
				}
				return;
			}
		}

		/** Catches what Python logs while a command runs, for when only the plain Execute Python Command is there. */
		class FPythonLogCapture : public FOutputDevice
		{
		public:
			explicit FPythonLogCapture(TArray<FOutputLine>& InLines) : Lines(InLines) {}

			virtual void Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category) override
			{
				static const FName PythonCategory(TEXT("LogPython"));
				if (Category == PythonCategory)
				{
					FOutputLine Line;
					Line.Kind = Verbosity <= ELogVerbosity::Error ? FOutputLine::EKind::Error
						: (Verbosity == ELogVerbosity::Warning ? FOutputLine::EKind::Warning : FOutputLine::EKind::Info);
					Line.Text = Message;
					Lines.Add(MoveTemp(Line));
				}
			}

		private:
			TArray<FOutputLine>& Lines;
		};

		/** A command that finds widget_anim_scripting - in this plugin's Content/Python, put on the path in case Unreal hasn't - and then does Call. */
		FString MakeCommand(const FString& Call)
		{
			FString Folder;
			if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("NameTBD")))
			{
				Folder = FPaths::ConvertRelativePathToFull(Plugin->GetContentDir() / TEXT("Python"));
			}
			Folder.ReplaceInline(TEXT("\\"), TEXT("/"));
			Folder.ReplaceInline(TEXT("'"), TEXT("\\'"));

			return FString::Printf(
				TEXT("import sys\n")
				TEXT("_widget_anim_folder = '%s'\n")
				TEXT("if _widget_anim_folder and _widget_anim_folder not in sys.path:\n")
				TEXT("    sys.path.append(_widget_anim_folder)\n")
				TEXT("import widget_anim_scripting\n")
				TEXT("%s\n"),
				*Folder, *Call);
		}
	}

	bool IsPythonAvailable()
	{
		UClass* Library = GetPythonLibrary();
		if (!Library)
		{
			return false;
		}

		// Python can be turned off with the plugin still loaded (-DisablePython): the library says so. An engine whose library
		// can't be asked is taken at its word that the plugin is there.
		bool bAvailable = false;
		const bool bCalled = CallStatic(Library, TEXT("IsPythonAvailable"),
			[](UFunction*, uint8*) {},
			[&bAvailable](UFunction* Function, uint8* Params) { bAvailable = ReadReturnBool(Function, Params); });
		return bCalled ? bAvailable : true;
	}

	bool RunPython(const FString& Command, TArray<FOutputLine>& OutOutput)
	{
		UClass* Library = GetPythonLibrary();
		if (!Library)
		{
			return false;
		}

		// The advanced form hands back what the command printed, line by line.
		bool bSucceeded = false;
		if (CallStatic(Library, TEXT("ExecutePythonCommandEx"),
			[&Command](UFunction* Function, uint8* Params) { FillCommand(Function, Params, Command); },
			[&bSucceeded, &OutOutput](UFunction* Function, uint8* Params)
			{
				bSucceeded = ReadReturnBool(Function, Params);
				ReadLogOutput(Function, Params, OutOutput);
			}))
		{
			return bSucceeded;
		}

		// Without it, the plain form, and the log listened to while it runs.
		FPythonLogCapture Capture(OutOutput);
		GLog->AddOutputDevice(&Capture);
		const bool bCalled = CallStatic(Library, TEXT("ExecutePythonCommand"),
			[&Command](UFunction* Function, uint8* Params) { FillCommand(Function, Params, Command); },
			[&bSucceeded](UFunction* Function, uint8* Params) { bSucceeded = ReadReturnBool(Function, Params); });
		GLog->RemoveOutputDevice(&Capture);
		return bCalled && bSucceeded;
	}

	bool RunScriptingCall(const FString& Call, TArray<FOutputLine>& OutOutput)
	{
		return RunPython(MakeCommand(Call), OutOutput);
	}

	namespace
	{
		/** The transaction an undo would undo now - to tell whether a run made one. */
		const FTransaction* GetTopUndoable()
		{
			UTransactor* Transactor = nullptr;
			if (GEditor)
			{
				Transactor = GEditor->Trans;
			}
			if (!Transactor)
			{
				return nullptr;
			}
			const int32 Index = Transactor->GetQueueLength() - Transactor->GetUndoCount() - 1;
			return Index >= 0 ? Transactor->GetTransaction(Index) : nullptr;
		}
	}

	FRunResult RunPanelScript(const TSharedRef<SWidgetAnimTimeline>& Panel, const FString& Code)
	{
		FRunResult Result;

		IWidgetAnimPanelSource* PanelSource = Panel->GetSource();
		if (!PanelSource)
		{
			Result.bFailed = true;
			Result.Error = TEXT("There is no animation open in the panel.");
			return Result;
		}
		if (!IsPythonAvailable())
		{
			Result.bFailed = true;
			Result.Error = TEXT("Python isn't available: turn on the Python Editor Script Plugin and restart the editor.");
			return Result;
		}

		TStrongObjectPtr<UWidgetAnimScriptAnimation> Animation(NewObject<UWidgetAnimScriptAnimation>(GetTransientPackage()));
		Animation->InitForPanel(PanelSource->AsShared(), Panel);
		Result.TracksBefore = Animation->CountTracks();
		Result.GroupsBefore = Animation->CountGroups();

		if (!Animation->IsEditable())
		{
			Result.bFailed = true;
			Result.Error = PanelSource->IsReadOnly() ? TEXT("The panel is locked: unlock it to run a script.") : TEXT("This animation can't be edited here.");
			return Result;
		}

		FPanelRunState& Run = GetPanelRun();
		Run = FPanelRunState();
		Run.Code = Code;
		Run.Animation = Animation.Get();

		// One run, one undoable edit - and the panel told once, at the end.
		static const FString RunTitle(TEXT("Run Widget Animation Script"));
		const FTransaction* TopBefore = GetTopUndoable();
		const double StartTime = FPlatformTime::Seconds();

		Animation->BeginEdit(RunTitle);
		const bool bExecuted = RunPython(MakeCommand(TEXT("widget_anim_scripting._run_panel_script()")), Result.Output);
		Animation->EndEdit();

		Result.Seconds = FPlatformTime::Seconds() - StartTime;
		Result.bRan = bExecuted;
		Result.bFailed = Run.bFailed || !bExecuted;
		Result.Error = Run.Error;
		Result.ErrorLine = Run.ErrorLine;
		Result.ErrorFile = Run.ErrorFile;
		if (Result.bFailed && Result.Error.IsEmpty())
		{
			// Failed before the script could say why - the module didn't load, say. Its last error line says what happened.
			for (int32 Index = Result.Output.Num() - 1; Index >= 0; --Index)
			{
				if (Result.Output[Index].Kind == FOutputLine::EKind::Error)
				{
					Result.Error = Result.Output[Index].Text.TrimStartAndEnd();
					break;
				}
			}
			if (Result.Error.IsEmpty())
			{
				Result.Error = TEXT("The script couldn't run.");
			}
		}
		Run = FPanelRunState();

		// A run that stopped with an error is taken back whole: half a pattern is never what was wanted.
		if (Result.bFailed && GEditor)
		{
			const FTransaction* TopAfter = GetTopUndoable();
			if (TopAfter && TopAfter != TopBefore && TopAfter->GetTitle().ToString() == RunTitle)
			{
				GEditor->UndoTransaction();
				Result.bRolledBack = true;
			}
		}

		Result.TracksAfter = Animation->CountTracks();
		Result.GroupsAfter = Animation->CountGroups();
		return Result;
	}

	FString GetProjectSnippetFolder()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("Python/WidgetAnimSnippets"));
	}

	FString GetFunctionsFile(bool bCreate)
	{
		const FString File = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("Python/WidgetAnimFunctions/my_functions.py"));
		if (bCreate && !IFileManager::Get().FileExists(*File))
		{
			// Started from the plugin's own starter, which shows how a function is written.
			FString Starter;
			if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("NameTBD")))
			{
				FFileHelper::LoadFileToString(Starter, *(Plugin->GetContentDir() / TEXT("Python/Templates/my_functions.py")));
			}
			if (Starter.IsEmpty())
			{
				Starter = TEXT("# Your own functions for Widget Animations scripts: what this file defines can be used in any script by name.\n");
			}
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), /*Tree=*/true);
			FFileHelper::SaveStringToFile(Starter, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		}
		return File;
	}

	void GetSnippetFolders(TArray<FString>& OutFolders)
	{
		OutFolders.Reset();
		if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("NameTBD")))
		{
			OutFolders.Add(FPaths::ConvertRelativePathToFull(Plugin->GetContentDir() / TEXT("Python/WidgetAnimSnippets")));
		}
		OutFolders.Add(FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("Python/WidgetAnimSnippets")));
	}
}

// =====================================================================================================
// UWidgetAnimScriptLibrary
// =====================================================================================================

UWidgetAnimScriptAnimation* UWidgetAnimScriptLibrary::GetOpenAnimation()
{
	const TSharedPtr<SWidgetAnimTimeline> Panel = WidgetAnimScripting::FindLastUsedPanel();
	IWidgetAnimPanelSource* PanelSource = Panel ? Panel->GetSource() : nullptr;
	if (!PanelSource)
	{
		return nullptr;
	}
	UWidgetAnimScriptAnimation* Animation = NewObject<UWidgetAnimScriptAnimation>(GetTransientPackage());
	Animation->InitForPanel(PanelSource->AsShared(), Panel);
	return Animation;
}

UWidgetAnimScriptAnimation* UWidgetAnimScriptLibrary::EditAnimationAsset(UWidgetAnimPresetAsset* Animation)
{
	if (!Animation)
	{
		return nullptr;
	}
	UWidgetAnimScriptAnimation* Scripted = NewObject<UWidgetAnimScriptAnimation>(GetTransientPackage());
	Scripted->InitForAsset(Animation);
	return Scripted;
}

FString UWidgetAnimScriptLibrary::GetPanelScriptCode()
{
	return WidgetAnimScripting::GetPanelRun().Code;
}

UWidgetAnimScriptAnimation* UWidgetAnimScriptLibrary::GetPanelScriptAnimation()
{
	return WidgetAnimScripting::GetPanelRun().Animation.Get();
}

void UWidgetAnimScriptLibrary::ReportPanelScriptError(const FString& Message, int32 Line)
{
	WidgetAnimScripting::FPanelRunState& Run = WidgetAnimScripting::GetPanelRun();
	Run.bFailed = true;
	Run.Error = Message;
	Run.ErrorLine = Line;
}

void UWidgetAnimScriptLibrary::ReportPanelScriptErrorIn(const FString& Message, int32 Line, const FString& File)
{
	ReportPanelScriptError(Message, Line);
	WidgetAnimScripting::GetPanelRun().ErrorFile = File;
}

#undef LOCTEXT_NAMESPACE
