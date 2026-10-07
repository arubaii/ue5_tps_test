// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Extensions/UIComponent.h"
#include "Misc/EngineVersionComparison.h"
#include "Slate/WidgetTransform.h"
#include "StructUtils/PropertyBag.h"

#include "NameTBD/WidgetAnimation/WidgetAnimLibrary.h" // EWidgetAnimMaterialSlot
#include "NameTBD/WidgetAnimation/WidgetAnimSpring.h"

#include "FlowListItemStyle.generated.h"

class UFlowList;
class UWidget;

/** What turns a Flow List Item Style from its Unselected look to its Selected one. */
UENUM(BlueprintType)
enum class EFlowListStyleTrigger : uint8
{
	Selected UMETA(ToolTip = "The item is the list's selected item: the look springs over as the selection arrives and leaves."),
	Focus UMETA(DisplayName = "Nearness To Focus", ToolTip = "Follows the item's distance to the focus: fully Selected at the focus, fully Unselected one item away and further, and in between as the list moves - the look glides with the scroll rather than switching."),
	Hovered UMETA(ToolTip = "The pointer is over the item."),
	SelectedOrHovered UMETA(DisplayName = "Selected Or Hovered", ToolTip = "Either - the highlight a menu shows the same whether it is used with a gamepad or a mouse.")
};

/** One material parameter a Flow List Item Style moves between its two looks. */
USTRUCT(BlueprintType)
struct NAMETBD_API FFlowListStyleMaterialParameter
{
	GENERATED_BODY()

	/** Which of the widget's materials: an Image's or Border's brush, a Text Block's font or its outline, a Retainer Box's effect. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material")
	EWidgetAnimMaterialSlot Slot = EWidgetAnimMaterialSlot::Brush;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material", meta = (DisplayName = "Parameter Name"))
	FName Parameter;

	/** A color (vector) parameter. Off: a scalar. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material", meta = (DisplayName = "Is Color"))
	bool bColor = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material", meta = (DisplayName = "Unselected", EditCondition = "!bColor", EditConditionHides))
	float UnselectedScalar = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material", meta = (DisplayName = "Selected", EditCondition = "!bColor", EditConditionHides))
	float SelectedScalar = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material", meta = (DisplayName = "Unselected", EditCondition = "bColor", EditConditionHides))
	FLinearColor UnselectedColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material", meta = (DisplayName = "Selected", EditCondition = "bColor", EditConditionHides))
	FLinearColor SelectedColor = FLinearColor::White;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFlowListStyleSelectedChanged, bool, bSelected);

/**
 * Gives any widget a Selected and an Unselected look, switched - or blended - by its Flow List item: add it from the
 * designer's Add Component button to an item, or to anything inside one - a button, the text in it, an icon - and each part
 * of the item changes with it. Works on a virtual list's entries too.
 *
 * A look is anything the widget is designed with, the same settings a Design track keys: an Image's or Border's brush (its
 * image or material, tint, size, and its rounded-box outline - corner radii, width, color), a Text Block's color, font and
 * shadow, a Button's colors, a Progress Bar's fill, padding. On a shape widget its shape - fill, stroke, corner rounding,
 * points, trim - and on a Shape Box its outline. Plus material parameters, render opacity and transform, and an animation to
 * play each way. Numbers and colors blend; an image or font face cross-fades.
 *
 * Both looks start as the widget is authored: change only what should differ. Settings left the same in both are never
 * touched, so anything else driving them - a Widget Animation, Blueprint - still can. The looks are laid out for the widget
 * the component is on as its Details are shown.
 */
UCLASS(DefaultToInstanced, EditInlineNew, BlueprintType, meta = (DisplayName = "Flow List Item Style"))
class NAMETBD_API UFlowListItemStyle : public UUIComponent
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item Style")
	EFlowListStyleTrigger Trigger = EFlowListStyleTrigger::Selected;

	/**
	 * How the look moves when the trigger switches: a spring from one look to the other, keeping its speed if the trigger
	 * switches back midway. Not used by Nearness To Focus, which follows the list itself.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item Style", meta = (EditCondition = "Trigger != EFlowListStyleTrigger::Focus", EditConditionHides))
	FWidgetSpringParams Transition;

	/**
	 * The look away from the trigger. Starts as the widget is authored. The four look bags are edited in Details only:
	 * Blueprint can't hold a property bag - use Force Look / Clear Forced Look to switch looks from Blueprint.
	 */
	UPROPERTY(EditAnywhere, Category = "Look", meta = (DisplayName = "Unselected Look", FixedLayout))
	FInstancedPropertyBag UnselectedLook;

	/** The look with the trigger on: change what should differ. */
	UPROPERTY(EditAnywhere, Category = "Look", meta = (DisplayName = "Selected Look", FixedLayout))
	FInstancedPropertyBag SelectedLook;

