// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Application/IInputProcessor.h"
#include "Widgets/SCompoundWidget.h"
#include "NameTBD/Menu/Settings/MenuSetting.h"
#include "SMenuControls.h"

class SMenuKeyGlyph;
class SMenuSelector;
class UMenuSettingRowWidget;
class SMenuSettingRow;
class STextBlock;
class UMenuSettingsSubsystem;

/** The sound a settings screen plays for an event. */
enum class EMenuSoundEvent : uint8
{
	Navigate,
	Change,
	Accept,
	Back,
	Tab,
	Error
};

/**
 * What a setting row needs from what holds it: the player's settings (none in the designer: preview values), the style, and the
 * services only a whole screen has (a dropdown list, a confirmation, the description panel, sounds, narration).
 */
class NAMETBD_API IMenuSettingsRowHost
{
public:
	virtual ~IMenuSettingsRowHost() = default;

	virtual UMenuSettingsSubsystem* GetSettings() const = 0;
	virtual FMenuSettingContext GetContext() const = 0;
	virtual FMenuStyleRef GetStyle() const = 0;
	virtual int32 GetUserIndex() const { return 0; }

	/** Values when there is no subsystem (the designer). */
	virtual FString GetPreviewValue(const UMenuSetting& Setting) const;
	virtual void SetPreviewValue(const UMenuSetting& Setting, const FString& Value) {}

	virtual void OnRowFocused(const TSharedRef<SMenuSettingRow>& Row) {}
	virtual void OnRowHovered(const TSharedRef<SMenuSettingRow>& Row, bool bHovered) {}

	/** Shows Row's options as a list. False: the host can't (the row steps instead). */
	virtual bool OpenOptionList(const TSharedRef<SMenuSettingRow>& Row) { return false; }

	/** Asks before OnYes (an action's Confirm Text). The default does it at once. */
	virtual void Confirm(const FText& Title, const FText& Body, TFunction<void()> OnYes) { OnYes(); }

	virtual void PlaySound(EMenuSoundEvent Event) {}
	virtual void Announce(const FText& Text) {}

	/** A key binding row started (true) or stopped waiting for a key. */
	virtual void OnKeyCaptureChanged(const TSharedRef<SMenuSettingRow>& Row, bool bCapturing) {}
};

/** Sees the next key, button or wheel for one Slate user before anything else and hands it to a key binding row. */
class FMenuKeyCaptureProcessor : public IInputProcessor
{
public:
	FMenuKeyCaptureProcessor(const TSharedRef<SMenuSettingRow>& InRow, int32 InUserIndex) : Row(InRow), UserIndex(InUserIndex) {}

	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override;
	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override;
	virtual bool HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override;
	virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
	virtual bool HandleMouseButtonDoubleClickEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
	virtual bool HandleMouseWheelOrGestureEvent(FSlateApplication& SlateApp, const FPointerEvent& InWheelEvent, const FPointerEvent* InGestureEvent) override;
	virtual bool HandleAnalogInputEvent(FSlateApplication& SlateApp, const FAnalogInputEvent& InAnalogInputEvent) override;
	virtual const TCHAR* GetDebugName() const override { return TEXT("NameTBDKeyCapture"); }

private:
	TWeakPtr<SMenuSettingRow> Row;
	int32 UserIndex = 0;
	float Elapsed = 0.f;
};

/**
 * One setting as a row: its name on the left, its control on the right (switch, slider, selector, dropdown, buttons, key slots,
 * an action button, a read-only value), drawn in the menu style. The row takes focus (a pad or keys move between rows): left /
 * right change its value, Accept toggles / opens / rebinds / runs. A row whose setting can't be changed now stays focusable,
 * greyed out, so the description can say why.
 */
