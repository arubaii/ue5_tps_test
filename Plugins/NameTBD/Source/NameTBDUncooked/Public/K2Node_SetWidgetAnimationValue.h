// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"
#include "NameTBD/WidgetAnimation/WidgetAnimDriver.h"

#include "K2Node_SetWidgetAnimationValue.generated.h"

class UBlueprint;
class UEdGraphPin;

/** One value a Set Widget Animation Value node can pick: a value-driven track's Value Name, and the type it takes. */
struct NAMETBDUNCOOKED_API FWidgetAnimValueChoice
{
	FName ValueName;
	EWidgetAnimValueKind Kind = EWidgetAnimValueKind::Float;
};

/** Where a node's values come from: one Widget Animation, or a widget's additive tracks - with the values in it. */
struct NAMETBDUNCOOKED_API FWidgetAnimValueSource
{
	/** What the node's Animation pin stores: the animation's object path, or UK2Node_SetWidgetAnimationValue::AdditiveTracksKey. */
	FString Key;

	/** What its dropdown shows. */
	FText Label;

	/** Whether it is a percentage animation (see EWidgetAnimTimeUnit): what Set Widget Animation Progress picks from. */
	bool bPercent = false;

	TArray<FWidgetAnimValueChoice> Values;
};

/** Where a node's lists come from: a Widget Blueprint, and - None for all of it - the designer widget in it Target is. */
struct NAMETBDUNCOOKED_API FWidgetAnimValueSourceScope
{
	const UBlueprint* Blueprint = nullptr;
	FName WidgetName;
};

/**
 * Set Widget Animation Value, picked from dropdowns: the Animation (one of those the widget's Widget Animation
 * Components play in Anim In and Anim Out, or its Additive Tracks), then the Value Name - the value-driven tracks in
 * it - and the Value pin takes that track's own type: a float, an integer, a Vector 2D or a Linear Color.
 *
 * The lists come from the Widget Animation Components on the Target and nested under it - what the node reaches when
 * it runs: with Target unconnected (Self), every component in this Widget Blueprint; with Target connected to one of
 * its designer widgets, that widget's and those inside it; with Target connected to a widget of another Widget
 * Blueprint class, that Blueprint's. Nested User Widgets add their own Blueprint's.
 *
 * Compiles to UWidgetAnimPlayLibrary::SetScopedValue*, so the value set is the one in the picked animation only; the
 * same name in another animation is left alone. Set All Matches sets it in every component found that has it, rather
 * than the first - for a screen holding many of the same widget.
 */
UCLASS()
class NAMETBDUNCOOKED_API UK2Node_SetWidgetAnimationValue : public UK2Node
{
	GENERATED_BODY()

public:

	static const FName TargetPinName;
	static const FName AnimationPinName;
	static const FName ValueNamePinName;
	static const FName ValuePinName;
	static const FName SnapPinName;
	static const FName StartIfNotPlayingPinName;
	static const FName SearchParentWidgetsPinName;
	static const FName SetAllMatchesPinName;

	/** What the Animation pin stores for the widget's additive tracks. */
	static const FString AdditiveTracksKey;

	// UEdGraphNode
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual void PinDefaultValueChanged(UEdGraphPin* Pin) override;
	virtual void PinConnectionListChanged(UEdGraphPin* Pin) override;
	virtual void ValidateNodeDuringCompilation(class FCompilerResultsLog& MessageLog) const override;

	// UK2Node
	virtual bool IsNodePure() const override { return false; }
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual FText GetMenuCategory() const override;
	virtual void ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins) override;
	virtual void ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;

	/** Every animation - and the additive tracks - the node can pick from, with their values. For the dropdowns. */
	virtual void GetValueSources(TArray<FWidgetAnimValueSource>& OutSources) const;

	/** The values in the picked animation. Empty when none is picked, or it has none. */
	void GetValueChoices(TArray<FWidgetAnimValueChoice>& OutChoices) const;

	/**
	 * Writes to the log where the lists were looked for and what was found there - the Target's Blueprint and widget,
	 * whether its designer preview is open, and every Widget Animation Component stored in it with the widget it is
	 * kept for. For when a list comes up empty; the dropdown calls it then.
	 */
	void LogValueSourceSearch() const;

	/**
	 * The scope Node's Target pin reaches - see the class comment. Shared with the other nodes that pick from what a widget's
	 * components play: Set State Machine Input.
	 */
	static FWidgetAnimValueSourceScope FindScopeFor(const UK2Node* Node, const UEdGraphPin* TargetPin);

	/**
	 * The Widget Animation Components in Scope: those of the designer preview when its editor is open, then those the
	 * Blueprints store, nested User Widgets' Blueprints included. The same component may be listed more than once.
	 */
	static void GatherComponentsInScope(const FWidgetAnimValueSourceScope& Scope, TArray<const class UWidgetAnimationComponent*>& OutComponents);

protected:

	/** The type the Value pin has: the picked track's. Saved, so the pin is right before anything is looked up. */
	UPROPERTY()
	EWidgetAnimValueKind ValueKind = EWidgetAnimValueKind::Float;

	/** The scope Target reaches: see the class comment. No Blueprint when Target is wired to something unknown. */
	FWidgetAnimValueSourceScope FindSourceScope(const UEdGraphPin* TargetPin) const;

	/** The sources, from the Widget Animation Components in Scope. */
	static void GatherValueSources(const FWidgetAnimValueSourceScope& Scope, TArray<FWidgetAnimValueSource>& OutSources);

	/** The type of the value SourceKey's Name is, when there is one. */
	bool FindValueKind(const UEdGraphPin* TargetPin, const FString& SourceKey, FName Name, EWidgetAnimValueKind& OutKind) const;

	/** The pin type of a value of Kind. */
	static FEdGraphPinType MakeValuePinType(EWidgetAnimValueKind Kind);

	/** Gives the Value pin the type the picked track takes, breaking links that no longer fit. */
	void RefreshValuePin();
};
