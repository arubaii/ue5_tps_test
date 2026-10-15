// Copyright AR GameSystems 2026. All rights reserved.

#include "NotificationEntryWidget.h"

#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "NameTBD/Widgets/WidgetSVGImage.h"
#include "NotificationHost.h"
#include "NotificationSubsystem.h"

// =====================================================================================================
// UNotificationEntryWidget
// =====================================================================================================

void UNotificationEntryWidget::SetNotification(const FNotificationView& InNotification, UNotificationHost* InHost, UNotificationSubsystem* InSubsystem, bool bInHistoryEntry)
{
	Notification = InNotification;
	WeakHost = InHost;
	WeakSubsystem = InSubsystem;
	bHistoryEntry = bInHistoryEntry;
	bWasShortcutTarget = IsShortcutTarget();
	NativeOnNotificationSet(false);
	OnNotificationSet(Notification);
}

void UNotificationEntryWidget::UpdateNotification(const FNotificationView& InNotification, bool bStacked)
{
	Notification = InNotification;
	NativeOnNotificationSet(bStacked);
	OnNotificationUpdated(Notification, bStacked);
}

void UNotificationEntryWidget::ClearNotification()
{
	OnNotificationCleared();
	if (ActionsPanel)
	{
		ActionsPanel->ClearChildren();
	}
	ActionWidgets.Reset();
	Notification = FNotificationView();
	WeakHost.Reset();
	WeakSubsystem.Reset();
}

void UNotificationEntryWidget::RefreshShortcutState()
{
	const bool bIsTarget = IsShortcutTarget();
	for (UNotificationActionWidget* ActionWidget : ActionWidgets)
	{
		if (ActionWidget)
		{
			ActionWidget->RefreshKeyHint();
		}
	}
	if (bIsTarget != bWasShortcutTarget)
	{
		bWasShortcutTarget = bIsTarget;
		OnShortcutTargetChanged(bIsTarget);
	}
}

void UNotificationEntryWidget::NativeOnNotificationSet(bool bStacked)
{
	if (bFillParts)
	{
		FillParts();
	}
}

