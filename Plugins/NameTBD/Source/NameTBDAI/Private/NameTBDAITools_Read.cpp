// Copyright AR GameSystems 2026. All rights reserved.

// Read tools: cheap, compact facts - the selection, a widget tree, valid options, docs, assets, the theme. Nothing here
// changes anything.

#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "ContentBrowserModule.h"
#include "Editor.h"
#include "Extensions/UIComponent.h"
#include "IContentBrowserSingleton.h"
#include "Materials/MaterialExpression.h"
#include "NameTBD/WidgetAnimation/WidgetAnimPreset.h"
#include "NameTBDAIDocs.h"
#include "NameTBDAIHelpers.h"
#include "NameTBDAIJson.h"
#include "NameTBDAILibrary.h"
#include "NameTBDAIMaterialGraph.h"
#include "NameTBDAITools.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/UObjectIterator.h"
#include "WidgetAnimScripting.h"
#include "WidgetAnimTrackPresets.h"
#include "WidgetBlueprint.h"

using namespace NameTBDAIJson;

namespace NameTBDAIReadTools
{
	/** Whether a class comes from a module the AI builds with: UMG, CommonUI, the plugin, or the project. */
	bool IsOfferedPackage(const UClass* Class)
	{
		const FString Package = Class->GetOutermost()->GetName();
		return Package == TEXT("/Script/UMG") || Package.StartsWith(TEXT("/Script/NameTBD")) || Package == TEXT("/Script/CommonUI")
			|| Package.StartsWith(TEXT("/Game/"));
	}

	TSharedPtr<FJsonValue> ListClasses(const UClass* Base, const FString& Filter, bool bSkipUserWidgets)
	{
		TArray<FString> Names;
		const FString Key = Normalize(Filter);
		for (TObjectIterator<UClass> It; It; ++It)
		{
			const UClass* Class = *It;
			if (!Class->IsChildOf(Base) || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists | CLASS_Hidden)
				|| Class->GetName().StartsWith(TEXT("SKEL_")) || Class->GetName().StartsWith(TEXT("REINST_")) || !IsOfferedPackage(Class))
			{
				continue;
			}
			if (bSkipUserWidgets && Class->IsChildOf(UUserWidget::StaticClass()))
			{
				continue;
			}
			const FString Name = Class->ClassGeneratedBy ? Class->GetPathName() : Class->GetName();
			if (!Key.IsEmpty() && !Normalize(Name).Contains(Key) && !Normalize(Class->GetMetaData(TEXT("DisplayName"))).Contains(Key))
			{
				continue;
			}
			Names.Add(Name);
		}
		Names.Sort();
		return Arr(Names);
	}

	TSharedPtr<FJsonObject> DescribeEnum(const UEnum* Enum)
	{
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("enum"), Enum->GetName());
		TArray<TSharedPtr<FJsonValue>> Values;
		for (int32 Index = 0; Index < Enum->NumEnums() - (Enum->ContainsExistingMax() ? 1 : 0); ++Index)
		{
			if (Enum->HasMetaData(TEXT("Hidden"), Index))
			{
				continue;
			}
			FString Entry = Enum->GetNameStringByIndex(Index);
			const FString Tip = Enum->GetToolTipTextByIndex(Index).ToString();
			if (!Tip.IsEmpty() && Tip != Entry)
			{
				Entry += TEXT(" - ") + Tip.Left(140);
			}
			Values.Add(Str(Entry));
		}
		Out->SetArrayField(TEXT("values"), Values);
		return Out;
	}

	/** A struct's or class's editable fields: name, type and the first line of its comment. */
	TSharedPtr<FJsonObject> DescribeFields(const UStruct* Struct, const FString& Filter)
	{
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("type"), Struct->GetName());
		const FString Summary = Struct->GetMetaData(TEXT("ToolTip"));
		if (!Summary.IsEmpty())
		{
			Out->SetStringField(TEXT("about"), Summary.Left(400));
		}
		const FString Key = Normalize(Filter);
		TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			const FProperty* Property = *It;
			if (Property->HasAnyPropertyFlags(CPF_Deprecated | CPF_Transient) || (Struct->IsA<UClass>() && !Property->HasAnyPropertyFlags(CPF_Edit)))
			{
				continue;
			}
			if (!Key.IsEmpty() && !Normalize(Property->GetAuthoredName()).Contains(Key))
			{
				continue;
			}
			FString Tip = Property->GetMetaData(TEXT("ToolTip"));
			int32 Break = INDEX_NONE;
			if (Tip.FindChar(TEXT('\n'), Break))
			{
				Tip = Tip.Left(Break);
			}
			FString Type = Property->GetCPPType();
			if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
			{
				Type = EnumProperty->GetEnum()->GetName();
			}
			Fields->SetStringField(Property->GetAuthoredName(), Tip.IsEmpty() ? Type : FString::Printf(TEXT("%s - %s"), *Type, *Tip.Left(160)));
		}
		Out->SetObjectField(TEXT("fields"), Fields);
		return Out;
	}

	TSharedPtr<FJsonObject> MotionRecipes();
}

