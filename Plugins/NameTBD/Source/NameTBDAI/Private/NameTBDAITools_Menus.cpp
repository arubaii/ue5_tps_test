// Copyright AR GameSystems 2026. All rights reserved.

// Menu tools: a whole settings menu in one call (tabs, a style data asset it is bound to, custom tabs), settings added to a
// schema (a Render Distance slider into a console variable), and the list of the settings there are.

#include "Blueprint/WidgetTree.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Factories/DataAssetFactory.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "NameTBD/Menu/Graphics/MenuUpscalers.h"
#include "NameTBD/Menu/Settings/MenuSetting.h"
#include "NameTBD/Menu/Settings/MenuSettingsDefaults.h"
#include "NameTBD/Menu/Settings/MenuSettingsSchema.h"
#include "NameTBD/Menu/Settings/MenuSettingTargets.h"
#include "NameTBD/Menu/Widgets/MenuStyle.h"
#include "NameTBD/Menu/Widgets/SettingsMenu.h"
#include "NameTBD/Widgets/WidgetSVG.h"
#include "NameTBDAIHelpers.h"
#include "NameTBDAIJson.h"
#include "NameTBDAITools.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"

using namespace NameTBDAIJson;

namespace NameTBDAIMenuTools
{
	/** A tab as asked ("video", "Sound", "keybindings") -> a built-in page Id, or None for a custom page. */
	FName BuiltInPage(const FString& Tab)
	{
		const FString Key = Normalize(Tab);
		struct FSynonym { const TCHAR* Word; const TCHAR* Page; };
		static const FSynonym Synonyms[] = {
			{ TEXT("gameplay"), TEXT("Gameplay") }, { TEXT("general"), TEXT("Gameplay") }, { TEXT("game"), TEXT("Gameplay") },
			{ TEXT("video"), TEXT("Video") }, { TEXT("graphics"), TEXT("Video") }, { TEXT("display"), TEXT("Video") }, { TEXT("visuals"), TEXT("Video") },
			{ TEXT("audio"), TEXT("Audio") }, { TEXT("sound"), TEXT("Audio") }, { TEXT("sounds"), TEXT("Audio") }, { TEXT("volume"), TEXT("Audio") },
			{ TEXT("controls"), TEXT("Controls") }, { TEXT("control"), TEXT("Controls") }, { TEXT("input"), TEXT("Controls") },
			{ TEXT("keybindings"), TEXT("Controls") }, { TEXT("keys"), TEXT("Controls") }, { TEXT("controller"), TEXT("Controls") },
			{ TEXT("accessibility"), TEXT("Accessibility") }, { TEXT("access"), TEXT("Accessibility") },
		};
		for (const FSynonym& Synonym : Synonyms)
		{
			if (Key == Synonym.Word)
			{
				return Synonym.Page;
			}
		}
		return NAME_None;
	}

	/** "Online Play" -> "OnlinePlay". */
	FName MakeId(const FString& Text)
	{
		FString Out;
		bool bUpper = true;
		for (const TCHAR Char : Text)
		{
			if (FChar::IsAlnum(Char))
			{
				Out.AppendChar(bUpper ? FChar::ToUpper(Char) : Char);
				bUpper = false;
			}
			else
			{
				bUpper = true;
			}
		}
		return FName(Out.IsEmpty() ? TEXT("Custom") : *Out);
	}

	UMenuSettingsSchema* FindOrCreateSchema(const FString& PathOrName, const FString& Folder, const FString& NewName, FNameTBDAIToolContext& Context, FString& OutError)
	{
		if (!PathOrName.IsEmpty())
		{
			FString FindError;
			if (UMenuSettingsSchema* Found = Cast<UMenuSettingsSchema>(NameTBDAIHelpers::FindAsset(PathOrName, UMenuSettingsSchema::StaticClass(), FindError)))
			{
				return Found;
			}
		}
		UDataAssetFactory* Factory = NewObject<UDataAssetFactory>();
		Factory->DataAssetClass = UMenuSettingsSchema::StaticClass();
		const FString Name = PathOrName.IsEmpty() || PathOrName.Contains(TEXT("/")) ? NewName : PathOrName;
		UMenuSettingsSchema* Schema = Cast<UMenuSettingsSchema>(NameTBDAIHelpers::CreateAsset(Folder, NameTBDAIHelpers::MakeAssetName(Name, TEXT("DA_")),
			UMenuSettingsSchema::StaticClass(), Factory, Context, OutError));
		if (Schema)
		{
			Schema->SetFlags(RF_Transactional);
		}
		return Schema;
	}

	FMenuSettingsPage& FindOrAddPage(UMenuSettingsSchema* Schema, FName PageId, const FString& DisplayName, int32 Order)
	{
		for (FMenuSettingsPage& Page : Schema->Pages)
		{
			if (Page.Id == PageId)
			{
				if (!DisplayName.IsEmpty() && BuiltInPage(DisplayName).IsNone())
				{
					Page.DisplayName = FText::FromString(DisplayName);
				}
				return Page;
			}
		}
		FMenuSettingsPage& Page = Schema->Pages.AddDefaulted_GetRef();
		Page.Id = PageId;
		// A built-in page added to: its own name and order stay (the merge keeps them when these are empty / default).
		if (BuiltInPage(PageId.ToString()).IsNone())
		{
			Page.DisplayName = FText::FromString(DisplayName.IsEmpty() ? PageId.ToString() : DisplayName);
			Page.Order = Order;
		}
		return Page;
	}

	FMenuSettingsSection& FindOrAddSection(FMenuSettingsPage& Page, const FString& Title)
	{
		const FString WantedTitle = Title.IsEmpty() ? FString(TEXT("General")) : Title;
		const FName Id = MakeId(WantedTitle);
		for (FMenuSettingsSection& Section : Page.Sections)
		{
			if (Section.GetMergeId() == Id || Section.Title.ToString().Equals(WantedTitle, ESearchCase::IgnoreCase))
			{
				return Section;
			}
		}
		FMenuSettingsSection& Section = Page.Sections.AddDefaulted_GetRef();
		Section.Id = Id;
		Section.Title = FText::FromString(WantedTitle);
		return Section;
	}

