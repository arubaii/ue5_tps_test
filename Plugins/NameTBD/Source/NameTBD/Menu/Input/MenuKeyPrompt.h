// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "MenuInputTypes.h"
#include "NameTBD/Widgets/WidgetSVGBrush.h"

#include "MenuKeyPrompt.generated.h"

class STextBlock;
class UInputAction;

/** How a key without art is drawn: a rounded cap with its name. */
USTRUCT(BlueprintType)
struct NAMETBD_API FMenuKeyCapStyle
{
	GENERATED_BODY()

	/** Height of a glyph or cap; a cap grows sideways to fit its name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Key Cap", meta = (ClampMin = "8", UIMax = "96"))
	float Height = 26.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Key Cap")
	FSlateFontInfo Font;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Key Cap")
	FLinearColor CapColor = FLinearColor(1.f, 1.f, 1.f, 0.12f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Key Cap")
	FLinearColor OutlineColor = FLinearColor(1.f, 1.f, 1.f, 0.55f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Key Cap", meta = (ClampMin = "0", UIMax = "4"))
	float OutlineWidth = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Key Cap")
	FLinearColor TextColor = FLinearColor::White;

	/** Corner radius as a share of the height (0.5: a pill; gamepad face buttons are drawn round). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Key Cap", meta = (ClampMin = "0", ClampMax = "0.5"))
	float CornerRadius = 0.22f;

	FMenuKeyCapStyle();
};

/** One key's glyph: its art (FMenuImage), else a key cap with its label. */
class NAMETBD_API SMenuKeyGlyph : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SMenuKeyGlyph) {}
		SLATE_ARGUMENT(FMenuKeyCapStyle, CapStyle)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetGlyph(const FMenuKeyGlyph& InGlyph);
	void SetCapStyle(const FMenuKeyCapStyle& InStyle);

	/** 0..1 drawn as a fill rising inside the cap (hold to confirm). */
	void SetHoldProgress(float InProgress);

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	FText GetCapText() const;

	FMenuKeyGlyph Glyph;
	FMenuKeyCapStyle CapStyle;
	FSlateBrush ImageBrush;
	mutable FWidgetSVGBrush SVGBrush;
	float HoldProgress = 0.f;

	/** Gamepad face buttons are drawn as circles. */
	bool bRound = false;
};

DECLARE_DELEGATE(FOnMenuKeyPromptClicked);

/** A prompt: the glyph for a key, and what it does ("[A] Accept"). Clickable when bound. */
class NAMETBD_API SMenuKeyPrompt : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMenuKeyPrompt)
		: _Spacing(8.f)
		, _LabelColor(FLinearColor::White)
	{}
		SLATE_ARGUMENT(FMenuKeyCapStyle, CapStyle)
		SLATE_ARGUMENT(FSlateFontInfo, LabelFont)
		SLATE_ARGUMENT(float, Spacing)
		SLATE_ATTRIBUTE(FText, Label)
		SLATE_ATTRIBUTE(FSlateColor, LabelColor)
		SLATE_EVENT(FOnMenuKeyPromptClicked, OnClicked)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetGlyph(const FMenuKeyGlyph& InGlyph);
	void SetCapStyle(const FMenuKeyCapStyle& InStyle);
	void SetLabelFont(const FSlateFontInfo& InFont);
	void SetHoldProgress(float InProgress);

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;

private:
	TSharedPtr<SMenuKeyGlyph> GlyphWidget;
	TSharedPtr<STextBlock> LabelWidget;
	FOnMenuKeyPromptClicked ClickedDelegate;
	bool bPressed = false;
};

/** Which device's key a prompt shows. */
UENUM(BlueprintType)
enum class EMenuKeyPromptDevice : uint8
{
	Current UMETA(ToolTip = "The device the player is using now: switches with them."),
	KeyboardMouse UMETA(DisplayName = "Keyboard & Mouse"),
	Gamepad
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMenuKeyPromptClickedEvent);

/**
 * Menu Key Prompt (palette NameTBD): the button to press for an input - "[A] Accept", "[Esc] Back", "[E] Interact" - that
 * follows the device the player uses (keyboard, Xbox, PlayStation, Switch) and their rebinding. Give it an Input Action (its
 * mapped key is found), or keys of your own. Art comes from the project's Key Glyph Sets; keys without art show as key caps.
 */
UCLASS(meta = (DisplayName = "Menu Key Prompt"))
class NAMETBD_API UMenuKeyPrompt : public UWidget
{
	GENERATED_BODY()

public:

	UMenuKeyPrompt();

	/** The action whose key shows (the player's binding on the device shown). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Prompt")
	TObjectPtr<UInputAction> InputAction;

	/** Shown on keyboard and mouse when there is no action (or it has no key there). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Prompt")
	FKey KeyboardKey;

	/** Shown on a gamepad when there is no action (or it has no key there). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Prompt")
	FKey GamepadKey;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Prompt")
	EMenuKeyPromptDevice ShowDevice = EMenuKeyPromptDevice::Current;

	/** What pressing it does ("Accept"). Empty: just the glyph. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Prompt")
	FText Label;

	/** Collapsed when there is no key to show. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Prompt")
	bool bHideWhenUnbound = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	FMenuKeyCapStyle KeyCap;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	FSlateFontInfo LabelFont;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	FLinearColor LabelColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance", meta = (ClampMin = "0", UIMax = "32"))
	float Spacing = 8.f;

	/** The prompt was clicked (or tapped): do what its key does. */
	UPROPERTY(BlueprintAssignable, Category = "Prompt")
	FOnMenuKeyPromptClickedEvent OnPromptClicked;

	UFUNCTION(BlueprintCallable, Category = "Menu|Prompt")
	void SetInputAction(UInputAction* InAction);

	UFUNCTION(BlueprintCallable, Category = "Menu|Prompt")
	void SetKeys(FKey InKeyboardKey, FKey InGamepadKey);

	UFUNCTION(BlueprintCallable, Category = "Menu|Prompt")
	void SetLabel(FText InLabel);

	/** 0..1: a hold-to-confirm fill inside the glyph. */
	UFUNCTION(BlueprintCallable, Category = "Menu|Prompt")
	void SetHoldProgress(float InProgress);

	/** The key shown now. */
	UFUNCTION(BlueprintPure, Category = "Menu|Prompt")
	FKey GetShownKey() const { return ShownKey; }

	/** Looks the key up again (called by itself on device and binding changes). */
	UFUNCTION(BlueprintCallable, Category = "Menu|Prompt")
	void RefreshPrompt();

	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	void HandleDeviceChanged(EMenuInputDevice Device);

	UFUNCTION()
	void HandleBindingsChanged();

	void HandleClicked();

	TSharedPtr<SMenuKeyPrompt> PromptWidget;
	FKey ShownKey;
	FDelegateHandle DeviceHandle;
	bool bBound = false;
};
