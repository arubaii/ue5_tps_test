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
				FString Error;
				UMaterialInterface* Master = NameTBDAILibrary::GetMaster(GetString(Args, TEXT("master")), Error);
				if (!Master)
				{
					return FNameTBDAIToolResult::Fail(Error);
				}
				UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
				Factory->InitialParent = Master;
				const FString Folder = GetString(Args, TEXT("folder"), TEXT("Materials"));
				UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(NameTBDAIHelpers::CreateAsset(Folder,
					NameTBDAIHelpers::MakeAssetName(GetString(Args, TEXT("name")), TEXT("MI_")), UMaterialInstanceConstant::StaticClass(), Factory, Context, Error));
				if (!Instance)
				{
					return FNameTBDAIToolResult::Fail(Error);
				}
				TArray<FString> Errors;
				SetInstanceParameters(Instance, GetObject(Args, TEXT("params")), Errors);
				FNameTBDAIToolResult Result = FNameTBDAIToolResult::Ok();
				Result.Data->SetStringField(TEXT("material"), Instance->GetPathName());
				Result.Data->SetStringField(TEXT("master"), Master->GetPathName());
				Result.Data->SetObjectField(TEXT("parameters"), DescribeParameters(Instance));
				Result.Warnings = Errors;
				return Result;
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
			TEXT("build_ui_material"),
			TEXT("Builds a whole UI material from a node graph - a new effect the masters don't cover - and compiles it, returning compile errors to fix. Expose every tunable as a ScalarParameter / VectorParameter so later changes are create_material_instance / set_material_parameters, not graph edits. Graph: {domain: UI (default), blend: Translucent | Additive | Masked, nodes: [{id, type (TextureCoordinate, VertexColor, Time, Panner, Noise, Multiply, Add, Subtract, Divide, Lerp, Sine, Power, Saturate, OneMinus, ComponentMask, AppendVector, Distance, SmoothStep, ScalarParameter, VectorParameter, TextureSampleParameter2D, MaterialFunctionCall, Custom...; list_options material_nodes), props {ParameterName, DefaultValue, R/G/B/A, Constant, Code...}, function (for MaterialFunctionCall: library function name or path), inputs {InputName: \"node\" | \"node.Output\" | number | [x,y,z] | \"#hex\"}}], outputs: {FinalColor: ref, Opacity: ref}}. Multiply the color by VertexColor.RGB and opacity by VertexColor.A so widget tints and Niagara particle colors apply. Rebuild an existing one by passing 'material'."),
			FNameTBDAISchema()
				.Str(TEXT("name"), TEXT("New material name (M_ added). Omit when rebuilding."))
				.Str(TEXT("material"), TEXT("An existing material (made by this tool) to rebuild from the graph."))
				.Str(TEXT("folder"), TEXT("Content folder (default: Output Folder/Materials)."))
				.Obj(TEXT("graph"), TEXT("The node graph (see description)."), true)
				.Build(),
			true, false, false,
			[](const TSharedPtr<FJsonObject>& Args, FNameTBDAIToolContext& Context)
			{
				FString Error;
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
					Material = Cast<UMaterial>(NameTBDAIHelpers::CreateAsset(GetString(Args, TEXT("folder"), TEXT("Materials")),
						NameTBDAIHelpers::MakeAssetName(GetString(Args, TEXT("name"), TEXT("UIEffect")), TEXT("M_")), UMaterial::StaticClass(), Factory, Context, Error));
					if (!Material)
					{
						return FNameTBDAIToolResult::Fail(Error);
					}
				}
				TArray<FString> Problems;
				const bool bCompiled = NameTBDAILibrary::BuildMaterialGraph(Material, GetObject(Args, TEXT("graph")), Problems);
				FNameTBDAIToolResult Result = FNameTBDAIToolResult::Ok();
				Result.Data->SetStringField(TEXT("material"), Material->GetPathName());
				Result.Data->SetBoolField(TEXT("compiled"), bCompiled);
				Result.Data->SetObjectField(TEXT("parameters"), DescribeParameters(Material));
				if (!bCompiled)
				{
					Result.Data->SetStringField(TEXT("next"), TEXT("Fix the graph and call build_ui_material again with 'material' set to rebuild it."));
				}
				Result.Warnings = Problems;
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
