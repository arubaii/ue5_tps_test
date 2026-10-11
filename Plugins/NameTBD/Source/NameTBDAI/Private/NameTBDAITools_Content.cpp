// Copyright AR GameSystems 2026. All rights reserved.

// Content tools: UI materials (instances of the library's masters, or whole new graphs) and Niagara UI systems (from the
// library's templates), plus their parameters.

#include "Components/Widget.h"
#include "Engine/Texture.h"
#include "Factories/MaterialFactoryNew.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "HAL/FileManager.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NameTBDAIHelpers.h"
#include "NameTBDAIJson.h"
#include "NameTBDAILibrary.h"
#include "NameTBDAIMaterialGraph.h"
#include "NameTBDAITools.h"
#include "NiagaraSystem.h"
#include "NiagaraUIWidget.h"
#include "WidgetBlueprint.h"

using namespace NameTBDAIJson;

namespace NameTBDAIContentTools
{
	/** Every parameter a material has, by kind. */
	TSharedPtr<FJsonObject> DescribeParameters(UMaterialInterface* Material)
	{
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		auto Add = [&Out](const TCHAR* Kind, const TArray<FName>& Names)
		{
			if (Names.Num() == 0)
			{
				return;
			}
			TArray<FString> Strings;
			for (const FName& Name : Names)
			{
				Strings.Add(Name.ToString());
			}
			Out->SetField(Kind, Arr(Strings));
		};
		TArray<FName> Names;
		UMaterialEditingLibrary::GetScalarParameterNames(Material, Names);
		Add(TEXT("scalar"), Names);
		Names.Reset();
		UMaterialEditingLibrary::GetVectorParameterNames(Material, Names);
		Add(TEXT("vector"), Names);
		Names.Reset();
		UMaterialEditingLibrary::GetTextureParameterNames(Material, Names);
		Add(TEXT("texture"), Names);
		Names.Reset();
		UMaterialEditingLibrary::GetStaticSwitchParameterNames(Material, Names);
		Add(TEXT("switch"), Names);
		return Out;
	}

	/** Sets parameters on an instance: numbers as scalars, colors as vectors, asset paths as textures, bools as switches. */
	void SetInstanceParameters(UMaterialInstanceConstant* Instance, const TSharedPtr<FJsonObject>& Params, TArray<FString>& OutErrors)
	{
		if (!Instance || !Params.IsValid())
		{
			return;
		}
		NameTBDAIHelpers::ExpandThemeTokens(Params, NameTBDAIHelpers::IsDark(FString()));
		Instance->Modify();

		TArray<FName> Scalars, Vectors, Textures, Switches;
		UMaterialEditingLibrary::GetScalarParameterNames(Instance, Scalars);
		UMaterialEditingLibrary::GetVectorParameterNames(Instance, Vectors);
		UMaterialEditingLibrary::GetTextureParameterNames(Instance, Textures);
		UMaterialEditingLibrary::GetStaticSwitchParameterNames(Instance, Switches);
		auto Match = [](const TArray<FName>& Names, const FString& Wanted) -> FName
		{
			const FString Key = Normalize(Wanted);
			for (const FName& Name : Names)
			{
				if (Normalize(Name.ToString()) == Key)
				{
					return Name;
				}
			}
			return NAME_None;
		};

		for (const auto& Pair : Params->Values)
		{
			double Number = 0.0;
			bool bValue = false;
			FLinearColor Color;
			if (const FName Scalar = Match(Scalars, Pair.Key); !Scalar.IsNone() && Pair.Value->TryGetNumber(Number))
			{
				UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(Instance, Scalar, static_cast<float>(Number));
			}
			else if (const FName Vector = Match(Vectors, Pair.Key); !Vector.IsNone() && ParseColor(Pair.Value, Color))
			{
				UMaterialEditingLibrary::SetMaterialInstanceVectorParameterValue(Instance, Vector, Color);
			}
			else if (const FName Switch = Match(Switches, Pair.Key); !Switch.IsNone() && Pair.Value->TryGetBool(bValue))
			{
				UMaterialEditingLibrary::SetMaterialInstanceStaticSwitchParameterValue(Instance, Switch, bValue);
			}
			else if (const FName Texture = Match(Textures, Pair.Key); !Texture.IsNone())
			{
				FString Error;
				UTexture* Found = Cast<UTexture>(NameTBDAIHelpers::FindAsset(Pair.Value->AsString(), UTexture::StaticClass(), Error));
				if (Found)
				{
					UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(Instance, Texture, Found);
				}
				else
				{
					OutErrors.Add(FString::Printf(TEXT("%s: %s"), *Pair.Key, *Error));
				}
			}
			else
			{
				OutErrors.Add(FString::Printf(TEXT("No parameter '%s' that takes %s. The material has: %s."), *Pair.Key, *ToString(Pair.Value), *ToString(DescribeParameters(Instance))));
			}
		}
		UMaterialEditingLibrary::UpdateMaterialInstance(Instance);
	}

