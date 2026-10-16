// Copyright AR GameSystems 2026. All rights reserved.

#include "SMenuSettingRow.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/LocalPlayer.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "NameTBD/Menu/Input/MenuInputSubsystem.h"
#include "NameTBD/Menu/Input/MenuKeyPrompt.h"
#include "NameTBD/Menu/Settings/MenuProjectSettings.h"
#include "NameTBD/Menu/Settings/MenuSettingTargets.h"
#include "NameTBD/Menu/Settings/MenuSettingsSubsystem.h"
#include "SettingsMenu.h"

#define LOCTEXT_NAMESPACE "SMenuSettingRow"

FString IMenuSettingsRowHost::GetPreviewValue(const UMenuSetting& Setting) const
{
	return Setting.GetDefaultValue(GetContext());
}

// =====================================================================================================
//  Key capture
// =====================================================================================================

void FMenuKeyCaptureProcessor::Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor)
{
	Elapsed += DeltaTime;
	if (Elapsed > UMenuProjectSettings::Get().KeyCaptureTimeout)
	{
		Elapsed = -1000.f;
		if (const TSharedPtr<SMenuSettingRow> Pinned = Row.Pin())
		{
			Pinned->QueueCaptureKey(FKey(), true);
		}
	}
}

bool FMenuKeyCaptureProcessor::HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
	if (static_cast<int32>(InKeyEvent.GetUserIndex()) != UserIndex)
	{
		return false;
	}
	if (const TSharedPtr<SMenuSettingRow> Pinned = Row.Pin())
	{
		if (!InKeyEvent.IsRepeat())
		{
			const FKey Key = InKeyEvent.GetKey();
			const bool bCancel = Key == EKeys::Escape || (Pinned->GetCaptureDevice() == EMenuKeyBindingDevice::Gamepad && Key == EKeys::Gamepad_Special_Right);
			Pinned->QueueCaptureKey(Key, bCancel);
		}
		return true;
	}
	return false;
}

bool FMenuKeyCaptureProcessor::HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
	return false;
}

bool FMenuKeyCaptureProcessor::HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	if (static_cast<int32>(MouseEvent.GetUserIndex()) != UserIndex || MouseEvent.IsTouchEvent())
	{
		return false;
	}
	if (const TSharedPtr<SMenuSettingRow> Pinned = Row.Pin())
	{
		if (Pinned->GetCaptureDevice() == EMenuKeyBindingDevice::KeyboardMouse)
		{
			Pinned->QueueCaptureKey(MouseEvent.GetEffectingButton(), false);
		}
		return true;
	}
	return false;
}

bool FMenuKeyCaptureProcessor::HandleMouseButtonDoubleClickEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	return Row.IsValid() && static_cast<int32>(MouseEvent.GetUserIndex()) == UserIndex;
}

bool FMenuKeyCaptureProcessor::HandleMouseWheelOrGestureEvent(FSlateApplication& SlateApp, const FPointerEvent& InWheelEvent, const FPointerEvent* InGestureEvent)
{
	if (static_cast<int32>(InWheelEvent.GetUserIndex()) != UserIndex || InGestureEvent)
	{
		return false;
	}
	if (const TSharedPtr<SMenuSettingRow> Pinned = Row.Pin())
	{
		if (Pinned->GetCaptureDevice() == EMenuKeyBindingDevice::KeyboardMouse && InWheelEvent.GetWheelDelta() != 0.f)
		{
			Pinned->QueueCaptureKey(InWheelEvent.GetWheelDelta() > 0.f ? EKeys::MouseScrollUp : EKeys::MouseScrollDown, false);
		}
		return true;
	}
	return false;
}

bool FMenuKeyCaptureProcessor::HandleAnalogInputEvent(FSlateApplication& SlateApp, const FAnalogInputEvent& InAnalogInputEvent)
{
	// Sticks don't move focus away while a key is awaited.
	return Row.IsValid() && static_cast<int32>(InAnalogInputEvent.GetUserIndex()) == UserIndex;
}

// =====================================================================================================
//  Row
// =====================================================================================================

