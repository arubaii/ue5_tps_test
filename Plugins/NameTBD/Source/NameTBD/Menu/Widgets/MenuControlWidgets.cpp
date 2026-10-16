// Copyright AR GameSystems 2026. All rights reserved.

#include "MenuControlWidgets.h"

#include "Engine/LocalPlayer.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SBoxPanel.h"
#include "NameTBD/Menu/Settings/MenuSetting.h"
#include "NameTBD/Menu/Settings/MenuSettingsSubsystem.h"
#include "MenuStyle.h"

#define LOCTEXT_NAMESPACE "MenuControlWidgets"

// =====================================================================================================
//  Host
// =====================================================================================================

void SMenuControlHost::Construct(const FArguments& InArgs, const FMenuStyleRef& InStyle)
{
	StyleHolder = InStyle;
	StepDelegate = InArgs._OnStep;
	ActivateDelegate = InArgs._OnActivate;
	ChildSlot
	.Padding(FMargin(2.f))
	[
		InArgs._Content.Widget
	];
}

void SMenuControlHost::SetContent(const TSharedRef<SWidget>& InContent)
{
	ChildSlot
	[
		InContent
	];
}

FReply SMenuControlHost::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (FSlateApplication::Get().GetNavigationActionFromKey(InKeyEvent) == EUINavigationAction::Accept && ActivateDelegate.IsBound())
	{
		ActivateDelegate.Execute();
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

FNavigationReply SMenuControlHost::OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent)
{
	const EUINavigation Navigation = InNavigationEvent.GetNavigationType();
	if ((Navigation == EUINavigation::Left || Navigation == EUINavigation::Right) && StepDelegate.IsBound() && IsEnabled())
	{
		StepDelegate.Execute(Navigation == EUINavigation::Left ? -1 : 1);
		return FNavigationReply::Stop();
	}
	return SCompoundWidget::OnNavigation(MyGeometry, InNavigationEvent);
}

FReply SMenuControlHost::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// A click the control didn't take still focuses it (for the keys after).
	return MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton ? FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse) : FReply::Unhandled();
}

int32 SMenuControlHost::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 MaxLayer = SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	const FMenuStyle& Style = StyleHolder->Style;
	if (HasAnyUserFocus().IsSet() && Style.FocusOutlineWidth > 0.f)
	{
		MenuDraw::RoundedBox(OutDrawElements, MaxLayer + 1, AllottedGeometry, FLinearColor::Transparent, Style.CornerRadius,
			StyleHolder->Palette().FocusOutline * InWidgetStyle.GetColorAndOpacityTint(), Style.FocusOutlineWidth);
		return MaxLayer + 1;
	}
	return MaxLayer;
}

// =====================================================================================================
//  Base
// =====================================================================================================

UMenuSettingsSubsystem* UMenuControlWidgetBase::FindSettings() const
{
	if (IsDesignTime())
	{
		return nullptr;
	}
	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	return LocalPlayer ? LocalPlayer->GetSubsystem<UMenuSettingsSubsystem>() : UMenuSettingsSubsystem::Get(this);
}

TSharedRef<SWidget> UMenuControlWidgetBase::RebuildWidget()
{
	StyleHolder->Style = UMenuStyleAsset::Resolve(StyleAsset);
	if (!StyleHandle.IsValid())
	{
		StyleHandle = UMenuStyleAsset::OnStyleChanged().AddUObject(this, &UMenuControlWidgetBase::HandleStyleChanged);
	}
	TWeakObjectPtr<UMenuControlWidgetBase> WeakControl(this);
	Host = SNew(SMenuControlHost, StyleHolder)
		.OnStep_Lambda([WeakControl](int32 Direction)
		{
			if (UMenuControlWidgetBase* Control = WeakControl.Get())
			{
				Control->StepControl(Direction);
			}
		})
		.OnActivate_Lambda([WeakControl]()
		{
			if (UMenuControlWidgetBase* Control = WeakControl.Get())
			{
				Control->ActivateControl();
			}
		})
		[
			MakeControl()
		];
	BindSetting();
	return Host.ToSharedRef();
}

