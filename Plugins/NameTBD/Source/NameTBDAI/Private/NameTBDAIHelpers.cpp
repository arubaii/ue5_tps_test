// Copyright AR GameSystems 2026. All rights reserved.

#include "NameTBDAIHelpers.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/Widget.h"
#include "Editor.h"
#include "IAssetTools.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "NameTBDAIJson.h"
#include "NameTBDAISettings.h"
#include "NameTBDAITheme.h"
#include "NameTBDAITools.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintEditor.h"
#include "WidgetSpringPresets.h"

// UI components are added to a Widget Blueprint through UMG's own blueprint extension - the one the designer's Add Component
// uses. Its header has moved between engine versions; without it, components can't be added from here (only edited).
#if __has_include("Extensions/UIComponentWidgetBlueprintExtension.h")
#include "Extensions/UIComponentWidgetBlueprintExtension.h"
#define NAMETBDAI_UI_COMPONENT_EXTENSION 1
#elif __has_include("UIComponentWidgetBlueprintExtension.h")
#include "UIComponentWidgetBlueprintExtension.h"
#define NAMETBDAI_UI_COMPONENT_EXTENSION 1
#else
#define NAMETBDAI_UI_COMPONENT_EXTENSION 0
#endif
#include "Extensions/UIComponent.h"

namespace NameTBDAIHelpers
{
	// =================================================================================================================
	// Assets
	// =================================================================================================================

	UObject* FindAsset(const FString& InPathOrName, const UClass* Class, FString& OutError)
	{
		const FString PathOrName = InPathOrName.TrimStartAndEnd();
		if (PathOrName.IsEmpty())
		{
			OutError = TEXT("No asset was named.");
			return nullptr;
		}
		const UClass* Wanted = Class ? Class : UObject::StaticClass();

		if (PathOrName.StartsWith(TEXT("/")))
		{
			FString ObjectPath = PathOrName;
			if (!ObjectPath.Contains(TEXT(".")))
			{
				ObjectPath += TEXT(".") + FPaths::GetBaseFilename(ObjectPath);
			}
			UObject* Found = StaticLoadObject(const_cast<UClass*>(Wanted), nullptr, *ObjectPath, nullptr, LOAD_Quiet | LOAD_NoWarn);
			if (!Found)
			{
				OutError = FString::Printf(TEXT("No %s at '%s'."), *Wanted->GetName(), *PathOrName);
			}
			return Found;
		}

		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		FARFilter Filter;
		Filter.ClassPaths.Add(Wanted->GetClassPathName());
		Filter.bRecursiveClasses = true;
		TArray<FAssetData> Assets;
		Registry.GetAssets(Filter, Assets);

		const FString Key = NameTBDAIJson::Normalize(PathOrName);
		const FAssetData* Best = nullptr;
		for (const FAssetData& Asset : Assets)
		{
			const bool bExact = Asset.AssetName.ToString().Equals(PathOrName, ESearchCase::IgnoreCase);
			const bool bLoose = NameTBDAIJson::Normalize(Asset.AssetName.ToString()) == Key;
			if (!bExact && !bLoose)
			{
				continue;
			}
			// The project's own over a plugin's, an exact name over a loose one.
			const bool bProject = Asset.PackageName.ToString().StartsWith(TEXT("/Game/"));
			const bool bBestProject = Best && Best->PackageName.ToString().StartsWith(TEXT("/Game/"));
			if (!Best || (bProject && !bBestProject) || (bExact && bProject == bBestProject))
			{
				Best = &Asset;
			}
		}
		if (!Best)
		{
			OutError = FString::Printf(TEXT("No %s called '%s' (find_assets lists them)."), *Wanted->GetName(), *PathOrName);
			return nullptr;
		}
		return Best->GetAsset();
	}

	FString ResolveFolder(const FString& InFolder)
	{
		FString Folder = InFolder.TrimStartAndEnd().Replace(TEXT("\\"), TEXT("/"));
		const FString Output = UNameTBDAISettings::Get().GetOutputFolder();
		if (Folder.IsEmpty())
		{
			return Output;
		}
		if (!Folder.StartsWith(TEXT("/")))
		{
			Folder = Output / Folder;
		}
		while (Folder.EndsWith(TEXT("/")))
		{
			Folder.LeftChopInline(1);
		}
		return Folder;
	}

