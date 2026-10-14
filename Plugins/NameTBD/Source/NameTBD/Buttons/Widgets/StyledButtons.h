// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "NameTBD/Buttons/ContextMenuButton.h"

#include "StyledButtons.generated.h"

class UWidgetTree;
class UTextBlock;

/** How a ready-made button moves between its states: the speed and curve of every look and transform it changes. */
UENUM(BlueprintType)
enum class EStyledButtonMotion : uint8
{
	Smooth UMETA(ToolTip = "Eases out over about a tenth of a second."),
	Snappy UMETA(ToolTip = "Quick, eased out - for dense menus."),
	Bouncy UMETA(ToolTip = "Overshoots a little and settles - a dot pops, a knob springs over."),
	Gentle UMETA(ToolTip = "Slow and eased both ways - calm, cinematic menus."),
	Instant UMETA(ToolTip = "No animation: every state shows at once.")
};

/** The corners of a ready-made button's background. */
UENUM(BlueprintType)
enum class EStyledButtonCorners : uint8
{
	Pill UMETA(ToolTip = "Ends fully rounded."),
	Rounded UMETA(ToolTip = "Corners rounded by Corner Rounding."),
	Square UMETA(ToolTip = "No rounding.")
};

/** A tint for one state, for UStyledButtonBase::AddTint. */
struct FStyledButtonStateTint
{
	EContextMenuState State = EContextMenuState::Normal;
	FLinearColor Tint = FLinearColor::White;
};

/**
 * The ready-made buttons: the controls modern menus are built from - settings, loadouts, pause and main menus - as Context
 * Menu Buttons that arrive already made.
 *
 *   Generic Button       a button with a label: confirm, cancel, apply.
 *   Pill Button          an on/off switch: a track whose knob slides over. Left and right set it from a gamepad.
 *   Checkbox             on or off, the check mark drawn on.
 *   Radio Button         one choice of several, a dot in a ring.
 *   Menu Item            a main or pause menu's entry: a bar slides in and the label steps aside on hover.
 *   Hold Button          hold to confirm: a ring fills while it is held, the action fires once it is full.
 *   Tile Button          a loadout or inventory slot: icon, name, rarity stripe, a frame when chosen.
 *   Choice Button        "Quality  < High >": steps through a list (or numbers, a stepper) with clicks, or left and right.
 *   Segmented Choice     "Low | Medium | High": every choice side by side, a highlight sliding to the chosen one.
 *
 * Each is dragged in from the Palette (NameTBD Buttons). Its parts - shapes, text, images - are ordinary widgets kept inside the
 * button, out of the Hierarchy, until Show Parts In Hierarchy is pressed in its Details (Style); hidden or shown they are the
 * same widgets, and changes made to them are kept. Its looks are ordinary Shape and Design Targets. The Widget Blueprint
 * editor's toolbar previews its states and values with their transitions.
 *
 * Style (top of the Details): the few settings most buttons need - colors, size, corners, Motion. Changing one remakes the
 * button's Shape and Design Targets and its parts' look from all of them, so for hand-authored targets turn Use Style off.
 * At runtime, set them and call Refresh Style.
 */
UCLASS(Abstract, meta = (DisplayName = "Ready-Made Button", PrioritizeCategories = "Style"))
class NAMETBD_API UStyledButtonBase : public UContextMenuButton
{
	GENERATED_BODY()

public:
	UStyledButtonBase(const FObjectInitializer& ObjectInitializer);

