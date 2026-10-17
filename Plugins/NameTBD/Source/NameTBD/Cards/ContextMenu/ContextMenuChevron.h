// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "Widgets/SLeafWidget.h"
#include "ContextMenuTypes.h"

#include "ContextMenuChevron.generated.h"

/** A ">" drawn as two strokes, centred in its geometry (plus the style's Offset). */
class NAMETBD_API SContextMenuChevron : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SContextMenuChevron)
		: _ArrowColor(FLinearColor::White)
	{}
		SLATE_ARGUMENT(FContextMenuChevronStyle, ChevronStyle)
		SLATE_ARGUMENT(FLinearColor, ArrowColor)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetChevronStyle(const FContextMenuChevronStyle& InStyle);
	void SetArrowColor(const FLinearColor& InColor);

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	FContextMenuChevronStyle ChevronStyle;
	FLinearColor ArrowColor = FLinearColor::White;
};

/**
 * Context Menu Chevron (palette NameTBD): the submenu arrow a native context menu row draws when its style has no Submenu
 * Arrow Image - sharp at any size and centred however tall the row is. Name it SubmenuArrow in a Context Menu Entry Widget
 * Blueprint and the row tints it with the style's icon colours for its state.
 */
UCLASS(meta = (DisplayName = "Context Menu Chevron"))
class NAMETBD_API UContextMenuChevron : public UWidget
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chevron")
	FContextMenuChevronStyle Chevron;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chevron")
	FLinearColor ArrowColor = FLinearColor::White;

	UFUNCTION(BlueprintCallable, Category = "Chevron")
	void SetArrowColor(FLinearColor InColor);

	UFUNCTION(BlueprintCallable, Category = "Chevron")
	void SetChevron(const FContextMenuChevronStyle& InChevron);

	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	TSharedPtr<SContextMenuChevron> ChevronWidget;
};