void SMenuSettingRow::Construct(const FArguments& InArgs, IMenuSettingsRowHost* InHost, UMenuSetting* InSetting)
{
	Host = InHost;
	Setting = InSetting;
	SettingId = InSetting ? InSetting->Id : NAME_None;
	StyleHolder = Host->GetStyle();
	Highlight.Snap(0.f);
	Refresh();

	CustomWidget = InArgs._CustomWidget;
	bCustomRow = InArgs._CustomRow.IsValid();
	if (bCustomRow)
	{
		ChildSlot
		[
			InArgs._CustomRow.ToSharedRef()
		];
		NotifyCustomWidget();
		return;
	}

	const TSharedRef<SWidget> Control = InArgs._CustomControl.IsValid() ? InArgs._CustomControl.ToSharedRef() : BuildControl();
	const FMenuStyleRef Holder = StyleHolder;
	ChildSlot
	[
		SNew(SBox)
		.HeightOverride_Lambda([Holder]() { return FOptionalSize(Holder->Style.RowHeight); })
		.Padding_Lambda([Holder]() { return Holder->Style.RowPadding; })
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			.Padding(0.f, 0.f, 16.f, 0.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text_Lambda([Holder, Label = InSetting ? InSetting->GetLabel() : FText::GetEmpty()]() { return Holder->Style.bUppercaseLabels ? Label.ToUpper() : Label; })
					.Font_Lambda([Holder]() { return Holder->Style.LabelFont; })
					.ColorAndOpacity_Lambda([this, Holder]() { return FSlateColor(bSettingEnabled ? Holder->Palette().Text : Holder->Palette().DisabledText); })
					.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text_Lambda([this]() { return GetMessage(); })
					.Font_Lambda([Holder]() { return Holder->Style.HintFont; })
					.ColorAndOpacity_Lambda([this, Holder]() { return FSlateColor(bMessageWarning ? Holder->Palette().Warning : Holder->Palette().MutedText); })
					.Visibility_Lambda([this]() { return Message.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride_Lambda([Holder]() { return FOptionalSize(Holder->Style.ValueWidth); })
				.VAlign(VAlign_Center)
				[
					Control
				]
			]
		]
	];
	if (SwitchWidget.IsValid())
	{
		SwitchWidget->SetFocusedAttribute(TAttribute<bool>::CreateSP(this, &SMenuSettingRow::IsRowFocused));
	}
	NotifyCustomWidget();
}

bool SMenuSettingRow::IsRowFocused() const
{
	return HasAnyUserFocus().IsSet();
}

void SMenuSettingRow::NotifyCustomWidget()
{
	UMenuSettingRowWidget* Widget = CustomWidget.Get();
	if (!Widget)
	{
		return;
	}
	FMenuSettingRowState State;
	State.bHovered = IsHovered();
	State.bFocused = HasAnyUserFocus().IsSet();
	State.bPressed = bPressedVisual;
	State.bEnabled = bSettingEnabled;
	State.bModified = bModified;
	State.bWaitingForKey = bCapturing;
	Widget->SetRowState(State);
}

SMenuSettingRow::~SMenuSettingRow()
{
	// A row going away mid-capture (the screen closed) must not leave its key catcher registered.
	StopCaptureProcessor();
}

bool SMenuSettingRow::UsesDropdown() const
{
	const UMenuSettingChoice* Choice = Cast<UMenuSettingChoice>(Setting.Get());
	if (!Choice)
	{
		return false;
	}
	switch (Choice->Display)
	{
	case EMenuChoiceDisplay::Dropdown: return true;
	case EMenuChoiceDisplay::Auto: return Options.Num() > StyleHolder->Style.SelectorMaxOptions;
	default: return false;
	}
}

bool SMenuSettingRow::UsesButtons() const
{
	const UMenuSettingChoice* Choice = Cast<UMenuSettingChoice>(Setting.Get());
	return Choice && Choice->Display == EMenuChoiceDisplay::Buttons && Options.Num() <= 4;
}

TSharedRef<SWidget> SMenuSettingRow::BuildControl()
{
	UMenuSetting* Current = Setting.Get();
	if (!Current)
	{
		return SNullWidget::NullWidget;
	}
	const FMenuStyleRef Holder = StyleHolder;
	const TAttribute<bool> Enabled = TAttribute<bool>::CreateLambda([this]() { return bSettingEnabled; });
	auto ValueLabel = [this, Holder](EHorizontalAlignment Align)
	{
		return SNew(STextBlock)
			.Text_Lambda([this]() { return ValueText; })
			.Font_Lambda([Holder]() { return Holder->Style.ValueFont; })
			.ColorAndOpacity_Lambda([this, Holder]() { return FSlateColor(bSettingEnabled ? Holder->Palette().Text : Holder->Palette().DisabledText); })
			.Justification(Align == HAlign_Right ? ETextJustify::Right : ETextJustify::Left);
	};

	switch (Current->GetKind())
	{
	case EMenuSettingKind::Bool:
		if (StyleHolder->Style.BoolDisplay == EMenuBoolDisplay::Switch)
		{
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				.Padding(0.f, 0.f, 12.f, 0.f)
				[
					ValueLabel(HAlign_Right)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SAssignNew(SwitchWidget, SMenuSwitch, StyleHolder)
					.IsEnabled(Enabled)
					.IsOn_Lambda([this]() { return MenuSettingValues::ToBool(CurrentValue); })
					.OnToggled_Lambda([this](bool bOn) { ChangeValue(MenuSettingValues::FromBool(bOn)); })
				];
		}
		return SAssignNew(SelectorWidget, SMenuSelector, StyleHolder)
			.IsEnabled(Enabled)
			.ValueText_Lambda([this]() { return ValueText; })
			.OptionCount(2)
			.OptionIndex_Lambda([this]() { return MenuSettingValues::ToBool(CurrentValue) ? 1 : 0; })
			.OnStep_Lambda([this](int32 Direction) { Step(Direction); });

	case EMenuSettingKind::Number:
	{
		const UMenuSettingNumber* Number = CastChecked<UMenuSettingNumber>(Current);
		const TWeakObjectPtr<const UMenuSettingNumber> WeakNumber = Number;
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			[
				SNew(SMenuSlider, StyleHolder)
				.IsEnabled(Enabled)
				.Fraction_Lambda([this, WeakNumber]() { return WeakNumber.IsValid() ? WeakNumber->ToFraction(CurrentValue) : 0.f; })
				.OnChanged_Lambda([this, WeakNumber](float Fraction)
				{
					if (WeakNumber.IsValid())
					{
						ChangeValue(WeakNumber->FromFraction(Fraction));
					}
				})
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(12.f, 0.f, 0.f, 0.f)
			[
				SNew(SBox)
				.MinDesiredWidth(64.f)
				.HAlign(HAlign_Right)
				[
					ValueLabel(HAlign_Right)
				]
			];
	}

	case EMenuSettingKind::Choice:
		if (UsesButtons())
		{
			TSharedRef<SHorizontalBox> Buttons = SNew(SHorizontalBox);
			for (int32 Index = 0; Index < Options.Num(); ++Index)
			{
				const FString OptionValue = Options[Index].Value;
				Buttons->AddSlot()
					.FillWidth(1.f)
					.Padding(Index > 0 ? 4.f : 0.f, 0.f, 0.f, 0.f)
					[
						SNew(SMenuButton, StyleHolder)
						.Kind(EMenuButtonKind::Segment)
						.IsFocusable(false)
						.IsEnabled(Enabled)
						.Text(Options[Index].GetLabel())
						.IsSelected_Lambda([this, OptionValue]() { return CurrentValue.Equals(OptionValue, ESearchCase::IgnoreCase); })
						.OnClicked_Lambda([this, OptionValue]() { ChangeValue(OptionValue); })
					];
			}
			return Buttons;
		}
		return SAssignNew(SelectorWidget, SMenuSelector, StyleHolder)
			.IsEnabled(Enabled)
			.IsDropdown(UsesDropdown())
			.ValueText_Lambda([this]() { return ValueText; })
			.OptionCount_Lambda([this]() { return Options.Num(); })
			.OptionIndex_Lambda([this]() { return OptionIndex; })
			.CanStepLeft_Lambda([this]()
			{
				const UMenuSettingChoice* Choice = Cast<UMenuSettingChoice>(Setting.Get());
				return Choice && (Choice->bWrap || OptionIndex != 0);
			})
			.CanStepRight_Lambda([this]()
			{
				const UMenuSettingChoice* Choice = Cast<UMenuSettingChoice>(Setting.Get());
				return Choice && (Choice->bWrap || OptionIndex != Options.Num() - 1);
			})
			.OnStep_Lambda([this](int32 Direction) { Step(Direction); })
			.OnOpen_Lambda([this]()
			{
				if (!Host->OpenOptionList(SharedThis(this)))
				{
					Step(1);
				}
			});

	case EMenuSettingKind::KeyBinding:
		return BuildKeySlots();

	case EMenuSettingKind::Action:
		return SNew(SBox)
			.HAlign(HAlign_Right)
			[
				SNew(SMenuButton, StyleHolder)
				.Kind(EMenuButtonKind::Secondary)
				.IsFocusable(false)
				.IsEnabled(Enabled)
				.MinWidth(140.f)
				.Text_Lambda([this]() { return ValueText; })
				.OnClicked_Lambda([this]() { RunAction(); })
			];

	default:
		return ValueLabel(HAlign_Right);
	}
}

TSharedRef<SWidget> SMenuSettingRow::BuildKeySlots()
{
	const UMenuSettingKeyBinding* Binding = Cast<UMenuSettingKeyBinding>(Setting.Get());
	const int32 Count = Binding ? FMath::Clamp(Binding->SlotCount, 1, 3) : 1;
	const FMenuStyleRef Holder = StyleHolder;
	TSharedRef<SHorizontalBox> Slots = SNew(SHorizontalBox);
	SlotGlyphs.SetNum(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		Slots->AddSlot()
			.FillWidth(1.f)
			.Padding(Index > 0 ? 6.f : 0.f, 0.f, 0.f, 0.f)
			[
				SNew(SMenuPartBox, StyleHolder)
				.Part_Lambda([](const FMenuStyle& PartStyle) { return &PartStyle.Skin.KeySlot; })
				.BuiltIn_Lambda([this, Index](const FMenuStyle& PartStyle, const FMenuPartState&)
				{
					const FMenuStylePalette& Palette = PartStyle.GetPalette();
					FMenuBuiltInBox Box;
					Box.Radius = 6.f;
					if (bCapturing && CaptureSlot == Index)
					{
						Box.Fill = Palette.Accent.CopyWithNewOpacity(0.35f);
					}
					else
					{
						const bool bSlotFocused = HasAnyUserFocus().IsSet() && SelectedSlot == Index;
						Box.Fill = bSlotFocused ? Palette.RowFocused : Palette.Row;
					}
					return Box;
				})
				.IsSelected_Lambda([this, Index]() { return bCapturing && CaptureSlot == Index; })
				.IsFocused_Lambda([this, Index]() { return HasAnyUserFocus().IsSet() && SelectedSlot == Index; })
				.Padding(FMargin(8.f, 4.f))
				.OnMouseButtonDown_Lambda([this, Index](const FGeometry&, const FPointerEvent& MouseEvent)
				{
					if (!bSettingEnabled)
					{
						return FReply::Handled();
					}
					SelectedSlot = Index;
					if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
					{
						if (UMenuSettingsSubsystem* Settings = Host->GetSettings())
						{
							if (const UMenuSettingKeyBinding* Row = Cast<UMenuSettingKeyBinding>(Setting.Get()))
							{
								Settings->UnbindKey(Row->MappingName, Row->Device, Index);
								Host->PlaySound(EMenuSoundEvent::Back);
							}
						}
						return FReply::Handled();
					}
					BeginCapture(Index);
					return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse);
				})
				[
					SNew(SOverlay)
					+ SOverlay::Slot()
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SAssignNew(SlotGlyphs[Index], SMenuKeyGlyph)
						.CapStyle(StyleHolder->Style.KeyCap)
						.Visibility_Lambda([this, Index]()
						{
							return (!(bCapturing && CaptureSlot == Index) && SlotKeys.IsValidIndex(Index) && SlotKeys[Index].IsValid()) ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
						})
					]
					+ SOverlay::Slot()
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Font_Lambda([Holder]() { return Holder->Style.HintFont; })
						.ColorAndOpacity_Lambda([Holder]() { return FSlateColor(Holder->Palette().MutedText); })
						.Text_Lambda([this, Index]()
						{
							if (bCapturing && CaptureSlot == Index)
							{
								return GetCaptureDevice() == EMenuKeyBindingDevice::Gamepad ? LOCTEXT("PressButton", "Press a button...") : LOCTEXT("PressKey", "Press a key...");
							}
							return LOCTEXT("Unbound", "-");
						})
						.Visibility_Lambda([this, Index]()
						{
							const bool bShowGlyph = !(bCapturing && CaptureSlot == Index) && SlotKeys.IsValidIndex(Index) && SlotKeys[Index].IsValid();
							return bShowGlyph ? EVisibility::Collapsed : EVisibility::HitTestInvisible;
						})
					]
				]
			];
	}
	Refresh();
	return Slots;
}

