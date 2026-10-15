// Copyright AR GameSystems 2026. All rights reserved.

#include "MenuDialog.h"

#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "NameTBD/Menu/MenuPlayerUtils.h"
#include "NameTBD/Menu/Settings/MenuSettingTargets.h"
#include "MenuStyle.h"

#define LOCTEXT_NAMESPACE "MenuDialog"

UAsyncAction_ShowMenuDialog* UAsyncAction_ShowMenuDialog::ShowMenuDialog(UObject* WorldContextObject, APlayerController* Player, FText Title, FText Body,
	const TArray<FMenuDialogButtonSpec>& Buttons, int32 DefaultButton, int32 CancelButton, UMenuStyleAsset* StyleAsset, float TimeoutSeconds, int32 TimeoutButton)
{
	UAsyncAction_ShowMenuDialog* Action = NewObject<UAsyncAction_ShowMenuDialog>();
	Action->Context = WorldContextObject;
	Action->PlayerController = Player;
	Action->DialogTitle = Title;
	Action->DialogBody = Body;
	Action->ButtonSpecs = Buttons;
	if (Action->ButtonSpecs.Num() == 0)
	{
		FMenuDialogButtonSpec& Ok = Action->ButtonSpecs.AddDefaulted_GetRef();
		Ok.Text = LOCTEXT("Ok", "OK");
		Ok.Style = EMenuDialogButtonStyle::Primary;
	}
	Action->DefaultIndex = FMath::Clamp(DefaultButton, 0, Action->ButtonSpecs.Num() - 1);
	Action->CancelIndex = Action->ButtonSpecs.IsValidIndex(CancelButton) ? CancelButton : INDEX_NONE;
	Action->TimeoutIndex = Action->ButtonSpecs.IsValidIndex(TimeoutButton) ? TimeoutButton : INDEX_NONE;
	Action->TimeLeft = TimeoutSeconds;
	Action->Style = StyleAsset;
	Action->RegisterWithGameInstance(WorldContextObject);
	return Action;
}

void UAsyncAction_ShowMenuDialog::Activate()
{
	ULocalPlayer* LocalPlayer = MenuPlayerUtils::ResolveLocalPlayer(Context.Get(), PlayerController.Get());
	UGameViewportClient* Viewport = LocalPlayer ? LocalPlayer->ViewportClient.Get() : nullptr;
	if (!Viewport)
	{
		Finish(INDEX_NONE);
		return;
	}
	StyleHolder->Style = UMenuStyleAsset::Resolve(Style);

	TArray<FMenuDialogButton> Buttons;
	for (const FMenuDialogButtonSpec& Spec : ButtonSpecs)
	{
		FMenuDialogButton& Button = Buttons.AddDefaulted_GetRef();
		Button.Text = Spec.Text;
		Button.Kind = Spec.Style == EMenuDialogButtonStyle::Primary ? EMenuButtonKind::Primary
			: Spec.Style == EMenuDialogButtonStyle::Subtle ? EMenuButtonKind::Ghost : EMenuButtonKind::Secondary;
	}

	TWeakObjectPtr<UAsyncAction_ShowMenuDialog> WeakThis(this);
	const FText Body = DialogBody;
	TSharedRef<SMenuDialog> Dialog = SNew(SMenuDialog, StyleHolder)
		.Title(DialogTitle)
		.Body(TAttribute<FText>::CreateLambda([WeakThis, Body]()
		{
			const UAsyncAction_ShowMenuDialog* Action = WeakThis.Get();
			if (Action && Action->TimeoutIndex != INDEX_NONE && Action->TimeLeft > 0.f)
			{
				return FText::Format(LOCTEXT("BodyCountdown", "{0}\n\n{1} in {2} s."), Body, Action->ButtonSpecs[Action->TimeoutIndex].Text, FText::AsNumber(FMath::CeilToInt(Action->TimeLeft)));
			}
			return Body;
		}))
		.Buttons(Buttons)
		.DefaultButton(DefaultIndex)
		.CancelButton(CancelIndex)
		.OnChoice_Lambda([WeakThis](int32 Index)
		{
			if (UAsyncAction_ShowMenuDialog* Action = WeakThis.Get())
			{
				// Next tick: the dialog isn't removed while its button is handling the click.
				Action->QueuedChoice = Index;
				Action->bChoiceQueued = true;
			}
		});
	DialogWidget = Dialog;
	Viewport->AddViewportWidgetForPlayer(LocalPlayer, Dialog, 20000);

	int32 UserIndex = 0;
	if (const TSharedPtr<const FSlateUser> User = LocalPlayer->GetSlateUser())
	{
		UserIndex = User->GetUserIndex();
	}
	if (FSlateApplication::IsInitialized())
	{
		PreviousFocus = FSlateApplication::Get().GetUserFocusedWidget(UserIndex);
	}
	Dialog->FocusDefault(UserIndex);
	if (MenuAccessibility::IsMenuNarrationEnabled())
	{
		UGameplayStatics::AnnounceAccessibleString(FString::Printf(TEXT("%s. %s"), *DialogTitle.ToString(), *DialogBody.ToString()));
	}
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UAsyncAction_ShowMenuDialog::Tick));
}

bool UAsyncAction_ShowMenuDialog::Tick(float DeltaTime)
{
	if (bChoiceQueued)
	{
		Finish(QueuedChoice);
		return false;
	}
	if (TimeoutIndex != INDEX_NONE && TimeLeft > 0.f)
	{
		TimeLeft -= FMath::Min(DeltaTime, 0.1f);
		if (TimeLeft <= 0.f)
		{
			Finish(TimeoutIndex);
			return false;
		}
	}
	return true;
}

void UAsyncAction_ShowMenuDialog::Cancel()
{
	Finish(INDEX_NONE);
}

void UAsyncAction_ShowMenuDialog::Finish(int32 Index)
{
	if (bFinished)
	{
		return;
	}
	bFinished = true;
	FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
	TickerHandle.Reset();

	ULocalPlayer* LocalPlayer = MenuPlayerUtils::ResolveLocalPlayer(Context.Get(), PlayerController.Get());
	if (DialogWidget.IsValid() && LocalPlayer && LocalPlayer->ViewportClient)
	{
		LocalPlayer->ViewportClient->RemoveViewportWidgetForPlayer(LocalPlayer, DialogWidget.ToSharedRef());
	}
	DialogWidget.Reset();
	if (const TSharedPtr<SWidget> Previous = PreviousFocus.Pin(); Previous.IsValid() && LocalPlayer && FSlateApplication::IsInitialized())
	{
		if (const TSharedPtr<const FSlateUser> User = LocalPlayer->GetSlateUser())
		{
			FSlateApplication::Get().SetUserFocus(User->GetUserIndex(), Previous, EFocusCause::SetDirectly);
		}
	}
	OnChosen.Broadcast(Index, ButtonSpecs.IsValidIndex(Index) ? ButtonSpecs[Index].Text : FText::GetEmpty());
	SetReadyToDestroy();
}

#undef LOCTEXT_NAMESPACE
