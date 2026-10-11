// Copyright AR GameSystems 2026. All rights reserved.

#include "NameTBDAILibrary.h"

#include "AssetToolsModule.h"
#include "Factories/MaterialFactoryNew.h"
#include "Factories/MaterialFunctionFactoryNew.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "IAssetTools.h"
#include "MaterialEditingLibrary.h"
#include "MaterialShared.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialFunction.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "CoreGlobals.h"
#include "Misc/ITransaction.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/EnumProperty.h"
#include "UObject/UnrealType.h"
#include "NameTBDAIDocs.h"
#include "NameTBDAIHelpers.h"
#include "NameTBDAIJson.h"
#include "NameTBDAIMaterialGraph.h"
#include "NameTBDAISettings.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraScript.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "Misc/PackageName.h"
#include "RHI.h"

// Making a Niagara system from an emitter template needs the Niagara editor's own helpers; their headers are public in the
// engine versions this was written against. Without them, templates come only from system assets (shipped or engine).
#if __has_include("NiagaraSystemFactoryNew.h") && __has_include("NiagaraEditorUtilities.h")
#include "NiagaraEditorUtilities.h"
#include "NiagaraSystemFactoryNew.h"
#define NAMETBDAI_NIAGARA_EMITTER_TEMPLATES 1
#else
#define NAMETBDAI_NIAGARA_EMITTER_TEMPLATES 0
#endif

using namespace NameTBDAIJson;

namespace NameTBDAILibrary
{
	namespace
	{
		/** Where a release ships library assets already built. */
		const TCHAR* ShippedRoot = TEXT("/NameTBD/AI");

		struct FDefinition
		{
			FString Name;
			FString Kind; // master, function
			TSharedPtr<FJsonObject> Json;
		};

		/** Resources/AI/Materials/*.json, read afresh when the folder changes. */
		const TArray<FDefinition>& GetDefinitions()
		{
			static TArray<FDefinition> Definitions;
			static FDateTime Stamp;
			const FString Folder = NameTBDAIDocs::GetResourcesDir() / TEXT("Materials");
			TArray<FString> Files;
			IFileManager::Get().FindFiles(Files, *(Folder / TEXT("*.json")), true, false);
			FDateTime Newest;
			for (const FString& File : Files)
			{
				Newest = FMath::Max(Newest, IFileManager::Get().GetTimeStamp(*(Folder / File)));
			}
			if (Definitions.Num() > 0 && Newest == Stamp)
			{
				return Definitions;
			}
			Stamp = Newest;
			Definitions.Reset();
			Files.Sort();
			for (const FString& File : Files)
			{
				FString Text;
				if (!FFileHelper::LoadFileToString(Text, *(Folder / File)))
				{
					continue;
				}
				TSharedPtr<FJsonObject> Json = ParseObject(Text);
				if (!Json.IsValid())
				{
					UE_LOG(LogTemp, Warning, TEXT("NameTBD AI: %s isn't valid JSON."), *File);
					continue;
				}
				FDefinition Definition;
				Definition.Name = GetString(Json, TEXT("name"), FPaths::GetBaseFilename(File));
				Definition.Kind = GetString(Json, TEXT("kind"), TEXT("master"));
				Definition.Json = Json;
				Definitions.Add(MoveTemp(Definition));
			}
			return Definitions;
		}

		FString StripPrefix(const FString& Name)
		{
			FString Out = Name;
			for (const TCHAR* Prefix : { TEXT("M_UI_"), TEXT("MF_UI_"), TEXT("NS_UI_"), TEXT("M_"), TEXT("MF_"), TEXT("NS_") })
			{
				if (Out.StartsWith(Prefix, ESearchCase::IgnoreCase))
				{
					Out = Out.Mid(FCString::Strlen(Prefix));
					break;
				}
			}
			return Out;
		}

		const FDefinition* FindDefinition(const FString& Name, const FString& Kind)
		{
			const FString Key = Normalize(StripPrefix(Name));
			for (const FDefinition& Definition : GetDefinitions())
			{
				if (Definition.Kind == Kind && Normalize(Definition.Name) == Key)
				{
					return &Definition;
				}
			}
			return nullptr;
		}

		UObject* LoadIfExists(const FString& PackagePath)
		{
			const FString ObjectPath = PackagePath + TEXT(".") + FPaths::GetBaseFilename(PackagePath);
			if (!FPackageName::DoesPackageExist(PackagePath) && !FindObject<UObject>(nullptr, *ObjectPath))
			{
				return nullptr;
			}
			return StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath, nullptr, LOAD_Quiet | LOAD_NoWarn);
		}

		/** A library asset: the shipped one, else the project's library copy. */
		UObject* FindBuilt(const FString& SubFolder, const FString& AssetName)
		{
			if (UObject* Shipped = LoadIfExists(FString(ShippedRoot) / SubFolder / AssetName))
			{
				return Shipped;
			}
			return LoadIfExists(UNameTBDAISettings::Get().GetLibraryFolder() / SubFolder / AssetName);
		}

		UObject* CreateLibraryAsset(const FString& SubFolder, const FString& AssetName, UClass* Class, UFactory* Factory)
		{
			IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
			return AssetTools.CreateAsset(AssetName, UNameTBDAISettings::Get().GetLibraryFolder() / SubFolder, Class, Factory);
		}

		void Save(UObject* Asset)
		{
			if (Asset)
			{
				Asset->MarkPackageDirty();
				UEditorLoadingAndSavingUtils::SavePackages({ Asset->GetOutermost() }, true);
			}
		}

		// ---- Graph building ------------------------------------------------------------------------------------------

		/** Creates nodes in a material or a function. */
		struct FGraphTarget
		{
			UMaterial* Material = nullptr;
			UMaterialFunction* Function = nullptr;

			UMaterialExpression* Create(UClass* Class, int32 X, int32 Y) const
			{
				return Material ? UMaterialEditingLibrary::CreateMaterialExpression(Material, Class, X, Y)
					: UMaterialEditingLibrary::CreateMaterialExpressionInFunction(Function, Class, X, Y);
			}
		};

