// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "WidgetAnimDriver.h" // EWidgetAnimValueKind

#include "WidgetAnimInput.generated.h"

class UScrollBox;
class UWidget;

// =====================================================================================================
// Value inputs
// =====================================================================================================
//
// A value-driven track follows whatever value it is given (see WidgetAnimDriver.h). Many of the values UI follows with
// its motion come from the player's own hand: where the pointer is over a card, whether a button is hovered, pressed or
// focused, how far something has been dragged, how far a list has been scrolled. So a value track can be given a Value
// Input in the Widget Animations editor, next to its Value Name and smoothing: the animation then carries it, and every
// widget that plays the animation follows the pointer or the scroll with nothing to set up where it is used.
//
// The input is measured on the widget the track animates - its Track Group's target, as the entry playing the
// animation resolves it - so a card animation tilts whichever card plays it. A Scroll input reads the Scroll Box that
// widget is in (or is, or holds).
//
// An input sets the track's value exactly as Set Widget Animation Value would, by the track's Value Name in its
// animation: the track's range, smoothing, Never Go Backwards and the rest all apply, and tracks sharing that name in
// the animation follow it too. Tracks sharing a name share one value, so one input: the first track with an input feeds
// it, and a value fed by an input shouldn't also be set from gameplay.
//
// Inputs are read by the track player every frame, before the tracks move, wherever the animation plays - a Widget
// Animation Component, a button, a card, a screen - and only a change is passed on. The first value after the track
// starts is snapped to, so a card doesn't glide in from the middle. An entrance track stops reading once its widget
// starts its exit, so an exit waiting for a value (On Exit) isn't held open by a pointer that keeps moving. They are
// read at runtime only: the editor's previews sweep value tracks on the clock, as they always have.
//
// Positions are measured on the widget as it is laid out, without its own render transform: a card that tilts or
// slides as the pointer moves over it doesn't move its own measurements along with it and start to chase itself.
//
// A game or licensee module adds an input of its own by deriving a USTRUCT from FWidgetAnimInputSource and overriding
// Sample; it is offered on every value track with no registration. Its non-reflected members are its runtime state,
// one copy per playing track.
//
// A widget drawn into the world - on a Diegetic Screen (NameTBD/Diegetic) - has a pointer of its own: the one a Widget
// Interaction Component points at its screen with, in the space of the screen's window rather than the desktop's. The
// context then carries that pointer (bWorldPointer, WorldPointer), and the inputs read it instead of the mouse - see
// FWorldPointer and ReadPointer below.

/** How a two-axis input - a position, an offset - is handed to its tracks. */
UENUM(BlueprintType)
enum class EWidgetAnimInputAxes : uint8
{
	Both UMETA(DisplayName = "X and Y", ToolTip = "Both, as a Vector 2D value: for tracks whose Value Type is Vector 2D - a Scale, or a Transform moving a whole Translation, Scale or Shear."),

	X UMETA(ToolTip = "Only X, as one number."),

	Y UMETA(ToolTip = "Only Y, as one number."),

	Distance UMETA(ToolTip = "How far from zero, whichever way, as one number: the length of X and Y together.")
};

/** What a Pointer Position input measures the pointer in. */
UENUM(BlueprintType)
enum class EWidgetAnimPointerUnits : uint8
{
	Centered UMETA(ToolTip = "-1 at the left or top edge, 0 in the middle, 1 at the right or bottom edge, whatever the widget's size. What a card tilting toward the pointer wants: the same tilt on a small card and a big one."),

	Fraction UMETA(ToolTip = "0 at the left or top edge, 1 at the right or bottom edge."),

	Pixels UMETA(ToolTip = "Distance from the middle, in the widget's own units (Slate units: the same at any screen resolution).")
};

/** What a Pointer Position input gives while the pointer is not over its widget. */
UENUM(BlueprintType)
enum class EWidgetAnimPointerAway : uint8
{
	ReturnToCenter UMETA(DisplayName = "Return To Middle", ToolTip = "The value of the widget's middle, so the tracks travel back to rest as the pointer leaves."),