class NAMETBD_API SMenuSettingRow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMenuSettingRow) {}
		/** Your UMG widget as the whole row: the row draws nothing itself and keeps doing the work (focus, steps, sounds, narration). */
		SLATE_ARGUMENT(TSharedPtr<SWidget>, CustomRow)
		/** Your UMG widget as the control on the right of the built-in row. */
		SLATE_ARGUMENT(TSharedPtr<SWidget>, CustomControl)
		/** The UMG widget told about the row's state, its steps and Accept. */
		SLATE_ARGUMENT(TWeakObjectPtr<UMenuSettingRowWidget>, CustomWidget)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, IMenuSettingsRowHost* InHost, UMenuSetting* InSetting);
	virtual ~SMenuSettingRow() override;

	/** Reads the value, options and state again (after any setting changed). */
	void Refresh();

	UMenuSetting* GetSetting() const { return Setting.Get(); }
	FName GetSettingId() const { return SettingId; }
	FString GetValue() const { return CurrentValue; }
	FText GetValueText() const { return ValueText; }
	bool IsSettingEnabled() const { return bSettingEnabled; }
	FText GetDisabledReason() const { return DisabledReason; }
	const TArray<FMenuSettingOption>& GetOptions() const { return Options; }
	int32 GetOptionIndex() const { return OptionIndex; }

	/** A message under the name for a few seconds (a swapped key, a refused one). */
	void ShowMessage(const FText& Message, bool bWarning);
	FText GetMessage() const { return Message; }

	/** Sets the value (made valid), plays the change sound, reads it out. */
	void ChangeValue(const FString& Value);

	/** Left / right. */
	void Step(int32 Direction);

	/** Accept / click on the row. */
	void Activate();

	/** Key binding rows. */
	void BeginCapture(int32 SlotIndex);
	void EndCapture(const FKey& Key);
	void CancelCapture();

	/** From the capture processor: handled next frame (not while Slate is iterating its processors). bCancel: give up. */
	void QueueCaptureKey(const FKey& Key, bool bCancel);
	bool IsCapturing() const { return bCapturing; }
	EMenuKeyBindingDevice GetCaptureDevice() const;

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent) override;
	virtual void OnFocusLost(const FFocusEvent& InFocusEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FNavigationReply OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	TSharedRef<SWidget> BuildControl();
	TSharedRef<SWidget> BuildKeySlots();
	bool UsesDropdown() const;
	bool UsesButtons() const;
	FString ReadValue() const;
	void RunAction();
	EActiveTimerReturnType Animate(double InCurrentTime, float InDeltaTime);
	void StartAnimating();
	FMenuPartState GetPartState() const;
	bool IsRowFocused() const;

	/** Tells a custom widget the row's state when it changed. */
	void NotifyCustomWidget();

	TWeakObjectPtr<UMenuSettingRowWidget> CustomWidget;
	bool bCustomRow = false;

	IMenuSettingsRowHost* Host = nullptr;
	TWeakObjectPtr<UMenuSetting> Setting;
	FName SettingId;
	FMenuStyleRef StyleHolder = MakeShared<FMenuStyleHolder>();

	FString CurrentValue;
	FText ValueText;
	TArray<FMenuSettingOption> Options;
	int32 OptionIndex = INDEX_NONE;
	bool bSettingEnabled = true;
	bool bModified = false;
	FText DisabledReason;

	FText Message;
	bool bMessageWarning = false;
	double MessageUntil = 0.0;

	/** Key binding rows: the slot left / right moved to, and capture. */
	int32 SelectedSlot = 0;
	bool bCapturing = false;
	int32 CaptureSlot = 0;
	TSharedPtr<FMenuKeyCaptureProcessor> CaptureProcessor;
	TArray<TSharedPtr<SMenuKeyGlyph>> SlotGlyphs;
	TArray<FKey> SlotKeys;
	FKey QueuedKey;
	bool bQueuedCancel = false;
	bool bQueued = false;
	EActiveTimerReturnType HandleQueuedCapture(double InCurrentTime, float InDeltaTime);
	void StopCaptureProcessor();

	TSharedPtr<SMenuSelector> SelectorWidget;
	TSharedPtr<SMenuSwitch> SwitchWidget;
	FWidgetMotionValue Highlight;
	FMenuSkinPainter SkinPainter;
	bool bAnimating = false;
	bool bPressedVisual = false;
};