	/**
	 * Whether the Style settings below make the button's looks. Off, they are hidden and the button's Shape and Design Targets
	 * and parts are left exactly as authored - for a look built by hand from here.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (DisplayPriority = "0"))
	bool bUseStyle = true;

	/** How the button moves between its states. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides, DisplayPriority = "1"))
	EStyledButtonMotion Motion = EStyledButtonMotion::Smooth;

	/**
	 * Seconds the motion takes on its way in - hovered, pressed, switched on, chosen. Motion still gives its curve (Bouncy still
	 * overshoots). 0: Motion's own pace (Snappy ~0.05 s, Smooth ~0.08 s, Bouncy ~0.12 s, Gentle ~0.2 s).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (DisplayName = "Enter Duration", EditCondition = "bUseStyle && Motion != EStyledButtonMotion::Instant", EditConditionHides, DisplayPriority = "2", ClampMin = "0.0", UIMax = "2.0", Units = "s"))
	float EnterDuration = 0.f;

	/** Seconds the motion takes on its way back out - unhovered, released, switched off. 0: Motion's own pace. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (DisplayName = "Exit Duration", EditCondition = "bUseStyle && Motion != EStyledButtonMotion::Instant", EditConditionHides, DisplayPriority = "3", ClampMin = "0.0", UIMax = "2.0", Units = "s"))
	float ExitDuration = 0.f;

	/** The color of on, chosen and active: a checked box, a switch that is on, a hover outline, a highlight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides, DisplayPriority = "4"))
	FLinearColor AccentColor;

	/** Makes the looks again from the Style settings - after setting them from Blueprint. */
	UFUNCTION(BlueprintCallable, Category = "Style")
	void RefreshStyle();

	/** Writes Text into the button's label (Button Text Widget, or its only Text Block). Nothing for a button with none. */
	UFUNCTION(BlueprintCallable, Category = "Context Menu Button|Label")
	void SetLabel(const FText& Text);

	/** The button's label, or empty when it has none. */
	UFUNCTION(BlueprintPure, Category = "Context Menu Button|Label")
	FText GetLabel();

#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	/** Makes the parts (BuildParts), then the looks (ApplyStyle). Clears the targets first: they are all the style's. */
	virtual void BuildDefaultContent() override final;
	virtual bool ShouldBuildDefaultContentWhenEmpty() const override { return true; }

	/** Makes the button's parts, once, and remembers each by role (RememberPart). */
	virtual void BuildParts() {}

	/**
	 * Makes the looks from the Style settings: the parts' own settings that the style decides, the Background Shape and its
	 * Look, and every Shape and Design Target (made again from scratch). Runs after BuildParts and whenever a Style setting
	 * changes; nothing while Use Style is off.
	 */
	virtual void ApplyStyle() {}

	/** ApplyStyle's start: the targets emptied and the looks' baselines forgotten. */
	void BeginStyle();

	// ---- Parts -----------------------------------------------------------------------------------

	/** Each part's widget name, by its role (Label, Knob...) - so the style finds its parts however they were renamed. */
	UPROPERTY()
	TMap<FName, FName> PartNames;

	void RememberPart(FName Role, const UWidget* Part);

	/** The part playing Role, if it is still in the button (listed or hidden). Marked modified, for undo - for the style. */
	UWidget* FindPart(FName Role);

	/** The part playing Role, as FindPart, left unmarked - for runtime updates. */
	UWidget* GetPart(FName Role);

	template <typename TWidget>
	TWidget* GetPart(FName Role)
	{
		return Cast<TWidget>(GetPart(Role));
	}

	template <typename TWidget>
	TWidget* FindPart(FName Role)
	{
		return Cast<TWidget>(FindPart(Role));
	}

	/** The tree the parts are made in: the Widget Blueprint's (or User Widget's) the button is in. */
	UWidgetTree* GetBuildTree() const;

	/** A new part of class Class named BaseName (made unique in the tree: Label, Label_1...). */
	UWidget* MakePart(UClass* Class, FName BaseName);

	template <typename TWidget>
	TWidget* MakePart(FName BaseName)
	{
		return Cast<TWidget>(MakePart(TWidget::StaticClass(), BaseName));
	}

	/** A Text Block with Text, at Size, white (tinted by its Design Target). */
	UTextBlock* MakeLabel(FName BaseName, const FText& Text, int32 Size);

