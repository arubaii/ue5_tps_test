// Copyright AR GameSystems 2026. All rights reserved.

#include "DiegeticUILibrary.h"

#include "Components/Widget.h"
#include "DiegeticInteractorComponent.h"
#include "DiegeticScreenComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"

UDiegeticScreenComponent* UDiegeticUILibrary::GetDiegeticScreen(const UWidget* OnScreen)
{
	return UDiegeticScreenComponent::FindScreenForWidget(OnScreen);
}

bool UDiegeticUILibrary::IsOnDiegeticScreen(const UWidget* OnScreen)
{
	return UDiegeticScreenComponent::FindScreenForWidget(OnScreen) != nullptr;
}

void UDiegeticUILibrary::MarkDiegeticContentChanged(const UWidget* OnScreen)
{
	if (UDiegeticScreenComponent* Screen = UDiegeticScreenComponent::FindScreenForWidget(OnScreen))
	{
		Screen->MarkContentChanged();
	}
}

FVector UDiegeticUILibrary::GetDiegeticWidgetWorldLocation(const UWidget* OnScreen)
{
	const UDiegeticScreenComponent* Screen = UDiegeticScreenComponent::FindScreenForWidget(OnScreen);
	return Screen ? Screen->GetWorldLocationOfWidget(OnScreen) : FVector::ZeroVector;
}

UDiegeticInteractorComponent* UDiegeticUILibrary::GetDiegeticInteractor(const APlayerController* Player)
{
	return UDiegeticInteractorComponent::FindInteractor(Player);
}

bool UDiegeticUILibrary::BeginUsingDiegeticScreen(APlayerController* Player, UDiegeticScreenComponent* Screen)
{
	return Screen && Screen->BeginUse(Player);
}

void UDiegeticUILibrary::EndUsingDiegeticScreen(APlayerController* Player)
{
	if (UDiegeticInteractorComponent* Interactor = UDiegeticInteractorComponent::FindInteractor(Player))
	{
		Interactor->EndUsing();
	}
}

UObject* UDiegeticUILibrary::GetDiegeticScreenContext(const UWidget* OnScreen)
{
	const UDiegeticScreenComponent* Screen = UDiegeticScreenComponent::FindScreenForWidget(OnScreen);
	return Screen ? Screen->GetScreenContext() : nullptr;
}

AActor* UDiegeticUILibrary::GetDiegeticScreenOwner(const UWidget* OnScreen)
{
	const UDiegeticScreenComponent* Screen = UDiegeticScreenComponent::FindScreenForWidget(OnScreen);
	return Screen ? Screen->GetOwner() : nullptr;
}

bool UDiegeticUILibrary::OpenDiegeticMenu(APlayerController* Player, UDiegeticScreenComponent* Screen)
{
	if (!Screen)
	{
		return false;
	}
	Screen->SetScreenShown(true);
	if (Screen->UseSettings.UseMode == EDiegeticUseMode::None)
	{
		return true;
	}
	const bool bUsing = Screen->BeginUse(Player);
	Screen->SetHideWhenUseEnds(bUsing);
	return bUsing;
}

void UDiegeticUILibrary::CloseDiegeticMenu(UDiegeticScreenComponent* Screen)
{
	if (Screen)
	{
		Screen->SetScreenShown(false);
	}
}

UDiegeticScreenComponent* UDiegeticUILibrary::GetUsedDiegeticScreen(const APlayerController* Player)
{
	const UDiegeticInteractorComponent* Interactor = UDiegeticInteractorComponent::FindInteractor(Player);
	return Interactor ? Interactor->GetUsedScreen() : nullptr;
}
