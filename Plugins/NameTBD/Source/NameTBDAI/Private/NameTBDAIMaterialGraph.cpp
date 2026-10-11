// Copyright AR GameSystems 2026. All rights reserved.

#include "NameTBDAIMaterialGraph.h"

#include "HAL/FileManager.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NameTBDAIDocs.h"
#include "NameTBDAIHelpers.h"
#include "NameTBDAIJson.h"
#include "NameTBDAILibrary.h"
#include "ShaderCore.h"
#include "UObject/UObjectIterator.h"

using namespace NameTBDAIJson;

namespace NameTBDAIMaterialGraph
{
	namespace
	{
		// =============================================================================================================
		// The catalog (Resources/AI/MaterialNodes.json) and the macros (Resources/AI/MaterialMacros.json)
		// =============================================================================================================

		struct FPinSpec
		{
			FString Name;
			TArray<FString> Aka;
			bool bOptional = false;

			/** "", "any", "1", "2", "3", "1|out". */
			FString Width;
		};

		struct FNodeSpec
		{
			FString Name;
			UClass* Class = nullptr;
			TArray<FString> Aka;
			FString About;
			FString Category;
			TArray<FPinSpec> Pins;

			/** Width rule: a number, "max:A,B", "same:Input", "sum:A,B", "mask", "list", "custom", "function", "input". */
			FString Out;

			/** Normalized alias -> the expression's own property name. */
			TMap<FString, FString> PropAliases;
			bool bHidden = false;
		};

		struct FCatalog
		{
			TArray<FNodeSpec> Nodes;
			TMap<FString, int32> ByName;
			TSet<FString> UiErrors;
			TSet<FString> UiWarnings;
			TArray<TPair<FString, FString>> Hints;
			TMap<FString, TSharedPtr<FJsonObject>> Macros;
			TMap<FString, FString> MacroNames;
		};

		UClass* FindExpressionClass(const FString& ShortName)
		{
			const FString Clean = ShortName.Replace(TEXT(" "), TEXT(""));
			UClass* Class = FindFirstObject<UClass>(*(TEXT("MaterialExpression") + Clean), EFindFirstObjectOptions::None);
			if (!Class)
			{
				Class = NameTBDAIJson::FindClass(TEXT("MaterialExpression") + Clean, UMaterialExpression::StaticClass());
			}
			if (!Class)
			{
				Class = NameTBDAIJson::FindClass(Clean, UMaterialExpression::StaticClass());
			}
			if (Class && (!Class->IsChildOf(UMaterialExpression::StaticClass()) || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated)))
			{
				return nullptr;
			}
			return Class;
		}

		FString ShortClassName(const UClass* Class)
		{
			return Class ? Class->GetName().Replace(TEXT("MaterialExpression"), TEXT("")) : FString();
		}

		FDateTime NewestStamp(const TArray<FString>& Files)
		{
			FDateTime Newest;
			for (const FString& File : Files)
			{
				Newest = FMath::Max(Newest, IFileManager::Get().GetTimeStamp(*File));
			}
			return Newest;
		}

		const FCatalog& GetCatalog()
		{
			static FCatalog Catalog;
			static FDateTime Stamp;
			static bool bLoaded = false;
			const FString NodesFile = NameTBDAIDocs::GetResourcesDir() / TEXT("MaterialNodes.json");
			const FString MacrosFile = NameTBDAIDocs::GetResourcesDir() / TEXT("MaterialMacros.json");
			const FDateTime Newest = NewestStamp({ NodesFile, MacrosFile });
			if (bLoaded && Newest == Stamp)
			{
				return Catalog;
			}
			bLoaded = true;
			Stamp = Newest;
			Catalog = FCatalog();

			FString Text;
			FFileHelper::LoadFileToString(Text, *NodesFile);
			const TSharedPtr<FJsonObject> Json = ParseObject(Text);
			if (!Json.IsValid())
			{
				UE_LOG(LogTemp, Warning, TEXT("NameTBD AI: Resources/AI/MaterialNodes.json is missing or isn't valid JSON - material graphs are checked by reflection only."));
			}
			if (TSharedPtr<FJsonObject> Nodes = GetObject(Json, TEXT("nodes")))
			{
				for (const auto& Pair : Nodes->Values)
				{
					const TSharedPtr<FJsonObject> Entry = Pair.Value->Type == EJson::Object ? Pair.Value->AsObject() : nullptr;
					if (!Entry.IsValid())
					{
						continue;
					}
					FNodeSpec Spec;
					Spec.Name = Pair.Key;
					Spec.Class = FindExpressionClass(GetString(Entry, TEXT("class"), Pair.Key));
					Spec.Aka = GetStringArray(Entry, TEXT("aka"));
					Spec.About = GetString(Entry, TEXT("about"));
					Spec.Category = GetString(Entry, TEXT("cat"));
					Spec.Out = GetString(Entry, TEXT("out"));
					Spec.bHidden = GetBool(Entry, TEXT("hide"));
					for (const TSharedPtr<FJsonValue>& PinValue : GetArray(Entry, TEXT("in")))
					{
						const TSharedPtr<FJsonObject> PinJson = PinValue->Type == EJson::Object ? PinValue->AsObject() : nullptr;
						if (!PinJson.IsValid())
						{
							continue;
						}
						FPinSpec Pin;
						Pin.Name = GetString(PinJson, TEXT("pin"));
						Pin.Aka = GetStringArray(PinJson, TEXT("aka"));
						Pin.bOptional = PinJson->HasField(TEXT("def"));
						Pin.Width = GetString(PinJson, TEXT("w"));
						Spec.Pins.Add(MoveTemp(Pin));
					}
					if (TSharedPtr<FJsonObject> Props = GetObject(Entry, TEXT("props")))
					{
						for (const auto& Prop : Props->Values)
						{
							Spec.PropAliases.Add(Normalize(Prop.Key), Prop.Value->AsString());
						}
					}
					const int32 Index = Catalog.Nodes.Add(MoveTemp(Spec));
					const FNodeSpec& Added = Catalog.Nodes[Index];
					Catalog.ByName.Add(Normalize(Added.Name), Index);
					for (const FString& Alias : Added.Aka)
					{
						if (!Catalog.ByName.Contains(Normalize(Alias)))
						{
							Catalog.ByName.Add(Normalize(Alias), Index);
						}
					}
					if (!Added.Class && !Added.bHidden)
					{
						UE_LOG(LogTemp, Log, TEXT("NameTBD AI: the material node '%s' isn't in this engine version."), *Added.Name);
					}
				}
			}
			for (const FString& Name : GetStringArray(Json, TEXT("ui_errors")))
			{
				Catalog.UiErrors.Add(Normalize(Name));
			}
			for (const FString& Name : GetStringArray(Json, TEXT("ui_warnings")))
			{
				Catalog.UiWarnings.Add(Normalize(Name));
			}
			for (const TSharedPtr<FJsonValue>& HintValue : GetArray(Json, TEXT("hints")))
			{
				if (HintValue->Type == EJson::Object)
				{
					Catalog.Hints.Emplace(GetString(HintValue->AsObject(), TEXT("match")).ToLower(), GetString(HintValue->AsObject(), TEXT("hint")));
				}
			}

			Text.Reset();
			FFileHelper::LoadFileToString(Text, *MacrosFile);
			if (TSharedPtr<FJsonObject> Macros = GetObject(ParseObject(Text), TEXT("macros")))
			{
				for (const auto& Pair : Macros->Values)
				{
					if (Pair.Value->Type == EJson::Object)
					{
						Catalog.Macros.Add(Normalize(Pair.Key), Pair.Value->AsObject());
						Catalog.MacroNames.Add(Normalize(Pair.Key), Pair.Key);
					}
				}
			}
			return Catalog;
		}

		const FNodeSpec* FindSpec(const FString& Type)
		{
			const FCatalog& Catalog = GetCatalog();
			const int32* Index = Catalog.ByName.Find(Normalize(Type));
			return Index ? &Catalog.Nodes[*Index] : nullptr;
		}

		const FNodeSpec* FindSpecForClass(const UClass* Class)
		{
			for (const FNodeSpec& Spec : GetCatalog().Nodes)
			{
				if (Spec.Class == Class)
				{
					return &Spec;
				}
			}
			return nullptr;
		}

		// =============================================================================================================
		// Small helpers
		// =============================================================================================================

		int32 EditDistance(const FString& A, const FString& B)
		{
			TArray<int32> Previous;
			TArray<int32> Row;
			Previous.SetNum(B.Len() + 1);
			Row.SetNum(B.Len() + 1);
			for (int32 J = 0; J <= B.Len(); ++J)
			{
				Previous[J] = J;
			}
			for (int32 I = 1; I <= A.Len(); ++I)
			{
				Row[0] = I;
				for (int32 J = 1; J <= B.Len(); ++J)
				{
					Row[J] = FMath::Min3(Previous[J] + 1, Row[J - 1] + 1, Previous[J - 1] + (A[I - 1] == B[J - 1] ? 0 : 1));
				}
				Swap(Previous, Row);
			}
			return Previous[B.Len()];
		}

		/** How close Wanted is to Candidate: the distance to the whole name or to one of its words, whichever is less. */
		int32 Closeness(const FString& Wanted, const FString& Candidate)
		{
			int32 Best = EditDistance(Wanted, Normalize(Candidate));
			TArray<FString> Words;
			Candidate.Replace(TEXT("_"), TEXT(" ")).ParseIntoArray(Words, TEXT(" "));
			for (const FString& Word : Words)
			{
				if (Words.Num() > 1)
				{
					Best = FMath::Min(Best, EditDistance(Wanted, Normalize(Word)) + 1);
				}
			}
			return Best;
		}

		/** Up to three names close to Wanted. */
		FString Suggest(const FString& Wanted, const TArray<FString>& Candidates)
		{
			const FString Key = Normalize(Wanted);
			TArray<TPair<int32, FString>> Scored;
			for (const FString& Candidate : Candidates)
			{
				const int32 Score = Closeness(Key, Candidate);
				if (Score <= FMath::Max(2, Key.Len() / 3) || Normalize(Candidate).Contains(Key) || (Key.Len() > 3 && Key.Contains(Normalize(Candidate))))
				{
					Scored.Emplace(Score, Candidate);
				}
			}
			Scored.Sort([](const TPair<int32, FString>& A, const TPair<int32, FString>& B) { return A.Key < B.Key; });
			TArray<FString> Names;
			for (const TPair<int32, FString>& Each : Scored)
			{
				Names.AddUnique(Each.Value);
				if (Names.Num() == 3)
				{
					break;
				}
			}
			return Names.Num() > 0 ? FString::Printf(TEXT(" - did you mean %s?"), *FString::Join(Names, TEXT(", "))) : FString();
		}

		TArray<FString> AllTypeNames()
		{
			TArray<FString> Names;
			const FCatalog& Catalog = GetCatalog();
			for (const FNodeSpec& Spec : Catalog.Nodes)
			{
				if (!Spec.bHidden && Spec.Class)
				{
					Names.Add(Spec.Name);
					Names.Append(Spec.Aka);
				}
			}
			for (const auto& Pair : Catalog.MacroNames)
			{
				Names.Add(Pair.Value);
			}
			return Names;
		}

		bool IsNumberText(const FString& Text)
		{
			double Number = 0.0;
			const FString Trimmed = Text.TrimStartAndEnd();
			return !Trimmed.IsEmpty() && (FChar::IsDigit(Trimmed[0]) || Trimmed[0] == TEXT('-') || Trimmed[0] == TEXT('.')) && LexTryParseString(Number, *Trimmed);
		}

		bool IsHexColor(const FString& Text)
		{
			if (!Text.StartsWith(TEXT("#")))
			{
				return false;
			}
			const int32 Digits = Text.Len() - 1;
			if (Digits != 3 && Digits != 4 && Digits != 6 && Digits != 8)
			{
				return false;
			}
			for (int32 Index = 1; Index < Text.Len(); ++Index)
			{
				if (!FChar::IsHexDigit(Text[Index]))
				{
					return false;
				}
			}
			return true;
		}

		/** "#RRGGBBAA" with alpha below FF: needs an alpha channel (a float4); else a float3 is the color. */
		bool HexHasAlpha(const FString& Text)
		{
			const FString Digits = Text.Mid(1);
			return (Digits.Len() == 8 && !Digits.Right(2).Equals(TEXT("FF"), ESearchCase::IgnoreCase))
				|| (Digits.Len() == 4 && !Digits.Right(1).Equals(TEXT("F"), ESearchCase::IgnoreCase));
		}

		bool IsNumberArray(const TSharedPtr<FJsonValue>& Value)
		{
			if (!Value.IsValid() || Value->Type != EJson::Array || Value->AsArray().Num() < 1 || Value->AsArray().Num() > 4)
			{
				return false;
			}
			for (const TSharedPtr<FJsonValue>& Item : Value->AsArray())
			{
				if (!Item.IsValid() || Item->Type != EJson::Number)
				{
					return false;
				}
			}
			return true;
		}

		TSharedPtr<FJsonObject> DeepCopy(const TSharedPtr<FJsonObject>& Object)
		{
			return Object.IsValid() ? ParseObject(ToString(Object)) : MakeShared<FJsonObject>();
		}

		/** Objects and lists copied (they may be changed); numbers, strings and bools are never changed in place, so shared. */
		TSharedPtr<FJsonValue> DeepCopy(const TSharedPtr<FJsonValue>& Value)
		{
			if (!Value.IsValid())
			{
				return nullptr;
			}
			if (Value->Type == EJson::Object)
			{
				return MakeShared<FJsonValueObject>(DeepCopy(Value->AsObject()));
			}
			if (Value->Type == EJson::Array)
			{
				TArray<TSharedPtr<FJsonValue>> Items;
				for (const TSharedPtr<FJsonValue>& Item : Value->AsArray())
				{
					Items.Add(DeepCopy(Item));
				}
				return MakeShared<FJsonValueArray>(Items);
			}
			return Value;
		}

		FString StripPinName(const FString& Name)
		{
			FString Left;
			FString Clean = Name;
			if (Clean.Split(TEXT(" ("), &Left, nullptr))
			{
				Clean = Left;
			}
			return Clean.TrimStartAndEnd();
		}

		/** Generic names models give the two inputs of A/B nodes. */
		bool IsGenericAlias(const FString& PinName, const FString& Given)
		{
			const FString Key = Normalize(Given);
			if (PinName == TEXT("A"))
			{
				return Key == TEXT("x") || Key == TEXT("in1") || Key == TEXT("input1") || Key == TEXT("first") || Key == TEXT("left") || Key == TEXT("value1") || Key == TEXT("a1") || Key == TEXT("inputa") || Key == TEXT("lhs");
			}
			if (PinName == TEXT("B"))
			{
				return Key == TEXT("y") || Key == TEXT("in2") || Key == TEXT("input2") || Key == TEXT("second") || Key == TEXT("right") || Key == TEXT("value2") || Key == TEXT("b1") || Key == TEXT("inputb") || Key == TEXT("rhs");
			}
			return false;
		}

		bool IsUVName(const FString& Name)
		{
			const FString Key = Normalize(Name);
			return Key == TEXT("uv") || Key == TEXT("uvs") || Key == TEXT("coordinate") || Key == TEXT("coordinates") || Key == TEXT("texcoord") || Key == TEXT("position");
		}

		/** "rg", "xyz", "a": a channel selection; normalized to RGBA letters. Empty if Name isn't one. */
		FString ChannelSelection(const FString& Name)
		{
			FString Out;
			const FString Key = Normalize(Name);
			if (Key == TEXT("alpha")) return TEXT("A");
			if (Key == TEXT("red")) return TEXT("R");
			if (Key == TEXT("green")) return TEXT("G");
			if (Key == TEXT("blue")) return TEXT("B");
			if (Key.IsEmpty() || Key.Len() > 4)
			{
				return FString();
			}
			for (const TCHAR Char : Key)
			{
				switch (Char)
				{
				case TEXT('r'): case TEXT('x'): Out += TEXT("R"); break;
				case TEXT('g'): case TEXT('y'): Out += TEXT("G"); break;
				case TEXT('b'): case TEXT('z'): Out += TEXT("B"); break;
				case TEXT('a'): case TEXT('w'): Out += TEXT("A"); break;
				default: return FString();
				}
			}
			return Out;
		}

		int32 ChannelIndex(TCHAR Channel)
		{
			return Channel == TEXT('R') ? 0 : Channel == TEXT('G') ? 1 : Channel == TEXT('B') ? 2 : 3;
		}

		// =============================================================================================================
		// Outputs and pins of a node, from the engine (class default objects) and the catalog
		// =============================================================================================================

		struct FOut
		{
			FString Name;

			/** 1-4; -1: the node's own computed width; 0: unknown. */
			int32 Width = 0;
		};

		struct FPinInfo
		{
			FString Name;
			TArray<FString> Names;
			bool bOptional = true;
			FString Width;
		};

		TArray<FOut> OutTableOf(UMaterialExpression* Expression)
		{
			TArray<FOut> Table;
			if (!Expression)
			{
				return Table;
			}
			TArray<FExpressionOutput>& Outputs = Expression->GetOutputs();
			for (int32 Index = 0; Index < Outputs.Num(); ++Index)
			{
				const FExpressionOutput& Output = Outputs[Index];
				FString Channels;
				if (Output.Mask)
				{
					if (Output.MaskR) Channels += TEXT("R");
					if (Output.MaskG) Channels += TEXT("G");
					if (Output.MaskB) Channels += TEXT("B");
					if (Output.MaskA) Channels += TEXT("A");
				}
				FOut Out;
				Out.Name = Output.OutputName.IsNone() ? Channels : Output.OutputName.ToString();
				Out.Width = Output.Mask ? Channels.Len() : (Index == 0 ? -1 : 0);
				Table.Add(Out);
			}
			return Table;
		}

		int32 FindOutputIn(const TArray<FOut>& Table, const FString& Name)
		{
			if (Table.Num() == 0)
			{
				return INDEX_NONE;
			}
			const FString Key = Normalize(Name);
			if (Key.IsEmpty())
			{
				return 0;
			}
			for (int32 Index = 0; Index < Table.Num(); ++Index)
			{
				if (Normalize(Table[Index].Name) == Key)
				{
					return Index;
				}
			}
			const FString Channels = ChannelSelection(Name);
			if (!Channels.IsEmpty())
			{
				for (int32 Index = 0; Index < Table.Num(); ++Index)
				{
					if (Table[Index].Name == Channels)
					{
						return Index;
					}
				}
				return INDEX_NONE;
			}
			if (Key == TEXT("output") || Key == TEXT("result") || Key == TEXT("out") || Key == TEXT("value") || Key == TEXT("color") || Key == TEXT("default") || Key == TEXT("mask"))
			{
				return 0;
			}
			return INDEX_NONE;
		}

		const TArray<FString>& EnginePinNames(UClass* Class)
		{
			static TMap<UClass*, TArray<FString>> Cache;
			if (const TArray<FString>* Found = Cache.Find(Class))
			{
				return *Found;
			}
			TArray<FString> Names;
			if (UMaterialExpression* Default = Class ? Cast<UMaterialExpression>(Class->GetDefaultObject()) : nullptr)
			{
				for (int32 Index = 0; Index < 64; ++Index)
				{
					if (!Default->GetInput(Index))
					{
						break;
					}
					Names.Add(StripPinName(Default->GetInputName(Index).ToString()));
				}
			}
			return Cache.Add(Class, MoveTemp(Names));
		}

