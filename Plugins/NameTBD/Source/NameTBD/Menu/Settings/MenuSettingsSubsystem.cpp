// Copyright AR GameSystems 2026. All rights reserved.

#include "MenuSettingsSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProperties.h"
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"
#include "RHI.h"
#include "UserSettings/EnhancedInputUserSettings.h"
#include "Widgets/Layout/SBox.h"
#include "MenuProjectSettings.h"
#include "MenuSettingsDefaults.h"
#include "MenuSettingTargets.h"
#include "NameTBD/Menu/Graph/StatGraph.h"
#include "NameTBD/Menu/Graphics/MenuUpscalers.h"
#include "NameTBD/Menu/Input/MenuInputSubsystem.h"
#include "NameTBD/Menu/MenuPlayerUtils.h"
#include "NameTBD/Notifications/NotificationSubsystem.h"

#define LOCTEXT_NAMESPACE "MenuSettingsSubsystem"

namespace MenuSettingsSubsystemLocal
{
	const FName PerformanceOverlayId(TEXT("Video.PerformanceOverlay"));

	EMenuInputDevice DeviceOf(const FKey& Key)
	{
		return MenuInputUtils::GetKeyDevice(Key);
	}

	bool MatchesDevice(const FKey& Key, EMenuKeyBindingDevice Device)
	{
		const EMenuInputDevice KeyDevice = DeviceOf(Key);
		return Device == EMenuKeyBindingDevice::Gamepad ? KeyDevice == EMenuInputDevice::Gamepad : KeyDevice == EMenuInputDevice::KeyboardMouse;
	}

	FString GpuVendorName()
	{
		switch (UMenuGraphicsLibrary::GetGPUVendor())
		{
		case EMenuGPUVendor::NVIDIA: return TEXT("NVIDIA");
		case EMenuGPUVendor::AMD: return TEXT("AMD");
		case EMenuGPUVendor::Intel: return TEXT("Intel");
		default: return TEXT("Any");
		}
	}
}

// =====================================================================================================
//  Lifecycle
// =====================================================================================================

UMenuSettingsSubsystem* UMenuSettingsSubsystem::Get(const UObject* WorldContext, const APlayerController* Player)
{
	ULocalPlayer* LocalPlayer = MenuPlayerUtils::ResolveLocalPlayer(WorldContext, Player);
	return LocalPlayer ? LocalPlayer->GetSubsystem<UMenuSettingsSubsystem>() : nullptr;
}

bool UMenuSettingsSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return Super::ShouldCreateSubsystem(Outer) && !IsRunningDedicatedServer();
}

void UMenuSettingsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Key bindings come from Enhanced Input's user settings; several targets write the notification and input preferences.
	Collection.InitializeDependency(UEnhancedInputLocalPlayerSubsystem::StaticClass());
	Collection.InitializeDependency(UNotificationSubsystem::StaticClass());
	Collection.InitializeDependency(UMenuInputSubsystem::StaticClass());

	BuiltInSchema = MenuSettingsDefaults::MakeBuiltInSchema(this);
	RegisterKeyBindingContexts();
	RebuildPages();

	const UMenuProjectSettings& Settings = UMenuProjectSettings::Get();
	MachineSave = LoadSave(Settings.MachineSaveSlot, 0);
	UserSave = LoadSave(Settings.UserSaveSlot, GetUserIndex());

	LoadValues();
	ApplySavedValues();

	PlayerWatchTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UMenuSettingsSubsystem::TickPlayerWatch), 0.25f);

#if WITH_EDITOR
	SchemaChangedHandle = UMenuSettingsSchema::OnAnySchemaChanged().AddWeakLambda(this, [this]()
	{
		RebuildPages();
		LoadValues();
	});
#endif
}

void UMenuSettingsSubsystem::Deinitialize()
{
	if (Benchmark.IsValid())
	{
		Benchmark->Cancel();
		Benchmark.Reset();
	}
	if (IsAwaitingConfirmation())
	{
		// Leaving with a display change unconfirmed: put it back, as the countdown would have.
		EndConfirmation(false);
	}
	FTSTicker::GetCoreTicker().RemoveTicker(PlayerWatchTicker);
	FTSTicker::GetCoreTicker().RemoveTicker(ConfirmTicker);
	UMenuSettingsSchema::OnAnySchemaChanged().Remove(SchemaChangedHandle);

	if (PerformanceOverlaySlate.IsValid())
	{
		if (ULocalPlayer* LocalPlayer = GetLocalPlayer(); LocalPlayer && LocalPlayer->ViewportClient)
		{
			LocalPlayer->ViewportClient->RemoveViewportWidgetForPlayer(LocalPlayer, PerformanceOverlaySlate.ToSharedRef());
		}
		PerformanceOverlaySlate.Reset();
	}

	// On the way out the save can't be left to a worker thread.
	bForceSyncSave = true;
	SaveSettings();
	Super::Deinitialize();
}

FMenuSettingContext UMenuSettingsSubsystem::MakeContext() const
{
	FMenuSettingContext Context;
	Context.LocalPlayer = GetLocalPlayer();
	Context.Subsystem = const_cast<UMenuSettingsSubsystem*>(this);
	return Context;
}

// =====================================================================================================
//  Definitions
// =====================================================================================================

void UMenuSettingsSubsystem::RebuildPages()
{
	TArray<UMenuSettingsSchema*> Extra;
	for (UMenuSettingsSchema* Schema : RuntimeSchemas)
	{
		Extra.Add(Schema);
	}
	TArray<const UMenuSettingsSchema*> Schemas;
	MenuSettingsSchemaUtils::GatherSchemas(BuiltInSchema, Extra, Schemas);
	MenuSettingsSchemaUtils::Merge(Schemas, MenuSettingsSchemaUtils::MakeProjectRules(), Pages);

	BuildKeyBindingSettings();

	SettingsById.Reset();
	for (const FMenuSettingsPage& Page : Pages)
	{
		for (const FMenuSettingsSection& Section : Page.Sections)
		{
			for (UMenuSetting* Setting : Section.Settings)
			{
				if (Setting && !Setting->Id.IsNone())
				{
					SettingsById.Add(Setting->Id, Setting);
				}
			}
		}
	}
	OnPagesChanged.Broadcast();
	PagesChangedNative.Broadcast();
}

void UMenuSettingsSubsystem::RegisterKeyBindingContexts()
{
	UEnhancedInputUserSettings* InputSettings = GetInputUserSettings();
	if (!InputSettings)
	{
		return;
	}
	for (const TSoftObjectPtr<UInputMappingContext>& Entry : UMenuProjectSettings::Get().KeyBindingContexts)
	{
		if (const UInputMappingContext* Context = Entry.LoadSynchronous())
		{
			InputSettings->RegisterInputMappingContext(Context);
		}
	}
}