	HoldLast UMETA(DisplayName = "Hold Last", ToolTip = "The last value measured while the pointer was over it, so the widget stays where the pointer left it."),

	KeepFollowing UMETA(DisplayName = "Keep Following", ToolTip = "Keeps measuring wherever the pointer is on screen, past the widget's edges: eyes following the cursor. Values past the tracks' range are held at its ends.")
};

/** Which state of a widget an Interaction input gives. */
UENUM(BlueprintType)
enum class EWidgetAnimInteractionState : uint8
{
	Hovered UMETA(ToolTip = "The pointer is over it - over a child, for a widget that isn't hit-testable itself (Self Hit Test Invisible)."),

	Pressed UMETA(ToolTip = "The left mouse button was pressed over it and is still held. A Button uses its own pressed state, which keyboard and gamepad presses also set."),

	Focused UMETA(ToolTip = "It, or something inside it, has keyboard or gamepad focus."),

	HoveredOrFocused UMETA(DisplayName = "Hovered Or Focused", ToolTip = "Either: the highlight a menu shows the same whether it is used with a mouse or a gamepad.")
};

/** What a Drag input does once the button is let go. */
UENUM(BlueprintType)
enum class EWidgetAnimDragRelease : uint8
{
	SpringBack UMETA(DisplayName = "Return To Zero", ToolTip = "Gives 0 again, so the tracks travel back to rest: a rubber-band pull, a card flung and caught. With Spring smoothing it springs back."),

	Stay UMETA(ToolTip = "Keeps the last offset until the next drag starts, which starts again from 0."),

	Accumulate UMETA(ToolTip = "Keeps the last offset, and the next drag adds on to it: something that can be dragged around and stays where it is put. Values past the tracks' range are held at its ends.")
};

/** What a Scroll input reads off its Scroll Box. */
UENUM(BlueprintType)
enum class EWidgetAnimScrollMeasure : uint8
{
	Progress UMETA(ToolTip = "How far through the scroll: 0 at the start, 1 at the end. A progress bar for reading, a header that shrinks as the list is scrolled."),

	Offset UMETA(ToolTip = "How far it is scrolled, in Slate units from the start. For effects that should happen over a fixed distance whatever the list's length."),

	Speed UMETA(ToolTip = "How fast it is scrolling, in Slate units per second: positive toward the end, negative back toward the start, 0 at rest. Items that skew or stretch while the list moves and settle as it stops."),

	PositionInView UMETA(DisplayName = "Position In View", ToolTip = "Where the widget measured is within the Scroll Box's visible area: 0 as its leading edge comes into view at the far end, 0.5 when it is in the middle, 1 as its trailing edge leaves at the near end. Items that fade and rise into place as they are scrolled into view, or parallax.")
};

class UWidgetComponent;

namespace WidgetAnimInput
{
	/** A mouse event that was counted: how many so far, and where the last one was, in desktop space. */
	struct FPointerEventCount
	{
		uint32 Count = 0;
		FVector2f Position = FVector2f::ZeroVector;
	};

	/**
	 * The pointer of a widget drawn into the world rather than on the desktop - a Diegetic Screen's - pointed at by a Widget
	 * Interaction Component. Positions are in the space of the widget's absolute geometry: its screen's window. Counts are of
	 * the presses made on that screen, which Slate's own input processing never sees (and double-clicks aren't made there).
	 */
	struct FWorldPointer
	{
		/** The component the widget is drawn by - for hit-testing a point on it. */
		TWeakObjectPtr<UWidgetComponent> Surface;

		/** Where the pointer is on it - or was last, once it has left. Only when bHasPosition: a pointer has been on it. */
		FVector2f Position = FVector2f::ZeroVector;
		bool bHasPosition = false;

		/** Whether a pointer is on it now. */
		bool bOver = false;

		/** Whether a left or right button pressed on it is still held. */
		bool bLeftDown = false;
		bool bRightDown = false;

		FPointerEventCount LeftPresses;
		FPointerEventCount RightPresses;
		FPointerEventCount RightReleases;
		FPointerEventCount DoubleClicks;

