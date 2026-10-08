// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "StyledButtons.h"
#include "Fonts/SlateFontInfo.h"

#include "ChoiceButtons.generated.h"

class UTextBlock;
class USizeBox;

/** What a Choice Button steps through. */
UENUM(BlueprintType)
enum class EChoiceButtonMode : uint8
{
	Options UMETA(ToolTip = "A list of choices, by name: Low, Medium, High."),
	Number UMETA(ToolTip = "A number between Min and Max, a Step at a time - a stepper: 60 to 120 FOV, 0 to 100% volume.")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnChoiceChanged, int32, SelectedIndex, float, Value);

/**
 * "Quality   < High >" - the selector row of every console settings menu: the setting's name, then its value between two
 * arrows. Clicking left of the value steps back and anything else forward (a key or gamepad press steps forward);
 * left and right step it while it is focused (a gamepad or the arrow keys), wrapping round or stopping at the ends. Its arrows
 * dim at an end it can't pass. Steps through a list of Options, or numbers (Mode Number) as a stepper.
 *
 * On Choice Changed reports every change, with the index (Options) and the value (Number; the index too, as steps from Min).
 */
UCLASS(meta = (DisplayName = "Choice Button"))
class NAMETBD_API UChoiceButton : public UStyledButtonBase
{
	GENERATED_BODY()

public:
	UChoiceButton(const FObjectInitializer& ObjectInitializer);

	// ---- Style -----------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides))
	EStyledButtonCorners Corners = EStyledButtonCorners::Rounded;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle && Corners == EStyledButtonCorners::Rounded", EditConditionHides, ClampMin = "0", UIMax = "32", ForceUnits = "px"))
	float CornerRounding = 8.f;

	/** Space around the row: the button's height follows it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides))
	FMargin ContentPadding = FMargin(16.f, 10.f);

	/** The width of the arrows and the value between them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides, ClampMin = "40", UIMax = "600", ForceUnits = "px"))
	float SelectorWidth = 200.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides, ClampMin = "6", UIMax = "64"))
	int32 FontSize = 15;

	/** The setting's name at the row's left. Off, the row is only the selector. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides))
	bool bShowLabel = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides))
	FLinearColor FillColor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides))
	FLinearColor OutlineColor;

	/** The label's color while idle (it brightens to Value Color on hover). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides))
	FLinearColor LabelColor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides))
	FLinearColor ValueColor;

	/** The arrows while idle (they light to the Accent Color on hover). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides))
	FLinearColor ArrowColor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides, ClampMin = "4", UIMax = "32", ForceUnits = "px"))
	float ArrowSize = 12.f;

	// ---- Choice ----------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Context Menu Button|Choice")
	EChoiceButtonMode Mode = EChoiceButtonMode::Options;

	/** The choices, in order. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Context Menu Button|Choice", meta = (EditCondition = "Mode == EChoiceButtonMode::Options", EditConditionHides))
	TArray<FText> Options;

	/** Which choice is chosen. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Context Menu Button|Choice", meta = (EditCondition = "Mode == EChoiceButtonMode::Options", EditConditionHides, ClampMin = "0"))
	int32 SelectedIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Context Menu Button|Choice", meta = (EditCondition = "Mode == EChoiceButtonMode::Number", EditConditionHides))
	float Value = 50.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Context Menu Button|Choice", meta = (EditCondition = "Mode == EChoiceButtonMode::Number", EditConditionHides))
	float MinValue = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Context Menu Button|Choice", meta = (EditCondition = "Mode == EChoiceButtonMode::Number", EditConditionHides))
	float MaxValue = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Context Menu Button|Choice", meta = (EditCondition = "Mode == EChoiceButtonMode::Number", EditConditionHides, ClampMin = "0.0001"))
	float Step = 5.f;

	/** Digits shown after the point. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Context Menu Button|Choice", meta = (EditCondition = "Mode == EChoiceButtonMode::Number", EditConditionHides, ClampMin = "0", ClampMax = "6"))
	int32 Decimals = 0;

	/** Shown after the number: %, s, x. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Context Menu Button|Choice", meta = (EditCondition = "Mode == EChoiceButtonMode::Number", EditConditionHides))
	FText Suffix;

	/** Stepping past the last goes round to the first, and back. Off, it stops at the ends and their arrow dims. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Context Menu Button|Choice")
	bool bWrap = true;

	/** Left and right step it while it is focused, rather than moving the focus. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Context Menu Button|Choice", meta = (DisplayName = "Left And Right Step It"))
	bool bLeftRightSteps = true;

	UPROPERTY(BlueprintAssignable, Category = "Context Menu Button|Choice")
	FOnChoiceChanged OnChoiceChanged;

	UFUNCTION(BlueprintCallable, Category = "Context Menu Button|Choice")
	void SelectNext();

	UFUNCTION(BlueprintCallable, Category = "Context Menu Button|Choice")
	void SelectPrevious();

	/** Steps by Steps (negative: back), wrapping or stopping as Wrap says. */
	UFUNCTION(BlueprintCallable, Category = "Context Menu Button|Choice")
	void StepBy(int32 Steps);