void UMenuSettingsSubsystem::BuildKeyBindingSettings()
{
	GeneratedSettings.Reset();
	UEnhancedInputUserSettings* InputSettings = GetInputUserSettings();
	const UEnhancedPlayerMappableKeyProfile* Profile = InputSettings ? InputSettings->GetActiveKeyProfile() : nullptr;

	for (FMenuSettingsPage& Page : Pages)
	{
		for (FMenuSettingsSection& Section : Page.Sections)
		{
			if (!Section.bKeyBindings)
			{
				continue;
			}
			if (!Profile)
			{
#if !UE_BUILD_SHIPPING
				// Say why the list is empty during development; a shipped game just shows no key binding rows.
				UMenuSettingInfo* Hint = NewObject<UMenuSettingInfo>(this);
				Hint->Id = FName(*FString::Printf(TEXT("Key.%s.Unavailable"), *Section.GetMergeId().ToString()));
				Hint->DisplayName = LOCTEXT("KeysUnavailable", "Key Bindings");
				Hint->Text = LOCTEXT("KeysUnavailableText", "Turn on Enable User Settings in Project Settings > Engine > Enhanced Input, and list your mapping contexts in Project Settings > Plugins > Menus > Key Binding Contexts.");
				Section.Settings.Add(Hint);
				GeneratedSettings.Add(Hint);
#endif
				continue;
			}

			struct FRow { FName Name; FText DisplayName; FText Category; };
			TArray<FRow> Rows;
			for (const TPair<FName, FKeyMappingRow>& Pair : Profile->GetPlayerMappingRows())
			{
				const FPlayerKeyMapping* First = nullptr;
				for (const FPlayerKeyMapping& Mapping : Pair.Value.Mappings)
				{
					const FKey Key = Mapping.GetDefaultKey().IsValid() ? Mapping.GetDefaultKey() : Mapping.GetCurrentKey();
					if (MenuSettingsSubsystemLocal::MatchesDevice(Key, Section.KeyBindingDevice))
					{
						First = &Mapping;
						break;
					}
				}
				if (!First)
				{
					continue;
				}
				const FText Category = First->GetDisplayCategory();
				if (Section.KeyBindingCategories.Num() > 0 && !Section.KeyBindingCategories.ContainsByPredicate([&Category](const FString& Wanted) { return Wanted.Equals(Category.ToString(), ESearchCase::IgnoreCase); }))
				{
					continue;
				}
				Rows.Add({ Pair.Key, First->GetDisplayName().IsEmpty() ? FText::FromName(Pair.Key) : First->GetDisplayName(), Category });
			}
			// Grouped by category (the screen puts a heading over each), then by name.
			Rows.StableSort([](const FRow& A, const FRow& B)
			{
				const int32 ByCategory = A.Category.CompareTo(B.Category);
				return ByCategory != 0 ? ByCategory < 0 : A.DisplayName.CompareTo(B.DisplayName) < 0;
			});

			for (const FRow& Row : Rows)
			{
				UMenuSettingKeyBinding* Binding = NewObject<UMenuSettingKeyBinding>(this);
				Binding->Id = FName(*FString::Printf(TEXT("Key.%s.%s"), Section.KeyBindingDevice == EMenuKeyBindingDevice::Gamepad ? TEXT("Gamepad") : TEXT("Keyboard"), *Row.Name.ToString()));
				Binding->DisplayName = Row.DisplayName;
				Binding->DisplayCategory = Row.Category;
				Binding->MappingName = Row.Name;
				Binding->Device = Section.KeyBindingDevice;
				Binding->SlotCount = Section.KeyBindingDevice == EMenuKeyBindingDevice::Gamepad ? 1 : 2;
				Binding->Keywords = { Row.Category.ToString(), TEXT("key"), TEXT("bind") };
				Section.Settings.Add(Binding);
				GeneratedSettings.Add(Binding);
			}
		}
	}
}

TArray<FName> UMenuSettingsSubsystem::GetPageIds() const
{
	TArray<FName> Ids;
	for (const FMenuSettingsPage& Page : Pages)
	{
		if (MenuSettingValues::IsCurrentPlatformIn(Page.Platforms))
		{
			Ids.Add(Page.Id);
		}
	}
	return Ids;
}

bool UMenuSettingsSubsystem::GetPage(FName PageId, FMenuSettingsPage& OutPage) const
{
	for (const FMenuSettingsPage& Page : Pages)
	{
		if (Page.Id == PageId)
		{
			OutPage = Page;
			return true;
		}
	}
	return false;
}

UMenuSetting* UMenuSettingsSubsystem::FindSetting(FName SettingId) const
{
	const TObjectPtr<UMenuSetting>* Found = SettingsById.Find(SettingId);
	return Found ? Found->Get() : nullptr;
}

FName UMenuSettingsSubsystem::FindPageOf(FName SettingId) const
{
	for (const FMenuSettingsPage& Page : Pages)
	{
		for (const FMenuSettingsSection& Section : Page.Sections)
		{
			for (const UMenuSetting* Setting : Section.Settings)
			{
				if (Setting && Setting->Id == SettingId)
				{
					return Page.Id;
				}
			}
		}
	}
	return NAME_None;
}

void UMenuSettingsSubsystem::RegisterSchema(UMenuSettingsSchema* Schema)
{
	if (Schema && !RuntimeSchemas.Contains(Schema))
	{
		RuntimeSchemas.Add(Schema);
		RebuildPages();
		LoadValues();
		ApplySavedValues();
	}
}

void UMenuSettingsSubsystem::UnregisterSchema(UMenuSettingsSchema* Schema)
{
	if (RuntimeSchemas.Remove(Schema) > 0)
	{
		RebuildPages();
		LoadValues();
	}
}

void UMenuSettingsSubsystem::RefreshKeyBindings()
{
	RegisterKeyBindingContexts();
	RebuildPages();
}

// =====================================================================================================
//  Loading and applying
// =====================================================================================================

UMenuSettingsSaveGame* UMenuSettingsSubsystem::LoadSave(const FString& Slot, int32 UserIndex) const
{
	if (!Slot.IsEmpty() && UGameplayStatics::DoesSaveGameExist(Slot, UserIndex))
	{
		if (UMenuSettingsSaveGame* Loaded = Cast<UMenuSettingsSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot, UserIndex)))
		{
			return Loaded;
		}
		UE_LOG(LogNameTBDSettings, Warning, TEXT("Settings save '%s' (user %d) couldn't be read; starting from defaults."), *Slot, UserIndex);
	}
	return NewObject<UMenuSettingsSaveGame>(const_cast<UMenuSettingsSubsystem*>(this));
}

void UMenuSettingsSubsystem::WriteSave(UMenuSettingsSaveGame* Save, const FString& Slot, int32 UserIndex) const
{
	if (!Save || Slot.IsEmpty())
	{
		return;
	}
	if (UMenuProjectSettings::Get().bSaveAsync && !bForceSyncSave)
	{
		UGameplayStatics::AsyncSaveGameToSlot(Save, Slot, UserIndex);
	}
	else
	{
		UGameplayStatics::SaveGameToSlot(Save, Slot, UserIndex);
	}
}

int32 UMenuSettingsSubsystem::GetUserIndex() const
{
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	return LocalPlayer ? FMath::Max(0, LocalPlayer->GetPlatformUserIndex()) : 0;
}

const FString* UMenuSettingsSubsystem::FindSavedValue(const UMenuSetting& Setting) const
{
	const UMenuSettingsSaveGame* Save = Setting.Scope == EMenuSettingScope::Machine ? MachineSave.Get() : UserSave.Get();
	return Save ? Save->Values.Find(Setting.Id) : nullptr;
}

void UMenuSettingsSubsystem::StoreValue(const UMenuSetting& Setting, const FString* Value)
{
	if (Setting.Target && !Setting.Target->UsesSavedValue())
	{
		return;
	}
	UMenuSettingsSaveGame* Save = Setting.Scope == EMenuSettingScope::Machine ? MachineSave.Get() : UserSave.Get();
	if (!Save)
	{
		return;
	}
	if (Value)
	{
		Save->Values.Add(Setting.Id, *Value);
	}
	else
	{
		Save->Values.Remove(Setting.Id);
	}
	(Setting.Scope == EMenuSettingScope::Machine ? bMachineDirty : bUserDirty) = true;
}

void UMenuSettingsSubsystem::LoadValues()
{
	const FMenuSettingContext Context = MakeContext();
	TMap<FName, FString> Loaded;
	for (const TPair<FName, TObjectPtr<UMenuSetting>>& Pair : SettingsById)
	{
		const UMenuSetting* Setting = Pair.Value;
		if (!Setting || !Setting->HoldsValue())
		{
			continue;
		}
		FString Value;
		const FString* Saved = FindSavedValue(*Setting);
		const bool bTargetKeepsIt = Setting->Target && !Setting->Target->UsesSavedValue();
		if (!bTargetKeepsIt && Saved)
		{
			Value = *Saved;
		}
		else if (!(Setting->Target && Setting->Target->Read(*Setting, Context, Value)))
		{
			Value = Setting->GetDefaultValue(Context);
		}
		Loaded.Add(Pair.Key, Setting->Sanitize(Value, Context));
	}
	Values = MoveTemp(Loaded);
}

bool UMenuSettingsSubsystem::ShouldWriteOnStart(const UMenuSetting& Setting) const
{
	// Only what the player chose (and what a target must have) is written: an untouched cvar, volume or camera stays the game's.
	return Setting.Target && Setting.Target->UsesSavedValue() && (FindSavedValue(Setting) != nullptr || Setting.Target->WritesDefaultOnStart(Setting));
}