		/** Whether a button pressed on it is held: the left or right mouse button. False for any other. */
		bool IsDown(const FKey& Button) const
		{
			return Button == EKeys::LeftMouseButton ? bLeftDown : Button == EKeys::RightMouseButton ? bRightDown : false;
		}
	};

	/**
	 * What finds the world pointer of the surface a widget is drawn on: false for a widget on the desktop. Set by the module
	 * that draws widgets into the world (the Diegetic UI's, at startup); null = every widget is on the desktop.
	 */
	using FWorldPointerResolver = TFunction<bool(const UWidget* /*Widget*/, FWorldPointer& /*OutPointer*/)>;
	NAMETBD_API void SetWorldPointerResolver(FWorldPointerResolver InResolver);
}

/**
 * Everything an input is given when it is read, once per frame per track.
 */
struct NAMETBD_API FWidgetAnimInputContext
{
	/** The widget whose animations are playing: the one the Widget Animation Component, button or card is on. */
	UWidget* HostWidget = nullptr;

	/** The widget the track animates - its Track Group's target, or the host widget. Never null when an input is read. */
	UWidget* MeasuredWidget = nullptr;

	/** Seconds since the last read. */
	float DeltaTime = 0.f;

	/** Where the pointer is, in desktop space (the space of a widget's absolute geometry). Only when bHasPointer. */
	FVector2f PointerPosition = FVector2f::ZeroVector;
	bool bHasPointer = false;

	/**
	 * The host widget is drawn into the world: PointerPosition is WorldPointer's, in its surface's space, and button state and
	 * counted presses are WorldPointer's too. Set by WidgetAnimInput::ReadPointer.
	 */
	bool bWorldPointer = false;
	WidgetAnimInput::FWorldPointer WorldPointer;
};

/**
 * What a value track's Value Input reads its value from. The base is never used itself: pick one of the inputs that
 * derive from it. See the note at the top of WidgetAnimInput.h.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimInputSource
{
	GENERATED_BODY()

	virtual ~FWidgetAnimInputSource() = default;

	/**
	 * Reads the input. Returns false when it has nothing to say this frame, which leaves the track where it is.
	 * OutValue is raw, up to four channels - see WidgetAnimValue::FromFloat and the others.
	 */
	virtual bool Sample(const FWidgetAnimInputContext& Context, FVector4& OutValue) { return false; }

	/** The kind of value it gives, for checking it against the track's Value Type in the editor. */
	virtual EWidgetAnimValueKind GetValueKind() const { return EWidgetAnimValueKind::Float; }

	/** A few words naming it where the track is listed - "Pointer Position", "Scroll Progress". */
	virtual FString GetSummary() const { return FString(); }
};

/** Where the pointer is over the widget: a card tilting toward it, a shine following it, eyes watching it. */
USTRUCT(BlueprintType, meta = (DisplayName = "Pointer Position"))
struct NAMETBD_API FWidgetAnimInput_PointerPosition : public FWidgetAnimInputSource
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	EWidgetAnimPointerUnits Units = EWidgetAnimPointerUnits::Centered;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	EWidgetAnimInputAxes Output = EWidgetAnimInputAxes::Both;

	/** What it gives while the pointer is not over the widget. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (DisplayName = "When Away"))
	EWidgetAnimPointerAway WhenAway = EWidgetAnimPointerAway::ReturnToCenter;

	virtual bool Sample(const FWidgetAnimInputContext& Context, FVector4& OutValue) override;
	virtual EWidgetAnimValueKind GetValueKind() const override;
	virtual FString GetSummary() const override;

private:
	FVector2D Last = FVector2D::ZeroVector;
	bool bHasLast = false;
};

/** 1 while the widget is hovered, pressed or focused, and 0 while not: highlights, press dips, focus rings. */
USTRUCT(BlueprintType, meta = (DisplayName = "Hover, Press or Focus"))
struct NAMETBD_API FWidgetAnimInput_Interaction : public FWidgetAnimInputSource
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	EWidgetAnimInteractionState State = EWidgetAnimInteractionState::HoveredOrFocused;

	virtual bool Sample(const FWidgetAnimInputContext& Context, FVector4& OutValue) override;
	virtual FString GetSummary() const override;

