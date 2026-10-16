// Copyright AR GameSystems 2026. All rights reserved.

// Widget tools: Widget Blueprints and their trees - made, filled, styled, laid out - and the ready-made buttons.

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelSlot.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/SizeBox.h"
#include "Components/Widget.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "NameTBD/Buttons/ContextMenuButton.h"
#include "NameTBD/Buttons/Widgets/StyledButtons.h"
#include "NameTBDAIDocs.h"
#include "NameTBDAIHelpers.h"
#include "NameTBDAIJson.h"
#include "NameTBDAITheme.h"
#include "NameTBDAITools.h"
#include "NameTBDAIWidgetStyling.h"
#include "NameTBD/Widgets/WidgetSVG.h"
#include "NameTBD/Widgets/WidgetSVGImage.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"

using namespace NameTBDAIJson;

namespace NameTBDAIWidgetTools
{
	FString MakeAssetNameLike(const FString& Text);

	/** Resources/AI/Roles.json: intent -> class, settings and slot, written with theme tokens. */
	TSharedPtr<FJsonObject> LoadRoles()
	{
		static TSharedPtr<FJsonObject> Cached;
		static FDateTime CachedStamp;
		const FString File = NameTBDAIDocs::GetResourcesDir() / TEXT("Roles.json");
		const FDateTime Stamp = IFileManager::Get().GetTimeStamp(*File);
		if (!Cached.IsValid() || Stamp != CachedStamp)
		{
			Cached = ParseObject(NameTBDAIDocs::LoadResourceText(TEXT("Roles.json")));
			CachedStamp = Stamp;
			if (!Cached.IsValid())
			{
				Cached = MakeShared<FJsonObject>();
			}
		}
		return Cached;
	}

	TSharedPtr<FJsonObject> FindRole(const FString& Role)
	{
		const TSharedPtr<FJsonObject> Roles = LoadRoles();
		const FString Key = Normalize(Role);
		for (const auto& Pair : Roles->Values)
		{
			if (Normalize(Pair.Key) == Key && Pair.Value->Type == EJson::Object)
			{
				return Pair.Value->AsObject();
			}
		}
		return nullptr;
	}

	/** A deep copy, so token expansion never touches the cached roles. */
	TSharedPtr<FJsonObject> Copy(const TSharedPtr<FJsonObject>& Source)
	{
		return Source.IsValid() ? ParseObject(ToString(Source)) : nullptr;
	}

	/**
	 * Which house role each widget was built with, per Widget Blueprint (Saved/NameTBDAI/roles.json) - so apply_theme can
	 * repaint them in the other palette without touching sizes or layout.
	 */
	FString GetRolesFile()
	{
		return FPaths::ProjectSavedDir() / TEXT("NameTBDAI") / TEXT("roles.json");
	}

	TSharedPtr<FJsonObject> LoadRememberedRoles()
	{
		FString Text;
		FFileHelper::LoadFileToString(Text, *GetRolesFile());
		TSharedPtr<FJsonObject> Roles = ParseObject(Text);
		return Roles.IsValid() ? Roles : MakeShared<FJsonObject>();
	}

	void RememberRole(const UWidgetBlueprint* Blueprint, const UWidget* Widget, const FString& Role)
	{
		TSharedPtr<FJsonObject> All = LoadRememberedRoles();
		TSharedPtr<FJsonObject> Own = GetObject(All, Blueprint->GetPathName());
		if (!Own.IsValid())
		{
			Own = MakeShared<FJsonObject>();
			All->SetObjectField(Blueprint->GetPathName(), Own);
		}
		Own->SetStringField(Widget->GetName(), Role);
		FFileHelper::SaveStringToFile(ToString(All, true), *GetRolesFile());
	}

