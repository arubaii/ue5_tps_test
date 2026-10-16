// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class IDetailLayoutBuilder;
class UContextMenuButton;
struct FPropertyChangedEvent;
class UWidgetBlueprint;

/**
 * A Context Menu Button's Details, beyond its properties:
 *
 *   - Instance Overridable: ticking it gives the Widget Blueprint that contains the button what its placements need - an
 *     Instance Editable "{Button}InstanceOverrides" variable already pointed at the button, and the On{Button}Clicked /
 *     EnabledStateChanged / Hovered / PressCompleted event dispatchers the button calls by itself - and compiles it, in one
 *     undoable step. Unticking removes the variable again (the dispatchers stay: graphs elsewhere may be bound to them). A status
 *     line under it says what the placements get. A button whose Widget Blueprint already has such a variable shows ticked.
 *   - Presets: Save As Preset / Update Preset under Button Preset, and Save As Preset for any one Shape or Design Target (a menu
 *     of the targets) under those lists - each made through the Save Asset As window and picked at once.
 *   - A ready-made button: Show / Hide Parts In Hierarchy at the top of its Style.
 *
 * Before the rows are laid out, every Shape Target's Style Settings are laid out for the vector shape it aims at (and the Shape
 * Style for Self), so a target shows Style Settings for a shape and a Shape Box Shape for anything else - never both. Picking
 * another target widget, Shape Style or preset lays the panel out again, since those change which rows apply.
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

	/** On{ButtonName}{Suffix}. */
	static FName GetDispatcherName(const UContextMenuButton* Button, const FDispatcherEventInfo& EventInfo);

	/** The Widget Blueprint whose tree contains this button, or null if it can't be reached from here. */
	static UWidgetBlueprint* FindOwningWidgetBlueprint(const UContextMenuButton* Button);

	/** True if a multicast delegate property of this exact name already exists on the blueprint's class. */
	static bool DoesDispatcherExist(const UWidgetBlueprint* Blueprint, FName DispatcherName);

	/**
	 * Adds an Event Dispatcher with the given bool parameters, without compiling (the caller compiles once for everything it adds).
	 * Mirrors what SMyBlueprint::OnAddNewDelegate does, plus the parameter pins.
	 */
	static bool AddDispatcher(UWidgetBlueprint* Blueprint, FName DispatcherName, const TArray<FName>& BoolParameterNames);

	/** Writes TargetButtonName into the variable's default, both on the CDO and in the blueprint's variable list. */
	static void StampTargetButtonName(UWidgetBlueprint* Blueprint, FName VariableName, FName TargetButtonName);

	/** The overrides variable this button gets: {ButtonName}InstanceOverrides. */
	static FName GetOverridesVariableName(const UContextMenuButton* Button);

	/** True if any widget above the button already declares an overrides struct claiming it. */
	static bool DoesOverridesVariableExist(const UWidgetBlueprint* Blueprint, const UContextMenuButton* Button);

	/** The blueprint's own overrides variables claiming this button (by name, or by Target Button Name). */
	static TArray<FName> FindOwnOverridesVariables(const UWidgetBlueprint* Blueprint, const UContextMenuButton* Button);

	/**
	 * Gives the button's Widget Blueprint what Instance Overridable promises (the overrides variable and the dispatchers), or takes
	 * the variable away again - one undoable step, compiled once.
	 */
	static void SyncInstanceOverridable(UContextMenuButton* Button);

	/** What the placements of the button's Widget Blueprint get, in a line. */
	FText GetInstanceOverridableStatus() const;

	/** The Widget Blueprint's own button behind a designer copy (presets are made from what is authored, not from a preview). */
	static UContextMenuButton* GetAuthoredButton(UContextMenuButton* Button);

	/** The menu of a button's Shape (bShape) or Design Targets, each saved as a preset when picked. */
	TSharedRef<SWidget> MakeSaveTargetPresetMenu(bool bShape) const;

	/** Weak so a details panel outliving its selection can't resurrect a button. */
	TArray<TWeakObjectPtr<UContextMenuButton>> CustomizedButtons;

	/** Lays the panel out again next frame after an edit that changes which rows apply, and runs Instance Overridable. */
	void HandleObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& Event);

	TWeakPtr<IDetailLayoutBuilder> WeakBuilder;
	FDelegateHandle PropertyChangedHandle;
};