	/** An instance (MI_) of a library master or any material, its parameters set - create_material_instance and build_ui_material's from_master. */
	FNameTBDAIToolResult MakeMaterialInstance(const FString& MasterName, const FString& Name, const FString& Folder, const TSharedPtr<FJsonObject>& Params, FNameTBDAIToolContext& Context)
	{
		FString Error;
		UMaterialInterface* Master = NameTBDAILibrary::GetMaster(MasterName, Error);
		if (!Master)
		{
			return FNameTBDAIToolResult::Fail(Error);
		}
		UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
		Factory->InitialParent = Master;
		UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(NameTBDAIHelpers::CreateAsset(Folder,
			NameTBDAIHelpers::MakeAssetName(Name, TEXT("MI_")), UMaterialInstanceConstant::StaticClass(), Factory, Context, Error));
		if (!Instance)
		{
			return FNameTBDAIToolResult::Fail(Error);
		}
		TArray<FString> Errors;
		SetInstanceParameters(Instance, Params, Errors);
		NameTBDAIMaterialGraph::RecordAttempt(Context.Model, nullptr, false, false, 0);
		FNameTBDAIToolResult Result = FNameTBDAIToolResult::Ok();
		Result.Data->SetStringField(TEXT("material"), Instance->GetPathName());
		Result.Data->SetStringField(TEXT("master"), Master->GetPathName());
		Result.Data->SetObjectField(TEXT("parameters"), DescribeParameters(Instance));
		Result.Warnings = Errors;
		return Result;
	}