	UFUNCTION(BlueprintCallable, Category = "Context Menu Button|Choice")
	void SetSelectedIndex(int32 InIndex, bool bBroadcast = false);

	UFUNCTION(BlueprintCallable, Category = "Context Menu Button|Choice")
	void SetValue(float InValue, bool bBroadcast = false);

	UFUNCTION(BlueprintCallable, Category = "Context Menu Button|Choice")
	void SetOptions(const TArray<FText>& InOptions, int32 InSelectedIndex = 0);

	UFUNCTION(BlueprintPure, Category = "Context Menu Button|Choice")
	int32 GetSelectedIndex() const { return SelectedIndex; }

	UFUNCTION(BlueprintPure, Category = "Context Menu Button|Choice")
	float GetValue() const { return Value; }

	/** What is shown: the chosen option, or the number with its suffix. */
	UFUNCTION(BlueprintPure, Category = "Context Menu Button|Choice")
	FText GetDisplayText() const;

#if WITH_EDITOR
	virtual int32 GetDesignerPreviewValueCount() const override;
	virtual int32 GetDesignerPreviewValueIndex() const override;
	virtual FText GetDesignerPreviewValueText() const override;
	virtual void SetDesignerPreviewValueIndex(int32 Index, bool bAnimate) override;
#endif

protected:
	virtual void BuildParts() override;
	virtual void ApplyStyle() override;
	virtual bool HandleNavigation(EUINavigation Direction) override;
	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

	UFUNCTION()
	void HandleChoiceClicked(bool bWillDestroyCard, bool bIsEnabling);

	/** Shows the choice and dims an arrow at an end; Direction (-1, 0, 1) kicks the value in from that side. */
	void UpdateDisplay(int32 Direction);

	EActiveTimerReturnType TickKick(double InCurrentTime, float InDeltaTime);

	/** How many steps the number has from Min to Max. */
	int32 GetNumberSteps() const;

	float KickOffset = 0.f;
	bool bIsKickTimerActive = false;

	/** The designer's toolbar is stepping the preview: the value kicks in as it does at runtime. */
	bool bAnimateInDesigner = false;

	/** The value's Text Block, found when the display updates - not on every frame of its slide. */
	TWeakObjectPtr<UTextBlock> KickedValueText;
};

/**
 * "Low | Medium | High" - every choice side by side in one track, a highlight sliding to the chosen one. Click a choice to
 * pick it; left and right step it while it is focused. The choices are made from Options while the button runs (and in the
 * designer's preview), styled by the Style settings.
 */
UCLASS(meta = (DisplayName = "Segmented Choice"))
class NAMETBD_API USegmentedChoiceButton : public UStyledButtonBase
{
	GENERATED_BODY()

public:
	USegmentedChoiceButton(const FObjectInitializer& ObjectInitializer);

	// ---- Style -----------------------------------------------------------------------------------