void UNotificationEntryWidget::FillParts()
{
	auto ShowText = [](UTextBlock* Block, const FText& Text)
	{
		if (Block)
		{
			Block->SetText(Text);
			Block->SetVisibility(Text.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
		}
	};
	ShowText(TitleText, Notification.Title);
	ShowText(BodyText, Notification.Body);

	if (CountText)
	{
		FText Count;
		if (Notification.Count > 1)
		{
			FFormatNamedArguments Args;
			Args.Add(TEXT("Count"), FText::AsNumber(Notification.Count));
			Count = FText::Format(FTextFormat(CountFormat), Args);
		}
		ShowText(CountText, Count);
	}

	if (IconImage)
	{
		const FNotificationIcon& Icon = Notification.Icon;
		const bool bTexture = Icon.ImageSource == EWidgetImageSource::Texture && Icon.Image;
		if (bTexture)
		{
			IconImage->SetBrushResourceObject(Icon.Image);
			IconImage->SetColorAndOpacity(Icon.Tint);
		}
		IconImage->SetVisibility(bTexture ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (IconSVG)
	{
		const FNotificationIcon& Icon = Notification.Icon;
		const bool bSVG = Icon.ImageSource == EWidgetImageSource::SVG && Icon.SVG;
		if (bSVG)
		{
			IconSVG->SetSVG(Icon.SVG);
			IconSVG->SetColorAndOpacity(Icon.Tint);
		}
		IconSVG->SetVisibility(bSVG ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	if (ProgressBar)
	{
		if (Notification.HasProgress())
		{
			ProgressBar->SetPercent(FMath::Clamp(Notification.Progress, 0.f, 1.f));
		}
		ProgressBar->SetVisibility(Notification.HasProgress() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	if (TimerBar)
	{
		const bool bTimed = !bHistoryEntry && Notification.Duration > 0.f;
		TimerBar->SetPercent(GetTimeRemainingFraction());
		TimerBar->SetVisibility(bTimed ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	if (ActionsPanel)
	{
		ActionsPanel->ClearChildren();
		ActionWidgets.Reset();
		if (!bHistoryEntry && ActionWidgetClass)
		{
			for (const FNotificationAction& Action : Notification.Actions)
			{
				UNotificationActionWidget* ActionWidget = CreateWidget<UNotificationActionWidget>(this, ActionWidgetClass);
				if (!ActionWidget)
				{
					continue;
				}
				ActionWidget->SetAction(Action, this);
				ActionsPanel->AddChild(ActionWidget);
				ActionWidgets.Add(ActionWidget);
			}
		}
		ActionsPanel->SetVisibility(ActionWidgets.Num() > 0 ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void UNotificationEntryWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!Notification.Handle.IsValid() || bHistoryEntry)
	{
		return;
	}
	if (TimerBar && bFillParts)
	{
		TimerBar->SetPercent(GetTimeRemainingFraction());
	}
	if (IsShortcutTarget() != bWasShortcutTarget)
	{
		RefreshShortcutState();
	}
}

void UNotificationEntryWidget::Dismiss()
{
	if (UNotificationSubsystem* Subsystem = WeakSubsystem.Get())
	{
		if (bHistoryEntry)
		{
			Subsystem->RemoveFromHistory(Notification.Handle);
		}
		else
		{
			Subsystem->DismissByPlayer(Notification.Handle);
		}
	}
}

void UNotificationEntryWidget::Respond(FName ActionId)
{
	if (UNotificationSubsystem* Subsystem = WeakSubsystem.Get())
	{
		if (!bHistoryEntry)
		{
			Subsystem->RespondToNotification(Notification.Handle, ActionId);
		}
	}
}

float UNotificationEntryWidget::GetTimeRemaining() const
{
	const UNotificationSubsystem* Subsystem = WeakSubsystem.Get();
	return Subsystem ? Subsystem->GetTimeRemaining(Notification.Handle) : 0.f;
}

float UNotificationEntryWidget::GetTimeRemainingFraction() const
{
	const UNotificationSubsystem* Subsystem = WeakSubsystem.Get();
	return Subsystem ? Subsystem->GetTimeRemainingFraction(Notification.Handle) : 1.f;
}

float UNotificationEntryWidget::GetHoldProgress(FName ActionId) const
{
	const UNotificationSubsystem* Subsystem = WeakSubsystem.Get();
	return Subsystem ? Subsystem->GetHoldProgress(Notification.Handle, ActionId) : 0.f;
}

bool UNotificationEntryWidget::IsShortcutTarget() const
{
	const UNotificationSubsystem* Subsystem = WeakSubsystem.Get();
	return !bHistoryEntry && Subsystem && Subsystem->IsShortcutTarget(Notification.Handle);
}

FKey UNotificationEntryWidget::GetActionKey(const FNotificationAction& Action) const
{
	const UNotificationSubsystem* Subsystem = WeakSubsystem.Get();
	return Subsystem ? Subsystem->GetDisplayKey(Action) : (Action.KeyboardKey.IsValid() ? Action.KeyboardKey : Action.GamepadKey);
}

FNotificationKeyGlyph UNotificationEntryWidget::GetKeyGlyph(const FKey& Key)
{
	return UNotificationSubsystem::GetKeyGlyph(Key);
}

// =====================================================================================================
// UNotificationActionWidget
// =====================================================================================================

void UNotificationActionWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (ActionButton)
	{
		ActionButton->OnClicked.AddUniqueDynamic(this, &UNotificationActionWidget::HandleClicked);
	}
}

void UNotificationActionWidget::SetAction(const FNotificationAction& InAction, UNotificationEntryWidget* InEntry)
{
	Action = InAction;
	WeakEntry = InEntry;
	if (LabelText)
	{
		LabelText->SetText(Action.Label);
	}
	RefreshKeyHint();
}

void UNotificationActionWidget::RefreshKeyHint()
{
	const UNotificationEntryWidget* Entry = WeakEntry.Get();
	const FKey Key = Entry ? Entry->GetActionKey(Action) : FKey();
	const bool bShowKey = Entry && Entry->IsShortcutTarget() && Key.IsValid();
	const FNotificationKeyGlyph Glyph = bShowKey ? UNotificationSubsystem::GetKeyGlyph(Key) : FNotificationKeyGlyph();
	if (KeyText)
	{
		KeyText->SetText(Glyph.Label);
		KeyText->SetVisibility(bShowKey ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (HoldBar)
	{
		HoldBar->SetPercent(0.f);
		HoldBar->SetVisibility(bShowKey && Action.HoldTime > 0.f ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	OnActionSet(Action, bShowKey, Glyph);
}

void UNotificationActionWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (HoldBar && Action.HoldTime > 0.f)
	{
		if (const UNotificationEntryWidget* Entry = WeakEntry.Get())
		{
			HoldBar->SetPercent(Entry->GetHoldProgress(Action.ActionId));
		}
	}
}

void UNotificationActionWidget::TakeAction()
{
	if (UNotificationEntryWidget* Entry = WeakEntry.Get())
	{
		Entry->Respond(Action.ActionId);
	}
}

void UNotificationActionWidget::HandleClicked()
{
	TakeAction();
}
