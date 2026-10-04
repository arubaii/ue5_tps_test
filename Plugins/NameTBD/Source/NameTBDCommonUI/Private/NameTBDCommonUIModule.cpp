// Copyright AR GameSystems 2026. All rights reserved.

#include "CommonButtonBase.h"
#include "Modules/ModuleManager.h"
#include "NameTBD/WidgetAnimation/WidgetAnimStateMachine.h"

/**
 * The Common UI module's one piece of setup: a Widget Animation State Machine on a Common UI button reads the button's own
 * states as its built-in inputs - Hovered, Pressed, Selected, Focused, Disabled - with nothing to set up. The core library
 * never depends on Common UI; it asks the providers registered here. See WidgetAnimMachineWidgetStates.
 */
class FNameTBDCommonUIModule : public IModuleInterface
{
public:

	virtual void StartupModule() override
	{
		ProviderHandle = WidgetAnimMachineWidgetStates::RegisterProvider([](UWidget* Widget, FWidgetAnimMachineWidgetState& OutState) -> bool
		{
			// The button itself, or the button whose tree the machine's widget is in - a component set up in a button's own
			// Widget Blueprint sits on a widget inside it.
			UCommonButtonBase* Button = Cast<UCommonButtonBase>(Widget);
			if (!Button && Widget)
			{
				Button = Widget->GetTypedOuter<UCommonButtonBase>();
			}
			if (!Button)
			{
				return false;
			}

			OutState.bHovered = Button->IsHovered();
			OutState.bPressed = Button->IsPressed();
			OutState.bSelected = Button->GetSelected();
			OutState.bFocused = Button->HasAnyUserFocus() || Button->HasFocusedDescendants();
			OutState.bDisabled = !Button->GetIsEnabled() || !Button->IsInteractionEnabled();
			return true;
		});
	}

	virtual void ShutdownModule() override
	{
		WidgetAnimMachineWidgetStates::UnregisterProvider(ProviderHandle);
		ProviderHandle = INDEX_NONE;
	}

private:

	int32 ProviderHandle = INDEX_NONE;
};

IMPLEMENT_MODULE(FNameTBDCommonUIModule, NameTBDCommonUI)