	UClass* SettingClass(const FString& Type)
	{
		const FString Key = Normalize(Type);
		if (Key == TEXT("bool") || Key == TEXT("toggle") || Key == TEXT("switch") || Key == TEXT("checkbox"))
		{
			return UMenuSettingBool::StaticClass();
		}
		if (Key == TEXT("number") || Key == TEXT("slider") || Key == TEXT("float") || Key == TEXT("int"))
		{
			return UMenuSettingNumber::StaticClass();
		}
		if (Key == TEXT("choice") || Key == TEXT("enum") || Key == TEXT("dropdown") || Key == TEXT("selector") || Key == TEXT("options"))
		{
			return UMenuSettingChoice::StaticClass();
		}
		if (Key == TEXT("action") || Key == TEXT("button"))
		{
			return UMenuSettingAction::StaticClass();
		}
		if (Key == TEXT("info") || Key == TEXT("label") || Key == TEXT("readonly"))
		{
			return UMenuSettingInfo::StaticClass();
		}
		if (Key == TEXT("keybinding") || Key == TEXT("key") || Key == TEXT("binding"))
		{
			return UMenuSettingKeyBinding::StaticClass();
		}
		return nullptr;
	}

	FString TargetClassName(const FString& Type)
	{
		const FString Key = Normalize(Type);
		if (Key == TEXT("cvar") || Key == TEXT("consolevariable") || Key == TEXT("console")) return TEXT("MenuSettingTarget_ConsoleVariable");
		if (Key == TEXT("gameusersetting") || Key == TEXT("gameusersettings") || Key == TEXT("gus") || Key == TEXT("scalability")) return TEXT("MenuSettingTarget_GameUserSettings");
		if (Key == TEXT("builtin") || Key == TEXT("engine")) return TEXT("MenuSettingTarget_BuiltIn");
		if (Key == TEXT("sound") || Key == TEXT("volume") || Key == TEXT("soundvolume")) return TEXT("MenuSettingTarget_SoundVolume");
		if (Key == TEXT("notification") || Key == TEXT("notifications")) return TEXT("MenuSettingTarget_Notifications");
		if (Key == TEXT("graphics") || Key == TEXT("upscaler") || Key == TEXT("graphicsfeature")) return TEXT("MenuSettingTarget_GraphicsFeature");
		if (Key == TEXT("language") || Key == TEXT("culture")) return TEXT("MenuSettingTarget_Language");
		if (Key == TEXT("systeminfo") || Key == TEXT("info")) return TEXT("MenuSettingTarget_SystemInfo");
		if (Key == TEXT("saved") || Key == TEXT("none") || Key == TEXT("stored")) return TEXT("MenuSettingTarget_Saved");
		return FString();
	}

	/** "is_true" -> "IsTrue", "==" -> "Equals" (the enum takes names loosely; symbols need mapping). */
	FString CompareName(const FString& Compare)
	{
		const FString Key = Compare.TrimStartAndEnd();
		if (Key == TEXT("==") || Key == TEXT("=")) return TEXT("Equals");
		if (Key == TEXT("!=")) return TEXT("NotEquals");
		if (Key == TEXT(">")) return TEXT("Greater");
		if (Key == TEXT(">=")) return TEXT("GreaterOrEqual");
		if (Key == TEXT("<")) return TEXT("Less");
		if (Key == TEXT("<=")) return TEXT("LessOrEqual");
		return Key.Replace(TEXT("_"), TEXT(""));
	}