void UMenuControlWidgetBase::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	StyleHolder->Style = UMenuStyleAsset::Resolve(StyleAsset);
	if (Host.IsValid())
	{
		Host->SetContent(MakeControl());
	}
	if (IsBound())
	{
		const FString Current = ReadSetting();
		if (!Current.IsEmpty())
		{
			ShowSettingValue(Current);
		}
	}
}

void UMenuControlWidgetBase::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	UnbindSetting();
	UMenuStyleAsset::OnStyleChanged().Remove(StyleHandle);
	StyleHandle.Reset();
	Host.Reset();
}

void UMenuControlWidgetBase::HandleStyleChanged(const UMenuStyleAsset* Changed)
{
	if (Changed == StyleAsset || !StyleAsset)
	{
		StyleHolder->Style = UMenuStyleAsset::Resolve(StyleAsset);
		if (Host.IsValid())
		{
			Host->SetContent(MakeControl());
		}
	}
}

void UMenuControlWidgetBase::SetStyleAsset(UMenuStyleAsset* InStyleAsset)
{
	StyleAsset = InStyleAsset;
	HandleStyleChanged(InStyleAsset);
}

void UMenuControlWidgetBase::SetSettingId(FName InSettingId)
{
	if (SettingId == InSettingId)
	{
		return;
	}
	UnbindSetting();
	SettingId = InSettingId;
	if (Host.IsValid())
	{
		Host->SetContent(MakeControl());
		BindSetting();
	}
}

void UMenuControlWidgetBase::BindSetting()
{
	UnbindSetting();
	UMenuSettingsSubsystem* Settings = IsBound() ? FindSettings() : nullptr;
	if (!Settings)
	{
		return;
	}
	BoundSettings = Settings;
	ChangedHandle = Settings->OnSettingChangedNative().AddWeakLambda(this, [this](FName ChangedId, const FString& NewValue)
	{
		// Any change can reach this one (a preset): read it again.
		ShowSettingValue(ReadSetting());
	});
	ShowSettingValue(ReadSetting());
}

void UMenuControlWidgetBase::UnbindSetting()
{
	if (UMenuSettingsSubsystem* Settings = BoundSettings.Get())
	{
		Settings->OnSettingChangedNative().Remove(ChangedHandle);
	}
	ChangedHandle.Reset();
	BoundSettings.Reset();
}

void UMenuControlWidgetBase::WriteSetting(const FString& NewValue)
{
	if (!IsBound())
	{
		return;
	}
	if (UMenuSettingsSubsystem* Settings = FindSettings())
	{
		Settings->SetSettingValue(SettingId, NewValue);
	}
}

FString UMenuControlWidgetBase::ReadSetting() const
{
	const UMenuSettingsSubsystem* Settings = IsBound() ? FindSettings() : nullptr;
	return Settings ? Settings->GetSettingValue(SettingId) : FString();
}

#if WITH_EDITOR
const FText UMenuControlWidgetBase::GetPaletteCategory()
{
	return LOCTEXT("PaletteCategory", "NameTBD");
}
#endif

// =====================================================================================================
//  Switch
// =====================================================================================================

TSharedRef<SWidget> UMenuSwitch::MakeControl()
{
	TWeakObjectPtr<UMenuSwitch> WeakSwitch(this);
	TSharedRef<SMenuSwitch> Switch = SNew(SMenuSwitch, StyleHolder)
		.IsOn_Lambda([WeakSwitch]() { return WeakSwitch.IsValid() && WeakSwitch->bOn; })
		.OnToggled_Lambda([WeakSwitch](bool bNewIsOn)
		{
			if (UMenuSwitch* Pinned = WeakSwitch.Get())
			{
				Pinned->Change(bNewIsOn);
			}
		});
	Switch->SetFocusedAttribute(TAttribute<bool>::CreateLambda([WeakSwitch]()
	{
		const UMenuSwitch* Pinned = WeakSwitch.Get();
		return Pinned && Pinned->Host.IsValid() && Pinned->Host->HasAnyUserFocus().IsSet();
	}));
	return Switch;
}

void UMenuSwitch::Change(bool bNewIsOn)
{
	if (bOn == bNewIsOn)
	{
		return;
	}
	bOn = bNewIsOn;
	WriteSetting(MenuSettingValues::FromBool(bNewIsOn));
	OnSwitchChanged.Broadcast(bNewIsOn);
}

