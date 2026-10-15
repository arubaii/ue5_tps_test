// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "GameFramework/SaveGame.h"
#include "InputCoreTypes.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "NameTBD/Menu/Graph/MenuBenchmark.h"
#include "MenuSetting.h"
#include "MenuSettingsSchema.h"
#include "MenuSettingTypes.h"

#include "MenuSettingsSubsystem.generated.h"

class APawn;
class APlayerController;
class UEnhancedInputUserSettings;
class UMenuSettingTarget;
class SWidget;
class UStatGraph;
class UWorld;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMenuSettingChanged, FName, SettingId, const FString&, Value);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMenuSettingAction, FName, SettingId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMenuSettingsConfirmationStarted, float, Seconds);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMenuSettingsConfirmationEnded, bool, bKept);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMenuSettingsEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMenuBenchmarkFinished, const FMenuBenchmarkResult&, Result);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnMenuSettingChangedNative, FName /*SettingId*/, const FString& /*Value*/);

/** What the plugin saves: setting values by Id. One for the machine, one per player. */
UCLASS()
class NAMETBD_API UMenuSettingsSaveGame : public USaveGame
{
	GENERATED_BODY()

public:

	UPROPERTY()
	int32 Version = 1;

	UPROPERTY()
	TMap<FName, FString> Values;
};

/**
 * Each local player's settings (split screen: one each): the merged settings pages, the values, applying them to their targets,
 * Apply / Revert with a confirmation countdown for display changes, saving, key bindings, and the performance overlay.
 *
 * Values: every setting that holds one has a current value. A setting whose target can be read (a cvar, Game User Settings)
 * shows what the engine has; one the player changed is saved and written again on every start; one never touched is never
 * written (the engine's scalability and ini decide). Changes made while Begin Editing's Apply Button policy is on wait for Apply
 * Settings (Get Setting Value already shows them).
 *
 * From Blueprint: Get Menu Settings (library) or Get Local Player Subsystem; from C++ UMenuSettingsSubsystem::Get.
 */
