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

	/**
	 * A check box drawn as a menu icon, for an on/off entry such as Invert Track: a menu's own check box sits left of the
	 * column its icons are in, so an entry with these as its icon lines up with the rest of the menu.
	 */
	static inline const FName MenuCheckedIcon = TEXT("NameTBD.Menu.Checked");
	static inline const FName MenuUncheckedIcon = TEXT("NameTBD.Menu.Unchecked");

	/** The Widget Animations panel's icon - its tab and its status-bar drawer: the shooting star in the editor's grays. */
	static inline const FName WidgetAnimationsIcon = TEXT("NameTBD.WidgetAnimationsIcon");

	/** The State Machine editor's Exit node: a door with the way out (Resources/WidgetAnim_StateMachineExit.svg). */
	static inline const FName StateMachineExitIcon = TEXT("NameTBD.StateMachine.ExitIcon");
	

private:
	static TSharedRef<class FSlateStyleSet> Create();
	static TSharedPtr<class FSlateStyleSet> StyleInstance;
};