	TArray<TSharedPtr<FJsonValue>> MakeConditions(const TArray<TSharedPtr<FJsonValue>>& In)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const TSharedPtr<FJsonValue>& Value : In)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(Object))
			{
				continue;
			}
			TSharedPtr<FJsonObject> Condition = MakeShared<FJsonObject>();
			Condition->SetStringField(TEXT("SettingId"), GetString(*Object, TEXT("setting"), GetString(*Object, TEXT("setting_id"))));
			Condition->SetStringField(TEXT("Compare"), CompareName(GetString(*Object, TEXT("compare"), TEXT("IsTrue"))));
			Condition->SetStringField(TEXT("Value"), GetString(*Object, TEXT("value")));
			Out.Add(MakeShared<FJsonValueObject>(Condition));
		}
		return Out;
	}

	/** One setting from a spec (see add_menu_settings). Null with OutError. */
	UMenuSetting* MakeSetting(UObject* Outer, const TSharedPtr<FJsonObject>& Spec, TArray<FString>& OutWarnings, FString& OutError)
	{
		UClass* Class = SettingClass(GetString(Spec, TEXT("type"), TEXT("bool")));
		if (!Class)
		{
			OutError = FString::Printf(TEXT("Unknown setting type '%s' (bool, number, choice, action, info, key_binding)."), *GetString(Spec, TEXT("type")));
			return nullptr;
		}
		const FString Id = GetString(Spec, TEXT("id"));
		if (Id.IsEmpty())
		{
			OutError = TEXT("Every setting needs an id (\"Video.RenderDistance\").");
			return nullptr;
		}
		UMenuSetting* Setting = NewObject<UMenuSetting>(Outer, Class, NAME_None, RF_Transactional);

		TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
		Props->SetStringField(TEXT("Id"), Id);
		Props->SetStringField(TEXT("DisplayName"), GetString(Spec, TEXT("name"), Id));
		if (Has(Spec, TEXT("description"))) Props->SetStringField(TEXT("Description"), GetString(Spec, TEXT("description")));
		if (Has(Spec, TEXT("scope"))) Props->SetStringField(TEXT("Scope"), GetString(Spec, TEXT("scope")));
		if (Has(Spec, TEXT("apply"))) Props->SetStringField(TEXT("ApplyMode"), GetString(Spec, TEXT("apply")).Replace(TEXT("_"), TEXT("")));
		if (Has(Spec, TEXT("requires_restart"))) Props->SetBoolField(TEXT("bRequiresRestart"), GetBool(Spec, TEXT("requires_restart")));
		if (Has(Spec, TEXT("requires_confirmation"))) Props->SetBoolField(TEXT("bRequiresConfirmation"), GetBool(Spec, TEXT("requires_confirmation")));
		if (Has(Spec, TEXT("keywords"))) Props->SetField(TEXT("Keywords"), Arr(GetStringArray(Spec, TEXT("keywords"))));
		if (Has(Spec, TEXT("platforms"))) Props->SetField(TEXT("Platforms"), Arr(GetStringArray(Spec, TEXT("platforms"))));
		if (Has(Spec, TEXT("disabled_reason"))) Props->SetStringField(TEXT("DisabledReason"), GetString(Spec, TEXT("disabled_reason")));
		if (Has(Spec, TEXT("reapply_when_changed"))) Props->SetField(TEXT("ReapplyWhenChanged"), Arr(GetStringArray(Spec, TEXT("reapply_when_changed"))));
		if (Has(Spec, TEXT("shown_when"))) Props->SetArrayField(TEXT("ShownWhen"), MakeConditions(GetArray(Spec, TEXT("shown_when"))));
		if (Has(Spec, TEXT("enabled_when"))) Props->SetArrayField(TEXT("EnabledWhen"), MakeConditions(GetArray(Spec, TEXT("enabled_when"))));

		if (Class == UMenuSettingBool::StaticClass())
		{
			if (Has(Spec, TEXT("default"))) Props->SetBoolField(TEXT("bDefaultValue"), GetBool(Spec, TEXT("default")));
		}
		else if (Class == UMenuSettingNumber::StaticClass())
		{
			if (Has(Spec, TEXT("default"))) Props->SetNumberField(TEXT("DefaultValue"), GetNumber(Spec, TEXT("default")));
			if (Has(Spec, TEXT("min"))) Props->SetNumberField(TEXT("MinValue"), GetNumber(Spec, TEXT("min")));
			if (Has(Spec, TEXT("max"))) Props->SetNumberField(TEXT("MaxValue"), GetNumber(Spec, TEXT("max")));
			if (Has(Spec, TEXT("step"))) Props->SetNumberField(TEXT("Step"), GetNumber(Spec, TEXT("step")));
			if (Has(Spec, TEXT("decimals"))) Props->SetNumberField(TEXT("Decimals"), GetNumber(Spec, TEXT("decimals")));
			if (Has(Spec, TEXT("format"))) Props->SetStringField(TEXT("Format"), GetString(Spec, TEXT("format")).Replace(TEXT("_"), TEXT("")));
			if (Has(Spec, TEXT("units"))) Props->SetStringField(TEXT("Units"), GetString(Spec, TEXT("units")));
			if (Has(Spec, TEXT("min_text"))) Props->SetStringField(TEXT("MinText"), GetString(Spec, TEXT("min_text")));
			if (Has(Spec, TEXT("max_text"))) Props->SetStringField(TEXT("MaxText"), GetString(Spec, TEXT("max_text")));
		}
		else if (Class == UMenuSettingChoice::StaticClass())
		{
			TArray<TSharedPtr<FJsonValue>> Options;
			for (const TSharedPtr<FJsonValue>& Value : GetArray(Spec, TEXT("options")))
			{
				TSharedPtr<FJsonObject> Option = MakeShared<FJsonObject>();
				const TSharedPtr<FJsonObject>* AsObject = nullptr;
				FString AsString;
				if (Value.IsValid() && Value->TryGetObject(AsObject))
				{
					const FString Label = GetString(*AsObject, TEXT("label"));
					Option->SetStringField(TEXT("Value"), GetString(*AsObject, TEXT("value"), Label));
					Option->SetStringField(TEXT("Label"), Label);
					Option->SetStringField(TEXT("Description"), GetString(*AsObject, TEXT("description")));
				}
				else if (Value.IsValid() && Value->TryGetString(AsString))
				{
					Option->SetStringField(TEXT("Value"), AsString);
					Option->SetStringField(TEXT("Label"), AsString);
				}
				else
				{
					continue;
				}
				Options.Add(MakeShared<FJsonValueObject>(Option));
			}
			if (Options.Num() > 0) Props->SetArrayField(TEXT("Options"), Options);
			if (Has(Spec, TEXT("default"))) Props->SetStringField(TEXT("DefaultValue"), GetString(Spec, TEXT("default")));
			if (Has(Spec, TEXT("display"))) Props->SetStringField(TEXT("Display"), GetString(Spec, TEXT("display")));
			if (Has(Spec, TEXT("wrap"))) Props->SetBoolField(TEXT("bWrap"), GetBool(Spec, TEXT("wrap")));
		}
		else if (Class == UMenuSettingAction::StaticClass())
		{
			if (Has(Spec, TEXT("action"))) Props->SetStringField(TEXT("ActionKind"), GetString(Spec, TEXT("action")).Replace(TEXT("_"), TEXT("")));
			if (Has(Spec, TEXT("button"))) Props->SetStringField(TEXT("ButtonText"), GetString(Spec, TEXT("button")));
			if (Has(Spec, TEXT("confirm"))) Props->SetStringField(TEXT("ConfirmText"), GetString(Spec, TEXT("confirm")));
		}
		else if (Class == UMenuSettingInfo::StaticClass())
		{
			if (Has(Spec, TEXT("text"))) Props->SetStringField(TEXT("Text"), GetString(Spec, TEXT("text")));
		}
		else if (Class == UMenuSettingKeyBinding::StaticClass())
		{
			Props->SetStringField(TEXT("MappingName"), GetString(Spec, TEXT("mapping"), GetString(Spec, TEXT("mapping_name"))));
			if (Has(Spec, TEXT("device"))) Props->SetStringField(TEXT("Device"), GetString(Spec, TEXT("device")).Replace(TEXT("_"), TEXT("")));
		}

		// Where the value goes: {"type": "cvar", "name": "r.ViewDistanceScale"} - the other keys are the target's fields.
		if (const TSharedPtr<FJsonObject> Target = GetObject(Spec, TEXT("target")))
		{
			FString ClassName = TargetClassName(GetString(Target, TEXT("type"), TEXT("saved")));
			if (ClassName.IsEmpty())
			{
				ClassName = GetString(Target, TEXT("class"));
			}
			if (ClassName.IsEmpty())
			{
				OutWarnings.Add(FString::Printf(TEXT("%s: unknown target type '%s' - left as a saved value."), *Id, *GetString(Target, TEXT("type"))));
			}
			else
			{
				TSharedPtr<FJsonObject> TargetProps = MakeShared<FJsonObject>();
				TargetProps->SetStringField(TEXT("$class"), ClassName);
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Target->Values)
				{
					if (Pair.Key != TEXT("type") && Pair.Key != TEXT("class"))
					{
						TargetProps->SetField(Pair.Key, Pair.Value);
					}
				}
				Props->SetObjectField(TEXT("Target"), TargetProps);
			}
		}
		if (const TSharedPtr<FJsonObject> Extra = GetObject(Spec, TEXT("props")))
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Extra->Values)
			{
				Props->SetField(Pair.Key, Pair.Value);
			}
		}

		TArray<FString> Errors;
		ApplyToObject(Setting, Props, Errors);
		for (const FString& Error : Errors)
		{
			OutWarnings.Add(FString::Printf(TEXT("%s: %s"), *Id, *Error));
		}
		return Setting;
	}

	/** Puts Setting into Section, replacing one with the same Id anywhere in the schema. */
	void PlaceSetting(UMenuSettingsSchema* Schema, FMenuSettingsSection& Section, UMenuSetting* Setting)
	{
		for (FMenuSettingsPage& Page : Schema->Pages)
		{
			for (FMenuSettingsSection& Each : Page.Sections)
			{
				for (TObjectPtr<UMenuSetting>& Existing : Each.Settings)
				{
					if (Existing && Existing->Id == Setting->Id)
					{
						Existing = Setting;
						return;
					}
				}
			}
		}
		Section.Settings.Add(Setting);
	}

	void RemoveRoot(UWidgetBlueprint* Blueprint)
	{
		UWidgetTree* Tree = Blueprint->WidgetTree;
		UWidget* Root = Tree ? Tree->RootWidget.Get() : nullptr;
		if (!Root)
		{
			return;
		}
		NameTBDAIHelpers::DropGuids(Blueprint, Root);
		Tree->RootWidget = nullptr;
		TArray<UWidget*> Removed;
		UWidgetTree::ForWidgetAndChildren(Root, [&Removed](UWidget* Each) { Removed.Add(Each); });
		for (UWidget* Each : Removed)
		{
			Each->Modify();
			Each->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors);
		}
	}

	/** The pages there are now: built-in (if on), the project's schemas, and Extra. */
	TSharedPtr<FJsonObject> DescribePages(UMenuSettingsSchema* Extra)
	{
		UMenuSettingsSchema* BuiltIn = MenuSettingsDefaults::MakeBuiltInSchema(GetTransientPackage());
		TArray<UMenuSettingsSchema*> ExtraSchemas;
		if (Extra)
		{
			ExtraSchemas.Add(Extra);
		}
		TArray<const UMenuSettingsSchema*> Schemas;
		MenuSettingsSchemaUtils::GatherSchemas(BuiltIn, ExtraSchemas, Schemas);
		TArray<FMenuSettingsPage> Pages;
		MenuSettingsSchemaUtils::Merge(Schemas, MenuSettingsSchemaUtils::MakeProjectRules(), Pages);

		TArray<TSharedPtr<FJsonValue>> PageList;
		for (const FMenuSettingsPage& Page : Pages)
		{
			TSharedPtr<FJsonObject> PageObject = MakeShared<FJsonObject>();
			PageObject->SetStringField(TEXT("id"), Page.Id.ToString());
			PageObject->SetStringField(TEXT("name"), Page.DisplayName.ToString());
			TArray<TSharedPtr<FJsonValue>> SectionList;
			for (const FMenuSettingsSection& Section : Page.Sections)
			{
				TSharedPtr<FJsonObject> SectionObject = MakeShared<FJsonObject>();
				SectionObject->SetStringField(TEXT("title"), Section.Title.ToString());
				if (Section.bKeyBindings)
				{
					SectionObject->SetStringField(TEXT("key_bindings"), Section.KeyBindingDevice == EMenuKeyBindingDevice::Gamepad ? TEXT("gamepad") : TEXT("keyboard"));
				}
				TArray<FString> Ids;
				for (const UMenuSetting* Setting : Section.Settings)
				{
					if (Setting)
					{
						Ids.Add(FString::Printf(TEXT("%s (%s)"), *Setting->Id.ToString(), *StaticEnum<EMenuSettingKind>()->GetNameStringByValue(static_cast<int64>(Setting->GetKind()))));
					}
				}
				SectionObject->SetField(TEXT("settings"), Arr(Ids));
				SectionList.Add(MakeShared<FJsonValueObject>(SectionObject));
			}
			PageObject->SetArrayField(TEXT("sections"), SectionList);
			PageList.Add(MakeShared<FJsonValueObject>(PageObject));
		}
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetArrayField(TEXT("pages"), PageList);
		return Out;
	}

	/** One skin image spec ("<path>", or {image, material_params}) as FWidgetEffectImage JSON: an SVG asset or a texture / material brush. */
	TSharedPtr<FJsonObject> MakeSkinImage(const TSharedPtr<FJsonValue>& Value, TArray<FString>& OutWarnings)
	{
		FString Path;
		TSharedPtr<FJsonObject> Extra;
		if (Value.IsValid() && Value->Type == EJson::String)
		{
			Path = Value->AsString();
		}
		else if (Value.IsValid() && Value->Type == EJson::Object)
		{
			Extra = Value->AsObject();
			Path = GetString(Extra, TEXT("image"));
		}
		if (Path.IsEmpty())
		{
			return nullptr;
		}
		FString Error;
		UObject* Asset = NameTBDAIHelpers::FindAsset(Path, nullptr, Error);
		if (!Asset)
		{
			OutWarnings.Add(Error);
			return nullptr;
		}
		TSharedPtr<FJsonObject> Image = MakeShared<FJsonObject>();
		if (Asset->IsA<UWidgetSVG>())
		{
			Image->SetStringField(TEXT("ImageSource"), TEXT("SVG"));
			Image->SetStringField(TEXT("SVG"), Asset->GetPathName());
		}
		else
		{
			Image->SetStringField(TEXT("ImageSource"), TEXT("Texture"));
			Image->SetStringField(TEXT("Image"), Asset->GetPathName());
		}
		if (Extra.IsValid() && Extra->HasField(TEXT("material_params")))
		{
			TArray<TSharedPtr<FJsonValue>> Parameters;
			for (const auto& Pair : GetObject(Extra, TEXT("material_params"))->Values)
			{
				TSharedPtr<FJsonObject> Parameter = MakeShared<FJsonObject>();
				Parameter->SetStringField(TEXT("Name"), Pair.Key);
				if (Pair.Value->Type == EJson::Number)
				{
					Parameter->SetStringField(TEXT("Type"), TEXT("Scalar"));
					Parameter->SetNumberField(TEXT("Scalar"), Pair.Value->AsNumber());
				}
				else
				{
					Parameter->SetStringField(TEXT("Type"), TEXT("Color"));
					Parameter->SetField(TEXT("Color"), Pair.Value);
				}
				Parameters.Add(MakeShared<FJsonValueObject>(Parameter));
			}
			Image->SetArrayField(TEXT("MaterialParameters"), Parameters);
		}
		return Image;
	}

	/**
	 * The skin argument as FMenuStyleSkin JSON: {part: "<path>" | {normal, hovered, focused, pressed, selected, disabled (each a path or
	 * {image, material_params}), tint_with_palette, tint, outset, keep_built_in}}.
	 */
	TSharedPtr<FJsonObject> MakeSkin(const TSharedPtr<FJsonObject>& Skin, TArray<FString>& OutWarnings)
	{
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		if (!Skin.IsValid())
		{
			return Out;
		}
		for (const auto& Pair : Skin->Values)
		{
			const FStructProperty* PartProperty = nullptr;
			for (TFieldIterator<FStructProperty> It(FMenuStyleSkin::StaticStruct()); It; ++It)
			{
				if (Normalize(It->GetName()) == Normalize(Pair.Key))
				{
					PartProperty = *It;
				}
			}
			if (!PartProperty)
			{
				TArray<FString> Names;
				for (TFieldIterator<FStructProperty> It(FMenuStyleSkin::StaticStruct()); It; ++It)
				{
					Names.Add(It->GetName());
				}
				OutWarnings.Add(FString::Printf(TEXT("No skin part '%s' (parts: %s)."), *Pair.Key, *FString::Join(Names, TEXT(", "))));
				continue;
			}
			TSharedPtr<FJsonObject> Part = MakeShared<FJsonObject>();
			if (Pair.Value->Type == EJson::String)
			{
				if (TSharedPtr<FJsonObject> Image = MakeSkinImage(Pair.Value, OutWarnings))
				{
					Part->SetObjectField(TEXT("Normal"), Image);
				}
			}
			else if (Pair.Value->Type == EJson::Object)
			{
				const TSharedPtr<FJsonObject> Spec = Pair.Value->AsObject();
				struct FStateKey { const TCHAR* Key; const TCHAR* Field; const TCHAR* Toggle; };
				const FStateKey States[] = {
					{ TEXT("normal"), TEXT("Normal"), nullptr }, { TEXT("hovered"), TEXT("Hovered"), TEXT("bHoveredImage") },
					{ TEXT("focused"), TEXT("Focused"), TEXT("bFocusedImage") }, { TEXT("pressed"), TEXT("Pressed"), TEXT("bPressedImage") },
					{ TEXT("selected"), TEXT("Selected"), TEXT("bSelectedImage") }, { TEXT("disabled"), TEXT("Disabled"), TEXT("bDisabledImage") } };
				for (const FStateKey& State : States)
				{
					if (TSharedPtr<FJsonObject> Image = MakeSkinImage(Spec->TryGetField(State.Key), OutWarnings))
					{
						Part->SetObjectField(State.Field, Image);
						if (State.Toggle)
						{
							Part->SetBoolField(State.Toggle, true);
						}
					}
				}
				if (Spec->HasField(TEXT("tint_with_palette")))
				{
					Part->SetBoolField(TEXT("bTintWithPalette"), GetBool(Spec, TEXT("tint_with_palette")));
				}
				if (Spec->HasField(TEXT("keep_built_in")))
				{
					Part->SetBoolField(TEXT("bKeepBuiltIn"), GetBool(Spec, TEXT("keep_built_in")));
				}
				if (Spec->HasField(TEXT("tint")))
				{
					Part->SetField(TEXT("Tint"), Spec->TryGetField(TEXT("tint")));
				}
				if (Spec->HasField(TEXT("outset")))
				{
					Part->SetField(TEXT("Outset"), Spec->TryGetField(TEXT("outset")));
				}
			}
			Out->SetObjectField(PartProperty->GetName(), Part);
		}
		return Out;
	}

	FString StylePresetName(const FString& Preset)
	{
		const FString Key = Normalize(Preset);
		if (Key.IsEmpty() || Key == TEXT("simple") || Key == TEXT("dark") || Key == TEXT("flat")) return TEXT("Simple");
		if (Key == TEXT("light")) return TEXT("Light");
		if (Key == TEXT("glass") || Key == TEXT("frosted")) return TEXT("Glass");
		if (Key == TEXT("minimal") || Key == TEXT("cinematic")) return TEXT("Minimal");
		if (Key == TEXT("console") || Key == TEXT("tv") || Key == TEXT("10foot")) return TEXT("Console");
		if (Key == TEXT("highcontrast") || Key == TEXT("contrast")) return TEXT("HighContrast");
		return TEXT("Simple");
	}
}