	/** Ends fully rounded - a pill. Off, the corners are rounded by Corner Rounding. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides))
	bool bFullyRounded = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle && !bFullyRounded", EditConditionHides, ClampMin = "0", UIMax = "32", ForceUnits = "px"))
	float CornerRounding = 8.f;

	/** The gap between the highlight and the track's edge. The highlight's corners follow the track's, this far in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides, ClampMin = "0", UIMax = "12", ForceUnits = "px"))
	float Inset = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides))
	FLinearColor FillColor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "bUseStyle", EditConditionHides))
	FLinearColor OutlineColor;

	/** Space around each choice's text: the button's size follows it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style")
	FMargin SegmentPadding = FMargin(14.f, 7.f);

	/** The choices' font. Left without a font, the Text Block default at size 14. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style")
	FSlateFontInfo Font;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style")
	FLinearColor TextColor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style")
	FLinearColor SelectedTextColor;

	// ---- Choice ----------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Context Menu Button|Choice")
	TArray<FText> Options;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Context Menu Button|Choice", meta = (ClampMin = "0"))
	int32 SelectedIndex = 0;

	/** Left and right step it while it is focused, rather than moving the focus. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Context Menu Button|Choice", meta = (DisplayName = "Left And Right Step It"))
	bool bLeftRightSteps = true;

	UPROPERTY(BlueprintAssignable, Category = "Context Menu Button|Choice")
	FOnChoiceChanged OnChoiceChanged;

	UFUNCTION(BlueprintCallable, Category = "Context Menu Button|Choice")
	void SetSelectedIndex(int32 InIndex, bool bBroadcast = false);

	UFUNCTION(BlueprintCallable, Category = "Context Menu Button|Choice")
	void SetOptions(const TArray<FText>& InOptions, int32 InSelectedIndex = 0);

	UFUNCTION(BlueprintPure, Category = "Context Menu Button|Choice")
	int32 GetSelectedIndex() const { return SelectedIndex; }

	UFUNCTION(BlueprintPure, Category = "Context Menu Button|Choice")
	FText GetSelectedOption() const;

#if WITH_EDITOR
	virtual int32 GetDesignerPreviewValueCount() const override { return Options.Num(); }
	virtual int32 GetDesignerPreviewValueIndex() const override { return SelectedIndex; }
	virtual FText GetDesignerPreviewValueText() const override { return GetSelectedOption(); }
	virtual void SetDesignerPreviewValueIndex(int32 Index, bool bAnimate) override;
#endif

protected:
	virtual void BuildParts() override;
	virtual void ApplyStyle() override;
	virtual bool HandleNavigation(EUINavigation Direction) override;
	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

	UFUNCTION()
	void HandleSegmentClicked(bool bWillDestroyCard, bool bIsEnabling);

	/** Makes a Text Block per option in the Segments box when the options changed, and styles them. */
	void RebuildSegments();

	/** Finds the parts the slide moves, once per build - not on every frame of it. */
	void CacheSlideParts();

	/** Places the highlight at Position (in choices) and colors the texts by how near each is. */
	void PlaceHighlight(float Position);

	/** How fast the highlight slides, from Motion: higher is quicker, 0 at once. */
	float GetSlideSpeed() const;

	void StartSlide();
	EActiveTimerReturnType TickSlide(double InCurrentTime, float InDeltaTime);

	UPROPERTY(Transient, DuplicateTransient)
	TArray<TObjectPtr<UTextBlock>> SegmentTexts;

	/** The options the segments were made from, to tell when they need making again. */
	TArray<FString> BuiltOptions;

	float HighlightPosition = 0.f;
	bool bHasPlacedHighlight = false;

	/** Ticks the slide has waited for the track to be laid out. */
	int32 UnplacedTicks = 0;

	TWeakObjectPtr<UWidget> CachedSegments;
	TWeakObjectPtr<UWidget> CachedHighlight;
	TWeakObjectPtr<USizeBox> CachedHighlightBox;
	bool bHasCachedSlideParts = false;
	bool bIsSlideTimerActive = false;

	/** The designer's toolbar picked a choice: the highlight slides there as it does at runtime (else the designer snaps). */
	bool bAnimateInDesigner = false;
};