FString SMenuSettingRow::ReadValue() const
{
	const UMenuSetting* Current = Setting.Get();
	if (!Current)
	{
		return FString();
	}
	if (const UMenuSettingsSubsystem* Settings = Host->GetSettings())
	{
		return Settings->GetSettingValue(SettingId);
	}
	return Host->GetPreviewValue(*Current);
}

void SMenuSettingRow::Refresh()
{
	const UMenuSetting* Current = Setting.Get();
	if (!Current)
	{
		return;
	}
	const FMenuSettingContext Context = Host->GetContext();
	UMenuSettingsSubsystem* Settings = Host->GetSettings();
	CurrentValue = ReadValue();

	if (const UMenuSettingChoice* Choice = Cast<UMenuSettingChoice>(Current))
	{
		Choice->GetOptions(Context, Options, CurrentValue);
		OptionIndex = UMenuSettingChoice::FindOption(Options, CurrentValue);
		ValueText = Options.IsValidIndex(OptionIndex) ? Options[OptionIndex].GetLabel() : FText::FromString(CurrentValue);
	}
	else
	{
		ValueText = Current->FormatValue(CurrentValue, Context);
	}

	if (Settings)
	{
		DisabledReason = FText::GetEmpty();
		bSettingEnabled = Settings->IsSettingEnabled(SettingId, DisabledReason);
		bModified = Settings->IsSettingModified(SettingId);
	}
	else
	{
		DisabledReason = FText::GetEmpty();
		bSettingEnabled = Current->IsAvailable(Context, DisabledReason);
		bModified = Current->HoldsValue() && CurrentValue != Current->GetDefaultValue(Context);
	}

	if (const UMenuSettingKeyBinding* Binding = Cast<UMenuSettingKeyBinding>(Current))
	{
		const UMenuInputSubsystem* Input = Context.LocalPlayer ? Context.LocalPlayer->GetSubsystem<UMenuInputSubsystem>() : nullptr;
		SlotKeys.SetNum(FMath::Clamp(Binding->SlotCount, 1, 3));
		for (int32 Index = 0; Index < SlotKeys.Num(); ++Index)
		{
			SlotKeys[Index] = Settings ? Settings->GetBoundKey(Binding->MappingName, Binding->Device, Index) : FKey();
			if (SlotGlyphs.IsValidIndex(Index) && SlotGlyphs[Index].IsValid())
			{
				FMenuKeyGlyph Glyph;
				if (Input)
				{
					Glyph = Input->GetKeyGlyph(SlotKeys[Index]);
				}
				else
				{
					Glyph.Key = SlotKeys[Index];
					Glyph.Label = MenuInputUtils::GetKeyLabel(SlotKeys[Index], EMenuGamepadFamily::Xbox);
				}
				SlotGlyphs[Index]->SetGlyph(Glyph);
			}
		}
		ValueText = Current->FormatValue(CurrentValue, Context);
	}

	if (!Message.IsEmpty() && FPlatformTime::Seconds() > MessageUntil && !bCapturing)
	{
		Message = FText::GetEmpty();
	}
	Invalidate(EInvalidateWidgetReason::Paint);
	NotifyCustomWidget();
}