namespace NameTBDAITools
{
	void RegisterMenuTools()
	{
		using namespace NameTBDAIMenuTools;

		// ---- create_settings_menu ----
		Register({
			TEXT("create_settings_menu"),
			TEXT("Makes a complete, working settings menu in one call: a Widget Blueprint (parent Menu Settings Screen) holding a Settings Menu with the tabs asked for, bound to a Menu Style data asset (made here) so its whole look is edited in one place. Built-in tabs: Gameplay, Video (display, DLSS/FSR/XeSS upscaling, frame generation, quality, effects, performance overlay, benchmark), Audio, Controls (sensitivity, invert, vibration, button prompts, key rebinding), Accessibility (colour blind modes, UI scale, subtitles, reduce motion, narration). Any other tab name becomes a custom page in a Settings Schema made here (add_menu_settings fills it). Restyle later with set_asset on the style (Style.Colors.Accent, Style.TitleFont.Size, Style.RowHeight...)."),
			FNameTBDAISchema()
				.Str(TEXT("name"), TEXT("Widget Blueprint name, e.g. SettingsMenu (WBP_ added)."), true)
				.Arr(TEXT("tabs"), TEXT("Tabs in order, e.g. [\"Controls\", \"Video\", \"Audio\", \"Accessibility\"]. Omit: every built-in page."), TEXT("string"))
				.Str(TEXT("folder"), TEXT("Content folder (default: the Output Folder)."))
				.Str(TEXT("title"), TEXT("Screen title (default \"Settings\")."))
				.Str(TEXT("style"), TEXT("Style preset: simple (dark, flat), light, glass, minimal, console (10-foot), high_contrast."))
				.Str(TEXT("style_asset"), TEXT("Use this existing Menu Style asset instead of making one."))
				.Obj(TEXT("colors"), TEXT("Palette overrides: background, surface, row, row_hovered, row_focused, accent, accent_text, text, muted_text, disabled_text, divider, track, focus_outline, warning, negative, scrim. \"#RRGGBB[AA]\" or theme tokens (\"@accent\")."))
				.Obj(TEXT("style_props"), TEXT("Other Style fields: {\"RowHeight\": 60, \"CornerRadius\": 12, \"TitleFont\": {\"Size\": 36}, \"TabStyle\": \"Pills\", \"BackgroundBlur\": 12, \"bUppercaseTabs\": true, \"TabsAlignment\": \"HAlign_Center\", \"SliderThumbSize\": 20}."))
				.Obj(TEXT("skin"), TEXT("Art per part (an art-directed, AAA look): {part: \"<texture, material or SVG path>\" | {normal, hovered, focused, pressed, selected, disabled (each a path or {image, material_params {Name: number|color}}), tint_with_palette, tint, outset {Left,Top,Right,Bottom}, keep_built_in}}. Parts: Row, FocusBar, ModifiedDot, SectionHeading, CategoryHeading, TabBar, Tab, TabIndicator, SwitchTrack, SwitchKnob, SliderTrack, SliderFill, SliderThumb, SelectorBox, SelectorArrow, Pip, Segment, Dropdown, DropdownArrow, KeySlot, PrimaryButton, SecondaryButton, GhostButton, FooterPrompt, SearchBox, DescriptionPanel, Footer, DialogCard, ListCard, ListOption, Scrim, ScrollThumb, ScrollTrack. A material gets Hover, Focus, Pressed, Selected, Disabled, Value, Time, Width, Height, PaletteColor, Accent each draw: one material can do every state (build_ui_material)."))
				.Obj(TEXT("widgets"), TEXT("Your own UMG Widget Blueprints for parts: {tab, heading, footer_prompt, description, search, dialog, option_list: \"<Widget Blueprint path>\"} (parents: MenuTabWidget, MenuHeadingWidget, MenuFooterPromptWidget, MenuDescriptionWidget, MenuSearchWidget, MenuDialogWidget, MenuOptionListWidget)."))
				.Str(TEXT("apply_policy"), TEXT("instant (changes apply as made; console style, default) or apply_button (PC style: Apply / unsaved changes prompt)."), false, { TEXT("instant"), TEXT("apply_button") })
				.Build(),
			true, false, false,
			[](const TSharedPtr<FJsonObject>& Args, FNameTBDAIToolContext& Context)
			{
				FString Error;
				TArray<FString> Warnings;
				const FString Folder = GetString(Args, TEXT("folder"));
				const FString BaseName = NameTBDAIHelpers::MakeAssetName(GetString(Args, TEXT("name"), TEXT("SettingsMenu")), FString());

				// Tabs: built-in pages by name, others become a schema's custom pages.
				TArray<FName> PageIds;
				TArray<FString> CustomTabs;
				for (const FString& Tab : GetStringArray(Args, TEXT("tabs")))
				{
					const FName BuiltIn = BuiltInPage(Tab);
					if (!BuiltIn.IsNone())
					{
						PageIds.AddUnique(BuiltIn);
					}
					else if (!Tab.TrimStartAndEnd().IsEmpty())
					{
						PageIds.AddUnique(MakeId(Tab));
						CustomTabs.Add(Tab.TrimStartAndEnd());
					}
				}

				UMenuSettingsSchema* Schema = nullptr;
				if (CustomTabs.Num() > 0)
				{
					Schema = FindOrCreateSchema(FString(), Folder, BaseName + TEXT("_Settings"), Context, Error);
					if (!Schema)
					{
						return FNameTBDAIToolResult::Fail(Error);
					}
					Schema->Modify();
					int32 Order = 600;
					for (const FString& Tab : CustomTabs)
					{
						FindOrAddPage(Schema, MakeId(Tab), Tab, Order);
						Order += 10;
					}
					Schema->PostEditChange();
					Context.Changes->NoteTouched(Schema, FString::Printf(TEXT("Added the %s page(s) to %s"), *FString::Join(CustomTabs, TEXT(", ")), *Schema->GetName()));
				}

				// The style asset the menu is bound to.
				UMenuStyleAsset* StyleAsset = nullptr;
				const FString ExistingStyle = GetString(Args, TEXT("style_asset"));
				if (!ExistingStyle.IsEmpty())
				{
					StyleAsset = Cast<UMenuStyleAsset>(NameTBDAIHelpers::FindAsset(ExistingStyle, UMenuStyleAsset::StaticClass(), Error));
					if (!StyleAsset)
					{
						return FNameTBDAIToolResult::Fail(Error);
					}
				}
				else
				{
					UDataAssetFactory* Factory = NewObject<UDataAssetFactory>();
					Factory->DataAssetClass = UMenuStyleAsset::StaticClass();
					StyleAsset = Cast<UMenuStyleAsset>(NameTBDAIHelpers::CreateAsset(Folder, NameTBDAIHelpers::MakeAssetName(BaseName + TEXT("_Style"), TEXT("DA_")),
						UMenuStyleAsset::StaticClass(), Factory, Context, Error));
					if (!StyleAsset)
					{
						return FNameTBDAIToolResult::Fail(Error);
					}
					StyleAsset->SetFlags(RF_Transactional);
					const UEnum* PresetEnum = StaticEnum<EMenuStylePreset>();
					const int64 PresetValue = PresetEnum->GetValueByNameString(StylePresetName(GetString(Args, TEXT("style"))));
					StyleAsset->ApplyPreset(PresetValue != INDEX_NONE ? static_cast<EMenuStylePreset>(PresetValue) : EMenuStylePreset::Simple);

					TSharedPtr<FJsonObject> StyleProps = GetObject(Args, TEXT("style_props"));
					if (!StyleProps.IsValid())
					{
						StyleProps = MakeShared<FJsonObject>();
					}
					if (const TSharedPtr<FJsonObject> Colors = GetObject(Args, TEXT("colors")))
					{
						StyleProps->SetObjectField(TEXT("Colors"), Colors);
					}
					if (const TSharedPtr<FJsonObject> Skin = GetObject(Args, TEXT("skin")))
					{
						StyleProps->SetObjectField(TEXT("Skin"), MakeSkin(Skin, Warnings));
					}
					if (const TSharedPtr<FJsonObject> PartWidgets = GetObject(Args, TEXT("widgets")))
					{
						TSharedPtr<FJsonObject> WidgetsJson = MakeShared<FJsonObject>();
						const TPair<const TCHAR*, const TCHAR*> Keys[] = { { TEXT("tab"), TEXT("Tab") }, { TEXT("heading"), TEXT("Heading") },
							{ TEXT("footer_prompt"), TEXT("FooterPrompt") }, { TEXT("description"), TEXT("Description") }, { TEXT("search"), TEXT("Search") },
							{ TEXT("dialog"), TEXT("Dialog") }, { TEXT("option_list"), TEXT("OptionList") } };
						for (const TPair<const TCHAR*, const TCHAR*>& Key : Keys)
						{
							FString Path = GetString(PartWidgets, Key.Key);
							if (Path.IsEmpty())
							{
								continue;
							}
							// A Widget Blueprint's generated class.
							FString FindError;
							if (UWidgetBlueprint* PartBlueprint = Cast<UWidgetBlueprint>(NameTBDAIHelpers::FindAsset(Path, UWidgetBlueprint::StaticClass(), FindError)))
							{
								Path = PartBlueprint->GeneratedClass ? PartBlueprint->GeneratedClass->GetPathName() : Path;
							}
							WidgetsJson->SetStringField(Key.Value, Path);
						}
						StyleProps->SetObjectField(TEXT("Widgets"), WidgetsJson);
					}
					if (StyleProps->Values.Num() > 0)
					{
						TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
						Props->SetObjectField(TEXT("Style"), StyleProps);
						NameTBDAIHelpers::ExpandThemeTokens(Props, NameTBDAIHelpers::IsDark(FString()));
						TArray<FString> Errors;
						ApplyToObject(StyleAsset, Props, Errors);
						Warnings.Append(Errors);
						// Edited values: the preset is now the project's own look.
						StyleAsset->Preset = EMenuStylePreset::Custom;
					}
				}

				// The Widget Blueprint: a Menu Settings Screen with the Settings Menu filling it.
				UWidgetBlueprintFactory* Factory = NewObject<UWidgetBlueprintFactory>();
				Factory->ParentClass = UMenuSettingsScreen::StaticClass();
				Factory->BlueprintType = BPTYPE_Normal;
				UWidgetBlueprint* Blueprint = Cast<UWidgetBlueprint>(NameTBDAIHelpers::CreateAsset(Folder, NameTBDAIHelpers::MakeAssetName(BaseName, TEXT("WBP_")),
					UWidgetBlueprint::StaticClass(), Factory, Context, Error));
				if (!Blueprint)
				{
					return FNameTBDAIToolResult::Fail(Error);
				}
				Blueprint->Modify();
				UWidgetTree* Tree = Blueprint->WidgetTree;
				Tree->SetFlags(RF_Transactional);
				Tree->Modify();
				RemoveRoot(Blueprint);

				UOverlay* Root = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("Root"));
				Tree->RootWidget = Root;
				USettingsMenu* Menu = Tree->ConstructWidget<USettingsMenu>(USettingsMenu::StaticClass(), TEXT("SettingsMenu"));
				Menu->bIsVariable = true;
				if (UOverlaySlot* MenuSlot = Root->AddChildToOverlay(Menu))
				{
					MenuSlot->SetHorizontalAlignment(HAlign_Fill);
					MenuSlot->SetVerticalAlignment(VAlign_Fill);
				}
				Menu->StyleAsset = StyleAsset;
				Menu->Pages = PageIds;
				Menu->Title = FText::FromString(GetString(Args, TEXT("title"), TEXT("Settings")));
				Menu->ApplyPolicy = Normalize(GetString(Args, TEXT("apply_policy"))) == TEXT("applybutton") ? EMenuApplyPolicy::ApplyButton : EMenuApplyPolicy::Instant;
				if (Schema)
				{
					Menu->ExtraSchemas.Add(Schema);
				}

				NameTBDAIHelpers::GiveGuids(Blueprint, Root);
				FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
				TArray<FString> Problems;
				const bool bCompiled = NameTBDAIHelpers::Compile(Blueprint, Problems);
				Warnings.Append(Problems);
				NameTBDAITools::SetLastAsset(Blueprint);
				Context.Changes->NoteTouched(Blueprint, FString::Printf(TEXT("Built the settings menu %s"), *Blueprint->GetName()));

				FNameTBDAIToolResult Result = FNameTBDAIToolResult::Ok();
				Result.Data->SetStringField(TEXT("blueprint"), Blueprint->GetPathName());
				Result.Data->SetBoolField(TEXT("compiled"), bCompiled);
				Result.Data->SetStringField(TEXT("style_asset"), StyleAsset->GetPathName());
				// What the style asset says now (for a check against what was asked).
				Result.Data->SetStringField(TEXT("style_preset"), StaticEnum<EMenuStylePreset>()->GetNameStringByValue(static_cast<int64>(StyleAsset->Preset)));
				if (Schema)
				{
					Result.Data->SetStringField(TEXT("schema"), Schema->GetPathName());
				}
				TArray<FString> PageNames;
				for (const FName& Id : PageIds)
				{
					PageNames.Add(Id.ToString());
				}
				Result.Data->SetField(TEXT("pages"), Arr(PageNames));
				Result.Data->SetStringField(TEXT("next"), TEXT("Open it from the Menu Manager (Screens row: Widget Class = this Blueprint, Input Mode UI Only, Focus On Open Screen) or Create Widget + Add to Viewport. Custom pages: add_menu_settings with the schema. Restyle: set_asset on the style asset."));
				Result.Warnings = Warnings;
				return Result;
			}
		});