void UMenuSettingsSubsystem::ApplySavedValues()
{
	const FMenuSettingContext Context = MakeContext();
	for (const TPair<FName, TObjectPtr<UMenuSetting>>& Pair : SettingsById)
	{
		const UMenuSetting* Setting = Pair.Value;
		if (!Setting || !Setting->HoldsValue() || !Setting->Target || !Setting->Target->UsesSavedValue())
		{
			continue;
		}
		if (!ShouldWriteOnStart(*Setting))
		{
			continue;
		}
		FText Reason;
		if (Setting->IsAvailable(Context, Reason))
		{
			Setting->Target->Write(*Setting, Context, Values.FindRef(Pair.Key));
		}
	}
	RefreshValuesFromTargets();
}

void UMenuSettingsSubsystem::RefreshValuesFromTargets()
{
	const FMenuSettingContext Context = MakeContext();
	for (TPair<FName, FString>& Pair : Values)
	{
		const UMenuSetting* Setting = FindSetting(Pair.Key);
		if (!Setting || !Setting->Target)
		{
			continue;
		}
		FString Read;
		if (Setting->Target->Read(*Setting, Context, Read))
		{
			Pair.Value = Setting->Sanitize(Read, Context);
		}
	}
}

void UMenuSettingsSubsystem::ReapplyAfterMapOrPawn()
{
	const FMenuSettingContext Context = MakeContext();
	for (const TPair<FName, TObjectPtr<UMenuSetting>>& Pair : SettingsById)
	{
		const UMenuSetting* Setting = Pair.Value;
		if (Setting && Setting->HoldsValue() && Setting->Target && Setting->Target->ReapplyAfterMapLoad() && ShouldWriteOnStart(*Setting))
		{
			FText Reason;
			if (Setting->IsAvailable(Context, Reason))
			{
				Setting->Target->Write(*Setting, Context, GetSettingValue(Pair.Key));
			}
		}
	}
}

bool UMenuSettingsSubsystem::TickPlayerWatch(float DeltaTime)
{
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	APlayerController* Controller = LocalPlayer ? LocalPlayer->GetPlayerController(LocalPlayer->GetWorld()) : nullptr;
	if (Controller != WatchedController.Get())
	{
		WatchedController = Controller;
		if (Controller)
		{
			HandlePlayerReady(Controller);
		}
	}
	return true;
}

void UMenuSettingsSubsystem::HandlePlayerReady(APlayerController* Controller)
{
	Controller->OnPossessedPawnChanged.AddUniqueDynamic(this, &UMenuSettingsSubsystem::HandlePawnChanged);
	ReapplyAfterMapOrPawn();
	PerformanceOverlayMode.Reset(); // a new viewport after a map change: put it back
	UpdatePerformanceOverlay();
}

void UMenuSettingsSubsystem::HandlePawnChanged(APawn* OldPawn, APawn* NewPawn)
{
	ReapplyAfterMapOrPawn();
}

// =====================================================================================================
//  Values
// =====================================================================================================

FString UMenuSettingsSubsystem::GetSettingValue(FName SettingId) const
{
	if (const FString* Waiting = Pending.Find(SettingId))
	{
		return *Waiting;
	}
	if (const FString* Current = Values.Find(SettingId))
	{
		return *Current;
	}
	return FString();
}

bool UMenuSettingsSubsystem::GetSettingBool(FName SettingId, bool bDefault) const
{
	const FString Value = GetSettingValue(SettingId);
	return Value.IsEmpty() ? bDefault : MenuSettingValues::ToBool(Value);
}

float UMenuSettingsSubsystem::GetSettingNumber(FName SettingId, float DefaultValue) const
{
	const FString Value = GetSettingValue(SettingId);
	return Value.IsEmpty() ? DefaultValue : static_cast<float>(MenuSettingValues::ToNumber(Value, DefaultValue));
}

FText UMenuSettingsSubsystem::GetSettingDisplayValue(FName SettingId) const
{
	const UMenuSetting* Setting = FindSetting(SettingId);
	return Setting ? Setting->FormatValue(GetSettingValue(SettingId), MakeContext()) : FText::GetEmpty();
}

FString UMenuSettingsSubsystem::GetSettingDefault(FName SettingId) const
{
	const UMenuSetting* Setting = FindSetting(SettingId);
	return Setting ? Setting->GetDefaultValue(MakeContext()) : FString();
}

bool UMenuSettingsSubsystem::IsSettingModified(FName SettingId) const
{
	const UMenuSetting* Setting = FindSetting(SettingId);
	if (!Setting || !Setting->HoldsValue())
	{
		return false;
	}
	const FString Value = GetSettingValue(SettingId);
	const FString Default = GetSettingDefault(SettingId);
	if (MenuSettingValues::IsNumber(Value) && MenuSettingValues::IsNumber(Default))
	{
		return !FMath::IsNearlyEqual(MenuSettingValues::ToNumber(Value), MenuSettingValues::ToNumber(Default), 1e-4);
	}
	return !Value.Equals(Default, ESearchCase::IgnoreCase);
}

TArray<FMenuSettingOption> UMenuSettingsSubsystem::GetSettingOptions(FName SettingId) const
{
	TArray<FMenuSettingOption> Options;
	if (const UMenuSettingChoice* Choice = Cast<UMenuSettingChoice>(FindSetting(SettingId)))
	{
		Choice->GetOptions(MakeContext(), Options, GetSettingValue(SettingId));
	}
	return Options;
}

bool UMenuSettingsSubsystem::ShouldWaitForApply(const UMenuSetting& Setting) const
{
	switch (Setting.ApplyMode)
	{
	case EMenuSettingApplyMode::Immediately: return false;
	case EMenuSettingApplyMode::OnApply: return EditingDepth > 0;
	default: return EditingDepth > 0 && bApplyButtonPolicy;
	}
}

bool UMenuSettingsSubsystem::SetSettingValue(FName SettingId, const FString& Value)
{
	const UMenuSetting* Setting = FindSetting(SettingId);
	if (!Setting || !Setting->HoldsValue())
	{
		return false;
	}
	FText Reason;
	if (!IsSettingEnabled(SettingId, Reason))
	{
		return false;
	}
	const FMenuSettingContext Context = MakeContext();
	const FString Clean = Setting->Sanitize(Value, Context);
	if (Clean == GetSettingValue(SettingId))
	{
		return true;
	}

	if (ShouldWaitForApply(*Setting))
	{
		// Back to what is applied: nothing waits any more.
		if (Clean == Values.FindRef(SettingId))
		{
			Pending.Remove(SettingId);
		}
		else
		{
			Pending.Add(SettingId, Clean);
		}
		OnSettingChanged.Broadcast(SettingId, Clean);
		SettingChangedNative.Broadcast(SettingId, Clean);
		return true;
	}

	const FString Old = Values.FindRef(SettingId);
	CommitValue(*Setting, Clean, true);
	if (Setting->bRequiresConfirmation)
	{
		TMap<FName, FString> Revert;
		Revert.Add(SettingId, Old);
		StartConfirmation(Revert);
	}
	return true;
}

bool UMenuSettingsSubsystem::SetSettingBool(FName SettingId, bool bValue)
{
	return SetSettingValue(SettingId, MenuSettingValues::FromBool(bValue));
}

bool UMenuSettingsSubsystem::SetSettingNumber(FName SettingId, float Value)
{
	return SetSettingValue(SettingId, MenuSettingValues::FromNumber(Value));
}

bool UMenuSettingsSubsystem::StepSetting(FName SettingId, int32 Direction)
{
	const UMenuSetting* Setting = FindSetting(SettingId);
	if (!Setting || Direction == 0)
	{
		return false;
	}
	return SetSettingValue(SettingId, Setting->StepValue(GetSettingValue(SettingId), Direction, MakeContext()));
}

void UMenuSettingsSubsystem::CommitValue(const UMenuSetting& Setting, const FString& Value, bool bFromPlayer)
{
	const TMap<FName, FString> Before = Values;
	TSet<FName> Visited;
	CommitInner(Setting, Value, bFromPlayer, Visited);
	RefreshValuesFromTargets();
	BroadcastChanges(Before);
	// The setting itself reports even when a target read it back unchanged (a cvar that didn't exist).
	if (Before.FindRef(Setting.Id) == Values.FindRef(Setting.Id))
	{
		OnSettingChanged.Broadcast(Setting.Id, Values.FindRef(Setting.Id));
		SettingChangedNative.Broadcast(Setting.Id, Values.FindRef(Setting.Id));
	}
	UpdatePerformanceOverlay();
}