	FString MakeAssetName(const FString& InName, const FString& Prefix)
	{
		FString Name;
		bool bUpperNext = true;
		for (const TCHAR Char : InName.TrimStartAndEnd())
		{
			if (FChar::IsAlnum(Char) || Char == TEXT('_'))
			{
				Name.AppendChar(bUpperNext ? FChar::ToUpper(Char) : Char);
				bUpperNext = false;
			}
			else
			{
				bUpperNext = true;
			}
		}
		if (Name.IsEmpty())
		{
			Name = TEXT("New");
		}
		if (!Prefix.IsEmpty() && !Name.StartsWith(Prefix, ESearchCase::IgnoreCase))
		{
			Name = Prefix + Name;
		}
		return Name;
	}

	UObject* CreateAsset(const FString& Folder, const FString& Name, UClass* Class, UFactory* Factory, FNameTBDAIToolContext& Context, FString& OutError)
	{
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		FString PackageName;
		FString AssetName;
		AssetTools.CreateUniqueAssetName(ResolveFolder(Folder) / Name, FString(), PackageName, AssetName);
		UObject* Created = AssetTools.CreateAsset(AssetName, FPackageName::GetLongPackagePath(PackageName), Class, Factory);
		if (!Created)
		{
			OutError = FString::Printf(TEXT("Couldn't create %s in %s."), *AssetName, *ResolveFolder(Folder));
			return nullptr;
		}
		Context.Changes->NoteCreated(Created, FString::Printf(TEXT("Created %s"), *Created->GetName()));
		return Created;
	}

	UObject* DuplicateAsset(UObject* Source, const FString& Folder, const FString& Name, FNameTBDAIToolContext& Context, FString& OutError)
	{
		if (!Source)
		{
			OutError = TEXT("Nothing to copy.");
			return nullptr;
		}
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		FString PackageName;
		FString AssetName;
		AssetTools.CreateUniqueAssetName(ResolveFolder(Folder) / Name, FString(), PackageName, AssetName);
		UObject* Copy = AssetTools.DuplicateAsset(AssetName, FPackageName::GetLongPackagePath(PackageName), Source);
		if (!Copy)
		{
			OutError = FString::Printf(TEXT("Couldn't copy %s."), *Source->GetName());
			return nullptr;
		}
		Context.Changes->NoteCreated(Copy, FString::Printf(TEXT("Created %s (from %s)"), *Copy->GetName(), *Source->GetName()));
		return Copy;
	}

	// =================================================================================================================
	// Widget Blueprints
	// =================================================================================================================

	FWidgetBlueprintEditor* FindOpenEditor(const UWidgetBlueprint* Blueprint)
	{
		UAssetEditorSubsystem* AssetEditors = GEditor && Blueprint ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
		IAssetEditorInstance* Instance = AssetEditors ? AssetEditors->FindEditorForAsset(const_cast<UWidgetBlueprint*>(Blueprint), /*bFocusIfOpen=*/false) : nullptr;
		if (!Instance || Instance->GetEditorName() != FName(TEXT("WidgetBlueprintEditor")))
		{
			return nullptr;
		}
		return static_cast<FWidgetBlueprintEditor*>(Instance);
	}

	UWidgetBlueprint* GetFocusedWidgetBlueprint()
	{
		UAssetEditorSubsystem* AssetEditors = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
		if (!AssetEditors)
		{
			return nullptr;
		}
		UWidgetBlueprint* Best = nullptr;
		double BestTime = -1.0;
		for (UObject* Asset : AssetEditors->GetAllEditedAssets())
		{
			UWidgetBlueprint* Blueprint = Cast<UWidgetBlueprint>(Asset);
			IAssetEditorInstance* Instance = Blueprint ? AssetEditors->FindEditorForAsset(Blueprint, false) : nullptr;
			if (Instance && Instance->GetLastActivationTime() > BestTime)
			{
				BestTime = Instance->GetLastActivationTime();
				Best = Blueprint;
			}
		}
		return Best;
	}

	UWidgetBlueprint* FindWidgetBlueprint(const FString& PathOrName, FString& OutError)
	{
		if (PathOrName.TrimStartAndEnd().IsEmpty())
		{
			if (UWidgetBlueprint* Last = Cast<UWidgetBlueprint>(NameTBDAITools::GetLastAsset()))
			{
				return Last;
			}
			if (UWidgetBlueprint* Focused = GetFocusedWidgetBlueprint())
			{
				return Focused;
			}
			OutError = TEXT("No Widget Blueprint was named, none was built yet, and none is open.");
			return nullptr;
		}
		return Cast<UWidgetBlueprint>(FindAsset(PathOrName, UWidgetBlueprint::StaticClass(), OutError));
	}