namespace NameTBDAITools
{
	void RegisterReadTools()
	{
		using namespace NameTBDAIReadTools;

		// ---- get_context ----
		Register({
			TEXT("get_context"),
			TEXT("What the user is looking at: open Widget Blueprints, the widgets selected in the designer (with their non-default settings), the asset built last, and the Content Browser selection. Call this first when a request says 'this', 'the selected', 'it'."),
			FNameTBDAISchema().Bool(TEXT("props"), TEXT("Include the selected widgets' settings (default true).")).Build(),
			false, true, false,
			[](const TSharedPtr<FJsonObject>& Args, FNameTBDAIToolContext&)
			{
				const bool bProps = GetBool(Args, TEXT("props"), true);
				TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
				TArray<FString> Open;
				if (UAssetEditorSubsystem* AssetEditors = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr)
				{
					for (UObject* Asset : AssetEditors->GetAllEditedAssets())
					{
						if (Asset)
						{
							Open.Add(Asset->GetPathName());
						}
					}
				}
				Out->SetField(TEXT("open_assets"), Arr(Open));

				if (UWidgetBlueprint* Focused = NameTBDAIHelpers::GetFocusedWidgetBlueprint())
				{
					Out->SetStringField(TEXT("focused_widget_blueprint"), Focused->GetPathName());
					TArray<TSharedPtr<FJsonValue>> Selected;
					for (UWidget* Widget : NameTBDAIHelpers::GetSelectedWidgets(Focused))
					{
						Selected.Add(Obj(NameTBDAIHelpers::DescribeWidget(Widget, bProps)));
					}
					Out->SetArrayField(TEXT("selected_widgets"), Selected);
				}
				if (UObject* Last = GetLastAsset())
				{
					Out->SetStringField(TEXT("last_built"), Last->GetPathName());
				}
				TArray<FAssetData> Browsed;
				FContentBrowserModule& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
				ContentBrowser.Get().GetSelectedAssets(Browsed);
				TArray<FString> BrowsedPaths;
				for (const FAssetData& Asset : Browsed)
				{
					BrowsedPaths.Add(FString::Printf(TEXT("%s (%s)"), *Asset.GetObjectPathString(), *Asset.AssetClassPath.GetAssetName().ToString()));
				}
				Out->SetField(TEXT("content_browser_selection"), Arr(BrowsedPaths));
				return FNameTBDAIToolResult::Ok(Out);
			}
		});

		// ---- describe_widget_blueprint ----
		Register({
			TEXT("describe_widget_blueprint"),
			TEXT("A Widget Blueprint's widget tree: names, classes, nesting; with props=true also each widget's and slot's non-default settings. Omit blueprint for the one built last or open."),
			FNameTBDAISchema()
				.Str(TEXT("blueprint"), TEXT("Path or name of the Widget Blueprint."))
				.Bool(TEXT("props"), TEXT("Include non-default settings (larger output)."))
				.Build(),
			false, true, false,
			[](const TSharedPtr<FJsonObject>& Args, FNameTBDAIToolContext&)
			{
				FString Error;
				UWidgetBlueprint* Blueprint = NameTBDAIHelpers::FindWidgetBlueprint(GetString(Args, TEXT("blueprint")), Error);
				if (!Blueprint)
				{
					return FNameTBDAIToolResult::Fail(Error);
				}
				return FNameTBDAIToolResult::Ok(NameTBDAIHelpers::DescribeTree(Blueprint, GetBool(Args, TEXT("props"), false)));
			}
		});

		// ---- describe_widget ----
		Register({
			TEXT("describe_widget"),
			TEXT("One widget of a Widget Blueprint: its non-default settings and its slot's; or, with 'path', one field's value (\"Fill.Color\", \"Font.Size\", \"AnimIn[0]\")."),
			FNameTBDAISchema()
				.Str(TEXT("blueprint"), TEXT("Widget Blueprint path or name (omit: the one built last or open)."))
				.Str(TEXT("widget"), TEXT("Widget name (omit: the root)."))
				.Str(TEXT("path"), TEXT("Optional field path to read."))
				.Build(),
			false, true, false,
			[](const TSharedPtr<FJsonObject>& Args, FNameTBDAIToolContext&)
			{
				FString Error;
				UWidgetBlueprint* Blueprint = NameTBDAIHelpers::FindWidgetBlueprint(GetString(Args, TEXT("blueprint")), Error);
				UWidget* Widget = Blueprint ? NameTBDAIHelpers::FindWidget(Blueprint, GetString(Args, TEXT("widget")), Error) : nullptr;
				if (!Widget)
				{
					return FNameTBDAIToolResult::Fail(Error);
				}
				const FString Path = GetString(Args, TEXT("path"));
				if (!Path.IsEmpty())
				{
					TSharedPtr<FJsonValue> Value = ReadPath(Widget->GetClass(), Widget, Path, Error);
					if (!Value.IsValid())
					{
						return FNameTBDAIToolResult::Fail(Error);
					}
					TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
					Out->SetField(Path, Value);
					return FNameTBDAIToolResult::Ok(Out);
				}
				return FNameTBDAIToolResult::Ok(NameTBDAIHelpers::DescribeWidget(Widget, true));
			}
		});

		// ---- list_options ----
		Register({
			TEXT("list_options"),
			TEXT("Valid values, so you never guess: kind = widget_classes | components | track_types | motion_recipes | spring_presets | track_presets | material_masters (filter = one master: its params and graph) | material_functions | material_nodes (build_ui_material's macros and nodes with exact inputs and output widths; filter = one node in detail, or \"all\") | material_macros | niagara_templates | theme | enum (name in 'filter', e.g. EWidgetAnimEasing) | fields (a class or struct's editable fields with types and meaning: name in 'filter', e.g. RectangleShape, FWidgetShapePaint, FWidgetAnimLayer)."),
			FNameTBDAISchema()
				.Str(TEXT("kind"), TEXT("What to list."), true, { TEXT("widget_classes"), TEXT("components"), TEXT("track_types"), TEXT("motion_recipes"), TEXT("spring_presets"), TEXT("track_presets"), TEXT("material_masters"), TEXT("material_functions"), TEXT("material_nodes"), TEXT("material_macros"), TEXT("niagara_templates"), TEXT("theme"), TEXT("enum"), TEXT("fields") })
				.Str(TEXT("filter"), TEXT("Narrows the list; for enum / fields, the type's name."))
				.Str(TEXT("field_filter"), TEXT("For fields: only fields whose name contains this."))
				.Build(),
			false, true, false,
			[](const TSharedPtr<FJsonObject>& Args, FNameTBDAIToolContext&)
			{
				const FString Kind = GetString(Args, TEXT("kind"));
				const FString Filter = GetString(Args, TEXT("filter"));
				TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();

				if (Kind == TEXT("widget_classes"))
				{
					Out->SetField(TEXT("widget_classes"), ListClasses(UWidget::StaticClass(), Filter, true));
					Out->SetStringField(TEXT("note"), TEXT("A Widget Blueprint can also be added by its asset path. Plugin shapes hold one child (content on top)."));
				}
				else if (Kind == TEXT("components"))
				{
					Out->SetField(TEXT("components"), ListClasses(UUIComponent::StaticClass(), Filter, false));
				}
				else if (Kind == TEXT("track_types"))
				{
					UWidgetAnimPresetAsset* Scratch = NewObject<UWidgetAnimPresetAsset>(GetTransientPackage());
					if (UWidgetAnimScriptAnimation* Script = UWidgetAnimScriptLibrary::EditAnimationAsset(Scratch))
					{
						Out->SetField(TEXT("track_types"), Arr(Script->GetTrackTypes()));
					}
					Out->SetStringField(TEXT("note"), TEXT("Fields per type: list_options fields FWidgetAnimLayer (registered types keep theirs under TypeParams)."));
				}
				else if (Kind == TEXT("motion_recipes"))
				{
					Out = MotionRecipes();
				}
				else if (Kind == TEXT("spring_presets"))
				{
					TArray<FString> Names;
					NameTBDAIHelpers::GetSpringPresetNames(Names);
					Out->SetField(TEXT("spring_presets"), Arr(Names));
					Out->SetStringField(TEXT("use"), TEXT("Set a spring field to \"@spring.Bouncy\"."));
				}
				else if (Kind == TEXT("track_presets"))
				{
					TArray<WidgetAnimTrackPresets::FPreset> Presets;
					WidgetAnimTrackPresets::GetPresets(Presets);
					TArray<FString> Names;
					for (const WidgetAnimTrackPresets::FPreset& Preset : Presets)
					{
						Names.Add(FString::Printf(TEXT("%s (%d track%s)"), *Preset.Name, Preset.Layers.Num(), Preset.Layers.Num() == 1 ? TEXT("") : TEXT("s")));
					}
					Out->SetField(TEXT("track_presets"), Arr(Names));
				}
				else if (Kind == TEXT("material_masters"))
				{
					// One master named: its parameters and graph (to extend with build_ui_material's from_master).
					const TSharedPtr<FJsonObject> One = Filter.IsEmpty() ? nullptr : NameTBDAILibrary::DescribeMaster(Filter);
					Out = One.IsValid() ? One : NameTBDAILibrary::DescribeMasters();
				}
				else if (Kind == TEXT("material_functions"))
				{
					Out = NameTBDAILibrary::DescribeFunctions();
				}
				else if (Kind == TEXT("material_nodes"))
				{
					Out = NameTBDAIMaterialGraph::DescribeNodes(Filter);
				}
				else if (Kind == TEXT("material_macros"))
				{
					Out = NameTBDAIMaterialGraph::DescribeMacros();
				}
				else if (Kind == TEXT("niagara_templates"))
				{
					Out = NameTBDAILibrary::DescribeNiagaraTemplates();
				}
				else if (Kind == TEXT("theme"))
				{
					Out = NameTBDAIHelpers::ThemeToJson(NameTBDAIHelpers::IsDark(Filter));
				}
				else if (Kind == TEXT("enum"))
				{
					const UEnum* Enum = FindFirstObject<UEnum>(*Filter, EFindFirstObjectOptions::None);
					if (!Enum && !Filter.StartsWith(TEXT("E")))
					{
						Enum = FindFirstObject<UEnum>(*(TEXT("E") + Filter), EFindFirstObjectOptions::None);
					}
					if (!Enum)
					{
						return FNameTBDAIToolResult::Fail(FString::Printf(TEXT("No enum '%s'."), *Filter));
					}
					Out = DescribeEnum(Enum);
				}
				else if (Kind == TEXT("fields"))
				{
					const UStruct* Struct = nullptr;
					FString Name = Filter.TrimStartAndEnd();
					if (UClass* Class = FindClass(Name, nullptr))
					{
						Struct = Class;
					}
					if (!Struct)
					{
						const FString Bare = Name.StartsWith(TEXT("F")) ? Name.Mid(1) : Name;
						Struct = FindFirstObject<UScriptStruct>(*Bare, EFindFirstObjectOptions::None);
					}
					if (!Struct)
					{
						return FNameTBDAIToolResult::Fail(FString::Printf(TEXT("No class or struct '%s'."), *Filter));
					}
					Out = DescribeFields(Struct, GetString(Args, TEXT("field_filter")));
				}
				return FNameTBDAIToolResult::Ok(Out);
			}
		});

		// ---- search_docs / get_docs ----
		Register({
			TEXT("search_docs"),
			TEXT("Searches the plugin's docs (guides, every class/struct/enum's own documentation, editor help). Returns topic ids, titles and snippets; read one with get_docs."),
			FNameTBDAISchema().Str(TEXT("query"), TEXT("Words to search for."), true).Int(TEXT("max"), TEXT("Most hits (default 6).")).Build(),
			false, true, false,
			[](const TSharedPtr<FJsonObject>& Args, FNameTBDAIToolContext&)
			{
				TArray<TSharedPtr<FJsonValue>> Hits;
				for (const NameTBDAIDocs::FHit& Hit : NameTBDAIDocs::Search(GetString(Args, TEXT("query")), FMath::Clamp(static_cast<int32>(GetNumber(Args, TEXT("max"), 6)), 1, 15)))
				{
					TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
					Entry->SetStringField(TEXT("id"), Hit.Topic->Id);
					Entry->SetStringField(TEXT("title"), Hit.Topic->Title);
					Entry->SetStringField(TEXT("snippet"), Hit.Snippet);
					Hits.Add(Obj(Entry));
				}
				TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
				Out->SetArrayField(TEXT("hits"), Hits);
				return FNameTBDAIToolResult::Ok(Out);
			}
		});

		Register({
			TEXT("get_docs"),
			TEXT("One doc topic by id (from search_docs) or title. Long topics come in pages."),
			FNameTBDAISchema().Str(TEXT("topic"), TEXT("Topic id or title."), true).Int(TEXT("page"), TEXT("Page, from 1.")).Build(),
			false, true, false,
			[](const TSharedPtr<FJsonObject>& Args, FNameTBDAIToolContext&)
			{
				const NameTBDAIDocs::FTopic* Topic = NameTBDAIDocs::Find(GetString(Args, TEXT("topic")));
				if (!Topic)
				{
					return FNameTBDAIToolResult::Fail(TEXT("No such topic - use search_docs."));
				}
				constexpr int32 PageChars = 2400;
				const int32 Pages = FMath::Max(1, FMath::DivideAndRoundUp(Topic->Text.Len(), PageChars));
				const int32 Page = FMath::Clamp(static_cast<int32>(GetNumber(Args, TEXT("page"), 1)), 1, Pages);
				TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
				Out->SetStringField(TEXT("title"), Topic->Title);
				Out->SetStringField(TEXT("text"), Topic->Text.Mid((Page - 1) * PageChars, PageChars));
				if (Pages > 1)
				{
					Out->SetStringField(TEXT("page"), FString::Printf(TEXT("%d of %d"), Page, Pages));
				}
				return FNameTBDAIToolResult::Ok(Out);
			}
		});

		// ---- find_assets ----
		Register({
			TEXT("find_assets"),
			TEXT("Finds assets by name: Widget Blueprints, Widget Animations (WA_), state machines (WASM_), materials, Niagara systems, textures, fonts."),
			FNameTBDAISchema()
				.Str(TEXT("query"), TEXT("Part of the name."))
				.Str(TEXT("class"), TEXT("Asset class: WidgetBlueprint, WidgetAnimPresetAsset, WidgetAnimStateMachineAsset, Material, MaterialInstanceConstant, NiagaraSystem, Texture2D, Font..."))
				.Str(TEXT("folder"), TEXT("Only under this folder (default: everything under /Game and the plugin)."))
				.Int(TEXT("max"), TEXT("Most results (default 30)."))
				.Build(),
			false, true, false,
			[](const TSharedPtr<FJsonObject>& Args, FNameTBDAIToolContext&)
			{
				IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
				FARFilter Filter;
				const FString ClassName = GetString(Args, TEXT("class"));
				if (!ClassName.IsEmpty())
				{
					FString Error;
					UClass* Class = FindClass(ClassName, UObject::StaticClass(), &Error);
					if (!Class)
					{
						return FNameTBDAIToolResult::Fail(Error);
					}
					Filter.ClassPaths.Add(Class->GetClassPathName());
					Filter.bRecursiveClasses = true;
				}
				const FString Folder = GetString(Args, TEXT("folder"));
				Filter.PackagePaths.Add(FName(Folder.IsEmpty() ? TEXT("/Game") : *Folder));
				if (Folder.IsEmpty())
				{
					Filter.PackagePaths.Add(TEXT("/NameTBD"));
				}
				Filter.bRecursivePaths = true;
				TArray<FAssetData> Assets;
				Registry.GetAssets(Filter, Assets);

				const FString Key = Normalize(GetString(Args, TEXT("query")));
				const int32 Max = FMath::Clamp(static_cast<int32>(GetNumber(Args, TEXT("max"), 30)), 1, 200);
				TArray<FString> Found;
				for (const FAssetData& Asset : Assets)
				{
					if (!Key.IsEmpty() && !Normalize(Asset.AssetName.ToString()).Contains(Key))
					{
						continue;
					}
					Found.Add(FString::Printf(TEXT("%s (%s)"), *Asset.PackageName.ToString(), *Asset.AssetClassPath.GetAssetName().ToString()));
					if (Found.Num() >= Max)
					{
						break;
					}
				}
				TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
				Out->SetField(TEXT("assets"), Arr(Found));
				return FNameTBDAIToolResult::Ok(Out);
			}
		});

		// ---- get_asset ----
		Register({
			TEXT("get_asset"),
			TEXT("An asset's non-default settings as compact JSON (a Widget Animation's groups and tracks, a state machine, a material instance's parameters, a data asset). 'fields' limits it to some fields or paths."),
			FNameTBDAISchema()
				.Str(TEXT("asset"), TEXT("Path or name."), true)
				.Arr(TEXT("fields"), TEXT("Only these fields or paths (\"Layers[2]\", \"LayerGroups\")."), TEXT("string"))
				.Build(),
			false, true, false,
			[](const TSharedPtr<FJsonObject>& Args, FNameTBDAIToolContext&)
			{
				FString Error;
				UObject* Asset = NameTBDAIHelpers::FindAsset(GetString(Args, TEXT("asset")), nullptr, Error);
				if (!Asset)
				{
					return FNameTBDAIToolResult::Fail(Error);
				}
				if (UWidgetBlueprint* Blueprint = Cast<UWidgetBlueprint>(Asset))
				{
					return FNameTBDAIToolResult::Ok(NameTBDAIHelpers::DescribeTree(Blueprint, true));
				}
				TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
				Out->SetStringField(TEXT("asset"), Asset->GetPathName());
				Out->SetStringField(TEXT("class"), Asset->GetClass()->GetName());
				const TArray<FString> Fields = GetStringArray(Args, TEXT("fields"));
				if (Fields.Num() == 0)
				{
					Out->SetObjectField(TEXT("props"), ReadObjectNonDefault(Asset, 4));
				}
				else
				{
					TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
					for (const FString& Field : Fields)
					{
						FString FieldError;
						TSharedPtr<FJsonValue> Value = ReadPath(Asset->GetClass(), Asset, Field, FieldError);
						Props->SetField(Field, Value.IsValid() ? Value : Str(FieldError));
					}
					Out->SetObjectField(TEXT("props"), Props);
				}
				return FNameTBDAIToolResult::Ok(Out);
			}
		});
	}
}
