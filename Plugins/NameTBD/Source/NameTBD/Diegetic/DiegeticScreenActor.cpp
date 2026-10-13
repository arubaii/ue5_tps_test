// Copyright AR GameSystems 2026. All rights reserved.

#include "DiegeticScreenActor.h"

#include "Components/SceneComponent.h"
#include "DiegeticScreenComponent.h"

ADiegeticScreenActor::ADiegeticScreenActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	// Sized in centimetres from the start: a 160 x 90 cm screen at 12 pixels to the centimetre (1920 x 1080).
	Screen = CreateDefaultSubobject<UDiegeticScreenComponent>(TEXT("Screen"));
	Screen->SetupAttachment(Root);
	Screen->ScreenSizing = EDiegeticScreenSizing::PhysicalSize;
}