	/** Keeps only the values that are theme colors ("@accent", "@text~0.5"), anywhere inside. Null when none are left. */
	TSharedPtr<FJsonValue> KeepColorTokens(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid())
		{
			return nullptr;
		}
		if (Value->Type == EJson::String)
		{
			FString Token = Value->AsString();
			if (!Token.StartsWith(TEXT("@")))
			{
				return nullptr;
			}
			Token = Token.Mid(1);
			int32 Tilde = INDEX_NONE;
			if (Token.FindChar(TEXT('~'), Tilde))
			{
				Token = Token.Left(Tilde);
			}
			FLinearColor Ignored;
			return NameTBDAIHelpers::GetThemeColor(Token, true, Ignored) ? Value : nullptr;
		}
		if (Value->Type == EJson::Object)
		{
			TSharedPtr<FJsonObject> Kept = MakeShared<FJsonObject>();
			for (const auto& Pair : Value->AsObject()->Values)
			{
				if (TSharedPtr<FJsonValue> Inner = KeepColorTokens(Pair.Value))
				{
					Kept->SetField(Pair.Key, Inner);
				}
			}
			return Kept->Values.Num() > 0 ? TSharedPtr<FJsonValue>(MakeShared<FJsonValueObject>(Kept)) : nullptr;
		}
		if (Value->Type == EJson::Array)
		{
			// Lists (gradient stops) are kept whole when they hold any color token: their entries go together.
			for (const TSharedPtr<FJsonValue>& Item : Value->AsArray())
			{
				if (KeepColorTokens(Item).IsValid())
				{
					return Value;
				}
			}
		}
		return nullptr;
	}

	/** Takes Widget and everything inside it out of the Blueprint's tree, as the designer's Delete does. */
	void RemoveWidgetFromTree(UWidgetBlueprint* Blueprint, UWidget* Widget)
	{
		UWidgetTree* Tree = Blueprint->WidgetTree;
		NameTBDAIHelpers::DropGuids(Blueprint, Widget);
		if (Tree->RootWidget == Widget)
		{
			Tree->RootWidget = nullptr;
		}
		else if (UPanelWidget* Parent = Widget->GetParent())
		{
			Parent->Modify();
			Parent->RemoveChild(Widget);
		}
		TArray<UWidget*> Removed;
		UWidgetTree::ForWidgetAndChildren(Widget, [&Removed](UWidget* Each) { Removed.Add(Each); });
		for (UWidget* Each : Removed)
		{
			Each->Modify();
			// Out of the tree: the compiler wants a GUID for every widget the tree owns, listed or not.
			Each->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors);
		}
	}

	struct FBuildContext
	{
		UWidgetBlueprint* Blueprint = nullptr;
		bool bDark = true;
		FNameTBDAIToolContext* Tool = nullptr;
		TArray<FString> Warnings;
		TArray<TSharedPtr<FJsonValue>> Made;
		/** Every widget made, for the contrast check at the end. */
		TArray<UWidget*> MadeWidgets;
	};

	/** "#RRGGBBAA" (sRGB, as the tools read colours) for a linear colour. */
	FString ToHex(const FLinearColor& Color)
	{
		return TEXT("#") + Color.ToFColor(true).ToHex();
	}

	/** What a Widget Blueprint remembers for a themed engine widget (its button variant), beside the house roles. */
	const TCHAR* EngineThemeMark = TEXT("$engine:");

	/** Checks the text and icons among Widgets against what is behind them; fixes go into Warnings so the model sees them. */
	void CheckContrast(const TArray<UWidget*>& Widgets, bool bDark, TArray<FString>& Warnings)
	{
		TArray<FString> Notes;
		NameTBDAIWidgetStyling::FixContrast(Widgets, bDark, Notes);
		for (const FString& Note : Notes)
		{
			Warnings.Add(TEXT("contrast fixed - ") + Note);
		}
	}

	UClass* ResolveReadyMadeKind(const FString& Kind)
	{
		const FString Key = Normalize(Kind);
		struct FKind { const TCHAR* Key; UClass* Value; };
		const FKind Kinds[] = {
			{ TEXT("generic"), UGenericButton::StaticClass() }, { TEXT("button"), UGenericButton::StaticClass() },
			{ TEXT("pillswitch"), UPillSwitchButton::StaticClass() }, { TEXT("switch"), UPillSwitchButton::StaticClass() }, { TEXT("toggle"), UPillSwitchButton::StaticClass() },
			{ TEXT("checkbox"), UCheckboxButton::StaticClass() },
			{ TEXT("radio"), URadioButton::StaticClass() },
			{ TEXT("menuitem"), UMenuItemButton::StaticClass() },
			{ TEXT("hold"), UHoldButton::StaticClass() },
			{ TEXT("tile"), UTileButton::StaticClass() },
		};
		for (const FKind& Pair : Kinds)
		{
			if (Key == Pair.Key)
			{
				return Pair.Value;
			}
		}
		// Choice / Segmented Choice live in ChoiceButtons.h; found by name like any class.
		if (Key == TEXT("choice"))
		{
			return FindClass(TEXT("ChoiceButton"), UStyledButtonBase::StaticClass());
		}
		if (Key == TEXT("segmented") || Key == TEXT("segmentedchoice"))
		{
			return FindClass(TEXT("SegmentedChoiceButton"), UStyledButtonBase::StaticClass());
		}
		if (Key == TEXT("glass") || Key == TEXT("glassbutton"))
		{
			return FindClass(TEXT("GlassButton"), UStyledButtonBase::StaticClass());
		}
		return FindClass(Kind, UStyledButtonBase::StaticClass());
	}

	/** Builds a ready-made button's parts at once - what the editor module does once a dropped one is placed. */
	void BuildReadyMadeParts(UStyledButtonBase* Button)
	{
		Button->CreatedFromPalette();
		if (Button->ConsumePendingDefaultContent())
		{
			TArray<UWidget*> Parts;
			Button->BuildDefaultContentForEditor(Parts);
			if (Parts.Num() > 0)
			{
				TArray<UWidget*> Moved;
				Button->SetPartsHidden(true, Moved);
			}
			Button->MarkPartsVisibilityDecided();
		}
	}

	/**
	 * Makes one widget from a spec and puts it in Parent (null: the root of an empty tree), then its children. Spec:
	 * {class, name, role, text, label, props, slot, index, children:[...], button: kind}.
	 */
	UWidget* BuildWidget(FBuildContext& Build, const TSharedPtr<FJsonObject>& Spec, UPanelWidget* Parent, int32 Index)
	{
		UWidgetBlueprint* Blueprint = Build.Blueprint;
		UWidgetTree* Tree = Blueprint->WidgetTree;

		// Role first: it may say the class and gives the house settings, which the spec's own settings then override.
		const FString RoleName = GetString(Spec, TEXT("role"));
		TSharedPtr<FJsonObject> Role;
		if (!RoleName.IsEmpty())
		{
			Role = Copy(FindRole(RoleName));
			if (!Role.IsValid())
			{
				TArray<FString> Names;
				LoadRoles()->Values.GetKeys(Names);
				Names.RemoveAll([](const FString& Name) { return Name.StartsWith(TEXT("$")); });
				Build.Warnings.Add(FString::Printf(TEXT("No role '%s' (roles: %s)."), *RoleName, *FString::Join(Names, TEXT(", "))));
			}
		}

		FString ClassName = GetString(Spec, TEXT("class"));
		const FString ButtonKind = GetString(Spec, TEXT("button"));
		if (ClassName.IsEmpty() && Role.IsValid())
		{
			ClassName = GetString(Role, TEXT("class"));
		}

		// Text and icons: a symbol the font doesn't have ("▶", "🔊") is the icon it means; other such characters go.
		FString Text = GetString(Spec, TEXT("text"));
		FString Label = GetString(Spec, TEXT("label"), Text);
		FString IconName = GetString(Spec, TEXT("icon"), Role.IsValid() ? GetString(Role, TEXT("icon")) : FString());
		if (IconName.IsEmpty())
		{
			const FString FromGlyph = NameTBDAIWidgetStyling::IconForGlyph(Label);
			if (!FromGlyph.IsEmpty())
			{
				Build.Warnings.Add(FString::Printf(TEXT("'%s' is a symbol the font doesn't draw: shown as the %s icon instead (use \"icon\": \"%s\")."), *Label, *FromGlyph, *FromGlyph));
				IconName = FromGlyph;
				Text.Reset();
				Label.Reset();
			}
		}
		{
			FString Removed;
			Text = NameTBDAIWidgetStyling::RemoveUndrawableGlyphs(Text, Removed);
			FString RemovedFromLabel;
			Label = NameTBDAIWidgetStyling::RemoveUndrawableGlyphs(Label, RemovedFromLabel);
			if (!Removed.IsEmpty() || !RemovedFromLabel.IsEmpty())
			{
				Build.Warnings.Add(FString::Printf(TEXT("Left out '%s': the font draws symbols and emoji as boxes. Use \"icon\" (list_options icons)."), *(Removed.IsEmpty() ? RemovedFromLabel : Removed)));
			}
		}
		// An icon on its own is an SVG Image (a text or image asked to show a library icon is one too).
		if (ButtonKind.IsEmpty() && !IconName.IsEmpty())
		{
			const FString ClassKey = Normalize(ClassName);
			const bool bTextOrImage = ClassKey == TEXT("textblock") || ClassKey == TEXT("text") || ClassKey == TEXT("richtextblock") || ClassKey == TEXT("image");
			if (ClassName.IsEmpty() || (bTextOrImage && !IconName.StartsWith(TEXT("/"))))
			{
				ClassName = TEXT("WidgetSVGImage");
			}
		}
		FString Error;
		UClass* Class = nullptr;
		if (!ButtonKind.IsEmpty())
		{
			Class = ResolveReadyMadeKind(ButtonKind);
			if (!Class)
			{
				Build.Warnings.Add(FString::Printf(TEXT("No ready-made button '%s': generic, pill_switch, checkbox, radio, menu_item, hold, tile, choice, segmented, glass."), *ButtonKind));
				return nullptr;
			}
		}
		else
		{
			Class = FindClass(ClassName.IsEmpty() ? TEXT("Overlay") : ClassName, UWidget::StaticClass(), &Error);
		}
		if (!Class)
		{
			Build.Warnings.Add(Error);
			return nullptr;
		}
		if (Class->HasAnyClassFlags(CLASS_Abstract))
		{
			Build.Warnings.Add(FString::Printf(TEXT("%s is abstract."), *Class->GetName()));
			return nullptr;
		}
		if (Class->ClassGeneratedBy == Blueprint)
		{
			Build.Warnings.Add(TEXT("A Widget Blueprint can't hold itself."));
			return nullptr;
		}
		if (Parent && !Parent->CanAddMoreChildren())
		{
			Build.Warnings.Add(FString::Printf(TEXT("%s takes one child and already has one - wrap them in an Overlay or a box."), *Parent->GetName()));
			return nullptr;
		}
		if (!Parent && Tree->RootWidget)
		{
			Build.Warnings.Add(TEXT("The Widget Blueprint already has a root: name a parent."));
			return nullptr;
		}

		FString BaseName = GetString(Spec, TEXT("name"));
		if (BaseName.IsEmpty())
		{
			BaseName = !RoleName.IsEmpty() ? RoleName : Class->GetName().Replace(TEXT("_C"), TEXT(""));
			BaseName = MakeAssetNameLike(BaseName);
		}
		const FName WidgetName = NameTBDAIHelpers::MakeWidgetName(Blueprint, BaseName);

		// A part the parent class binds by name (a Media Box Transport Bar's PlayPauseButton): the name and class must match.
		bool bOptionalPart = false;
		if (UClass* PartClass = NameTBDAIWidgetStyling::FindBoundPartClass(Blueprint->ParentClass, BaseName, bOptionalPart))
		{
			if (WidgetName.ToString() != BaseName)
			{
				Build.Warnings.Add(FString::Printf(TEXT("%s is a part %s binds by name, but that name is taken: this one is %s and isn't bound. Remove or rename the other first."),
					*BaseName, *Blueprint->ParentClass->GetName(), *WidgetName.ToString()));
			}
			else if (!Class->IsChildOf(PartClass))
			{
				Build.Warnings.Add(FString::Printf(TEXT("%s is bound by %s as a %s, but this is a %s: it won't be wired up (use class %s, or a ready-made button for a Button part)."),
					*BaseName, *Blueprint->ParentClass->GetName(), *PartClass->GetName(), *Class->GetName(), *PartClass->GetName()));
			}
		}

		Tree->Modify();
		UWidget* Widget = Tree->ConstructWidget<UWidget>(Class, WidgetName);
		if (!Widget)
		{
			Build.Warnings.Add(FString::Printf(TEXT("Couldn't make a %s."), *Class->GetName()));
			return nullptr;
		}
		Widget->SetFlags(RF_Transactional);

		if (Parent)
		{
			Parent->Modify();
			const int32 Count = Parent->GetChildrenCount();
			if (Index >= 0 && Index < Count)
			{
				Parent->InsertChildAt(Index, Widget);
			}
			else
			{
				Parent->AddChild(Widget);
			}
		}
		else
		{
			Tree->RootWidget = Widget;
		}

		if (UStyledButtonBase* ReadyMade = Cast<UStyledButtonBase>(Widget))
		{
			BuildReadyMadeParts(ReadyMade);
		}
		else
		{
			// What the palette does for a dropped widget: some set their own defaults there.
			Widget->CreatedFromPalette();
		}

		// Engine widgets in the house look (an engine Button is light grey, its text unreadable in the dark theme), before the
		// spec's own settings so those win. "theme": false keeps the engine's look.
		const FString VariantName = GetString(Spec, TEXT("variant"), Role.IsValid() ? GetString(Role, TEXT("variant")) : FString());
		const bool bTheme = GetBool(Spec, TEXT("theme"), true);
		if (bTheme && NameTBDAIWidgetStyling::IsThemedEngineWidget(Widget))
		{
			NameTBDAIWidgetStyling::ThemeEngineWidget(Widget, Build.bDark, NameTBDAIWidgetStyling::ParseVariant(VariantName));
			if (!Role.IsValid())
			{
				RememberRole(Blueprint, Widget, FString(EngineThemeMark) + (VariantName.IsEmpty() ? FString(TEXT("secondary")) : VariantName));
			}
		}

		// Settings: the role's, then the spec's; text / label shorthands.
		TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
		if (Role.IsValid())
		{
			if (TSharedPtr<FJsonObject> RoleProps = GetObject(Role, TEXT("props")))
			{
				Props->Values = RoleProps->Values;
			}
		}
		if (TSharedPtr<FJsonObject> Own = GetObject(Spec, TEXT("props")))
		{
			for (const auto& Pair : Own->Values)
			{
				Props->SetField(Pair.Key, Pair.Value);
			}
		}
		if (!Text.IsEmpty() && !Cast<UStyledButtonBase>(Widget) && FindFProperty<FProperty>(Widget->GetClass(), TEXT("Text")))
		{
			Props->SetStringField(TEXT("Text"), Text);
		}

		// An Image given a texture icon: its brush.
		if (!IconName.IsEmpty() && Widget->IsA<UImage>())
		{
			FString IconError;
			if (UObject* Icon = NameTBDAIWidgetStyling::FindOrMakeIcon(IconName, *Build.Tool, IconError))
			{
				if (Icon->IsA<UWidgetSVG>())
				{
					Build.Warnings.Add(FString::Printf(TEXT("%s: an Image can't draw an SVG - use an SVG Image (WidgetSVGImage) for %s."), *Widget->GetName(), *IconName));
				}
				else
				{
					Props->SetStringField(TEXT("Brush"), Icon->GetPathName());
				}
			}
			else
			{
				Build.Warnings.Add(IconError);
			}
		}

		// An SVG Image showing an icon: its asset, a size, the theme's text colour unless the spec says.
		if (!IconName.IsEmpty() && Widget->IsA<UWidgetSVGImage>())
		{
			FString IconError;
			if (UObject* Icon = NameTBDAIWidgetStyling::FindOrMakeIcon(IconName, *Build.Tool, IconError))
			{
				Props->SetStringField(TEXT("SVG"), Icon->GetPathName());
				if (!Props->HasField(TEXT("ImageSize")))
				{
					const double IconSize = GetNumber(Spec, TEXT("icon_size"), 24.0);
					Props->SetBoolField(TEXT("bOverrideImageSize"), true);
					Props->SetArrayField(TEXT("ImageSize"), { NameTBDAIJson::Num(IconSize), NameTBDAIJson::Num(IconSize) });
				}
				if (!Props->HasField(TEXT("ColorAndOpacity")))
				{
					Props->SetStringField(TEXT("ColorAndOpacity"), TEXT("@text"));
				}
			}
			else
			{
				Build.Warnings.Add(IconError);
			}
		}

		TSharedPtr<FJsonObject> Slot = MakeShared<FJsonObject>();
		if (Role.IsValid())
		{
			if (TSharedPtr<FJsonObject> RoleSlot = GetObject(Role, TEXT("slot")))
			{
				Slot->Values = RoleSlot->Values;
			}
		}
		if (TSharedPtr<FJsonObject> OwnSlot = GetObject(Spec, TEXT("slot")))
		{
			for (const auto& Pair : OwnSlot->Values)
			{
				Slot->SetField(Pair.Key, Pair.Value);
			}
		}

		if (Cast<UStyledButtonBase>(Widget))
		{
			// House defaults a ready-made button takes unless told otherwise.
			if (!Props->HasField(TEXT("AccentColor")))
			{
				Props->SetStringField(TEXT("AccentColor"), TEXT("@accent"));
			}
			if (!Props->HasField(TEXT("Motion")))
			{
				Props->SetStringField(TEXT("Motion"), UNameTBDAITheme::Get().ButtonMotion.ToString());
			}
		}

		TArray<FString> Errors;
		NameTBDAIHelpers::ApplyWidgetProps(Widget, Props, Slot, Build.bDark, Errors);
		for (const FString& Problem : Errors)
		{
			Build.Warnings.Add(FString::Printf(TEXT("%s: %s"), *Widget->GetName(), *Problem));
		}

		if (UStyledButtonBase* ReadyMade = Cast<UStyledButtonBase>(Widget))
		{
			if (!Label.IsEmpty())
			{
				ReadyMade->SetLabel(FText::FromString(Label));
			}
			if (!IconName.IsEmpty())
			{
				Build.Warnings.Add(FString::Printf(TEXT("%s: a ready-made button shows no icon from 'icon' (a Tile Button has an Icon part; or use a Button)."), *Widget->GetName()));
			}
		}

		TSharedPtr<FJsonObject> Made = MakeShared<FJsonObject>();
		Made->SetStringField(TEXT("name"), Widget->GetName());
		Made->SetStringField(TEXT("class"), Class->GetName());
		if (Parent)
		{
			Made->SetStringField(TEXT("parent"), Parent->GetName());
		}
		Build.Made.Add(Obj(Made));
		Build.MadeWidgets.Add(Widget);
		if (Role.IsValid())
		{
			RememberRole(Blueprint, Widget, RoleName);
		}
		Build.Tool->Changes->NoteTouched(Blueprint, FString::Printf(TEXT("Added %s '%s'"), *Class->GetName(), *Widget->GetName()));

		// Children.
		TArray<TSharedPtr<FJsonValue>> Children = GetArray(Spec, TEXT("children"));

		// A Button given text and / or an icon and no children: they become its content, in a colour that reads on its fill.
		if (Children.Num() == 0 && Widget->IsA<UButton>() && !Widget->IsA<UStyledButtonBase>() && (!Label.IsEmpty() || !IconName.IsEmpty()))
		{
			FLinearColor Fill = NameTBDAIHelpers::GetPalette(Build.bDark).SurfaceRaised;
			NameTBDAIWidgetStyling::GetButtonFill(Widget, Fill);
			const NameTBDAIWidgetStyling::EButtonVariant Variant = NameTBDAIWidgetStyling::ParseVariant(VariantName);
			const FLinearColor Behind = (Fill.A < 0.5f || Variant == NameTBDAIWidgetStyling::EButtonVariant::Ghost) ? NameTBDAIHelpers::GetPalette(Build.bDark).Surface : Fill;
			const FString Ink = ToHex(NameTBDAIWidgetStyling::ReadableOn(Behind));

			TSharedPtr<FJsonObject> IconSpec;
			if (!IconName.IsEmpty())
			{
				IconSpec = MakeShared<FJsonObject>();
				IconSpec->SetStringField(TEXT("class"), TEXT("WidgetSVGImage"));
				IconSpec->SetStringField(TEXT("name"), WidgetName.ToString() + TEXT("Icon"));
				IconSpec->SetStringField(TEXT("icon"), IconName);
				IconSpec->SetNumberField(TEXT("icon_size"), GetNumber(Spec, TEXT("icon_size"), Role.IsValid() ? GetNumber(Role, TEXT("icon_size"), 20.0) : 20.0));
				TSharedPtr<FJsonObject> IconProps = MakeShared<FJsonObject>();
				IconProps->SetStringField(TEXT("ColorAndOpacity"), Ink);
				IconProps->SetStringField(TEXT("Visibility"), TEXT("HitTestInvisible"));
				IconSpec->SetObjectField(TEXT("props"), IconProps);
			}
			TSharedPtr<FJsonObject> LabelSpec;
			if (!Label.IsEmpty())
			{
				LabelSpec = MakeShared<FJsonObject>();
				LabelSpec->SetStringField(TEXT("class"), TEXT("TextBlock"));
				LabelSpec->SetStringField(TEXT("name"), WidgetName.ToString() + TEXT("Label"));
				LabelSpec->SetStringField(TEXT("text"), Label);
				TSharedPtr<FJsonObject> LabelProps = MakeShared<FJsonObject>();
				TSharedPtr<FJsonObject> Font = MakeShared<FJsonObject>();
				Font->SetStringField(TEXT("Size"), TEXT("@type.body"));
				Font->SetStringField(TEXT("TypefaceFontName"), TEXT("@font.strong"));
				LabelProps->SetObjectField(TEXT("Font"), Font);
				LabelProps->SetStringField(TEXT("ColorAndOpacity"), Ink);
				LabelProps->SetStringField(TEXT("Visibility"), TEXT("HitTestInvisible"));
				LabelSpec->SetObjectField(TEXT("props"), LabelProps);
			}
			if (IconSpec.IsValid() && LabelSpec.IsValid())
			{
				TSharedPtr<FJsonObject> RowSpec = MakeShared<FJsonObject>();
				RowSpec->SetStringField(TEXT("class"), TEXT("HorizontalBox"));
				RowSpec->SetStringField(TEXT("name"), WidgetName.ToString() + TEXT("Content"));
				TSharedPtr<FJsonObject> LabelSlot = MakeShared<FJsonObject>();
				LabelSlot->SetStringField(TEXT("align"), TEXT("left"));
				TSharedPtr<FJsonObject> LabelPadding = MakeShared<FJsonObject>();
				LabelPadding->SetNumberField(TEXT("Left"), 8.0);
				LabelSlot->SetObjectField(TEXT("Padding"), LabelPadding);
				LabelSpec->SetObjectField(TEXT("slot"), LabelSlot);
				TSharedPtr<FJsonObject> IconSlot = MakeShared<FJsonObject>();
				IconSlot->SetStringField(TEXT("align"), TEXT("left"));
				IconSpec->SetObjectField(TEXT("slot"), IconSlot);
				RowSpec->SetArrayField(TEXT("children"), { Obj(IconSpec), Obj(LabelSpec) });
				Children.Add(Obj(RowSpec));
			}
			else
			{
				Children.Add(Obj(IconSpec.IsValid() ? IconSpec : LabelSpec));
			}
		}
		if (Children.Num() > 0)
		{
			UPanelWidget* AsPanel = Cast<UPanelWidget>(Widget);
			if (!AsPanel)
			{
				Build.Warnings.Add(FString::Printf(TEXT("%s (%s) can't hold children."), *Widget->GetName(), *Class->GetName()));
			}
			else
			{
				for (const TSharedPtr<FJsonValue>& Child : Children)
				{
					if (Child.IsValid() && Child->Type == EJson::Object)
					{
						BuildWidget(Build, Child->AsObject(), AsPanel, INDEX_NONE);
					}
				}
			}
		}
		return Widget;
	}

	/** Compiles, gives every new widget its GUID, notes the change; the result's data and warnings. */
	void FinishBlueprintEdit(UWidgetBlueprint* Blueprint, FNameTBDAIToolResult& Result, bool bStructural)
	{
		if (bStructural && Blueprint->WidgetTree && Blueprint->WidgetTree->RootWidget)
		{
			NameTBDAIHelpers::GiveGuids(Blueprint, Blueprint->WidgetTree->RootWidget);
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		}
		else
		{
			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		}
		TArray<FString> Problems;
		const bool bCompiled = NameTBDAIHelpers::Compile(Blueprint, Problems);
		Result.Warnings.Append(Problems);
		if (Result.Data.IsValid())
		{
			Result.Data->SetStringField(TEXT("blueprint"), Blueprint->GetPathName());
			Result.Data->SetBoolField(TEXT("compiled"), bCompiled);
		}
		NameTBDAITools::SetLastAsset(Blueprint);
	}

	FString MakeAssetNameLike(const FString& Text)
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
		return Out.IsEmpty() ? FString(TEXT("Widget")) : Out;
	}

	/** {"Type": ...} for a shape's Fill or a stroke's Paint, from set_shape_paint's arguments. */
	TSharedPtr<FJsonObject> MakePaint(const TSharedPtr<FJsonObject>& Args, TArray<FString>& OutErrors)
	{
		TSharedPtr<FJsonObject> Paint = MakeShared<FJsonObject>();
		const FString Material = GetString(Args, TEXT("material"));
		const TSharedPtr<FJsonObject> Gradient = GetObject(Args, TEXT("gradient"));
		if (!Material.IsEmpty())
		{
			Paint->SetStringField(TEXT("Type"), TEXT("Image"));
			Paint->SetStringField(TEXT("Image"), Material);
			Paint->SetStringField(TEXT("Color"), GetString(Args, TEXT("color"), TEXT("#FFFFFF")));
			const FString Mapping = GetString(Args, TEXT("image_mapping"));
			if (!Mapping.IsEmpty())
			{
				Paint->SetStringField(TEXT("ImageMapping"), Mapping);
			}
		}
		else if (Gradient.IsValid())
		{
			const FString Kind = GetString(Gradient, TEXT("type"), TEXT("linear")).ToLower();
			Paint->SetStringField(TEXT("Type"), Kind.StartsWith(TEXT("rad")) ? TEXT("RadialGradient") : TEXT("LinearGradient"));
			TArray<TSharedPtr<FJsonValue>> Stops;
			for (const TSharedPtr<FJsonValue>& Stop : GetArray(Gradient, TEXT("stops")))
			{
				TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
				if (Stop->Type == EJson::Array && Stop->AsArray().Num() == 2)
				{
					// [position, color]
					Entry->SetField(TEXT("Position"), Stop->AsArray()[0]);
					Entry->SetField(TEXT("Color"), Stop->AsArray()[1]);
				}
				else if (Stop->Type == EJson::Object)
				{
					Entry->Values = Stop->AsObject()->Values;
				}
				else
				{
					OutErrors.Add(TEXT("A gradient stop is [position, color] or {\"Position\": p, \"Color\": c}."));
					continue;
				}
				Stops.Add(Obj(Entry));
			}
			if (Stops.Num() < 2)
			{
				OutErrors.Add(TEXT("A gradient needs at least two stops."));
			}
			Paint->SetArrayField(TEXT("GradientStops"), Stops);
			if (Has(Gradient, TEXT("angle")))
			{
				Paint->SetNumberField(TEXT("GradientAngle"), GetNumber(Gradient, TEXT("angle")));
			}
			if (Has(Gradient, TEXT("center")))
			{
				Paint->SetField(TEXT("GradientCenter"), Gradient->TryGetField(TEXT("center")));
			}
			if (Has(Gradient, TEXT("radius")))
			{
				Paint->SetNumberField(TEXT("GradientRadius"), GetNumber(Gradient, TEXT("radius")));
			}
			Paint->SetStringField(TEXT("Color"), TEXT("#FFFFFF"));
		}
		else
		{
			Paint->SetStringField(TEXT("Type"), TEXT("Color"));
			Paint->SetStringField(TEXT("Color"), GetString(Args, TEXT("color"), TEXT("@surface")));
		}
		return Paint;
	}

	FString SpecSchemaHelp()
	{
		return TEXT("A widget spec: {class (widget class or Widget Blueprint path; optional with role), name, role (house preset: card, panel, pill, divider, display, title, heading, body, caption, label, glow, aurora, particles, icon, stack, row, frame, button, button_primary, button_ghost, icon_button, toolbar - list_options fields isn't needed for these), text (Text field shorthand; on a Button: its label, built inside it in a colour that reads on it), icon (an icon name - list_options icons: play, pause, stop, volume, mute, subtitles, fullscreen, settings, close... - or an SVG / texture path: on a Button its content beside any text, elsewhere an SVG Image; icon_size px), variant (engine Button: primary | secondary | ghost), theme (false: keep an engine widget's own look), button (ready-made kind: generic, pill_switch, checkbox, radio, menu_item, hold, tile, choice, segmented, glass), label (a ready-made button's label), props {field: value}, slot {align: top_left|top|center|bottom|fill..., padding, any slot field}, children [specs]}. Engine Buttons, Sliders, Check Boxes, Progress Bars and Editable Text Boxes are built in the theme's look and every text is checked against what is behind it. Never write symbols or emoji (the font draws them as boxes): use icon. Values take theme tokens: \"@accent\", \"@text~0.6\", \"@space.4\", \"@radius.large\", \"@type.title\", \"@font.strong\", \"@spring.Bouncy\"; colors as \"#RRGGBB[AA]\".");
	}
}

