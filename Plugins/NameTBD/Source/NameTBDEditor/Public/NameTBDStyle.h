#pragma once
#include "CoreMinimal.h"
#include "Styling/SlateStyle.h"

class FNameTBDStyle
{
public:
	static void Initialize();
	static void Shutdown();
	static void ReloadTextures();
	static const ISlateStyle& Get();
	static FName GetStyleSetName();

	/** Context menu icons tinted to say what they do: the green plus of Add Animation Track, the red trash can of Delete. */
	static inline const FName MenuAddIcon = TEXT("NameTBD.Menu.Add");
	static inline const FName MenuDeleteIcon = TEXT("NameTBD.Menu.Delete");

	/** The Widget Animations panel's icon - its tab and its status-bar drawer: the shooting star in the editor's grays. */
	static inline const FName WidgetAnimationsIcon = TEXT("NameTBD.WidgetAnimationsIcon");
	

private:
	static TSharedRef<class FSlateStyleSet> Create();
	static TSharedPtr<class FSlateStyleSet> StyleInstance;
};