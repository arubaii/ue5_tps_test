#include "NameTBDStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/AppStyle.h"
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

    return Style;
}