	/** Content as the button's only child. */
	void SetButtonContent(UWidget* Content);

	/** The button's content padding and alignment, listed or hidden. */
	void SetContentLayout(const FMargin& Padding, EHorizontalAlignment HAlign = HAlign_Center, EVerticalAlignment VAlign = VAlign_Center);

	/** Content inside a new Size Box. The Size Box is returned. */
	UWidget* Sized(UWidget* Content, FName BaseName);

	/** Sets a part's Size Box to Size (a side at 0 or less is left free). */
	static void SetBoxSize(UWidget* SizeBox, const FVector2D& Size);

	// ---- Looks -----------------------------------------------------------------------------------

	/** A Look laid out for From's kind of shape: From's settings as they are, then changed by Change. */
	static FInstancedPropertyBag MakeLookFrom(const UShapeWidget& From, TFunctionRef<void(UShapeWidget&)> Change);

	template <typename TShape>
	static FInstancedPropertyBag MakeLook(const TShape& From, TFunctionRef<void(TShape&)> Change)
	{
		return MakeLookFrom(From, [&Change](UShapeWidget& Shape) { Change(static_cast<TShape&>(Shape)); });
	}

	/**
	 * A model of the Background Shape to author its looks from: a shape set up by Setup, which also becomes the Background
	 * Look. Kept in the transient package; nothing holds it once the style is applied.
	 */
	template <typename TShape>
	TShape* MakeBackground(TFunctionRef<void(TShape&)> Setup)
	{
		TShape* Model = NewObject<TShape>(GetTransientPackage());
		Setup(*Model);
		SetBackgroundFrom(*Model);
		return Model;
	}

	void SetBackgroundFrom(const UShapeWidget& Model);

	/** Sets a Rectangle's corners as Corners says (Rounding for Rounded). */
	static void SetCorners(class URectangleShape& Shape, EStyledButtonCorners Corners, float Rounding);

	/** A Shape Target for Part (this button for Self), Custom, moving as Motion says. */
	FContextMenuShapeTarget& AddShapeTarget(UWidget* Part);

	/** Target's own settings for State, switched on (as a copy of its Base Shape) if they weren't. */
	static FContextMenuShapeStateSettings& OverrideShape(FContextMenuShapeTarget& Target, EContextMenuState State);

	/** A Design Target for Part (this button for Self), Custom, driving nothing until a state says so. */
	FContextMenuDesignTarget& AddDesignTarget(UWidget* Part);

	/** Target's own design for State, switched on (as a copy of its Base Design) if it wasn't. */
	static FContextMenuWidgetDesign& OverrideDesign(FContextMenuDesignTarget& Target, EContextMenuState State);

	/** A Design Target tinting Part: Base everywhere, then each state given its own tint. */
	FContextMenuDesignTarget& AddTint(UWidget* Part, const FLinearColor& Base, const TArray<FStyledButtonStateTint>& States);

	/** The button fades to Opacity while disabled (a Design Target on Self). */
	void AddDisabledFade(float Opacity = 0.4f);

	/** The name a target stores for Part. */
	FName TargetNameOf(const UWidget* Part) const;

	/** Color, lighter by Amount (0..1) toward white, alpha kept - a hover. */
	static FLinearColor Lighter(const FLinearColor& Color, float Amount);

	/**
	 * A Design Target's blend speed for Duration seconds (most of the way there by then), else Motion's. Design Targets ease
	 * toward their goal, so the speed is about 3 / duration.
	 */
	float GetDesignSpeed(float Duration) const;

	/** A Shape Target's blend speed for Duration seconds (all the way there by then), else Motion's: 1 / duration. */
	float GetShapeSpeed(float Duration) const;
};

// =====================================================================================================
// Buttons
// =====================================================================================================