		FString ListInputs(UMaterialExpression* Expression)
		{
			TArray<FString> Names;
			for (int32 Index = 0; Index < 64; ++Index)
			{
				if (!Expression->GetInput(Index))
				{
					break;
				}
				const FString Name = Expression->GetInputName(Index).ToString();
				Names.Add(Name.IsEmpty() ? FString(TEXT("(unnamed)")) : Name);
			}
			return FString::Join(Names, TEXT(", "));
		}

		ECustomMaterialOutputType CustomOutputType(int32 Width)
		{
			switch (Width)
			{
			case 1: return CMOT_Float1;
			case 2: return CMOT_Float2;
			case 4: return CMOT_Float4;
			default: return CMOT_Float3;
			}
		}

		/**
		 * Every live node of a prepared graph made in Target, its settings written, then wired by the engine's own pin and
		 * output indices. OutExpressions is parallel to Graph.Nodes (null where a node wasn't made).
		 */
		void BuildNodes(const FGraphTarget& Target, const NameTBDAIMaterialGraph::FGraph& Graph, TArray<UMaterialExpression*>& OutExpressions, TArray<FString>& OutProblems)
		{
			OutExpressions.Init(nullptr, Graph.Nodes.Num());
			int32 Column = 0;
			for (int32 Index = 0; Index < Graph.Nodes.Num(); ++Index)
			{
				const NameTBDAIMaterialGraph::FNode& Node = Graph.Nodes[Index];
				if (!Node.bLive || !Node.Class)
				{
					continue;
				}
				UMaterialExpression* Expression = Target.Create(Node.Class, -300 * (1 + Column / 6), 160 * (Column % 6));
				++Column;
				if (!Expression)
				{
					OutProblems.Add(FString::Printf(TEXT("%s: couldn't make the %s node."), *NameTBDAIMaterialGraph::DisplayId(Graph, Index), *Node.Type));
					continue;
				}

				if (UMaterialExpressionMaterialFunctionCall* Call = Cast<UMaterialExpressionMaterialFunctionCall>(Expression))
				{
					FString Error;
					UMaterialFunctionInterface* Function = Node.Function.StartsWith(TEXT("/"))
						? Cast<UMaterialFunctionInterface>(NameTBDAIHelpers::FindAsset(Node.Function, UMaterialFunctionInterface::StaticClass(), Error))
						: GetFunction(Node.Function, Error);
					if (!Function || !Call->SetMaterialFunction(Function))
					{
						OutProblems.Add(FString::Printf(TEXT("%s: %s"), *NameTBDAIMaterialGraph::DisplayId(Graph, Index), Error.IsEmpty() ? TEXT("couldn't use the function.") : *Error));
					}
				}
				if (UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expression))
				{
					Custom->Code = Node.Code;
					Custom->OutputType = CustomOutputType(Node.CustomWidth);
					Custom->Inputs.Reset();
					for (const FString& Name : Node.CustomInputs)
					{
						FCustomInput Input;
						Input.InputName = FName(*Name);
						Custom->Inputs.Add(Input);
					}
					for (const FString& Include : Node.Includes)
					{
						Custom->IncludeFilePaths.AddUnique(Include);
					}
					if (UNameTBDAISettings::Get().bShowHlslInGraph)
					{
						// The code in the node's comment bubble, readable in the graph. Display only: Code is what compiles.
						Custom->Desc = Node.Code;
						Custom->bCommentBubbleVisible = true;
					}
				}
				if (Node.Props.IsValid() && Node.Props->Values.Num() > 0)
				{
					TArray<FString> Errors;
					WriteObject(Expression->GetClass(), Expression, Node.Props, Expression, Errors);
					for (const FString& Error : Errors)
					{
						OutProblems.Add(FString::Printf(TEXT("%s: %s"), *NameTBDAIMaterialGraph::DisplayId(Graph, Index), *Error));
					}
				}
				OutExpressions[Index] = Expression;
			}