	/** The graph a build / validate call means: hlsl made into one, graph, or a master's graph extended by graph. Null with OutError. */
	TSharedPtr<FJsonObject> GraphFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutError)
	{
		TSharedPtr<FJsonObject> Graph = GetObject(Args, TEXT("graph"));
		const FString MasterName = GetString(Args, TEXT("from_master"));
		if (const TSharedPtr<FJsonObject> Hlsl = GetObject(Args, TEXT("hlsl")))
		{
			if (!MasterName.IsEmpty() || Graph.IsValid())
			{
				OutError = TEXT("Give hlsl, or graph (with from_master) - not both. A variation of a master: list_options material_masters <Name> shows its hlsl to start from.");
				return nullptr;
			}
			// blend / tint written next to hlsl count too.
			for (const TCHAR* Key : { TEXT("blend"), TEXT("tint") })
			{
				if (!Hlsl->HasField(Key) && Args->HasField(Key))
				{
					Hlsl->SetField(Key, Args->TryGetField(Key));
				}
			}
			// The Custom node is titled with the material's name in the graph.
			if (!Hlsl->HasField(TEXT("title")))
			{
				const FString Title = Args->HasField(TEXT("name")) ? GetString(Args, TEXT("name")) : FPaths::GetBaseFilename(GetString(Args, TEXT("material")));
				Hlsl->SetStringField(TEXT("title"), Title.IsEmpty() ? FString(TEXT("HLSL")) : Title);
			}
			return NameTBDAIMaterialGraph::HlslToGraph(Hlsl, false, OutError);
		}
		if (MasterName.IsEmpty())
		{
			if (!Graph.IsValid())
			{
				OutError = TEXT("Give 'hlsl' ({code, params}) or 'graph' ({nodes, outputs}) - or 'from_master' to start from a library master.");
			}
			return Graph;
		}
		const TSharedPtr<FJsonObject> MasterGraph = NameTBDAILibrary::GetMasterGraph(MasterName);
		if (!MasterGraph.IsValid())
		{
			TArray<FString> Names;
			NameTBDAILibrary::DescribeMasters()->Values.GetKeys(Names);
			Names.Remove(TEXT("$use"));
			OutError = FString::Printf(TEXT("No library master '%s' (masters: %s)."), *MasterName, *FString::Join(Names, TEXT(", ")));
			return nullptr;
		}
		return NameTBDAIMaterialGraph::MergeWithMaster(MasterGraph, Graph.IsValid() ? Graph : MakeShared<FJsonObject>(), OutError);
	}

	// ---- Which template a Niagara system was made from (for friendly parameter names) ---------------------------------

	FString GetRegistryFile()
	{
		return FPaths::ProjectSavedDir() / TEXT("NameTBDAI") / TEXT("niagara_systems.json");
	}

	void RememberTemplate(const UNiagaraSystem* System, const FString& Template)
	{
		FString Text;
		FFileHelper::LoadFileToString(Text, *GetRegistryFile());
		TSharedPtr<FJsonObject> Registry = ParseObject(Text);
		if (!Registry.IsValid())
		{
			Registry = MakeShared<FJsonObject>();
		}
		Registry->SetStringField(System->GetPathName(), Template);
		FFileHelper::SaveStringToFile(ToString(Registry, true), *GetRegistryFile());
	}

	FString RecallTemplate(const UNiagaraSystem* System)
	{
		FString Text;
		FFileHelper::LoadFileToString(Text, *GetRegistryFile());
		const TSharedPtr<FJsonObject> Registry = ParseObject(Text);
		return Registry.IsValid() && System ? GetString(Registry, System->GetPathName()) : FString();
	}

	/**
	 * Friendly params ({"rate": 20, "color": "#FFFFFF40"}) to module constants through the template's param map:
	 * "params": {"rate": {"default": 12, "constants": {"SpawnRate.SpawnRate": "{value}"}}}. Unknown names into OutUnknown.
	 */
	TSharedPtr<FJsonObject> MapParams(const TSharedPtr<FJsonObject>& Recipe, const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutWidgetProps, TArray<FString>& OutUnknown)
	{
		TSharedPtr<FJsonObject> Constants = MakeShared<FJsonObject>();
		OutWidgetProps = MakeShared<FJsonObject>();
		const TSharedPtr<FJsonObject> Map = GetObject(Recipe, TEXT("params"));
		if (!Params.IsValid())
		{
			return Constants;
		}
		for (const auto& Pair : Params->Values)
		{
			TSharedPtr<FJsonObject> Entry;
			if (Map.IsValid())
			{
				for (const auto& Candidate : Map->Values)
				{
					if (Normalize(Candidate.Key) == Normalize(Pair.Key) && Candidate.Value->Type == EJson::Object)
					{
						Entry = Candidate.Value->AsObject();
					}
				}
			}
			if (!Entry.IsValid())
			{
				OutUnknown.Add(Pair.Key);
				continue;
			}
			auto Fill = [&Pair](const TSharedPtr<FJsonValue>& Template) -> TSharedPtr<FJsonValue>
			{
				// "{value}" is the value given; "{value*0.5}" scales a number.
				FString Text;
				if (Template->TryGetString(Text) && Text.StartsWith(TEXT("{value")))
				{
					double Scale = 1.0;
					int32 Star = INDEX_NONE;
					if (Text.FindChar(TEXT('*'), Star))
					{
						LexTryParseString(Scale, *Text.Mid(Star + 1).LeftChop(1));
					}
					double Number = 0.0;
					if (Scale != 1.0 && Pair.Value->TryGetNumber(Number))
					{
						return Num(Number * Scale);
					}
					return Pair.Value;
				}
				if (Template->Type == EJson::Array)
				{
					TArray<TSharedPtr<FJsonValue>> Items;
					double Number = 0.0;
					for (const TSharedPtr<FJsonValue>& Item : Template->AsArray())
					{
						FString ItemText;
						if (Item->TryGetString(ItemText) && ItemText.StartsWith(TEXT("{value")) && Pair.Value->TryGetNumber(Number))
						{
							double Scale = 1.0;
							int32 Star = INDEX_NONE;
							if (ItemText.FindChar(TEXT('*'), Star))
							{
								LexTryParseString(Scale, *ItemText.Mid(Star + 1).LeftChop(1));
							}
							Items.Add(Num(Number * Scale));
						}
						else
						{
							Items.Add(Item);
						}
					}
					return MakeShared<FJsonValueArray>(Items);
				}
				return Template;
			};
			if (TSharedPtr<FJsonObject> EntryConstants = GetObject(Entry, TEXT("constants")))
			{
				for (const auto& Constant : EntryConstants->Values)
				{
					Constants->SetField(Constant.Key, Fill(Constant.Value));
				}
			}
			if (TSharedPtr<FJsonObject> EntryWidget = GetObject(Entry, TEXT("widget")))
			{
				for (const auto& Prop : EntryWidget->Values)
				{
					OutWidgetProps->SetField(Prop.Key, Fill(Prop.Value));
				}
			}
		}
		return Constants;
	}

	FString ListParamNames(const TSharedPtr<FJsonObject>& Recipe)
	{
		TArray<FString> Names;
		if (TSharedPtr<FJsonObject> Map = GetObject(Recipe, TEXT("params")))
		{
			Map->Values.GetKeys(Names);
		}
		return FString::Join(Names, TEXT(", "));
	}

	TSharedPtr<FJsonObject> DescribeSystem(UNiagaraSystem* System)
	{
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("system"), System->GetPathName());
		TArray<FNiagaraVariable> UserParameters;
		System->GetExposedParameters().GetUserParameters(UserParameters);
		TArray<FString> Names;
		for (const FNiagaraVariable& Variable : UserParameters)
		{
			Names.Add(FString::Printf(TEXT("%s (%s)"), *Variable.GetName().ToString().Replace(TEXT("User."), TEXT("")), *Variable.GetType().GetName()));
		}
		if (Names.Num() > 0)
		{
			Out->SetField(TEXT("user_parameters"), Arr(Names));
		}
		return Out;
	}
}