	/** A shape widget's shape, or a Shape Box's outline, away from the trigger. Not shown on other widgets. */
	UPROPERTY(EditAnywhere, Category = "Look|Shape", meta = (DisplayName = "Unselected Shape", FixedLayout))
	FInstancedPropertyBag UnselectedShape;

	UPROPERTY(EditAnywhere, Category = "Look|Shape", meta = (DisplayName = "Selected Shape", FixedLayout))
	FInstancedPropertyBag SelectedShape;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look|Material")
	TArray<FFlowListStyleMaterialParameter> MaterialParameters;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look|Render", meta = (InlineEditConditionToggle))
	bool bStyleRenderOpacity = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look|Render", meta = (EditCondition = "bStyleRenderOpacity", ClampMin = "0", ClampMax = "1"))
	float UnselectedRenderOpacity = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look|Render", meta = (EditCondition = "bStyleRenderOpacity", ClampMin = "0", ClampMax = "1"))
	float SelectedRenderOpacity = 1.f;

	/** Moves, scales or turns the widget between the looks. Takes over its Render Transform - leave off where a Widget Animation moves it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look|Render", meta = (InlineEditConditionToggle))
	bool bStyleRenderTransform = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look|Render", meta = (EditCondition = "bStyleRenderTransform"))
	FWidgetTransform UnselectedTransform;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look|Render", meta = (EditCondition = "bStyleRenderTransform"))
	FWidgetTransform SelectedTransform;

	/** An Anim ID on this widget's Widget Animation Component (or one around it) played as it becomes selected. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (DisplayName = "On Selected Anim ID"))
	FName SelectedAnimId;

	/** An Anim ID played as it stops being selected. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (DisplayName = "On Unselected Anim ID"))
	FName UnselectedAnimId;

	/** The trigger switched on or off - past halfway, for Nearness To Focus. */
	UPROPERTY(BlueprintAssignable, Category = "Item Style")
	FOnFlowListStyleSelectedChanged OnSelectedChanged;

	UFlowListItemStyle();

	/** How far the look is toward Selected: 0 to 1. */
	UFUNCTION(BlueprintPure, Category = "Item Style")
	float GetAmount() const { return Amount; }

	/** Shows a look outright, whatever the trigger says, until Clear Forced Look: for previews, tutorials and disabled items. */
	UFUNCTION(BlueprintCallable, Category = "Item Style")
	void ForceLook(bool bSelected);

	UFUNCTION(BlueprintCallable, Category = "Item Style")
	void ClearForcedLook();

	/**
	 * Lays the looks out for the widget the component is on - its settings, from how it is authored - keeping what they
	 * already hold. Run as its Details are shown and on load. True if anything changed.
	 */
	bool SyncLooksToOwner();

	/** The widget whose settings lay the looks out: the styled widget, or - for the Details' template - a live copy's or the authored one. */
	UWidget* FindLayoutSource() const;

	virtual void PostLoad() override;
	virtual void BeginDestroy() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
#if WITH_EDITOR
	/**
	 * The designer's preview copies of this - the template its Details edit - take its settings at once and start again in
	 * the look their list gives them, so a Transition or a look edited is seen without compiling.
	 */
	void PushEditsToPreviewCopies();
#endif

protected:
	// UUIComponent
#if UE_VERSION_OLDER_THAN(5, 6, 0)
	virtual bool OnInitialize() override;
#else
	virtual void OnInitialize() override;
	virtual void OnPreConstruct(bool bIsDesignTime) override;
#endif
	virtual void OnConstruct() override;
	virtual void OnDestruct() override;

private:
	/** The widget styled: the UI Component's owner. */
	UWidget* GetStyledWidget() const;

	void Start();
	void Stop();
	void BindList();
	void HandleListUpdated();

	/** Where the trigger says the look should be: 0 or 1, or between for Nearness To Focus. Also which item of which list it is on. */
	float ReadGoal(const UFlowList** OutList = nullptr, int32* OutIndex = nullptr) const;

	/** Moves toward the goal; true while still moving. */
	bool Advance(float DeltaTime);
	bool OnTick(float DeltaTime);

	void Apply(bool bForce);
	void GatherDifferences();

	float Amount = 0.f;
	float Velocity = 0.f;
	float AppliedAmount = -1.f;
	bool bLastSelected = false;
	bool bHasLastSelected = false;
	TOptional<bool> ForcedLook;

	/** The item last styled: a pooled entry given another item jumps to that item's look rather than animating from the last one's. */
	TWeakObjectPtr<const UFlowList> LastList;
	int32 LastItemIndex = INDEX_NONE;

	TWeakObjectPtr<UFlowList> BoundList;
	FDelegateHandle ListUpdatedHandle;
	FTSTicker::FDelegateHandle TickerHandle;

	/** The settings the looks set differently, found once per change of the looks - the only ones written. */
	TArray<const FProperty*> DesignDifferences;
	TArray<const FProperty*> ShapeDifferences;
	bool bDifferencesGathered = false;
};