		/** A catalog node's pins in the engine's order, the catalog's knowledge (aliases, optional, widths) attached. */
		TArray<FPinInfo> PinsOf(const FNodeSpec* Spec, UClass* Class)
		{
			TArray<FPinInfo> Pins;
			const TArray<FString>& Engine = EnginePinNames(Class);
			TSet<int32> Used;
			for (int32 Index = 0; Index < Engine.Num(); ++Index)
			{
				FPinInfo Info;
				Info.Name = Engine[Index];
				Info.Names.Add(Engine[Index]);
				if (Spec)
				{
					for (int32 SpecIndex = 0; SpecIndex < Spec->Pins.Num(); ++SpecIndex)
					{
						const FPinSpec& Pin = Spec->Pins[SpecIndex];
						bool bMatch = Normalize(Pin.Name) == Normalize(Engine[Index]) || (Engine.Num() == 1 && Spec->Pins.Num() == 1);
						for (const FString& Alias : Pin.Aka)
						{
							bMatch |= Normalize(Alias) == Normalize(Engine[Index]);
						}
						if (bMatch && !Used.Contains(SpecIndex))
						{
							Used.Add(SpecIndex);
							Info.Name = Pin.Name;
							Info.Names.Add(Pin.Name);
							Info.Names.Append(Pin.Aka);
							Info.bOptional = Pin.bOptional;
							Info.Width = Pin.Width;
							break;
						}
					}
				}
				Pins.Add(MoveTemp(Info));
			}
			if (Spec && Engine.Num() > 0)
			{
				for (int32 SpecIndex = 0; SpecIndex < Spec->Pins.Num(); ++SpecIndex)
				{
					if (!Used.Contains(SpecIndex))
					{
						UE_LOG(LogTemp, Log, TEXT("NameTBD AI: %s has no input '%s' in this engine (inputs: %s) - check MaterialNodes.json."),
							*Spec->Name, *Spec->Pins[SpecIndex].Name, *FString::Join(Engine, TEXT(", ")));
					}
				}
			}
			return Pins;
		}

		int32 WidthOfInputType(const FString& Type)
		{
			const FString Key = Normalize(Type);
			if (Key.Contains(TEXT("scalar")) || Key == TEXT("float") || Key == TEXT("float1"))
			{
				return 1;
			}
			if (Key.Contains(TEXT("2")))
			{
				return 2;
			}
			if (Key.Contains(TEXT("3")))
			{
				return 3;
			}
			if (Key.Contains(TEXT("4")))
			{
				return 4;
			}
			return 0;
		}

		/** A library function's inputs and outputs (with the outputs' widths, from checking its own graph). */
		bool FunctionSignature(const FString& Function, int32 Depth, TArray<FPinInfo>& OutPins, TArray<FOut>& OutOuts);

		// =============================================================================================================
		// Prepare
		// =============================================================================================================

		FGraph PrepareAt(const TSharedPtr<FJsonObject>& In, bool bFunction, int32 Depth);

		class FPreparer
		{
		public:
			FPreparer(FGraph& InGraph, int32 InDepth)
				: Out(InGraph)
				, Level(InDepth)
			{
			}

			void Run(const TSharedPtr<FJsonObject>& In)
			{
				TSharedPtr<FJsonObject> Copy = DeepCopy(In);
				NameTBDAIHelpers::ExpandThemeTokens(Copy, NameTBDAIHelpers::IsDark(FString()));
				ReadForm(Copy);
				ExpandMacros();
				Inline();
				MakeNodes();
				MakeWires();
				Order();
				Widths();
				FinishOutputs();
			}

		private:
			FGraph& Out;
			int32 Level = 0;

			// The JSON stage.
			TArray<TSharedPtr<FJsonObject>> JsonNodes;
			TArray<TPair<FString, TSharedPtr<FJsonValue>>> JsonOutputs;
			TArray<TSharedPtr<FJsonObject>> JsonInputs;
			bool bTint = true;
			int32 Made = 0;
			TMap<FString, FString> ParamNodes;
			TMap<FString, FString> ParamKinds;

			// Parallel to Out.Nodes.
			TArray<const FNodeSpec*> Specs;
			TArray<TArray<FPinInfo>> PinTables;
			TArray<TArray<FOut>> OutTables;
			TArray<bool> AnyPins;
			TArray<bool> UnknownPin;

			/** Ids whose node couldn't be made: refs to them aren't reported again. */
			TSet<FString> Failed;
			TArray<int32> Topo;

			// ---- Problems ------------------------------------------------------------------------------------------

			void Error(const FString& Code, const FString& Node, const FString& Message)
			{
				Out.Problems.Add({ Code, Node, Message, true });
			}

			void Warn(const FString& Code, const FString& Node, const FString& Message)
			{
				Out.Problems.Add({ Code, Node, Message, false });
			}

			void Fix(const FString& Message)
			{
				Out.Fixed.AddUnique(Message);
			}

			/** What every material gets (not a slip of the model's): listed apart, not counted as fixes. */
			void Added(const FString& Message)
			{
				Out.Added.AddUnique(Message);
			}

			// ---- 1. Whatever form the model wrote, as one canonical form -------------------------------------------

			static bool IsKnownNodeKey(const FString& Key)
			{
				static const TSet<FString> Known = { TEXT("id"), TEXT("name"), TEXT("type"), TEXT("kind"), TEXT("node"), TEXT("class"), TEXT("op"),
					TEXT("props"), TEXT("settings"), TEXT("properties"), TEXT("inputs"), TEXT("in"), TEXT("pins"), TEXT("function"), TEXT("func"),
					TEXT("code"), TEXT("hlsl"), TEXT("output"), TEXT("outputtype"), TEXT("description"), TEXT("comment"), TEXT("x"), TEXT("y"),
					TEXT("position"), TEXT("pos"), TEXT("macro") };
				return Known.Contains(Normalize(Key));
			}

			/** {"node": "a", "output": "R"} / {"from": "a.R"} / {"value": 1} -> a plain ref; params stay objects. */
			static TSharedPtr<FJsonValue> PlainRef(const TSharedPtr<FJsonValue>& Value)
			{
				if (!Value.IsValid() || Value->Type != EJson::Object)
				{
					return Value;
				}
				const TSharedPtr<FJsonObject> Object = Value->AsObject();
				if (Object->HasField(TEXT("param")) || Object->HasField(TEXT("parameter")))
				{
					return Value;
				}
				FString Node;
				if (Object->TryGetStringField(TEXT("node"), Node) || Object->TryGetStringField(TEXT("from"), Node) || Object->TryGetStringField(TEXT("ref"), Node) || Object->TryGetStringField(TEXT("id"), Node))
				{
					FString Output;
					if (Object->TryGetStringField(TEXT("output"), Output) || Object->TryGetStringField(TEXT("pin"), Output) || Object->TryGetStringField(TEXT("out"), Output))
					{
						Node += TEXT(".") + Output;
					}
					return Str(Node);
				}
				if (TSharedPtr<FJsonValue> Inner = Object->TryGetField(TEXT("value")))
				{
					return Inner;
				}
				return Value;
			}