void UMenuSettingsSubsystem::CommitInner(const UMenuSetting& Setting, const FString& Value, bool bFromPlayer, TSet<FName>& Visited)
{
	if (Visited.Contains(Setting.Id))
	{
		return;
	}
	Visited.Add(Setting.Id);

	const FMenuSettingContext Context = MakeContext();
	FText Reason;
	if (Setting.Target && Setting.IsAvailable(Context, Reason))
	{
		Setting.Target->Write(Setting, Context, Value);
		DirtyTargets.Add(Setting.Target->GetClass(), Setting.Target.Get());
	}
	Values.Add(Setting.Id, Value);
	Pending.Remove(Setting.Id);
	if (bFromPlayer)
	{
		StoreValue(Setting, &Value);
	}

	if (bFromPlayer && Setting.bRequiresRestart && !RestartSettings.Contains(Setting.Id))
	{
		RestartSettings.Add(Setting.Id);
		if (UMenuProjectSettings::Get().bNotifyRestartRequired)
		{
			PostNotification(LOCTEXT("RestartTitle", "Restart required"),
				FText::Format(LOCTEXT("RestartBody", "{0} takes effect after the game restarts."), Setting.GetLabel()));
		}
	}

	// Whatever reads this one's value is written again (Resolution Scale after the upscaler changed).
	RefreshValuesFromTargets();
	for (const TPair<FName, TObjectPtr<UMenuSetting>>& Pair : SettingsById)
	{
		const UMenuSetting* Dependent = Pair.Value;
		if (Dependent && Dependent != &Setting && Dependent->HoldsValue() && Dependent->ReapplyWhenChanged.Contains(Setting.Id))
		{
			FText DependentReason;
			if (Dependent->IsAvailable(Context, DependentReason))
			{
				CommitInner(*Dependent, Dependent->Sanitize(Values.FindRef(Pair.Key), Context), false, Visited);
			}
		}
	}
}

void UMenuSettingsSubsystem::BroadcastChanges(const TMap<FName, FString>& Before)
{
	for (const TPair<FName, FString>& Pair : Values)
	{
		const FString* Old = Before.Find(Pair.Key);
		if (!Old || *Old != Pair.Value)
		{
			OnSettingChanged.Broadcast(Pair.Key, Pair.Value);
			SettingChangedNative.Broadcast(Pair.Key, Pair.Value);
		}
	}
}

void UMenuSettingsSubsystem::ResetSetting(FName SettingId)
{
	const UMenuSetting* Setting = FindSetting(SettingId);
	if (!Setting)
	{
		return;
	}
	if (const UMenuSettingKeyBinding* Binding = Cast<UMenuSettingKeyBinding>(Setting))
	{
		ResetKeyBinding(Binding->MappingName);
		return;
	}
	if (!Setting->HoldsValue())
	{
		return;
	}
	Pending.Remove(SettingId);
	CommitValue(*Setting, Setting->GetDefaultValue(MakeContext()), false);
	// Back to default is back to the engine deciding: no saved value, so it isn't written on the next start.
	StoreValue(*Setting, nullptr);
}

void UMenuSettingsSubsystem::ResetPage(FName PageId)
{
	for (const FMenuSettingsPage& Page : Pages)
	{
		if (Page.Id != PageId)
		{
			continue;
		}
		for (const FMenuSettingsSection& Section : Page.Sections)
		{
			for (const UMenuSetting* Setting : Section.Settings)
			{
				if (Setting && IsSettingShown(Setting->Id))
				{
					ResetSetting(Setting->Id);
				}
			}
		}
	}
}

void UMenuSettingsSubsystem::ResetAllSettings(bool bIncludeKeyBindings)
{
	TArray<FName> Ids;
	SettingsById.GenerateKeyArray(Ids);
	for (const FName& Id : Ids)
	{
		const UMenuSetting* Setting = FindSetting(Id);
		if (Setting && Setting->HoldsValue())
		{
			ResetSetting(Id);
		}
	}
	if (bIncludeKeyBindings)
	{
		ResetAllKeyBindings();
	}
}

TArray<FMenuSettingValue> UMenuSettingsSubsystem::GetAllSettingValues() const
{
	TArray<FMenuSettingValue> Out;
	for (const TPair<FName, FString>& Pair : Values)
	{
		FMenuSettingValue& Entry = Out.AddDefaulted_GetRef();
		Entry.Id = Pair.Key;
		Entry.Value = GetSettingValue(Pair.Key);
	}
	return Out;
}

void UMenuSettingsSubsystem::SetAllSettingValues(const TArray<FMenuSettingValue>& InValues)
{
	for (const FMenuSettingValue& Entry : InValues)
	{
		if (const UMenuSetting* Setting = FindSetting(Entry.Id))
		{
			if (Setting->HoldsValue())
			{
				CommitValue(*Setting, Setting->Sanitize(Entry.Value, MakeContext()), true);
			}
		}
	}
	SaveSettings();
}

// =====================================================================================================
//  Conditions
// =====================================================================================================

FString UMenuSettingsSubsystem::GetConditionValue(FName SettingId) const
{
	const FString Name = SettingId.ToString();
	if (!Name.StartsWith(TEXT("@")))
	{
		return GetSettingValue(SettingId);
	}
	if (Name == TEXT("@ActiveUpscaler")) return UMenuGraphicsLibrary::GetActiveGraphicsFeature(EMenuGraphicsFeatureKind::Upscaler).ToString();
	if (Name == TEXT("@ActiveFrameGeneration")) return UMenuGraphicsLibrary::GetActiveGraphicsFeature(EMenuGraphicsFeatureKind::FrameGeneration).ToString();
	if (Name == TEXT("@ActiveLatencyReduction")) return UMenuGraphicsLibrary::GetActiveGraphicsFeature(EMenuGraphicsFeatureKind::LatencyReduction).ToString();
	if (Name == TEXT("@GPUVendor")) return MenuSettingsSubsystemLocal::GpuVendorName();
	if (Name == TEXT("@Platform")) return FPlatformProperties::IniPlatformName();
	if (Name == TEXT("@RayTracingSupported")) return MenuSettingValues::FromBool(GRHISupportsRayTracing);
	if (Name == TEXT("@InputDevice"))
	{
		const UMenuInputSubsystem* Input = GetLocalPlayer() ? GetLocalPlayer()->GetSubsystem<UMenuInputSubsystem>() : nullptr;
		const EMenuInputDevice Device = Input ? Input->GetCurrentDevice() : EMenuInputDevice::KeyboardMouse;
		return Device == EMenuInputDevice::Gamepad ? TEXT("Gamepad") : Device == EMenuInputDevice::Touch ? TEXT("Touch") : TEXT("KeyboardMouse");
	}
	if (Name == TEXT("@Editing")) return MenuSettingValues::FromBool(IsEditing());
	return FString();
}

bool UMenuSettingsSubsystem::AreConditionsMet(const TArray<FMenuSettingCondition>& Conditions) const
{
	for (const FMenuSettingCondition& Condition : Conditions)
	{
		const bool bKnown = Condition.SettingId.ToString().StartsWith(TEXT("@")) || SettingsById.Contains(Condition.SettingId);
		if (!bKnown || !Condition.Test(GetConditionValue(Condition.SettingId)))
		{
			return false;
		}
	}
	return true;
}

bool UMenuSettingsSubsystem::IsSettingShown(FName SettingId) const
{
	const UMenuSetting* Setting = FindSetting(SettingId);
	if (!Setting)
	{
		return false;
	}
	FText Reason;
	if (!MenuSettingValues::IsCurrentPlatformIn(Setting->Platforms))
	{
		return false;
	}
	if (!Setting->IsAvailable(MakeContext(), Reason) && Setting->WhenUnavailable == EMenuUnavailableBehavior::Hide)
	{
		return false;
	}
	return AreConditionsMet(Setting->ShownWhen);
}