	UWidget* FindWidget(UWidgetBlueprint* Blueprint, const FString& InName, FString& OutError)
	{
		UWidgetTree* Tree = Blueprint ? Blueprint->WidgetTree.Get() : nullptr;
		if (!Tree)
		{
			OutError = TEXT("The Widget Blueprint has no widget tree.");
			return nullptr;
		}
		const FString Name = InName.TrimStartAndEnd();
		if (Name.IsEmpty() || Name.Equals(TEXT("root"), ESearchCase::IgnoreCase))
		{
			if (!Tree->RootWidget)
			{
				OutError = TEXT("The Widget Blueprint is empty.");
			}
			return Tree->RootWidget;
		}
		if (UWidget* Exact = Tree->FindWidget(FName(*Name)))
		{
			return Exact;
		}
		TArray<UWidget*> Widgets;
		Tree->GetAllWidgets(Widgets);
		const FString Key = NameTBDAIJson::Normalize(Name);
		TArray<FString> Names;
		for (UWidget* Widget : Widgets)
		{
			if (NameTBDAIJson::Normalize(Widget->GetName()) == Key || NameTBDAIJson::Normalize(Widget->GetDisplayLabel()) == Key)
			{
				return Widget;
			}
			Names.Add(Widget->GetName());
		}
		OutError = FString::Printf(TEXT("No widget '%s' in %s. Widgets: %s."), *Name, *Blueprint->GetName(), *FString::Join(Names, TEXT(", ")));
		return nullptr;
	}