void SMenuSettingRow::ShowMessage(const FText& InMessage, bool bWarning)
{
	Message = InMessage;
	bMessageWarning = bWarning;
	MessageUntil = FPlatformTime::Seconds() + 4.0;
	Invalidate(EInvalidateWidgetReason::Layout);
}

void SMenuSettingRow::ChangeValue(const FString& Value)
{
	UMenuSetting* Current = Setting.Get();
	if (!Current)
	{
		return;
	}
	if (!bSettingEnabled)
	{
		Host->PlaySound(EMenuSoundEvent::Error);
		return;
	}
	const FMenuSettingContext Context = Host->GetContext();
	const FString Clean = Current->Sanitize(Value, Context);
	if (Clean == CurrentValue)
	{
		return;
	}
	if (UMenuSettingsSubsystem* Settings = Host->GetSettings())
	{
		Settings->SetSettingValue(SettingId, Clean);
	}
	else
	{
		Host->SetPreviewValue(*Current, Clean);
	}
	Refresh();
	Host->PlaySound(EMenuSoundEvent::Change);
	if (MenuAccessibility::IsMenuNarrationEnabled())
	{
		Host->Announce(ValueText);
	}
}

void SMenuSettingRow::Step(int32 Direction)
{
	UMenuSetting* Current = Setting.Get();
	if (!Current || Direction == 0)
	{
		return;
	}
	if (UMenuSettingRowWidget* Widget = CustomWidget.Get())
	{
		Widget->HandleRowStepped(Direction);
	}
	switch (Current->GetKind())
	{
	case EMenuSettingKind::KeyBinding:
	{
		const int32 Count = SlotKeys.Num();
		const int32 Next = FMath::Clamp(SelectedSlot + Direction, 0, FMath::Max(0, Count - 1));
		if (Next != SelectedSlot)
		{
			SelectedSlot = Next;
			Host->PlaySound(EMenuSoundEvent::Navigate);
			Invalidate(EInvalidateWidgetReason::Paint);
		}
		return;
	}
	case EMenuSettingKind::Bool:
	case EMenuSettingKind::Number:
	case EMenuSettingKind::Choice:
	{
		if (!bSettingEnabled)
		{
			Host->PlaySound(EMenuSoundEvent::Error);
			return;
		}
		const FString Next = Current->StepValue(CurrentValue, Direction, Host->GetContext());
		if (SelectorWidget.IsValid())
		{
			SelectorWidget->Pulse(Direction);
		}
		ChangeValue(Next);
		return;
	}
	default:
		return;
	}
}

