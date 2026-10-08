// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class IDetailLayoutBuilder;
class UContextMenuButton;
struct FPropertyChangedEvent;
class UWidgetBlueprint;

/**
 * Adds the "Add Matching Dispatcher" buttons to a Context Menu Button's Events category.
 *
 * The button matches Event Dispatchers on the widgets above it by the On{ButtonName}{Event} convention
 * (see UContextMenuButton::BroadcastToOwnerDispatchers). Typing that name by hand and then adding two
 * bool parameters in the right order is exactly the kind of thing that gets done slightly wrong once and
 * then debugged for twenty minutes, so this creates it correctly instead.
 *
 * Before the rows are laid out, every Shape Target's Look is laid out for the vector shape it aims at (and Background Look
 * for the Background Shape), so a target shows a Look for a shape and a Shape Box Shape for anything else - never both.
 * Picking another target widget or Background Shape lays the panel out again, since those change which rows apply.
 */
class FContextMenuButtonDetails : public IDetailCustomization
{
public:

	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual ~FContextMenuButtonDetails() override;

	virtual void CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder) override;
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:

	/** One of the button's four routable events, as the editor needs to describe it. */
	struct FDispatcherEventInfo
	{
		/** Appended after the button's name: On + ButtonName + Suffix. */
		const TCHAR* NameSuffix;

		/** Parameter names, in the order the button broadcasts them. All bools. */
		TArray<FName> ParameterNames;
	};

	static TArray<FDispatcherEventInfo> GetEventInfos();

	/** The Widget Blueprint whose tree contains this button, or null if it can't be reached from here. */
	static UWidgetBlueprint* FindOwningWidgetBlueprint(const UContextMenuButton* Button);

	/** True if a multicast delegate property of this exact name already exists on the blueprint's class. */
	static bool DoesDispatcherExist(const UWidgetBlueprint* Blueprint, FName DispatcherName);

	/**
	 * Creates an Event Dispatcher with the given bool parameters and recompiles the blueprint.
	 * Mirrors what SMyBlueprint::OnAddNewDelegate does, plus the parameter pins.
	 */
	static bool CreateDispatcher(UWidgetBlueprint* Blueprint, FName DispatcherName, const TArray<FName>& BoolParameterNames);

	/**
	 * Creates a Context Menu Button Instance Overrides variable on the containing widget, marks it
	 * Instance Editable, and stamps its Target Button Name default with this button's name.
	 *
	 * That last part is what removes the setup step: the button matches on Target Button Name exactly, so
	 * nothing has to be named by convention and nothing has to call Apply Instance Overrides.
	 */
	static bool CreateOverridesVariable(UWidgetBlueprint* Blueprint, FName VariableName, FName TargetButtonName);

	/** Writes TargetButtonName into the new variable's default, both on the CDO and in the blueprint's variable list. */
	static void StampTargetButtonName(UWidgetBlueprint* Blueprint, FName VariableName, FName TargetButtonName);

	/** The overrides variable this button would create or use: {ButtonName}InstanceOverrides. */
	static FName GetOverridesVariableName(const UContextMenuButton* Button);

	/** True if any widget above the button already declares an overrides struct claiming it. */
	static bool DoesOverridesVariableExist(const UWidgetBlueprint* Blueprint, const UContextMenuButton* Button);

	FReply OnAddOverridesClicked();
	bool CanAddOverrides() const;
	FText GetAddOverridesTooltip() const;

	/** Weak so a details panel outliving its selection can't resurrect a button. */
	TArray<TWeakObjectPtr<UContextMenuButton>> CustomizedButtons;

	/** Lays the panel out again next frame after an edit that changes which rows apply. */
	void HandleObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& Event);

	TWeakPtr<IDetailLayoutBuilder> WeakBuilder;
	FDelegateHandle PropertyChangedHandle;

	FReply OnAddDispatcherClicked(FDispatcherEventInfo EventInfo);
	bool CanAddDispatcher(FDispatcherEventInfo EventInfo) const;
	FText GetAddDispatcherTooltip(FDispatcherEventInfo EventInfo) const;
};