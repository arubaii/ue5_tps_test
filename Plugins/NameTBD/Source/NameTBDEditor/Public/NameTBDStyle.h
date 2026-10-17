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

	/** A state node's badge while the state runs actions on enter or exit: a lightning bolt (Resources/WidgetAnim_StateMachineActions.svg). */
	static inline const FName StateMachineActionsIcon = TEXT("NameTBD.StateMachine.ActionsIcon");

	/**
	 * The Custom Shape path editor's toolbar, and Save: the plugin's own SVGs in Resources (ShapeEdit_*.svg, WidgetAnim_Save.svg),
	 * white so a button's text color tints them. Left unset when a file is missing; the button then shows its label alone.
	 */
	static inline const FName ShapeSelectIcon = TEXT("NameTBD.Shape.Select");
	static inline const FName ShapePenIcon = TEXT("NameTBD.Shape.Pen");
	static inline const FName ShapePencilIcon = TEXT("NameTBD.Shape.Pencil");
	static inline const FName ShapeStraightIcon = TEXT("NameTBD.Shape.Straight");
	static inline const FName ShapeMirroredIcon = TEXT("NameTBD.Shape.Mirrored");
	static inline const FName ShapeAsymmetricIcon = TEXT("NameTBD.Shape.Asymmetric");
	static inline const FName ShapeDisconnectedIcon = TEXT("NameTBD.Shape.Disconnected");
	static inline const FName ShapeClosePathIcon = TEXT("NameTBD.Shape.ClosePath");
	static inline const FName ShapeOpenPathIcon = TEXT("NameTBD.Shape.OpenPath");
	static inline const FName ShapeReverseIcon = TEXT("NameTBD.Shape.Reverse");
	static inline const FName ShapeDeleteIcon = TEXT("NameTBD.Shape.Delete");
	static inline const FName ShapeDoneIcon = TEXT("NameTBD.Shape.Done");
	static inline const FName ShapeEditIcon = TEXT("NameTBD.Shape.Edit");
	static inline const FName SaveIcon = TEXT("NameTBD.Save");

	/**
	 * An animation timeline in general (the panel's Animation Timeline mode, the Script area): a clapper over a strip of keyed
	 * lanes (Resources/WidgetAnim_Timeline.svg). Each timeline by its kind, wherever one is named (the panel's picker and its
	 * list, the State Machine editor's Timelines): a Track Sheet's, bars on lanes (Resources/WidgetAnim_TrackSheet.svg); a Dope
	 * Sheet's, keys on lanes, no bars (Resources/WidgetAnim_DopeSheet.svg).
	 */
	static inline const FName TimelineIcon = TEXT("NameTBD.Timeline.TimelineIcon");
	static inline const FName TrackSheetIcon = TEXT("NameTBD.Timeline.TrackSheetIcon");
	static inline const FName DopeSheetIcon = TEXT("NameTBD.Timeline.DopeSheetIcon");

	/** The Widget Animations panel's Graph Editor, where the editor has no Curve Editor icon of its own. */
	static inline const FName GraphEditorIcon = TEXT("NameTBD.GraphEditor");

	/** The timeline's start and end markers: an upside-down triangle, white to be tinted, drawn smooth at any size. */
	static inline const FName RangeMarkerIcon = TEXT("NameTBD.Timeline.RangeMarker");

	// Animation Parameters: the chip at the end of a Selection panel row - an empty pill to bind the field to a parameter, a
	// filled one while it is bound.
	static inline const FName ParameterBindIcon = TEXT("NameTBD.Timeline.ParameterBind");
	static inline const FName ParameterBoundIcon = TEXT("NameTBD.Timeline.ParameterBound");
	static inline const FName ParameterMirroredIcon = TEXT("NameTBD.Timeline.ParameterMirrored");

	/** One of the plugin's own icons, or null when its file was missing at startup. */
	static const FSlateBrush* FindIcon(FName Name);
	

private:
	static TSharedRef<class FSlateStyleSet> Create();
	static TSharedPtr<class FSlateStyleSet> StyleInstance;
};