void SMenuSettingRow::Activate()
{
	UMenuSetting* Current = Setting.Get();
	if (!Current)
	{
		return;
	}
	if (UMenuSettingRowWidget* Widget = CustomWidget.Get())
	{
		// Your widget may handle Accept itself (open its own list): then the row does nothing more.
		if (Widget->HandleRowActivated())
		{
			return;
		}
	}
	switch (Current->GetKind())
	{
	case EMenuSettingKind::Bool:
		Step(1);
		return;
	case EMenuSettingKind::Choice:
		if (UsesDropdown() && bSettingEnabled && Host->OpenOptionList(SharedThis(this)))
		{
			Host->PlaySound(EMenuSoundEvent::Accept);
			return;
		}
		Step(1);
		return;
	case EMenuSettingKind::KeyBinding:
		BeginCapture(SelectedSlot);
		return;
	case EMenuSettingKind::Action:
		RunAction();
		return;
	default:
		return;
	}
}

void SMenuSettingRow::RunAction()
{
	const UMenuSettingAction* Action = Cast<UMenuSettingAction>(Setting.Get());
	UMenuSettingsSubsystem* Settings = Host->GetSettings();
	if (!Action || !Settings || !bSettingEnabled)
	{
		Host->PlaySound(EMenuSoundEvent::Error);
		return;
	}
	Host->PlaySound(EMenuSoundEvent::Accept);
	const TWeakObjectPtr<UMenuSettingsSubsystem> WeakSettings = Settings;
	const FName Id = SettingId;
	auto Run = [WeakSettings, Id]()
	{
		if (UMenuSettingsSubsystem* Pinned = WeakSettings.Get())
		{
			Pinned->RunSettingAction(Id);
		}
	};
	if (Action->ConfirmText.IsEmpty())
	{
		Run();
	}
	else
	{
		Host->Confirm(Action->GetLabel(), Action->ConfirmText, Run);
	}
}