private:
	bool bPressStartedOver = false;
	bool bWasDown = false;
	/** The left presses counted when last sampled (WidgetAnimInput::GetLeftPressCount) - bHasPressCount once there is one. */
	uint32 LastPressCount = 0;
	bool bHasPressCount = false;
};

/**
 * How far the pointer has been dragged since a button was pressed over the widget, in the widget's own units: a
 * rubber-band pull, a sheet pulled down, a knob turned by dragging.
 */
USTRUCT(BlueprintType, meta = (DisplayName = "Drag"))
struct NAMETBD_API FWidgetAnimInput_Drag : public FWidgetAnimInputSource
{
	GENERATED_BODY()

	/** The mouse button that drags. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	FKey Button = EKeys::LeftMouseButton;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	EWidgetAnimInputAxes Output = EWidgetAnimInputAxes::Both;

	/** Multiplies the offset: 0.01 turns a 100 unit drag into 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	float Scale = 1.f;

	/** How far the pointer has to move before the drag counts, in the widget's own units, so a click doesn't nudge it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (ClampMin = "0", UIMin = "0", UIMax = "20"))
	float DeadZone = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (DisplayName = "On Release"))
	EWidgetAnimDragRelease OnRelease = EWidgetAnimDragRelease::SpringBack;

	virtual bool Sample(const FWidgetAnimInputContext& Context, FVector4& OutValue) override;
	virtual EWidgetAnimValueKind GetValueKind() const override;
	virtual FString GetSummary() const override;

private:
	/** Where the press started, in desktop space. */
	FVector2f PressPosition = FVector2f::ZeroVector;

	/** The offset kept from earlier drags (Accumulate), and the one shown now, before Scale - local units. */
	FVector2D Kept = FVector2D::ZeroVector;
	FVector2D Current = FVector2D::ZeroVector;

	bool bWasDown = false;
	bool bDragging = false;
	bool bPastDeadZone = false;
};

/** How far, or how fast, a Scroll Box is scrolled, or where the widget sits in its view. */
USTRUCT(BlueprintType, meta = (DisplayName = "Scroll"))
struct NAMETBD_API FWidgetAnimInput_Scroll : public FWidgetAnimInputSource
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	EWidgetAnimScrollMeasure Measure = EWidgetAnimScrollMeasure::Progress;

	virtual bool Sample(const FWidgetAnimInputContext& Context, FVector4& OutValue) override;
	virtual FString GetSummary() const override;

private:
	/** The Scroll Box read, found once for the widget measured on - see WidgetAnimInput::FindScrollBox. */
	TWeakObjectPtr<UScrollBox> ScrollBox;
	TWeakObjectPtr<UWidget> SearchedFrom;
	bool bSearched = false;

	/** The offset last frame, for Speed. */
	float LastOffset = 0.f;
	bool bHasLastOffset = false;
};

/** Measuring helpers shared by the inputs, for inputs added elsewhere too. */
namespace WidgetAnimInput
{
	/**
	 * Whether the pointer is over Widget: Slate's own hover, which knows what is on top, when Slate's hit test can reach the
	 * widget (nothing from it outwards is Not Hit-Testable (Self & All Children)). Otherwise its own Visible parts - itself
	 * when Visible, and every Visible widget inside it, by each one's own setting - or, with none, its laid-out rectangle.
	 */
	NAMETBD_API bool IsPointerOver(const UWidget* Widget, const FVector2f& PointerPosition);

	/**
	 * Whether a point - where an event was, rather than where the pointer is - is over Widget: what Slate hit-tests there, which
	 * knows what is on top and doesn't depend on hover (lost to whatever has the mouse captured), when Slate's hit test can reach
	 * the widget; otherwise its own Visible parts, as IsPointerOver.
	 */
	NAMETBD_API bool IsPointUnder(const UWidget* Widget, const FVector2f& Point);