void UMenuSwitch::SetOn(bool bNewIsOn)
{
	Change(bNewIsOn);
}

void UMenuSwitch::StepControl(int32 Direction)
{
	Change(Direction > 0);
}

void UMenuSwitch::ActivateControl()
{
	Change(!bOn);
}

void UMenuSwitch::ShowSettingValue(const FString& Value)
{
	if (!Value.IsEmpty())
	{
		bOn = MenuSettingValues::ToBool(Value);
	}
}

// =====================================================================================================
//  Slider
// =====================================================================================================

float UMenuSlider::Snap(float InValue) const
{
	const float Low = FMath::Min(MinValue, MaxValue);
	const float High = FMath::Max(MinValue, MaxValue);
	float Snapped = FMath::Clamp(InValue, Low, High);
	if (StepSize > 0.f)
	{
		Snapped = FMath::Clamp(Low + FMath::RoundToFloat((Snapped - Low) / StepSize) * StepSize, Low, High);
	}
	return Snapped;
}

float UMenuSlider::ToFraction() const
{
	const float Range = MaxValue - MinValue;
	return FMath::IsNearlyZero(Range) ? 0.f : FMath::Clamp((Value - MinValue) / Range, 0.f, 1.f);
}

TSharedRef<SWidget> UMenuSlider::MakeControl()
{
	// Bound to a Number: its range and step.
	if (const UMenuSettingsSubsystem* Settings = IsBound() ? FindSettings() : nullptr)
	{
		if (const UMenuSettingNumber* Number = Cast<UMenuSettingNumber>(Settings->FindSetting(SettingId)))
		{
			MinValue = Number->MinValue;
			MaxValue = Number->MaxValue;
			StepSize = Number->Step;
		}
	}
	TWeakObjectPtr<UMenuSlider> WeakSlider(this);
	return SNew(SMenuSlider, StyleHolder)
		.Fraction_Lambda([WeakSlider]() { return WeakSlider.IsValid() ? WeakSlider->ToFraction() : 0.f; })
		.OnChanged_Lambda([WeakSlider](float Fraction)
		{
			if (UMenuSlider* Pinned = WeakSlider.Get())
			{
				Pinned->Change(FMath::Lerp(Pinned->MinValue, Pinned->MaxValue, Fraction), false);
			}
		})
		.OnCommitted_Lambda([WeakSlider](float Fraction)
		{
			if (UMenuSlider* Pinned = WeakSlider.Get())
			{
				Pinned->Change(FMath::Lerp(Pinned->MinValue, Pinned->MaxValue, Fraction), true);
			}
		});
}

void UMenuSlider::Change(float NewValue, bool bCommit)
{
	const float Snapped = Snap(NewValue);
	const bool bChanged = !FMath::IsNearlyEqual(Snapped, Value);
	Value = Snapped;
	if (bChanged)
	{
		WriteSetting(FString::SanitizeFloat(Snapped));
		OnSliderChanged.Broadcast(Snapped);
	}
	if (bCommit)
	{
		OnSliderCommitted.Broadcast(Snapped);
	}
}

void UMenuSlider::SetValue(float InValue)
{
	Change(InValue, true);
}

void UMenuSlider::StepControl(int32 Direction)
{
	const float Step = StepSize > 0.f ? StepSize : (MaxValue - MinValue) / 20.f;
	Change(Value + Step * static_cast<float>(Direction), true);
}

void UMenuSlider::ShowSettingValue(const FString& InSettingValue)
{
	if (!InSettingValue.IsEmpty())
	{
		Value = FCString::Atof(*InSettingValue);
	}
}

// =====================================================================================================
//  Selector
// =====================================================================================================

FText UMenuSelector::GetSelectedOption() const
{
	return Options.IsValidIndex(SelectedIndex) ? Options[SelectedIndex] : FText::GetEmpty();
}

void UMenuSelector::SetOptions(const TArray<FText>& InOptions)
{
	Options = InOptions;
	SelectedIndex = FMath::Clamp(SelectedIndex, 0, FMath::Max(0, Options.Num() - 1));
}