/** A button with a label - the confirm, cancel and apply of every dialog. Lights on hover, presses in. */
UCLASS(meta = (DisplayName = "Generic Button"))
class NAMETBD_API UGenericButton : public UStyledButtonBase
{
	GENERATED_BODY()

public:
	UGenericButton(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides))
	EStyledButtonCorners Corners = EStyledButtonCorners::Pill;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle && Corners == EStyledButtonCorners::Rounded", EditConditionHides, ClampMin = "0", UIMax = "32", ForceUnits = "px"))
	float CornerRounding = 8.f;

	/** Space around the label: the button's size follows it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides))
	FMargin ContentPadding = FMargin(28.f, 10.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides))
	FLinearColor FillColor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides))
	FLinearColor OutlineColor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides))
	FLinearColor TextColor;

	/** How far it presses in, as a scale. 1 doesn't. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides, ClampMin = "0.5", ClampMax = "1.2"))
	float PressScale = 0.97f;

protected:
	virtual void BuildParts() override;
	virtual void ApplyStyle() override;
};

/**
 * An on/off switch: a track whose knob slides over. Its enabled state is the value - On Context Enabled State Changed reports
 * it. Left and right set it from a gamepad or the keyboard (Left And Right Set It), as console settings menus do.
 */
UCLASS(meta = (DisplayName = "Pill Button"))
class NAMETBD_API UPillSwitchButton : public UStyledButtonBase
{
	GENERATED_BODY()

public:
	UPillSwitchButton(const FObjectInitializer& ObjectInitializer);

	/** The track's size: the button's size. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides))
	FVector2D TrackSize = FVector2D(44.0, 24.0);

	/** The gap between the knob and the track's edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides, ClampMin = "0", UIMax = "8", ForceUnits = "px"))
	float KnobInset = 3.f;

	/** The track while off (on, it is the Accent Color). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides))
	FLinearColor TrackColor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides))
	FLinearColor OutlineColor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides))
	FLinearColor KnobColor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides))
	FLinearColor KnobOnColor;

	/** Left turns it off and right on while it is focused, rather than moving the focus. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Toggle", meta = (DisplayName = "Left And Right Set It"))
	bool bLeftRightSetsValue = true;

protected:
	virtual void BuildParts() override;
	virtual void ApplyStyle() override;
	virtual bool HandleNavigation(EUINavigation Direction) override;

	/** The knob follows the size the switch is laid out at (a slot bigger or smaller than Track Size), not only Track Size. */
	virtual bool WantsLayoutSizeChanges() const override { return bUseStyle; }
	virtual void HandleLayoutSizeChanged(const FVector2D& NewSize) override;

	/** The knob's size and how far it slides, for a track of TrackExtent. */
	void ComputeKnob(const FVector2D& TrackExtent, float& OutKnob, float& OutTravel) const;

	/** Sizes the knob, its padding and its switched-on slide for a track of TrackExtent - the parts and Shape Target only. */
	void FitKnob(const FVector2D& TrackExtent, bool bMarkModified);

	/** The size the knob was last fitted to. */
	FVector2D FittedTrackSize = FVector2D::ZeroVector;
};

/** On or off, a check mark drawing itself on. Its enabled state is the value. */
UCLASS(meta = (DisplayName = "Checkbox"))
class NAMETBD_API UCheckboxButton : public UStyledButtonBase
{
	GENERATED_BODY()

public:
	UCheckboxButton(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides, ClampMin = "8", UIMax = "64", ForceUnits = "px"))
	float BoxSize = 22.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides, ClampMin = "0", UIMax = "16", ForceUnits = "px"))
	float CornerRounding = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides, ClampMin = "0", UIMax = "6", ForceUnits = "px"))
	float OutlineWidth = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides))
	FLinearColor OutlineColor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides))
	FLinearColor CheckColor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides, ClampMin = "0.5", UIMax = "6", ForceUnits = "px"))
	float CheckWidth = 2.5f;

protected:
	virtual void BuildParts() override;
	virtual void ApplyStyle() override;
};

