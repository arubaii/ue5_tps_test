// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"
#include "NameTBD/WidgetAnimation/WidgetAnimParameters.h"

class IPropertyHandle;

/**
 * A value's type shown the way a Blueprint's My Blueprint panel shows a variable's: a small pill in the type's pin color, then
 * the type's name ("● Float"). Shared by the State Machine editor's Inputs panel and the Widget Animations panel's
 * Parameters, so both read like Unreal's own variables.
 *
 * The colors are the engine's own - asked of the Blueprint schema (UEdGraphSchema_K2::GetPinTypeColor), so they follow the
 * Graph Editor settings: Bool, Float, Integer, Vector 2D and Color as their pins are, a Choice as an enum's. Trigger and Press
 * And Hold have no Blueprint type, and keep the plugin's own colors (NameTBD::Colors::StateMachineEditor::InputTrigger / InputHold).
 */
namespace WidgetAnimTypePill
{
	enum class EKind : uint8
	{
		Bool,
		Float,
		Integer,
		Choice,
		Vector2D,
		Color,
		Trigger,
		Hold
	};

	/** An animation parameter's type as the Blueprint type it reads as. */
	EKind FromParameterType(EWidgetAnimParameterType Type);

	/** The pin color of the type in Blueprints - or, for a Trigger and a Press And Hold, the plugin's own. */
	FLinearColor GetColor(EKind Kind);

	/** "Bool", "Float", "Integer", "Choice", "Vector 2D", "Color", "Trigger", "Hold". */
	FText GetLabel(EKind Kind);

	/** The pill and the type's name, as one widget. Muted: the name in gray - a value nothing reads. */
	TSharedRef<SWidget> MakePill(EKind Kind, bool bMuted = false);

	/**
	 * An editor for a value of one of these types, by its handle: a number, a bool's box, an enum's dropdown as the property
	 * editor makes them; a Vector 2D as its X and Y side by side, and a Color as a swatch that opens the color picker - which a
	 * bare value widget (IPropertyHandle::CreatePropertyValueWidget) leaves empty for a struct.
	 */
	TSharedRef<SWidget> MakeValueEditor(const TSharedRef<IPropertyHandle>& Handle);

	/**
	 * A "+" button that drops down a type picker as the Blueprint editor's variable type picker is: a search box, then each
	 * type's pill and name, in the order given. Picking one (click, or Enter on the first match) calls OnPicked.
	 */
	TSharedRef<SWidget> MakeTypePickerButton(const TArray<EKind>& Kinds, TFunction<void(EKind)> OnPicked, TFunction<FText(EKind)> GetToolTip,
		const TAttribute<FText>& ButtonToolTip, TAttribute<bool> IsEnabled = true, bool bAsAddButton = false);

	/** How wide the type column is in a list of rows made with MakePill, so the names and types line up row under row. */
	constexpr float TypeColumnWidth = 92.f;

	/** How wide the value column is beside it. */
	constexpr float ValueColumnWidth = 124.f;
}
