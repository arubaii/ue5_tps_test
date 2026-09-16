#include "NameTBDStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Interfaces/IPluginManager.h"

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

	// Point to the plugin's root /Resources folder
	FString ContentDir = IPluginManager::Get().FindPlugin(TEXT("NameTBD"))->GetBaseDir() / TEXT("Resources");
	Style->SetContentRoot(ContentDir);

	// Register the 16x16 Class Icon. 
	// NOTE: Replace "YourDataAssetClass" with the name of your C++ class, omitting the 'U' prefix.
	// E.g., for UNameTBDDataAsset, use "ClassIcon.NameTBDDataAsset"
	Style->Set("ClassIcon.YourDataAssetClass", new FSlateImageBrush(Style->RootToContentDir(TEXT("Icon16_NameTBD.png")), FVector2D(16.0f, 16.0f)));

	return Style;
}