/** One choice of several: the radio buttons sharing a container (or an Enable Group) allow one chosen at a time. */
UCLASS(meta = (DisplayName = "Radio Button"))
class NAMETBD_API URadioButton : public UStyledButtonBase
{
	GENERATED_BODY()

public:
	URadioButton(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides, ClampMin = "8", UIMax = "64", ForceUnits = "px"))
	float Size = 22.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides, ClampMin = "0", UIMax = "6", ForceUnits = "px"))
	float RingWidth = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides))
	FLinearColor RingColor;

	/** The dot's size when chosen, as a fraction of the ring. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides, ClampMin = "0.1", ClampMax = "1"))
	float DotScale = 0.5f;

protected:
	virtual void BuildParts() override;
	virtual void ApplyStyle() override;
};

/** An entry of a main or pause menu: a bar slides in at its side and the label steps aside as it is hovered or focused. */
UCLASS(meta = (DisplayName = "Menu Item"))
class NAMETBD_API UMenuItemButton : public UStyledButtonBase
{
	GENERATED_BODY()

public:
	UMenuItemButton(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides, ClampMin = "6", UIMax = "64"))
	int32 FontSize = 20;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides))
	FLinearColor TextColor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides))
	FLinearColor HoverTextColor;

	/** The row's color while hovered. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides))
	FLinearColor HoverFillColor;

	/** The accent bar's size: width and height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides))
	FVector2D BarSize = FVector2D(4.0, 24.0);

	/** How far the label steps aside on hover. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides, UIMin = "0", UIMax = "24", ForceUnits = "px"))
	float HoverShift = 6.f;

protected:
	virtual void BuildParts() override;
	virtual void ApplyStyle() override;
};

/**
 * Hold to confirm - quit, delete a save, reset settings: a ring fills while it is held and the action fires once, when it is
 * full; let go early and it empties. Press Duration sets how long.
 */
UCLASS(meta = (DisplayName = "Hold Button"))
class NAMETBD_API UHoldButton : public UStyledButtonBase
{
	GENERATED_BODY()

public:
	UHoldButton(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides))
	EStyledButtonCorners Corners = EStyledButtonCorners::Pill;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle && Corners == EStyledButtonCorners::Rounded", EditConditionHides, ClampMin = "0", UIMax = "32", ForceUnits = "px"))
	float CornerRounding = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides))
	FLinearColor FillColor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides))
	FLinearColor OutlineColor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides))
	FLinearColor TextColor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides, ClampMin = "8", UIMax = "64", ForceUnits = "px"))
	float RingSize = 22.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides, ClampMin = "0.5", UIMax = "8", ForceUnits = "px"))
	float RingWidth = 3.f;

protected:
	virtual void BuildParts() override;
	virtual void ApplyStyle() override;
};

/**
 * A loadout or inventory slot: an icon, a name, a rarity stripe (color it per item), lifted on hover and framed when chosen -
 * one chosen per container. Click the chosen one again to clear it.
 */
UCLASS(meta = (DisplayName = "Tile Button"))
class NAMETBD_API UTileButton : public UStyledButtonBase
{
	GENERATED_BODY()

public:
	UTileButton(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides))
	FVector2D TileSize = FVector2D(120.0, 120.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides, ClampMin = "0", UIMax = "32", ForceUnits = "px"))
	float CornerRounding = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides, ClampMin = "0", UIMax = "256", ForceUnits = "px"))
	float IconSize = 64.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides))
	FLinearColor FillColor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides))
	FLinearColor OutlineColor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides))
	FLinearColor RarityColor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides))
	bool bShowLabel = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides))
	bool bShowRarity = true;

	/** How much it lifts on hover, as a scale. 1 doesn't. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides, ClampMin = "0.8", ClampMax = "1.3"))
	float HoverScale = 1.03f;

protected:
	virtual void BuildParts() override;
	virtual void ApplyStyle() override;
};