namespace NameTBDAITools
{
	void RegisterMaterialTools()
	{
		using namespace NameTBDAIContentTools;

		Register({
			TEXT("create_material_instance"),
			TEXT("Makes a Material Instance (MI_) of a library master UI material (list_options material_masters: Aurora, SoftGlow, Shimmer, Gradient, NoiseFog, Vignette, RingPulse, Dissolve, SoftDot, Bubble) or of any material, with parameters set. Instant and can't break - the fast path for looks. Use it with set_shape_paint(material=...) or as a brush."),
			FNameTBDAISchema()
				.Str(TEXT("master"), TEXT("Library master name, or a material path."), true)
				.Str(TEXT("name"), TEXT("Asset name (MI_ added)."), true)
				.Str(TEXT("folder"), TEXT("Content folder (default: Output Folder/Materials)."))
				.Obj(TEXT("params"), TEXT("{ParamName: number | color (#hex, @token) | texture path | bool}."))
				.Build(),
			true, false, false,
			[](const TSharedPtr<FJsonObject>& Args, FNameTBDAIToolContext& Context)
			{
				return MakeMaterialInstance(GetString(Args, TEXT("master")), GetString(Args, TEXT("name")), GetString(Args, TEXT("folder"), TEXT("Materials")), GetObject(Args, TEXT("params")), Context);
			}
		});

		Register({
			TEXT("set_material_parameters"),
			TEXT("Changes a Material Instance's parameters (\"make the glow purpler\", \"slower\"). Everything using it updates."),
			FNameTBDAISchema()
				.Str(TEXT("material"), TEXT("Material Instance path or name."), true)
				.Obj(TEXT("params"), TEXT("{ParamName: value}."), true)
				.Build(),
			true, false, false,
			[](const TSharedPtr<FJsonObject>& Args, FNameTBDAIToolContext& Context)
			{
				FString Error;
				UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(NameTBDAIHelpers::FindAsset(GetString(Args, TEXT("material")), UMaterialInstanceConstant::StaticClass(), Error));
				if (!Instance)
				{
					return FNameTBDAIToolResult::Fail(Error.IsEmpty() ? FString(TEXT("That isn't a Material Instance - make one with create_material_instance.")) : Error);
				}
				TArray<FString> Errors;
				SetInstanceParameters(Instance, GetObject(Args, TEXT("params")), Errors);
				Context.Changes->NoteTouched(Instance, FString::Printf(TEXT("Changed %s"), *Instance->GetName()));
				FNameTBDAIToolResult Result = FNameTBDAIToolResult::Ok();
				Result.Warnings = Errors;
				return Result;
			}
		});

		Register({
			TEXT("validate_material"),
			TEXT("Checks a build_ui_material graph or hlsl without building anything (no asset): every problem at once, each tied to its node id with what to change, plus what would be fixed for you. HLSL code itself is only checked by the real compile (build_ui_material). Same arguments as build_ui_material."),
			FNameTBDAISchema()
				.Obj(TEXT("hlsl"), TEXT("{code, params, blend} (see build_ui_material)."))
				.Obj(TEXT("graph"), TEXT("The node graph (see build_ui_material)."))
				.Str(TEXT("from_master"), TEXT("A library master the graph extends."))
				.Build(),
			false, false, false,
			[](const TSharedPtr<FJsonObject>& Args, FNameTBDAIToolContext& Context)
			{
				FString Error;
				const TSharedPtr<FJsonObject> Graph = GraphFromArgs(Args, Error);
				if (!Graph.IsValid())
				{
					return FNameTBDAIToolResult::Fail(Error);
				}
				const NameTBDAIMaterialGraph::FGraph Prepared = NameTBDAIMaterialGraph::Prepare(Graph);
				NameTBDAIMaterialGraph::RecordAttempt(Context.Model, &Prepared, false, false, 0);
				FNameTBDAIToolResult Result = FNameTBDAIToolResult::Ok(Prepared.Summary());
				Result.Data->SetStringField(TEXT("next"), Prepared.HasErrors() ? TEXT("Fix every error, then build_ui_material.") : TEXT("build_ui_material with the same graph."));
				return Result;
			}
		});

		Register({
			TEXT("build_ui_material"),
			TEXT("A new UI material, for a look no master covers (a master instance is better when one fits: create_material_instance). "
				"Write it in HLSL: hlsl {code, params, blend}. code is the body of a function that returns float4(color, alpha); it can use uv (float2, 0..1, (0,0) top left), time (seconds) and every param by its name - a number is a float, a color (\"#RRGGBB\" or \"#RRGGBBAA\") a float4 RGBA, [x, y] a float2. Each param becomes a material parameter, so later changes are set_material_parameters on an instance - expose colors, sizes, speeds. blend: Translucent (default) | Additive (glows) | Masked. The widget's tint and opacity are applied for you, and the alpha is kept in 0..1. "
				"HLSL rules: statements end with ;, declare every variable (float, float2, float3, float4), no functions, textures or # lines, built-ins (sin, cos, frac, floor, abs, pow, lerp, saturate, smoothstep, distance, length, dot, atan2...) plus the helpers NTB_Noise(p) (0..1), NTB_Fbm(p, octaves), NTB_Hash(p), NTB_SdCircle(uv, center, radius), NTB_SdRoundRect(uv, center, halfSize, radius) (distances, < 0 inside), NTB_Fill(distance, softness), NTB_Outline(distance, width, softness), NTB_Ramp3(t, colorA, colorB, colorC), NTB_Wave(time, speed); smoothstep works either way (smoothstep(0.5, 0.2, x) falls); sin(time * 6.2832 * Speed) waves Speed times a second. "
				"Example: {name: \"PulseRing\", hlsl: {blend: \"Additive\", params: {Color: \"#7C5CFF\", Radius: 0.35, Speed: 1.5}, code: \"float d = distance(uv, float2(0.5, 0.5)); float ring = 1.0 - smoothstep(0.0, 0.03, abs(d - Radius)); float pulse = 0.6 + 0.4 * sin(time * 6.2832 * Speed); return float4(Color.rgb, ring * pulse * Color.a);\"}}. "
				"Compile errors come back with a hint - fix the code and call again with 'material' set to rebuild in place. list_options material_masters <Name> shows a library master's own hlsl (a good start for a variation). "
				"Also takes a node graph instead (graph {nodes, outputs}; macros and nodes: list_options material_nodes), and from_master + graph to add nodes on top of a master (\"master.FinalColor\" = its color)."),
			FNameTBDAISchema()
				.Str(TEXT("name"), TEXT("New material name (M_ added; MI_ with from_master alone). Omit when rebuilding."))
				.Str(TEXT("material"), TEXT("An existing material (made by this tool) to rebuild."))
				.Str(TEXT("folder"), TEXT("Content folder (default: Output Folder/Materials)."))
				.Obj(TEXT("hlsl"), TEXT("{code, params, blend} - the material in HLSL (see description)."))
				.Obj(TEXT("graph"), TEXT("Or a node graph (see description)."))
				.Str(TEXT("from_master"), TEXT("A library master to start from (Aurora, SoftGlow, Shimmer, Gradient, NoiseFog, Vignette, RingPulse, Dissolve, SoftDot, Bubble)."))
				.Obj(TEXT("params"), TEXT("With from_master alone: the instance's parameters {Name: value}."))
				.Build(),
			true, false, false,
			[](const TSharedPtr<FJsonObject>& Args, FNameTBDAIToolContext& Context)
			{
				const FString Folder = GetString(Args, TEXT("folder"), TEXT("Materials"));
				const TSharedPtr<FJsonObject> GivenGraph = GetObject(Args, TEXT("graph"));
				const FString MasterName = GetString(Args, TEXT("from_master"));
				if (!MasterName.IsEmpty() && (!GivenGraph.IsValid() || GivenGraph->Values.Num() == 0) && !Args->HasField(TEXT("hlsl")))
				{
					// Nothing to add to the master: an instance - instant, nothing to compile.
					FNameTBDAIToolResult Instance = MakeMaterialInstance(MasterName, GetString(Args, TEXT("name"), MasterName), Folder, GetObject(Args, TEXT("params")), Context);
					if (Instance.bOk)
					{
						Instance.Data->SetStringField(TEXT("note"), TEXT("No graph given: made an instance of the master (no compile). Tune it with set_material_parameters."));
					}
					return Instance;
				}

				FString Error;
				const TSharedPtr<FJsonObject> Graph = GraphFromArgs(Args, Error);
				if (!Graph.IsValid())
				{
					return FNameTBDAIToolResult::Fail(Error);
				}
				const NameTBDAIMaterialGraph::FGraph Prepared = NameTBDAIMaterialGraph::Prepare(Graph);
				if (Prepared.HasErrors())
				{
					NameTBDAIMaterialGraph::RecordAttempt(Context.Model, &Prepared, true, false, 0);
					FNameTBDAIToolResult Result = FNameTBDAIToolResult::Fail(TEXT("The graph has problems - nothing was built. Fix every error listed and call build_ui_material again."));
					Result.Data = Prepared.Summary();
					return Result;
				}

				UMaterial* Material = nullptr;
				const FString Existing = GetString(Args, TEXT("material"));
				if (!Existing.IsEmpty())
				{
					Material = Cast<UMaterial>(NameTBDAIHelpers::FindAsset(Existing, UMaterial::StaticClass(), Error));
					if (!Material)
					{
						return FNameTBDAIToolResult::Fail(Error);
					}
					Context.Changes->NoteTouched(Material, FString::Printf(TEXT("Rebuilt %s"), *Material->GetName()));
				}
				else
				{
					UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
					Material = Cast<UMaterial>(NameTBDAIHelpers::CreateAsset(Folder,
						NameTBDAIHelpers::MakeAssetName(GetString(Args, TEXT("name"), TEXT("UIEffect")), TEXT("M_")), UMaterial::StaticClass(), Factory, Context, Error));
					if (!Material)
					{
						return FNameTBDAIToolResult::Fail(Error);
					}
				}

				TArray<FString> Problems;
				int32 CompileErrors = 0;
				const bool bCompiled = NameTBDAILibrary::BuildPreparedMaterial(Material, Prepared, Problems, CompileErrors);
				NameTBDAIMaterialGraph::RecordAttempt(Context.Model, &Prepared, true, bCompiled, CompileErrors);

				FNameTBDAIToolResult Result = bCompiled ? FNameTBDAIToolResult::Ok(Prepared.Summary())
					: FNameTBDAIToolResult::Fail(TEXT("Built, but it didn't compile - fix what's listed and call build_ui_material again with 'material' set to rebuild it."));
				if (!bCompiled)
				{
					Result.Data = Prepared.Summary();
					Result.Data->SetBoolField(TEXT("ok"), false);
					Result.Data->SetField(TEXT("compile_errors"), Arr(Problems));
				}
				else
				{
					Result.Warnings = Problems;
				}
				Result.Data->SetStringField(TEXT("material"), Material->GetPathName());
				Result.Data->SetBoolField(TEXT("compiled"), bCompiled);
				Result.Data->SetObjectField(TEXT("parameters"), DescribeParameters(Material));
				if (bCompiled && Prepared.Fixed.Num() > 0)
				{
					Result.Data->SetStringField(TEXT("note"), TEXT("Built with the fixes listed - read them: write it that way next time."));
				}
				return Result;
			}
		});
	}

