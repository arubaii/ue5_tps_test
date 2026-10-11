#include "NameTBDStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateTypes.h"
#include "Brushes/SlateImageBrush.h"
#include "NameTBDColors.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

TSharedPtr<FSlateStyleSet> FNameTBDStyle::StyleInstance = nullptr;

void FNameTBDStyle::Initialize()
{
    if (!StyleInstance.IsValid())
    {
       StyleInstance = Create();
       FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
    }
}

void FNameTBDStyle::Shutdown()
{
    if (StyleInstance.IsValid())
    {
       FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
       StyleInstance.Reset();
    }
}

FName FNameTBDStyle::GetStyleSetName()
{
    static FName StyleSetName(TEXT("NameTBDStyle"));
    return StyleSetName;
}

const ISlateStyle& FNameTBDStyle::Get()
{
    return *StyleInstance;
}

const FSlateBrush* FNameTBDStyle::FindIcon(FName Name)
{
    return StyleInstance.IsValid() ? StyleInstance->GetOptionalBrush(Name, nullptr, nullptr) : nullptr;
}

TSharedRef<FSlateStyleSet> FNameTBDStyle::Create()
{
    TSharedRef<FSlateStyleSet> Style = MakeShareable(new FSlateStyleSet(GetStyleSetName()));

    bool bAppliedCustomIcon = false;

    if (TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("NameTBD")))
    {
       FString SVGFilePath = Plugin->GetBaseDir() / TEXT("Resources/shooting-star-class-icon.svg");

       if (FPaths::FileExists(SVGFilePath))
       {
          UE_LOG(LogTemp, Log, TEXT("FNameTBDStyle: Found custom SVG asset at %s"), *SVGFilePath);

          Style->Set(
             "ClassIcon.WidgetAnimPresetAsset",
             new FSlateVectorImageBrush(SVGFilePath, FVector2D(16.0f, 16.0f))
          );

          bAppliedCustomIcon = true;
       }
       else
       {
          UE_LOG(LogTemp, Warning, TEXT("FNameTBDStyle: SVG file not found at %s"), *SVGFilePath);
       }
    }
    else
    {
       UE_LOG(LogTemp, Warning, TEXT("FNameTBDStyle: Could not find plugin 'NameTBD'."));
    }

    if (!bAppliedCustomIcon)
    {
       UE_LOG(LogTemp, Warning, TEXT("FNameTBDStyle: Falling back to tinted DataAsset icon."));

       const FSlateBrush* DefaultDataAssetBrush = FAppStyle::GetBrush("ClassIcon.DataAsset");
       FString EngineIconPath = DefaultDataAssetBrush->GetResourceName().ToString();

       Style->Set(
          "ClassIcon.WidgetAnimPresetAsset",
          new FSlateVectorImageBrush(
             EngineIconPath,
             FVector2D(16.0f, 16.0f),
             FLinearColor(FColor(235, 168, 52))
          )
       );
    }

    // The Widget Animations panel's own icon - its tab, and its drawer on the status bar: the plugin's shooting star
    // in the editor's icon grays. The Details tab's icon stands in if the file is missing.
    {
       const TSharedPtr<IPlugin> IconPlugin = IPluginManager::Get().FindPlugin(TEXT("NameTBD"));
       const FString EditorIconPath = IconPlugin.IsValid() ? IconPlugin->GetBaseDir() / TEXT("Resources/widget-animations-editor-icon.svg") : FString();
       if (!EditorIconPath.IsEmpty() && FPaths::FileExists(EditorIconPath))
       {
          Style->Set(WidgetAnimationsIcon, new FSlateVectorImageBrush(EditorIconPath, FVector2D(16.0f, 16.0f)));
       }
       else
       {
          Style->Set(WidgetAnimationsIcon, new FSlateBrush(*FAppStyle::GetBrush("LevelEditor.Tabs.Details")));
       }
    }

    // Menu icons in a color of their own. A menu draws its icons in its foreground color, which can't be set per
    // entry, but it multiplies a brush's own tint: so a copy of the editor's icon with a tint shows in that color,
    // dimmer at rest and full on hover, as the menu's other icons brighten.
    auto AddTintedCopy = [&Style](const FName Name, const FName EditorIcon, const FLinearColor& Tint)
    {
       FSlateBrush* Brush = new FSlateBrush(*FAppStyle::GetBrush(EditorIcon));
       Brush->TintColor = FSlateColor(Tint);
       Style->Set(Name, Brush);
    };
    AddTintedCopy(MenuAddIcon, "Icons.Plus", NameTBD::Colors::WidgetAnimEditor::MenuAddIconTint);
    AddTintedCopy(MenuDeleteIcon, "Icons.Delete", NameTBD::Colors::WidgetAnimEditor::MenuDeleteIconTint);

    // Check boxes as menu icons - see MenuCheckedIcon. The plugin's own SVGs; the editor's check box images if those are missing.
    {
       const TSharedPtr<IPlugin> CheckPlugin = IPluginManager::Get().FindPlugin(TEXT("NameTBD"));
       const FCheckBoxStyle& EditorCheckBox = FAppStyle::Get().GetWidgetStyle<FCheckBoxStyle>("Checkbox");
       auto AddCheckIcon = [&Style, &CheckPlugin](const FName Name, const TCHAR* File, const FSlateBrush& Fallback)
       {
          const FString Path = CheckPlugin.IsValid() ? CheckPlugin->GetBaseDir() / TEXT("Resources") / File : FString();
          if (!Path.IsEmpty() && FPaths::FileExists(Path))
          {
             Style->Set(Name, new FSlateVectorImageBrush(Path, FVector2D(16.0f, 16.0f)));
          }
          else
          {
             FSlateBrush* Brush = new FSlateBrush(Fallback);
             Brush->ImageSize = FVector2D(16.0f, 16.0f);
             Style->Set(Name, Brush);
          }
       };
       AddCheckIcon(MenuCheckedIcon, TEXT("WidgetAnim_MenuChecked.svg"), EditorCheckBox.CheckedImage);
       AddCheckIcon(MenuUncheckedIcon, TEXT("WidgetAnim_MenuUnchecked.svg"), EditorCheckBox.UncheckedImage);
    }

    // The State Machine editor's Exit node. Left unset when the file is missing: the node falls back to an editor icon.
    {
       const TSharedPtr<IPlugin> ExitPlugin = IPluginManager::Get().FindPlugin(TEXT("NameTBD"));
       const FString ExitPath = ExitPlugin.IsValid() ? ExitPlugin->GetBaseDir() / TEXT("Resources/WidgetAnim_StateMachineExit.svg") : FString();
       if (!ExitPath.IsEmpty() && FPaths::FileExists(ExitPath))
       {
          Style->Set(StateMachineExitIcon, new FSlateVectorImageBrush(ExitPath, FVector2D(16.0f, 16.0f)));
       }
    }

    // The Custom Shape path editor's icons, Save and the Graph Editor. Each left unset when its file is missing - see FindIcon.
    {
       const TSharedPtr<IPlugin> IconsPlugin = IPluginManager::Get().FindPlugin(TEXT("NameTBD"));
       const TPair<FName, const TCHAR*> Icons[] =
       {
          { ShapeSelectIcon, TEXT("ShapeEdit_Select.svg") },
          { ShapePenIcon, TEXT("ShapeEdit_Pen.svg") },
          { ShapeStraightIcon, TEXT("ShapeEdit_Straight.svg") },
          { ShapeMirroredIcon, TEXT("ShapeEdit_Mirrored.svg") },
          { ShapeAsymmetricIcon, TEXT("ShapeEdit_Asymmetric.svg") },
          { ShapeDisconnectedIcon, TEXT("ShapeEdit_Disconnected.svg") },
          { ShapeClosePathIcon, TEXT("ShapeEdit_ClosePath.svg") },
          { ShapeOpenPathIcon, TEXT("ShapeEdit_OpenPath.svg") },
          { ShapeReverseIcon, TEXT("ShapeEdit_Reverse.svg") },
          { ShapeDeleteIcon, TEXT("ShapeEdit_Delete.svg") },
          { ShapeDoneIcon, TEXT("ShapeEdit_Done.svg") },
          { ShapeEditIcon, TEXT("ShapeEdit_EditShape.svg") },
          { SaveIcon, TEXT("WidgetAnim_Save.svg") },
          { GraphEditorIcon, TEXT("WidgetAnim_GraphEditor.svg") },
          { RangeMarkerIcon, TEXT("WidgetAnim_RangeMarker.svg") },
          { ParameterBindIcon, TEXT("WidgetAnim_ParameterBind.svg") },
          { ParameterBoundIcon, TEXT("WidgetAnim_ParameterBound.svg") },
          { ParameterMirroredIcon, TEXT("WidgetAnim_ParameterMirrored.svg") },
          { StateMachineActionsIcon, TEXT("WidgetAnim_StateMachineActions.svg") },
          { TimelineIcon, TEXT("WidgetAnim_Timeline.svg") },
          { TrackSheetIcon, TEXT("WidgetAnim_TrackSheet.svg") },
          { DopeSheetIcon, TEXT("WidgetAnim_DopeSheet.svg") },
       };
       for (const TPair<FName, const TCHAR*>& Icon : Icons)
       {
          const FString Path = IconsPlugin.IsValid() ? IconsPlugin->GetBaseDir() / TEXT("Resources") / Icon.Value : FString();
          if (!Path.IsEmpty() && FPaths::FileExists(Path))
          {
             Style->Set(Icon.Key, new FSlateVectorImageBrush(Path, FVector2D(16.0f, 16.0f)));
          }
       }
    }

    return Style;
}