UCLASS(DisplayName = "Menu Settings Subsystem")
class NAMETBD_API UMenuSettingsSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:

	/** The settings of Player (or the player owning WorldContext, else the first local player). Null on a dedicated server. */
	static UMenuSettingsSubsystem* Get(const UObject* WorldContext, const APlayerController* Player = nullptr);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// -------------------------------------------------------------------------------------------------
	// Definitions
	// -------------------------------------------------------------------------------------------------

	/** The merged pages, in order. */
	const TArray<FMenuSettingsPage>& GetPages() const { return Pages; }

	UFUNCTION(BlueprintPure, Category = "Menu|Settings")
	TArray<FName> GetPageIds() const;

	UFUNCTION(BlueprintPure, Category = "Menu|Settings")
	bool GetPage(FName PageId, FMenuSettingsPage& OutPage) const;

	UFUNCTION(BlueprintPure, Category = "Menu|Settings")
	UMenuSetting* FindSetting(FName SettingId) const;

	/** The page a setting is on (None when on none). */
	FName FindPageOf(FName SettingId) const;

	/** Adds a schema's pages and settings (merged over what is there, by its Priority). A Settings Menu's Extra Schemas come here. */
	UFUNCTION(BlueprintCallable, Category = "Menu|Settings")
	void RegisterSchema(UMenuSettingsSchema* Schema);

	UFUNCTION(BlueprintCallable, Category = "Menu|Settings")
	void UnregisterSchema(UMenuSettingsSchema* Schema);

	/** Lists the key bindings again (after registering more mapping contexts). */
	UFUNCTION(BlueprintCallable, Category = "Menu|Settings")
	void RefreshKeyBindings();

	/** Fired when the pages were rebuilt (a schema registered, key bindings refreshed). */
	UPROPERTY(BlueprintAssignable, Category = "Menu|Settings")
	FOnMenuSettingsEvent OnPagesChanged;

	FSimpleMulticastDelegate& OnPagesChangedNative() { return PagesChangedNative; }

	// -------------------------------------------------------------------------------------------------
	// Values
	// -------------------------------------------------------------------------------------------------

	/** The value, as text (a change waiting for Apply included). Empty for no such setting. */
	UFUNCTION(BlueprintPure, Category = "Menu|Settings")
	FString GetSettingValue(FName SettingId) const;

	UFUNCTION(BlueprintPure, Category = "Menu|Settings")
	bool GetSettingBool(FName SettingId, bool bDefault = false) const;

	UFUNCTION(BlueprintPure, Category = "Menu|Settings")
	float GetSettingNumber(FName SettingId, float DefaultValue = 0.f) const;

	/** The value as the player reads it ("On", "75%", "High"). */
	UFUNCTION(BlueprintPure, Category = "Menu|Settings")
	FText GetSettingDisplayValue(FName SettingId) const;

	UFUNCTION(BlueprintPure, Category = "Menu|Settings")
	FString GetSettingDefault(FName SettingId) const;

	/** Its value isn't its default. */
	UFUNCTION(BlueprintPure, Category = "Menu|Settings")
	bool IsSettingModified(FName SettingId) const;

	/** The Choice's options as offered now. */
	UFUNCTION(BlueprintPure, Category = "Menu|Settings")
	TArray<FMenuSettingOption> GetSettingOptions(FName SettingId) const;

	/** Changes a setting (made valid first). Applied at once, or waiting for Apply (see Begin Editing). False: no such setting, or it can't change now. */
	UFUNCTION(BlueprintCallable, Category = "Menu|Settings")
	bool SetSettingValue(FName SettingId, const FString& Value);

	UFUNCTION(BlueprintCallable, Category = "Menu|Settings")
	bool SetSettingBool(FName SettingId, bool bValue);

	UFUNCTION(BlueprintCallable, Category = "Menu|Settings")
	bool SetSettingNumber(FName SettingId, float Value);

	/** Moves a setting one step (left / right on a pad): Direction -1 or 1. */
	UFUNCTION(BlueprintCallable, Category = "Menu|Settings")
	bool StepSetting(FName SettingId, int32 Direction);

	UFUNCTION(BlueprintCallable, Category = "Menu|Settings")
	void ResetSetting(FName SettingId);

	/** Every setting of a page to its default (and its key bindings). */
	UFUNCTION(BlueprintCallable, Category = "Menu|Settings")
	void ResetPage(FName PageId);

	UFUNCTION(BlueprintCallable, Category = "Menu|Settings")
	void ResetAllSettings(bool bIncludeKeyBindings = true);

	/** Listed now: available here (or greyed out when unavailable), on this platform, its Shown When conditions hold. */
	UFUNCTION(BlueprintPure, Category = "Menu|Settings")
	bool IsSettingShown(FName SettingId) const;

	/** All of these conditions hold now (a section's Shown When). Conditions can test "@ActiveUpscaler", "@GPUVendor", "@InputDevice", "@Platform", "@RayTracingSupported". */
	bool TestConditions(const TArray<FMenuSettingCondition>& Conditions) const { return AreConditionsMet(Conditions); }

	/** Can be changed now; Reason says why not. */
	UFUNCTION(BlueprintPure, Category = "Menu|Settings")
	bool IsSettingEnabled(FName SettingId, FText& Reason) const;

	/** Does what an Action setting does (an Event action only fires On Setting Action). */
	UFUNCTION(BlueprintCallable, Category = "Menu|Settings")
	void RunSettingAction(FName SettingId);

	/** Every setting's value (for the game's own save, or the cloud). */
	UFUNCTION(BlueprintCallable, Category = "Menu|Settings")
	TArray<FMenuSettingValue> GetAllSettingValues() const;

	/** Sets many at once (a restored cloud save, a preset), applied and saved. */
	UFUNCTION(BlueprintCallable, Category = "Menu|Settings")
	void SetAllSettingValues(const TArray<FMenuSettingValue>& InValues);

	UPROPERTY(BlueprintAssignable, Category = "Menu|Settings")
	FOnMenuSettingChanged OnSettingChanged;

	FOnMenuSettingChangedNative& OnSettingChangedNative() { return SettingChangedNative; }

	/** An Event action was run (also fired for the built-in actions, after they ran). */
	UPROPERTY(BlueprintAssignable, Category = "Menu|Settings")
	FOnMenuSettingAction OnSettingAction;

	// -------------------------------------------------------------------------------------------------
	// Editing: Apply, Revert, confirmation, saving
	// -------------------------------------------------------------------------------------------------

	/**
	 * A settings screen opened: what Revert goes back to is taken now. bApplyButton: settings with Apply Mode Default wait for
	 * Apply Settings (else they apply as they change). Calls nest; the outermost End Editing saves.
	 */
	UFUNCTION(BlueprintCallable, Category = "Menu|Settings")
	void BeginEditing(bool bApplyButton = false);

	/** The screen closed: changes still waiting are dropped unless bApplyPending, and what changed is saved. */
	UFUNCTION(BlueprintCallable, Category = "Menu|Settings")
	void EndEditing(bool bApplyPending = false);

	UFUNCTION(BlueprintPure, Category = "Menu|Settings")
	bool IsEditing() const { return EditingDepth > 0; }

	/** Changes waiting for Apply. */
	UFUNCTION(BlueprintPure, Category = "Menu|Settings")
	bool HasPendingChanges() const { return Pending.Num() > 0; }

	/** Anything differs from when editing began (applied or waiting). */
	UFUNCTION(BlueprintPure, Category = "Menu|Settings")
	bool HasChangesSinceEditingBegan() const;

	/** Applies what waits, asks for confirmation when a display change is among it, and saves. */
	UFUNCTION(BlueprintCallable, Category = "Menu|Settings")
	void ApplySettings();

	/** Everything back to how it was when editing began (waiting changes dropped). */
	UFUNCTION(BlueprintCallable, Category = "Menu|Settings")
	void RevertSettings();

	/** Writes the save files (and the engine's Game User Settings, and the key bindings). */
	UFUNCTION(BlueprintCallable, Category = "Menu|Settings")
	void SaveSettings();

	UFUNCTION(BlueprintPure, Category = "Menu|Settings")
	bool IsAwaitingConfirmation() const { return ConfirmTimeLeft > 0.f; }

	UFUNCTION(BlueprintPure, Category = "Menu|Settings")
	float GetConfirmationTimeLeft() const { return FMath::Max(0.f, ConfirmTimeLeft); }

	/** Answers the confirmation: keep the display change, or put it back. */
	UFUNCTION(BlueprintCallable, Category = "Menu|Settings")
	void ConfirmSettings(bool bKeep);

	/** A setting needing a restart was changed since the game started. */
	UFUNCTION(BlueprintPure, Category = "Menu|Settings")
	bool IsRestartRequired() const { return RestartSettings.Num() > 0; }

	UPROPERTY(BlueprintAssignable, Category = "Menu|Settings")
	FOnMenuSettingsEvent OnSettingsApplied;

	UPROPERTY(BlueprintAssignable, Category = "Menu|Settings")
	FOnMenuSettingsConfirmationStarted OnConfirmationStarted;

	UPROPERTY(BlueprintAssignable, Category = "Menu|Settings")
	FOnMenuSettingsConfirmationEnded OnConfirmationEnded;

	// -------------------------------------------------------------------------------------------------
	// Key bindings (Enhanced Input user settings)
	// -------------------------------------------------------------------------------------------------

	/** Enhanced Input's user settings are on, so keys can be rebound. */
	UFUNCTION(BlueprintPure, Category = "Menu|Settings|Keys")
	bool CanRebindKeys() const;

	/** The key in a binding's slot (0 primary, 1 secondary) for a device. None when unbound. */
	UFUNCTION(BlueprintPure, Category = "Menu|Settings|Keys")
	FKey GetBoundKey(FName MappingName, EMenuKeyBindingDevice Device, int32 SlotIndex) const;

	/** Its default key there. */
	UFUNCTION(BlueprintPure, Category = "Menu|Settings|Keys")
	FKey GetDefaultBoundKey(FName MappingName, EMenuKeyBindingDevice Device, int32 SlotIndex) const;

	/**
	 * Binds Key there, by the project's Key Conflict Policy. False (with Message) when it isn't bound: a reserved key, the wrong
	 * device, refused for a conflict. Message also says what a swap did.
	 */
	UFUNCTION(BlueprintCallable, Category = "Menu|Settings|Keys")
	bool BindKey(FName MappingName, EMenuKeyBindingDevice Device, int32 SlotIndex, FKey Key, FText& Message);

	UFUNCTION(BlueprintCallable, Category = "Menu|Settings|Keys")
	void UnbindKey(FName MappingName, EMenuKeyBindingDevice Device, int32 SlotIndex);

	UFUNCTION(BlueprintCallable, Category = "Menu|Settings|Keys")
	void ResetKeyBinding(FName MappingName);

	UFUNCTION(BlueprintCallable, Category = "Menu|Settings|Keys")
	void ResetAllKeyBindings();

	/** Another mapping using Key on the same device (None when free). */
	UFUNCTION(BlueprintPure, Category = "Menu|Settings|Keys")
	FName FindKeyConflict(FName MappingName, FKey Key) const;

	/** A mapping's name as the player reads it. */
	UFUNCTION(BlueprintPure, Category = "Menu|Settings|Keys")
	FText GetKeyBindingDisplayName(FName MappingName) const;

	/** Fired when a key binding changed. */
	UPROPERTY(BlueprintAssignable, Category = "Menu|Settings|Keys")
	FOnMenuSettingsEvent OnKeyBindingsChanged;

	FSimpleMulticastDelegate& OnKeyBindingsChangedNative() { return KeyBindingsChangedNative; }

	// -------------------------------------------------------------------------------------------------
	// Benchmark and overlay
	// -------------------------------------------------------------------------------------------------

	/** Measures performance for Seconds; On Benchmark Finished reports it (and a notification, with Notify). */
	UFUNCTION(BlueprintCallable, Category = "Menu|Settings")
	void StartBenchmark(float Seconds = 15.f, bool bNotify = true);

	UFUNCTION(BlueprintPure, Category = "Menu|Settings")
	bool IsBenchmarkRunning() const { return Benchmark.IsValid(); }

	UPROPERTY(BlueprintAssignable, Category = "Menu|Settings")
	FOnMenuBenchmarkFinished OnBenchmarkFinished;

	/** What setting targets and formatting see for this player. */
	FMenuSettingContext MakeContext() const;