		// ---- add_menu_settings ----
		Register({
			TEXT("add_menu_settings"),
			TEXT("Adds settings to a Settings Schema (made if needed) on a page and section - new pages, or the built-in ones (Video, Audio, Controls, Accessibility, Gameplay); a setting with an existing Id replaces it. Each setting: {id: \"Video.RenderDistance\", type: bool|number|choice|action|info|key_binding, name, description, default, (number) min, max, step, format: number|percent|percent_value|multiplier, units, min_text, (choice) options: [\"Low\", ...] or [{value, label, description}], display: auto|selector|dropdown|buttons, (action) action: event|auto_detect_graphics|reset_page|reset_all|reset_key_bindings|run_benchmark, button, confirm, target: {type: cvar (name, scale, invert, also_set) | game_user_setting (field: VSync, ShadowQuality, Resolution...) | built_in (field: DisplayGamma, UIScale, ColorBlindMode, Subtitles, Vibration, FieldOfView...) | sound (channel: Music...) | notification (field) | graphics (field: Upscaler, UpscalerMode, FrameGeneration, LatencyReduction, Sharpness, ResolutionScale) | language | saved (the game reads it)}, scope: user|machine, apply: default|immediately|on_apply, requires_restart, requires_confirmation, shown_when / enabled_when: [{setting, compare: is_true|is_false|equals|not_equals|greater|less|is_any_of, value}] (setting may be a system value: @ActiveUpscaler, @GPUVendor, @InputDevice, @Platform, @RayTracingSupported), keywords, props: {any other field}}. Pass menu to add the schema to a Settings Menu's Extra Schemas."),
			FNameTBDAISchema()
				.Str(TEXT("schema"), TEXT("Settings Schema path or name (made when not found)."))
				.Str(TEXT("folder"), TEXT("Content folder for a new schema."))
				.Str(TEXT("page"), TEXT("Page id or name (\"Video\", \"Online\")."), true)
				.Str(TEXT("section"), TEXT("Section title (\"Graphics\"; default General)."))
				.Arr(TEXT("settings"), TEXT("Setting specs (see the description)."), TEXT("object"), true)
				.Str(TEXT("menu"), TEXT("A settings menu Widget Blueprint to show this schema in (its SettingsMenu's Extra Schemas)."))
				.Build(),
			true, false, false,
			[](const TSharedPtr<FJsonObject>& Args, FNameTBDAIToolContext& Context)
			{
				FString Error;
				const FString PageArg = GetString(Args, TEXT("page"));
				const FName BuiltIn = BuiltInPage(PageArg);
				const FName PageId = BuiltIn.IsNone() ? MakeId(PageArg) : BuiltIn;
				UMenuSettingsSchema* Schema = FindOrCreateSchema(GetString(Args, TEXT("schema")), GetString(Args, TEXT("folder")), PageId.ToString() + TEXT("Settings"), Context, Error);
				if (!Schema)
				{
					return FNameTBDAIToolResult::Fail(Error);
				}
				Schema->Modify();
				FMenuSettingsPage& Page = FindOrAddPage(Schema, PageId, PageArg, 600);
				FMenuSettingsSection& Section = FindOrAddSection(Page, GetString(Args, TEXT("section")));

				TArray<FString> Warnings;
				TArray<FString> Added;
				for (const TSharedPtr<FJsonValue>& Value : GetArray(Args, TEXT("settings")))
				{
					const TSharedPtr<FJsonObject>* Spec = nullptr;
					if (!Value.IsValid() || !Value->TryGetObject(Spec))
					{
						Warnings.Add(TEXT("Each setting is an object spec."));
						continue;
					}
					FString SettingError;
					if (UMenuSetting* Setting = MakeSetting(Schema, *Spec, Warnings, SettingError))
					{
						PlaceSetting(Schema, Section, Setting);
						Added.Add(Setting->Id.ToString());
					}
					else
					{
						Warnings.Add(SettingError);
					}
				}
				if (Added.Num() == 0)
				{
					return FNameTBDAIToolResult::Fail(FString::Join(Warnings, TEXT(" ")));
				}
				Schema->PostEditChange();
				Context.Changes->NoteTouched(Schema, FString::Printf(TEXT("Added %s to %s"), *FString::Join(Added, TEXT(", ")), *Schema->GetName()));
				NameTBDAITools::SetLastAsset(Schema);

				// Shown in a menu: its Settings Menu lists this schema.
				const FString MenuName = GetString(Args, TEXT("menu"));
				if (!MenuName.IsEmpty())
				{
					UWidgetBlueprint* Blueprint = NameTBDAIHelpers::FindWidgetBlueprint(MenuName, Error);
					USettingsMenu* Menu = nullptr;
					if (Blueprint && Blueprint->WidgetTree)
					{
						Blueprint->WidgetTree->ForEachWidget([&Menu](UWidget* Widget)
						{
							if (!Menu)
							{
								Menu = Cast<USettingsMenu>(Widget);
							}
						});
					}
					if (Menu)
					{
						Blueprint->Modify();
						Menu->Modify();
						Menu->ExtraSchemas.AddUnique(Schema);
						if (!BuiltIn.IsNone() || Menu->Pages.Num() == 0 || Menu->Pages.Contains(PageId))
						{
							// Already shown (a built-in page, or every page).
						}
						else
						{
							Menu->Pages.Add(PageId);
						}
						FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
						TArray<FString> Problems;
						NameTBDAIHelpers::Compile(Blueprint, Problems);
						Warnings.Append(Problems);
						Context.Changes->NoteTouched(Blueprint, FString::Printf(TEXT("%s shows %s"), *Blueprint->GetName(), *Schema->GetName()));
					}
					else
					{
						Warnings.Add(FString::Printf(TEXT("No Settings Menu found in '%s' (%s) - add the schema to Project Settings > Plugins > Menus > Schemas instead."), *MenuName, *Error));
					}
				}

				FNameTBDAIToolResult Result = FNameTBDAIToolResult::Ok();
				Result.Data->SetStringField(TEXT("schema"), Schema->GetPathName());
				Result.Data->SetStringField(TEXT("page"), PageId.ToString());
				Result.Data->SetField(TEXT("added"), Arr(Added));
				if (MenuName.IsEmpty())
				{
					Result.Data->SetStringField(TEXT("note"), TEXT("Shown where this schema is listed: a Settings Menu's Extra Schemas (pass menu) or Project Settings > Plugins > Menus > Schemas (every menu)."));
				}
				Result.Warnings = Warnings;
				return Result;
			}
		});

