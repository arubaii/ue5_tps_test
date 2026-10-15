// Copyright AR GameSystems 2026. All rights reserved.

#include "MenuPlayerUtils.h"

#include "Blueprint/UserWidget.h"
#include "Components/ActorComponent.h"
#include "Components/Widget.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "NameTBD/Menu/Settings/MenuSettingTypes.h"

namespace MenuPlayerUtils
{
	ULocalPlayer* ResolveLocalPlayer(const UObject* Context, const APlayerController* Player)
	{
		ULocalPlayer* LocalPlayer = Player ? Player->GetLocalPlayer() : nullptr;
		if (!LocalPlayer && Context)
		{
			const AActor* Actor = Cast<AActor>(Context);
			if (const UActorComponent* Component = Cast<UActorComponent>(Context))
			{
				Actor = Component->GetOwner();
			}
			if (const ULocalPlayer* AsLocalPlayer = Cast<ULocalPlayer>(Context))
			{
				LocalPlayer = const_cast<ULocalPlayer*>(AsLocalPlayer);
			}
			else if (const UUserWidget* AsUserWidget = Cast<UUserWidget>(Context))
			{
				LocalPlayer = AsUserWidget->GetOwningLocalPlayer();
			}
			else if (const UWidget* AsWidget = Cast<UWidget>(Context))
			{
				LocalPlayer = AsWidget->GetOwningLocalPlayer();
			}
			else if (const APlayerController* AsController = Cast<APlayerController>(Actor))
			{
				LocalPlayer = AsController->GetLocalPlayer();
			}
			else if (const APawn* AsPawn = Cast<APawn>(Actor))
			{
				if (const APlayerController* PawnController = Cast<APlayerController>(AsPawn->GetController()))
				{
					LocalPlayer = PawnController->GetLocalPlayer();
				}
			}
		}
		if (!LocalPlayer && Context && GEngine)
		{
			if (UWorld* World = GEngine->GetWorldFromContextObject(Context, EGetWorldErrorMode::ReturnNull))
			{
				LocalPlayer = World->GetFirstLocalPlayerFromController();
				if (!LocalPlayer && World->GetGameInstance())
				{
					LocalPlayer = World->GetGameInstance()->GetFirstGamePlayer();
				}
			}
		}
		return LocalPlayer;
	}

	UWorld* FindPlayingWorld(UWorld* World)
	{
		if (World && World->GetGameInstance() && World->GetGameInstance()->GetFirstGamePlayer())
		{
			return World;
		}
		if (GEngine)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if ((Context.WorldType == EWorldType::PIE || Context.WorldType == EWorldType::Game) && Context.World())
				{
					return Context.World();
				}
			}
		}
		return World;
	}

	void Report(const FString& Message)
	{
		UE_LOG(LogNameTBDSettings, Display, TEXT("%s"), *Message);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Cyan, Message);
		}
	}
}