	void RegisterNiagaraTools()
	{
		using namespace NameTBDAIContentTools;

		Register({
			TEXT("create_niagara_ui_system"),
			TEXT("Makes a Niagara system for UI (NS_) from a library template (list_options niagara_templates: RisingBubbles, Sparkles, Confetti, Embers, GlowRing, Trail, RisingSparkleLoop), CPU / local space / UI material, tuned by friendly params (rate, size, opacity, rise_speed, color, lifetime... per template). Then show it with a NiagaraUIWidget (role particles, props {NiagaraSystem: <path>}). Returns notes on what each param did."),
			FNameTBDAISchema()
				.Str(TEXT("template"), TEXT("Template name."), true)
				.Str(TEXT("name"), TEXT("Asset name (NS_ added)."), true)
				.Str(TEXT("folder"), TEXT("Content folder (default: Output Folder/Particles)."))
				.Obj(TEXT("params"), TEXT("Friendly params, e.g. {rate: 10, opacity: 0.25, rise_speed: 60, size: 10, color: \"#FFFFFF\"}."))
				.Obj(TEXT("constants"), TEXT("Advanced: module inputs directly, {\"Module.Input\": value}."))
				.Build(),
			true, false, false,
			[](const TSharedPtr<FJsonObject>& Args, FNameTBDAIToolContext& Context)
			{
				FString Error;
				TArray<FString> Notes;
				const FString TemplateName = GetString(Args, TEXT("template"));
				UNiagaraSystem* Template = NameTBDAILibrary::GetNiagaraTemplate(TemplateName, Error, Notes);
				if (!Template)
				{
					return FNameTBDAIToolResult::Fail(Error);
				}
				UNiagaraSystem* System = Cast<UNiagaraSystem>(NameTBDAIHelpers::DuplicateAsset(Template, GetString(Args, TEXT("folder"), TEXT("Particles")),
					NameTBDAIHelpers::MakeAssetName(GetString(Args, TEXT("name")), TEXT("NS_")), Context, Error));
				if (!System)
				{
					return FNameTBDAIToolResult::Fail(Error);
				}
				RememberTemplate(System, TemplateName);

				const TSharedPtr<FJsonObject> Recipe = NameTBDAILibrary::FindNiagaraRecipe(TemplateName);
				TArray<FString> Unknown;
				TSharedPtr<FJsonObject> WidgetProps;
				TSharedPtr<FJsonObject> Constants = MapParams(Recipe, GetObject(Args, TEXT("params")), WidgetProps, Unknown);
				if (TSharedPtr<FJsonObject> Raw = GetObject(Args, TEXT("constants")))
				{
					for (const auto& Pair : Raw->Values)
					{
						Constants->SetField(Pair.Key, Pair.Value);
					}
				}
				NameTBDAIHelpers::ExpandThemeTokens(Constants, NameTBDAIHelpers::IsDark(FString()));
				NameTBDAILibrary::ApplyNiagaraConstants(System, Constants, Notes);
				System->RequestCompile(false);
				System->WaitForCompilationComplete();

				FNameTBDAIToolResult Result = FNameTBDAIToolResult::Ok(DescribeSystem(System));
				if (TSharedPtr<FJsonObject> Suggested = GetObject(Recipe, TEXT("widget")))
				{
					for (const auto& Pair : WidgetProps->Values)
					{
						Suggested->SetField(Pair.Key, Pair.Value);
					}
					Suggested->SetStringField(TEXT("NiagaraSystem"), System->GetPathName());
					Result.Data->SetObjectField(TEXT("widget_props"), Suggested);
				}
				Result.Data->SetField(TEXT("notes"), Arr(Notes));
				if (Unknown.Num() > 0)
				{
					Result.Warnings.Add(FString::Printf(TEXT("Params not known to %s: %s (it has: %s)."), *TemplateName, *FString::Join(Unknown, TEXT(", ")), *ListParamNames(Recipe)));
				}
				return Result;
			}
		});

		Register({
			TEXT("set_niagara_parameters"),
			TEXT("Tunes particles: on a NiagaraUIWidget in a Widget Blueprint, its User Parameters (by name) and its own settings (ColorAndOpacity, ParticleScale, Origin...); and, for a system made by create_niagara_ui_system, the template's friendly params (rate, size, rise_speed...) on the system asset itself. Give blueprint+widget, or system."),
			FNameTBDAISchema()
				.Str(TEXT("blueprint"), TEXT("Widget Blueprint holding the Niagara widget (omit: the one built last or open)."))
				.Str(TEXT("widget"), TEXT("The NiagaraUIWidget's name."))
				.Str(TEXT("system"), TEXT("Or a Niagara system asset directly."))
				.Obj(TEXT("params"), TEXT("{name: value}: User Parameters, the widget's own fields, or the template's friendly params."), true)
				.Build(),
			true, false, false,
			[](const TSharedPtr<FJsonObject>& Args, FNameTBDAIToolContext& Context)
			{
				FString Error;
				UWidgetBlueprint* Blueprint = nullptr;
				UNiagaraUIWidget* Widget = nullptr;
				UNiagaraSystem* System = nullptr;
				const FString SystemName = GetString(Args, TEXT("system"));
				if (!SystemName.IsEmpty())
				{
					System = Cast<UNiagaraSystem>(NameTBDAIHelpers::FindAsset(SystemName, UNiagaraSystem::StaticClass(), Error));
				}
				else
				{
					Blueprint = NameTBDAIHelpers::FindWidgetBlueprint(GetString(Args, TEXT("blueprint")), Error);
					Widget = Blueprint ? Cast<UNiagaraUIWidget>(NameTBDAIHelpers::FindWidget(Blueprint, GetString(Args, TEXT("widget")), Error)) : nullptr;
					System = Widget ? Widget->NiagaraSystem.Get() : nullptr;
					if (Blueprint && !Widget && Error.IsEmpty())
					{
						Error = TEXT("That widget isn't a NiagaraUIWidget.");
					}
				}
				if (!System && !Widget)
				{
					return FNameTBDAIToolResult::Fail(Error);
				}

				TSharedPtr<FJsonObject> Params = GetObject(Args, TEXT("params"));
				NameTBDAIHelpers::ExpandThemeTokens(Params, NameTBDAIHelpers::IsDark(FString()));
				TSharedPtr<FJsonObject> Remaining = MakeShared<FJsonObject>();
				TArray<FString> Errors;
				TArray<FString> Notes;

				// 1. The widget: its User Parameters bag, then its own fields.
				if (Widget)
				{
					TSharedPtr<FJsonObject> WidgetProps = MakeShared<FJsonObject>();
					TSharedPtr<FJsonObject> BagProps = MakeShared<FJsonObject>();
					const UPropertyBag* Bag = Widget->UserParameters.GetPropertyBagStruct();
					for (const auto& Pair : Params->Values)
					{
						if (Bag && FindField(Bag, Pair.Key))
						{
							BagProps->SetField(Pair.Key, Pair.Value);
						}
						else if (FindField(Widget->GetClass(), Pair.Key))
						{
							WidgetProps->SetField(Pair.Key, Pair.Value);
						}
						else
						{
							Remaining->SetField(Pair.Key, Pair.Value);
						}
					}
					if (BagProps->Values.Num() > 0)
					{
						WidgetProps->SetObjectField(TEXT("UserParameters"), BagProps);
					}
					Blueprint->Modify();
					NameTBDAIJson::ApplyToObject(Widget, WidgetProps, Errors);
					Context.Changes->NoteTouched(Blueprint, FString::Printf(TEXT("Tuned %s"), *Widget->GetName()));
				}
				else
				{
					Remaining = Params;
				}

				// 2. The system: the template's friendly params, as module constants.
				if (Remaining->Values.Num() > 0)
				{
					const FString Template = RecallTemplate(System);
					const TSharedPtr<FJsonObject> Recipe = Template.IsEmpty() ? nullptr : NameTBDAILibrary::FindNiagaraRecipe(Template);
					if (!Recipe.IsValid())
					{
						TArray<FString> Names;
						Remaining->Values.GetKeys(Names);
						Errors.Add(FString::Printf(TEXT("%s: not a User Parameter or widget field%s."), *FString::Join(Names, TEXT(", ")),
							System ? *FString::Printf(TEXT(" (%s)"), *ToString(DescribeSystem(System))) : TEXT("")));
					}
					else
					{
						TArray<FString> Unknown;
						TSharedPtr<FJsonObject> WidgetFromParams;
						TSharedPtr<FJsonObject> Constants = MapParams(Recipe, Remaining, WidgetFromParams, Unknown);
						System->Modify();
						NameTBDAILibrary::ApplyNiagaraConstants(System, Constants, Notes);
						System->RequestCompile(false);
						System->WaitForCompilationComplete();
						Context.Changes->NoteTouched(System, FString::Printf(TEXT("Tuned %s"), *System->GetName()));
						if (Widget && WidgetFromParams->Values.Num() > 0)
						{
							NameTBDAIJson::ApplyToObject(Widget, WidgetFromParams, Errors);
						}
						if (Unknown.Num() > 0)
						{
							Errors.Add(FString::Printf(TEXT("Unknown params: %s (the template has: %s)."), *FString::Join(Unknown, TEXT(", ")), *ListParamNames(Recipe)));
						}
					}
				}

				FNameTBDAIToolResult Result = FNameTBDAIToolResult::Ok();
				if (Notes.Num() > 0)
				{
					Result.Data->SetField(TEXT("notes"), Arr(Notes));
				}
				if (Blueprint)
				{
					TArray<FString> Problems;
					NameTBDAIHelpers::Compile(Blueprint, Problems);
					Errors.Append(Problems);
				}
				Result.Warnings = Errors;
				return Result;
			}
		});
	}
}