bool UMenuSettingsSubsystem::IsSettingEnabled(FName SettingId, FText& Reason) const
{
	const UMenuSetting* Setting = FindSetting(SettingId);
	if (!Setting)
	{
		return false;
	}
	if (!Setting->IsAvailable(MakeContext(), Reason))
	{
		return false;
	}
	if (!AreConditionsMet(Setting->EnabledWhen))
	{
		Reason = Setting->DisabledReason.IsEmpty() ? LOCTEXT("DisabledByOther", "Not available with your other settings.") : Setting->DisabledReason;
		return false;
	}
	if (Setting->GetKind() == EMenuSettingKind::KeyBinding && !CanRebindKeys())
	{
		Reason = LOCTEXT("NoRebind", "Keys can't be rebound in this game.");
		return false;
	}
	return true;
}

// =====================================================================================================
//  Actions
// =====================================================================================================

void UMenuSettingsSubsystem::RunSettingAction(FName SettingId)
{
	const UMenuSettingAction* Action = Cast<UMenuSettingAction>(FindSetting(SettingId));
	if (!Action)
	{
		return;
	}
	FText Reason;
	if (!IsSettingEnabled(SettingId, Reason))
	{
		return;
	}
	switch (Action->ActionKind)
	{
	case EMenuSettingActionKind::AutoDetectGraphics: RunAutoDetect(); break;
	case EMenuSettingActionKind::ResetPage: ResetPage(FindPageOf(SettingId)); break;
	case EMenuSettingActionKind::ResetAll: ResetAllSettings(true); break;
	case EMenuSettingActionKind::ResetKeyBindings: ResetAllKeyBindings(); break;
	case EMenuSettingActionKind::RunBenchmark: StartBenchmark(Action->BenchmarkSeconds, true); break;
	default: break;
	}
	OnSettingAction.Broadcast(SettingId);
}

void UMenuSettingsSubsystem::RunAutoDetect()
{
	UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!Settings)
	{
		return;
	}
	const TMap<FName, FString> Before = Values;
	// The engine's own measurement (a short CPU and GPU test), then its recommended scalability levels.
	Settings->RunHardwareBenchmark();
	Settings->ApplyHardwareBenchmarkResults();

	for (const TPair<FName, TObjectPtr<UMenuSetting>>& Pair : SettingsById)
	{
		if (Pair.Value && Pair.Value->Target && Pair.Value->Target->IsA<UMenuSettingTarget_GameUserSettings>())
		{
			DirtyTargets.Add(Pair.Value->Target->GetClass(), Pair.Value->Target.Get());
			break;
		}
	}
	if (const UMenuSetting* Upscaler = FindSetting(TEXT("Video.Upscaler")))
	{
		CommitValue(*Upscaler, TEXT("Auto"), true);
	}
	RefreshValuesFromTargets();
	BroadcastChanges(Before);
	SaveSettings();
}

// =====================================================================================================
//  Editing, applying, confirming, saving
// =====================================================================================================

void UMenuSettingsSubsystem::BeginEditing(bool bApplyButton)
{
	if (EditingDepth++ > 0)
	{
		return;
	}
	bApplyButtonPolicy = bApplyButton;
	RefreshValuesFromTargets();
	Snapshot = Values;
	MachineSnapshot = MachineSave ? MachineSave->Values : TMap<FName, FString>();
	UserSnapshot = UserSave ? UserSave->Values : TMap<FName, FString>();
	KeySnapshot.Reset();
}

void UMenuSettingsSubsystem::EndEditing(bool bApplyPending)
{
	if (EditingDepth == 0 || --EditingDepth > 0)
	{
		return;
	}
	if (bApplyPending)
	{
		ApplySettings();
	}
	else if (Pending.Num() > 0)
	{
		const TMap<FName, FString> Dropped = MoveTemp(Pending);
		Pending.Reset();
		for (const TPair<FName, FString>& Pair : Dropped)
		{
			OnSettingChanged.Broadcast(Pair.Key, GetSettingValue(Pair.Key));
			SettingChangedNative.Broadcast(Pair.Key, GetSettingValue(Pair.Key));
		}
	}
	SaveSettings();
	Snapshot.Reset();
	MachineSnapshot.Reset();
	UserSnapshot.Reset();
	KeySnapshot.Reset();
	bApplyButtonPolicy = false;
}

bool UMenuSettingsSubsystem::HasChangesSinceEditingBegan() const
{
	if (Pending.Num() > 0 || KeySnapshot.Num() > 0)
	{
		return true;
	}
	for (const TPair<FName, FString>& Pair : Snapshot)
	{
		if (Values.FindRef(Pair.Key) != Pair.Value)
		{
			return true;
		}
	}
	return false;
}

void UMenuSettingsSubsystem::ApplySettings()
{
	TMap<FName, FString> Revert;
	const TMap<FName, FString> Waiting = MoveTemp(Pending);
	Pending.Reset();
	for (const TPair<FName, FString>& Pair : Waiting)
	{
		if (const UMenuSetting* Setting = FindSetting(Pair.Key))
		{
			if (Setting->bRequiresConfirmation)
			{
				Revert.Add(Pair.Key, Values.FindRef(Pair.Key));
			}
			CommitValue(*Setting, Pair.Value, true);
		}
	}
	if (Revert.Num() > 0)
	{
		StartConfirmation(Revert);
	}
	SaveSettings();
	OnSettingsApplied.Broadcast();
}

void UMenuSettingsSubsystem::RevertSettings()
{
	if (IsAwaitingConfirmation())
	{
		EndConfirmation(false);
	}
	const TMap<FName, FString> Dropped = MoveTemp(Pending);
	Pending.Reset();
	for (const TPair<FName, FString>& Pair : Dropped)
	{
		OnSettingChanged.Broadcast(Pair.Key, GetSettingValue(Pair.Key));
		SettingChangedNative.Broadcast(Pair.Key, GetSettingValue(Pair.Key));
	}
	for (const TPair<FName, FString>& Pair : Snapshot)
	{
		const UMenuSetting* Setting = FindSetting(Pair.Key);
		if (Setting && Values.FindRef(Pair.Key) != Pair.Value)
		{
			CommitValue(*Setting, Pair.Value, false);
		}
	}
	if (MachineSave)
	{
		MachineSave->Values = MachineSnapshot;
		bMachineDirty = true;
	}
	if (UserSave)
	{
		UserSave->Values = UserSnapshot;
		bUserDirty = true;
	}
	if (KeySnapshot.Num() > 0)
	{
		const TMap<TPair<FName, uint8>, FKey> Keys = MoveTemp(KeySnapshot);
		KeySnapshot.Reset();
		for (const TPair<TPair<FName, uint8>, FKey>& Pair : Keys)
		{
			MapKeyRaw(Pair.Key.Key, Pair.Key.Value, Pair.Value);
		}
		bKeyBindingsDirty = true;
		OnKeyBindingsChanged.Broadcast();
		KeyBindingsChangedNative.Broadcast();
	}
	SaveSettings();
}

void UMenuSettingsSubsystem::SaveSettings()
{
	const UMenuProjectSettings& Settings = UMenuProjectSettings::Get();
	if (bMachineDirty)
	{
		WriteSave(MachineSave, Settings.MachineSaveSlot, 0);
		bMachineDirty = false;
	}
	if (bUserDirty)
	{
		WriteSave(UserSave, Settings.UserSaveSlot, GetUserIndex());
		bUserDirty = false;
	}
	const FMenuSettingContext Context = MakeContext();
	for (const TPair<UClass*, TWeakObjectPtr<const UMenuSettingTarget>>& Pair : DirtyTargets)
	{
		if (const UMenuSettingTarget* Target = Pair.Value.Get())
		{
			Target->Save(Context);
		}
	}
	DirtyTargets.Reset();
	if (bKeyBindingsDirty)
	{
		if (UEnhancedInputUserSettings* InputSettings = GetInputUserSettings())
		{
			InputSettings->SaveSettings();
		}
		bKeyBindingsDirty = false;
	}
}

void UMenuSettingsSubsystem::StartConfirmation(const TMap<FName, FString>& RevertValues)
{
	// A second display change while one waits keeps the first one's way back.
	for (const TPair<FName, FString>& Pair : RevertValues)
	{
		if (!ConfirmRevert.Contains(Pair.Key))
		{
			ConfirmRevert.Add(Pair.Key, Pair.Value);
		}
	}
	ConfirmTimeLeft = FMath::Max(1.f, UMenuProjectSettings::Get().ConfirmationSeconds);
	if (!ConfirmTicker.IsValid())
	{
		ConfirmTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UMenuSettingsSubsystem::TickConfirmation));
	}
	OnConfirmationStarted.Broadcast(ConfirmTimeLeft);
}