private:

	/** Builds Pages from the schemas, Settings By Id, and the key binding rows. Keeps values. */
	void RebuildPages();
	void BuildKeyBindingSettings();
	void RegisterKeyBindingContexts();

	/** Current values from the saves (or what the targets read, or defaults). */
	void LoadValues();

	/** On start: writes every saved value to its target (and the defaults targets want written). */
	void ApplySavedValues();
	bool ShouldWriteOnStart(const UMenuSetting& Setting) const;

	void RefreshValuesFromTargets();

	/** Writes Value to the setting's target now, records it, and follows up (reapply list, restart, events). */
	void CommitValue(const UMenuSetting& Setting, const FString& Value, bool bFromPlayer);
	void CommitInner(const UMenuSetting& Setting, const FString& Value, bool bFromPlayer, TSet<FName>& Visited);

	/** OnSettingChanged for every value that differs from Before. */
	void BroadcastChanges(const TMap<FName, FString>& Before);

	/** Resolved apply mode for a change made now. */
	bool ShouldWaitForApply(const UMenuSetting& Setting) const;

	bool AreConditionsMet(const TArray<FMenuSettingCondition>& Conditions) const;

	/** A setting's value, or one of the system values conditions can test ("@ActiveUpscaler", "@GPUVendor", "@InputDevice"...). */
	FString GetConditionValue(FName SettingId) const;

	void StartConfirmation(const TMap<FName, FString>& RevertValues);
	bool TickConfirmation(float DeltaTime);
	void EndConfirmation(bool bKept);

	/** Records Value (or its removal) in the save its scope goes to. */
	void StoreValue(const UMenuSetting& Setting, const FString* Value);
	UMenuSettingsSaveGame* LoadSave(const FString& Slot, int32 UserIndex) const;
	void WriteSave(UMenuSettingsSaveGame* Save, const FString& Slot, int32 UserIndex) const;
	int32 GetUserIndex() const;
	const FString* FindSavedValue(const UMenuSetting& Setting) const;

	/** Watches for the player's controller arriving or changing (map loads, PIE start). */
	bool TickPlayerWatch(float DeltaTime);
	void HandlePlayerReady(APlayerController* Controller);

	UFUNCTION()
	void HandlePawnChanged(APawn* OldPawn, APawn* NewPawn);

	void ReapplyAfterMapOrPawn();
	void UpdatePerformanceOverlay();
	void PostNotification(const FText& Title, const FText& Body) const;
	void HandleBenchmarkFinished(const FMenuBenchmarkResult& Result);
	void RunAutoDetect();

	// Key binding plumbing.
	UEnhancedInputUserSettings* GetInputUserSettings() const;
	bool FindMappingSlot(FName MappingName, EMenuKeyBindingDevice Device, int32 SlotIndex, uint8& OutSlot, FKey& OutKey, FKey& OutDefaultKey) const;
	uint8 FindFreeSlot(FName MappingName) const;
	bool MapKeyRaw(FName MappingName, uint8 SlotValue, const FKey& Key);
	void RememberKey(FName MappingName, uint8 SlotValue);

	UPROPERTY(Transient)
	TArray<FMenuSettingsPage> Pages;

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UMenuSetting>> SettingsById;

	UPROPERTY(Transient)
	TObjectPtr<UMenuSettingsSchema> BuiltInSchema;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMenuSettingsSchema>> RuntimeSchemas;

	/** Key binding rows made for the sections that list them. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMenuSetting>> GeneratedSettings;

	UPROPERTY(Transient)
	TObjectPtr<UMenuSettingsSaveGame> MachineSave;

	UPROPERTY(Transient)
	TObjectPtr<UMenuSettingsSaveGame> UserSave;

	UPROPERTY(Transient)
	TObjectPtr<UStatGraph> PerformanceOverlay;

	TSharedPtr<SWidget> PerformanceOverlaySlate;
	FString PerformanceOverlayMode;

	/** Current (applied) values of every setting that holds one. */
	TMap<FName, FString> Values;

	/** Changes waiting for Apply. */
	TMap<FName, FString> Pending;

	/** When editing began: the values, the saves, and the keys (mapping + slot) changed since. */
	TMap<FName, FString> Snapshot;
	TMap<FName, FString> MachineSnapshot;
	TMap<FName, FString> UserSnapshot;
	TMap<TPair<FName, uint8>, FKey> KeySnapshot;

	/** What a running confirmation goes back to. */
	TMap<FName, FString> ConfirmRevert;
	float ConfirmTimeLeft = 0.f;
	FTSTicker::FDelegateHandle ConfirmTicker;

	TSet<FName> RestartSettings;

	/** One target of each kind written since the last save (their engine saves: Game User Settings). */
	TMap<UClass*, TWeakObjectPtr<const UMenuSettingTarget>> DirtyTargets;

	int32 EditingDepth = 0;
	bool bApplyButtonPolicy = false;
	bool bMachineDirty = false;
	bool bUserDirty = false;
	bool bKeyBindingsDirty = false;
	bool bNotifyBenchmark = true;
	bool bForceSyncSave = false;

	TWeakObjectPtr<APlayerController> WatchedController;
	FTSTicker::FDelegateHandle PlayerWatchTicker;
	FDelegateHandle SchemaChangedHandle;

	TSharedPtr<FMenuBenchmarkRun> Benchmark;
	FOnMenuSettingChangedNative SettingChangedNative;
	FSimpleMulticastDelegate PagesChangedNative;
	FSimpleMulticastDelegate KeyBindingsChangedNative;
};
