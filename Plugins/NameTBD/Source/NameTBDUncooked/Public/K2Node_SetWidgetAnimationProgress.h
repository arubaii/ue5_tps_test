// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "K2Node_SetWidgetAnimationValue.h"

#include "K2Node_SetWidgetAnimationProgress.generated.h"

/**
 * Set Widget Animation Progress, picked from a dropdown: the Animation is one of the percentage animations (Time In
 * Percent - see EWidgetAnimTimeUnit) the widget's Widget Animation Components play in Anim In and Anim Out, found exactly as
 * Set Widget Animation Value finds its animations, and Progress is where it goes, 0 for 0% and 1 for 100%. Only the
 * entries playing that animation take it.
 *
 * Shares Set Widget Animation Value's machinery - its Target scope, its search for components, its Animation dropdown -
 * and compiles to UWidgetAnimPlayLibrary::SetScopedProgress. Set Widget Animation Progress By Id sets it by an entry's
 * Anim ID instead.
 */
UCLASS()
class NAMETBDUNCOOKED_API UK2Node_SetWidgetAnimationProgress : public UK2Node_SetWidgetAnimationValue
{
	GENERATED_BODY()

public:

	static const FName ProgressPinName;

	// UEdGraphNode
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual void ValidateNodeDuringCompilation(class FCompilerResultsLog& MessageLog) const override;

	// UK2Node
	virtual FText GetMenuCategory() const override;
	virtual void ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;

	// UK2Node_SetWidgetAnimationValue
	/** The percentage animations only - no additive tracks, which are never a percentage animation. */
	virtual void GetValueSources(TArray<FWidgetAnimValueSource>& OutSources) const override;
};