	/**
	 * IsPointUnder for a point given in the context's pointer space: the desktop's, or - with a world pointer - its surface's,
	 * hit-tested by the surface (UWidgetComponent::GetHitWidgetPath).
	 */
	NAMETBD_API bool IsPointUnder(const FWidgetAnimInputContext& Context, const UWidget* Widget, const FVector2f& Point);

	/**
	 * Fills the context's pointer for its HostWidget: the world pointer of the surface the widget is drawn on (bWorldPointer),
	 * or the desktop's (GetPointerPosition). What every reader of inputs calls once per frame, before sampling.
	 */
	NAMETBD_API void ReadPointer(FWidgetAnimInputContext& Context);

	/** Whether Button is held, from the context's pointer: its world pointer's buttons, or the mouse's (IsMouseButtonDown). */
	NAMETBD_API bool IsButtonDown(const FWidgetAnimInputContext& Context, const FKey& Button);

	/**
	 * A desktop point in Widget's local space, and its size - as laid out, without Widget's own render transform, so
	 * an animation moving the widget doesn't move its measurements. False when it has no geometry yet.
	 */
	NAMETBD_API bool ToUnanimatedLocal(const UWidget* Widget, const FVector2f& DesktopPoint, FVector2f& OutLocal, FVector2f& OutSize);

	/** A desktop-space offset in Widget's local units, the same way. */
	NAMETBD_API FVector2f ToUnanimatedLocalVector(const UWidget* Widget, const FVector2f& DesktopVector);

	/** Whether a mouse button is held. Asked of Slate once a frame, however many inputs ask. */
	NAMETBD_API bool IsMouseButtonDown(const FKey& Button);

	/**
	 * Where the pointer is on the desktop this frame - false with no Slate application to ask. Asked of the platform once a
	 * frame and shared: every player, machine and input reading it used to ask the operating system itself, every frame.
	 */
	NAMETBD_API bool GetPointerPosition(FVector2f& OutPosition);

	/** A two-axis value as the value Axes asks for. */
	NAMETBD_API FVector4 FromAxes(EWidgetAnimInputAxes Axes, const FVector2D& Value);

	/**
	 * Input events counted as Slate sees them, before any widget handles them - for what a state machine's built-in pulses
	 * (Double Clicked, Right Clicked, Accept, Back, the Gamepad buttons) can't read off a widget's state. Each is a count of
	 * the times it happened since the counting began: a reader keeps the count it last saw, and a new count is a new event,
	 * whenever in the frame it came. Counting starts with the first call of StartCountingEvents (a machine that reads any
	 * of them calls it), and stops with StopCountingEvents (the module's shutdown).
	 */
	NAMETBD_API void StartCountingEvents();
	NAMETBD_API void StopCountingEvents();

	/** Presses of Key - first presses, not a held key's repeats. */
	NAMETBD_API uint32 GetKeyPressCount(const FKey& Key);

	/** Presses of the UI's confirm (bAccept) or cancel button, as Slate's navigation config reads them - keys and gamepads alike. */
	NAMETBD_API uint32 GetNavigationPressCount(bool bAccept);

	/** Left button presses (a touch's too, and the second press of a double-click); left button double-clicks; right button presses and releases. */
	NAMETBD_API FPointerEventCount GetLeftPressCount();
	NAMETBD_API FPointerEventCount GetDoubleClickCount();
	NAMETBD_API FPointerEventCount GetRightPressCount();
	NAMETBD_API FPointerEventCount GetRightReleaseCount();

	/** The kind of value Axes gives. */
	inline EWidgetAnimValueKind KindOfAxes(EWidgetAnimInputAxes Axes)
	{
		return Axes == EWidgetAnimInputAxes::Both ? EWidgetAnimValueKind::Vector2D : EWidgetAnimValueKind::Float;
	}

	/** The Scroll Box an input measured on Widget reads: Widget itself, the nearest one around it, or the first inside it. */
	NAMETBD_API UScrollBox* FindScrollBox(UWidget* Widget);
}
