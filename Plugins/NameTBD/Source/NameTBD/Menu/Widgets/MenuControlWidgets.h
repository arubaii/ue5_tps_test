// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "SMenuControls.h"

#include "MenuControlWidgets.generated.h"

class SMenuControlHost;
class UMenuSettingsSubsystem;
class UMenuStyleAsset;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMenuSwitchChangedEvent, bool, bNewOn);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMenuSliderChangedEvent, float, NewValue);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMenuSelectionChangedEvent, int32, NewIndex, const FText&, NewOption);

/**
 * The base of the standalone menu controls (palette NameTBD): a Settings Menu's switch, slider, selector and segmented buttons,
 * anywhere in your own UMG - drawn in a Menu Style (its colours, sizes and skin), focusable (left / right change it, Accept
 * toggles), and optionally bound to a setting by Id, so they show and change the player's setting with no Blueprint.
 */
UCLASS(Abstract)
class NAMETBD_API UMenuControlWidgetBase : public UWidget
{
	GENERATED_BODY()

public:

	/** The look. Empty: the project's Default Style, else Simple. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Menu Control")
	TObjectPtr<UMenuStyleAsset> StyleAsset;

	/**
	 * A setting this control shows and changes ("Audio.MusicVolume", "Accessibility.Subtitles"): a switch takes a Bool, a slider
	 * a Number (its range), a selector or segmented control a Choice (its options). None: the control's own value, yours to use.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Menu Control")
	FName SettingId;

	UFUNCTION(BlueprintCallable, Category = "Menu|Controls")
	void SetStyleAsset(UMenuStyleAsset* InStyleAsset);

	UFUNCTION(BlueprintCallable, Category = "Menu|Controls")
	void SetSettingId(FName InSettingId);

	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

	/** The control itself (inside a focusable host). */
	virtual TSharedRef<SWidget> MakeControl() PURE_VIRTUAL(UMenuControlWidgetBase::MakeControl, return SNullWidget::NullWidget;);

	/** Left / right on the focused control. */
	virtual void StepControl(int32 Direction) {}

	/** Accept on the focused control. */
	virtual void ActivateControl() {}

	/** The bound setting's value arrived (or changed): show it. */
	virtual void ShowSettingValue(const FString& Value) {}

	UMenuSettingsSubsystem* FindSettings() const;

	/** Writes the bound setting (when bound). */
	void WriteSetting(const FString& Value);

	/** The bound setting's current value, or empty. */
	FString ReadSetting() const;

	bool IsBound() const { return !SettingId.IsNone(); }

	FMenuStyleRef StyleHolder = MakeShared<FMenuStyleHolder>();
	TSharedPtr<SMenuControlHost> Host;

private:
	void BindSetting();
	void UnbindSetting();
	void HandleStyleChanged(const UMenuStyleAsset* Changed);

	TWeakObjectPtr<UMenuSettingsSubsystem> BoundSettings;
	FDelegateHandle ChangedHandle;
	FDelegateHandle StyleHandle;
};

/** Menu Switch: an on / off switch in a menu style. */
UCLASS(meta = (DisplayName = "Menu Switch"))
class NAMETBD_API UMenuSwitch : public UMenuControlWidgetBase
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Switch")
	bool bOn = false;

	UPROPERTY(BlueprintAssignable, Category = "Switch")
	FOnMenuSwitchChangedEvent OnSwitchChanged;

	UFUNCTION(BlueprintCallable, Category = "Menu|Controls")
	void SetOn(bool bNewIsOn);

	UFUNCTION(BlueprintPure, Category = "Menu|Controls")
	bool IsOn() const { return bOn; }

protected:
	virtual TSharedRef<SWidget> MakeControl() override;
	virtual void StepControl(int32 Direction) override;
	virtual void ActivateControl() override;
	virtual void ShowSettingValue(const FString& Value) override;

private:
	void Change(bool bNewIsOn);
};

/** Menu Slider: a track, fill and knob in a menu style (drag, click, left / right by Step Size). */
UCLASS(meta = (DisplayName = "Menu Slider"))
class NAMETBD_API UMenuSlider : public UMenuControlWidgetBase
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slider")
	float Value = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slider")
	float MinValue = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slider")
	float MaxValue = 1.f;

	/** Left / right move by this; values snap to it (0: no snapping, left / right move by a twentieth). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slider", meta = (ClampMin = "0"))
	float StepSize = 0.05f;

	/** While dragging. */
	UPROPERTY(BlueprintAssignable, Category = "Slider")
	FOnMenuSliderChangedEvent OnSliderChanged;

	/** The drag ended, or a step was made. */
	UPROPERTY(BlueprintAssignable, Category = "Slider")
	FOnMenuSliderChangedEvent OnSliderCommitted;

	UFUNCTION(BlueprintCallable, Category = "Menu|Controls")
	void SetValue(float InValue);

	UFUNCTION(BlueprintPure, Category = "Menu|Controls")
	float GetValue() const { return Value; }

protected:
	virtual TSharedRef<SWidget> MakeControl() override;
	virtual void StepControl(int32 Direction) override;
	virtual void ShowSettingValue(const FString& InSettingValue) override;

private:
	float Snap(float InValue) const;
	float ToFraction() const;
	void Change(float NewValue, bool bCommit);
};

/** Menu Selector: "< High >" with arrows and pips, in a menu style. */
UCLASS(meta = (DisplayName = "Menu Selector"))
class NAMETBD_API UMenuSelector : public UMenuControlWidgetBase
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Selector")
	TArray<FText> Options;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Selector")
	int32 SelectedIndex = 0;

	/** Right past the last goes to the first. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Selector")
	bool bWrap = false;

	UPROPERTY(BlueprintAssignable, Category = "Selector")
	FOnMenuSelectionChangedEvent OnSelectionChanged;

	UFUNCTION(BlueprintCallable, Category = "Menu|Controls")
	void SetSelectedIndex(int32 InIndex);

	UFUNCTION(BlueprintCallable, Category = "Menu|Controls")
	void SetOptions(const TArray<FText>& InOptions);

	UFUNCTION(BlueprintPure, Category = "Menu|Controls")
	FText GetSelectedOption() const;

protected:
	virtual TSharedRef<SWidget> MakeControl() override;
	virtual void StepControl(int32 Direction) override;
	virtual void ActivateControl() override { StepControl(1); }
	virtual void ShowSettingValue(const FString& Value) override;

	/** Bound: the setting's option values, by index. */
	TArray<FString> OptionValues;

	void Select(int32 NewIndex);
};

/** Menu Segmented Control: options side by side (Off | Low | High), the picked one filled, in a menu style. */
UCLASS(meta = (DisplayName = "Menu Segmented Control"))
class NAMETBD_API UMenuSegmentedControl : public UMenuSelector
{
	GENERATED_BODY()

protected:
	virtual TSharedRef<SWidget> MakeControl() override;
};

/** Holds a standalone menu control: takes focus, steps on left / right, Accept activates, draws the focus outline. */
class NAMETBD_API SMenuControlHost : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMenuControlHost) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_EVENT(FOnMenuSelectorStep, OnStep)
		SLATE_EVENT(FOnMenuControlClicked, OnActivate)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const FMenuStyleRef& InStyle);

	void SetContent(const TSharedRef<SWidget>& InContent);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FNavigationReply OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	FMenuStyleRef StyleHolder = MakeShared<FMenuStyleHolder>();
	FOnMenuSelectorStep StepDelegate;
	FOnMenuControlClicked ActivateDelegate;
};