namespace NameTBDAITools
{
	void RegisterWidgetTools()
	{
		using namespace NameTBDAIWidgetTools;

		// ---- create_widget_blueprint ----
		Register({
			TEXT("create_widget_blueprint"),
			TEXT("Makes a new Widget Blueprint (name made unique; WBP_ prefix added) with a root panel, optionally at a fixed size (a Size Box around the root). Returns where to add children. Then use add_widgets to fill it in one call."),
			FNameTBDAISchema()
				.Str(TEXT("name"), TEXT("Asset name, e.g. GreetingCard."), true)
				.Str(TEXT("folder"), TEXT("Content folder (default: the settings' Output Folder)."))
				.Str(TEXT("root"), TEXT("Root panel class (default Overlay): Overlay, CanvasPanel, VerticalBox, HorizontalBox, GridPanel..."))
				.Arr(TEXT("size"), TEXT("[width, height]: a fixed size (the root sits in a Size Box). Omit to fill whatever it is placed in."), TEXT("number"))
				.Str(TEXT("parent_class"), TEXT("Parent class (default UserWidget), e.g. CommonActivatableWidget or a Blueprint path."))
				.Build(),
			true, false, false,
			[](const TSharedPtr<FJsonObject>& Args, FNameTBDAIToolContext& Context)
			{
				FString Error;
				UClass* ParentClass = UUserWidget::StaticClass();
				const FString ParentName = GetString(Args, TEXT("parent_class"));
				if (!ParentName.IsEmpty())
				{
					ParentClass = FindClass(ParentName, UUserWidget::StaticClass(), &Error);
					if (!ParentClass)
					{
						return FNameTBDAIToolResult::Fail(Error);
					}
				}

				UWidgetBlueprintFactory* Factory = NewObject<UWidgetBlueprintFactory>();
				Factory->ParentClass = ParentClass;
				Factory->BlueprintType = BPTYPE_Normal;
				UWidgetBlueprint* Blueprint = Cast<UWidgetBlueprint>(NameTBDAIHelpers::CreateAsset(GetString(Args, TEXT("folder")),
					NameTBDAIHelpers::MakeAssetName(GetString(Args, TEXT("name")), TEXT("WBP_")), UWidgetBlueprint::StaticClass(), Factory, Context, Error));
				if (!Blueprint)
				{
					return FNameTBDAIToolResult::Fail(Error);
				}
				Blueprint->Modify();
				UWidgetTree* Tree = Blueprint->WidgetTree;
				Tree->SetFlags(RF_Transactional);
				Tree->Modify();

				// Start from an empty tree: whatever root the factory made goes.
				if (Tree->RootWidget)
				{
					RemoveWidgetFromTree(Blueprint, Tree->RootWidget);
				}

				FBuildContext Build;
				Build.Blueprint = Blueprint;
				Build.bDark = NameTBDAIHelpers::IsDark(FString());
				Build.Tool = &Context;

				const TArray<TSharedPtr<FJsonValue>> Size = GetArray(Args, TEXT("size"));
				UPanelWidget* Holder = nullptr;
				if (Size.Num() == 2)
				{
					TSharedPtr<FJsonObject> FrameSpec = MakeShared<FJsonObject>();
					FrameSpec->SetStringField(TEXT("class"), TEXT("SizeBox"));
					FrameSpec->SetStringField(TEXT("name"), TEXT("Frame"));
					TSharedPtr<FJsonObject> FrameProps = MakeShared<FJsonObject>();
					FrameProps->SetBoolField(TEXT("bOverride_WidthOverride"), true);
					FrameProps->SetBoolField(TEXT("bOverride_HeightOverride"), true);
					FrameProps->SetField(TEXT("WidthOverride"), Size[0]);
					FrameProps->SetField(TEXT("HeightOverride"), Size[1]);
					FrameSpec->SetObjectField(TEXT("props"), FrameProps);
					Holder = Cast<UPanelWidget>(BuildWidget(Build, FrameSpec, nullptr, INDEX_NONE));
				}

				TSharedPtr<FJsonObject> RootSpec = MakeShared<FJsonObject>();
				RootSpec->SetStringField(TEXT("class"), GetString(Args, TEXT("root"), TEXT("Overlay")));
				RootSpec->SetStringField(TEXT("name"), Holder ? TEXT("Content") : TEXT("Root"));
				UWidget* Root = BuildWidget(Build, RootSpec, Holder, INDEX_NONE);
				if (!Root)
				{
					return FNameTBDAIToolResult::Fail(FString::Join(Build.Warnings, TEXT(" ")));
				}

				FNameTBDAIToolResult Result = FNameTBDAIToolResult::Ok();
				Result.Data->SetStringField(TEXT("add_children_to"), Root->GetName());
				// What the parent class wires up by name: build these with exactly these names and classes.
				const TArray<TSharedPtr<FJsonValue>> Parts = NameTBDAIWidgetStyling::DescribeBoundParts(ParentClass);
				if (Parts.Num() > 0)
				{
					Result.Data->SetArrayField(TEXT("bound_parts"), Parts);
					Result.Data->SetStringField(TEXT("bound_parts_note"), TEXT("The parent class finds these widgets by name: give them exactly these names and classes (a Button part takes a Button or a ready-made button)."));
				}
				Result.Warnings = Build.Warnings;
				FinishBlueprintEdit(Blueprint, Result, true);
				return Result;
			}
		});

		// ---- add_widgets ----
		Register({
			TEXT("add_widgets"),
			FString(TEXT("Adds widgets - a whole nested tree in one call - to a Widget Blueprint, under 'parent', in order (later ones draw on top in an Overlay). ")) + SpecSchemaHelp(),
			FNameTBDAISchema()
				.Str(TEXT("blueprint"), TEXT("Widget Blueprint (omit: the one built last or open)."))
				.Str(TEXT("parent"), TEXT("Parent widget name (omit: the root; for an empty Blueprint the first spec becomes the root)."))
				.Int(TEXT("index"), TEXT("Where among the parent's children the first goes (default: last)."))
				.Arr(TEXT("widgets"), TEXT("Widget specs, each may have children."), TEXT("object"), true)
				.Str(TEXT("mode"), TEXT("Theme palette for tokens: dark or light (default: the theme's)."))
				.Build(),
			true, false, false,
			[](const TSharedPtr<FJsonObject>& Args, FNameTBDAIToolContext& Context)
			{
				FString Error;
				UWidgetBlueprint* Blueprint = NameTBDAIHelpers::FindWidgetBlueprint(GetString(Args, TEXT("blueprint")), Error);
				if (!Blueprint)
				{
					return FNameTBDAIToolResult::Fail(Error);
				}
				Blueprint->Modify();
				UPanelWidget* Parent = nullptr;
				if (Blueprint->WidgetTree->RootWidget)
				{
					UWidget* Found = NameTBDAIHelpers::FindWidget(Blueprint, GetString(Args, TEXT("parent")), Error);
					Parent = Cast<UPanelWidget>(Found);
					if (!Parent)
					{
						return FNameTBDAIToolResult::Fail(Found ? FString::Printf(TEXT("%s can't hold children."), *Found->GetName()) : Error);
					}
				}

				FBuildContext Build;
				Build.Blueprint = Blueprint;
				Build.bDark = NameTBDAIHelpers::IsDark(GetString(Args, TEXT("mode")));
				Build.Tool = &Context;
				int32 Index = static_cast<int32>(GetNumber(Args, TEXT("index"), INDEX_NONE));
				for (const TSharedPtr<FJsonValue>& Spec : GetArray(Args, TEXT("widgets")))
				{
					if (!Spec.IsValid() || Spec->Type != EJson::Object)
					{
						Build.Warnings.Add(TEXT("Each widget is an object spec."));
						continue;
					}
					UWidget* Made = BuildWidget(Build, Spec->AsObject(), Parent, Index);
					if (!Parent && Made)
					{
						Parent = Cast<UPanelWidget>(Made); // further specs go inside the new root
					}
					if (Index >= 0)
					{
						++Index;
					}
				}
				if (Build.Made.Num() == 0)
				{
					return FNameTBDAIToolResult::Fail(FString::Join(Build.Warnings, TEXT(" ")));
				}
				CheckContrast(Build.MadeWidgets, Build.bDark, Build.Warnings);
				FNameTBDAIToolResult Result = FNameTBDAIToolResult::Ok();
				Result.Data->SetArrayField(TEXT("added"), Build.Made);
				Result.Warnings = Build.Warnings;
				FinishBlueprintEdit(Blueprint, Result, true);
				return Result;
			}
		});

		// ---- set_widget ----
		Register({
			TEXT("set_widget"),
			TEXT("Changes a widget's settings and/or its slot (layout in its parent). Only the fields given change. Paths work as keys (\"Fill.Color\", \"Font.Size\"); \"List+\" appends. role re-applies a house preset first. Theme tokens and #hex colors work."),
			FNameTBDAISchema()
				.Str(TEXT("blueprint"), TEXT("Widget Blueprint (omit: the one built last or open)."))
				.Str(TEXT("widget"), TEXT("Widget name (omit: the root)."), true)
				.Obj(TEXT("props"), TEXT("Fields to set."))
				.Obj(TEXT("slot"), TEXT("Slot fields; shorthands: align (top_left, center, bottom...), padding."))
				.Str(TEXT("text"), TEXT("Shorthand for the Text field / a ready-made button's label."))
				.Str(TEXT("role"), TEXT("Apply a house preset's settings first."))
				.Str(TEXT("variant"), TEXT("An engine Button's look again: primary (accent filled), secondary (raised, outlined), ghost (no fill until hovered). Also re-themes an engine Slider, Check Box, Progress Bar or Editable Text Box."))
				.Str(TEXT("mode"), TEXT("dark or light, for tokens."))
				.Build(),
			true, false, false,
			[](const TSharedPtr<FJsonObject>& Args, FNameTBDAIToolContext& Context)
			{
				FString Error;
				UWidgetBlueprint* Blueprint = NameTBDAIHelpers::FindWidgetBlueprint(GetString(Args, TEXT("blueprint")), Error);
				UWidget* Widget = Blueprint ? NameTBDAIHelpers::FindWidget(Blueprint, GetString(Args, TEXT("widget")), Error) : nullptr;
				if (!Widget)
				{
					return FNameTBDAIToolResult::Fail(Error);
				}
				Blueprint->Modify();
				const bool bDark = NameTBDAIHelpers::IsDark(GetString(Args, TEXT("mode")));
				TArray<FString> Errors;

				TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
				TSharedPtr<FJsonObject> Slot = MakeShared<FJsonObject>();
				const FString RoleName = GetString(Args, TEXT("role"));
				if (!RoleName.IsEmpty())
				{
					if (TSharedPtr<FJsonObject> Role = Copy(FindRole(RoleName)))
					{
						if (TSharedPtr<FJsonObject> RoleProps = GetObject(Role, TEXT("props"))) { Props->Values = RoleProps->Values; }
						if (TSharedPtr<FJsonObject> RoleSlot = GetObject(Role, TEXT("slot"))) { Slot->Values = RoleSlot->Values; }
					}
					else
					{
						Errors.Add(FString::Printf(TEXT("No role '%s'."), *RoleName));
					}
				}
				if (TSharedPtr<FJsonObject> Own = GetObject(Args, TEXT("props")))
				{
					for (const auto& Pair : Own->Values) { Props->SetField(Pair.Key, Pair.Value); }
				}
				if (TSharedPtr<FJsonObject> OwnSlot = GetObject(Args, TEXT("slot")))
				{
					for (const auto& Pair : OwnSlot->Values) { Slot->SetField(Pair.Key, Pair.Value); }
				}
				FString Text = GetString(Args, TEXT("text"));
				{
					FString Removed;
					Text = NameTBDAIWidgetStyling::RemoveUndrawableGlyphs(Text, Removed);
					if (!Removed.IsEmpty())
					{
						Errors.Add(FString::Printf(TEXT("Left out '%s': the font draws symbols and emoji as boxes. Use add_widgets with \"icon\" (list_options icons)."), *Removed));
					}
				}
				const FString VariantName = GetString(Args, TEXT("variant"));
				if (!VariantName.IsEmpty() && NameTBDAIWidgetStyling::IsThemedEngineWidget(Widget))
				{
					NameTBDAIWidgetStyling::ThemeEngineWidget(Widget, bDark, NameTBDAIWidgetStyling::ParseVariant(VariantName));
					RememberRole(Blueprint, Widget, FString(EngineThemeMark) + VariantName);
				}
				UStyledButtonBase* ReadyMade = Cast<UStyledButtonBase>(Widget);
				if (!Text.IsEmpty() && !ReadyMade && FindFProperty<FProperty>(Widget->GetClass(), TEXT("Text")))
				{
					Props->SetStringField(TEXT("Text"), Text);
				}

				NameTBDAIHelpers::ApplyWidgetProps(Widget, Props, Slot, bDark, Errors);
				if (!RoleName.IsEmpty() && FindRole(RoleName).IsValid())
				{
					RememberRole(Blueprint, Widget, RoleName);
				}
				if (ReadyMade && !Text.IsEmpty())
				{
					ReadyMade->SetLabel(FText::FromString(Text));
				}
				TArray<UWidget*> Checked;
				UWidgetTree::ForWidgetAndChildren(Widget, [&Checked](UWidget* Each) { Checked.Add(Each); });
				CheckContrast(Checked, bDark, Errors);
				Context.Changes->NoteTouched(Blueprint, FString::Printf(TEXT("Changed %s"), *Widget->GetName()));

				FNameTBDAIToolResult Result = FNameTBDAIToolResult::Ok();
				Result.Data->SetStringField(TEXT("widget"), Widget->GetName());
				Result.Warnings = Errors;
				FinishBlueprintEdit(Blueprint, Result, false);
				return Result;
			}
		});

		// ---- remove_widgets ----
		Register({
			TEXT("remove_widgets"),
			TEXT("Removes widgets and everything inside them."),
			FNameTBDAISchema()
				.Str(TEXT("blueprint"), TEXT("Widget Blueprint (omit: the one built last or open)."))
				.Arr(TEXT("widgets"), TEXT("Widget names."), TEXT("string"), true)
				.Build(),
			true, false, false,
			[](const TSharedPtr<FJsonObject>& Args, FNameTBDAIToolContext& Context)
			{
				FString Error;
				UWidgetBlueprint* Blueprint = NameTBDAIHelpers::FindWidgetBlueprint(GetString(Args, TEXT("blueprint")), Error);
				if (!Blueprint)
				{
					return FNameTBDAIToolResult::Fail(Error);
				}
				Blueprint->Modify();
				Blueprint->WidgetTree->Modify();
				TArray<FString> Removed;
				TArray<FString> Warnings;
				for (const FString& Name : GetStringArray(Args, TEXT("widgets")))
				{
					UWidget* Widget = NameTBDAIHelpers::FindWidget(Blueprint, Name, Error);
					if (!Widget)
					{
						Warnings.Add(Error);
						continue;
					}
					Removed.Add(Widget->GetName());
					RemoveWidgetFromTree(Blueprint, Widget);
				}
				Context.Changes->NoteTouched(Blueprint, FString::Printf(TEXT("Removed %s"), *FString::Join(Removed, TEXT(", "))));
				FNameTBDAIToolResult Result = FNameTBDAIToolResult::Ok();
				Result.Data->SetField(TEXT("removed"), Arr(Removed));
				Result.Warnings = Warnings;
				FinishBlueprintEdit(Blueprint, Result, true);
				return Result;
			}
		});

		// ---- move_widget ----
		Register({
			TEXT("move_widget"),
			TEXT("Moves a widget into another parent and/or to another place among its siblings (in an Overlay, later = on top). Its slot settings start fresh in a new parent; pass slot to set them."),
			FNameTBDAISchema()
				.Str(TEXT("blueprint"), TEXT("Widget Blueprint (omit: the one built last or open)."))
				.Str(TEXT("widget"), TEXT("Widget to move."), true)
				.Str(TEXT("parent"), TEXT("New parent (omit: the same one)."))
				.Int(TEXT("index"), TEXT("Place among the parent's children (default: last)."))
				.Obj(TEXT("slot"), TEXT("Slot settings in the new place."))
				.Build(),
			true, false, false,
			[](const TSharedPtr<FJsonObject>& Args, FNameTBDAIToolContext& Context)
			{
				FString Error;
				UWidgetBlueprint* Blueprint = NameTBDAIHelpers::FindWidgetBlueprint(GetString(Args, TEXT("blueprint")), Error);
				UWidget* Widget = Blueprint ? NameTBDAIHelpers::FindWidget(Blueprint, GetString(Args, TEXT("widget")), Error) : nullptr;
				if (!Widget)
				{
					return FNameTBDAIToolResult::Fail(Error);
				}
				UPanelWidget* OldParent = Widget->GetParent();
				UPanelWidget* NewParent = OldParent;
				const FString ParentName = GetString(Args, TEXT("parent"));
				if (!ParentName.IsEmpty())
				{
					NewParent = Cast<UPanelWidget>(NameTBDAIHelpers::FindWidget(Blueprint, ParentName, Error));
					if (!NewParent)
					{
						return FNameTBDAIToolResult::Fail(Error.IsEmpty() ? FString::Printf(TEXT("%s can't hold children."), *ParentName) : Error);
					}
				}
				if (!NewParent)
				{
					return FNameTBDAIToolResult::Fail(TEXT("The root can't be moved."));
				}
				for (UWidget* Up = NewParent; Up; Up = Up->GetParent())
				{
					if (Up == Widget)
					{
						return FNameTBDAIToolResult::Fail(TEXT("A widget can't go inside itself."));
					}
				}
				if (NewParent != OldParent && !NewParent->CanAddMoreChildren())
				{
					return FNameTBDAIToolResult::Fail(FString::Printf(TEXT("%s takes one child and has one."), *NewParent->GetName()));
				}

				Blueprint->Modify();
				Widget->Modify();
				if (OldParent)
				{
					OldParent->Modify();
					OldParent->RemoveChild(Widget);
				}
				NewParent->Modify();
				const int32 Index = static_cast<int32>(GetNumber(Args, TEXT("index"), INDEX_NONE));
				if (Index >= 0 && Index < NewParent->GetChildrenCount())
				{
					NewParent->InsertChildAt(Index, Widget);
				}
				else
				{
					NewParent->AddChild(Widget);
				}
				TArray<FString> Errors;
				NameTBDAIHelpers::ApplyWidgetProps(Widget, nullptr, GetObject(Args, TEXT("slot")), NameTBDAIHelpers::IsDark(FString()), Errors);
				Context.Changes->NoteTouched(Blueprint, FString::Printf(TEXT("Moved %s into %s"), *Widget->GetName(), *NewParent->GetName()));
				FNameTBDAIToolResult Result = FNameTBDAIToolResult::Ok();
				Result.Warnings = Errors;
				FinishBlueprintEdit(Blueprint, Result, true);
				return Result;
			}
		});

		// ---- set_shape_paint ----
		Register({
			TEXT("set_shape_paint"),
			TEXT("Paints a plugin shape (Rectangle, Ellipse, Star, Triangle, Polygon, Custom Shape) or the fill/stroke of a ready-made button's background: a flat color, a linear or radial gradient, or a material (a UI material or material instance - e.g. one from create_material_instance). target stroke also sets width / enables it."),
			FNameTBDAISchema()
				.Str(TEXT("blueprint"), TEXT("Widget Blueprint (omit: the one built last or open)."))
				.Str(TEXT("widget"), TEXT("The shape widget."), true)
				.Str(TEXT("target"), TEXT("fill (default) or stroke."), false, { TEXT("fill"), TEXT("stroke") })
				.Str(TEXT("color"), TEXT("Flat color (#RRGGBB[AA] or a token like @accent), or the tint over a material."))
				.Obj(TEXT("gradient"), TEXT("{type: linear|radial, stops: [[0, \"#2EE6C58C\"], [1, \"#00000000\"]], angle (linear, degrees: 0 left->right, 90 top->bottom, -90 bottom->top), center [x,y] and radius (radial, fractions of the box)}."))
				.Str(TEXT("material"), TEXT("A material path: painted with it (Image paint)."))
				.Str(TEXT("image_mapping"), TEXT("Stroke with a material: Box (stretched) or AlongOutline (U runs along the outline - for edge glows)."))
				.Num(TEXT("stroke_width"), TEXT("Stroke width in px (target stroke)."))
				.Str(TEXT("stroke_align"), TEXT("Inside, Center or Outside (target stroke)."))
				.Str(TEXT("mode"), TEXT("dark or light, for tokens."))
				.Build(),
			true, false, false,
			[](const TSharedPtr<FJsonObject>& Args, FNameTBDAIToolContext& Context)
			{
				FString Error;
				UWidgetBlueprint* Blueprint = NameTBDAIHelpers::FindWidgetBlueprint(GetString(Args, TEXT("blueprint")), Error);
				UWidget* Widget = Blueprint ? NameTBDAIHelpers::FindWidget(Blueprint, GetString(Args, TEXT("widget")), Error) : nullptr;
				if (!Widget)
				{
					return FNameTBDAIToolResult::Fail(Error);
				}
				TArray<FString> Errors;
				TSharedPtr<FJsonObject> Paint = MakePaint(Args, Errors);
				TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
				const bool bStroke = GetString(Args, TEXT("target"), TEXT("fill")) == TEXT("stroke");

				// A ready-made button's background is its Background Look.
				const bool bButton = Cast<UContextMenuButton>(Widget) != nullptr;
				const FString Prefix = bButton ? TEXT("BackgroundLook.") : TEXT("");
				if (bStroke)
				{
					TSharedPtr<FJsonObject> Stroke = MakeShared<FJsonObject>();
					Stroke->SetBoolField(TEXT("bEnabled"), true);
					Stroke->SetObjectField(TEXT("Paint"), Paint);
					if (Has(Args, TEXT("stroke_width")))
					{
						Stroke->SetNumberField(TEXT("Width"), GetNumber(Args, TEXT("stroke_width")));
					}
					const FString Align = GetString(Args, TEXT("stroke_align"));
					if (!Align.IsEmpty())
					{
						Stroke->SetStringField(TEXT("Align"), Align);
					}
					Props->SetObjectField(Prefix + TEXT("Stroke"), Stroke);
				}
				else
				{
					if (!bButton)
					{
						Props->SetBoolField(TEXT("bFill"), true);
					}
					Props->SetObjectField(Prefix + TEXT("Fill"), Paint);
				}
				Blueprint->Modify();
				NameTBDAIHelpers::ApplyWidgetProps(Widget, Props, nullptr, NameTBDAIHelpers::IsDark(GetString(Args, TEXT("mode"))), Errors);
				Context.Changes->NoteTouched(Blueprint, FString::Printf(TEXT("Painted %s's %s"), *Widget->GetName(), bStroke ? TEXT("stroke") : TEXT("fill")));
				FNameTBDAIToolResult Result = FNameTBDAIToolResult::Ok();
				Result.Warnings = Errors;
				FinishBlueprintEdit(Blueprint, Result, false);
				return Result;
			}
		});

		// ---- compile_and_save ----
		Register({
			TEXT("compile_and_save"),
			TEXT("Compiles a Widget Blueprint (reporting errors and warnings) and saves it - or saves any other asset."),
			FNameTBDAISchema().Str(TEXT("asset"), TEXT("Asset path or name (omit: the one built last or open).")).Build(),
			true, false, false,
			[](const TSharedPtr<FJsonObject>& Args, FNameTBDAIToolContext& Context)
			{
				FString Error;
				const FString Name = GetString(Args, TEXT("asset"));
				UObject* Asset = Name.IsEmpty() ? static_cast<UObject*>(NameTBDAIHelpers::FindWidgetBlueprint(FString(), Error)) : NameTBDAIHelpers::FindAsset(Name, nullptr, Error);
				if (!Asset)
				{
					return FNameTBDAIToolResult::Fail(Error);
				}
				FNameTBDAIToolResult Result = FNameTBDAIToolResult::Ok();
				if (UWidgetBlueprint* Blueprint = Cast<UWidgetBlueprint>(Asset))
				{
					TArray<FString> Problems;
					Result.Data->SetBoolField(TEXT("compiled"), NameTBDAIHelpers::Compile(Blueprint, Problems));
					Result.Warnings = Problems;
				}
				FNameTBDAIChangeSet Save;
				Save.NoteTouched(Asset);
				Asset->MarkPackageDirty();
				Result.Data->SetNumberField(TEXT("saved"), Save.SaveAll());
				Context.Changes->NoteTouched(Asset);
				return Result;
			}
		});

		// ---- apply_theme ----
		Register({
			TEXT("apply_theme"),
			TEXT("Repaints a Widget Blueprint in the theme's dark or light palette: every widget built with a house role gets that role's colors in the new palette (sizes and layout untouched); ready-made buttons get the accent; engine Buttons, Sliders, Check Boxes, Progress Bars and Editable Text Boxes get the house look in it; then every text is checked against what is behind it. See the palette with list_options theme."),
			FNameTBDAISchema()
				.Str(TEXT("blueprint"), TEXT("Widget Blueprint (omit: the one built last or open)."))
				.Str(TEXT("mode"), TEXT("dark or light."), true, { TEXT("dark"), TEXT("light") })
				.Bool(TEXT("engine_widgets"), TEXT("Also theme engine widgets that weren't built themed (default true)."))
				.Build(),
			true, false, false,
			[](const TSharedPtr<FJsonObject>& Args, FNameTBDAIToolContext& Context)
			{
				FString Error;
				UWidgetBlueprint* Blueprint = NameTBDAIHelpers::FindWidgetBlueprint(GetString(Args, TEXT("blueprint")), Error);
				if (!Blueprint)
				{
					return FNameTBDAIToolResult::Fail(Error);
				}
				const bool bDark = NameTBDAIHelpers::IsDark(GetString(Args, TEXT("mode")));
				Blueprint->Modify();
				const TSharedPtr<FJsonObject> Remembered = GetObject(LoadRememberedRoles(), Blueprint->GetPathName());
				TArray<FString> Errors;
				TArray<FString> Repainted;
				TArray<UWidget*> Widgets;
				Blueprint->WidgetTree->GetAllWidgets(Widgets);
				for (UWidget* Widget : Widgets)
				{
					TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
					const FString RoleName = Remembered.IsValid() ? GetString(Remembered, Widget->GetName()) : FString();
					if (RoleName.StartsWith(EngineThemeMark) || (RoleName.IsEmpty() && NameTBDAIWidgetStyling::IsThemedEngineWidget(Widget) && GetBool(Args, TEXT("engine_widgets"), true)))
					{
						// An engine widget in the house look: themed again in the other palette.
						NameTBDAIWidgetStyling::ThemeEngineWidget(Widget, bDark, NameTBDAIWidgetStyling::ParseVariant(RoleName.Mid(FCString::Strlen(EngineThemeMark))));
						Repainted.Add(Widget->GetName());
						continue;
					}
					if (TSharedPtr<FJsonObject> Role = RoleName.IsEmpty() ? nullptr : Copy(FindRole(RoleName)))
					{
						const TSharedPtr<FJsonObject> RoleProps = GetObject(Role, TEXT("props"));
						if (TSharedPtr<FJsonValue> Colors = RoleProps.IsValid() ? KeepColorTokens(MakeShared<FJsonValueObject>(RoleProps)) : nullptr)
						{
							Props = Colors->AsObject();
						}
					}
					if (Cast<UStyledButtonBase>(Widget))
					{
						Props->SetStringField(TEXT("AccentColor"), TEXT("@accent"));
					}
					if (Props->Values.Num() == 0)
					{
						continue;
					}
					NameTBDAIHelpers::ApplyWidgetProps(Widget, Props, nullptr, bDark, Errors);
					Repainted.Add(Widget->GetName());
				}
				CheckContrast(Widgets, bDark, Errors);
				Context.Changes->NoteTouched(Blueprint, FString::Printf(TEXT("Applied the %s theme to %s"), bDark ? TEXT("dark") : TEXT("light"), *Blueprint->GetName()));
				FNameTBDAIToolResult Result = FNameTBDAIToolResult::Ok();
				Result.Data->SetField(TEXT("repainted"), Arr(Repainted));
				if (Repainted.Num() == 0)
				{
					Result.Data->SetStringField(TEXT("note"), TEXT("No widget here was built with a house role: set colors with set_widget and theme tokens (@surface, @text...)."));
				}
				Result.Warnings = Errors;
				FinishBlueprintEdit(Blueprint, Result, false);
				return Result;
			}
		});

		// ---- open_asset ----
		Register({
			TEXT("open_asset"),
			TEXT("Opens an asset's editor (a Widget Blueprint opens its designer) so the user sees it."),
			FNameTBDAISchema().Str(TEXT("asset"), TEXT("Asset path or name (omit: the one built last)."), false).Build(),
			false, true, false,
			[](const TSharedPtr<FJsonObject>& Args, FNameTBDAIToolContext&)
			{
				FString Error;
				const FString Name = GetString(Args, TEXT("asset"));
				UObject* Asset = Name.IsEmpty() ? GetLastAsset() : NameTBDAIHelpers::FindAsset(Name, nullptr, Error);
				if (!Asset)
				{
					return FNameTBDAIToolResult::Fail(Error.IsEmpty() ? FString(TEXT("Nothing was built yet.")) : Error);
				}
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Asset);
				return FNameTBDAIToolResult::Ok();
			}
		});
	}
}