void UMenuSelector::Select(int32 NewIndex)
{
	if (!Options.IsValidIndex(NewIndex) || NewIndex == SelectedIndex)
	{
		return;
	}
	SelectedIndex = NewIndex;
	if (OptionValues.IsValidIndex(NewIndex))
	{
		WriteSetting(OptionValues[NewIndex]);
	}
	OnSelectionChanged.Broadcast(NewIndex, Options[NewIndex]);
}

void UMenuSelector::SetSelectedIndex(int32 InIndex)
{
	Select(InIndex);
}

void UMenuSelector::StepControl(int32 Direction)
{
	const int32 Count = Options.Num();
	if (Count == 0)
	{
		return;
	}
	int32 Next = SelectedIndex + Direction;
	Next = bWrap ? (Next + Count) % Count : FMath::Clamp(Next, 0, Count - 1);
	Select(Next);
}

void UMenuSelector::ShowSettingValue(const FString& Value)
{
	const UMenuSettingsSubsystem* Settings = FindSettings();
	if (!Settings || !IsBound())
	{
		return;
	}
	// The setting's options, as they are now (a dropdown's resolutions, the upscaler's modes).
	const TArray<FMenuSettingOption> SettingOptions = Settings->GetSettingOptions(SettingId);
	TArray<FString> NewValues;
	for (const FMenuSettingOption& Option : SettingOptions)
	{
		NewValues.Add(Option.Value);
	}
	const bool bOptionsChanged = NewValues != OptionValues;
	Options.Reset();
	OptionValues = MoveTemp(NewValues);
	for (const FMenuSettingOption& Option : SettingOptions)
	{
		Options.Add(Option.GetLabel());
	}
	SelectedIndex = FMath::Max(0, UMenuSettingChoice::FindOption(SettingOptions, Value));
	if (bOptionsChanged && Host.IsValid())
	{
		// A segmented control has one button per option: built again.
		Host->SetContent(MakeControl());
	}
}

TSharedRef<SWidget> UMenuSelector::MakeControl()
{
	TWeakObjectPtr<UMenuSelector> WeakSelector(this);
	return SNew(SMenuSelector, StyleHolder)
		.ValueText_Lambda([WeakSelector]() { return WeakSelector.IsValid() ? WeakSelector->GetSelectedOption() : FText::GetEmpty(); })
		.OptionCount_Lambda([WeakSelector]() { return WeakSelector.IsValid() ? WeakSelector->Options.Num() : 0; })
		.OptionIndex_Lambda([WeakSelector]() { return WeakSelector.IsValid() ? WeakSelector->SelectedIndex : 0; })
		.CanStepLeft_Lambda([WeakSelector]() { return WeakSelector.IsValid() && (WeakSelector->bWrap || WeakSelector->SelectedIndex > 0); })
		.CanStepRight_Lambda([WeakSelector]() { return WeakSelector.IsValid() && (WeakSelector->bWrap || WeakSelector->SelectedIndex < WeakSelector->Options.Num() - 1); })
		.OnStep_Lambda([WeakSelector](int32 Direction)
		{
			if (UMenuSelector* Pinned = WeakSelector.Get())
			{
				Pinned->StepControl(Direction);
			}
		});
}

// =====================================================================================================
//  Segmented
// =====================================================================================================

TSharedRef<SWidget> UMenuSegmentedControl::MakeControl()
{
	TSharedRef<SHorizontalBox> Segments = SNew(SHorizontalBox);
	TWeakObjectPtr<UMenuSegmentedControl> WeakControl(this);
	for (int32 Index = 0; Index < Options.Num(); ++Index)
	{
		Segments->AddSlot()
			.FillWidth(1.f)
			.Padding(Index > 0 ? 4.f : 0.f, 0.f, 0.f, 0.f)
			[
				SNew(SMenuButton, StyleHolder)
				.Kind(EMenuButtonKind::Segment)
				.IsFocusable(false)
				.Text(Options[Index])
				.IsSelected_Lambda([WeakControl, Index]() { return WeakControl.IsValid() && WeakControl->SelectedIndex == Index; })
				.OnClicked_Lambda([WeakControl, Index]()
				{
					if (UMenuSegmentedControl* Pinned = WeakControl.Get())
					{
						Pinned->Select(Index);
					}
				})
			];
	}
	return Segments;
}

#undef LOCTEXT_NAMESPACE