			void ReadForm(const TSharedPtr<FJsonObject>& In)
			{
				if (!Out.bFunction)
				{
					const FString Domain = Normalize(GetString(In, TEXT("domain"), TEXT("ui")));
					Out.Domain = Domain == TEXT("surface") ? TEXT("Surface") : Domain == TEXT("postprocess") ? TEXT("PostProcess") : TEXT("UI");
					const FString Blend = Normalize(GetString(In, TEXT("blend"), GetString(In, TEXT("blendmode"), TEXT("translucent"))));
					Out.Blend = Blend.Contains(TEXT("add")) || Blend == TEXT("glow") ? TEXT("Additive")
						: Blend.Contains(TEXT("mask")) || Blend == TEXT("cutout") ? TEXT("Masked")
						: Blend == TEXT("opaque") || Blend == TEXT("solid") ? TEXT("Opaque")
						: Blend == TEXT("alphacomposite") || Blend == TEXT("premultiplied") ? TEXT("AlphaComposite") : TEXT("Translucent");
					bool bValue = true;
					if ((In->TryGetBoolField(TEXT("tint"), bValue) || In->TryGetBoolField(TEXT("vertex_color"), bValue)) && !bValue)
					{
						bTint = false;
					}
				}

				// Nodes: a list, or a map by id.
				TArray<TSharedPtr<FJsonValue>> Items;
				const TSharedPtr<FJsonValue> NodesValue = In->TryGetField(TEXT("nodes"));
				if (NodesValue.IsValid() && NodesValue->Type == EJson::Array)
				{
					Items = NodesValue->AsArray();
				}
				else if (NodesValue.IsValid() && NodesValue->Type == EJson::Object)
				{
					for (const auto& Pair : NodesValue->AsObject()->Values)
					{
						if (Pair.Value->Type == EJson::Object)
						{
							TSharedPtr<FJsonObject> Item = Pair.Value->AsObject();
							if (!Item->HasField(TEXT("id")))
							{
								Item->SetStringField(TEXT("id"), Pair.Key);
							}
							Items.Add(MakeShared<FJsonValueObject>(Item));
						}
					}
				}
				for (const TSharedPtr<FJsonValue>& Item : Items)
				{
					TSharedPtr<FJsonObject> Spec;
					if (Item.IsValid() && Item->Type == EJson::Object)
					{
						Spec = Item->AsObject();
					}
					else if (Item.IsValid() && Item->Type == EJson::String)
					{
						Spec = ParseObject(Item->AsString());
					}
					if (!Spec.IsValid())
					{
						Error(TEXT("json"), FString(), TEXT("each node is an object {id, type, props, inputs}"));
						continue;
					}
					TSharedPtr<FJsonObject> Node = MakeShared<FJsonObject>();
					FString Id = GetString(Spec, TEXT("id"));
					const bool bNameIsId = Id.IsEmpty();
					if (bNameIsId)
					{
						Id = GetString(Spec, TEXT("name"));
					}
					FString Type;
					for (const TCHAR* Key : { TEXT("type"), TEXT("kind"), TEXT("node"), TEXT("class"), TEXT("op") })
					{
						if (Type.IsEmpty())
						{
							Type = GetString(Spec, Key);
						}
					}
					Node->SetStringField(TEXT("id"), Id);
					Node->SetStringField(TEXT("type"), Type);

					TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
					for (const TCHAR* Key : { TEXT("props"), TEXT("settings"), TEXT("properties") })
					{
						if (TSharedPtr<FJsonObject> Found = GetObject(Spec, Key))
						{
							for (const auto& Pair : Found->Values)
							{
								Props->SetField(Pair.Key, Pair.Value);
							}
						}
					}
					Node->SetObjectField(TEXT("props"), Props);

					// Settings written straight on the node ({"type": "ScalarParameter", "ParameterName": "Speed"}): used
					// where the node has them, ignored (with a warning) where they were only labels.
					TSharedPtr<FJsonObject> Loose = MakeShared<FJsonObject>();
					for (const auto& Pair : Spec->Values)
					{
						if (!IsKnownNodeKey(Pair.Key))
						{
							Loose->SetField(Pair.Key, Pair.Value);
						}
					}
					if (!bNameIsId && Spec->HasField(TEXT("name")))
					{
						Loose->SetField(TEXT("name"), Spec->TryGetField(TEXT("name")));
					}
					Node->SetObjectField(TEXT("loose"), Loose);

					TSharedPtr<FJsonObject> Inputs = MakeShared<FJsonObject>();
					for (const TCHAR* Key : { TEXT("inputs"), TEXT("in"), TEXT("pins") })
					{
						const TSharedPtr<FJsonValue> Found = Spec->TryGetField(Key);
						if (Found.IsValid() && Found->Type == EJson::Object)
						{
							for (const auto& Pair : Found->AsObject()->Values)
							{
								Inputs->SetField(Pair.Key, PlainRef(Pair.Value));
							}
						}
						else if (Found.IsValid() && Found->Type == EJson::Array)
						{
							// Inputs by position: ["a", 0.5].
							int32 Position = 0;
							for (const TSharedPtr<FJsonValue>& Each : Found->AsArray())
							{
								Inputs->SetField(FString::Printf(TEXT("#%d"), Position++), PlainRef(Each));
							}
						}
					}
					Node->SetObjectField(TEXT("inputs"), Inputs);

					for (const TCHAR* Key : { TEXT("function"), TEXT("func") })
					{
						if (Spec->HasField(Key))
						{
							Node->SetStringField(TEXT("function"), GetString(Spec, Key));
						}
					}
					for (const TCHAR* Key : { TEXT("code"), TEXT("hlsl") })
					{
						if (Spec->HasField(Key))
						{
							Node->SetStringField(TEXT("code"), GetString(Spec, Key));
						}
					}
					for (const TCHAR* Key : { TEXT("output"), TEXT("output_type"), TEXT("outputType") })
					{
						if (Spec->HasField(Key) && Spec->TryGetField(Key)->Type == EJson::String)
						{
							Node->SetStringField(TEXT("output"), GetString(Spec, Key));
						}
					}
					if (Spec->HasField(TEXT("macro")))
					{
						Node->SetStringField(TEXT("macro"), GetString(Spec, TEXT("macro")));
					}
					if (Id.IsEmpty())
					{
						Error(TEXT("json"), FString(), FString::Printf(TEXT("a %s node has no id"), Type.IsEmpty() ? TEXT("typeless") : *Type));
						continue;
					}
					JsonNodes.Add(Node);
				}

				// Outputs.
				TSharedPtr<FJsonObject> Outputs = GetObject(In, TEXT("outputs"));
				if (!Outputs.IsValid())
				{
					Outputs = GetObject(In, TEXT("output"));
				}
				if (Outputs.IsValid())
				{
					for (const auto& Pair : Outputs->Values)
					{
						JsonOutputs.Emplace(Pair.Key, PlainRef(Pair.Value));
					}
				}

				// Wires as a list: [{"from": "a", "to": "b.A"}] or [["a", "b.A"]]; "to" an output: "Material.FinalColor", "output.Opacity", "Opacity".
				for (const TCHAR* Key : { TEXT("connections"), TEXT("edges"), TEXT("links"), TEXT("wires") })
				{
					for (const TSharedPtr<FJsonValue>& Each : GetArray(In, Key))
					{
						TSharedPtr<FJsonValue> From;
						FString To;
						if (Each->Type == EJson::Array && Each->AsArray().Num() == 2)
						{
							From = Each->AsArray()[0];
							To = Each->AsArray()[1]->AsString();
						}
						else if (Each->Type == EJson::Object)
						{
							const TSharedPtr<FJsonObject> Edge = Each->AsObject();
							From = Edge->TryGetField(TEXT("from"));
							if (!From.IsValid())
							{
								From = Edge->TryGetField(TEXT("source"));
							}
							To = GetString(Edge, TEXT("to"), GetString(Edge, TEXT("target")));
							FString FromPin;
							if (From.IsValid() && From->Type == EJson::String && (Edge->TryGetStringField(TEXT("from_output"), FromPin) || Edge->TryGetStringField(TEXT("output"), FromPin)))
							{
								From = Str(From->AsString() + TEXT(".") + FromPin);
							}
							FString ToPin;
							if (Edge->TryGetStringField(TEXT("to_input"), ToPin) || Edge->TryGetStringField(TEXT("input"), ToPin))
							{
								To += TEXT(".") + ToPin;
							}
						}
						if (!From.IsValid() || To.IsEmpty())
						{
							Error(TEXT("json"), FString(), FString::Printf(TEXT("a connection needs from and to: %s"), *ToString(Each)));
							continue;
						}
						FString Node;
						FString Pin;
						if (!To.Split(TEXT("."), &Node, &Pin, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
						{
							Node = FString();
							Pin = To;
						}
						const FString NodeKey = Normalize(Node);
						if (NodeKey.IsEmpty() || NodeKey == TEXT("output") || NodeKey == TEXT("outputs") || NodeKey == TEXT("material") || NodeKey == TEXT("result") || NodeKey == TEXT("root"))
						{
							JsonOutputs.Emplace(Pin, PlainRef(From));
							continue;
						}
						bool bFound = false;
						for (const TSharedPtr<FJsonObject>& Each2 : JsonNodes)
						{
							if (GetString(Each2, TEXT("id")) == Node)
							{
								Each2->GetObjectField(TEXT("inputs"))->SetField(Pin, PlainRef(From));
								bFound = true;
							}
						}
						if (!bFound)
						{
							Error(TEXT("missing_node"), Node, FString::Printf(TEXT("connection to '%s': no node '%s'"), *To, *Node));
						}
					}
				}

				if (Out.bFunction)
				{
					for (const TSharedPtr<FJsonValue>& Each : GetArray(In, TEXT("inputs")))
					{
						if (Each->Type == EJson::Object)
						{
							JsonInputs.Add(Each->AsObject());
						}
					}
				}
			}

			// ---- 2. Macro nodes -> their subgraphs ------------------------------------------------------------------

			static bool IsConstantText(const FString& Text)
			{
				return IsNumberText(Text) || IsHexColor(Text);
			}

			void ExpandMacros()
			{
				const FCatalog& Catalog = GetCatalog();
				for (int32 Round = 0; Round < 4; ++Round)
				{
					// Macro id -> its outputs (name -> inner ref; the first is the default).
					TMap<FString, TArray<TPair<FString, FString>>> OutMaps;
					TArray<TSharedPtr<FJsonObject>> Expanded;
					for (const TSharedPtr<FJsonObject>& Node : JsonNodes)
					{
						const TSharedPtr<FJsonObject>* Macro = Catalog.Macros.Find(Normalize(GetString(Node, TEXT("type"))));
						if (!Macro)
						{
							Expanded.Add(Node);
							continue;
						}
						const FString Id = GetString(Node, TEXT("id"));
						const FString Prefix = Id + TEXT("/");
						const FString MacroName = Catalog.MacroNames.FindRef(Normalize(GetString(Node, TEXT("type"))));

						// The macro's inputs: given, or their defaults.
						TMap<FString, TSharedPtr<FJsonValue>> Values;
						TMap<FString, TSharedPtr<FJsonValue>> Given;
						TMap<FString, FString> GivenNames;
						for (const auto& Pair : Node->GetObjectField(TEXT("inputs"))->Values)
						{
							Given.Add(Normalize(Pair.Key), Pair.Value);
							GivenNames.Add(Normalize(Pair.Key), Pair.Key);
						}
						for (const TCHAR* Key : { TEXT("props"), TEXT("loose") })
						{
							// Inputs written as props, or straight on the node, are inputs too.
							if (TSharedPtr<FJsonObject> Props = GetObject(Node, Key))
							{
								for (const auto& Pair : Props->Values)
								{
									if (!Given.Contains(Normalize(Pair.Key)) && Normalize(Pair.Key) != TEXT("name"))
									{
										Given.Add(Normalize(Pair.Key), PlainRef(Pair.Value));
										GivenNames.Add(Normalize(Pair.Key), Pair.Key);
									}
								}
							}
						}
						TArray<FString> InputNames;
						if (TSharedPtr<FJsonObject> InDefs = GetObject(*Macro, TEXT("in")))
						{
							for (const auto& Pair : InDefs->Values)
							{
								InputNames.Add(Pair.Key);
								TSharedPtr<FJsonValue> Value;
								if (TSharedPtr<FJsonValue>* Found = Given.Find(Normalize(Pair.Key)))
								{
									Value = *Found;
									Given.Remove(Normalize(Pair.Key));
								}
								else if (Pair.Value->Type == EJson::Object)
								{
									Value = DeepCopy(Pair.Value->AsObject()->TryGetField(TEXT("def")));
								}
								Values.Add(Normalize(Pair.Key), Value.IsValid() ? Value : Num(0.0));
							}
						}
						for (const auto& Pair : Given)
						{
							Error(TEXT("unknown_pin"), Id, FString::Printf(TEXT("%s has no input '%s' (inputs: %s)"), *MacroName, *GivenNames.FindRef(Pair.Key), *FString::Join(InputNames, TEXT(", "))));
						}

						for (const TSharedPtr<FJsonValue>& InnerValue : GetArray(*Macro, TEXT("nodes")))
						{
							const TSharedPtr<FJsonObject> Inner = DeepCopy(InnerValue->AsObject());
							Inner->SetStringField(TEXT("id"), Prefix + GetString(Inner, TEXT("id")));
							if (!Inner->HasField(TEXT("macro")))
							{
								Inner->SetStringField(TEXT("macro"), Id);
							}
							if (!Inner->HasField(TEXT("props")))
							{
								Inner->SetObjectField(TEXT("props"), MakeShared<FJsonObject>());
							}
							TSharedPtr<FJsonObject> Inputs = MakeShared<FJsonObject>();
							if (TSharedPtr<FJsonObject> InnerInputs = GetObject(Inner, TEXT("inputs")))
							{
								for (const auto& Pair : InnerInputs->Values)
								{
									FString Text;
									if (Pair.Value->TryGetString(Text) && Text.StartsWith(TEXT("$")))
									{
										const FString Key = Normalize(Text.Mid(1));
										const TSharedPtr<FJsonValue>* Value = Values.Find(Key);
										Inputs->SetField(Pair.Key, Value ? DeepCopy(*Value) : Str(Text));
									}
									else if (Pair.Value->TryGetString(Text) && !IsConstantText(Text))
									{
										Inputs->SetField(Pair.Key, Str(Prefix + Text));
									}
									else
									{
										Inputs->SetField(Pair.Key, Pair.Value);
									}
								}
							}
							Inner->SetObjectField(TEXT("inputs"), Inputs);
							Expanded.Add(Inner);
						}

						TArray<TPair<FString, FString>>& Map = OutMaps.Add(Id);
						if (TSharedPtr<FJsonObject> MacroOuts = GetObject(*Macro, TEXT("out")))
						{
							for (const auto& Pair : MacroOuts->Values)
							{
								Map.Emplace(Pair.Key, Prefix + Pair.Value->AsString());
							}
						}
					}
					if (OutMaps.Num() == 0)
					{
						return;
					}
					JsonNodes = MoveTemp(Expanded);

					// Refs to a macro node -> the inner node behind the named output.
					auto Rewrite = [this, &OutMaps](const TSharedPtr<FJsonValue>& Value) -> TSharedPtr<FJsonValue>
					{
						FString Text;
						if (!Value.IsValid() || !Value->TryGetString(Text) || IsConstantText(Text))
						{
							return Value;
						}
						FString Node = Text;
						FString Output;
						if (!OutMaps.Contains(Node))
						{
							Text.Split(TEXT("."), &Node, &Output, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
						}
						const TArray<TPair<FString, FString>>* Map = OutMaps.Find(Node);
						if (!Map || Map->Num() == 0)
						{
							return Value;
						}
						if (Output.IsEmpty())
						{
							return Str((*Map)[0].Value);
						}
						for (const TPair<FString, FString>& Pair : *Map)
						{
							if (Normalize(Pair.Key) == Normalize(Output))
							{
								return Str(Pair.Value);
							}
						}
						TArray<FString> Names;
						for (const TPair<FString, FString>& Pair : *Map)
						{
							Names.Add(Pair.Key);
						}
						Error(TEXT("unknown_output"), Node, FString::Printf(TEXT("'%s': this macro's outputs are %s"), *Text, *FString::Join(Names, TEXT(", "))));
						return Str((*Map)[0].Value);
					};
					for (const TSharedPtr<FJsonObject>& Node : JsonNodes)
					{
						for (auto& Pair : Node->GetObjectField(TEXT("inputs"))->Values)
						{
							Pair.Value = Rewrite(Pair.Value);
						}
					}
					for (TPair<FString, TSharedPtr<FJsonValue>>& Pair : JsonOutputs)
					{
						Pair.Value = Rewrite(Pair.Value);
					}
				}
			}

			// ---- 3. Constants, parameters and UV / Time written into inputs -> nodes -------------------------------

			/** A node of the graph by id - a function graph's own inputs included. */
			bool HasJsonNode(const FString& Id) const
			{
				return JsonNodes.ContainsByPredicate([&Id](const TSharedPtr<FJsonObject>& Node) { return GetString(Node, TEXT("id")) == Id; })
					|| JsonInputs.ContainsByPredicate([&Id](const TSharedPtr<FJsonObject>& Input) { return GetString(Input, TEXT("name")) == Id; });
			}

			FString AddJsonNode(const FString& Id, const FString& Type, const TSharedPtr<FJsonObject>& Props, const TSharedPtr<FJsonObject>& Inputs = nullptr)
			{
				TSharedPtr<FJsonObject> Node = MakeShared<FJsonObject>();
				Node->SetStringField(TEXT("id"), Id);
				Node->SetStringField(TEXT("type"), Type);
				Node->SetObjectField(TEXT("props"), Props.IsValid() ? Props : MakeShared<FJsonObject>());
				Node->SetObjectField(TEXT("inputs"), Inputs.IsValid() ? Inputs : MakeShared<FJsonObject>());
				JsonNodes.Add(Node);
				return Id;
			}

			FString SharedNode(const FString& Id, const FString& Type)
			{
				if (!HasJsonNode(Id))
				{
					AddJsonNode(Id, Type, nullptr);
				}
				return Id;
			}

			FString ConstantNode(const TSharedPtr<FJsonValue>& Value)
			{
				const FString Id = FString::Printf(TEXT("_k%d"), ++Made);
				TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
				FString Text;
				if (Value->Type == EJson::Number || (Value->TryGetString(Text) && IsNumberText(Text)))
				{
					double Number = 0.0;
					if (Value->Type == EJson::Number)
					{
						Number = Value->AsNumber();
					}
					else
					{
						LexTryParseString(Number, *Text.TrimStartAndEnd());
					}
					Props->SetNumberField(TEXT("R"), Number);
					return AddJsonNode(Id, TEXT("Constant"), Props);
				}
				if (Value->Type == EJson::Boolean)
				{
					Props->SetNumberField(TEXT("R"), Value->AsBool() ? 1.0 : 0.0);
					return AddJsonNode(Id, TEXT("Constant"), Props);
				}
				if (Value->Type == EJson::Array)
				{
					const TArray<TSharedPtr<FJsonValue>>& Items = Value->AsArray();
					if (Items.Num() == 1)
					{
						Props->SetNumberField(TEXT("R"), Items[0]->AsNumber());
						return AddJsonNode(Id, TEXT("Constant"), Props);
					}
					if (Items.Num() == 2)
					{
						Props->SetNumberField(TEXT("R"), Items[0]->AsNumber());
						Props->SetNumberField(TEXT("G"), Items[1]->AsNumber());
						return AddJsonNode(Id, TEXT("Constant2Vector"), Props);
					}
					Props->SetField(TEXT("Constant"), Value);
					return AddJsonNode(Id, Items.Num() == 3 ? TEXT("Constant3Vector") : TEXT("Constant4Vector"), Props);
				}
				// "#RRGGBB": the color as a float3 (a float4 only when it has a real alpha).
				Props->SetStringField(TEXT("Constant"), Text);
				return AddJsonNode(Id, HexHasAlpha(Text) ? TEXT("Constant4Vector") : TEXT("Constant3Vector"), Props);
			}

			FString ParamNode(const TSharedPtr<FJsonObject>& Spec, const FString& Where)
			{
				const FString Name = GetString(Spec, TEXT("param"), GetString(Spec, TEXT("parameter")));
				TSharedPtr<FJsonValue> Value = Spec->TryGetField(TEXT("value"));
				if (!Value.IsValid())
				{
					Value = Spec->TryGetField(TEXT("default"));
				}
				if (!Value.IsValid())
				{
					Value = Num(Spec->HasField(TEXT("color")) ? 0.0 : 1.0);
					if (TSharedPtr<FJsonValue> Color = Spec->TryGetField(TEXT("color")))
					{
						Value = Color;
					}
				}
				FString Text;
				const bool bColor = (Value->Type == EJson::String && Value->TryGetString(Text) && !IsNumberText(Text)) || (Value->Type == EJson::Array && Value->AsArray().Num() >= 3);
				const bool bPair = Value->Type == EJson::Array && Value->AsArray().Num() == 2;
				const FString Kind = bColor || bPair ? TEXT("VectorParameter") : TEXT("ScalarParameter");
				if (Name.IsEmpty())
				{
					Error(TEXT("param"), Where, TEXT("a {\"param\": ...} input needs the parameter's name"));
					return ConstantNode(Value);
				}
				if (const FString* Existing = ParamNodes.Find(Name))
				{
					if (ParamKinds.FindRef(Name) != Kind + (bPair ? TEXT("2") : TEXT("")))
					{
						Error(TEXT("param"), Where, FString::Printf(TEXT("the parameter '%s' is used both as a number and as a color - give them two names"), *Name));
					}
					return *Existing;
				}
				const FString Id = TEXT("p_") + Name;
				TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
				Props->SetStringField(TEXT("ParameterName"), Name);
				if (bPair)
				{
					TArray<TSharedPtr<FJsonValue>> Padded = Value->AsArray();
					Padded.Add(Num(0.0));
					Padded.Add(Num(0.0));
					Props->SetField(TEXT("DefaultValue"), MakeShared<FJsonValueArray>(Padded));
				}
				else if (bColor)
				{
					Props->SetField(TEXT("DefaultValue"), Value);
				}
				else
				{
					double Number = 0.0;
					if (Value->Type == EJson::Number)
					{
						Number = Value->AsNumber();
					}
					else if (Value->TryGetString(Text))
					{
						LexTryParseString(Number, *Text);
					}
					Props->SetNumberField(TEXT("DefaultValue"), Number);
					if (Spec->HasField(TEXT("min")))
					{
						Props->SetNumberField(TEXT("SliderMin"), GetNumber(Spec, TEXT("min")));
					}
					if (Spec->HasField(TEXT("max")))
					{
						Props->SetNumberField(TEXT("SliderMax"), GetNumber(Spec, TEXT("max")));
					}
				}
				if (Spec->HasField(TEXT("group")))
				{
					Props->SetStringField(TEXT("Group"), GetString(Spec, TEXT("group")));
				}
				AddJsonNode(Id, Kind, Props);
				FString Ref = Id;
				if (bPair)
				{
					// A float2 parameter: a color parameter's R and G.
					TSharedPtr<FJsonObject> MaskProps = MakeShared<FJsonObject>();
					MaskProps->SetStringField(TEXT("channels"), TEXT("rg"));
					TSharedPtr<FJsonObject> MaskInputs = MakeShared<FJsonObject>();
					MaskInputs->SetStringField(TEXT("Input"), Id);
					Ref = AddJsonNode(Id + TEXT("/rg"), TEXT("ComponentMask"), MaskProps, MaskInputs);
				}
				ParamNodes.Add(Name, Ref);
				ParamKinds.Add(Name, Kind + (bPair ? TEXT("2") : TEXT("")));
				return Ref;
			}

			/** An input's value as a ref to a node: constants and parameters become nodes; "$uv" / "$time" the shared ones. */
			TSharedPtr<FJsonValue> InlineRef(const TSharedPtr<FJsonValue>& Value, const FString& Where)
			{
				if (!Value.IsValid() || Value->IsNull())
				{
					Error(TEXT("json"), Where, TEXT("an input is null - give a node, a number or a color"));
					return nullptr;
				}
				FString Text;
				if (Value->TryGetString(Text) && Value->Type == EJson::String)
				{
					if (IsNumberText(Text) || IsHexColor(Text.TrimStartAndEnd()))
					{
						return Str(ConstantNode(Value));
					}
					// "$uv", "uv", "$uv.x", "$time"...: the shared UV / Time node, with the channel asked for.
					FString Node = Text;
					FString Output;
					Text.Split(TEXT("."), &Node, &Output, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
					if (!HasJsonNode(Text) && !HasJsonNode(Node))
					{
						const FString NodeKey = Normalize(Node);
						const FString Suffix = Output.IsEmpty() ? FString() : TEXT(".") + Output;
						if (NodeKey == TEXT("$uv") || NodeKey == TEXT("$texcoord") || NodeKey == TEXT("uv") || NodeKey == TEXT("uvs") || NodeKey == TEXT("texcoord") || NodeKey == TEXT("texturecoordinate"))
						{
							return Str(SharedNode(TEXT("_uv"), TEXT("TextureCoordinate")) + Suffix);
						}
						if (NodeKey == TEXT("$time") || NodeKey == TEXT("time"))
						{
							return Str(SharedNode(TEXT("_time"), TEXT("Time")) + Suffix);
						}
						FLinearColor Color;
						if (Output.IsEmpty() && ParseColor(Value, Color))
						{
							return Str(ConstantNode(Str(ColorToHex(Color))));
						}
					}
					return Value;
				}
				if (Value->Type == EJson::Number || Value->Type == EJson::Boolean || IsNumberArray(Value))
				{
					return Str(ConstantNode(Value));
				}
				if (Value->Type == EJson::Object && (Value->AsObject()->HasField(TEXT("param")) || Value->AsObject()->HasField(TEXT("parameter"))))
				{
					return Str(ParamNode(Value->AsObject(), Where));
				}
				Error(TEXT("json"), Where, FString::Printf(TEXT("can't read %s as an input - give a node id (\"n\" or \"n.R\"), a number, [x, y], \"#hex\" or {\"param\": \"Name\", \"value\": v}"), *ToString(Value).Left(80)));
				return nullptr;
			}

			void Inline()
			{
				const int32 Count = JsonNodes.Num();
				for (int32 Index = 0; Index < Count; ++Index)
				{
					const TSharedPtr<FJsonObject> Node = JsonNodes[Index];
					const FString Id = GetString(Node, TEXT("id"));
					TSharedPtr<FJsonObject> Inputs = MakeShared<FJsonObject>();
					for (const auto& Pair : Node->GetObjectField(TEXT("inputs"))->Values)
					{
						if (TSharedPtr<FJsonValue> Ref = InlineRef(Pair.Value, Id))
						{
							Inputs->SetField(Pair.Key, Ref);
						}
					}
					Node->SetObjectField(TEXT("inputs"), Inputs);
				}
				for (TPair<FString, TSharedPtr<FJsonValue>>& Pair : JsonOutputs)
				{
					Pair.Value = InlineRef(Pair.Value, TEXT("outputs"));
				}
			}

			// ---- 4. Nodes: types, settings ---------------------------------------------------------------------------

			int32 AddNode(FNode&& Node, const FNodeSpec* Spec)
			{
				const int32 Index = Out.Nodes.Add(MoveTemp(Node));
				FNode& Added = Out.Nodes[Index];
				Specs.Add(Spec);
				AnyPins.Add(false);
				UnknownPin.Add(false);
				TArray<FPinInfo> Pins;
				TArray<FOut> Outs;
				if (Added.Type == TEXT("MaterialFunctionCall"))
				{
					if (!FunctionSignature(Added.Function, Level, Pins, Outs))
					{
						AnyPins[Index] = true;
						Outs = { { FString(), 0 } };
					}
				}
				else if (Added.Type == TEXT("Custom"))
				{
					for (const FString& Name : Added.CustomInputs)
					{
						FPinInfo Info;
						Info.Name = Name;
						Info.Names = { Name };
						Info.bOptional = false;
						Pins.Add(Info);
					}
					Outs = { { FString(), -1 } };
				}
				else if (Added.Type == TEXT("FunctionInput"))
				{
					Outs = { { FString(), -1 } };
				}
				else if (Added.Class)
				{
					Pins = PinsOf(Spec, Added.Class);
					Outs = OutTableOf(Cast<UMaterialExpression>(Added.Class->GetDefaultObject()));
				}
				PinTables.Add(MoveTemp(Pins));
				OutTables.Add(MoveTemp(Outs));
				return Index;
			}

			/** Props: aliases to the expression's own names, the shorthands ({channels: "rg"}, {tiling: [2, 1]}...), then checked. */
			TSharedPtr<FJsonObject> NormalizeProps(FNode& Node, const FNodeSpec* Spec, const TSharedPtr<FJsonObject>& In)
			{
				TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
				if (!In.IsValid())
				{
					return Props;
				}
				for (const auto& Pair : In->Values)
				{
					const FString Key = Normalize(Pair.Key);
					FString Real = Pair.Key;
					if (Spec)
					{
						if (const FString* Alias = Spec->PropAliases.Find(Key))
						{
							Real = *Alias;
						}
					}
					FString Text;
					// Custom node settings.
					if (Node.Type == TEXT("Custom"))
					{
						if (Key == TEXT("code") || Key == TEXT("hlsl"))
						{
							Node.Code = Pair.Value->AsString();
							continue;
						}
						if (Key == TEXT("output") || Key == TEXT("outputtype"))
						{
							Node.CustomWidth = FMath::Clamp(WidthOfInputType(Pair.Value->AsString()), 1, 4);
							continue;
						}
						if (Key == TEXT("inputs"))
						{
							continue;
						}
					}
					if (Node.Type == TEXT("ComponentMask") && (Key == TEXT("channels") || Key == TEXT("mask") || Key == TEXT("components") || Key == TEXT("channel")))
					{
						const FString Channels = ChannelSelection(Pair.Value->AsString());
						if (Channels.IsEmpty())
						{
							Error(TEXT("prop"), Node.Id, FString::Printf(TEXT("channels '%s': write the channels to keep, like \"rg\" or \"a\""), *Pair.Value->AsString()));
						}
						for (const TCHAR* Channel : { TEXT("R"), TEXT("G"), TEXT("B"), TEXT("A") })
						{
							Props->SetBoolField(Channel, Channels.Contains(Channel));
						}
						continue;
					}
					if (Node.Type == TEXT("TextureCoordinate") && Key == TEXT("tiling"))
					{
						const TArray<TSharedPtr<FJsonValue>> Parts = Pair.Value->Type == EJson::Array ? Pair.Value->AsArray() : TArray<TSharedPtr<FJsonValue>>{ Pair.Value, Pair.Value };
						Props->SetField(TEXT("UTiling"), Parts.IsValidIndex(0) ? Parts[0] : Num(1.0));
						Props->SetField(TEXT("VTiling"), Parts.IsValidIndex(1) ? Parts[1] : Parts[0]);
						continue;
					}
					if ((Node.Type == TEXT("Panner") && Key == TEXT("speed")) || (Node.Type == TEXT("Rotator") && Key == TEXT("center")))
					{
						const FString X = Node.Type == TEXT("Panner") ? TEXT("SpeedX") : TEXT("CenterX");
						const FString Y = Node.Type == TEXT("Panner") ? TEXT("SpeedY") : TEXT("CenterY");
						const TArray<TSharedPtr<FJsonValue>> Parts = Pair.Value->Type == EJson::Array ? Pair.Value->AsArray() : TArray<TSharedPtr<FJsonValue>>{ Pair.Value, Num(0.0) };
						Props->SetField(X, Parts.IsValidIndex(0) ? Parts[0] : Num(0.0));
						Props->SetField(Y, Parts.IsValidIndex(1) ? Parts[1] : Num(0.0));
						continue;
					}
					if (Node.Type == TEXT("Constant2Vector") && (Key == TEXT("value") || Key == TEXT("constant")) && Pair.Value->Type == EJson::Array)
					{
						const TArray<TSharedPtr<FJsonValue>>& Parts = Pair.Value->AsArray();
						Props->SetField(TEXT("R"), Parts.IsValidIndex(0) ? Parts[0] : Num(0.0));
						Props->SetField(TEXT("G"), Parts.IsValidIndex(1) ? Parts[1] : Num(0.0));
						continue;
					}
					TSharedPtr<FJsonValue> Value = Pair.Value;
					if (Normalize(Real) == TEXT("defaultvalue") && Value->Type == EJson::Array && Value->AsArray().Num() < 4 && Node.Type == TEXT("VectorParameter"))
					{
						TArray<TSharedPtr<FJsonValue>> Padded = Value->AsArray();
						while (Padded.Num() < 3)
						{
							Padded.Add(Num(0.0));
						}
						Padded.Add(Num(1.0));
						Value = MakeShared<FJsonValueArray>(Padded);
					}
					Props->SetField(Real, Value);
				}

				// Every remaining setting must exist on the expression.
				if (Node.Class && Node.Type != TEXT("Custom"))
				{
					TArray<FString> Bad;
					for (const auto& Pair : Props->Values)
					{
						FString Path = Pair.Key;
						FString Head = Path;
						Path.Split(TEXT("."), &Head, nullptr);
						FString FieldError;
						if (!FindField(Node.Class, Head, &FieldError))
						{
							Bad.Add(Pair.Key);
						}
					}
					if (Bad.Num() > 0)
					{
						TArray<FString> Fields;
						for (TFieldIterator<FProperty> It(Node.Class); It && Fields.Num() < 16; ++It)
						{
							if (It->HasAnyPropertyFlags(CPF_Edit) && !(CastField<FStructProperty>(*It) && CastField<FStructProperty>(*It)->Struct->GetName().Contains(TEXT("ExpressionInput"))))
							{
								Fields.AddUnique(It->GetName());
							}
						}
						Error(TEXT("prop"), Node.Id, FString::Printf(TEXT("%s has no setting %s (settings: %s)"), *Node.Type, *FString::Join(Bad, TEXT(", ")), *FString::Join(Fields, TEXT(", "))));
						for (const FString& Key : Bad)
						{
							Props->RemoveField(Key);
						}
					}
				}
				return Props;
			}

			static bool IsParameterType(const FString& Type)
			{
				return Type.Contains(TEXT("Parameter"));
			}

			void MakeNodes()
			{
				const FCatalog& Catalog = GetCatalog();

				// A function graph's own inputs come first, so nodes can name them.
				int32 Sort = 0;
				for (const TSharedPtr<FJsonObject>& Input : JsonInputs)
				{
					FNode Node;
					Node.Id = GetString(Input, TEXT("name"));
					Node.Type = TEXT("FunctionInput");
					Node.Class = FindExpressionClass(TEXT("FunctionInput"));
					Node.InputName = Node.Id;
					Node.InputWidth = WidthOfInputType(GetString(Input, TEXT("type"), TEXT("Scalar")));
					Node.Width = Node.InputWidth;
					Node.Props = MakeShared<FJsonObject>();
					Node.Props->SetStringField(TEXT("InputName"), Node.Id);
					Node.Props->SetStringField(TEXT("InputType"), GetString(Input, TEXT("type"), TEXT("Scalar")));
					Node.Props->SetNumberField(TEXT("SortPriority"), Sort++);
					if (TSharedPtr<FJsonValue> Preview = Input->TryGetField(TEXT("preview")))
					{
						Node.Props->SetField(TEXT("PreviewValue"), Preview);
						Node.Props->SetBoolField(TEXT("bUsePreviewValueAsDefault"), true);
					}
					AddNode(MoveTemp(Node), nullptr);
				}

				TMap<FString, int32> ParamNames;
				for (const TSharedPtr<FJsonObject>& Json : JsonNodes)
				{
					FNode Node;
					Node.Id = GetString(Json, TEXT("id"));
					Node.Macro = GetString(Json, TEXT("macro"));
					const FString Written = GetString(Json, TEXT("type"));
					if (Out.Find(Node.Id) != INDEX_NONE)
					{
						Error(TEXT("json"), Node.Id, FString::Printf(TEXT("two nodes are called '%s' - ids must be unique"), *Node.Id));
						continue;
					}
					if (Catalog.Macros.Contains(Normalize(Written)))
					{
						Error(TEXT("unknown_node"), Node.Id, TEXT("macros nest at most 4 deep"));
						Failed.Add(Node.Id);
						continue;
					}

					const FNodeSpec* Spec = FindSpec(Written);
					if (Spec && Spec->bHidden && !Out.bFunction)
					{
						Spec = nullptr;
					}
					if (Spec)
					{
						if (!Spec->Class)
						{
							Error(TEXT("unknown_node"), Node.Id, FString::Printf(TEXT("%s isn't in this engine version%s"), *Spec->Name, *Suggest(Written, AllTypeNames())));
							Failed.Add(Node.Id);
							continue;
						}
						Node.Type = Spec->Name;
						Node.Class = Spec->Class;
						if (Normalize(Written) != Normalize(Spec->Name))
						{
							Fix(FString::Printf(TEXT("%s: %s is %s"), *Node.Id, *Written, *Spec->Name));
						}
					}
					else
					{
						Node.Class = Written.IsEmpty() ? nullptr : FindExpressionClass(Written);
						if (!Node.Class)
						{
							Error(TEXT("unknown_node"), Node.Id, FString::Printf(TEXT("no node type '%s'%s (list_options material_nodes)"), *Written, *Suggest(Written, AllTypeNames())));
							Failed.Add(Node.Id);
							continue;
						}
						Spec = FindSpecForClass(Node.Class);
						Node.Type = Spec ? Spec->Name : ShortClassName(Node.Class);
					}

					if (Out.Domain == TEXT("UI") && !Out.bFunction)
					{
						const FString Key = Normalize(ShortClassName(Node.Class));
						if (Catalog.UiErrors.Contains(Key))
						{
							Error(TEXT("ui"), Node.Id, FString::Printf(TEXT("%s doesn't work in a UI material (no scene, depth or lights in UI) - build the look from UV, Time and maths"), *Node.Type));
						}
						else if (Catalog.UiWarnings.Contains(Key))
						{
							Warn(TEXT("ui"), Node.Id, FString::Printf(TEXT("%s means little in a UI material (no world or camera) - UV-based maths is usually what's wanted"), *Node.Type));
						}
					}

					Node.Function = GetString(Json, TEXT("function"));
					if (Node.Type == TEXT("MaterialFunctionCall"))
					{
						if (Node.Function.IsEmpty())
						{
							Node.Function = GetString(GetObject(Json, TEXT("props")), TEXT("function"), GetString(GetObject(Json, TEXT("loose")), TEXT("function")));
						}
						TArray<FString> Library;
						NameTBDAILibrary::DescribeFunctions()->Values.GetKeys(Library);
						Library.Remove(TEXT("$use"));
						FString Ignored;
						if (Node.Function.IsEmpty())
						{
							Error(TEXT("function"), Node.Id, FString::Printf(TEXT("a MaterialFunctionCall needs \"function\": a library function (%s) or a path"), *FString::Join(Library, TEXT(", "))));
						}
						else if (!NameTBDAILibrary::FindDefinitionJson(Node.Function, TEXT("function")).IsValid()
							&& !NameTBDAIHelpers::FindAsset(Node.Function, UMaterialFunctionInterface::StaticClass(), Ignored))
						{
							Error(TEXT("function"), Node.Id, FString::Printf(TEXT("no material function '%s'%s (library: %s)"), *Node.Function, *Suggest(Node.Function, Library), *FString::Join(Library, TEXT(", "))));
						}
					}

					// Settings written straight on the node: those the node has.
					TSharedPtr<FJsonObject> PropsJson = DeepCopy(GetObject(Json, TEXT("props")));
					if (TSharedPtr<FJsonObject> Loose = GetObject(Json, TEXT("loose")))
					{
						for (const auto& Pair : Loose->Values)
						{
							const bool bAlias = Spec && Spec->PropAliases.Contains(Normalize(Pair.Key));
							const bool bCustom = Node.Type == TEXT("Custom") && (Normalize(Pair.Key) == TEXT("description"));
							if (bAlias || bCustom || (Node.Class && FindField(Node.Class, Pair.Key)) || (Node.Type == TEXT("ComponentMask") && Normalize(Pair.Key) == TEXT("channels")))
							{
								PropsJson->SetField(Pair.Key, Pair.Value);
							}
							else if (Normalize(Pair.Key) != TEXT("function"))
							{
								Warn(TEXT("prop"), Node.Id, FString::Printf(TEXT("'%s' isn't a setting of %s - ignored (settings go in props)"), *Pair.Key, *Node.Type));
							}
						}
					}
					if (Node.Type == TEXT("Custom"))
					{
						Node.Code = GetString(Json, TEXT("code"));
						if (Json->HasField(TEXT("output")))
						{
							Node.CustomWidth = FMath::Clamp(WidthOfInputType(GetString(Json, TEXT("output"))), 1, 4);
						}
						for (const auto& Pair : Json->GetObjectField(TEXT("inputs"))->Values)
						{
							Node.CustomInputs.Add(Pair.Key);
						}
					}
					PropsJson->RemoveField(TEXT("function"));
					Node.Props = NormalizeProps(Node, Spec, PropsJson);
					if (Node.Type == TEXT("Custom"))
					{
						CheckCustom(Node);
					}

					if (IsParameterType(Node.Type))
					{
						FString Name = GetString(Node.Props, TEXT("ParameterName"));
						if (Name.IsEmpty())
						{
							Name = Node.Id;
							Name.Split(TEXT("/"), nullptr, &Name, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
							Name.RemoveFromStart(TEXT("p_"));
							if (!Name.IsEmpty())
							{
								Name[0] = FChar::ToUpper(Name[0]);
							}
							Node.Props->SetStringField(TEXT("ParameterName"), Name);
							Fix(FString::Printf(TEXT("%s: parameter named '%s' (give ParameterName to choose)"), *Node.Id, *Name));
						}
						if (const int32* Previous = ParamNames.Find(Name))
						{
							if (Out.Nodes[*Previous].Type == Node.Type)
							{
								// The same parameter twice: one node, used from both places.
								Redirects.Add(Node.Id, Out.Nodes[*Previous].Id);
								Fix(FString::Printf(TEXT("%s: the parameter '%s' was made twice - one node now"), *Node.Id, *Name));
								continue;
							}
							Error(TEXT("param"), Node.Id, FString::Printf(TEXT("two parameters are called '%s' (a %s and a %s) - rename one"), *Name, *Out.Nodes[*Previous].Type, *Node.Type));
						}
						ParamNames.Add(Name, Out.Nodes.Num());
					}
					MadeFrom.Add(Json.Get(), AddNode(MoveTemp(Node), Spec));
				}
			}

			/** A node folded into another (the same parameter made twice): its id -> the one kept. */
			TMap<FString, FString> Redirects;

			/** Which node each JSON node became. */
			TMap<const FJsonObject*, int32> MadeFrom;

			void CheckCustom(FNode& Node)
			{
				FString Code = Node.Code.TrimStartAndEnd();
				if (Code.IsEmpty())
				{
					Error(TEXT("custom"), Node.Id, TEXT("a Custom node needs \"code\" (HLSL that returns a value, e.g. \"return a * b;\")"));
					return;
				}
				if (!Code.Contains(TEXT("return")))
				{
					Code = TEXT("return ") + Code;
					if (!Code.EndsWith(TEXT(";")))
					{
						Code += TEXT(";");
					}
					Fix(FString::Printf(TEXT("%s: the code had no 'return' - it returns its expression now"), *Node.Id));
				}
				Node.Code = Code;
				CheckCustomCode(Node);
				for (const FString& Name : Node.CustomInputs)
				{
					bool bValid = !Name.IsEmpty() && (FChar::IsAlpha(Name[0]) || Name[0] == TEXT('_'));
					for (const TCHAR Char : Name)
					{
						bValid &= FChar::IsAlnum(Char) || Char == TEXT('_');
					}
					if (!bValid)
					{
						Error(TEXT("custom"), Node.Id, FString::Printf(TEXT("input '%s' isn't a valid HLSL name - inputs are the code's variables (letters, digits, _)"), *Name));
					}
					else if (!Node.Code.Contains(Name))
					{
						Warn(TEXT("custom"), Node.Id, FString::Printf(TEXT("the code doesn't use its input '%s'"), *Name));
					}
				}
			}

			/** Code with its comments blanked out - the same length, so positions match the code's. */
			static FString WithoutComments(const FString& Code)
			{
				FString Out = Code;
				const int32 Len = Out.Len();
				for (int32 At = 0; At + 1 < Len; ++At)
				{
					if (Out[At] == TEXT('/') && Out[At + 1] == TEXT('/'))
					{
						for (; At < Len && Out[At] != TEXT('\n'); ++At)
						{
							Out[At] = TEXT(' ');
						}
					}
					else if (Out[At] == TEXT('/') && Out[At + 1] == TEXT('*'))
					{
						const int32 Close = Out.Find(TEXT("*/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, At + 2);
						const int32 Stop = Close == INDEX_NONE ? Len : Close + 2;
						for (; At < Stop; ++At)
						{
							Out[At] = Out[At] == TEXT('\n') ? TEXT('\n') : TEXT(' ');
						}
						--At;
					}
				}
				return Out;
			}

			/** An identifier in a Custom node's code. */
			struct FWord
			{
				int32 At = 0;
				FString Text;

				/** A '(' follows: a call. */
				bool bCall = false;

				/** A '.' follows (View.ViewSizeAndInvSize) / precedes (uv.x). */
				bool bDotAfter = false;
				bool bMember = false;
			};

			/** Every identifier in Code (its comments blanked), numbers' suffixes and exponents (0.5f, 1e-6) left out. */
			static TArray<FWord> WordsOf(const FString& Code)
			{
				TArray<FWord> Words;
				auto IsWordChar = [](TCHAR Char) { return FChar::IsAlnum(Char) || Char == TEXT('_'); };
				const int32 Len = Code.Len();
				for (int32 At = 0; At < Len; ++At)
				{
					if (!IsWordChar(Code[At]) || (At > 0 && IsWordChar(Code[At - 1])))
					{
						continue;
					}
					int32 End = At;
					while (End < Len && IsWordChar(Code[End]))
					{
						++End;
					}
					if (!FChar::IsDigit(Code[At]))
					{
						FWord Word;
						Word.At = At;
						Word.Text = Code.Mid(At, End - At);
						int32 Next = End;
						while (Next < Len && FChar::IsWhitespace(Code[Next]))
						{
							++Next;
						}
						Word.bCall = Next < Len && Code[Next] == TEXT('(');
						Word.bDotAfter = Next < Len && Code[Next] == TEXT('.');
						int32 Previous = At - 1;
						while (Previous >= 0 && FChar::IsWhitespace(Code[Previous]))
						{
							--Previous;
						}
						Word.bMember = Previous >= 0 && Code[Previous] == TEXT('.');
						Words.Add(MoveTemp(Word));
					}
					At = End - 1;
				}
				return Words;
			}

			/**
			 * A Custom node's code made portable before the shader compiler sees it: what can't work on every platform refused
			 * with what to write instead, half precision raised to float, and the maths that is NaN or undefined on some GPUs
			 * (pow / sqrt / log of a negative, smoothstep with lo >= hi) run through the NTB_ helpers, which it then includes.
			 */
			void CheckCustomCode(FNode& Node)
			{
				const FString Stripped = WithoutComments(Node.Code);
				const TArray<FWord> Words = WordsOf(Stripped);

				struct FBanned
				{
					const TCHAR* Word;
					bool bCall;
					const TCHAR* Instead;
				};
				static const FBanned Banned[] = {
					{ TEXT("Texture2D"), false, TEXT("a texture can't be declared in the code - wire a TextureObject into an input and sample it with Texture2DSample(Tex, TexSampler, uv)") },
					{ TEXT("SamplerState"), false, TEXT("a sampler can't be declared in the code - a TextureObject input brings its own (<input>Sampler)") },
					{ TEXT("tex2D"), true, TEXT("tex2D is Direct3D 9 only - wire a TextureObject into an input and use Texture2DSample(Tex, TexSampler, uv)") },
					{ TEXT("discard"), false, TEXT("discard isn't allowed in a UI material - use blend Masked and output 0 / 1 to OpacityMask") },
					{ TEXT("clip"), true, TEXT("clip isn't allowed in a UI material - use blend Masked and output 0 / 1 to OpacityMask") },
					{ TEXT("groupshared"), false, TEXT("groupshared is compute-only") },
					{ TEXT("cbuffer"), false, TEXT("a cbuffer can't be declared in the code - use params") },
				};
				// Shader Model 6 intrinsics (WaveActiveSum, QuadReadAcrossX...): not on mobile or older graphics APIs.
				static const TCHAR* const Sm6[] = { TEXT("WaveActive"), TEXT("WaveRead"), TEXT("WavePrefix"), TEXT("WaveGet"), TEXT("WaveIs"), TEXT("WaveMatch"),
					TEXT("WaveMulti"), TEXT("QuadRead"), TEXT("QuadAny"), TEXT("QuadAll") };
				static const TSet<FString> Internals = { TEXT("View"), TEXT("ResolvedView"), TEXT("Primitive"), TEXT("Parameters") };
				TSet<FString> Reported;
				for (const FWord& Word : Words)
				{
					if (Word.bMember || Reported.Contains(Word.Text))
					{
						continue;
					}
					for (const FBanned& Item : Banned)
					{
						if (Word.Text == Item.Word && (Word.bCall || !Item.bCall))
						{
							Error(TEXT("custom"), Node.Id, FString::Printf(TEXT("'%s': %s"), Item.Word, Item.Instead));
							Reported.Add(Word.Text);
						}
					}
					for (const TCHAR* Prefix : Sm6)
					{
						if (Word.bCall && Word.Text.StartsWith(Prefix, ESearchCase::CaseSensitive))
						{
							Error(TEXT("custom"), Node.Id, FString::Printf(TEXT("'%s' needs Shader Model 6 - not on mobile or older graphics APIs; compute the value directly"), *Word.Text));
							Reported.Add(Word.Text);
							break;
						}
					}
					if (Word.bDotAfter && Internals.Contains(Word.Text))
					{
						Warn(TEXT("custom"), Node.Id, FString::Printf(TEXT("'%s.' is the engine's internal shader data - it changes between engine versions and platforms; use the uv / time inputs (or wire a node into an input)"), *Word.Text));
						Reported.Add(Word.Text);
					}
				}
				TArray<FString> Lines;
				Stripped.ParseIntoArrayLines(Lines);
				for (const FString& Line : Lines)
				{
					if (Line.TrimStart().StartsWith(TEXT("#")))
					{
						Error(TEXT("custom"), Node.Id, TEXT("'#' lines (#include, #define...) can't go in the code - the NTB_ helpers are included for you; write constants as static const float"));
						break;
					}
				}

				// The rewrites, last first so the earlier positions stay right.
				static const TMap<FString, FString> Wider = { { TEXT("half"), TEXT("float") }, { TEXT("half2"), TEXT("float2") }, { TEXT("half3"), TEXT("float3") },
					{ TEXT("half4"), TEXT("float4") }, { TEXT("min16float"), TEXT("float") }, { TEXT("min16float2"), TEXT("float2") }, { TEXT("min16float3"), TEXT("float3") },
					{ TEXT("min16float4"), TEXT("float4") } };
				static const TMap<FString, FString> Safe = { { TEXT("pow"), TEXT("NTB_Pow") }, { TEXT("sqrt"), TEXT("NTB_Sqrt") }, { TEXT("log"), TEXT("NTB_Log") },
					{ TEXT("log2"), TEXT("NTB_Log2") }, { TEXT("smoothstep"), TEXT("NTB_SmoothStep") } };
				const bool bHelpers = HelpersAvailable();
				bool bWidened = false;
				bool bMadeSafe = false;
				bool bUsesHelpers = false;
				for (int32 Index = Words.Num() - 1; Index >= 0; --Index)
				{
					const FWord& Word = Words[Index];
					if (Word.bMember)
					{
						continue;
					}
					const FString* Replacement = Wider.Find(Word.Text);
					bWidened |= Replacement != nullptr;
					if (!Replacement && bHelpers && Word.bCall)
					{
						Replacement = Safe.Find(Word.Text);
						bMadeSafe |= Replacement != nullptr;
					}
					if (Replacement)
					{
						Node.Code = Node.Code.Left(Word.At) + *Replacement + Node.Code.Mid(Word.At + Word.Text.Len());
					}
					bUsesHelpers |= Word.Text.StartsWith(TEXT("NTB_"), ESearchCase::CaseSensitive) || (Replacement && Replacement->StartsWith(TEXT("NTB_")));
				}
				if (bWidened)
				{
					Added(TEXT("half / min16float are float: full precision on every GPU"));
				}
				if (bMadeSafe)
				{
					Added(TEXT("pow / sqrt / log / smoothstep run as their NTB_ versions: never NaN, smoothstep either way, the same on every GPU"));
				}
				if (bUsesHelpers)
				{
					if (bHelpers)
					{
						Node.Includes.AddUnique(HelpersInclude);
					}
					else
					{
						Error(TEXT("custom"), Node.Id, TEXT("the NTB_ helpers aren't available in this editor (the NameTBDShaders module didn't load) - write the maths inline"));
					}
				}
			}

			// ---- 5. Wires ---------------------------------------------------------------------------------------------

			/** "n", "n.R", "g/d.Mask" -> the node's index and the output name. */
			bool ParseRef(const FString& Ref, int32& OutIndex, FString& OutOutput, const FString& Where)
			{
				FString Id = Ref;
				OutOutput.Reset();
				if (const FString* Redirected = Redirects.Find(Id))
				{
					Id = *Redirected;
				}
				OutIndex = Out.Find(Id);
				if (OutIndex == INDEX_NONE)
				{
					FString Node;
					FString Output;
					if (Ref.Split(TEXT("."), &Node, &Output, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
					{
						if (const FString* Redirected = Redirects.Find(Node))
						{
							Node = *Redirected;
						}
						OutIndex = Out.Find(Node);
						OutOutput = Output;
						Id = Node;
					}
				}
				if (OutIndex == INDEX_NONE)
				{
					if (!Failed.Contains(Id))
					{
						TArray<FString> Ids;
						for (const FNode& Each : Out.Nodes)
						{
							if (!Each.Id.StartsWith(TEXT("_")) && !Each.Id.Contains(TEXT("/")))
							{
								Ids.Add(Each.Id);
							}
							if (!Each.Macro.IsEmpty())
							{
								Ids.AddUnique(Each.Macro);
							}
						}
						Error(TEXT("missing_node"), Where, FString::Printf(TEXT("'%s' isn't a node%s"), *Ref, *Suggest(Id, Ids)));
					}
					return false;
				}
				return true;
			}

			void MakeWires()
			{
				for (const TSharedPtr<FJsonObject>& Json : JsonNodes)
				{
					const int32* MadeIndex = MadeFrom.Find(Json.Get());
					if (!MadeIndex)
					{
						continue;
					}
					const int32 Index = *MadeIndex;
					// Copies: the repairs below add nodes, which moves the node and pin arrays.
					const TArray<FPinInfo> Pins = PinTables[Index];
					FNode& Node = Out.Nodes[Index];
					TSet<FString> Wired;
					for (const auto& Pair : Json->GetObjectField(TEXT("inputs"))->Values)
					{
						FString Ref;
						if (!Pair.Value.IsValid() || !Pair.Value->TryGetString(Ref))
						{
							continue;
						}
						const FPinInfo* Pin = nullptr;
						if (!AnyPins[Index])
						{
							const FString Given = Normalize(Pair.Key);
							for (const FPinInfo& Each : Pins)
							{
								for (const FString& Name : Each.Names)
								{
									if (Normalize(Name) == Given)
									{
										Pin = &Each;
										break;
									}
								}
								if (Pin)
								{
									break;
								}
							}
							for (int32 Each = 0; !Pin && Each < Pins.Num(); ++Each)
							{
								if (IsGenericAlias(Pins[Each].Name, Pair.Key))
								{
									Pin = &Pins[Each];
								}
							}
							if (!Pin && Pair.Key.StartsWith(TEXT("#")))
							{
								const int32 Position = FCString::Atoi(*Pair.Key.Mid(1));
								Pin = Pins.IsValidIndex(Position) ? &Pins[Position] : nullptr;
							}
							if (!Pin && Pins.Num() == 1)
							{
								Pin = &Pins[0];
							}
							if (!Pin)
							{
								TArray<FString> Names;
								for (const FPinInfo& Each : Pins)
								{
									Names.Add(Each.Name);
								}
								Error(TEXT("unknown_pin"), Node.Id, FString::Printf(TEXT("%s has no input '%s' (inputs: %s)"), *Node.Type, *Pair.Key, Names.Num() ? *FString::Join(Names, TEXT(", ")) : TEXT("none")));
								UnknownPin[Index] = true;
								continue;
							}
							if (Normalize(Pin->Name) != Given && !Pair.Key.StartsWith(TEXT("#")) && Node.Macro.IsEmpty())
							{
								Fix(FString::Printf(TEXT("%s: input %s is %s"), *Node.Id, *Pair.Key, *Pin->Name));
							}
						}
						FWire Wire;
						Wire.Pin = Pin ? Pin->Name : Pair.Key;
						Wire.PinNames = Pin ? Pin->Names : TArray<FString>{ Pair.Key };
						if (Wired.Contains(Wire.Pin))
						{
							Error(TEXT("json"), Node.Id, FString::Printf(TEXT("input %s is wired twice"), *Wire.Pin));
							continue;
						}
						if (!ParseRef(Ref, Wire.From, Wire.FromOutput, Node.Id))
						{
							Wired.Add(Wire.Pin);
							continue;
						}
						Wired.Add(Wire.Pin);
						Node.Wires.Add(MoveTemp(Wire));
					}

					// Inputs left unwired. (No reference to the node from here: UVNode / NoiseFromUV add nodes.)
					const FString Id = Node.Id;
					const FString Type = Node.Type;
					for (const FPinInfo& Pin : Pins)
					{
						if (Wired.Contains(Pin.Name))
						{
							continue;
						}
						if (Type == TEXT("MaterialFunctionCall") && IsUVName(Pin.Name))
						{
							// A library function's UV input would take its preview value (one point): the widget's UV instead.
							const int32 UV = Out.Find(UVNode());
							Out.Nodes[Index].Wires.Add({ Pin.Name, Pin.Names, UV, FString() });
							Fix(FString::Printf(TEXT("%s: %s wired to the widget's UV"), *Id, *Pin.Name));
							continue;
						}
						if (Type == TEXT("Noise") && Normalize(Pin.Name) == TEXT("position"))
						{
							if (Out.Domain == TEXT("UI"))
							{
								NoiseFromUV(Index, Pin);
							}
							continue;
						}
						if (!Pin.bOptional && !UnknownPin[Index])
						{
							Error(TEXT("missing_input"), Id, FString::Printf(TEXT("%s input %s isn't wired - give a node or a number"), *Type, *Pin.Name));
						}
					}
				}

				// The graph's outputs.
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : JsonOutputs)
				{
					FString Ref;
					if (!Pair.Value.IsValid() || !Pair.Value->TryGetString(Ref))
					{
						continue;
					}
					FOutput Output;
					Output.Name = Pair.Key;
					if (ParseRef(Ref, Output.From, Output.FromOutput, TEXT("outputs")))
					{
						Out.Outputs.Add(Output);
					}
				}
				FixFallingSmoothSteps();
			}

			/** A Constant node's value. */
			bool ConstantValue(const FWire& Wire, double& OutValue) const
			{
				if (!Out.Nodes.IsValidIndex(Wire.From) || Out.Nodes[Wire.From].Type != TEXT("Constant"))
				{
					return false;
				}
				OutValue = GetNumber(Out.Nodes[Wire.From].Props, TEXT("R"));
				return true;
			}

			/**
			 * Unreal's SmoothStep only rises: with Min above Max it gives 0 everywhere instead of a falling edge. With constant
			 * Min / Max the two are swapped and a OneMinus put after the node (what everything read from it now reads).
			 */
			void FixFallingSmoothSteps()
			{
				const int32 Count = Out.Nodes.Num();
				for (int32 Index = 0; Index < Count; ++Index)
				{
					if (Out.Nodes[Index].Type != TEXT("SmoothStep"))
					{
						continue;
					}
					const int32 MinWire = FindWire(Index, TEXT("Min"));
					const int32 MaxWire = FindWire(Index, TEXT("Max"));
					double MinValue = 0.0;
					double MaxValue = 1.0;
					if (MinWire == INDEX_NONE || MaxWire == INDEX_NONE
						|| !ConstantValue(Out.Nodes[Index].Wires[MinWire], MinValue) || !ConstantValue(Out.Nodes[Index].Wires[MaxWire], MaxValue)
						|| MinValue <= MaxValue)
					{
						continue;
					}
					{
						TArray<FWire>& Wires = Out.Nodes[Index].Wires;
						Swap(Wires[MinWire].From, Wires[MaxWire].From);
						Swap(Wires[MinWire].FromOutput, Wires[MaxWire].FromOutput);
					}
					const FString Id = Out.Nodes[Index].Id;
					const int32 Flip = Repair(TEXT("OneMinus"), Id + TEXT("_flip"), { { TEXT("Input"), Index } }, 0);
					for (int32 Other = 0; Other < Out.Nodes.Num(); ++Other)
					{
						if (Other == Flip)
						{
							continue;
						}
						for (FWire& Wire : Out.Nodes[Other].Wires)
						{
							if (Wire.From == Index)
							{
								Wire.From = Flip;
							}
						}
					}
					for (FOutput& Output : Out.Outputs)
					{
						if (Output.From == Index)
						{
							Output.From = Flip;
						}
					}
					Fix(FString::Printf(TEXT("%s: SmoothStep only rises (Min %g above Max %g gives 0 everywhere) - Min and Max swapped and a OneMinus put after it"), *Id, MinValue, MaxValue));
				}
			}

			FString UVNode()
			{
				if (Out.Find(TEXT("_uv")) == INDEX_NONE)
				{
					FNode Node;
					Node.Id = TEXT("_uv");
					Node.Type = TEXT("TextureCoordinate");
					Node.Class = FindExpressionClass(TEXT("TextureCoordinate"));
					Node.Props = MakeShared<FJsonObject>();
					Node.Width = 2;
					AddNode(MoveTemp(Node), FindSpec(TEXT("TextureCoordinate")));
				}
				return TEXT("_uv");
			}

			/** In UI there is no world position: a Noise reads the widget's UV (times 8 for some detail) instead. */
			void NoiseFromUV(int32 NoiseIndex, const FPinInfo& Pin)
			{
				const int32 UV = Out.Find(UVNode());
				const int32 Scale = Repair(TEXT("Constant"), FString::Printf(TEXT("_k%d"), ++Made), {}, 1);
				Out.Nodes[Scale].Props->SetNumberField(TEXT("R"), 8.0);
				const int32 Scaled = Repair(TEXT("Multiply"), FString::Printf(TEXT("_noiseuv%d"), ++Made), { { TEXT("A"), UV }, { TEXT("B"), Scale } }, 2);
				const int32 Zero = Repair(TEXT("Constant"), FString::Printf(TEXT("_k%d"), ++Made), {}, 1);
				const int32 Position = Repair(TEXT("AppendVector"), FString::Printf(TEXT("_noisepos%d"), ++Made), { { TEXT("A"), Scaled }, { TEXT("B"), Zero } }, 3);
				Out.Nodes[NoiseIndex].Wires.Add({ Pin.Name, Pin.Names, Position, FString() });
				Fix(FString::Printf(TEXT("%s: Noise had no Position (UI has no world) - it reads UV x 8 now; wire Position to change the detail"), *Out.Nodes[NoiseIndex].Id));
			}

			struct FRepairInput
			{
				FString Pin;
				int32 From = INDEX_NONE;
				FString FromOutput;
			};

			/** A node the repairs add: wired to existing nodes by pin name, its width given. */
			int32 Repair(const FString& Type, const FString& Id, const TArray<FRepairInput>& Inputs, int32 Width)
			{
				FNode Node;
				Node.Id = Id;
				const FNodeSpec* Spec = FindSpec(Type);
				Node.Type = Spec ? Spec->Name : Type;
				Node.Class = Spec ? Spec->Class : FindExpressionClass(Type);
				Node.Props = MakeShared<FJsonObject>();
				Node.Width = Width;
				const int32 Index = AddNode(MoveTemp(Node), Spec);
				for (const FRepairInput& Input : Inputs)
				{
					FWire Wire;
					Wire.Pin = Input.Pin;
					Wire.PinNames = { Input.Pin };
					for (const FPinInfo& Pin : PinTables[Index])
					{
						if (Pin.Name == Input.Pin)
						{
							Wire.PinNames = Pin.Names;
						}
					}
					Wire.From = Input.From;
					Wire.FromOutput = Input.FromOutput;
					Out.Nodes[Index].Wires.Add(MoveTemp(Wire));
				}
				return Index;
			}

			// ---- 6. Order: loops, and what reaches the outputs --------------------------------------------------------

			void Order()
			{
				TArray<uint8> State;
				State.SetNumZeroed(Out.Nodes.Num());
				TArray<int32> Stack;
				TFunction<void(int32)> Visit = [&](int32 Index)
				{
					if (State[Index] == 2)
					{
						return;
					}
					if (State[Index] == 1)
					{
						TArray<FString> Loop;
						for (int32 Position = Stack.Find(Index); Position != INDEX_NONE && Position < Stack.Num(); ++Position)
						{
							Loop.Add(Out.Nodes[Stack[Position]].Id);
						}
						Loop.Add(Out.Nodes[Index].Id);
						Error(TEXT("cycle"), Out.Nodes[Index].Id, FString::Printf(TEXT("a loop: %s - a material can't feed a node back into itself"), *FString::Join(Loop, TEXT(" -> "))));
						return;
					}
					State[Index] = 1;
					Stack.Push(Index);
					for (const FWire& Wire : Out.Nodes[Index].Wires)
					{
						if (Out.Nodes.IsValidIndex(Wire.From))
						{
							Visit(Wire.From);
						}
					}
					Stack.Pop();
					State[Index] = 2;
					Topo.Add(Index);
				};
				for (const FOutput& Output : Out.Outputs)
				{
					Visit(Output.From);
				}
				if (Out.bFunction)
				{
					// A function's inputs are part of its signature, used or not.
					for (int32 Index = 0; Index < Out.Nodes.Num(); ++Index)
					{
						if (Out.Nodes[Index].Type == TEXT("FunctionInput"))
						{
							Visit(Index);
						}
					}
				}
				TArray<FString> Unused;
				for (int32 Index = 0; Index < Out.Nodes.Num(); ++Index)
				{
					FNode& Node = Out.Nodes[Index];
					Node.bLive = State[Index] == 2;
					if (!Node.bLive && !Node.Id.StartsWith(TEXT("_")) && !Node.Id.Contains(TEXT("/")))
					{
						Unused.Add(Node.Id);
					}
				}
				if (Unused.Num() > 0)
				{
					Fix(FString::Printf(TEXT("left out %s: nothing uses %s"), *FString::Join(Unused, TEXT(", ")), Unused.Num() == 1 ? TEXT("it") : TEXT("them")));
				}
			}

			// ---- 7. Widths on every wire ------------------------------------------------------------------------------
			// The repairs add nodes, which moves Out.Nodes and the tables: nothing below keeps a reference to a node or a
			// wire across a repair - nodes and wires are reached by index each time.

			int32 OutputWidth(int32 From, const FString& FromOutput) const
			{
				if (!Out.Nodes.IsValidIndex(From))
				{
					return 0;
				}
				const TArray<FOut>& Table = OutTables[From];
				const int32 Slot = FindOutputIn(Table, FromOutput);
				if (Slot == INDEX_NONE)
				{
					return 0;
				}
				return Table[Slot].Width == -1 ? Out.Nodes[From].Width : Table[Slot].Width;
			}

			/** A ComponentMask keeping Channels of From's output: the new node's index. */
			int32 MaskOf(int32 From, const FString& FromOutput, const FString& Channels)
			{
				const int32 Mask = Repair(TEXT("ComponentMask"), FString::Printf(TEXT("_mask%d"), ++Made), { { TEXT("Input"), From, FromOutput } }, Channels.Len());
				for (const TCHAR* Channel : { TEXT("R"), TEXT("G"), TEXT("B"), TEXT("A") })
				{
					Out.Nodes[Mask].Props->SetBoolField(Channel, Channels.Contains(Channel));
				}
				return Mask;
			}

			/** From's output (Width wide) with a 0 appended: the new node's index. */
			int32 AppendZeroOf(int32 From, const FString& FromOutput, int32 Width)
			{
				const int32 Source = FromOutput.IsEmpty() ? From : MaskOf(From, FromOutput, FString(TEXT("RGBA")).Left(Width));
				const int32 Zero = Repair(TEXT("Constant"), FString::Printf(TEXT("_k%d"), ++Made), {}, 1);
				return Repair(TEXT("AppendVector"), FString::Printf(TEXT("_append%d"), ++Made), { { TEXT("A"), Source }, { TEXT("B"), Zero } }, Width + 1);
			}

			void Rewire(int32 NodeIndex, int32 WireIndex, int32 NewFrom)
			{
				FWire& Wire = Out.Nodes[NodeIndex].Wires[WireIndex];
				Wire.From = NewFrom;
				Wire.FromOutput.Reset();
			}

			int32 FindWire(int32 NodeIndex, const FString& Pin) const
			{
				return Out.Nodes[NodeIndex].Wires.IndexOfByPredicate([&Pin](const FWire& Wire) { return Wire.Pin == Pin; });
			}

			/**
			 * The source output named exists - or a channel pick on a node that has one output ("n.R") becomes a mask, From
			 * then pointing at it. False (reported) when it can't be read.
			 */
			bool CheckSource(int32& From, FString& FromOutput, const FString& Owner)
			{
				if (AnyPins[From] || FindOutputIn(OutTables[From], FromOutput) != INDEX_NONE)
				{
					return true;
				}
				const FString SourceId = Out.Nodes[From].Id;
				const FString SourceType = Out.Nodes[From].Type;
				const FString Channels = ChannelSelection(FromOutput);
				const int32 SourceWidth = OutputWidth(From, FString());
				bool bInRange = !Channels.IsEmpty();
				for (const TCHAR Channel : Channels)
				{
					bInRange &= SourceWidth == 0 || ChannelIndex(Channel) < SourceWidth;
				}
				if (bInRange)
				{
					const FString Written = FromOutput;
					From = MaskOf(From, FString(), Channels);
					FromOutput.Reset();
					if (!SourceId.StartsWith(TEXT("_")))
					{
						Fix(FString::Printf(TEXT("%s: '%s.%s' - %s has one output, so its %s is masked out"), *Owner, *SourceId, *Written, *SourceType, *Channels));
					}
					return true;
				}
				TArray<FString> Names;
				for (const FOut& Each : OutTables[From])
				{
					Names.Add(Each.Name.IsEmpty() ? FString(TEXT("(default)")) : Each.Name);
				}
				Error(TEXT("unknown_output"), Owner, FString::Printf(TEXT("'%s.%s': %s"), *SourceId, *FromOutput,
					Names.Num() <= 1 ? *FString::Printf(TEXT("%s has one output - write '%s'%s"), *SourceType, *SourceId, SourceWidth > 1 ? TEXT(" (or a channel: .R, .G...)") : TEXT(""))
						: *FString::Printf(TEXT("%s's outputs are %s"), *SourceType, *FString::Join(Names, TEXT(", ")))));
				return false;
			}

			static FString WidthName(int32 Width)
			{
				return Width > 0 ? FString::Printf(TEXT("float%d"), Width) : FString(TEXT("?"));
			}

			void ComputeWidth(int32 Index)
			{
				const FNodeSpec* Spec = Specs[Index];
				const TArray<FPinInfo> Pins = PinTables[Index];
				const FString Id = Out.Nodes[Index].Id;
				const FString Type = Out.Nodes[Index].Type;

				// The width coming into each wired pin.
				TMap<FString, int32> In;
				for (int32 WireIndex = 0; WireIndex < Out.Nodes[Index].Wires.Num(); ++WireIndex)
				{
					int32 From = Out.Nodes[Index].Wires[WireIndex].From;
					FString FromOutput = Out.Nodes[Index].Wires[WireIndex].FromOutput;
					const bool bReadable = CheckSource(From, FromOutput, Id);
					FWire& Wire = Out.Nodes[Index].Wires[WireIndex];
					Wire.From = From;
					Wire.FromOutput = FromOutput;
					if (bReadable)
					{
						In.Add(Wire.Pin, OutputWidth(From, FromOutput));
					}
				}

				// Pins that take one width.
				for (const FPinInfo& Pin : Pins)
				{
					const int32 WireIndex = FindWire(Index, Pin.Name);
					const int32 Given = In.FindRef(Pin.Name);
					if (WireIndex == INDEX_NONE || Given == 0 || Pin.Width.IsEmpty() || Pin.Width == TEXT("any") || Pin.Width == TEXT("1|out"))
					{
						continue;
					}
					const int32 Need = FCString::Atoi(*Pin.Width);
					if (Need == 0 || Given == Need || (Type == TEXT("MaterialFunctionCall") && Given == 1))
					{
						continue;
					}
					const FWire Wire = Out.Nodes[Index].Wires[WireIndex];
					if (Given > Need && Need > 1)
					{
						Rewire(Index, WireIndex, MaskOf(Wire.From, Wire.FromOutput, FString(TEXT("RGBA")).Left(Need)));
						In.Add(Pin.Name, Need);
						Fix(FString::Printf(TEXT("%s.%s takes %s: the %s going in is masked to it"), *Id, *Pin.Name, *WidthName(Need), *WidthName(Given)));
					}
					else if (Given == Need - 1 && Need >= 3)
					{
						Rewire(Index, WireIndex, AppendZeroOf(Wire.From, Wire.FromOutput, Given));
						In.Add(Pin.Name, Need);
						Fix(FString::Printf(TEXT("%s.%s takes %s: the %s going in is appended with 0"), *Id, *Pin.Name, *WidthName(Need), *WidthName(Given)));
					}
					else
					{
						Error(TEXT("width"), Id, FString::Printf(TEXT("%s takes %s but gets %s - %s"), *Pin.Name, *WidthName(Need), *WidthName(Given),
							Need == 1 ? TEXT("mask one channel (\"n.R\") or use a scalar") : TEXT("give it a value of that width (ComponentMask / AppendVector)")));
					}
				}

				int32 Width = 0;
				const FString Rule = Spec ? Spec->Out : FString();
				if (Rule.StartsWith(TEXT("max:")))
				{
					TArray<FString> RulePins;
					Rule.Mid(4).ParseIntoArray(RulePins, TEXT(","));
					TSet<int32> Wide;
					bool bAnyScalar = false;
					for (const FString& Pin : RulePins)
					{
						const int32 PinWidth = In.FindRef(Pin);
						if (PinWidth > 1)
						{
							Wide.Add(PinWidth);
						}
						bAnyScalar |= PinWidth == 1;
					}
					if (Wide.Num() == 2 && Wide.Contains(3) && Wide.Contains(4))
					{
						for (const FString& Pin : RulePins)
						{
							const int32 WireIndex = FindWire(Index, Pin);
							if (In.FindRef(Pin) == 4 && WireIndex != INDEX_NONE)
							{
								const FWire Wire = Out.Nodes[Index].Wires[WireIndex];
								Rewire(Index, WireIndex, MaskOf(Wire.From, Wire.FromOutput, TEXT("RGB")));
								In.Add(Pin, 3);
							}
						}
						Wide.Reset();
						Wide.Add(3);
						Fix(FString::Printf(TEXT("%s: a float4 met a float3 - the float4 is masked to RGB"), *Id));
					}
					else if (Wide.Num() > 1)
					{
						TArray<FString> Parts;
						for (const FString& Pin : RulePins)
						{
							if (In.FindRef(Pin) > 0)
							{
								Parts.Add(FString::Printf(TEXT("%s=%s"), *Pin, *WidthName(In.FindRef(Pin))));
							}
						}
						Error(TEXT("width"), Id, FString::Printf(TEXT("%s mixes %s - make them one width (ComponentMask the wider, or AppendVector the narrower); a scalar mixes with anything"),
							*Type, *FString::Join(Parts, TEXT(" and "))));
					}
					for (const int32 Each : Wide)
					{
						Width = FMath::Max(Width, Each);
					}
					if (Width == 0 && bAnyScalar)
					{
						Width = 1;
					}
				}
				else if (Rule.StartsWith(TEXT("same:")))
				{
					Width = In.FindRef(Rule.Mid(5));
				}
				else if (Rule.StartsWith(TEXT("sum:")))
				{
					TArray<FString> RulePins;
					Rule.Mid(4).ParseIntoArray(RulePins, TEXT(","));
					int32 Sum = 0;
					bool bKnown = RulePins.Num() > 0;
					for (const FString& Pin : RulePins)
					{
						bKnown &= In.FindRef(Pin) > 0;
						Sum += In.FindRef(Pin);
					}
					Width = bKnown ? Sum : 0;
					if (bKnown && Sum > 4)
					{
						Error(TEXT("width"), Id, FString::Printf(TEXT("AppendVector of %s and %s makes float%d (at most float4)"), *WidthName(In.FindRef(RulePins[0])), *WidthName(In.FindRef(RulePins.Last())), Sum));
					}
				}
				else if (Rule == TEXT("mask"))
				{
					FString Channels;
					for (const TCHAR* Channel : { TEXT("R"), TEXT("G"), TEXT("B"), TEXT("A") })
					{
						if (GetBool(Out.Nodes[Index].Props, Channel))
						{
							Channels += Channel;
						}
					}
					Width = Channels.Len();
					if (Channels.IsEmpty())
					{
						Error(TEXT("prop"), Id, TEXT("ComponentMask keeps no channel - props {channels: \"rg\"} (or R / G / B / A true)"));
					}
					const int32 Source = In.FindRef(TEXT("Input"));
					for (const TCHAR Channel : Channels)
					{
						if (Source > 0 && ChannelIndex(Channel) >= Source)
						{
							Error(TEXT("width"), Id, FString::Printf(TEXT("ComponentMask takes %c from a %s (it has %s)"), Channel, *WidthName(Source), *FString(TEXT("RGBA")).Left(Source)));
							break;
						}
					}
				}
				else if (FCString::Atoi(*Rule) > 0)
				{
					Width = FCString::Atoi(*Rule);
				}
				else if (Type == TEXT("Custom"))
				{
					Width = Out.Nodes[Index].CustomWidth;
				}
				else if (Type == TEXT("FunctionInput"))
				{
					Width = Out.Nodes[Index].InputWidth;
				}
				else if (OutTables[Index].Num() > 0 && OutTables[Index][0].Width > 0)
				{
					Width = OutTables[Index][0].Width;
				}
				Out.Nodes[Index].Width = Width;

				// "Scalar or the node's own width" pins (Lerp's Alpha, Power's Exponent, Clamp's Min / Max).
				for (const FPinInfo& Pin : Pins)
				{
					const int32 Given = In.FindRef(Pin.Name);
					const int32 WireIndex = FindWire(Index, Pin.Name);
					if (Pin.Width != TEXT("1|out") || Given <= 1 || Width == 0 || Given == Width || WireIndex == INDEX_NONE)
					{
						continue;
					}
					if (Given == 4 && Width == 3)
					{
						const FWire Wire = Out.Nodes[Index].Wires[WireIndex];
						Rewire(Index, WireIndex, MaskOf(Wire.From, Wire.FromOutput, TEXT("RGB")));
						Fix(FString::Printf(TEXT("%s.%s: a float4 met a float3 - masked to RGB"), *Id, *Pin.Name));
						continue;
					}
					Error(TEXT("width"), Id, FString::Printf(TEXT("%s is %s but the values are %s - give it a scalar (one channel) or the same width%s"), *Pin.Name, *WidthName(Given), *WidthName(Width),
						Type == TEXT("LinearInterpolate") ? TEXT(" (Alpha is the 0..1 mix; the colors go in A and B)") : TEXT("")));
				}
			}

			void Widths()
			{
				for (const int32 Index : Topo)
				{
					ComputeWidth(Index);
				}
			}

			// ---- 8. Outputs: names, widths, opacity kept in 0..1, the widget tint ---------------------------------

			bool Bounded(int32 Index, const FString& FromOutput, int32 Depth) const
			{
				if (!Out.Nodes.IsValidIndex(Index) || Depth > 12)
				{
					return false;
				}
				const FNode& Node = Out.Nodes[Index];
				auto Input = [this, &Node, Depth](const TCHAR* Pin) -> bool
				{
					const FWire* Wire = Node.Wires.FindByPredicate([Pin](const FWire& Each) { return Each.Pin == Pin; });
					return Wire && Bounded(Wire->From, Wire->FromOutput, Depth + 1);
				};
				const FString& Type = Node.Type;
				if (Type == TEXT("Saturate") || Type == TEXT("SmoothStep") || Type == TEXT("Step") || Type == TEXT("VertexColor") || Type == TEXT("Frac")
					|| Type == TEXT("SphereMask") || Type.StartsWith(TEXT("TextureSample")) || (Type == TEXT("Clamp") && !Node.Wires.ContainsByPredicate([](const FWire& Wire) { return Wire.Pin != TEXT("Input"); })))
				{
					return true;
				}
				if (Type == TEXT("Constant") || Type == TEXT("ScalarParameter"))
				{
					const double Value = GetNumber(Node.Props, Type == TEXT("Constant") ? TEXT("R") : TEXT("DefaultValue"), Type == TEXT("Constant") ? 0.0 : 0.0);
					return Value >= 0.0 && Value <= 1.0;
				}
				if (Type == TEXT("VectorParameter") || Type == TEXT("Constant3Vector") || Type == TEXT("Constant4Vector"))
				{
					return true; // colors (alpha included) are picked in 0..1
				}
				if (Type == TEXT("OneMinus") || Type == TEXT("ComponentMask") || Type == TEXT("Power"))
				{
					return Input(Type == TEXT("Power") ? TEXT("Base") : TEXT("Input"));
				}
				if (Type == TEXT("Multiply") || Type == TEXT("Max") || Type == TEXT("Min"))
				{
					return Input(TEXT("A")) && Input(TEXT("B"));
				}
				if (Type == TEXT("LinearInterpolate"))
				{
					return Input(TEXT("A")) && Input(TEXT("B")) && Input(TEXT("Alpha"));
				}
				return false;
			}

			FString MaterialOutputName(const FString& Name) const
			{
				const FString Key = Normalize(Name);
				if (Key == TEXT("finalcolor") || Key == TEXT("final") || Key == TEXT("emissive") || Key == TEXT("emissivecolor") || Key == TEXT("color") || Key == TEXT("rgb") || Key == TEXT("colour"))
				{
					return TEXT("FinalColor");
				}
				if (Key == TEXT("opacity") || Key == TEXT("alpha") || Key == TEXT("a") || Key == TEXT("transparency"))
				{
					return TEXT("Opacity");
				}
				if (Key == TEXT("opacitymask") || Key == TEXT("mask") || Key == TEXT("cutout") || Key == TEXT("alphamask"))
				{
					return TEXT("OpacityMask");
				}
				if (Key == TEXT("basecolor") || Key == TEXT("albedo") || Key == TEXT("diffuse"))
				{
					return TEXT("BaseColor");
				}
				if (Key == TEXT("metallic")) return TEXT("Metallic");
				if (Key == TEXT("roughness")) return TEXT("Roughness");
				if (Key == TEXT("specular")) return TEXT("Specular");
				if (Key == TEXT("normal")) return TEXT("Normal");
				if (Key == TEXT("worldpositionoffset") || Key == TEXT("wpo")) return TEXT("WorldPositionOffset");
				return FString();
			}

			static bool IsScalarOutput(const FString& Name)
			{
				return Name == TEXT("Opacity") || Name == TEXT("OpacityMask") || Name == TEXT("Metallic") || Name == TEXT("Roughness") || Name == TEXT("Specular");
			}

			void FinishOutputs()
			{
				if (Out.bFunction)
				{
					for (FOutput& Output : Out.Outputs)
					{
						if (CheckSource(Output.From, Output.FromOutput, TEXT("outputs")))
						{
							Output.Width = OutputWidth(Output.From, Output.FromOutput);
						}
					}
					if (Out.Outputs.Num() == 0)
					{
						Error(TEXT("output"), TEXT("outputs"), TEXT("a function needs outputs {Name: ref}"));
					}
					return;
				}

				// Names.
				TArray<FOutput> Named;
				for (FOutput& Output : Out.Outputs)
				{
					FString Name = MaterialOutputName(Output.Name);
					if (Name.IsEmpty())
					{
						Error(TEXT("output"), TEXT("outputs"), FString::Printf(TEXT("no material output '%s' - a UI material has FinalColor and Opacity (OpacityMask when Masked)"), *Output.Name));
						continue;
					}
					if (Out.Domain == TEXT("UI") && Name == TEXT("BaseColor"))
					{
						Fix(TEXT("BaseColor: a UI material shows FinalColor (Emissive) - it goes there"));
						Name = TEXT("FinalColor");
					}
					if (Out.Domain == TEXT("UI") && (Name == TEXT("Metallic") || Name == TEXT("Roughness") || Name == TEXT("Specular") || Name == TEXT("Normal") || Name == TEXT("WorldPositionOffset")))
					{
						Warn(TEXT("output"), TEXT("outputs"), FString::Printf(TEXT("%s does nothing in a UI material - left out"), *Name));
						continue;
					}
					if (Named.ContainsByPredicate([&Name](const FOutput& Each) { return Each.Name == Name; }))
					{
						Error(TEXT("output"), TEXT("outputs"), FString::Printf(TEXT("two outputs feed %s"), *Name));
						continue;
					}
					Output.Name = Name;
					Named.Add(Output);
				}
				Out.Outputs = MoveTemp(Named);
				auto FindOutput = [this](const TCHAR* Name) { return Out.Outputs.IndexOfByPredicate([Name](const FOutput& Each) { return Each.Name == Name; }); };

				// The blend mode and the opacity output it reads.
				const int32 Opacity = FindOutput(TEXT("Opacity"));
				const int32 OpacityMask = FindOutput(TEXT("OpacityMask"));
				if (Out.Blend == TEXT("Masked") && Opacity != INDEX_NONE && OpacityMask == INDEX_NONE)
				{
					Out.Outputs[Opacity].Name = TEXT("OpacityMask");
					Fix(TEXT("Opacity -> OpacityMask (a Masked material reads that)"));
				}
				else if (Out.Blend != TEXT("Masked") && OpacityMask != INDEX_NONE && Opacity == INDEX_NONE)
				{
					Out.Outputs[OpacityMask].Name = TEXT("Opacity");
					Fix(FString::Printf(TEXT("OpacityMask -> Opacity (a %s material reads that)"), *Out.Blend));
				}
				if (Out.Blend == TEXT("Opaque") && FindOutput(TEXT("Opacity")) != INDEX_NONE)
				{
					Out.Blend = TEXT("Translucent");
					Fix(TEXT("blend Opaque -> Translucent (it has an Opacity)"));
				}

				// Widths.
				for (FOutput& Output : Out.Outputs)
				{
					if (!CheckSource(Output.From, Output.FromOutput, TEXT("outputs")))
					{
						continue;
					}
					const int32 Width = OutputWidth(Output.From, Output.FromOutput);
					if (IsScalarOutput(Output.Name) && Width > 1)
					{
						Output.From = MaskOf(Output.From, Output.FromOutput, Width == 4 ? TEXT("A") : TEXT("R"));
						Output.FromOutput.Reset();
						Fix(FString::Printf(TEXT("%s got a %s - %s"), *Output.Name, *WidthName(Width), Width == 4 ? TEXT("its alpha is used") : TEXT("its first channel is used (give it a 0..1 mask)")));
					}
					else if (!IsScalarOutput(Output.Name) && Width == 4)
					{
						Output.From = MaskOf(Output.From, Output.FromOutput, TEXT("RGB"));
						Output.FromOutput.Reset();
						Fix(FString::Printf(TEXT("%s got a float4 - its RGB is used"), *Output.Name));
					}
					else if (!IsScalarOutput(Output.Name) && Width == 2)
					{
						Error(TEXT("width"), TEXT("outputs"), FString::Printf(TEXT("%s gets a float2 - it needs a color (float3) or a scalar"), *Output.Name));
					}
					Output.Width = OutputWidth(Output.From, Output.FromOutput);
				}

				if (FindOutput(TEXT("FinalColor")) == INDEX_NONE && FindOutput(TEXT("BaseColor")) == INDEX_NONE)
				{
					Error(TEXT("output"), TEXT("outputs"), TEXT("nothing feeds FinalColor - give outputs {\"FinalColor\": ref, \"Opacity\": ref}"));
					return;
				}

				// Opacity kept in 0..1.
				for (FOutput& Output : Out.Outputs)
				{
					if ((Output.Name == TEXT("Opacity") || Output.Name == TEXT("OpacityMask")) && !Bounded(Output.From, Output.FromOutput, 0))
					{
						const int32 Saturate = Repair(TEXT("Saturate"), FString::Printf(TEXT("_sat%d"), ++Made), { { TEXT("Input"), Output.From, Output.FromOutput } }, 1);
						Output.From = Saturate;
						Output.FromOutput.Reset();
						Added(FString::Printf(TEXT("Saturate on %s"), *Output.Name));
					}
				}

				// The widget's tint and the particles' colors come in through the vertex color.
				const bool bUsesVertexColor = Out.Nodes.ContainsByPredicate([](const FNode& Node) { return Node.bLive && Node.Type == TEXT("VertexColor"); });
				if (Out.Domain == TEXT("UI") && bTint && !bUsesVertexColor)
				{
					const int32 VertexColor = Repair(TEXT("VertexColor"), TEXT("_vc"), {}, 3);
					const int32 Color = FindOutput(TEXT("FinalColor"));
					FOutput& ColorOutput = Out.Outputs[Color];
					const int32 Tinted = Repair(TEXT("Multiply"), TEXT("_tint_rgb"), { { TEXT("A"), ColorOutput.From, ColorOutput.FromOutput }, { TEXT("B"), VertexColor } }, 3);
					ColorOutput.From = Tinted;
					ColorOutput.FromOutput.Reset();
					ColorOutput.Width = 3;

					const FString AlphaName = Out.Blend == TEXT("Masked") ? TEXT("OpacityMask") : TEXT("Opacity");
					const int32 Alpha = FindOutput(*AlphaName);
					if (Alpha != INDEX_NONE)
					{
						FOutput& AlphaOutput = Out.Outputs[Alpha];
						const int32 TintedAlpha = Repair(TEXT("Multiply"), TEXT("_tint_a"), { { TEXT("A"), AlphaOutput.From, AlphaOutput.FromOutput }, { TEXT("B"), VertexColor, TEXT("A") } }, 1);
						AlphaOutput.From = TintedAlpha;
						AlphaOutput.FromOutput.Reset();
					}
					else if (Out.Blend != TEXT("Opaque"))
					{
						FOutput AlphaOutput;
						AlphaOutput.Name = AlphaName;
						AlphaOutput.From = VertexColor;
						AlphaOutput.FromOutput = TEXT("A");
						AlphaOutput.Width = 1;
						Out.Outputs.Add(AlphaOutput);
					}
					Added(TEXT("VertexColor tint (\"tint\": false to leave it out)"));
				}
			}
		};

		FGraph PrepareAt(const TSharedPtr<FJsonObject>& In, bool bFunction, int32 Depth)
		{
			FGraph Graph;
			Graph.bFunction = bFunction;
			if (!In.IsValid())
			{
				Graph.Problems.Add({ TEXT("json"), FString(), TEXT("no graph - give {nodes: [...], outputs: {FinalColor: ..., Opacity: ...}}"), true });
				return Graph;
			}
			FPreparer Preparer(Graph, Depth);
			Preparer.Run(In);
			return Graph;
		}

		bool FunctionSignature(const FString& Function, int32 Depth, TArray<FPinInfo>& OutPins, TArray<FOut>& OutOuts)
		{
			OutPins.Reset();
			OutOuts.Reset();
			if (Function.StartsWith(TEXT("/")) || Depth > 3)
			{
				return false; // a project function: its pins are taken as written, checked when built
			}
			const TSharedPtr<FJsonObject> Definition = NameTBDAILibrary::FindDefinitionJson(Function, TEXT("function"));
			const TSharedPtr<FJsonObject> Graph = Definition.IsValid() ? DefinitionGraph(Definition, true) : nullptr;
			if (!Graph.IsValid())
			{
				return false;
			}
			for (const TSharedPtr<FJsonValue>& Each : GetArray(Graph, TEXT("inputs")))
			{
				if (Each->Type != EJson::Object)
				{
					continue;
				}
				FPinInfo Info;
				Info.Name = GetString(Each->AsObject(), TEXT("name"));
				Info.Names = { Info.Name };
				Info.bOptional = true;
				const int32 Width = WidthOfInputType(GetString(Each->AsObject(), TEXT("type"), TEXT("Scalar")));
				Info.Width = Width > 0 ? FString::FromInt(Width) : FString();
				OutPins.Add(Info);
			}
			const FGraph Inner = PrepareAt(Graph, true, Depth + 1);
			for (const FOutput& Output : Inner.Outputs)
			{
				OutOuts.Add({ Output.Name, Output.Width });
			}
			return true;
		}

		// ---- Stats -------------------------------------------------------------------------------------------------

		FString StatsFile()
		{
			return FPaths::ProjectSavedDir() / TEXT("NameTBDAI") / TEXT("stats.json");
		}

		/** Per model: is the material being worked on still clean (no failed call yet)? */
		TMap<FString, bool>& OpenAttempts()
		{
			static TMap<FString, bool> Open;
			return Open;
		}

		void AddCount(const TSharedPtr<FJsonObject>& Object, const FString& Field, double Amount)
		{
			Object->SetNumberField(Field, GetNumber(Object, Field) + Amount);
		}
	}

	// =================================================================================================================

	bool FGraph::HasErrors() const
	{
		return CountErrors() > 0;
	}

	int32 FGraph::CountErrors() const
	{
		int32 Count = 0;
		for (const FProblem& Problem : Problems)
		{
			Count += Problem.bError ? 1 : 0;
		}
		return Count;
	}

	int32 FGraph::Find(const FString& Id) const
	{
		return Nodes.IndexOfByPredicate([&Id](const FNode& Node) { return Node.Id == Id; });
	}

	static FString ProblemLine(const FGraph& Graph, const FProblem& Problem)
	{
		if (Problem.Node.IsEmpty())
		{
			return Problem.Message;
		}
		FString Node = Problem.Node;
		FString Macro;
		if (Node.Split(TEXT("/"), &Macro, nullptr))
		{
			Node = FString::Printf(TEXT("%s (inside %s)"), *Problem.Node, *Macro);
		}
		return Node + TEXT(": ") + Problem.Message;
	}

	TArray<FString> FGraph::ErrorLines() const
	{
		TArray<FString> Lines;
		for (const FProblem& Problem : Problems)
		{
			if (Problem.bError)
			{
				Lines.AddUnique(ProblemLine(*this, Problem));
			}
		}
		return Lines;
	}

	TArray<FString> FGraph::WarningLines() const
	{
		TArray<FString> Lines;
		for (const FProblem& Problem : Problems)
		{
			if (!Problem.bError)
			{
				Lines.AddUnique(ProblemLine(*this, Problem));
			}
		}
		return Lines;
	}

	TSharedPtr<FJsonObject> FGraph::Summary() const
	{
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetBoolField(TEXT("ok"), !HasErrors());
		if (HasErrors())
		{
			Out->SetField(TEXT("errors"), Arr(ErrorLines()));
		}
		if (Fixed.Num() > 0)
		{
			Out->SetField(TEXT("fixed"), Arr(Fixed));
		}
		if (Added.Num() > 0)
		{
			Out->SetField(TEXT("added"), Arr(Added));
		}
		const TArray<FString> Warnings = WarningLines();
		if (Warnings.Num() > 0)
		{
			Out->SetField(TEXT("warnings"), Arr(Warnings));
		}
		int32 Live = 0;
		TArray<FString> Parameters;
		for (const FNode& Node : Nodes)
		{
			if (!Node.bLive)
			{
				continue;
			}
			++Live;
			if (Node.Type.Contains(TEXT("Parameter")))
			{
				Parameters.AddUnique(GetString(Node.Props, TEXT("ParameterName")));
			}
		}
		Out->SetNumberField(TEXT("nodes"), Live);
		if (Parameters.Num() > 0)
		{
			Out->SetField(TEXT("parameters"), Arr(Parameters));
		}
		TSharedPtr<FJsonObject> OutputWidths = MakeShared<FJsonObject>();
		for (const FOutput& Output : Outputs)
		{
			OutputWidths->SetStringField(Output.Name, Output.Width > 0 ? FString::Printf(TEXT("float%d"), Output.Width) : FString(TEXT("?")));
		}
		Out->SetObjectField(TEXT("outputs"), OutputWidths);
		if (!bFunction)
		{
			Out->SetStringField(TEXT("blend"), Blend);
		}
		return Out;
	}

	FGraph Prepare(const TSharedPtr<FJsonObject>& Graph, bool bFunction)
	{
		return PrepareAt(Graph, bFunction, 0);
	}

	const TCHAR* const HelpersInclude = TEXT("/Plugin/NameTBDAI/NameTBDUI.ush");

	bool HelpersAvailable()
	{
		return AllShaderSourceDirectoryMappings().Contains(TEXT("/Plugin/NameTBDAI"));
	}

	namespace
	{
		/** Name used in Code as a whole identifier (not part of a longer one). Case-sensitive, as HLSL is. */
		bool UsesIdentifier(const FString& Code, const FString& Name)
		{
			auto IsIdentifierChar = [](TCHAR Char) { return FChar::IsAlnum(Char) || Char == TEXT('_'); };
			int32 From = 0;
			while (true)
			{
				const int32 At = Code.Find(*Name, ESearchCase::CaseSensitive, ESearchDir::FromStart, From);
				if (At == INDEX_NONE)
				{
					return false;
				}
				const bool bStart = At == 0 || !IsIdentifierChar(Code[At - 1]);
				const bool bEnd = At + Name.Len() >= Code.Len() || !IsIdentifierChar(Code[At + Name.Len()]);
				if (bStart && bEnd)
				{
					return true;
				}
				From = At + 1;
			}
		}

		/** Every use of Name in Code picks color channels only (Color.rgb, Color.r, Color.xy...) - never its alpha or all four. */
		bool UsesOnlyRgb(const FString& Code, const FString& Name)
		{
			auto IsIdentifierChar = [](TCHAR Char) { return FChar::IsAlnum(Char) || Char == TEXT('_'); };
			for (int32 At = Code.Find(*Name, ESearchCase::CaseSensitive); At != INDEX_NONE; At = Code.Find(*Name, ESearchCase::CaseSensitive, ESearchDir::FromStart, At + 1))
			{
				int32 End = At + Name.Len();
				if ((At > 0 && IsIdentifierChar(Code[At - 1])) || (End < Code.Len() && IsIdentifierChar(Code[End])))
				{
					continue;
				}
				while (End < Code.Len() && FChar::IsWhitespace(Code[End]))
				{
					++End;
				}
				if (End >= Code.Len() || Code[End] != TEXT('.'))
				{
					return false;
				}
				int32 Channels = 0;
				for (++End; End < Code.Len() && IsIdentifierChar(Code[End]); ++End, ++Channels)
				{
					const TCHAR Char = Code[End];
					if (Char != TEXT('r') && Char != TEXT('g') && Char != TEXT('b') && Char != TEXT('x') && Char != TEXT('y') && Char != TEXT('z'))
					{
						return false;
					}
				}
				if (Channels == 0)
				{
					return false;
				}
			}
			return true;
		}

		/** "code": a string, or a list of lines. */
		FString CodeOf(const TSharedPtr<FJsonObject>& Object)
		{
			const TSharedPtr<FJsonValue> Value = Object.IsValid() ? Object->TryGetField(TEXT("code")) : nullptr;
			if (!Value.IsValid())
			{
				return FString();
			}
			if (Value->Type == EJson::Array)
			{
				TArray<FString> Lines;
				for (const TSharedPtr<FJsonValue>& Line : Value->AsArray())
				{
					Lines.Add(Line->AsString());
				}
				return FString::Join(Lines, TEXT("\n"));
			}
			return Value->AsString();
		}

		bool IsColorValue(const TSharedPtr<FJsonValue>& Value)
		{
			if (!Value.IsValid())
			{
				return false;
			}
			if (Value->Type == EJson::String)
			{
				return !IsNumberText(Value->AsString());
			}
			return Value->Type == EJson::Array && Value->AsArray().Num() >= 3;
		}
	}

	TSharedPtr<FJsonObject> HlslToGraph(const TSharedPtr<FJsonObject>& Hlsl, bool bFunction, FString& OutError)
	{
		if (!Hlsl.IsValid())
		{
			OutError = TEXT("hlsl needs {code, params}");
			return nullptr;
		}
		TSharedPtr<FJsonObject> Graph = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Nodes;

		if (bFunction)
		{
			// One Custom node per output, reading the function's own inputs (and the widget's time) by name.
			Graph->SetArrayField(TEXT("inputs"), GetArray(Hlsl, TEXT("inputs")));
			TSharedPtr<FJsonObject> Outputs = MakeShared<FJsonObject>();
			const TSharedPtr<FJsonObject> OutputSpecs = GetObject(Hlsl, TEXT("outputs"));
			if (!OutputSpecs.IsValid() || OutputSpecs->Values.Num() == 0)
			{
				OutError = TEXT("an hlsl function needs outputs {Name: {type, code}}");
				return nullptr;
			}
			for (const auto& Pair : OutputSpecs->Values)
			{
				const TSharedPtr<FJsonObject> Spec = Pair.Value->Type == EJson::Object ? Pair.Value->AsObject() : nullptr;
				const FString Code = CodeOf(Spec);
				TSharedPtr<FJsonObject> Inputs = MakeShared<FJsonObject>();
				for (const TSharedPtr<FJsonValue>& Input : GetArray(Hlsl, TEXT("inputs")))
				{
					const FString Name = GetString(Input->AsObject(), TEXT("name"));
					if (UsesIdentifier(Code, Name))
					{
						Inputs->SetStringField(Name, Name);
					}
				}
				if (UsesIdentifier(Code, TEXT("time")))
				{
					Inputs->SetStringField(TEXT("time"), TEXT("$time"));
				}
				TSharedPtr<FJsonObject> Node = MakeShared<FJsonObject>();
				Node->SetStringField(TEXT("id"), TEXT("_out_") + Pair.Key);
				Node->SetStringField(TEXT("type"), TEXT("Custom"));
				Node->SetStringField(TEXT("code"), Code);
				Node->SetStringField(TEXT("output"), GetString(Spec, TEXT("type"), TEXT("float1")));
				Node->SetObjectField(TEXT("inputs"), Inputs);
				TSharedPtr<FJsonObject> NodeProps = MakeShared<FJsonObject>();
				const FString Title = GetString(Hlsl, TEXT("title"));
				NodeProps->SetStringField(TEXT("Description"), Title.IsEmpty() ? Pair.Key : Title + TEXT(" ") + Pair.Key);
				Node->SetObjectField(TEXT("props"), NodeProps);
				Nodes.Add(MakeShared<FJsonValueObject>(Node));
				Outputs->SetStringField(Pair.Key, TEXT("_out_") + Pair.Key);
			}
			Graph->SetArrayField(TEXT("nodes"), Nodes);
			Graph->SetObjectField(TEXT("outputs"), Outputs);
			return Graph;
		}

		const FString Code = CodeOf(Hlsl);
		if (Code.TrimStartAndEnd().IsEmpty())
		{
			OutError = TEXT("hlsl needs code: HLSL that returns float4(color, alpha), using uv (float2, 0..1, (0,0) top left), time (seconds) and the params by name");
			return nullptr;
		}
		TSharedPtr<FJsonObject> Inputs = MakeShared<FJsonObject>();
		if (UsesIdentifier(Code, TEXT("uv")))
		{
			Inputs->SetStringField(TEXT("uv"), TEXT("$uv"));
		}
		if (UsesIdentifier(Code, TEXT("time")))
		{
			Inputs->SetStringField(TEXT("time"), TEXT("$time"));
		}
		if (const TSharedPtr<FJsonObject> Params = GetObject(Hlsl, TEXT("params")))
		{
			static const TSet<FString> Reserved = { TEXT("uv"), TEXT("time"), TEXT("Parameters"), TEXT("View"), TEXT("Primitive"), TEXT("in"), TEXT("out"),
				TEXT("inout"), TEXT("return"), TEXT("for"), TEXT("if"), TEXT("else"), TEXT("while"), TEXT("float"), TEXT("int"), TEXT("bool"), TEXT("true"), TEXT("false"),
				TEXT("sin"), TEXT("cos"), TEXT("tan"), TEXT("abs"), TEXT("min"), TEXT("max"), TEXT("pow"), TEXT("exp"), TEXT("log"), TEXT("frac"), TEXT("floor"),
				TEXT("ceil"), TEXT("round"), TEXT("sign"), TEXT("sqrt"), TEXT("lerp"), TEXT("step"), TEXT("smoothstep"), TEXT("saturate"), TEXT("clamp"),
				TEXT("length"), TEXT("distance"), TEXT("dot"), TEXT("cross"), TEXT("normalize"), TEXT("atan2"), TEXT("fmod"), TEXT("radians"), TEXT("degrees") };
			for (const auto& Pair : Params->Values)
			{
				bool bIdentifier = !Pair.Key.IsEmpty() && (FChar::IsAlpha(Pair.Key[0]) || Pair.Key[0] == TEXT('_'));
				for (const TCHAR Char : Pair.Key)
				{
					bIdentifier &= FChar::IsAlnum(Char) || Char == TEXT('_');
				}
				if (!bIdentifier || Reserved.Contains(Pair.Key) || (Pair.Key.Len() <= 2 && FChar::IsLower(Pair.Key[0])))
				{
					OutError = FString::Printf(TEXT("param '%s': a param is a variable in the code - letters, digits and _, not an HLSL word or built-in (uv and time are given); short lower-case names clash with the code's own variables - use names like Speed, GlowColor"), *Pair.Key);
					return nullptr;
				}
				// {Name: value} or {Name: {value, group, min, max}}.
				TSharedPtr<FJsonObject> Param = MakeShared<FJsonObject>();
				if (Pair.Value->Type == EJson::Object)
				{
					Param->Values = Pair.Value->AsObject()->Values;
					if (!Param->HasField(TEXT("value")) && Param->HasField(TEXT("default")))
					{
						Param->SetField(TEXT("value"), Param->TryGetField(TEXT("default")));
					}
				}
				else
				{
					Param->SetField(TEXT("value"), Pair.Value);
				}
				Param->SetStringField(TEXT("param"), Pair.Key);
				if (IsColorValue(Param->TryGetField(TEXT("value"))) && UsesOnlyRgb(Code, Pair.Key))
				{
					// Only its color channels are read: the parameter's RGB straight in (float3), no alpha appended.
					Inputs->SetObjectField(Pair.Key, Param);
				}
				else if (IsColorValue(Param->TryGetField(TEXT("value"))))
				{
					// A color reaches the code as float4: the parameter's RGB with its A appended.
					TSharedPtr<FJsonObject> Append = MakeShared<FJsonObject>();
					Append->SetStringField(TEXT("id"), TEXT("_rgba_") + Pair.Key);
					Append->SetStringField(TEXT("type"), TEXT("AppendVector"));
					TSharedPtr<FJsonObject> AppendInputs = MakeShared<FJsonObject>();
					AppendInputs->SetObjectField(TEXT("A"), Param);
					AppendInputs->SetStringField(TEXT("B"), TEXT("p_") + Pair.Key + TEXT(".A"));
					Append->SetObjectField(TEXT("inputs"), AppendInputs);
					Nodes.Add(MakeShared<FJsonValueObject>(Append));
					Inputs->SetStringField(Pair.Key, TEXT("_rgba_") + Pair.Key);
				}
				else
				{
					Inputs->SetObjectField(Pair.Key, Param);
				}
			}
		}
		TSharedPtr<FJsonObject> Custom = MakeShared<FJsonObject>();
		Custom->SetStringField(TEXT("id"), TEXT("_hlsl"));
		Custom->SetStringField(TEXT("type"), TEXT("Custom"));
		Custom->SetStringField(TEXT("code"), Code);
		Custom->SetStringField(TEXT("output"), TEXT("float4"));
		Custom->SetObjectField(TEXT("inputs"), Inputs);
		// The node's title in the graph: the material's name instead of "Custom".
		TSharedPtr<FJsonObject> CustomProps = MakeShared<FJsonObject>();
		CustomProps->SetStringField(TEXT("Description"), GetString(Hlsl, TEXT("title"), TEXT("HLSL")));
		Custom->SetObjectField(TEXT("props"), CustomProps);
		Nodes.Add(MakeShared<FJsonValueObject>(Custom));
		for (const TCHAR* Channels : { TEXT("rgb"), TEXT("a") })
		{
			TSharedPtr<FJsonObject> Mask = MakeShared<FJsonObject>();
			Mask->SetStringField(TEXT("id"), FString(TEXT("_hlsl_")) + Channels);
			Mask->SetStringField(TEXT("type"), TEXT("ComponentMask"));
			TSharedPtr<FJsonObject> MaskProps = MakeShared<FJsonObject>();
			MaskProps->SetStringField(TEXT("channels"), Channels);
			Mask->SetObjectField(TEXT("props"), MaskProps);
			TSharedPtr<FJsonObject> MaskInputs = MakeShared<FJsonObject>();
			MaskInputs->SetStringField(TEXT("Input"), TEXT("_hlsl"));
			Mask->SetObjectField(TEXT("inputs"), MaskInputs);
			Nodes.Add(MakeShared<FJsonValueObject>(Mask));
		}
		Graph->SetArrayField(TEXT("nodes"), Nodes);
		TSharedPtr<FJsonObject> Outputs = MakeShared<FJsonObject>();
		Outputs->SetStringField(TEXT("FinalColor"), TEXT("_hlsl_rgb"));
		Outputs->SetStringField(TEXT("Opacity"), TEXT("_hlsl_a"));
		Graph->SetObjectField(TEXT("outputs"), Outputs);
		for (const TCHAR* Key : { TEXT("blend"), TEXT("domain"), TEXT("tint") })
		{
			if (Hlsl->HasField(Key))
			{
				Graph->SetField(Key, Hlsl->TryGetField(Key));
			}
		}
		return Graph;
	}

	TSharedPtr<FJsonObject> DefinitionGraph(const TSharedPtr<FJsonObject>& Definition, bool bFunction)
	{
		if (const TSharedPtr<FJsonObject> Hlsl = GetObject(Definition, TEXT("hlsl")))
		{
			// Its Custom nodes are titled with the definition's name.
			TSharedPtr<FJsonObject> Titled = MakeShared<FJsonObject>();
			Titled->Values = Hlsl->Values;
			if (!Titled->HasField(TEXT("title")))
			{
				Titled->SetStringField(TEXT("title"), GetString(Definition, TEXT("name")));
			}
			FString Error;
			const TSharedPtr<FJsonObject> Graph = HlslToGraph(Titled, bFunction, Error);
			if (!Graph.IsValid())
			{
				UE_LOG(LogTemp, Warning, TEXT("NameTBD AI: %s: %s"), *GetString(Definition, TEXT("name")), *Error);
			}
			return Graph;
		}
		return GetObject(Definition, TEXT("graph"));
	}

	TSharedPtr<FJsonObject> MergeWithMaster(const TSharedPtr<FJsonObject>& MasterGraph, const TSharedPtr<FJsonObject>& Extra, FString& OutError)
	{
		TSharedPtr<FJsonObject> Merged = DeepCopy(MasterGraph);
		const TSharedPtr<FJsonObject> Added = DeepCopy(Extra);
		TArray<TSharedPtr<FJsonValue>> Nodes = GetArray(Merged, TEXT("nodes"));
		TSet<FString> Ids;
		for (const TSharedPtr<FJsonValue>& Node : Nodes)
		{
			Ids.Add(GetString(Node->AsObject(), TEXT("id")));
		}
		const TSharedPtr<FJsonObject> MasterOutputs = GetObject(Merged, TEXT("outputs"));

		// "master.FinalColor" -> what the master's FinalColor was.
		TFunction<void(TSharedPtr<FJsonValue>&)> Rewrite = [&](TSharedPtr<FJsonValue>& Value)
		{
			FString Text;
			if (!Value.IsValid() || !Value->TryGetString(Text) || Value->Type != EJson::String || !Text.StartsWith(TEXT("master."), ESearchCase::IgnoreCase))
			{
				return;
			}
			const FString Wanted = Normalize(Text.Mid(7));
			if (MasterOutputs.IsValid())
			{
				for (const auto& Pair : MasterOutputs->Values)
				{
					const FString Key = Normalize(Pair.Key);
					if (Key == Wanted || (Wanted == TEXT("color") && Key == TEXT("finalcolor")) || (Wanted == TEXT("alpha") && Key == TEXT("opacity")))
					{
						Value = DeepCopy(Pair.Value);
						return;
					}
				}
			}
			OutError = FString::Printf(TEXT("'%s': the master's outputs are FinalColor and Opacity"), *Text);
		};

		TArray<TSharedPtr<FJsonValue>> Items;
		const TSharedPtr<FJsonValue> AddedNodes = Added->TryGetField(TEXT("nodes"));
		if (AddedNodes.IsValid() && AddedNodes->Type == EJson::Array)
		{
			Items = AddedNodes->AsArray();
		}
		else if (AddedNodes.IsValid() && AddedNodes->Type == EJson::Object)
		{
			for (const auto& Pair : AddedNodes->AsObject()->Values)
			{
				if (Pair.Value->Type == EJson::Object)
				{
					Pair.Value->AsObject()->SetStringField(TEXT("id"), GetString(Pair.Value->AsObject(), TEXT("id"), Pair.Key));
					Items.Add(Pair.Value);
				}
			}
		}
		for (const TSharedPtr<FJsonValue>& Item : Items)
		{
			if (Item->Type != EJson::Object)
			{
				continue;
			}
			const TSharedPtr<FJsonObject> Node = Item->AsObject();
			const FString Id = GetString(Node, TEXT("id"));
			if (Ids.Contains(Id))
			{
				OutError = FString::Printf(TEXT("the id '%s' is the master's own node - pick another id"), *Id);
				return nullptr;
			}
			Ids.Add(Id);
			for (const TCHAR* Key : { TEXT("inputs"), TEXT("in") })
			{
				if (TSharedPtr<FJsonObject> Inputs = GetObject(Node, Key))
				{
					for (auto& Pair : Inputs->Values)
					{
						Rewrite(Pair.Value);
					}
				}
			}
			Nodes.Add(Item);
		}
		Merged->SetArrayField(TEXT("nodes"), Nodes);
		if (TSharedPtr<FJsonObject> Outputs = GetObject(Added, TEXT("outputs")))
		{
			TSharedPtr<FJsonObject> NewOutputs = MasterOutputs.IsValid() ? MasterOutputs : MakeShared<FJsonObject>();
			for (auto& Pair : Outputs->Values)
			{
				TSharedPtr<FJsonValue> Value = Pair.Value;
				Rewrite(Value);
				// Replace the master's output of the same meaning ("Emissive" replaces "FinalColor").
				TArray<FString> Keys;
				NewOutputs->Values.GetKeys(Keys);
				for (const FString& Key : Keys)
				{
					const FString A = Normalize(Key);
					const FString B = Normalize(Pair.Key);
					const bool bColorA = A == TEXT("finalcolor") || A == TEXT("emissive") || A == TEXT("emissivecolor") || A == TEXT("color");
					const bool bColorB = B == TEXT("finalcolor") || B == TEXT("emissive") || B == TEXT("emissivecolor") || B == TEXT("color");
					const bool bAlphaA = A == TEXT("opacity") || A == TEXT("alpha");
					const bool bAlphaB = B == TEXT("opacity") || B == TEXT("alpha");
					if (A == B || (bColorA && bColorB) || (bAlphaA && bAlphaB))
					{
						NewOutputs->RemoveField(Key);
					}
				}
				NewOutputs->SetField(Pair.Key, Value);
			}
			Merged->SetObjectField(TEXT("outputs"), NewOutputs);
		}
		for (const TCHAR* Key : { TEXT("blend"), TEXT("domain"), TEXT("tint") })
		{
			if (Added->HasField(Key))
			{
				Merged->SetField(Key, Added->TryGetField(Key));
			}
		}
		if (!OutError.IsEmpty())
		{
			return nullptr;
		}
		return Merged;
	}

	int32 FindInputIndex(UMaterialExpression* Expression, const TArray<FString>& Names)
	{
		if (!Expression)
		{
			return INDEX_NONE;
		}
		int32 Count = 0;
		for (int32 Index = 0; Index < 64 && Expression->GetInput(Index); ++Index)
		{
			++Count;
			const FString Engine = Normalize(StripPinName(Expression->GetInputName(Index).ToString()));
			for (const FString& Name : Names)
			{
				if (Normalize(Name) == Engine)
				{
					return Index;
				}
			}
		}
		return Count == 1 ? 0 : INDEX_NONE;
	}

	int32 FindOutputIndex(UMaterialExpression* Expression, const FString& Name)
	{
		return FindOutputIn(OutTableOf(Expression), Name);
	}

	FString HintFor(const FString& CompileError)
	{
		const FString Lower = CompileError.ToLower();
		for (const TPair<FString, FString>& Hint : GetCatalog().Hints)
		{
			if (!Hint.Key.IsEmpty() && Lower.Contains(Hint.Key))
			{
				return Hint.Value;
			}
		}
		return FString();
	}

	FString DisplayId(const FGraph& Graph, int32 NodeIndex)
	{
		if (!Graph.Nodes.IsValidIndex(NodeIndex))
		{
			return FString();
		}
		const FNode& Node = Graph.Nodes[NodeIndex];
		if (Node.Id == TEXT("_hlsl") || Node.Id.StartsWith(TEXT("_out_")))
		{
			return TEXT("the hlsl code");
		}
		return Node.Macro.IsEmpty() ? Node.Id : FString::Printf(TEXT("%s (inside %s)"), *Node.Id, *Node.Macro);
	}

	// ---- What the model can look up --------------------------------------------------------------------------------

	namespace
	{
		FString DescribeRule(const FNodeSpec& Spec)
		{
			const FString& Rule = Spec.Out;
			if (Rule.StartsWith(TEXT("max:")))
			{
				return TEXT("widest of ") + Rule.Mid(4).Replace(TEXT(","), TEXT("/"));
			}
			if (Rule.StartsWith(TEXT("same:")))
			{
				return TEXT("width of ") + Rule.Mid(5);
			}
			if (Rule.StartsWith(TEXT("sum:")))
			{
				return TEXT("A + B widths");
			}
			if (Rule == TEXT("mask"))
			{
				return TEXT("the channels kept");
			}
			if (Rule == TEXT("list") && Spec.Class)
			{
				TArray<FString> Names;
				for (const FOut& Out : OutTableOf(Cast<UMaterialExpression>(Spec.Class->GetDefaultObject())))
				{
					Names.Add(FString::Printf(TEXT("%s float%d"), *Out.Name, FMath::Max(Out.Width, 1)));
				}
				return FString::Join(Names, TEXT(", "));
			}
			if (FCString::Atoi(*Rule) > 0)
			{
				return TEXT("float") + Rule;
			}
			return Rule;
		}

		FString PinLine(const FNodeSpec& Spec)
		{
			TArray<FString> Parts;
			for (const FPinSpec& Pin : Spec.Pins)
			{
				Parts.Add(Pin.bOptional ? Pin.Name + TEXT("?") : Pin.Name);
			}
			return FString::Join(Parts, TEXT(", "));
		}

		TSharedPtr<FJsonObject> DescribeNodeDetail(const FNodeSpec* Spec, UClass* Class)
		{
			TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
			if (Spec)
			{
				Out->SetStringField(TEXT("about"), Spec->About);
				if (Spec->Aka.Num() > 0)
				{
					Out->SetStringField(TEXT("also_called"), FString::Join(Spec->Aka, TEXT(", ")));
				}
			}
			TArray<FString> Pins;
			for (const FPinInfo& Pin : PinsOf(Spec, Class))
			{
				FString Line = Pin.Name;
				if (Pin.bOptional)
				{
					Line += TEXT(" (optional)");
				}
				if (!Pin.Width.IsEmpty() && Pin.Width != TEXT("any"))
				{
					Line += Pin.Width == TEXT("1|out") ? TEXT(" scalar or same width") : FString::Printf(TEXT(" float%s"), *Pin.Width);
				}
				Pins.Add(Line);
			}
			Out->SetField(TEXT("inputs"), Arr(Pins));
			TArray<FString> Outputs;
			for (const FOut& Each : OutTableOf(Cast<UMaterialExpression>(Class->GetDefaultObject())))
			{
				Outputs.Add(FString::Printf(TEXT("%s%s"), Each.Name.IsEmpty() ? TEXT("(default)") : *Each.Name,
					Each.Width > 0 ? *FString::Printf(TEXT(" float%d"), Each.Width) : (Spec ? *FString::Printf(TEXT(" %s"), *DescribeRule(*Spec)) : TEXT(""))));
			}
			Out->SetField(TEXT("outputs"), Arr(Outputs));
			TArray<FString> Settings;
			for (TFieldIterator<FProperty> It(Class); It && Settings.Num() < 20; ++It)
			{
				const FStructProperty* Struct = CastField<FStructProperty>(*It);
				if (It->HasAnyPropertyFlags(CPF_Edit) && !(Struct && Struct->Struct->GetName().Contains(TEXT("ExpressionInput"))) && It->GetOwnerClass() != UMaterialExpression::StaticClass())
				{
					Settings.AddUnique(It->GetName());
				}
			}
			if (Spec)
			{
				for (const auto& Pair : Spec->PropAliases)
				{
					Settings.AddUnique(FString::Printf(TEXT("%s (= %s)"), *Pair.Key, *Pair.Value));
				}
			}
			Out->SetField(TEXT("props"), Arr(Settings));
			return Out;
		}
	}

	TSharedPtr<FJsonObject> DescribeMacros()
	{
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		const FCatalog& Catalog = GetCatalog();
		for (const auto& Pair : Catalog.Macros)
		{
			TArray<FString> Inputs;
			if (TSharedPtr<FJsonObject> In = GetObject(Pair.Value, TEXT("in")))
			{
				for (const auto& Input : In->Values)
				{
					const TSharedPtr<FJsonValue> Default = Input.Value->Type == EJson::Object ? Input.Value->AsObject()->TryGetField(TEXT("def")) : nullptr;
					const FString DefaultText = Default.IsValid() ? ToString(Default).Replace(TEXT("\""), TEXT("")) : FString();
					Inputs.Add(FString::Printf(TEXT("%s=%s"), *Input.Key, DefaultText == TEXT("$uv") ? TEXT("uv") : *DefaultText));
				}
			}
			TArray<FString> Outputs;
			if (TSharedPtr<FJsonObject> MacroOut = GetObject(Pair.Value, TEXT("out")))
			{
				MacroOut->Values.GetKeys(Outputs);
			}
			Out->SetStringField(Catalog.MacroNames.FindRef(Pair.Key), FString::Printf(TEXT("%s. In: %s -> %s"), *GetString(Pair.Value, TEXT("about")),
				*FString::Join(Inputs, TEXT(", ")), *FString::Join(Outputs, TEXT(", "))));
		}
		return Out;
	}

	TSharedPtr<FJsonObject> DescribeNodes(const FString& Filter)
	{
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		const FCatalog& Catalog = GetCatalog();
		const FString Key = Normalize(Filter);

		if (Key == TEXT("all"))
		{
			TArray<FString> Names;
			for (TObjectIterator<UClass> It; It; ++It)
			{
				if (It->IsChildOf(UMaterialExpression::StaticClass()) && !It->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_Hidden))
				{
					Names.Add(ShortClassName(*It));
				}
			}
			Names.Sort();
			Out->SetField(TEXT("material_nodes"), Arr(Names));
			Out->SetStringField(TEXT("note"), TEXT("Every expression the engine has; outside the catalog a node is checked by its pin names only. Details: list_options material_nodes <Name>."));
			return Out;
		}

		if (Key.IsEmpty())
		{
			TMap<FString, TArray<FString>> ByCategory;
			for (const FNodeSpec& Spec : Catalog.Nodes)
			{
				if (Spec.bHidden || !Spec.Class)
				{
					continue;
				}
				ByCategory.FindOrAdd(Spec.Category).Add(FString::Printf(TEXT("%s(%s) -> %s: %s"), *Spec.Name, *PinLine(Spec), *DescribeRule(Spec), *Spec.About));
			}
			TSharedPtr<FJsonObject> Nodes = MakeShared<FJsonObject>();
			for (const auto& Pair : ByCategory)
			{
				Nodes->SetField(Pair.Key, Arr(Pair.Value));
			}
			Out->SetObjectField(TEXT("nodes"), Nodes);
			Out->SetObjectField(TEXT("macros"), DescribeMacros());
			Out->SetStringField(TEXT("rules"), TEXT("Inputs: \"id\" or \"id.Output\" (R, G, B, A, RGB...), a number, [x, y], \"#RRGGBB\", \"$uv\", \"$time\", or {\"param\": \"Name\", \"value\": v} for a tunable parameter. Pin? = optional. Scalars mix with any width; float3 and float4 are matched for you; other widths must agree. Outputs: FinalColor (color) and Opacity (0..1). VertexColor is multiplied in for you. validate_material checks a graph without building it."));
			return Out;
		}

		// One node (or a few) in detail.
		TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
		for (const FNodeSpec& Spec : Catalog.Nodes)
		{
			bool bMatch = Normalize(Spec.Name).Contains(Key);
			for (const FString& Alias : Spec.Aka)
			{
				bMatch |= Normalize(Alias) == Key;
			}
			if (bMatch && Spec.Class && !Spec.bHidden && Details->Values.Num() < 6)
			{
				Details->SetObjectField(Spec.Name, DescribeNodeDetail(&Spec, Spec.Class));
			}
		}
		const TSharedPtr<FJsonObject> Macros = DescribeMacros();
		for (const auto& Pair : Macros->Values)
		{
			if (Normalize(Pair.Key).Contains(Key))
			{
				Details->SetField(Pair.Key + TEXT(" (macro)"), Pair.Value);
			}
		}
		if (Details->Values.Num() == 0)
		{
			if (UClass* Class = FindExpressionClass(Filter))
			{
				Details->SetObjectField(ShortClassName(Class), DescribeNodeDetail(FindSpecForClass(Class), Class));
			}
		}
		if (Details->Values.Num() == 0)
		{
			Out->SetStringField(TEXT("error"), FString::Printf(TEXT("No node or macro '%s'%s"), *Filter, *Suggest(Filter, AllTypeNames())));
			return Out;
		}
		Out->SetObjectField(TEXT("material_nodes"), Details);
		return Out;
	}

	// ---- Stats -----------------------------------------------------------------------------------------------------

	void RecordAttempt(const FString& InModel, const FGraph* Graph, bool bBuildCall, bool bCompiled, int32 CompileErrors)
	{
		const FString Model = InModel.IsEmpty() ? FString(TEXT("unknown")) : InModel;
		FString Text;
		FFileHelper::LoadFileToString(Text, *StatsFile());
		TSharedPtr<FJsonObject> Stats = ParseObject(Text);
		if (!Stats.IsValid())
		{
			Stats = MakeShared<FJsonObject>();
			Stats->SetStringField(TEXT("about"), TEXT("Material generation per model: materials = built and compiled, first_try = built with no failed validate / build before it, instances = made from a master (can't fail), calls = validate + build calls, problems = the mistakes the validator caught, by kind. Delete the file to start over."));
		}
		TSharedPtr<FJsonObject> Models = GetObject(Stats, TEXT("models"));
		if (!Models.IsValid())
		{
			Models = MakeShared<FJsonObject>();
			Stats->SetObjectField(TEXT("models"), Models);
		}
		TSharedPtr<FJsonObject> Entry = GetObject(Models, Model);
		if (!Entry.IsValid())
		{
			Entry = MakeShared<FJsonObject>();
			Models->SetObjectField(Model, Entry);
		}

		if (!Graph)
		{
			AddCount(Entry, TEXT("instances"), 1);
		}
		else
		{
			AddCount(Entry, TEXT("calls"), 1);
			AddCount(Entry, TEXT("fixed"), Graph->Fixed.Num());
			AddCount(Entry, TEXT("compile_errors"), CompileErrors);
			TSharedPtr<FJsonObject> Kinds = GetObject(Entry, TEXT("problems"));
			if (!Kinds.IsValid())
			{
				Kinds = MakeShared<FJsonObject>();
				Entry->SetObjectField(TEXT("problems"), Kinds);
			}
			for (const FProblem& Problem : Graph->Problems)
			{
				if (Problem.bError)
				{
					AddCount(Kinds, Problem.Code, 1);
				}
			}
			if (CompileErrors > 0)
			{
				AddCount(Kinds, TEXT("compile"), CompileErrors);
			}

			const bool bOk = !Graph->HasErrors() && (!bBuildCall || bCompiled);
			bool& bClean = OpenAttempts().FindOrAdd(Model, true);
			bClean &= bOk;
			if (bBuildCall && bOk)
			{
				AddCount(Entry, TEXT("materials"), 1);
				AddCount(Entry, TEXT("first_try"), bClean ? 1 : 0);
				OpenAttempts().Remove(Model);
			}
			const double Materials = GetNumber(Entry, TEXT("materials"));
			if (Materials > 0)
			{
				Entry->SetNumberField(TEXT("first_try_rate"), FMath::RoundToDouble(GetNumber(Entry, TEXT("first_try")) / Materials * 1000.0) / 1000.0);
			}
		}
		Stats->SetStringField(TEXT("updated"), FDateTime::Now().ToIso8601());
		FFileHelper::SaveStringToFile(ToString(Stats, true), *StatsFile());
	}
}