EMenuKeyBindingDevice SMenuSettingRow::GetCaptureDevice() const
{
	const UMenuSettingKeyBinding* Binding = Cast<UMenuSettingKeyBinding>(Setting.Get());
	return Binding ? Binding->Device : EMenuKeyBindingDevice::KeyboardMouse;
}

void SMenuSettingRow::BeginCapture(int32 SlotIndex)
{
	if (bCapturing || !Host->GetSettings() || !bSettingEnabled || !FSlateApplication::IsInitialized())
	{
		Host->PlaySound(EMenuSoundEvent::Error);
		return;
	}
	bCapturing = true;
	CaptureSlot = SlotIndex;
	SelectedSlot = SlotIndex;
	Message = FText::GetEmpty();
	CaptureProcessor = MakeShared<FMenuKeyCaptureProcessor>(SharedThis(this), Host->GetUserIndex());
	FSlateApplication::Get().RegisterInputPreProcessor(CaptureProcessor, 0);
	Host->PlaySound(EMenuSoundEvent::Accept);
	Host->OnKeyCaptureChanged(SharedThis(this), true);
	if (MenuAccessibility::IsMenuNarrationEnabled())
	{
		Host->Announce(GetCaptureDevice() == EMenuKeyBindingDevice::Gamepad ? LOCTEXT("PressButton", "Press a button...") : LOCTEXT("PressKey", "Press a key..."));
	}
	Invalidate(EInvalidateWidgetReason::Paint);
	NotifyCustomWidget();
}

void SMenuSettingRow::StopCaptureProcessor()
{
	if (CaptureProcessor.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(CaptureProcessor);
	}
	CaptureProcessor.Reset();
}

void SMenuSettingRow::QueueCaptureKey(const FKey& Key, bool bCancel)
{
	if (!bCapturing)
	{
		return;
	}
	QueuedKey = Key;
	bQueuedCancel = bCancel;
	if (!bQueued)
	{
		bQueued = true;
		RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SMenuSettingRow::HandleQueuedCapture));
	}
}

EActiveTimerReturnType SMenuSettingRow::HandleQueuedCapture(double InCurrentTime, float InDeltaTime)
{
	bQueued = false;
	if (!bCapturing)
	{
		return EActiveTimerReturnType::Stop;
	}
	if (bQueuedCancel)
	{
		CancelCapture();
		return EActiveTimerReturnType::Stop;
	}
	// The wrong device's key: say so and keep waiting.
	const EMenuInputDevice KeyDevice = MenuInputUtils::GetKeyDevice(QueuedKey);
	const bool bWantPad = GetCaptureDevice() == EMenuKeyBindingDevice::Gamepad;
	if ((KeyDevice == EMenuInputDevice::Gamepad) != bWantPad)
	{
		ShowMessage(bWantPad ? LOCTEXT("WantPad", "Press a controller button (Menu cancels).") : LOCTEXT("WantKey", "Press a key or mouse button (Esc cancels)."), true);
		Host->PlaySound(EMenuSoundEvent::Error);
		return EActiveTimerReturnType::Stop;
	}
	EndCapture(QueuedKey);
	return EActiveTimerReturnType::Stop;
}