bool UMenuSettingsSubsystem::TickConfirmation(float DeltaTime)
{
	// Real time, capped so a long hitch (the mode change itself) doesn't eat the countdown.
	ConfirmTimeLeft -= FMath::Min(DeltaTime, 0.1f);
	if (ConfirmTimeLeft <= 0.f)
	{
		ConfirmTicker.Reset();
		EndConfirmation(false);
		return false;
	}
	return true;
}

void UMenuSettingsSubsystem::ConfirmSettings(bool bKeep)
{
	if (IsAwaitingConfirmation())
	{
		EndConfirmation(bKeep);
	}
}

void UMenuSettingsSubsystem::EndConfirmation(bool bKept)
{
	ConfirmTimeLeft = 0.f;
	if (ConfirmTicker.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(ConfirmTicker);
		ConfirmTicker.Reset();
	}
	const TMap<FName, FString> Revert = MoveTemp(ConfirmRevert);
	ConfirmRevert.Reset();
	if (!bKept)
	{
		for (const TPair<FName, FString>& Pair : Revert)
		{
			if (const UMenuSetting* Setting = FindSetting(Pair.Key))
			{
				CommitValue(*Setting, Pair.Value, true);
			}
		}
	}
	SaveSettings();
	OnConfirmationEnded.Broadcast(bKept);
}

// =====================================================================================================
//  Key bindings
// =====================================================================================================

UEnhancedInputUserSettings* UMenuSettingsSubsystem::GetInputUserSettings() const
{
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	const UEnhancedInputLocalPlayerSubsystem* Input = LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	return Input ? Input->GetUserSettings() : nullptr;
}

bool UMenuSettingsSubsystem::CanRebindKeys() const
{
	return GetInputUserSettings() != nullptr;
}

bool UMenuSettingsSubsystem::FindMappingSlot(FName MappingName, EMenuKeyBindingDevice Device, int32 SlotIndex, uint8& OutSlot, FKey& OutKey, FKey& OutDefaultKey) const
{
	const UEnhancedInputUserSettings* InputSettings = GetInputUserSettings();
	const UEnhancedPlayerMappableKeyProfile* Profile = InputSettings ? InputSettings->GetActiveKeyProfile() : nullptr;
	const FKeyMappingRow* Row = Profile ? Profile->GetPlayerMappingRows().Find(MappingName) : nullptr;
	if (!Row)
	{
		return false;
	}
	// This device's mappings by slot order: the first is primary, the next secondary.
	TArray<const FPlayerKeyMapping*> ForDevice;
	for (const FPlayerKeyMapping& Mapping : Row->Mappings)
	{
		const FKey Key = Mapping.GetDefaultKey().IsValid() ? Mapping.GetDefaultKey() : Mapping.GetCurrentKey();
		if (MenuSettingsSubsystemLocal::MatchesDevice(Key, Device) || (!Key.IsValid() && MenuSettingsSubsystemLocal::MatchesDevice(Mapping.GetCurrentKey(), Device)))
		{
			ForDevice.Add(&Mapping);
		}
	}
	ForDevice.Sort([](const FPlayerKeyMapping& A, const FPlayerKeyMapping& B) { return static_cast<uint8>(A.GetSlot()) < static_cast<uint8>(B.GetSlot()); });
	if (!ForDevice.IsValidIndex(SlotIndex))
	{
		return false;
	}
	OutSlot = static_cast<uint8>(ForDevice[SlotIndex]->GetSlot());
	OutKey = ForDevice[SlotIndex]->GetCurrentKey();
	OutDefaultKey = ForDevice[SlotIndex]->GetDefaultKey();
	return true;
}

uint8 UMenuSettingsSubsystem::FindFreeSlot(FName MappingName) const
{
	const UEnhancedInputUserSettings* InputSettings = GetInputUserSettings();
	const UEnhancedPlayerMappableKeyProfile* Profile = InputSettings ? InputSettings->GetActiveKeyProfile() : nullptr;
	const FKeyMappingRow* Row = Profile ? Profile->GetPlayerMappingRows().Find(MappingName) : nullptr;
	TSet<uint8> Used;
	if (Row)
	{
		for (const FPlayerKeyMapping& Mapping : Row->Mappings)
		{
			Used.Add(static_cast<uint8>(Mapping.GetSlot()));
		}
	}
	for (uint8 Candidate = static_cast<uint8>(EPlayerMappableKeySlot::First); Candidate < static_cast<uint8>(EPlayerMappableKeySlot::Unspecified); ++Candidate)
	{
		if (!Used.Contains(Candidate))
		{
			return Candidate;
		}
	}
	return static_cast<uint8>(EPlayerMappableKeySlot::Second);
}

void UMenuSettingsSubsystem::RememberKey(FName MappingName, uint8 SlotValue)
{
	if (EditingDepth == 0)
	{
		return;
	}
	const TPair<FName, uint8> Entry(MappingName, SlotValue);
	if (KeySnapshot.Contains(Entry))
	{
		return;
	}
	FKey Current;
	const UEnhancedInputUserSettings* InputSettings = GetInputUserSettings();
	const UEnhancedPlayerMappableKeyProfile* Profile = InputSettings ? InputSettings->GetActiveKeyProfile() : nullptr;
	if (const FKeyMappingRow* Row = Profile ? Profile->GetPlayerMappingRows().Find(MappingName) : nullptr)
	{
		for (const FPlayerKeyMapping& Mapping : Row->Mappings)
		{
			if (static_cast<uint8>(Mapping.GetSlot()) == SlotValue)
			{
				Current = Mapping.GetCurrentKey();
			}
		}
	}
	KeySnapshot.Add(Entry, Current);
}

bool UMenuSettingsSubsystem::MapKeyRaw(FName MappingName, uint8 SlotValue, const FKey& Key)
{
	UEnhancedInputUserSettings* InputSettings = GetInputUserSettings();
	if (!InputSettings)
	{
		return false;
	}
	FMapPlayerKeyArgs Args;
	Args.MappingName = MappingName;
	Args.Slot = static_cast<EPlayerMappableKeySlot>(SlotValue);
	Args.NewKey = Key;
	Args.bCreateMatchingSlotIfNeeded = true;
	FGameplayTagContainer Failure;
	if (Key.IsValid())
	{
		InputSettings->MapPlayerKey(Args, Failure);
	}
	else
	{
		// Clearing a slot: an invalid key where Enhanced Input takes one, else its unmap.
		InputSettings->MapPlayerKey(Args, Failure);
		if (!Failure.IsEmpty())
		{
			Failure.Reset();
			InputSettings->UnMapPlayerKey(Args, Failure);
		}
	}
	if (!Failure.IsEmpty())
	{
		UE_LOG(LogNameTBDSettings, Warning, TEXT("Couldn't bind %s to %s: %s"), *Key.ToString(), *MappingName.ToString(), *Failure.ToStringSimple());
		return false;
	}
	bKeyBindingsDirty = true;
	return true;
}

FKey UMenuSettingsSubsystem::GetBoundKey(FName MappingName, EMenuKeyBindingDevice Device, int32 SlotIndex) const
{
	uint8 SlotValue = 0;
	FKey Key;
	FKey DefaultKey;
	return FindMappingSlot(MappingName, Device, SlotIndex, SlotValue, Key, DefaultKey) ? Key : EKeys::Invalid;
}

FKey UMenuSettingsSubsystem::GetDefaultBoundKey(FName MappingName, EMenuKeyBindingDevice Device, int32 SlotIndex) const
{
	uint8 SlotValue = 0;
	FKey Key;
	FKey DefaultKey;
	return FindMappingSlot(MappingName, Device, SlotIndex, SlotValue, Key, DefaultKey) ? DefaultKey : EKeys::Invalid;
}

FName UMenuSettingsSubsystem::FindKeyConflict(FName MappingName, FKey Key) const
{
	const UEnhancedInputUserSettings* InputSettings = GetInputUserSettings();
	const UEnhancedPlayerMappableKeyProfile* Profile = InputSettings ? InputSettings->GetActiveKeyProfile() : nullptr;
	if (!Profile || !Key.IsValid())
	{
		return NAME_None;
	}
	for (const TPair<FName, FKeyMappingRow>& Pair : Profile->GetPlayerMappingRows())
	{
		if (Pair.Key == MappingName)
		{
			continue;
		}
		for (const FPlayerKeyMapping& Mapping : Pair.Value.Mappings)
		{
			if (Mapping.GetCurrentKey() == Key)
			{
				return Pair.Key;
			}
		}
	}
	return NAME_None;
}

