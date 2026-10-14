// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "StyledButtons.h"
#include "NameTBD/Widgets/Glass/GlassPanel.h"

#include "GlassButton.generated.h"

class UGlassPanel;

/**
 * A button made of glass: a Glass Panel behind its label blurs whatever is behind the button, and lights up as it is hovered,
 * focused from a gamepad or pressed - more blur, a brighter edge and sheen, a press that darkens and sinks. Any glass preset
 * (frosted, liquid, acrylic, tinted...). A ready-made button: its parts (the glass, the label) are inside it, and Show Parts In
 * Hierarchy opens them up - the Glass Panel's own settings (its Glass, Hover Response) then take over.
 */
UCLASS(meta = (DisplayName = "Glass Button"))
class NAMETBD_API UGlassButton : public UStyledButtonBase
{
	GENERATED_BODY()

public:
	UGlassButton(const FObjectInitializer& ObjectInitializer);

	/** The glass the button is made of. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides))
	EGlassPreset GlassPreset = EGlassPreset::Liquid;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides))
	EStyledButtonCorners Corners = EStyledButtonCorners::Pill;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle && Corners == EStyledButtonCorners::Rounded", EditConditionHides, ClampMin = "0", UIMax = "32", ForceUnits = "px"))
	float CornerRounding = 12.f;

	/** Space around the label: the button's size follows it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides))
	FMargin ContentPadding = FMargin(28.f, 12.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides))
	FLinearColor TextColor;

	/** How strongly the glass lights up on hover: 0 not at all, 1 as the Glass Panel's Hover Response, 2 twice that. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides, ClampMin = "0", UIMax = "2"))
	float HoverGlow = 1.f;

	/** How far it presses in, as a scale. 1 doesn't. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides, ClampMin = "0.5", ClampMax = "1.2"))
	float PressScale = 0.97f;

	/** The Glass Panel the button is made of (one of its parts). */
	UFUNCTION(BlueprintPure, Category = "Glass Button")
	UGlassPanel* GetGlassPart() const;

protected:
	virtual void BuildParts() override;
	virtual void ApplyStyle() override;
	virtual void OnVisualStateChanged(EContextMenuState NewState, bool bInstant) override;
};