	FName MakeWidgetName(UWidgetBlueprint* Blueprint, const FString& Base)
	{
		FString Clean;
		for (const TCHAR Char : Base)
		{
			if (FChar::IsAlnum(Char) || Char == TEXT('_'))
			{
				Clean.AppendChar(Char);
			}
		}
		if (Clean.IsEmpty() || FChar::IsDigit(Clean[0]))
		{
			Clean = TEXT("Widget") + Clean;
		}

		UWidgetTree* Tree = Blueprint->WidgetTree;
		auto IsFree = [Tree, Blueprint](const FString& Candidate)
		{
			const FName CandidateName(*Candidate);
			if (Tree->FindWidget(CandidateName))
			{
				return false;
			}
			// Also not a name an object inside the tree already has (hidden parts, leftovers).
			if (StaticFindObjectFast(UObject::StaticClass(), Tree, CandidateName))
			{
				return false;
			}
			return FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, CandidateName) == INDEX_NONE;
		};
		if (IsFree(Clean))
		{
			return FName(*Clean);
		}
		for (int32 Suffix = 1; Suffix < 10000; ++Suffix)
		{
			const FString Candidate = FString::Printf(TEXT("%s_%d"), *Clean, Suffix);
			if (IsFree(Candidate))
			{
				return FName(*Candidate);
			}
		}
		return MakeUniqueObjectName(Tree, UWidget::StaticClass(), FName(*Clean));
	}

	namespace
	{
		FMapProperty* GetGuidMapProperty()
		{
			return FindFProperty<FMapProperty>(UWidgetBlueprint::StaticClass(), TEXT("WidgetVariableNameToGuidMap"));
		}
	}

	void GiveGuids(UWidgetBlueprint* Blueprint, UWidget* Widget)
	{
		FMapProperty* MapProperty = GetGuidMapProperty();
		if (!Blueprint || !Widget || !MapProperty)
		{
			return;
		}
		FScriptMapHelper Helper(MapProperty, MapProperty->ContainerPtrToValuePtr<void>(Blueprint));
		UWidgetTree::ForWidgetAndChildren(Widget, [&Helper, Blueprint](UWidget* Each)
		{
			// Only widgets that are the Blueprint's own: a ready-made button's hidden parts live in a tree of the button's.
			if (!Each || Each->GetTypedOuter<UWidgetTree>() != Blueprint->WidgetTree)
			{
				return;
			}
			const FName Key = Each->GetFName();
			if (!Helper.FindValueFromHash(&Key))
			{
				const FGuid Guid = FGuid::NewGuid();
				Helper.AddPair(&Key, &Guid);
			}
		});
	}

	void DropGuids(UWidgetBlueprint* Blueprint, UWidget* Widget)
	{
		FMapProperty* MapProperty = GetGuidMapProperty();
		if (!Blueprint || !Widget || !MapProperty)
		{
			return;
		}
		FScriptMapHelper Helper(MapProperty, MapProperty->ContainerPtrToValuePtr<void>(Blueprint));
		UWidgetTree::ForWidgetAndChildren(Widget, [&Helper](UWidget* Each)
		{
			if (Each)
			{
				const FName Key = Each->GetFName();
				Helper.RemovePair(&Key);
			}
		});
	}

	bool Compile(UWidgetBlueprint* Blueprint, TArray<FString>& OutProblems)
	{
		if (!Blueprint)
		{
			return false;
		}
		FCompilerResultsLog Results;
		Results.bSilentMode = true;
		Results.SetSourcePath(Blueprint->GetPathName());
		FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &Results);
		for (const TSharedRef<FTokenizedMessage>& Message : Results.Messages)
		{
			const EMessageSeverity::Type Severity = Message->GetSeverity();
			if (Severity == EMessageSeverity::Error || Severity == EMessageSeverity::Warning || Severity == EMessageSeverity::PerformanceWarning)
			{
				FString Text = Message->ToText().ToString();
				if (Text.Len() > 240)
				{
					Text = Text.Left(240) + TEXT("...");
				}
				OutProblems.Add(FString::Printf(TEXT("%s: %s"), Severity == EMessageSeverity::Error ? TEXT("compile error") : TEXT("compile warning"), *Text));
			}
		}
		return Results.NumErrors == 0 && Blueprint->Status != BS_Error;
	}

	UUIComponent* FindOrAddComponent(UWidgetBlueprint* Blueprint, UWidget* Widget, UClass* Class, bool bAdd, FString& OutError)
	{
		if (!Blueprint || !Widget || !Class)
		{
			OutError = TEXT("Nothing to add a component to.");
			return nullptr;
		}
#if NAMETBDAI_UI_COMPONENT_EXTENSION
		UUIComponentWidgetBlueprintExtension* Extension = UWidgetBlueprintExtension::RequestExtension<UUIComponentWidgetBlueprintExtension>(Blueprint);
		if (!Extension)
		{
			OutError = TEXT("This Widget Blueprint can't hold UI components.");
			return nullptr;
		}
		if (UUIComponent* Existing = Extension->GetComponent(Class, Widget->GetFName()))
		{
			return Existing;
		}
		if (!bAdd)
		{
			OutError = FString::Printf(TEXT("%s has no %s."), *Widget->GetName(), *Class->GetName());
			return nullptr;
		}
		Blueprint->Modify();
		Extension->Modify();
		UUIComponent* Added = Extension->AddComponent(Class, Widget->GetFName());
		if (!Added)
		{
			OutError = FString::Printf(TEXT("Couldn't add a %s to %s."), *Class->GetName(), *Widget->GetName());
			return nullptr;
		}
		Added->SetFlags(RF_Transactional);
		return Added;
#else
		OutError = TEXT("UI components can't be added from here in this engine version: add it with the designer's Add Component, then this tool can edit it.");
		return nullptr;
#endif
	}

	TArray<UWidget*> GetSelectedWidgets(const UWidgetBlueprint* Blueprint)
	{
		TArray<UWidget*> Out;
		if (FWidgetBlueprintEditor* Editor = FindOpenEditor(Blueprint))
		{
			for (const FWidgetReference& Reference : Editor->GetSelectedWidgets())
			{
				if (UWidget* Template = Reference.GetTemplate())
				{
					Out.Add(Template);
				}
			}
		}
		return Out;
	}

	TSharedPtr<FJsonObject> DescribeWidget(UWidget* Widget, bool bProps)
	{
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		if (!Widget)
		{
			return Out;
		}
		Out->SetStringField(TEXT("name"), Widget->GetName());
		Out->SetStringField(TEXT("class"), Widget->GetClass()->ClassGeneratedBy ? Widget->GetClass()->GetPathName() : Widget->GetClass()->GetName());
		if (UPanelWidget* Parent = Widget->GetParent())
		{
			Out->SetStringField(TEXT("parent"), Parent->GetName());
		}
		if (bProps)
		{
			const TSharedPtr<FJsonObject> Props = NameTBDAIJson::ReadObjectNonDefault(Widget, 3);
			if (Props->Values.Num() > 0)
			{
				Out->SetObjectField(TEXT("props"), Props);
			}
			if (Widget->Slot)
			{
				const TSharedPtr<FJsonObject> SlotProps = NameTBDAIJson::ReadObjectNonDefault(Widget->Slot, 3);
				SlotProps->SetStringField(TEXT("$class"), Widget->Slot->GetClass()->GetName());
				Out->SetObjectField(TEXT("slot"), SlotProps);
			}
		}
		return Out;
	}

	namespace
	{
		TSharedPtr<FJsonObject> DescribeBranch(UWidget* Widget, bool bProps, int32& Budget)
		{
			TSharedPtr<FJsonObject> Out = DescribeWidget(Widget, bProps);
			Out->RemoveField(TEXT("parent"));
			--Budget;
			if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
			{
				TArray<TSharedPtr<FJsonValue>> Children;
				for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
				{
					UWidget* Child = Panel->GetChildAt(Index);
					if (!Child)
					{
						continue;
					}
					if (Budget <= 0)
					{
						Children.Add(NameTBDAIJson::Str(FString::Printf(TEXT("... %d more"), Panel->GetChildrenCount() - Index)));
						break;
					}
					Children.Add(NameTBDAIJson::Obj(DescribeBranch(Child, bProps, Budget)));
				}
				if (Children.Num() > 0)
				{
					Out->SetArrayField(TEXT("children"), Children);
				}
			}
			return Out;
		}
	}

	TSharedPtr<FJsonObject> DescribeTree(UWidgetBlueprint* Blueprint, bool bProps, int32 MaxWidgets)
	{
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		if (!Blueprint)
		{
			return Out;
		}
		Out->SetStringField(TEXT("asset"), Blueprint->GetPathName());
		Out->SetStringField(TEXT("parent_class"), Blueprint->ParentClass ? Blueprint->ParentClass->GetName() : FString());
		if (Blueprint->WidgetTree && Blueprint->WidgetTree->RootWidget)
		{
			int32 Budget = MaxWidgets;
			Out->SetObjectField(TEXT("root"), DescribeBranch(Blueprint->WidgetTree->RootWidget, bProps, Budget));
		}
		return Out;
	}

	namespace
	{
		/** "top_left" -> horizontal and vertical alignment names, plus the canvas anchor. */
		bool ParseAlign(const FString& Text, FString& OutH, FString& OutV, FVector2D& OutAnchor)
		{
			const FString Key = Text.ToLower().Replace(TEXT("-"), TEXT("_")).Replace(TEXT(" "), TEXT("_"));
			OutH = TEXT("Fill");
			OutV = TEXT("Fill");
			OutAnchor = FVector2D(0.5, 0.5);
			bool bAny = false;
			TArray<FString> Words;
			Key.ParseIntoArray(Words, TEXT("_"));
			bool bVertical = false;
			bool bHorizontal = false;
			for (const FString& Word : Words)
			{
				if (Word == TEXT("top")) { OutV = TEXT("Top"); OutAnchor.Y = 0.0; bVertical = true; bAny = true; }
				else if (Word == TEXT("bottom")) { OutV = TEXT("Bottom"); OutAnchor.Y = 1.0; bVertical = true; bAny = true; }
				else if (Word == TEXT("left")) { OutH = TEXT("Left"); OutAnchor.X = 0.0; bHorizontal = true; bAny = true; }
				else if (Word == TEXT("right")) { OutH = TEXT("Right"); OutAnchor.X = 1.0; bHorizontal = true; bAny = true; }
				else if (Word == TEXT("center") || Word == TEXT("middle"))
				{
					// "center" alone centers both ways; with a side it centers the other.
					bAny = true;
				}
				else if (Word == TEXT("fill") || Word == TEXT("stretch")) { bAny = true; }
			}
			const bool bHasCenter = Words.Contains(TEXT("center")) || Words.Contains(TEXT("middle"));
			if (bHasCenter)
			{
				if (!bHorizontal) { OutH = TEXT("Center"); }
				if (!bVertical) { OutV = TEXT("Center"); }
			}
			return bAny;
		}
	}

	void ApplyWidgetProps(UWidget* Widget, TSharedPtr<FJsonObject> Props, TSharedPtr<FJsonObject> Slot, bool bDark, TArray<FString>& OutErrors)
	{
		if (!Widget)
		{
			return;
		}
		if (Props.IsValid())
		{
			ExpandThemeTokens(Props, bDark);
			NameTBDAIJson::ApplyToObject(Widget, Props, OutErrors);
		}
		if (Slot.IsValid() && Slot->Values.Num() > 0)
		{
			if (!Widget->Slot)
			{
				OutErrors.Add(FString::Printf(TEXT("%s has no slot (it's the root): slot settings ignored."), *Widget->GetName()));
				return;
			}
			ExpandThemeTokens(Slot, bDark);
			TSharedPtr<FJsonObject> SlotProps = MakeShared<FJsonObject>();
			SlotProps->Values = Slot->Values;

			FString Align;
			if (SlotProps->TryGetStringField(TEXT("align"), Align))
			{
				SlotProps->RemoveField(TEXT("align"));
				FString H, V;
				FVector2D Anchor;
				if (!ParseAlign(Align, H, V, Anchor))
				{
					OutErrors.Add(FString::Printf(TEXT("align '%s': use top_left, top, top_right, left, center, right, bottom_left, bottom, bottom_right or fill."), *Align));
				}
				else if (Cast<UCanvasPanelSlot>(Widget->Slot))
				{
					const bool bFill = Align.ToLower().Contains(TEXT("fill"));
					TSharedPtr<FJsonObject> Anchors = MakeShared<FJsonObject>();
					TArray<TSharedPtr<FJsonValue>> Min = { NameTBDAIJson::Num(bFill ? 0.0 : Anchor.X), NameTBDAIJson::Num(bFill ? 0.0 : Anchor.Y) };
					TArray<TSharedPtr<FJsonValue>> Max = { NameTBDAIJson::Num(bFill ? 1.0 : Anchor.X), NameTBDAIJson::Num(bFill ? 1.0 : Anchor.Y) };
					Anchors->SetArrayField(TEXT("Minimum"), Min);
					Anchors->SetArrayField(TEXT("Maximum"), Max);
					SlotProps->SetObjectField(TEXT("LayoutData.Anchors"), Anchors);
					TArray<TSharedPtr<FJsonValue>> Alignment = { NameTBDAIJson::Num(Anchor.X), NameTBDAIJson::Num(Anchor.Y) };
					SlotProps->SetArrayField(TEXT("LayoutData.Alignment"), Alignment);
					if (bFill)
					{
						SlotProps->SetNumberField(TEXT("LayoutData.Offsets"), 0.0);
					}
				}
				else
				{
					if (NameTBDAIJson::FindField(Widget->Slot->GetClass(), TEXT("HorizontalAlignment")))
					{
						SlotProps->SetStringField(TEXT("HorizontalAlignment"), H);
					}
					if (NameTBDAIJson::FindField(Widget->Slot->GetClass(), TEXT("VerticalAlignment")))
					{
						SlotProps->SetStringField(TEXT("VerticalAlignment"), V);
					}
				}
			}
			NameTBDAIJson::ApplyToObject(Widget->Slot, SlotProps, OutErrors);
			Widget->Slot->SynchronizeProperties();
		}
	}

	// =================================================================================================================
	// Theme
	// =================================================================================================================

	bool IsDark(const FString& Mode)
	{
		const FString Key = Mode.ToLower();
		if (Key.Contains(TEXT("light")) || Key.Contains(TEXT("day")))
		{
			return false;
		}
		if (Key.Contains(TEXT("dark")) || Key.Contains(TEXT("night")))
		{
			return true;
		}
		return UNameTBDAITheme::Get().bDarkByDefault;
	}

	const FNameTBDAIPalette& GetPalette(bool bDark)
	{
		return UNameTBDAITheme::Get().GetPalette(bDark);
	}

	bool GetThemeColor(const FString& Role, bool bDark, FLinearColor& OutColor)
	{
		const FNameTBDAIPalette& Palette = GetPalette(bDark);
		const FString Key = NameTBDAIJson::Normalize(Role);
		struct FRole { const TCHAR* Key; const FLinearColor* Color; };
		const FRole Roles[] = {
			{ TEXT("background"), &Palette.Background }, { TEXT("bg"), &Palette.Background },
			{ TEXT("surface"), &Palette.Surface }, { TEXT("card"), &Palette.Surface },
			{ TEXT("raised"), &Palette.SurfaceRaised }, { TEXT("surfaceraised"), &Palette.SurfaceRaised },
			{ TEXT("outline"), &Palette.Outline }, { TEXT("border"), &Palette.Outline },
			{ TEXT("text"), &Palette.Text }, { TEXT("muted"), &Palette.TextMuted }, { TEXT("textmuted"), &Palette.TextMuted },
			{ TEXT("accent"), &Palette.Accent }, { TEXT("primary"), &Palette.Accent },
			{ TEXT("accent2"), &Palette.AccentAlt }, { TEXT("accentalt"), &Palette.AccentAlt }, { TEXT("secondary"), &Palette.AccentAlt },
			{ TEXT("glow1"), &Palette.Glow1 }, { TEXT("glow2"), &Palette.Glow2 }, { TEXT("glow3"), &Palette.Glow3 },
			{ TEXT("success"), &Palette.Success }, { TEXT("warning"), &Palette.Warning }, { TEXT("danger"), &Palette.Danger },
			{ TEXT("error"), &Palette.Danger },
		};
		for (const FRole& Pair : Roles)
		{
			if (Key == Pair.Key)
			{
				OutColor = *Pair.Color;
				return true;
			}
		}
		return false;
	}

	bool FindSpringPreset(const FString& Name, FWidgetSpringParams& OutSpring)
	{
		const FString Key = NameTBDAIJson::Normalize(Name);
		for (const WidgetSpringPresets::FBuiltIn& BuiltIn : WidgetSpringPresets::GetBuiltIns())
		{
			if (NameTBDAIJson::Normalize(BuiltIn.Name.ToString()) == Key)
			{
				OutSpring = BuiltIn.Spring;
				return true;
			}
		}
		for (const FWidgetSpringPreset& Preset : UWidgetSpringPresetSettings::Get().Presets)
		{
			if (NameTBDAIJson::Normalize(Preset.Name) == Key)
			{
				OutSpring = Preset.Spring;
				return true;
			}
		}
		return false;
	}

	void GetSpringPresetNames(TArray<FString>& OutNames)
	{
		for (const WidgetSpringPresets::FBuiltIn& BuiltIn : WidgetSpringPresets::GetBuiltIns())
		{
			OutNames.Add(BuiltIn.Name.ToString());
		}
		for (const FWidgetSpringPreset& Preset : UWidgetSpringPresetSettings::Get().Presets)
		{
			OutNames.Add(Preset.Name);
		}
	}

	namespace
	{
		/** "@accent", "@text~0.6", "@space.3", "@radius.large", "@type.title", "@font.strong" -> its value; null if not a token. */
		TSharedPtr<FJsonValue> ExpandToken(const FString& Text, bool bDark)
		{
			if (!Text.StartsWith(TEXT("@")) || Text.Len() < 2)
			{
				return nullptr;
			}
			FString Token = Text.Mid(1).TrimStartAndEnd();
			double AlphaScale = 1.0;
			int32 Tilde = INDEX_NONE;
			if (Token.FindChar(TEXT('~'), Tilde))
			{
				LexTryParseString(AlphaScale, *Token.Mid(Tilde + 1));
				Token = Token.Left(Tilde);
			}

			const UNameTBDAITheme& Theme = UNameTBDAITheme::Get();
			FString Group, Item;
			if (!Token.Split(TEXT("."), &Group, &Item))
			{
				Group = Token;
			}
			Group = Group.ToLower();
			Item = Item.ToLower();

			if (Group == TEXT("space") || Group == TEXT("spacing"))
			{
				return NameTBDAIJson::Num(Theme.GetSpacing(FCString::Atoi(*Item)));
			}
			if (Group == TEXT("radius"))
			{
				const float Radius = Item == TEXT("small") ? Theme.RadiusSmall : Item == TEXT("large") ? Theme.RadiusLarge : Item == TEXT("pill") ? 9999.f : Theme.RadiusMedium;
				return NameTBDAIJson::Num(Radius);
			}
			if (Group == TEXT("type") || Group == TEXT("size"))
			{
				const FNameTBDAITypeScale& Type = Theme.Type;
				const int32 Size = Item == TEXT("display") ? Type.Display : Item == TEXT("title") ? Type.Title : Item == TEXT("heading") ? Type.Heading
					: Item == TEXT("caption") ? Type.Caption : Type.Body;
				return NameTBDAIJson::Num(Size);
			}
			if (Group == TEXT("spring"))
			{
				FWidgetSpringParams Spring;
				if (FindSpringPreset(Item.IsEmpty() ? Theme.SpringPreset.ToString() : Item, Spring))
				{
					TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
					for (TFieldIterator<FProperty> It(FWidgetSpringParams::StaticStruct()); It; ++It)
					{
						Fields->SetField(It->GetAuthoredName(), NameTBDAIJson::ReadValue(*It, It->ContainerPtrToValuePtr<void>(&Spring), 2));
					}
					return NameTBDAIJson::Obj(Fields);
				}
				return nullptr;
			}
			if (Group == TEXT("font"))
			{
				return NameTBDAIJson::Str((Item == TEXT("strong") || Item == TEXT("bold") ? Theme.Type.StrongTypeface : Theme.Type.RegularTypeface).ToString());
			}

			FLinearColor Color;
			if (GetThemeColor(Group, bDark, Color))
			{
				Color.A = static_cast<float>(FMath::Clamp(Color.A * AlphaScale, 0.0, 1.0));
				return NameTBDAIJson::Str(NameTBDAIJson::ColorToHex(Color));
			}
			return nullptr;
		}

		void ExpandValue(TSharedPtr<FJsonValue>& Value, bool bDark)
		{
			if (!Value.IsValid())
			{
				return;
			}
			if (Value->Type == EJson::String)
			{
				if (TSharedPtr<FJsonValue> Expanded = ExpandToken(Value->AsString(), bDark))
				{
					Value = Expanded;
				}
			}
			else if (Value->Type == EJson::Object)
			{
				ExpandThemeTokens(Value->AsObject(), bDark);
			}
			else if (Value->Type == EJson::Array)
			{
				TArray<TSharedPtr<FJsonValue>> Items = Value->AsArray();
				for (TSharedPtr<FJsonValue>& Item : Items)
				{
					ExpandValue(Item, bDark);
				}
				Value = MakeShared<FJsonValueArray>(Items);
			}
		}
	}

	void ExpandThemeTokens(const TSharedPtr<FJsonObject>& Object, bool bDark)
	{
		if (!Object.IsValid())
		{
			return;
		}
		for (auto& Pair : Object->Values)
		{
			ExpandValue(Pair.Value, bDark);
		}
	}

	TSharedPtr<FJsonObject> ThemeToJson(bool bDark)
	{
		const UNameTBDAITheme& Theme = UNameTBDAITheme::Get();
		const FNameTBDAIPalette& Palette = Theme.GetPalette(bDark);
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("mode"), bDark ? TEXT("dark") : TEXT("light"));

		TSharedPtr<FJsonObject> Colors = MakeShared<FJsonObject>();
		struct FEntry { const TCHAR* Key; FLinearColor Value; };
		const FEntry Entries[] = {
			{ TEXT("background"), Palette.Background }, { TEXT("surface"), Palette.Surface }, { TEXT("raised"), Palette.SurfaceRaised },
			{ TEXT("outline"), Palette.Outline }, { TEXT("text"), Palette.Text }, { TEXT("muted"), Palette.TextMuted },
			{ TEXT("accent"), Palette.Accent }, { TEXT("accent2"), Palette.AccentAlt }, { TEXT("glow1"), Palette.Glow1 },
			{ TEXT("glow2"), Palette.Glow2 }, { TEXT("glow3"), Palette.Glow3 }, { TEXT("success"), Palette.Success },
			{ TEXT("warning"), Palette.Warning }, { TEXT("danger"), Palette.Danger },
		};
		for (const FEntry& Entry : Entries)
		{
			Colors->SetStringField(Entry.Key, NameTBDAIJson::ColorToHex(Entry.Value));
		}
		Out->SetObjectField(TEXT("colors"), Colors);

		TSharedPtr<FJsonObject> Type = MakeShared<FJsonObject>();
		Type->SetNumberField(TEXT("display"), Theme.Type.Display);
		Type->SetNumberField(TEXT("title"), Theme.Type.Title);
		Type->SetNumberField(TEXT("heading"), Theme.Type.Heading);
		Type->SetNumberField(TEXT("body"), Theme.Type.Body);
		Type->SetNumberField(TEXT("caption"), Theme.Type.Caption);
		Type->SetStringField(TEXT("strong_typeface"), Theme.Type.StrongTypeface.ToString());
		Type->SetStringField(TEXT("regular_typeface"), Theme.Type.RegularTypeface.ToString());
		Out->SetObjectField(TEXT("type"), Type);

		TArray<TSharedPtr<FJsonValue>> Spacing;
		for (const float Step : Theme.Spacing)
		{
			Spacing.Add(NameTBDAIJson::Num(Step));
		}
		Out->SetArrayField(TEXT("spacing"), Spacing);

		TSharedPtr<FJsonObject> Radius = MakeShared<FJsonObject>();
		Radius->SetNumberField(TEXT("small"), Theme.RadiusSmall);
		Radius->SetNumberField(TEXT("medium"), Theme.RadiusMedium);
		Radius->SetNumberField(TEXT("large"), Theme.RadiusLarge);
		Out->SetObjectField(TEXT("radius"), Radius);

		TSharedPtr<FJsonObject> Motion = MakeShared<FJsonObject>();
		Motion->SetStringField(TEXT("button_motion"), Theme.ButtonMotion.ToString());
		Motion->SetNumberField(TEXT("entrance_seconds"), Theme.EntranceDuration);
		Motion->SetNumberField(TEXT("stagger_seconds"), Theme.Stagger);
		Motion->SetStringField(TEXT("spring_preset"), Theme.SpringPreset.ToString());
		Out->SetObjectField(TEXT("motion"), Motion);

		TArray<TSharedPtr<FJsonValue>> CardSize = { NameTBDAIJson::Num(Theme.CardSize.X), NameTBDAIJson::Num(Theme.CardSize.Y) };
		Out->SetArrayField(TEXT("card_size"), CardSize);
		Out->SetStringField(TEXT("tokens"), TEXT("Use in any value: @accent, @text~0.6 (alpha x0.6), @space.3, @radius.large, @type.title, @font.strong"));
		return Out;
	}
}
