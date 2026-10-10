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
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant2Vector.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionConstant4Vector.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialFunction.h"
#include "Misc/FileHelper.h"
#include "CoreGlobals.h"
#include "Misc/ITransaction.h"
#include "Templates/UnrealTemplate.h"
#include "NameTBDAIDocs.h"
#include "NameTBDAIHelpers.h"
#include "NameTBDAIJson.h"
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

		UClass* FindExpressionClass(const FString& Type)
		{
			if (UClass* Class = FindClass(TEXT("MaterialExpression") + Type.Replace(TEXT(" "), TEXT("")), UMaterialExpression::StaticClass()))
			{
				return Class;
			}
			return FindClass(Type, UMaterialExpression::StaticClass());
		}

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

		struct FGraphBuild
		{
			FGraphTarget Target;
			TMap<FString, UMaterialExpression*> Nodes;
			TArray<FString>* Problems = nullptr;
			int32 ConstantCount = 0;

			/** The expression and output a reference names: "node", "node.Output", a number, [x,y(,z(,w))] or a color. */
			UMaterialExpression* Resolve(const TSharedPtr<FJsonValue>& Ref, FString& OutOutput)
			{
				OutOutput.Reset();
				if (!Ref.IsValid())
				{
					return nullptr;
				}
				const int32 X = -1400;
				const int32 Y = 200 * (ConstantCount++);
				double Number = 0.0;
				if (Ref->Type == EJson::Number && Ref->TryGetNumber(Number))
				{
					UMaterialExpressionConstant* Constant = Cast<UMaterialExpressionConstant>(Target.Create(UMaterialExpressionConstant::StaticClass(), X, Y));
					Constant->R = static_cast<float>(Number);
					return Constant;
				}
				FLinearColor Color;
				if (Ref->Type == EJson::Array || (Ref->Type == EJson::String && Ref->AsString().StartsWith(TEXT("#"))))
				{
					const int32 Count = Ref->Type == EJson::Array ? Ref->AsArray().Num() : 4;
					if (Count == 2)
					{
						UMaterialExpressionConstant2Vector* Constant = Cast<UMaterialExpressionConstant2Vector>(Target.Create(UMaterialExpressionConstant2Vector::StaticClass(), X, Y));
						Constant->R = static_cast<float>(Ref->AsArray()[0]->AsNumber());
						Constant->G = static_cast<float>(Ref->AsArray()[1]->AsNumber());
						return Constant;
					}
					if (ParseColor(Ref, Color))
					{
						if (Count == 3)
						{
							UMaterialExpressionConstant3Vector* Constant = Cast<UMaterialExpressionConstant3Vector>(Target.Create(UMaterialExpressionConstant3Vector::StaticClass(), X, Y));
							Constant->Constant = Color;
							return Constant;
						}
						UMaterialExpressionConstant4Vector* Constant = Cast<UMaterialExpressionConstant4Vector>(Target.Create(UMaterialExpressionConstant4Vector::StaticClass(), X, Y));
						Constant->Constant = Color;
						return Constant;
					}
					Problems->Add(FString::Printf(TEXT("Can't read %s as a constant."), *ToString(Ref)));
					return nullptr;
				}
				FString Text;
				if (!Ref->TryGetString(Text))
				{
					Problems->Add(FString::Printf(TEXT("Can't read %s as a node reference."), *ToString(Ref)));
					return nullptr;
				}
				FString Id = Text;
				Text.Split(TEXT("."), &Id, &OutOutput);
				if (UMaterialExpression** Found = Nodes.Find(Id))
				{
					return *Found;
				}
				Problems->Add(FString::Printf(TEXT("No node '%s'."), *Id));
				return nullptr;
			}

			void Connect(const TSharedPtr<FJsonValue>& Ref, UMaterialExpression* To, const FString& InputName, const FString& ToId)
			{
				FString Output;
				UMaterialExpression* From = Resolve(Ref, Output);
				if (!From)
				{
					return;
				}
				if (!UMaterialEditingLibrary::ConnectMaterialExpressions(From, Output, To, InputName))
				{
					Problems->Add(FString::Printf(TEXT("Couldn't connect %s to %s.%s (its inputs: %s)."), *ToString(Ref), *ToId, *InputName, *ListInputs(To)));
				}
			}
		};

		/** Nodes and their wiring, shared by materials and functions. */
		void BuildNodes(FGraphBuild& Build, const TSharedPtr<FJsonObject>& Graph)
		{
			const bool bDark = NameTBDAIHelpers::IsDark(FString());
			const TArray<TSharedPtr<FJsonValue>> NodeSpecs = GetArray(Graph, TEXT("nodes"));
			struct FPending
			{
				FString Id;
				UMaterialExpression* Expression = nullptr;
				TSharedPtr<FJsonObject> Inputs;
			};
			TArray<FPending> Pending;

			int32 Column = 0;
			for (const TSharedPtr<FJsonValue>& Value : NodeSpecs)
			{
				const TSharedPtr<FJsonObject> Spec = Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : nullptr;
				if (!Spec.IsValid())
				{
					Build.Problems->Add(TEXT("Each node is an object {id, type, props, inputs}."));
					continue;
				}
				const FString Id = GetString(Spec, TEXT("id"));
				const FString Type = GetString(Spec, TEXT("type"));
				UClass* Class = FindExpressionClass(Type);
				if (Id.IsEmpty() || !Class)
				{
					Build.Problems->Add(FString::Printf(TEXT("Node '%s': %s"), *Id, Class ? TEXT("needs an id.") : *FString::Printf(TEXT("no node type '%s' (list_options material_nodes)."), *Type)));
					continue;
				}
				if (Build.Nodes.Contains(Id))
				{
					Build.Problems->Add(FString::Printf(TEXT("Two nodes are called '%s'."), *Id));
					continue;
				}
				UMaterialExpression* Expression = Build.Target.Create(Class, -300 * (1 + Column / 6), 160 * (Column % 6));
				++Column;
				if (!Expression)
				{
					Build.Problems->Add(FString::Printf(TEXT("Couldn't make node '%s'."), *Id));
					continue;
				}

				if (UMaterialExpressionMaterialFunctionCall* Call = Cast<UMaterialExpressionMaterialFunctionCall>(Expression))
				{
					const FString FunctionName = GetString(Spec, TEXT("function"));
					FString Error;
					UMaterialFunctionInterface* Function = FunctionName.StartsWith(TEXT("/"))
						? Cast<UMaterialFunctionInterface>(NameTBDAIHelpers::FindAsset(FunctionName, UMaterialFunctionInterface::StaticClass(), Error))
						: GetFunction(FunctionName, Error);
					if (!Function || !Call->SetMaterialFunction(Function))
					{
						Build.Problems->Add(FString::Printf(TEXT("Node '%s': %s"), *Id, Error.IsEmpty() ? TEXT("couldn't use the function.") : *Error));
					}
				}

				if (TSharedPtr<FJsonObject> Props = GetObject(Spec, TEXT("props")))
				{
					NameTBDAIHelpers::ExpandThemeTokens(Props, bDark);
					TArray<FString> Errors;
					WriteObject(Expression->GetClass(), Expression, Props, Expression, Errors);
					for (const FString& Error : Errors)
					{
						Build.Problems->Add(FString::Printf(TEXT("Node '%s': %s"), *Id, *Error));
					}
				}
				Build.Nodes.Add(Id, Expression);
				Pending.Add({ Id, Expression, GetObject(Spec, TEXT("inputs")) });
			}

			for (const FPending& Each : Pending)
			{
				if (!Each.Inputs.IsValid())
				{
					continue;
				}
				for (const auto& Pair : Each.Inputs->Values)
				{
					Build.Connect(Pair.Value, Each.Expression, Pair.Key, Each.Id);
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
			if (Key == TEXT("worldpositionoffset")) { OutProperty = MP_WorldPositionOffset; return true; }
			return false;
		}

		void ApplyDomainAndBlend(UMaterial* Material, const TSharedPtr<FJsonObject>& Graph)
		{
			const FString Domain = Normalize(GetString(Graph, TEXT("domain"), TEXT("ui")));
			Material->MaterialDomain = Domain == TEXT("surface") ? MD_Surface : Domain == TEXT("postprocess") ? MD_PostProcess : MD_UI;
			const FString Blend = Normalize(GetString(Graph, TEXT("blend"), TEXT("translucent")));
			Material->BlendMode = Blend == TEXT("additive") ? BLEND_Additive : Blend == TEXT("opaque") ? BLEND_Opaque
				: Blend == TEXT("masked") ? BLEND_Masked : Blend == TEXT("alphacomposite") ? BLEND_AlphaComposite : BLEND_Translucent;
		}

		UMaterial* BuildMasterAsset(const FDefinition& Definition, FString& OutError)
		{
			// Library assets are shared and saved at once: never part of a request's undo step (a Discard must not empty them).
			TGuardValue<ITransaction*> NoUndo(GUndo, nullptr);
			const FString AssetName = TEXT("M_UI_") + Definition.Name;
			UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
			UMaterial* Material = Cast<UMaterial>(CreateLibraryAsset(TEXT("Materials"), AssetName, UMaterial::StaticClass(), Factory));
			if (!Material)
			{
				OutError = FString::Printf(TEXT("Couldn't make %s in the library folder."), *AssetName);
				return nullptr;
			}
			TArray<FString> Problems;
			const bool bCompiled = BuildMaterialGraph(Material, GetObject(Definition.Json, TEXT("graph")), Problems);
			if (!bCompiled)
			{
				UE_LOG(LogTemp, Warning, TEXT("NameTBD AI: library material %s: %s"), *AssetName, *FString::Join(Problems, TEXT(" | ")));
			}
			Save(Material);
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
			Function->Description = GetString(Definition.Json, TEXT("about"));
			TArray<FString> Problems;
			if (!BuildFunctionGraph(Function, GetObject(Definition.Json, TEXT("graph")), Problems))
			{
				UE_LOG(LogTemp, Warning, TEXT("NameTBD AI: library function %s: %s"), *AssetName, *FString::Join(Problems, TEXT(" | ")));
			}
			Save(Function);
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
		Out->SetStringField(TEXT("$use"), TEXT("In create_ui_material: {\"id\": \"c\", \"type\": \"MaterialFunctionCall\", \"function\": \"SoftCircle\", \"inputs\": {...}}; outputs by name: \"c.Result\"."));
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
		if (!Material || !Graph.IsValid())
		{
			OutProblems.Add(TEXT("No graph."));
			return false;
		}
		Material->Modify();
		UMaterialEditingLibrary::DeleteAllMaterialExpressions(Material);
		ApplyDomainAndBlend(Material, Graph);

		FGraphBuild Build;
		Build.Target.Material = Material;
		Build.Problems = &OutProblems;
		BuildNodes(Build, Graph);

		bool bAnyOutput = false;
		if (TSharedPtr<FJsonObject> Outputs = GetObject(Graph, TEXT("outputs")))
		{
			for (const auto& Pair : Outputs->Values)
			{
				EMaterialProperty Property;
				if (!ParseMaterialProperty(Pair.Key, Property))
				{
					OutProblems.Add(FString::Printf(TEXT("Output '%s': use FinalColor, Opacity, OpacityMask."), *Pair.Key));
					continue;
				}
				FString Output;
				if (UMaterialExpression* From = Build.Resolve(Pair.Value, Output))
				{
					bAnyOutput |= UMaterialEditingLibrary::ConnectMaterialProperty(From, Output, Property);
				}
			}
		}
		if (!bAnyOutput)
		{
			OutProblems.Add(TEXT("Nothing reaches the material's outputs: give outputs {\"FinalColor\": ..., \"Opacity\": ...}."));
		}

		UMaterialEditingLibrary::LayoutMaterialExpressions(Material);
		UMaterialEditingLibrary::RecompileMaterial(Material);

		// The compile's own errors - translation (graph) and shader - for the model to fix its graph from.
		if (FMaterialResource* Resource = Material->GetMaterialResource(GMaxRHIShaderPlatform))
		{
			Resource->FinishCompilation();
			for (const FString& Error : Resource->GetCompileErrors())
			{
				OutProblems.Add(TEXT("compile: ") + Error.Left(300));
			}
			return bAnyOutput && Resource->GetCompileErrors().Num() == 0 && OutProblems.Num() == 0;
		}
		return bAnyOutput && OutProblems.Num() == 0;
	}

	bool BuildFunctionGraph(UMaterialFunction* Function, const TSharedPtr<FJsonObject>& Graph, TArray<FString>& OutProblems)
	{
		if (!Function || !Graph.IsValid())
		{
			OutProblems.Add(TEXT("No graph."));
			return false;
		}
		Function->Modify();
		UMaterialEditingLibrary::DeleteAllMaterialExpressionsInFunction(Function);

		FGraphBuild Build;
		Build.Target.Function = Function;
		Build.Problems = &OutProblems;

		// Inputs first, so nodes can name them: {"name": "UV", "type": "Vector2", "preview": [0.5, 0.5]}.
		int32 Sort = 0;
		for (const TSharedPtr<FJsonValue>& Value : GetArray(Graph, TEXT("inputs")))
		{
			const TSharedPtr<FJsonObject> Spec = Value->Type == EJson::Object ? Value->AsObject() : nullptr;
			if (!Spec.IsValid())
			{
				continue;
			}
			UMaterialExpressionFunctionInput* Input = Cast<UMaterialExpressionFunctionInput>(Build.Target.Create(UMaterialExpressionFunctionInput::StaticClass(), -1200, 200 * Sort));
			TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
			const FString Name = GetString(Spec, TEXT("name"));
			Props->SetStringField(TEXT("InputName"), Name);
			Props->SetStringField(TEXT("InputType"), GetString(Spec, TEXT("type"), TEXT("Scalar")));
			Props->SetNumberField(TEXT("SortPriority"), Sort++);
			if (TSharedPtr<FJsonValue> Preview = Spec->TryGetField(TEXT("preview")))
			{
				Props->SetField(TEXT("PreviewValue"), Preview);
				Props->SetBoolField(TEXT("bUsePreviewValueAsDefault"), true);
			}
			TArray<FString> Errors;
			WriteObject(Input->GetClass(), Input, Props, Input, Errors);
			OutProblems.Append(Errors);
			Build.Nodes.Add(Name, Input);
		}

		BuildNodes(Build, Graph);

		Sort = 0;
		if (TSharedPtr<FJsonObject> Outputs = GetObject(Graph, TEXT("outputs")))
		{
			for (const auto& Pair : Outputs->Values)
			{
				UMaterialExpressionFunctionOutput* Output = Cast<UMaterialExpressionFunctionOutput>(Build.Target.Create(UMaterialExpressionFunctionOutput::StaticClass(), 400, 200 * Sort));
				Output->OutputName = FName(*Pair.Key);
				Output->SortPriority = Sort++;
				Build.Connect(Pair.Value, Output, FString(), Pair.Key);
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

	void BuildAll(TArray<FString>& OutReport)
	{
		for (const FDefinition& Definition : GetDefinitions())
		{
			FString Error;
			const bool bOk = Definition.Kind == TEXT("function") ? GetFunction(Definition.Name, Error) != nullptr : GetMaster(Definition.Name, Error) != nullptr;
			if (!bOk)
			{
				OutReport.Add(FString::Printf(TEXT("%s: %s"), *Definition.Name, *Error));
			}
		}
		for (const auto& Pair : LoadNiagaraRecipes()->Values)
		{
			FString Error;
			TArray<FString> Notes;
			if (!GetNiagaraTemplate(Pair.Key, Error, Notes))
			{
				OutReport.Add(FString::Printf(TEXT("%s: %s"), *Pair.Key, *Error));
			}
		}
	}
}