			for (int32 Index = 0; Index < Graph.Nodes.Num(); ++Index)
			{
				UMaterialExpression* To = OutExpressions[Index];
				if (!To)
				{
					continue;
				}
				for (const NameTBDAIMaterialGraph::FWire& Wire : Graph.Nodes[Index].Wires)
				{
					UMaterialExpression* From = OutExpressions.IsValidIndex(Wire.From) ? OutExpressions[Wire.From] : nullptr;
					if (!From)
					{
						continue;
					}
					const int32 OutputIndex = NameTBDAIMaterialGraph::FindOutputIndex(From, Wire.FromOutput);
					const int32 InputIndex = NameTBDAIMaterialGraph::FindInputIndex(To, Wire.PinNames);
					FExpressionInput* Input = InputIndex != INDEX_NONE ? To->GetInput(InputIndex) : nullptr;
					if (OutputIndex == INDEX_NONE || !Input)
					{
						OutProblems.Add(FString::Printf(TEXT("%s: couldn't connect %s%s to its %s (its inputs: %s)."), *NameTBDAIMaterialGraph::DisplayId(Graph, Index), *Graph.Nodes[Wire.From].Id,
							Wire.FromOutput.IsEmpty() ? TEXT("") : *(TEXT(".") + Wire.FromOutput), *Wire.Pin, *ListInputs(To)));
						continue;
					}
					Input->Connect(OutputIndex, From);
				}
			}
		}

		bool ParseMaterialProperty(const FString& Name, EMaterialProperty& OutProperty)
		{
			const FString Key = Normalize(Name);
			if (Key == TEXT("finalcolor") || Key == TEXT("emissive") || Key == TEXT("emissivecolor") || Key == TEXT("color")) { OutProperty = MP_EmissiveColor; return true; }
			if (Key == TEXT("opacity")) { OutProperty = MP_Opacity; return true; }
			if (Key == TEXT("opacitymask")) { OutProperty = MP_OpacityMask; return true; }
			if (Key == TEXT("basecolor")) { OutProperty = MP_BaseColor; return true; }
			if (Key == TEXT("metallic")) { OutProperty = MP_Metallic; return true; }
			if (Key == TEXT("roughness")) { OutProperty = MP_Roughness; return true; }
			if (Key == TEXT("specular")) { OutProperty = MP_Specular; return true; }
			if (Key == TEXT("normal")) { OutProperty = MP_Normal; return true; }
			if (Key == TEXT("worldpositionoffset")) { OutProperty = MP_WorldPositionOffset; return true; }
			return false;
		}

		void ApplyDomainAndBlend(UMaterial* Material, const NameTBDAIMaterialGraph::FGraph& Graph)
		{
			const FString Domain = Normalize(Graph.Domain);
			Material->MaterialDomain = Domain == TEXT("surface") ? MD_Surface : Domain == TEXT("postprocess") ? MD_PostProcess : MD_UI;
			const FString Blend = Normalize(Graph.Blend);
			Material->BlendMode = Blend == TEXT("additive") ? BLEND_Additive : Blend == TEXT("opaque") ? BLEND_Opaque
				: Blend == TEXT("masked") ? BLEND_Masked : Blend == TEXT("alphacomposite") ? BLEND_AlphaComposite : BLEND_Translucent;

			// Mobile runs a material's maths at half precision by default: time-driven waves step and noise bands after a few
			// minutes. Full precision for the material's own expressions only - the rest of the shader keeps its default.
			// Found by name, as the setting's form has changed between engine versions; a choice made by hand is kept.
			const FProperty* Precision = FindFProperty<FProperty>(UMaterial::StaticClass(), TEXT("FloatPrecisionMode"));
			const FByteProperty* AsByte = CastField<FByteProperty>(Precision);
			const FEnumProperty* AsEnum = CastField<FEnumProperty>(Precision);
			const UEnum* PrecisionEnum = AsByte ? AsByte->GetIntPropertyEnum() : AsEnum ? AsEnum->GetEnum() : nullptr;
			const FNumericProperty* Underlying = AsByte ? static_cast<const FNumericProperty*>(AsByte) : AsEnum ? AsEnum->GetUnderlyingProperty() : nullptr;
			if (PrecisionEnum && Underlying)
			{
				void* Value = Precision->ContainerPtrToValuePtr<void>(Material);
				const int64 Full = PrecisionEnum->GetValueByNameString(TEXT("MFPM_Full_MaterialExpressionOnly"));
				if (Full != INDEX_NONE && Underlying->GetSignedIntPropertyValue(Value) == PrecisionEnum->GetValueByNameString(TEXT("MFPM_Default")))
				{
					Underlying->SetIntPropertyValue(Value, Full);
				}
			}
		}

		/** "(Node Multiply) Arithmetic between..." -> the class short name in the parentheses ("Multiply"), or empty. */
		FString NodeNameInError(const FString& Error)
		{
			const int32 Start = Error.Find(TEXT("(Node "));
			if (Start == INDEX_NONE)
			{
				return FString();
			}
			const int32 End = Error.Find(TEXT(")"), ESearchCase::IgnoreCase, ESearchDir::FromStart, Start);
			return End == INDEX_NONE ? FString() : Error.Mid(Start + 6, End - Start - 6).TrimStartAndEnd();
		}

		// ---- Which definition each library asset was built from ------------------------------------------------------
		// A project's library copies are rebuilt in place when their definition changed (a fixed graph in a plugin update),
		// so a library built by an older version doesn't keep a broken material.

		FString StampFile()
		{
			return FPaths::ProjectSavedDir() / TEXT("NameTBDAI") / TEXT("library_versions.json");
		}

		/** The definition's fingerprint; a master's includes the functions' (it calls them). */
		/** Bumped when what a definition builds into changes (fix5: the NTB_ helpers, full precision, titled Custom nodes). */
		const TCHAR* BuilderVersion = TEXT("builder 2");

		FString Fingerprint(const FDefinition& Definition)
		{
			// The same definition builds differently with or without the helpers (NameTBDShaders), so that counts too.
			FString Text = ToString(Definition.Json) + BuilderVersion + (NameTBDAIMaterialGraph::HelpersAvailable() ? TEXT(" helpers") : TEXT(""));
			if (Definition.Kind == TEXT("master"))
			{
				for (const FDefinition& Each : GetDefinitions())
				{
					if (Each.Kind == TEXT("function"))
					{
						Text += ToString(Each.Json);
					}
				}
			}
			return FMD5::HashAnsiString(*Text);
		}

		bool IsCurrent(const FString& AssetName, const FDefinition& Definition)
		{
			FString Text;
			FFileHelper::LoadFileToString(Text, *StampFile());
			return GetString(ParseObject(Text), AssetName) == Fingerprint(Definition);
		}

		void Stamp(const FString& AssetName, const FDefinition& Definition)
		{
			FString Text;
			FFileHelper::LoadFileToString(Text, *StampFile());
			TSharedPtr<FJsonObject> Stamps = ParseObject(Text);
			if (!Stamps.IsValid())
			{
				Stamps = MakeShared<FJsonObject>();
			}
			Stamps->SetStringField(AssetName, Fingerprint(Definition));
			FFileHelper::SaveStringToFile(ToString(Stamps, true), *StampFile());
		}

		/** While BuildAll runs: where each built / rebuilt / failed asset is reported. */
		TArray<FString>* BuildReport = nullptr;

		void Report(const FString& Line, bool bProblem)
		{
			if (bProblem)
			{
				UE_LOG(LogTemp, Warning, TEXT("NameTBD AI: %s"), *Line);
			}
			else
			{
				UE_LOG(LogTemp, Display, TEXT("NameTBD AI: %s"), *Line);
			}
			if (BuildReport)
			{
				BuildReport->Add(Line);
			}
		}

		/** An editor open on a library asset works on its own copy, and would write it back over the rebuilt one: closed first. */
		void CloseEditorsFor(UObject* Asset)
		{
			if (GEditor && Asset)
			{
				if (UAssetEditorSubsystem* Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
				{
					Editors->CloseAllEditorsForAsset(Asset);
				}
			}
		}

		bool IsShipped(const UObject* Asset)
		{
			return Asset && Asset->GetPathName().StartsWith(FString(ShippedRoot) + TEXT("/"));
		}

		void BuildMasterInto(UMaterial* Material, const FDefinition& Definition)
		{
			// Library assets are shared and saved at once: never part of a request's undo step (a Discard must not empty them).
			TGuardValue<ITransaction*> NoUndo(GUndo, nullptr);
			CloseEditorsFor(Material);
			TArray<FString> Problems;
			if (BuildMaterialGraph(Material, NameTBDAIMaterialGraph::DefinitionGraph(Definition.Json, false), Problems))
			{
				Report(FString::Printf(TEXT("built %s"), *Material->GetName()), false);
			}
			else
			{
				Report(FString::Printf(TEXT("%s didn't compile: %s"), *Material->GetName(), *FString::Join(Problems, TEXT(" | "))), true);
			}
			Save(Material);
			Stamp(Material->GetName(), Definition);
		}

		void BuildFunctionInto(UMaterialFunction* Function, const FDefinition& Definition)
		{
			TGuardValue<ITransaction*> NoUndo(GUndo, nullptr);
			CloseEditorsFor(Function);
			Function->Description = GetString(Definition.Json, TEXT("about"));
			TArray<FString> Problems;
			if (BuildFunctionGraph(Function, NameTBDAIMaterialGraph::DefinitionGraph(Definition.Json, true), Problems))
			{
				Report(FString::Printf(TEXT("built %s"), *Function->GetName()), false);
			}
			else
			{
				Report(FString::Printf(TEXT("%s didn't build: %s"), *Function->GetName(), *FString::Join(Problems, TEXT(" | "))), true);
			}
			Save(Function);
			Stamp(Function->GetName(), Definition);
		}

		UMaterial* BuildMasterAsset(const FDefinition& Definition, FString& OutError)
		{
			TGuardValue<ITransaction*> NoUndo(GUndo, nullptr);
			const FString AssetName = TEXT("M_UI_") + Definition.Name;
			UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
			UMaterial* Material = Cast<UMaterial>(CreateLibraryAsset(TEXT("Materials"), AssetName, UMaterial::StaticClass(), Factory));
			if (!Material)
			{
				OutError = FString::Printf(TEXT("Couldn't make %s in the library folder."), *AssetName);
				return nullptr;
			}
			BuildMasterInto(Material, Definition);
			return Material;
		}

		UMaterialFunction* BuildFunctionAsset(const FDefinition& Definition, FString& OutError)
		{
			TGuardValue<ITransaction*> NoUndo(GUndo, nullptr);
			const FString AssetName = TEXT("MF_UI_") + Definition.Name;
			UMaterialFunctionFactoryNew* Factory = NewObject<UMaterialFunctionFactoryNew>();
			UMaterialFunction* Function = Cast<UMaterialFunction>(CreateLibraryAsset(TEXT("Materials"), AssetName, UMaterialFunction::StaticClass(), Factory));
			if (!Function)
			{
				OutError = FString::Printf(TEXT("Couldn't make %s in the library folder."), *AssetName);
				return nullptr;
			}
			BuildFunctionInto(Function, Definition);
			return Function;
		}

		TSharedPtr<FJsonObject> DescribeDefinitions(const FString& Kind)
		{
			TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
			for (const FDefinition& Definition : GetDefinitions())
			{
				if (Definition.Kind != Kind)
				{
					continue;
				}
				FString Line = GetString(Definition.Json, TEXT("about"));
				if (TSharedPtr<FJsonObject> Params = GetObject(Definition.Json, TEXT("params")))
				{
					TArray<FString> Names;
					for (const auto& Param : Params->Values)
					{
						Names.Add(FString::Printf(TEXT("%s (%s)"), *Param.Key, *ToString(Param.Value).Replace(TEXT("\""), TEXT(""))));
					}
					Line += TEXT(" Params: ") + FString::Join(Names, TEXT("; "));
				}
				if (TSharedPtr<FJsonObject> Inputs = GetObject(Definition.Json, TEXT("signature")))
				{
					Line += TEXT(" ") + ToString(Inputs);
				}
				Out->SetStringField(Definition.Name, Line);
			}
			return Out;
		}

		// ---- Niagara -------------------------------------------------------------------------------------------------

		TSharedPtr<FJsonObject> LoadNiagaraRecipes()
		{
			const TSharedPtr<FJsonObject> Json = ParseObject(NameTBDAIDocs::LoadResourceText(TEXT("Niagara/Templates.json")));
			return Json.IsValid() ? Json : MakeShared<FJsonObject>();
		}

		void GatherScripts(UNiagaraSystem* System, TArray<UNiagaraScript*>& OutScripts)
		{
			if (UNiagaraScript* Spawn = System->GetSystemSpawnScript())
			{
				OutScripts.Add(Spawn);
			}
			if (UNiagaraScript* Update = System->GetSystemUpdateScript())
			{
				OutScripts.Add(Update);
			}
			for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
			{
				if (FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData())
				{
					Data->GetScripts(OutScripts, /*bCompilableOnly=*/false);
				}
			}
		}

		bool SetConstant(FNiagaraParameterStore& Store, const FNiagaraVariable& Variable, const TSharedPtr<FJsonValue>& Value)
		{
			const FNiagaraTypeDefinition& Type = Variable.GetType();
			double Number = 0.0;
			if (Type == FNiagaraTypeDefinition::GetFloatDef() && Value->TryGetNumber(Number))
			{
				return Store.SetParameterValue<float>(static_cast<float>(Number), Variable);
			}
			if (Type == FNiagaraTypeDefinition::GetIntDef() && Value->TryGetNumber(Number))
			{
				return Store.SetParameterValue<int32>(static_cast<int32>(Number), Variable);
			}
			if (Type == FNiagaraTypeDefinition::GetBoolDef())
			{
				bool bValue = false;
				if (Value->TryGetBool(bValue))
				{
					FNiagaraBool Bool;
					Bool.SetValue(bValue);
					return Store.SetParameterValue<FNiagaraBool>(Bool, Variable);
				}
				return false;
			}
			if (Type == FNiagaraTypeDefinition::GetColorDef())
			{
				FLinearColor Color;
				return ParseColor(Value, Color) && Store.SetParameterValue<FLinearColor>(Color, Variable);
			}
			if (Value->Type == EJson::Array)
			{
				const TArray<TSharedPtr<FJsonValue>>& Parts = Value->AsArray();
				auto At = [&Parts](int32 Index) { return Parts.IsValidIndex(Index) ? static_cast<float>(Parts[Index]->AsNumber()) : 0.f; };
				if (Type == FNiagaraTypeDefinition::GetVec2Def())
				{
					return Store.SetParameterValue<FVector2f>(FVector2f(At(0), At(1)), Variable);
				}
				if (Type == FNiagaraTypeDefinition::GetVec3Def() || Type == FNiagaraTypeDefinition::GetPositionDef())
				{
					return Store.SetParameterValue<FVector3f>(FVector3f(At(0), At(1), At(2)), Variable);
				}
				if (Type == FNiagaraTypeDefinition::GetVec4Def())
				{
					return Store.SetParameterValue<FVector4f>(FVector4f(At(0), At(1), At(2), At(3)), Variable);
				}
			}
			return false;
		}
	}

	// =================================================================================================================

	TSharedPtr<FJsonObject> DescribeMasters()
	{
		TSharedPtr<FJsonObject> Out = DescribeDefinitions(TEXT("master"));
		Out->SetStringField(TEXT("$use"), TEXT("create_material_instance(master, params) - an instance is instant and can't break; then set_shape_paint(material=...) or a brush."));
		return Out;
	}

	TSharedPtr<FJsonObject> DescribeFunctions()
	{
		TSharedPtr<FJsonObject> Out = DescribeDefinitions(TEXT("function"));
		Out->SetStringField(TEXT("$use"), TEXT("In build_ui_material: {\"id\": \"c\", \"type\": \"MaterialFunctionCall\", \"function\": \"SoftCircle\", \"inputs\": {...}}; outputs by name: \"c.Result\". The macros (list_options material_nodes) are usually shorter."));
		return Out;
	}

	TSharedPtr<FJsonObject> FindDefinitionJson(const FString& Name, const FString& Kind)
	{
		const FDefinition* Definition = FindDefinition(Name, Kind);
		return Definition ? Definition->Json : nullptr;
	}

	TSharedPtr<FJsonObject> GetMasterGraph(const FString& Name)
	{
		const TSharedPtr<FJsonObject> Definition = FindDefinitionJson(Name, TEXT("master"));
		return Definition.IsValid() ? NameTBDAIMaterialGraph::DefinitionGraph(Definition, false) : nullptr;
	}

	TSharedPtr<FJsonObject> DescribeMaster(const FString& Name)
	{
		const FDefinition* Definition = FindDefinition(Name, TEXT("master"));
		if (!Definition)
		{
			return nullptr;
		}
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("name"), Definition->Name);
		Out->SetStringField(TEXT("about"), GetString(Definition->Json, TEXT("about")));
		if (TSharedPtr<FJsonObject> Params = GetObject(Definition->Json, TEXT("params")))
		{
			Out->SetObjectField(TEXT("params"), Params);
		}
		if (const TSharedPtr<FJsonObject> Hlsl = GetObject(Definition->Json, TEXT("hlsl")))
		{
			// Written in HLSL: the code and its params are what to read (and to start a variation from).
			Out->SetObjectField(TEXT("hlsl"), Hlsl);
			Out->SetStringField(TEXT("$use"), TEXT("Tune it: create_material_instance. A variation: build_ui_material {name, hlsl: {code, params}} with this code changed. Add nodes on top: build_ui_material {from_master, graph: {nodes: [...], outputs: {FinalColor: ...}}} - \"master.FinalColor\" / \"master.Opacity\" are what it outputs."));
		}
		else
		{
			Out->SetObjectField(TEXT("graph"), GetObject(Definition->Json, TEXT("graph")));
			Out->SetStringField(TEXT("$use"), TEXT("Tune it: create_material_instance. Extend it: build_ui_material {from_master, graph: {nodes: [your nodes - ids not in this graph], outputs: {FinalColor: ...}}} - \"master.FinalColor\" / \"master.Opacity\" are what the master outputs."));
		}
		return Out;
	}

	TSharedPtr<FJsonObject> DescribeNiagaraTemplates()
	{
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		for (const auto& Pair : LoadNiagaraRecipes()->Values)
		{
			if (Pair.Value->Type != EJson::Object)
			{
				continue;
			}
			const TSharedPtr<FJsonObject> Recipe = Pair.Value->AsObject();
			FString Line = GetString(Recipe, TEXT("about"));
			if (TSharedPtr<FJsonObject> Params = GetObject(Recipe, TEXT("params")))
			{
				TArray<FString> Names;
				for (const auto& Param : Params->Values)
				{
					const TSharedPtr<FJsonObject> Info = Param.Value->Type == EJson::Object ? Param.Value->AsObject() : nullptr;
					Names.Add(FString::Printf(TEXT("%s=%s"), *Param.Key, Info.IsValid() ? *ToString(Info->TryGetField(TEXT("default"))) : TEXT("?")));
				}
				Line += TEXT(" Params: ") + FString::Join(Names, TEXT(", "));
			}
			Out->SetStringField(Pair.Key, Line);
		}
		Out->SetStringField(TEXT("$use"), TEXT("create_niagara_ui_system(template, params) then add a NiagaraUIWidget (role particles) with NiagaraSystem set to it."));
		return Out;
	}

	UMaterialInterface* GetMaster(const FString& Name, FString& OutError)
	{
		if (Name.StartsWith(TEXT("/")))
		{
			return Cast<UMaterialInterface>(NameTBDAIHelpers::FindAsset(Name, UMaterialInterface::StaticClass(), OutError));
		}
		const FDefinition* Definition = FindDefinition(Name, TEXT("master"));
		if (!Definition)
		{
			// Not a library master: any material of the project by name.
			return Cast<UMaterialInterface>(NameTBDAIHelpers::FindAsset(Name, UMaterialInterface::StaticClass(), OutError));
		}
		const FString AssetName = TEXT("M_UI_") + Definition->Name;
		if (UMaterialInterface* Built = Cast<UMaterialInterface>(FindBuilt(TEXT("Materials"), AssetName)))
		{
			UMaterial* Material = Cast<UMaterial>(Built);
			if (Material && !IsShipped(Material) && !IsCurrent(AssetName, *Definition))
			{
				UE_LOG(LogTemp, Log, TEXT("NameTBD AI: %s was built from an older definition - rebuilding it."), *AssetName);
				BuildMasterInto(Material, *Definition);
			}
			return Built;
		}
		return BuildMasterAsset(*Definition, OutError);
	}

	UMaterialFunction* GetFunction(const FString& Name, FString& OutError)
	{
		const FDefinition* Definition = FindDefinition(Name, TEXT("function"));
		if (!Definition)
		{
			if (UMaterialFunction* Project = Cast<UMaterialFunction>(NameTBDAIHelpers::FindAsset(Name, UMaterialFunction::StaticClass(), OutError)))
			{
				return Project;
			}
			OutError = FString::Printf(TEXT("No material function '%s' (list_options material_functions)."), *Name);
			return nullptr;
		}
		const FString AssetName = TEXT("MF_UI_") + Definition->Name;
		if (UMaterialFunction* Built = Cast<UMaterialFunction>(FindBuilt(TEXT("Materials"), AssetName)))
		{
			if (!IsShipped(Built) && !IsCurrent(AssetName, *Definition))
			{
				UE_LOG(LogTemp, Log, TEXT("NameTBD AI: %s was built from an older definition - rebuilding it."), *AssetName);
				BuildFunctionInto(Built, *Definition);
			}
			return Built;
		}
		return BuildFunctionAsset(*Definition, OutError);
	}

	UMaterialInterface* GetSoftDotMaterial(FString& OutError)
	{
		return GetMaster(TEXT("SoftDot"), OutError);
	}

	bool BuildMaterialGraph(UMaterial* Material, const TSharedPtr<FJsonObject>& Graph, TArray<FString>& OutProblems)
	{
		const NameTBDAIMaterialGraph::FGraph Prepared = NameTBDAIMaterialGraph::Prepare(Graph, false);
		OutProblems.Append(Prepared.ErrorLines());
		if (Prepared.HasErrors())
		{
			return false;
		}
		int32 CompileErrors = 0;
		return BuildPreparedMaterial(Material, Prepared, OutProblems, CompileErrors);
	}

	bool BuildPreparedMaterial(UMaterial* Material, const NameTBDAIMaterialGraph::FGraph& Graph, TArray<FString>& OutProblems, int32& OutCompileErrors)
	{
		OutCompileErrors = 0;
		if (!Material)
		{
			OutProblems.Add(TEXT("No material."));
			return false;
		}
		Material->Modify();
		UMaterialEditingLibrary::DeleteAllMaterialExpressions(Material);
		ApplyDomainAndBlend(Material, Graph);

		FGraphTarget Target;
		Target.Material = Material;
		TArray<UMaterialExpression*> Expressions;
		const int32 ProblemsBefore = OutProblems.Num();
		BuildNodes(Target, Graph, Expressions, OutProblems);

		bool bAnyOutput = false;
		for (const NameTBDAIMaterialGraph::FOutput& Output : Graph.Outputs)
		{
			EMaterialProperty Property;
			UMaterialExpression* From = Expressions.IsValidIndex(Output.From) ? Expressions[Output.From] : nullptr;
			if (!ParseMaterialProperty(Output.Name, Property) || !From)
			{
				continue;
			}
			const int32 OutputIndex = NameTBDAIMaterialGraph::FindOutputIndex(From, Output.FromOutput);
			FExpressionInput* Input = Material->GetExpressionInputForProperty(Property);
			if (Input && OutputIndex != INDEX_NONE)
			{
				Input->Connect(OutputIndex, From);
				bAnyOutput = true;
			}
			else
			{
				OutProblems.Add(FString::Printf(TEXT("outputs: couldn't connect %s to %s."), *Graph.Nodes[Output.From].Id, *Output.Name));
			}
		}
		if (!bAnyOutput)
		{
			OutProblems.Add(TEXT("Nothing reaches the material's outputs: give outputs {\"FinalColor\": ..., \"Opacity\": ...}."));
		}

		UMaterialEditingLibrary::LayoutMaterialExpressions(Material);
		UMaterialEditingLibrary::RecompileMaterial(Material);

		// The compile's own errors, tied back to the graph's nodes where the compiler says which expression failed.
		FMaterialResource* Resource = Material->GetMaterialResource(GMaxRHIShaderPlatform);
		if (!Resource)
		{
			return bAnyOutput && OutProblems.Num() == ProblemsBefore;
		}
		Resource->FinishCompilation();
		const TArray<FString> Errors = Resource->GetCompileErrors();
		TArray<UMaterialExpression*> ErrorExpressions;
		for (UMaterialExpression* Expression : Resource->GetErrorExpressions())
		{
			ErrorExpressions.Add(Expression);
		}
		auto NodeOf = [&Expressions](const UMaterialExpression* Expression) { return Expressions.IndexOfByKey(Expression); };
		TSet<int32> Used;
		int32 CustomCount = 0;
		int32 LastCustom = INDEX_NONE;
		for (int32 Index = 0; Index < Graph.Nodes.Num(); ++Index)
		{
			if (Graph.Nodes[Index].bLive && Graph.Nodes[Index].Type == TEXT("Custom"))
			{
				++CustomCount;
				LastCustom = Index;
			}
		}
		for (int32 ErrorIndex = 0; ErrorIndex < Errors.Num(); ++ErrorIndex)
		{
			const FString& Error = Errors[ErrorIndex];
			int32 Node = INDEX_NONE;
			// The expression the compiler blamed: by the "(Node X)" in the text, else by position when the lists line up.
			const FString Named = NodeNameInError(Error);
			for (int32 Each = 0; Node == INDEX_NONE && Each < ErrorExpressions.Num(); ++Each)
			{
				const UMaterialExpression* Expression = ErrorExpressions[Each];
				if (!Used.Contains(Each) && Expression && !Named.IsEmpty()
					&& (Expression->GetClass()->GetName().Contains(Named.Replace(TEXT(" "), TEXT(""))) || Named.Contains(Expression->GetClass()->GetName().Replace(TEXT("MaterialExpression"), TEXT("")))))
				{
					Used.Add(Each);
					Node = NodeOf(Expression);
				}
			}
			if (Node == INDEX_NONE && ErrorExpressions.Num() == Errors.Num() && ErrorExpressions[ErrorIndex])
			{
				Node = NodeOf(ErrorExpressions[ErrorIndex]);
			}
			if (Node == INDEX_NONE && CustomCount == 1 && (Error.Contains(TEXT("error X")) || Error.Contains(TEXT("Custom")) || Error.Contains(TEXT("/Engine/Generated"))))
			{
				Node = LastCustom;
			}
			const FString Hint = NameTBDAIMaterialGraph::HintFor(Error);
			OutProblems.Add(FString::Printf(TEXT("compile: %s%s%s"), Node != INDEX_NONE ? *(NameTBDAIMaterialGraph::DisplayId(Graph, Node) + TEXT(": ")) : TEXT(""), *Error.Left(300),
				Hint.IsEmpty() ? TEXT("") : *(TEXT(" - ") + Hint)));
		}
		OutCompileErrors = Errors.Num();
		return bAnyOutput && Errors.Num() == 0 && OutProblems.Num() == ProblemsBefore;
	}

	bool BuildFunctionGraph(UMaterialFunction* Function, const TSharedPtr<FJsonObject>& Graph, TArray<FString>& OutProblems)
	{
		if (!Function)
		{
			OutProblems.Add(TEXT("No function."));
			return false;
		}
		const NameTBDAIMaterialGraph::FGraph Prepared = NameTBDAIMaterialGraph::Prepare(Graph, true);
		OutProblems.Append(Prepared.ErrorLines());
		if (Prepared.HasErrors())
		{
			return false;
		}
		Function->Modify();
		UMaterialEditingLibrary::DeleteAllMaterialExpressionsInFunction(Function);

		FGraphTarget Target;
		Target.Function = Function;
		TArray<UMaterialExpression*> Expressions;
		BuildNodes(Target, Prepared, Expressions, OutProblems);

		int32 Sort = 0;
		for (const NameTBDAIMaterialGraph::FOutput& Output : Prepared.Outputs)
		{
			UMaterialExpressionFunctionOutput* Node = Cast<UMaterialExpressionFunctionOutput>(Target.Create(UMaterialExpressionFunctionOutput::StaticClass(), 400, 200 * Sort));
			UMaterialExpression* From = Expressions.IsValidIndex(Output.From) ? Expressions[Output.From] : nullptr;
			if (!Node || !From)
			{
				continue;
			}
			Node->OutputName = FName(*Output.Name);
			Node->SortPriority = Sort++;
			const int32 OutputIndex = NameTBDAIMaterialGraph::FindOutputIndex(From, Output.FromOutput);
			if (FExpressionInput* Input = Node->GetInput(0); Input && OutputIndex != INDEX_NONE)
			{
				Input->Connect(OutputIndex, From);
			}
		}
		UMaterialEditingLibrary::LayoutMaterialFunctionExpressions(Function);
		UMaterialEditingLibrary::UpdateMaterialFunction(Function, nullptr);
		return OutProblems.Num() == 0;
	}

	TSharedPtr<FJsonObject> FindNiagaraRecipe(const FString& Name)
	{
		const FString Key = Normalize(StripPrefix(Name));
		for (const auto& Pair : LoadNiagaraRecipes()->Values)
		{
			if (Normalize(Pair.Key) == Key && Pair.Value->Type == EJson::Object)
			{
				return Pair.Value->AsObject();
			}
		}
		return nullptr;
	}

	/**
	 * Writes module constants into the system's scripts: {"SpawnRate.SpawnRate": 12, "GravityForce.Gravity": [0,0,40]} - each
	 * key matched (loosely) against the end of the constants' names ("Constants.<Emitter>.<Module>.<Input>"). Notes say
	 * which took. True if any did.
	 */
	bool ApplyNiagaraConstants(UNiagaraSystem* System, const TSharedPtr<FJsonObject>& Constants, TArray<FString>& OutNotes)
	{
		if (!System || !Constants.IsValid() || Constants->Values.Num() == 0)
		{
			return false;
		}
		TArray<UNiagaraScript*> Scripts;
		GatherScripts(System, Scripts);
		bool bAny = false;
		for (const auto& Pair : Constants->Values)
		{
			const FString Key = Normalize(Pair.Key);
			int32 Set = 0;
			for (UNiagaraScript* Script : Scripts)
			{
				TArray<FNiagaraVariable> Variables;
				Script->RapidIterationParameters.GetParameters(Variables);
				for (const FNiagaraVariable& Variable : Variables)
				{
					if (!Normalize(Variable.GetName().ToString()).EndsWith(Key))
					{
						continue;
					}
					Script->Modify();
					if (SetConstant(Script->RapidIterationParameters, Variable, Pair.Value))
					{
						++Set;
					}
				}
			}
			OutNotes.Add(Set > 0 ? FString::Printf(TEXT("%s set"), *Pair.Key) : FString::Printf(TEXT("%s: no such module input in this system (left as it was)"), *Pair.Key));
			bAny |= Set > 0;
		}
		return bAny;
	}

	/** CPU simulation, local space, and a UI material on every sprite renderer whose material isn't one - what UI drawing needs. */
	void MakeUIReady(UNiagaraSystem* System, UMaterialInterface* Material, TArray<FString>& OutNotes)
	{
		for (FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
		{
			FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
			UNiagaraEmitter* Emitter = Handle.GetInstance().Emitter;
			if (!Data || !Emitter)
			{
				continue;
			}
			Emitter->Modify();
			if (Data->SimTarget != ENiagaraSimTarget::CPUSim)
			{
				Data->SimTarget = ENiagaraSimTarget::CPUSim;
				OutNotes.Add(FString::Printf(TEXT("%s: set to CPU simulation (UI draws CPU emitters)"), *Handle.GetName().ToString()));
			}
			Data->bLocalSpace = true;
			for (UNiagaraRendererProperties* Renderer : Data->GetRenderers())
			{
				UNiagaraSpriteRendererProperties* Sprite = Cast<UNiagaraSpriteRendererProperties>(Renderer);
				if (!Sprite || !Material)
				{
					continue;
				}
				const UMaterial* Base = Sprite->Material ? Sprite->Material->GetMaterial() : nullptr;
				if (!Base || Base->MaterialDomain != MD_UI)
				{
					Sprite->Modify();
					Sprite->Material = Material;
				}
			}
		}
	}

	UNiagaraSystem* GetNiagaraTemplate(const FString& Name, FString& OutError, TArray<FString>& OutNotes)
	{
		const TSharedPtr<FJsonObject> Recipe = FindNiagaraRecipe(Name);
		if (!Recipe.IsValid())
		{
			TArray<FString> Names;
			LoadNiagaraRecipes()->Values.GetKeys(Names);
			OutError = FString::Printf(TEXT("No Niagara UI template '%s'. Templates: %s."), *Name, *FString::Join(Names, TEXT(", ")));
			return nullptr;
		}
		const FString AssetName = TEXT("NS_UI_") + StripPrefix(Name).Replace(TEXT(" "), TEXT(""));
		if (UNiagaraSystem* Built = Cast<UNiagaraSystem>(FindBuilt(TEXT("Niagara"), AssetName)))
		{
			return Built;
		}

		TGuardValue<ITransaction*> NoUndo(GUndo, nullptr);
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		const FString LibraryNiagara = UNameTBDAISettings::Get().GetLibraryFolder() / TEXT("Niagara");
		UNiagaraSystem* System = nullptr;

		// 1. A system asset to start from: shipped, the plugin's own, or the engine's.
		for (const FString& Base : GetStringArray(Recipe, TEXT("base_systems")))
		{
			FString Ignored;
			if (UNiagaraSystem* Source = Cast<UNiagaraSystem>(NameTBDAIHelpers::FindAsset(Base, UNiagaraSystem::StaticClass(), Ignored)))
			{
				System = Cast<UNiagaraSystem>(AssetTools.DuplicateAsset(AssetName, LibraryNiagara, Source));
				if (System)
				{
					OutNotes.Add(FString::Printf(TEXT("Template built from %s"), *Source->GetPathName()));
					break;
				}
			}
		}

#if NAMETBDAI_NIAGARA_EMITTER_TEMPLATES
		// 2. An emitter template in a new system.
		if (!System)
		{
			for (const FString& Base : GetStringArray(Recipe, TEXT("base_emitters")))
			{
				FString Ignored;
				UNiagaraEmitter* Emitter = Cast<UNiagaraEmitter>(NameTBDAIHelpers::FindAsset(Base, UNiagaraEmitter::StaticClass(), Ignored));
				if (!Emitter)
				{
					continue;
				}
				UNiagaraSystemFactoryNew* Factory = NewObject<UNiagaraSystemFactoryNew>();
				System = Cast<UNiagaraSystem>(AssetTools.CreateAsset(AssetName, LibraryNiagara, UNiagaraSystem::StaticClass(), Factory));
				if (System)
				{
					FNiagaraEditorUtilities::AddEmitterToSystem(*System, *Emitter, Emitter->GetExposedVersion().VersionGuid, true);
					OutNotes.Add(FString::Printf(TEXT("Template built from the emitter %s"), *Emitter->GetPathName()));
					break;
				}
			}
		}
#endif
		if (!System)
		{
			OutError = FString::Printf(TEXT("The template '%s' isn't shipped built (%s/Niagara/%s) and none of its bases were found in this engine. Author it once in the Niagara editor (see the recipe in Resources/AI/Niagara/Templates.json) and save it there."),
				*Name, ShippedRoot, *AssetName);
			return nullptr;
		}

		FString MaterialError;
		UMaterialInterface* Material = GetMaster(GetString(Recipe, TEXT("material"), TEXT("SoftDot")), MaterialError);
		MakeUIReady(System, Material, OutNotes);
		ApplyNiagaraConstants(System, GetObject(Recipe, TEXT("constants")), OutNotes);
		System->RequestCompile(/*bForce=*/true);
		System->WaitForCompilationComplete();
		Save(System);
		return System;
	}

	void BuildAll(TArray<FString>& OutReport, bool bForce)
	{
		if (bForce)
		{
			// No stamps: every project copy counts as built from an older definition, and is rebuilt once.
			IFileManager::Get().Delete(*StampFile(), false, false, true);
		}
		TGuardValue<TArray<FString>*> Collect(BuildReport, &OutReport);
		UE_LOG(LogTemp, Display, TEXT("NameTBD AI: building the library%s (%s)..."), bForce ? TEXT(", every asset again") : TEXT(""), *UNameTBDAISettings::Get().GetLibraryFolder());

		// Functions before masters: a master calls them.
		for (const TCHAR* Kind : { TEXT("function"), TEXT("master") })
		{
			for (const FDefinition& Definition : GetDefinitions())
			{
				if (Definition.Kind != Kind)
				{
					continue;
				}
				FString Error;
				const bool bOk = Definition.Kind == TEXT("function") ? GetFunction(Definition.Name, Error) != nullptr : GetMaster(Definition.Name, Error) != nullptr;
				if (!bOk)
				{
					Report(FString::Printf(TEXT("%s: %s"), *Definition.Name, *Error), true);
				}
			}
		}
		for (const auto& Pair : LoadNiagaraRecipes()->Values)
		{
			FString Error;
			TArray<FString> Notes;
			if (!GetNiagaraTemplate(Pair.Key, Error, Notes))
			{
				Report(FString::Printf(TEXT("%s: %s"), *Pair.Key, *Error), true);
			}
		}
		UE_LOG(LogTemp, Display, TEXT("NameTBD AI: library done - %d line(s) above (nothing listed: everything was already built and current)."), OutReport.Num());
	}
}