void SMenuSettingRow::EndCapture(const FKey& Key)
{
	StopCaptureProcessor();
	bCapturing = false;
	const UMenuSettingKeyBinding* Binding = Cast<UMenuSettingKeyBinding>(Setting.Get());
	UMenuSettingsSubsystem* Settings = Host->GetSettings();
	if (Binding && Settings && Key.IsValid())
	{
		FText Result;
		if (Settings->BindKey(Binding->MappingName, Binding->Device, CaptureSlot, Key, Result))
		{
			Host->PlaySound(EMenuSoundEvent::Accept);
			if (!Result.IsEmpty())
			{
				ShowMessage(Result, false);
			}
			if (MenuAccessibility::IsMenuNarrationEnabled())
			{
				Host->Announce(FText::Format(LOCTEXT("BoundTo", "{0}: {1}"), Binding->GetLabel(), Key.GetDisplayName()));
			}
		}
		else
		{
			Host->PlaySound(EMenuSoundEvent::Error);
			ShowMessage(Result, true);
		}
	}
	Refresh();
	Host->OnKeyCaptureChanged(SharedThis(this), false);
}

void SMenuSettingRow::CancelCapture()
{
	if (!bCapturing)
	{
		return;
	}
	StopCaptureProcessor();
	bCapturing = false;
	Host->PlaySound(EMenuSoundEvent::Back);
	Refresh();
	Host->OnKeyCaptureChanged(SharedThis(this), false);
}

// =====================================================================================================
//  Input
// =====================================================================================================

FReply SMenuSettingRow::OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent)
{
	if (InFocusEvent.GetCause() == EFocusCause::Navigation)
	{
		Host->PlaySound(EMenuSoundEvent::Navigate);
	}
	Host->OnRowFocused(SharedThis(this));
	StartAnimating();
	return FReply::Handled();
}

void SMenuSettingRow::OnFocusLost(const FFocusEvent& InFocusEvent)
{
	SCompoundWidget::OnFocusLost(InFocusEvent);
	if (bCapturing)
	{
		CancelCapture();
	}
	StartAnimating();
}

FReply SMenuSettingRow::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (bCapturing)
	{
		return FReply::Handled();
	}
	if (FSlateApplication::Get().GetNavigationActionFromKey(InKeyEvent) == EUINavigationAction::Accept)
	{
		Activate();
		return FReply::Handled();
	}
	const UMenuSettingKeyBinding* Binding = Cast<UMenuSettingKeyBinding>(Setting.Get());
	if (Binding && (InKeyEvent.GetKey() == EKeys::Delete || InKeyEvent.GetKey() == EKeys::BackSpace))
	{
		if (UMenuSettingsSubsystem* Settings = Host->GetSettings())
		{
			Settings->UnbindKey(Binding->MappingName, Binding->Device, SelectedSlot);
			Host->PlaySound(EMenuSoundEvent::Back);
		}
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

FNavigationReply SMenuSettingRow::OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent)
{
	if (bCapturing)
	{
		return FNavigationReply::Stop();
	}
	const EUINavigation Navigation = InNavigationEvent.GetNavigationType();
	const UMenuSetting* Current = Setting.Get();
	const bool bSteps = Current && (Current->GetKind() == EMenuSettingKind::Bool || Current->GetKind() == EMenuSettingKind::Number
		|| Current->GetKind() == EMenuSettingKind::Choice || Current->GetKind() == EMenuSettingKind::KeyBinding);
	if (bSteps && (Navigation == EUINavigation::Left || Navigation == EUINavigation::Right))
	{
		Step(Navigation == EUINavigation::Left ? -1 : 1);
		return FNavigationReply::Stop();
	}
	return SCompoundWidget::OnNavigation(MyGeometry, InNavigationEvent);
}

FReply SMenuSettingRow::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bPressedVisual = true;
		NotifyCustomWidget();
		Invalidate(EInvalidateWidgetReason::Paint);
		return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse);
	}
	return FReply::Unhandled();
}

FReply SMenuSettingRow::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bPressedVisual && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bPressedVisual = false;
		NotifyCustomWidget();
		Invalidate(EInvalidateWidgetReason::Paint);
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void SMenuSettingRow::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseEnter(MyGeometry, MouseEvent);
	Host->OnRowHovered(SharedThis(this), true);
	StartAnimating();
}

void SMenuSettingRow::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseLeave(MouseEvent);
	bPressedVisual = false;
	Host->OnRowHovered(SharedThis(this), false);
	StartAnimating();
}