FText UMenuSettingsSubsystem::GetKeyBindingDisplayName(FName MappingName) const
{
	const UEnhancedInputUserSettings* InputSettings = GetInputUserSettings();
	const UEnhancedPlayerMappableKeyProfile* Profile = InputSettings ? InputSettings->GetActiveKeyProfile() : nullptr;
	if (const FKeyMappingRow* Row = Profile ? Profile->GetPlayerMappingRows().Find(MappingName) : nullptr)
	{
		for (const FPlayerKeyMapping& Mapping : Row->Mappings)
		{
			if (!Mapping.GetDisplayName().IsEmpty())
			{
				return Mapping.GetDisplayName();
			}
		}
	}
	return FText::FromName(MappingName);
}

bool UMenuSettingsSubsystem::BindKey(FName MappingName, EMenuKeyBindingDevice Device, int32 SlotIndex, FKey Key, FText& Message)
{
	const UMenuProjectSettings& Settings = UMenuProjectSettings::Get();
	if (!CanRebindKeys())
	{
		Message = LOCTEXT("CantRebind", "Keys can't be rebound in this game.");
		return false;
	}
	if (!Key.IsValid())
	{
		return false;
	}
	if (Settings.ReservedKeys.Contains(Key))
	{
		Message = FText::Format(LOCTEXT("Reserved", "{0} is reserved and can't be bound."), Key.GetDisplayName(false));
		return false;
	}
	if (!MenuSettingsSubsystemLocal::MatchesDevice(Key, Device))
	{
		Message = Device == EMenuKeyBindingDevice::Gamepad ? LOCTEXT("WantPad", "Press a controller button.") : LOCTEXT("WantKey", "Press a key or mouse button.");
		return false;
	}

	uint8 SlotValue = 0;
	FKey OldKey;
	FKey DefaultKey;
	if (!FindMappingSlot(MappingName, Device, SlotIndex, SlotValue, OldKey, DefaultKey))
	{
		SlotValue = FindFreeSlot(MappingName);
	}
	if (OldKey == Key)
	{
		return true;
	}

	const FName Conflict = FindKeyConflict(MappingName, Key);
	Message = FText::GetEmpty();
	if (!Conflict.IsNone())
	{
		const FText Other = GetKeyBindingDisplayName(Conflict);
		switch (Settings.KeyConflictPolicy)
		{
		case EMenuKeyConflictPolicy::Refuse:
			Message = FText::Format(LOCTEXT("Refused", "{0} is already used by {1}."), Key.GetDisplayName(false), Other);
			return false;
		case EMenuKeyConflictPolicy::Swap:
		case EMenuKeyConflictPolicy::Unbind:
		{
			// The other mapping's slot holding the key takes this one's old key (Swap) or none (Unbind).
			for (int32 OtherIndex = 0; OtherIndex < 4; ++OtherIndex)
			{
				uint8 OtherSlot = 0;
				FKey OtherKey;
				FKey OtherDefault;
				if (FindMappingSlot(Conflict, Device, OtherIndex, OtherSlot, OtherKey, OtherDefault) && OtherKey == Key)
				{
					const FKey Given = Settings.KeyConflictPolicy == EMenuKeyConflictPolicy::Swap ? OldKey : FKey();
					RememberKey(Conflict, OtherSlot);
					MapKeyRaw(Conflict, OtherSlot, Given);
					Message = Given.IsValid()
						? FText::Format(LOCTEXT("Swapped", "{0} now uses {1}."), Other, Given.GetDisplayName(false))
						: FText::Format(LOCTEXT("Unbound", "{0} is now unbound."), Other);
					break;
				}
			}
			break;
		}
		default:
			break;
		}
	}

	RememberKey(MappingName, SlotValue);
	const bool bBound = MapKeyRaw(MappingName, SlotValue, Key);
	OnKeyBindingsChanged.Broadcast();
	KeyBindingsChangedNative.Broadcast();
	return bBound;
}

void UMenuSettingsSubsystem::UnbindKey(FName MappingName, EMenuKeyBindingDevice Device, int32 SlotIndex)
{
	uint8 SlotValue = 0;
	FKey Key;
	FKey DefaultKey;
	if (FindMappingSlot(MappingName, Device, SlotIndex, SlotValue, Key, DefaultKey) && Key.IsValid())
	{
		RememberKey(MappingName, SlotValue);
		MapKeyRaw(MappingName, SlotValue, FKey());
		OnKeyBindingsChanged.Broadcast();
		KeyBindingsChangedNative.Broadcast();
	}
}

void UMenuSettingsSubsystem::ResetKeyBinding(FName MappingName)
{
	UEnhancedInputUserSettings* InputSettings = GetInputUserSettings();
	if (!InputSettings)
	{
		return;
	}
	const UEnhancedPlayerMappableKeyProfile* Profile = InputSettings->GetActiveKeyProfile();
	if (const FKeyMappingRow* Row = Profile ? Profile->GetPlayerMappingRows().Find(MappingName) : nullptr)
	{
		for (const FPlayerKeyMapping& Mapping : Row->Mappings)
		{
			RememberKey(MappingName, static_cast<uint8>(Mapping.GetSlot()));
		}
	}
	FMapPlayerKeyArgs Args;
	Args.MappingName = MappingName;
	FGameplayTagContainer Failure;
	InputSettings->ResetAllPlayerKeysInRow(Args, Failure);
	bKeyBindingsDirty = true;
	OnKeyBindingsChanged.Broadcast();
	KeyBindingsChangedNative.Broadcast();
}

void UMenuSettingsSubsystem::ResetAllKeyBindings()
{
	const UEnhancedInputUserSettings* InputSettings = GetInputUserSettings();
	const UEnhancedPlayerMappableKeyProfile* Profile = InputSettings ? InputSettings->GetActiveKeyProfile() : nullptr;
	if (!Profile)
	{
		return;
	}
	TArray<FName> Names;
	Profile->GetPlayerMappingRows().GenerateKeyArray(Names);
	for (const FName& Name : Names)
	{
		ResetKeyBinding(Name);
	}
}

// =====================================================================================================
//  Benchmark, overlay, notifications
// =====================================================================================================

void UMenuSettingsSubsystem::StartBenchmark(float Seconds, bool bNotify)
{
	if (Benchmark.IsValid())
	{
		return;
	}
	bNotifyBenchmark = bNotify;
	if (bNotify)
	{
		PostNotification(LOCTEXT("BenchmarkStarted", "Benchmark running"),
			FText::Format(LOCTEXT("BenchmarkStartedBody", "Measuring for {0} seconds..."), FText::AsNumber(FMath::RoundToInt(Seconds))));
	}
	Benchmark = FMenuBenchmarkRun::Start(Seconds, 1.f, true, FMenuBenchmarkRun::FOnFinished::CreateUObject(this, &UMenuSettingsSubsystem::HandleBenchmarkFinished));
}

void UMenuSettingsSubsystem::HandleBenchmarkFinished(const FMenuBenchmarkResult& Result)
{
	const FMenuBenchmarkResult Copy = Result;
	Benchmark.Reset();
	if (bNotifyBenchmark)
	{
		PostNotification(LOCTEXT("BenchmarkDone", "Benchmark results"), Copy.ToText());
	}
	OnBenchmarkFinished.Broadcast(Copy);
}

void UMenuSettingsSubsystem::PostNotification(const FText& Title, const FText& Body) const
{
	UNotificationSubsystem* Notifications = GetLocalPlayer() ? GetLocalPlayer()->GetSubsystem<UNotificationSubsystem>() : nullptr;
	if (!Notifications)
	{
		UE_LOG(LogNameTBDSettings, Display, TEXT("%s: %s"), *Title.ToString(), *Body.ToString());
		return;
	}
	FNotificationRequest Request;
	Request.Type = NotificationTags::Type_Info;
	Request.Title = Title;
	Request.Body = Body;
	Notifications->PostNotification(Request);
}