		// ---- list_menu_settings ----
		Register({
			TEXT("list_menu_settings"),
			TEXT("Lists the settings pages, sections and setting ids there are (built-in and the project's, plus a schema of yours), the graphics features (DLSS, FSR, XeSS...: installed here or not), and the style presets - for conditions (shown_when), replacing a built-in setting by id, or answering what a menu offers."),
			FNameTBDAISchema()
				.Str(TEXT("schema"), TEXT("Also merge this Settings Schema (path or name)."))
				.Build(),
			false, true, false,
			[](const TSharedPtr<FJsonObject>& Args, FNameTBDAIToolContext& Context)
			{
				FString Error;
				UMenuSettingsSchema* Extra = nullptr;
				const FString SchemaName = GetString(Args, TEXT("schema"));
				if (!SchemaName.IsEmpty())
				{
					Extra = Cast<UMenuSettingsSchema>(NameTBDAIHelpers::FindAsset(SchemaName, UMenuSettingsSchema::StaticClass(), Error));
				}
				FNameTBDAIToolResult Result = FNameTBDAIToolResult::Ok(DescribePages(Extra));
				TArray<FString> Features;
				for (const FMenuGraphicsFeatureStatus& Status : UMenuGraphicsLibrary::GetGraphicsFeatures())
				{
					Features.Add(FString::Printf(TEXT("%s (%s)%s"), *Status.Id.ToString(), *StaticEnum<EMenuGraphicsFeatureKind>()->GetNameStringByValue(static_cast<int64>(Status.Kind)),
						Status.bInstalled ? TEXT("") : TEXT(" not installed")));
				}
				Result.Data->SetField(TEXT("graphics_features"), Arr(Features));
				Result.Data->SetField(TEXT("style_presets"), Arr({ TEXT("simple"), TEXT("light"), TEXT("glass"), TEXT("minimal"), TEXT("console"), TEXT("high_contrast") }));
				if (!Error.IsEmpty())
				{
					Result.Warnings.Add(Error);
				}
				return Result;
			}
		});
	}
}