EActiveTimerReturnType SMenuSettingRow::Animate(double InCurrentTime, float InDeltaTime)
{
	const bool bMoving = Highlight.Advance(InDeltaTime);
	Invalidate(EInvalidateWidgetReason::Paint);
	if (!bMoving)
	{
		bAnimating = false;
		return EActiveTimerReturnType::Stop;
	}
	return EActiveTimerReturnType::Continue;
}

void SMenuSettingRow::StartAnimating()
{
	const float Target = HasAnyUserFocus().IsSet() ? 2.f : (IsHovered() ? 1.f : 0.f);
	const EWidgetMotion Motion = MenuAccessibility::IsReduceMotionEnabled() ? EWidgetMotion::Instant : StyleHolder->Style.Motion;
	Highlight.SetTarget(Target, Motion, 0.15f);
	if (!bAnimating && Highlight.IsMoving())
	{
		bAnimating = true;
		RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SMenuSettingRow::Animate));
	}
	Invalidate(EInvalidateWidgetReason::Paint);
	NotifyCustomWidget();
}

int32 SMenuSettingRow::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FMenuStyle& Style = StyleHolder->Style;
	const FMenuStyleSkin& Skin = Style.Skin;
	const FMenuStylePalette& Palette = StyleHolder->Palette();
	const FLinearColor Tint = InWidgetStyle.GetColorAndOpacityTint();
	const FVector2f Size(AllottedGeometry.GetLocalSize());

	// 0 rest, 1 hovered, 2 focused - blended.
	const float State = FMath::Clamp(Highlight.Value, 0.f, 2.f);
	const FLinearColor Fill = State <= 1.f ? FMath::Lerp(Palette.Row, Palette.RowHovered, State) : FMath::Lerp(Palette.RowHovered, Palette.RowFocused, State - 1.f);
	const bool bFocused = HasAnyUserFocus().IsSet();
	const FMenuPartState PartState = GetPartState();
	if (bCustomRow)
	{
		// Your widget is the row: it draws its own states (On Row State Changed).
		return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	}

	if (Skin.Row.DrawsBuiltIn())
	{
		MenuDraw::RoundedBox(OutDrawElements, LayerId, AllottedGeometry, Fill * Tint, Style.CornerRadius,
			bFocused ? Palette.FocusOutline * Tint : FLinearColor::Transparent, bFocused ? Style.FocusOutlineWidth : 0.f);
	}
	SkinPainter.Draw(OutDrawElements, LayerId, AllottedGeometry, Skin.Row, PartState, Fill, Palette, Tint, *this, 0);

	if (bFocused && Style.bFocusBar)
	{
		const FVector2f BarOffset(0.f, Size.Y * 0.22f);
		const FVector2f BarSize(3.f, Size.Y * 0.56f);
		if (Skin.FocusBar.DrawsBuiltIn())
		{
			MenuDraw::RoundedBoxAt(OutDrawElements, LayerId + 1, AllottedGeometry, BarOffset, BarSize, Palette.Accent * Tint, 1.5f);
		}
		SkinPainter.Draw(OutDrawElements, LayerId + 1, AllottedGeometry, Skin.FocusBar, PartState, Palette.Accent, Palette, Tint, *this, 1, BarOffset, BarSize);
	}
	if (bModified && Style.bShowModifiedDot)
	{
		const float Dot = 6.f;
		const FVector2f DotOffset(FMath::Max(4.f, Style.RowPadding.Left * 0.5f - Dot * 0.5f), (Size.Y - Dot) * 0.5f);
		if (Skin.ModifiedDot.DrawsBuiltIn())
		{
			MenuDraw::RoundedBoxAt(OutDrawElements, LayerId + 1, AllottedGeometry, DotOffset, FVector2f(Dot, Dot), Palette.Accent * Tint, Dot * 0.5f);
		}
		SkinPainter.Draw(OutDrawElements, LayerId + 1, AllottedGeometry, Skin.ModifiedDot, PartState, Palette.Accent, Palette, Tint, *this, 2, DotOffset, FVector2f(Dot, Dot));
	}
	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId + 2, InWidgetStyle, bParentEnabled);
}

FMenuPartState SMenuSettingRow::GetPartState() const
{
	const float State = FMath::Clamp(Highlight.Value, 0.f, 2.f);
	FMenuPartState PartState;
	PartState.Hover = FMath::Min(State, 1.f);
	PartState.Focus = FMath::Max(State - 1.f, 0.f);
	PartState.bPressed = bPressedVisual;
	PartState.bSelected = bCapturing;
	PartState.bDisabled = !bSettingEnabled;
	return PartState;
}

#undef LOCTEXT_NAMESPACE