void UMenuSettingsSubsystem::UpdatePerformanceOverlay()
{
	const FString Mode = Values.Contains(MenuSettingsSubsystemLocal::PerformanceOverlayId) ? GetSettingValue(MenuSettingsSubsystemLocal::PerformanceOverlayId) : FString(TEXT("Off"));
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	UGameViewportClient* Viewport = LocalPlayer ? LocalPlayer->ViewportClient.Get() : nullptr;
	if (Mode == PerformanceOverlayMode && (PerformanceOverlaySlate.IsValid() || Mode.Equals(TEXT("Off"), ESearchCase::IgnoreCase)))
	{
		return;
	}
	if (PerformanceOverlaySlate.IsValid() && Viewport)
	{
		Viewport->RemoveViewportWidgetForPlayer(LocalPlayer, PerformanceOverlaySlate.ToSharedRef());
	}
	PerformanceOverlaySlate.Reset();
	PerformanceOverlayMode = Mode;
	if (!Viewport || Mode.IsEmpty() || Mode.Equals(TEXT("Off"), ESearchCase::IgnoreCase))
	{
		return;
	}

	if (!PerformanceOverlay)
	{
		PerformanceOverlay = NewObject<UStatGraph>(this);
	}
	const EStatGraphPreset Preset = Mode.Equals(TEXT("FPS"), ESearchCase::IgnoreCase) ? EStatGraphPreset::CompactFPS
		: Mode.Equals(TEXT("FrameTimes"), ESearchCase::IgnoreCase) ? EStatGraphPreset::FrameTimes : EStatGraphPreset::Detailed;
	PerformanceOverlay->ApplyPreset(Preset);

	EHorizontalAlignment Horizontal = HAlign_Right;
	EVerticalAlignment Vertical = VAlign_Top;
	switch (UMenuProjectSettings::Get().PerformanceOverlayCorner)
	{
	case EMenuOverlayCorner::TopLeft: Horizontal = HAlign_Left; break;
	case EMenuOverlayCorner::BottomLeft: Horizontal = HAlign_Left; Vertical = VAlign_Bottom; break;
	case EMenuOverlayCorner::BottomRight: Vertical = VAlign_Bottom; break;
	default: break;
	}
	PerformanceOverlaySlate = SNew(SBox)
		.HAlign(Horizontal)
		.VAlign(Vertical)
		.Padding(FMargin(16.f))
		.Visibility(EVisibility::HitTestInvisible)
		[
			PerformanceOverlay->TakeWidget()
		];
	Viewport->AddViewportWidgetForPlayer(LocalPlayer, PerformanceOverlaySlate.ToSharedRef(), 10000);
}

// =====================================================================================================
//  Console
// =====================================================================================================

namespace MenuSettingsConsole
{
	UMenuSettingsSubsystem* FindFor(UWorld* World)
	{
		UWorld* Playing = MenuPlayerUtils::FindPlayingWorld(World);
		UMenuSettingsSubsystem* Settings = Playing ? UMenuSettingsSubsystem::Get(Playing) : nullptr;
		if (!Settings)
		{
			UE_LOG(LogNameTBDSettings, Warning, TEXT("NameTBD.Settings: no local player - run the command while playing (PIE or a game)."));
		}
		return Settings;
	}

	FAutoConsoleCommandWithWorldAndArgs List(
		TEXT("NameTBD.Settings.List"),
		TEXT("Lists every setting with its value, default, and whether it is shown and enabled. Optional: a filter (\"Video\")."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UMenuSettingsSubsystem* Settings = FindFor(World);
			if (!Settings)
			{
				return;
			}
			const FString Filter = Args.Num() > 0 ? Args[0] : FString();
			for (const FMenuSettingsPage& Page : Settings->GetPages())
			{
				for (const FMenuSettingsSection& Section : Page.Sections)
				{
					for (const UMenuSetting* Setting : Section.Settings)
					{
						if (!Setting || (!Filter.IsEmpty() && !Setting->Id.ToString().Contains(Filter)))
						{
							continue;
						}
						FText Reason;
						const bool bEnabled = Settings->IsSettingEnabled(Setting->Id, Reason);
						UE_LOG(LogNameTBDSettings, Display, TEXT("%-36s = %-20s (default %s)%s%s%s"), *Setting->Id.ToString(), *Settings->GetSettingValue(Setting->Id),
							*Settings->GetSettingDefault(Setting->Id), Settings->IsSettingShown(Setting->Id) ? TEXT("") : TEXT(" [hidden]"),
							bEnabled ? TEXT("") : TEXT(" [disabled: "), bEnabled ? TEXT("") : *(Reason.ToString() + TEXT("]")));
					}
				}
			}
			MenuPlayerUtils::Report(TEXT("NameTBD.Settings.List: written to the log."));
		}));

	FAutoConsoleCommandWithWorldAndArgs Set(
		TEXT("NameTBD.Settings.Set"),
		TEXT("NameTBD.Settings.Set <Id> <Value>: changes a setting (applied and saved)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UMenuSettingsSubsystem* Settings = FindFor(World);
			if (!Settings || Args.Num() < 2)
			{
				MenuPlayerUtils::Report(TEXT("Usage: NameTBD.Settings.Set <Id> <Value>"));
				return;
			}
			const FName Id(*Args[0]);
			const bool bSet = Settings->SetSettingValue(Id, FString::Join(TArrayView<const FString>(Args).Slice(1, Args.Num() - 1), TEXT(" ")));
			Settings->SaveSettings();
			MenuPlayerUtils::Report(FString::Printf(TEXT("%s %s = %s"), bSet ? TEXT("Set") : TEXT("Couldn't set"), *Id.ToString(), *Settings->GetSettingValue(Id)));
		}));

	FAutoConsoleCommandWithWorldAndArgs Reset(
		TEXT("NameTBD.Settings.Reset"),
		TEXT("NameTBD.Settings.Reset [Id]: puts one setting (or every setting and key binding) back to its default."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UMenuSettingsSubsystem* Settings = FindFor(World))
			{
				if (Args.Num() > 0)
				{
					Settings->ResetSetting(FName(*Args[0]));
				}
				else
				{
					Settings->ResetAllSettings(true);
				}
				Settings->SaveSettings();
				MenuPlayerUtils::Report(Args.Num() > 0 ? FString::Printf(TEXT("Reset %s."), *Args[0]) : FString(TEXT("Reset every setting.")));
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs Graphics(
		TEXT("NameTBD.Settings.Graphics"),
		TEXT("Lists the graphics features (DLSS, FSR, XeSS, TSR, frame generation, Reflex...): installed, supported here, active."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UE_LOG(LogNameTBDSettings, Display, TEXT("GPU: %s (vendor %s)"), *UMenuGraphicsLibrary::GetGPUName(), *MenuSettingsSubsystemLocal::GpuVendorName());
			for (const FMenuGraphicsFeatureStatus& Status : UMenuGraphicsLibrary::GetGraphicsFeatures())
			{
				UE_LOG(LogNameTBDSettings, Display, TEXT("%-10s installed %-3s supported %-3s %s%s"), *Status.Id.ToString(), Status.bInstalled ? TEXT("yes") : TEXT("no"),
					Status.bSupported ? TEXT("yes") : TEXT("no"), UMenuGraphicsLibrary::GetActiveGraphicsFeature(Status.Kind) == Status.Id ? TEXT("[active] ") : TEXT(""), *Status.Reason.ToString());
			}
			MenuPlayerUtils::Report(FString::Printf(TEXT("Graphics features written to the log. Auto picks: %s"), *UMenuGraphicsLibrary::GetRecommendedUpscaler().ToString()));
		}));

	FAutoConsoleCommandWithWorldAndArgs Bench(
		TEXT("NameTBD.Benchmark"),
		TEXT("NameTBD.Benchmark [seconds]: measures frame rate and frame times (default 15 s) and reports them (log, notification, CSV in Saved/Benchmarks)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UMenuSettingsSubsystem* Settings = FindFor(World))
			{
				const float Seconds = Args.Num() > 0 ? FMath::Max(1.f, FCString::Atof(*Args[0])) : 15.f;
				Settings->StartBenchmark(Seconds, true);
				MenuPlayerUtils::Report(FString::Printf(TEXT("Benchmark: measuring for %.0f s."), Seconds));
			}
		}));
}

#undef LOCTEXT_NAMESPACE
