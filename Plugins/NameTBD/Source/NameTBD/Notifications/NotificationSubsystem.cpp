// Copyright AR GameSystems 2026. All rights reserved.

#include "NotificationSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Components/ActorComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Stats/Stats.h"
#include "Widgets/Layout/SBox.h"
#include "NotificationHost.h"
#include "NotificationSettings.h"

#define LOCTEXT_NAMESPACE "NotificationSubsystem"

int32 UNotificationSubsystem::NextId = 1;

/**
 * Watches every key for the action shortcuts (whatever has focus - the game, a menu), and which device the player used
 * last, for the key hints. Takes a key only when it answered a notification.
 */
class FNotificationInputProcessor : public IInputProcessor
{
public:
	explicit FNotificationInputProcessor(UNotificationSubsystem* InOwner) : Owner(InOwner) {}

	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}

	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
	{
		UNotificationSubsystem* Subsystem = Owner.Get();
		return Subsystem && Subsystem->HandleKey(InKeyEvent.GetKey(), static_cast<int32>(InKeyEvent.GetUserIndex()), true, InKeyEvent.IsRepeat());
	}

	virtual bool HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
	{
		UNotificationSubsystem* Subsystem = Owner.Get();
		return Subsystem && Subsystem->HandleKey(InKeyEvent.GetKey(), static_cast<int32>(InKeyEvent.GetUserIndex()), false, false);
	}

	virtual bool HandleAnalogInputEvent(FSlateApplication& SlateApp, const FAnalogInputEvent& InAnalogInputEvent) override
	{
		if (UNotificationSubsystem* Subsystem = Owner.Get())
		{
			if (InAnalogInputEvent.GetKey().IsGamepadKey() && FMath::Abs(InAnalogInputEvent.GetAnalogValue()) > 0.35f)
			{
				Subsystem->NoteInputDevice(true);
			}
		}
		return false;
	}

	virtual bool HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
	{
		if (UNotificationSubsystem* Subsystem = Owner.Get())
		{
			if (!MouseEvent.IsTouchEvent() && MouseEvent.GetCursorDelta().SizeSquared() > 4.0)
			{
				Subsystem->NoteInputDevice(false);
			}
		}
		return false;
	}

	virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
	{
		if (UNotificationSubsystem* Subsystem = Owner.Get())
		{
			Subsystem->NoteInputDevice(false);
		}
		return false;
	}

	virtual const TCHAR* GetDebugName() const override { return TEXT("NameTBDNotificationInput"); }

private:
	TWeakObjectPtr<UNotificationSubsystem> Owner;
};

namespace NotificationSubsystemPrivate
{
	/** Seconds a notification may take to play out before it is taken as gone (its host stopped ticking). */
	constexpr double MaxLeaveSeconds = 5.0;

	/** Seconds the screen reader is given per word before the next announcement. */
	constexpr float AnnounceSecondsPerWord = 0.4f;

	bool HasKeyedAction(const FNotificationRecord& Record)
	{
		for (const FNotificationAction& Action : Record.Request.Actions)
		{
			if (Action.GamepadKey.IsValid() || Action.KeyboardKey.IsValid())
			{
				return true;
			}
		}
		return false;
	}

	/** Higher priority first; then newer (bNewest) or older first. */
	bool IsBefore(const FNotificationRecord& A, const FNotificationRecord& B, bool bByPriority, bool bNewest)
	{
		const bool bCriticalA = A.Priority == ENotificationPriority::Critical;
		const bool bCriticalB = B.Priority == ENotificationPriority::Critical;
		if (bCriticalA != bCriticalB)
		{
			return bCriticalA;
		}
		if (bByPriority && A.Priority != B.Priority)
		{
			return A.Priority > B.Priority;
		}
		return bNewest ? A.Sequence > B.Sequence : A.Sequence < B.Sequence;
	}
}

// =====================================================================================================
// FNotificationRecord
// =====================================================================================================

FNotificationView FNotificationRecord::MakeView() const
{
	FNotificationView View;
	View.Handle = FNotificationHandle(Id);
	View.Type = Request.Type;
	View.Channel = Channel;
	View.Priority = Priority;
	View.Severity = Severity;
	View.Title = NotificationUtils::FormatText(Request.Title, Request);
	View.Body = NotificationUtils::FormatText(Request.Body, Request);
	View.Icon = Request.Icon.HasImage() ? Request.Icon : TypeSettings.Icon;
	View.Count = Request.Count;
	View.Value = Request.Value;
	View.Progress = Request.Progress;
	View.Actions = Request.Actions;
	View.Payload = Request.Payload;
	View.ContextObject = Request.ContextObject;
	View.Duration = Duration;
	View.bCanDismiss = TypeSettings.bAllowDismiss;
	View.PostedAt = PostedAt;
	View.DismissReason = LeaveReason;
	View.RespondedAction = RespondedAction;
	return View;
}

// =====================================================================================================
// Lifetime
// =====================================================================================================

UNotificationSubsystem* UNotificationSubsystem::Get(const UObject* WorldContext, const APlayerController* Player)
{
	const ULocalPlayer* LocalPlayer = Player ? Player->GetLocalPlayer() : nullptr;

	if (!LocalPlayer && WorldContext)
	{
		const AActor* Actor = Cast<AActor>(WorldContext);
		if (const UActorComponent* Component = Cast<UActorComponent>(WorldContext))
		{
			Actor = Component->GetOwner();
		}

		if (const ULocalPlayer* AsLocalPlayer = Cast<ULocalPlayer>(WorldContext))
		{
			LocalPlayer = AsLocalPlayer;
		}
		else if (const UUserWidget* AsUserWidget = Cast<UUserWidget>(WorldContext))
		{
			LocalPlayer = AsUserWidget->GetOwningLocalPlayer();
		}
		else if (const UWidget* AsWidget = Cast<UWidget>(WorldContext))
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

	if (!LocalPlayer && WorldContext && GEngine)
	{
		if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull))
		{
			LocalPlayer = World->GetFirstLocalPlayerFromController();
			if (!LocalPlayer && World->GetGameInstance())
			{
				LocalPlayer = World->GetGameInstance()->GetFirstGamePlayer();
			}
		}
	}
	return LocalPlayer ? const_cast<ULocalPlayer*>(LocalPlayer)->GetSubsystem<UNotificationSubsystem>() : nullptr;
}

void UNotificationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const UNotificationSettings& Settings = UNotificationSettings::Get();
	Preferences = Settings.DefaultPreferences;

	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UNotificationSubsystem::Tick), 0.f);

	if (FSlateApplication::IsInitialized())
	{
		InputProcessor = MakeShared<FNotificationInputProcessor>(this);
		FSlateApplication::Get().RegisterInputPreProcessor(InputProcessor);
	}

	// Every type's own entry widget, loaded now: the first notification of a type then shows without a hitch.
	TArray<FSoftObjectPath> Classes;
	for (const TPair<FGameplayTag, FNotificationTypeSettings>& Pair : Settings.Types)
	{
		if (!Pair.Value.EntryWidgetClass.IsNull())
		{
			Classes.AddUnique(Pair.Value.EntryWidgetClass.ToSoftObjectPath());
		}
	}
	if (!Settings.DefaultType.EntryWidgetClass.IsNull())
	{
		Classes.AddUnique(Settings.DefaultType.EntryWidgetClass.ToSoftObjectPath());
	}
	if (Classes.Num() > 0)
	{
		PreloadHandle = Streamable.RequestAsyncLoad(Classes);
	}
}

void UNotificationSubsystem::Deinitialize()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
	if (InputProcessor.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(InputProcessor);
	}
	InputProcessor.Reset();

	// Whoever waits on one hears that it went.
	const TArray<TSharedRef<FNotificationRecord>> Remaining = Records;
	Records.Reset();
	for (const TSharedRef<FNotificationRecord>& Record : Remaining)
	{
		for (const FOnNotificationFinishedNative& Callback : Record->FinishedCallbacks)
		{
			Callback.ExecuteIfBound(FNotificationHandle(Record->Id), Record->RespondedAction, ENotificationDismissReason::Cleared);
		}
	}

	if (UWorld* World = GetPlayerWorld())
	{
		for (TPair<FGameplayTag, FUISoundRuntime>& Pair : SoundRuntimes)
		{
			FUISoundPlayer::Cancel(World, Pair.Value);
		}
	}
	SoundRuntimes.Reset();
	if (PreloadHandle.IsValid())
	{
		PreloadHandle->CancelHandle();
		PreloadHandle.Reset();
	}
	RemoveDefaultHosts(/*bAll=*/true);
	Hosts.Reset();
	Super::Deinitialize();
}

void UNotificationSubsystem::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(InThis, Collector);
	UNotificationSubsystem* This = CastChecked<UNotificationSubsystem>(InThis);
	for (const TSharedRef<FNotificationRecord>& Record : This->Records)
	{
		Collector.AddPropertyReferencesWithStructARO(FNotificationRequest::StaticStruct(), &Record->Request, This);
	}
}

UWorld* UNotificationSubsystem::GetPlayerWorld() const
{
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	return LocalPlayer ? LocalPlayer->GetWorld() : nullptr;
}

APlayerController* UNotificationSubsystem::GetPlayerController() const
{
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	return LocalPlayer ? LocalPlayer->GetPlayerController(GetPlayerWorld()) : nullptr;
}

int32 UNotificationSubsystem::GetSlateUserIndex() const
{
	if (const ULocalPlayer* LocalPlayer = GetLocalPlayer())
	{
		if (const TSharedPtr<const FSlateUser> User = LocalPlayer->GetSlateUser())
		{
			return User->GetUserIndex();
		}
	}
	return -1;
}

// =====================================================================================================
// Posting
// =====================================================================================================

void UNotificationSubsystem::ResolveRecord(FNotificationRecord& Record) const
{
	const UNotificationSettings& Settings = UNotificationSettings::Get();
	const FNotificationRequest& Request = Record.Request;
	Record.TypeSettings = Settings.FindType(Request.Type);

	if (Request.bOverrideChannel && Request.Channel.IsValid())
	{
		Record.Channel = Request.Channel;
	}
	else
	{
		Record.Channel = Record.TypeSettings.Channel.IsValid() ? Record.TypeSettings.Channel : Settings.DefaultChannel;
	}
	Record.Priority = Request.bOverridePriority ? Request.Priority : Record.TypeSettings.Priority;
	Record.Severity = Request.bOverrideSeverity ? Request.Severity : Record.TypeSettings.Severity;

	// Unless the poster names the key:
	// - Merge / Replace: the type and the title as written - "+{Value} XP" is one key for every XP post, "Objective Updated"
	//   replaces the last one whatever its body says.
	// - Always New / Ignore (and Cooldown): the type and the whole text as shown, title and body with their arguments - only a
	//   true repeat is dropped ("Saved / Slot 1" and "Saved / Slot 2" are two notifications). Hashed (case kept) to keep the
	//   name short.
	if (!Request.StackKey.IsNone())
	{
		Record.StackKey = Request.StackKey;
	}
	else if (Record.TypeSettings.StackPolicy == ENotificationStackPolicy::Merge || Record.TypeSettings.StackPolicy == ENotificationStackPolicy::Replace)
	{
		Record.StackKey = FName(*FString::Printf(TEXT("%s|%s"), *Request.Type.ToString(), *Request.Title.ToString().Left(160)));
	}
	else
	{
		const FString Shown = NotificationUtils::FormatText(Request.Title, Request).ToString() + TEXT("\n") + NotificationUtils::FormatText(Request.Body, Request).ToString();
		Record.StackKey = FName(*FString::Printf(TEXT("%s|%08x|%d"), *Request.Type.ToString(), FCrc::StrCrc32(*Shown), Shown.Len()));
	}
}

float UNotificationSubsystem::ResolveDuration(const FNotificationRecord& Record) const
{
	if (Preferences.bKeepUntilDismissed)
	{
		return 0.f;
	}
	const UNotificationSettings& Settings = UNotificationSettings::Get();
	float Seconds = Record.Request.bOverrideDuration ? Record.Request.Duration : Record.TypeSettings.Duration;
	if (Seconds <= 0.f)
	{
		return 0.f;
	}
	if (!Record.Request.bOverrideDuration && Record.TypeSettings.bAddReadingTime)
	{
		const FNotificationView View = Record.MakeView();
		const int32 Words = NotificationUtils::CountWords(View.Title.ToString() + TEXT(" ") + View.Body.ToString());
		Seconds += static_cast<float>(Words) * 60.f / FMath::Max(Settings.ReadingWordsPerMinute, 60.f);
	}
	Seconds = FMath::Max(Seconds, Settings.MinimumDuration);
	if (Record.Request.Actions.Num() > 0)
	{
		Seconds = FMath::Max(Seconds, Settings.MinimumActionDuration);
	}
	return Seconds * FMath::Clamp(Preferences.DurationScale, 0.25f, 10.f);
}

FNotificationHandle UNotificationSubsystem::PostNotification(const FNotificationRequest& Request)
{
	return PostNotificationWithCallbacks(Request, FOnNotificationActionNative(), FOnNotificationFinishedNative());
}

FNotificationHandle UNotificationSubsystem::PostNotificationWithCallbacks(const FNotificationRequest& Request, FOnNotificationActionNative OnAction, FOnNotificationFinishedNative OnFinished)
{
	const double Now = FPlatformTime::Seconds();
	TSharedRef<FNotificationRecord> NewRecord = MakeShared<FNotificationRecord>();
	FNotificationRecord& Record = *NewRecord;
	Record.Request = Request;
	Record.Request.Count = FMath::Max(Record.Request.Count, 0);
	if (OnAction.IsBound())
	{
		Record.ActionCallbacks.Add(MoveTemp(OnAction));
	}
	if (OnFinished.IsBound())
	{
		Record.FinishedCallbacks.Add(MoveTemp(OnFinished));
	}
	ResolveRecord(Record);
	const FNotificationTypeSettings& Type = Record.TypeSettings;

	// Throttled: one like it a moment ago (merging types are never throttled - they add up instead).
	if (Type.Cooldown > 0.f && Type.StackPolicy != ENotificationStackPolicy::Merge)
	{
		if (const double* Last = LastPostTimes.Find(Record.StackKey))
		{
			if (Now - *Last < Type.Cooldown)
			{
				UE_LOG(LogNameTBDNotifications, Verbose, TEXT("Throttled %s (cooldown %.2fs)"), *Record.StackKey.ToString(), Type.Cooldown);
				for (const FOnNotificationFinishedNative& Callback : Record.FinishedCallbacks)
				{
					Callback.ExecuteIfBound(FNotificationHandle(), NAME_None, ENotificationDismissReason::Throttled);
				}
				return FNotificationHandle();
			}
		}
	}
	if (LastPostTimes.Num() > 256)
	{
		for (auto It = LastPostTimes.CreateIterator(); It; ++It)
		{
			if (Now - It.Value() > 60.0)
			{
				It.RemoveCurrent();
			}
		}
	}
	LastPostTimes.Add(Record.StackKey, Now);

	// Into one like it.
	if (Type.StackPolicy != ENotificationStackPolicy::None)
	{
		if (const TSharedPtr<FNotificationRecord> Target = FindStackTarget(Record))
		{
			const FNotificationHandle TargetHandle(Target->Id);
			if (Type.StackPolicy == ENotificationStackPolicy::Ignore)
			{
				for (const FOnNotificationFinishedNative& Callback : Record.FinishedCallbacks)
				{
					Callback.ExecuteIfBound(TargetHandle, NAME_None, ENotificationDismissReason::Throttled);
				}
				return TargetHandle;
			}

			FNotificationRequest& Into = Target->Request;
			if (Type.StackPolicy == ENotificationStackPolicy::Merge)
			{
				Into.Count += Request.Count;
				Into.Value += Request.Value;
				if (!Request.Title.IsEmpty()) { Into.Title = Request.Title; }
				if (!Request.Body.IsEmpty()) { Into.Body = Request.Body; }
				if (Request.TextArgs.Num() > 0) { Into.TextArgs = Request.TextArgs; }
				if (Request.Icon.HasImage()) { Into.Icon = Request.Icon; }
				if (Request.Progress >= 0.f) { Into.Progress = Request.Progress; }
				if (Request.Actions.Num() > 0) { Into.Actions = Request.Actions; }
				if (Request.Payload.IsValid()) { Into.Payload = Request.Payload; }
				if (Request.ContextObject) { Into.ContextObject = Request.ContextObject; }
			}
			else
			{
				Into = Request;
			}
			Target->ActionCallbacks.Append(Record.ActionCallbacks);
			Target->FinishedCallbacks.Append(Record.FinishedCallbacks);
			Target->StackTime = Now;
			if (Target->State == ENotificationState::Pending)
			{
				Target->QueuedSince = Now;
			}
			else if (Type.bRefreshOnStack && Target->State == ENotificationState::Visible)
			{
				Target->Duration = ResolveDuration(*Target);
				Target->Remaining = Target->Duration;
			}
			if (Target->State == ENotificationState::Visible)
			{
				PushViewToHost(*Target, /*bStacked=*/true);
				PlaySound(*Target, Type.StackedSound);
			}
			OnNotificationUpdated.Broadcast(Target->MakeView());
			return TargetHandle;
		}
	}

	Record.Id = NextId++;
	if (NextId <= 0)
	{
		NextId = 1;
	}
	Record.PostTime = Now;
	Record.StackTime = Now;
	Record.QueuedSince = Now;
	Record.PostedAt = FDateTime::UtcNow();
	Record.Sequence = NextSequence++;
	{
		const UWorld* PostWorld = GetPlayerWorld();
		Record.bPostedWhilePaused = PostWorld && PostWorld->IsPaused();
	}

	// This one takes others away ("Double Kill" takes "Kill").
	if (!Type.CancelsTypes.IsEmpty())
	{
		const TArray<TSharedRef<FNotificationRecord>> Snapshot = Records;
		for (const TSharedRef<FNotificationRecord>& Other : Snapshot)
		{
			if (Other->State != ENotificationState::Leaving && Other->Request.Type.MatchesAny(Type.CancelsTypes))
			{
				BeginDismiss(Other->Id, ENotificationDismissReason::Cancelled);
			}
		}
	}

	const FNotificationHandle Handle(Record.Id);
	const bool bIgnoresSuppression = Type.bIgnoreSuppression;
	if (IsBlockedByPlayer(Record) || (!bIgnoresSuppression && IsSuppressed(Record, ENotificationSuppressionMode::Drop)))
	{
		Record.LeaveReason = ENotificationDismissReason::Suppressed;
		OnNotificationPosted.Broadcast(Record.MakeView());
		ReportGone(NewRecord);
		return Handle;
	}

	Records.Add(NewRecord);
	UE_LOG(LogNameTBDNotifications, Verbose, TEXT("Posted %d %s on %s"), Record.Id, *Record.Request.Type.ToString(), *Record.Channel.ToString());
	OnNotificationPosted.Broadcast(Record.MakeView());

	// The posting handler may have dismissed it already.
	if (Records.Contains(NewRecord))
	{
		TrimQueue(NewRecord->Channel);
		if (Records.Contains(NewRecord))
		{
			const UWorld* World = GetPlayerWorld();
			ScheduleChannel(NewRecord->Channel, World && World->IsPaused(), Now);
		}
	}
	return Handle;
}

TSharedPtr<FNotificationRecord> UNotificationSubsystem::FindStackTarget(const FNotificationRecord& Incoming) const
{
	const double Now = FPlatformTime::Seconds();
	const float Window = Incoming.TypeSettings.StackWindow;
	TSharedPtr<FNotificationRecord> Best;
	for (const TSharedRef<FNotificationRecord>& Record : Records)
	{
		if (Record->State == ENotificationState::Leaving || Record->StackKey != Incoming.StackKey || Record->Request.Type != Incoming.Request.Type)
		{
			continue;
		}
		if (Window > 0.f && Now - Record->StackTime > Window)
		{
			continue;
		}
		if (!Best.IsValid() || Record->Sequence > Best->Sequence)
		{
			Best = Record;
		}
	}
	return Best;
}

bool UNotificationSubsystem::UpdateNotification(FNotificationHandle Handle, const FNotificationRequest& Request, bool bRestartTime)
{
	FNotificationRecord* Record = FindRecordMutable(Handle.Id);
	if (!Record || Record->State == ENotificationState::Leaving)
	{
		return false;
	}
	const FGameplayTag OldChannel = Record->Channel;
	const FGameplayTag OldType = Record->Request.Type;
	Record->Request = Request;
	if (!Record->Request.Type.IsValid())
	{
		Record->Request.Type = OldType;
	}
	ResolveRecord(*Record);
	Record->StackTime = FPlatformTime::Seconds();

	if (Record->State == ENotificationState::Visible && Record->Channel != OldChannel)
	{
		// Moved to another channel: it leaves its host and waits for one of the new channel's.
		ReturnToQueue(*Record);
	}
	else if (Record->State == ENotificationState::Visible)
	{
		if (bRestartTime)
		{
			Record->Duration = ResolveDuration(*Record);
			Record->Remaining = Record->Duration;
		}
		PushViewToHost(*Record, false);
	}
	OnNotificationUpdated.Broadcast(Record->MakeView());
	return true;
}

bool UNotificationSubsystem::SetNotificationProgress(FNotificationHandle Handle, float InProgress)
{
	FNotificationRecord* Record = FindRecordMutable(Handle.Id);
	if (!Record || Record->State == ENotificationState::Leaving)
	{
		return false;
	}
	Record->Request.Progress = InProgress < 0.f ? -1.f : FMath::Clamp(InProgress, 0.f, 1.f);
	PushViewToHost(*Record, false);
	OnNotificationUpdated.Broadcast(Record->MakeView());
	return true;
}

bool UNotificationSubsystem::SetNotificationText(FNotificationHandle Handle, const FText& InTitle, const FText& InBody)
{
	FNotificationRecord* Record = FindRecordMutable(Handle.Id);
	if (!Record || Record->State == ENotificationState::Leaving)
	{
		return false;
	}
	if (!InTitle.IsEmpty())
	{
		Record->Request.Title = InTitle;
	}
	if (!InBody.IsEmpty())
	{
		Record->Request.Body = InBody;
	}
	PushViewToHost(*Record, false);
	OnNotificationUpdated.Broadcast(Record->MakeView());
	return true;
}

bool UNotificationSubsystem::DismissNotification(FNotificationHandle Handle)
{
	const FNotificationRecord* Record = FindRecord(Handle.Id);
	if (!Record || Record->State == ENotificationState::Leaving)
	{
		return false;
	}
	BeginDismiss(Handle.Id, ENotificationDismissReason::Code);
	return true;
}

void UNotificationSubsystem::DismissByPlayer(FNotificationHandle Handle)
{
	const FNotificationRecord* Record = FindRecord(Handle.Id);
	if (!Record || Record->State == ENotificationState::Leaving || !Record->TypeSettings.bAllowDismiss)
	{
		return;
	}
	PlaySound(*Record, Record->TypeSettings.DismissedSound);
	BeginDismiss(Handle.Id, ENotificationDismissReason::User);
}

bool UNotificationSubsystem::RespondToNotification(FNotificationHandle Handle, FName ActionId)
{
	return RespondInternal(Handle.Id, ActionId, /*bTimeout=*/false);
}

bool UNotificationSubsystem::RespondInternal(int32 Id, FName ActionId, bool bTimeout)
{
	FNotificationRecord* Record = FindRecordMutable(Id);
	if (!Record || Record->State == ENotificationState::Leaving)
	{
		return false;
	}
	const FNotificationAction* Action = Record->Request.Actions.FindByPredicate([ActionId](const FNotificationAction& Each) { return Each.ActionId == ActionId; });
	const bool bDismisses = bTimeout || !Action || Action->bDismisses;
	Record->RespondedAction = ActionId;
	if (!bTimeout)
	{
		PlaySound(*Record, Record->TypeSettings.ActionSound);
	}

	// Handlers may post, update or dismiss: the record is looked up again afterwards.
	const FNotificationView View = Record->MakeView();
	const TArray<FOnNotificationActionNative> Callbacks = Record->ActionCallbacks;
	OnNotificationAction.Broadcast(View, ActionId);
	for (const FOnNotificationActionNative& Callback : Callbacks)
	{
		Callback.ExecuteIfBound(FNotificationHandle(Id), ActionId);
	}

	if (bDismisses && FindRecord(Id))
	{
		BeginDismiss(Id, bTimeout ? ENotificationDismissReason::TimedOut : ENotificationDismissReason::Responded);
	}
	return true;
}

void UNotificationSubsystem::ClearNotifications(FGameplayTagContainer TypesOrChannels, bool bIncludeQueued)
{
	const TArray<TSharedRef<FNotificationRecord>> Snapshot = Records;
	for (const TSharedRef<FNotificationRecord>& Record : Snapshot)
	{
		if (!MatchesFilter(*Record, TypesOrChannels) || (Record->State == ENotificationState::Pending && !bIncludeQueued))
		{
			continue;
		}
		BeginDismiss(Record->Id, ENotificationDismissReason::Cleared);
	}
}

bool UNotificationSubsystem::IsNotificationActive(FNotificationHandle Handle) const
{
	const FNotificationRecord* Record = FindRecord(Handle.Id);
	return Record && Record->State != ENotificationState::Leaving;
}

bool UNotificationSubsystem::IsNotificationVisible(FNotificationHandle Handle) const
{
	const FNotificationRecord* Record = FindRecord(Handle.Id);
	return Record && Record->State == ENotificationState::Visible;
}

bool UNotificationSubsystem::GetNotification(FNotificationHandle Handle, FNotificationView& OutNotification) const
{
	const FNotificationRecord* Record = FindRecord(Handle.Id);
	if (!Record || Record->State == ENotificationState::Leaving)
	{
		return false;
	}
	OutNotification = Record->MakeView();
	return true;
}

TArray<FNotificationView> UNotificationSubsystem::GetActiveNotifications(FGameplayTagContainer TypesOrChannels) const
{
	TArray<const FNotificationRecord*> Matching;
	for (const TSharedRef<FNotificationRecord>& Record : Records)
	{
		if (Record->State != ENotificationState::Leaving && MatchesFilter(*Record, TypesOrChannels))
		{
			Matching.Add(&Record.Get());
		}
	}
	Matching.Sort([](const FNotificationRecord& A, const FNotificationRecord& B)
	{
		if (A.State != B.State)
		{
			return A.State == ENotificationState::Visible;
		}
		return A.Sequence < B.Sequence;
	});
	TArray<FNotificationView> Out;
	for (const FNotificationRecord* Record : Matching)
	{
		Out.Add(Record->MakeView());
	}
	return Out;
}

int32 UNotificationSubsystem::GetQueuedCount(FGameplayTagContainer InChannels) const
{
	int32 Count = 0;
	for (const TSharedRef<FNotificationRecord>& Record : Records)
	{
		if (Record->State == ENotificationState::Pending && (InChannels.IsEmpty() || Record->Channel.MatchesAny(InChannels)))
		{
			++Count;
		}
	}
	return Count;
}

float UNotificationSubsystem::GetTimeRemaining(FNotificationHandle Handle) const
{
	const FNotificationRecord* Record = FindRecord(Handle.Id);
	return Record && Record->Duration > 0.f ? FMath::Max(Record->Remaining, 0.f) : 0.f;
}

float UNotificationSubsystem::GetTimeRemainingFraction(FNotificationHandle Handle) const
{
	const FNotificationRecord* Record = FindRecord(Handle.Id);
	if (!Record || Record->Duration <= 0.f)
	{
		return Record ? 1.f : 0.f;
	}
	return FMath::Clamp(Record->Remaining / Record->Duration, 0.f, 1.f);
}

const FNotificationRecord* UNotificationSubsystem::FindRecord(int32 Id) const
{
	if (Id == 0)
	{
		return nullptr;
	}
	for (const TSharedRef<FNotificationRecord>& Record : Records)
	{
		if (Record->Id == Id)
		{
			return &Record.Get();
		}
	}
	return nullptr;
}

FNotificationRecord* UNotificationSubsystem::FindRecordMutable(int32 Id)
{
	return const_cast<FNotificationRecord*>(FindRecord(Id));
}

bool UNotificationSubsystem::MatchesFilter(const FNotificationRecord& Record, const FGameplayTagContainer& TypesOrChannels) const
{
	return TypesOrChannels.IsEmpty() || Record.Request.Type.MatchesAny(TypesOrChannels) || Record.Channel.MatchesAny(TypesOrChannels);
}

// =====================================================================================================
// Suppression and preferences
// =====================================================================================================

int32 UNotificationSubsystem::PushNotificationSuppression(ENotificationSuppressionMode Mode, FGameplayTagContainer TypesOrChannels)
{
	FSuppression& Suppression = Suppressions.AddDefaulted_GetRef();
	Suppression.Id = NextSuppressionId++;
	Suppression.Mode = Mode;
	Suppression.Filter = TypesOrChannels;
	return Suppression.Id;
}

void UNotificationSubsystem::PopNotificationSuppression(int32 SuppressionId)
{
	Suppressions.RemoveAll([SuppressionId](const FSuppression& Each) { return Each.Id == SuppressionId; });
}

bool UNotificationSubsystem::IsSuppressed(const FNotificationRecord& Record, ENotificationSuppressionMode Mode) const
{
	for (const FSuppression& Suppression : Suppressions)
	{
		if (Suppression.Mode == Mode && MatchesFilter(Record, Suppression.Filter))
		{
			return true;
		}
	}
	return false;
}

bool UNotificationSubsystem::IsBlockedByPlayer(const FNotificationRecord& Record) const
{
	if (Record.TypeSettings.bIgnoreSuppression)
	{
		return false;
	}
	if (Preferences.bDoNotDisturb)
	{
		return true;
	}
	return Record.TypeSettings.bPlayerCanMute && Record.Request.Type.IsValid() && Record.Request.Type.MatchesAny(Preferences.MutedTypes);
}

bool UNotificationSubsystem::IsCinematic() const
{
	if (!UNotificationSettings::Get().bHoldDuringCinematics)
	{
		return false;
	}
	const APlayerController* Controller = GetPlayerController();
	return Controller && Controller->bCinematicMode;
}

bool UNotificationSubsystem::ShouldHide(const FNotificationRecord& Record) const
{
	if (Record.TypeSettings.bIgnoreSuppression)
	{
		return false;
	}
	return IsCinematic() || IsSuppressed(Record, ENotificationSuppressionMode::Hold);
}

bool UNotificationSubsystem::IsHeld(const FNotificationRecord& Record, bool bGamePaused) const
{
	if (Record.TypeSettings.bIgnoreSuppression)
	{
		return false;
	}
	// A pause doesn't hold new ones back: what is posted while paused comes from the pause menu and belongs on screen.
	return IsCinematic() || IsSuppressed(Record, ENotificationSuppressionMode::Hold) || IsSuppressed(Record, ENotificationSuppressionMode::Freeze);
}

bool UNotificationSubsystem::IsFrozen(const FNotificationRecord& Record, bool bGamePaused) const
{
	if (bGamePaused && Record.TypeSettings.bPauseWhileGamePaused && !Record.bPostedWhilePaused)
	{
		return true;
	}
	if (HeldRecordId == Record.Id)
	{
		return true;
	}
	if (const UNotificationHost* Host = Record.Host.Get())
	{
		if (Host->IsHoldingTime())
		{
			return true;
		}
	}
	return !Record.TypeSettings.bIgnoreSuppression && IsSuppressed(Record, ENotificationSuppressionMode::Freeze);
}

void UNotificationSubsystem::SetNotificationPreferences(const FNotificationUserPreferences& InPreferences)
{
	const float OldScale = FMath::Clamp(Preferences.DurationScale, 0.25f, 10.f);
	const float NewScale = FMath::Clamp(InPreferences.DurationScale, 0.25f, 10.f);
	const bool bKeepChanged = InPreferences.bKeepUntilDismissed != Preferences.bKeepUntilDismissed;
	Preferences = InPreferences;

	// Those already on screen follow the new timing straight away (the rest get it as they show).
	for (const TSharedRef<FNotificationRecord>& Record : Records)
	{
		if (!Record->bShownOnce || Record->State == ENotificationState::Leaving)
		{
			continue;
		}
		if (bKeepChanged)
		{
			Record->Duration = ResolveDuration(*Record);
			Record->Remaining = Record->Duration;
		}
		else if (Record->Duration > 0.f && !FMath::IsNearlyEqual(OldScale, NewScale))
		{
			const float Ratio = NewScale / OldScale;
			Record->Duration *= Ratio;
			Record->Remaining *= Ratio;
		}
		PushViewToHost(*Record, false);
	}
	for (const TWeakObjectPtr<UNotificationHost>& Host : Hosts)
	{
		if (UNotificationHost* Live = Host.Get())
		{
			Live->ApplyPreferences();
		}
	}
}

TArray<FNotificationTypeInfo> UNotificationSubsystem::GetNotificationTypes() const
{
	TArray<FNotificationTypeInfo> Out;
	for (const TPair<FGameplayTag, FNotificationTypeSettings>& Pair : UNotificationSettings::Get().Types)
	{
		if (!Pair.Value.bPlayerCanMute || Pair.Value.bIgnoreSuppression)
		{
			continue;
		}
		FNotificationTypeInfo& Info = Out.AddDefaulted_GetRef();
		Info.Type = Pair.Key;
		Info.DisplayName = Pair.Value.DisplayName.IsEmpty() ? FText::FromName(Pair.Key.GetTagName()) : Pair.Value.DisplayName;
		Info.bMuted = Pair.Key.MatchesAny(Preferences.MutedTypes);
	}
	Out.Sort([](const FNotificationTypeInfo& A, const FNotificationTypeInfo& B) { return A.DisplayName.CompareTo(B.DisplayName) < 0; });
	return Out;
}

void UNotificationSubsystem::SetNotificationTypeMuted(FGameplayTag Type, bool bMuted)
{
	FNotificationUserPreferences Changed = Preferences;
	if (bMuted)
	{
		Changed.MutedTypes.AddTag(Type);
	}
	else
	{
		Changed.MutedTypes.RemoveTag(Type);
	}
	SetNotificationPreferences(Changed);
}

// =====================================================================================================
// History
// =====================================================================================================

void UNotificationSubsystem::AddToHistory(const FNotificationView& View, bool bRead)
{
	const int32 MaxHistory = UNotificationSettings::Get().MaxHistory;
	if (MaxHistory <= 0)
	{
		return;
	}
	FNotificationView Entry = View;
	Entry.bRead = bRead;
	History.Insert(Entry, 0);
	if (History.Num() > MaxHistory)
	{
		History.SetNum(MaxHistory);
	}
	OnHistoryChanged.Broadcast();
	BroadcastUnreadIfChanged();
}

int32 UNotificationSubsystem::GetUnreadCount() const
{
	int32 Count = 0;
	for (const FNotificationView& Entry : History)
	{
		Count += Entry.bRead ? 0 : 1;
	}
	return Count;
}

void UNotificationSubsystem::BroadcastUnreadIfChanged()
{
	const int32 Unread = GetUnreadCount();
	if (Unread != LastBroadcastUnread)
	{
		LastBroadcastUnread = Unread;
		OnUnreadCountChanged.Broadcast(Unread);
	}
}

void UNotificationSubsystem::MarkNotificationsRead()
{
	bool bChanged = false;
	for (FNotificationView& Entry : History)
	{
		bChanged |= !Entry.bRead;
		Entry.bRead = true;
	}
	if (bChanged)
	{
		OnHistoryChanged.Broadcast();
		BroadcastUnreadIfChanged();
	}
}

void UNotificationSubsystem::RemoveFromHistory(FNotificationHandle Handle)
{
	if (History.RemoveAll([Handle](const FNotificationView& Entry) { return Entry.Handle == Handle; }) > 0)
	{
		OnHistoryChanged.Broadcast();
		BroadcastUnreadIfChanged();
	}
}

void UNotificationSubsystem::ClearNotificationHistory()
{
	if (History.Num() > 0)
	{
		History.Reset();
		OnHistoryChanged.Broadcast();
		BroadcastUnreadIfChanged();
	}
}

void UNotificationSubsystem::RestoreNotificationHistory(const TArray<FNotificationView>& SavedHistory)
{
	const int32 MaxHistory = UNotificationSettings::Get().MaxHistory;
	History.Reset();
	for (const FNotificationView& Saved : SavedHistory)
	{
		if (History.Num() >= MaxHistory)
		{
			break;
		}
		// Handles from another session would collide with this one's: each restored entry gets a new one.
		FNotificationView& Entry = History.Add_GetRef(Saved);
		Entry.Handle = FNotificationHandle(NextId++);
		Entry.Actions.Reset();
	}
	OnHistoryChanged.Broadcast();
	BroadcastUnreadIfChanged();
}

// =====================================================================================================
// Hosts
// =====================================================================================================

void UNotificationSubsystem::RegisterHost(UNotificationHost* Host)
{
	if (!Host)
	{
		return;
	}
	Hosts.AddUnique(Host);
	Host->ApplyPreferences();
	if (!Host->bIsDefaultHost && Host->Source == ENotificationHostSource::Live)
	{
		// The game has its own host for these channels: default ones for them step aside (what they show moves over).
		GameHostChannels.AppendTags(Host->Channels);
		RemoveDefaultHosts(/*bAll=*/false, &Host->Channels);
		for (const FGameplayTag& Channel : Host->Channels)
		{
			WarnedNoHost.Remove(Channel);
		}
	}
}

void UNotificationSubsystem::UnregisterHost(UNotificationHost* Host)
{
	Hosts.Remove(Host);
	TArray<int32> Leaving;
	for (const TSharedRef<FNotificationRecord>& Record : Records)
	{
		if (Record->Host.Get() != Host)
		{
			continue;
		}
		if (Record->State == ENotificationState::Visible)
		{
			// Its host went (the HUD rebuilt): it waits, with its time, for a host of its channel.
			Record->State = ENotificationState::Pending;
			Record->Host.Reset();
			Record->QueuedSince = FPlatformTime::Seconds();
		}
		else if (Record->State == ENotificationState::Leaving)
		{
			Leaving.Add(Record->Id);
		}
	}
	for (const int32 Id : Leaving)
	{
		FinishRemoval(Id);
	}
}

void UNotificationSubsystem::NotifyHostEntryGone(UNotificationHost* Host, int32 Id)
{
	FNotificationRecord* Record = FindRecordMutable(Id);
	if (!Record || Record->Host.Get() != Host)
	{
		return;
	}
	if (Record->State == ENotificationState::Leaving)
	{
		FinishRemoval(Id);
	}
	else if (Record->State == ENotificationState::Visible)
	{
		Record->State = ENotificationState::Pending;
		Record->Host.Reset();
		Record->QueuedSince = FPlatformTime::Seconds();
	}
}

void UNotificationSubsystem::NotifyClicked(FNotificationHandle Handle)
{
	if (const FNotificationRecord* Record = FindRecord(Handle.Id))
	{
		OnNotificationClicked.Broadcast(Record->MakeView());
		return;
	}
	if (const FNotificationView* Entry = History.FindByPredicate([Handle](const FNotificationView& Each) { return Each.Handle == Handle; }))
	{
		const FNotificationView Copy = *Entry;
		OnNotificationClicked.Broadcast(Copy);
	}
}

UNotificationHost* UNotificationSubsystem::FindHostFor(const FGameplayTag& Channel) const
{
	UNotificationHost* Best = nullptr;
	for (const TWeakObjectPtr<UNotificationHost>& Weak : Hosts)
	{
		UNotificationHost* Host = Weak.Get();
		if (!Host || !Host->ShowsChannel(Channel) || !Host->IsLive())
		{
			continue;
		}
		if (!Best || Host->HostPriority >= Best->HostPriority)
		{
			Best = Host;
		}
	}
	return Best;
}

void UNotificationSubsystem::HandleNoHost(const FGameplayTag& Channel, double Now)
{
	const UNotificationSettings& Settings = UNotificationSettings::Get();
	const bool bGameHasHost = Channel.MatchesAny(GameHostChannels);
	if (Settings.bCreateDefaultHosts && !bGameHasHost)
	{
		const double WaitingSince = NoHostSince.FindOrAdd(Channel, Now);
		if (Now - WaitingSince >= Settings.DefaultHostDelay)
		{
			NoHostSince.Remove(Channel);
			AddDefaultHost(Channel);
		}
		return;
	}
	if (!WarnedNoHost.Contains(Channel))
	{
		WarnedNoHost.Add(Channel);
		UE_LOG(LogNameTBDNotifications, Warning, TEXT("Notifications on %s are waiting: no Notification Host on screen shows that channel (%s)."),
			*Channel.ToString(), bGameHasHost
				? TEXT("the game's host for it isn't on screen - hidden, collapsed or removed")
				: TEXT("place a Notification Host with it in its Channels, or turn on Create Default Hosts in Project Settings > Plugins > Notifications"));
	}
}

void UNotificationSubsystem::AddDefaultHost(const FGameplayTag& Channel)
{
	using namespace NotificationTags;

	// A default host that a map change took off the screen is replaced, not counted.
	RemoveDefaultHosts(/*bAll=*/false);
	for (const TObjectPtr<UNotificationHost>& Existing : DefaultHosts)
	{
		if (Existing && Existing->ShowsChannel(Channel))
		{
			return;
		}
	}

	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	UGameViewportClient* Viewport = LocalPlayer ? LocalPlayer->ViewportClient : nullptr;
	if (!Viewport)
	{
		return;
	}

	// Where each built-in channel usually goes on a HUD.
	ENotificationHostPreset HostPreset = ENotificationHostPreset::Toasts;
	EHorizontalAlignment Across = HAlign_Right;
	EVerticalAlignment Down = VAlign_Top;
	float HostWidth = 380.f;
	FMargin Margin(32.f, 32.f);
	if (Channel.MatchesTag(Channel_Banner))
	{
		HostPreset = ENotificationHostPreset::Banner;
		Across = HAlign_Center;
		HostWidth = 720.f;
		Margin = FMargin(32.f, 96.f, 32.f, 32.f);
	}
	else if (Channel.MatchesTag(Channel_Feed))
	{
		HostPreset = ENotificationHostPreset::Feed;
		Across = HAlign_Left;
		HostWidth = 360.f;
	}
	else if (Channel.MatchesTag(Channel_Hint))
	{
		HostPreset = ENotificationHostPreset::Hints;
		Across = HAlign_Center;
		Down = VAlign_Bottom;
		HostWidth = 640.f;
		Margin = FMargin(32.f, 32.f, 32.f, 120.f);
	}

	UNotificationHost* Host = NewObject<UNotificationHost>(this, NAME_None, RF_Transient);
	Host->bIsDefaultHost = true;
	Host->SubsystemOverride = this;
	Host->bPreviewInDesigner = false;
	Host->ApplyPreset(HostPreset);
	if (HostPreset == ENotificationHostPreset::Toasts && !Channel.MatchesTag(Channel_Toast))
	{
		Host->Channels = FGameplayTagContainer(Channel);
	}
	// Any host of the game's for the channel wins over it.
	Host->HostPriority = -1000;

	const TSharedRef<SWidget> HostWidget = Host->TakeWidget();
	const TSharedRef<SWidget> Placed = SNew(SBox)
		.HAlign(Across)
		.VAlign(Down)
		.Padding(Margin)
		[
			SNew(SBox)
			.WidthOverride(HostWidth)
			[
				HostWidget
			]
		];
	Viewport->AddViewportWidgetForPlayer(LocalPlayer, Placed, 9000);
	DefaultHosts.Add(Host);
	DefaultHostWidgets.Add(Placed);

	UE_LOG(LogNameTBDNotifications, Display, TEXT("No Notification Host shows %s: added a default one to the player's screen (Create Default Hosts in Project Settings > Plugins > Notifications). Place a Notification Host in your HUD to choose where it goes and how it looks."),
		*Channel.ToString());
}

void UNotificationSubsystem::RemoveDefaultHosts(bool bAll, const FGameplayTagContainer* CoveredChannels)
{
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	UGameViewportClient* Viewport = LocalPlayer ? LocalPlayer->ViewportClient : nullptr;
	for (int32 Index = DefaultHosts.Num() - 1; Index >= 0; --Index)
	{
		UNotificationHost* Host = DefaultHosts[Index];
		bool bRemove = bAll || !Host || !Host->IsOnScreen();
		if (!bRemove && CoveredChannels)
		{
			for (const FGameplayTag& Shown : Host->Channels)
			{
				bRemove |= Shown.MatchesAny(*CoveredChannels);
			}
		}
		if (!bRemove)
		{
			continue;
		}
		if (Viewport && LocalPlayer && DefaultHostWidgets[Index].IsValid())
		{
			Viewport->RemoveViewportWidgetForPlayer(LocalPlayer, DefaultHostWidgets[Index].ToSharedRef());
		}
		if (Host)
		{
			// Unregisters it: what it showed waits for the next host of its channel, with the time it had.
			Host->ReleaseSlateResources(true);
		}
		DefaultHosts.RemoveAt(Index);
		DefaultHostWidgets.RemoveAt(Index);
	}
}

void UNotificationSubsystem::PushViewToHost(const FNotificationRecord& Record, bool bStacked)
{
	if (Record.State != ENotificationState::Visible)
	{
		return;
	}
	if (UNotificationHost* Host = Record.Host.Get())
	{
		Host->UpdateEntry(Record.MakeView(), bStacked);
	}
}

// =====================================================================================================
// Scheduling
// =====================================================================================================

void UNotificationSubsystem::Show(FNotificationRecord& Record, UNotificationHost* Host)
{
	const bool bFirstTime = !Record.bShownOnce;
	Record.State = ENotificationState::Visible;
	Record.Host = Host;
	if (bFirstTime)
	{
		Record.bShownOnce = true;
		Record.Duration = ResolveDuration(Record);
		Record.Remaining = Record.Duration;
	}
	const FNotificationView View = Record.MakeView();
	Host->ShowEntry(View, bFirstTime);
	if (bFirstTime)
	{
		PlaySound(Record, Record.TypeSettings.ShownSound);
		Announce(Record);
		OnNotificationShown.Broadcast(View);
	}
}

void UNotificationSubsystem::ReturnToQueue(FNotificationRecord& Record)
{
	UNotificationHost* Host = Record.Host.Get();
	Record.State = ENotificationState::Pending;
	Record.Host.Reset();
	Record.QueuedSince = FPlatformTime::Seconds();
	LastLeaveTimes.Add(Record.Channel, Record.QueuedSince);
	if (HeldRecordId == Record.Id)
	{
		HeldRecordId = 0;
		HeldFor = 0.f;
	}
	if (Host)
	{
		Host->HideEntry(Record.Id);
	}
}

void UNotificationSubsystem::BeginDismiss(int32 Id, ENotificationDismissReason Reason)
{
	FNotificationRecord* Record = FindRecordMutable(Id);
	if (!Record || Record->State == ENotificationState::Leaving)
	{
		return;
	}
	Record->LeaveReason = Reason;
	if (HeldRecordId == Id)
	{
		HeldRecordId = 0;
		HeldFor = 0.f;
	}

	UNotificationHost* Host = Record->Host.Get();
	if (Record->State == ENotificationState::Visible && Host && Host->HasEntry(Id))
	{
		Record->State = ENotificationState::Leaving;
		Record->LeavingSince = FPlatformTime::Seconds();
		LastLeaveTimes.Add(Record->Channel, Record->LeavingSince);
		Host->HideEntry(Id);
		return;
	}
	FinishRemoval(Id);
}

void UNotificationSubsystem::FinishRemoval(int32 Id)
{
	const int32 Index = Records.IndexOfByPredicate([Id](const TSharedRef<FNotificationRecord>& Record) { return Record->Id == Id; });
	if (Index == INDEX_NONE)
	{
		return;
	}
	const TSharedRef<FNotificationRecord> Gone = Records[Index];
	Records.RemoveAt(Index);
	if (Gone->bShownOnce)
	{
		LastLeaveTimes.Add(Gone->Channel, FPlatformTime::Seconds());
	}
	if (ShortcutTargetId == Id)
	{
		UpdateShortcutTarget();
	}
	ReportGone(Gone);
}

void UNotificationSubsystem::ReportGone(const TSharedRef<FNotificationRecord>& Record)
{
	const FNotificationView View = Record->MakeView();
	const ENotificationDismissReason Reason = Record->LeaveReason;
	UE_LOG(LogNameTBDNotifications, Verbose, TEXT("Gone %d %s (%s)"), Record->Id, *Record->Request.Type.ToString(), *UEnum::GetValueAsString(Reason));
	if (Record->TypeSettings.bKeepInHistory)
	{
		AddToHistory(View, Record->bShownOnce);
	}
	OnNotificationDismissed.Broadcast(View, Reason);
	for (const FOnNotificationFinishedNative& Callback : Record->FinishedCallbacks)
	{
		Callback.ExecuteIfBound(View.Handle, Record->RespondedAction, Reason);
	}
}

void UNotificationSubsystem::TrimQueue(const FGameplayTag& Channel)
{
	const int32 MaxQueued = UNotificationSettings::Get().FindChannel(Channel).MaxQueued;
	if (MaxQueued <= 0)
	{
		return;
	}
	for (;;)
	{
		const FNotificationRecord* Victim = nullptr;
		int32 Waiting = 0;
		for (const TSharedRef<FNotificationRecord>& Record : Records)
		{
			if (Record->State != ENotificationState::Pending || Record->Channel != Channel)
			{
				continue;
			}
			++Waiting;
			// The least important, then the oldest: what is least worth showing late.
			if (!Victim || Record->Priority < Victim->Priority || (Record->Priority == Victim->Priority && Record->Sequence < Victim->Sequence))
			{
				Victim = &Record.Get();
			}
		}
		if (Waiting <= MaxQueued || !Victim)
		{
			return;
		}
		BeginDismiss(Victim->Id, ENotificationDismissReason::Overflow);
	}
}

void UNotificationSubsystem::ScheduleChannel(const FGameplayTag& Channel, bool bGamePaused, double Now)
{
	using namespace NotificationSubsystemPrivate;

	const FNotificationChannelSettings& ChannelSettings = UNotificationSettings::Get().FindChannel(Channel);
	UNotificationHost* Host = FindHostFor(Channel);

	TArray<FNotificationRecord*> Waiting;
	TArray<FNotificationRecord*> OnScreen;
	bool bAnyLeaving = false;
	for (const TSharedRef<FNotificationRecord>& Record : Records)
	{
		if (Record->Channel != Channel)
		{
			continue;
		}
		if (Record->State == ENotificationState::Pending)
		{
			Waiting.Add(&Record.Get());
		}
		else
		{
			OnScreen.Add(&Record.Get());
			bAnyLeaving |= Record->State == ENotificationState::Leaving;
		}
	}
	if (Waiting.Num() == 0)
	{
		return;
	}

	if (Host)
	{
		NoHostSince.Remove(Channel);
	}
	else
	{
		if (ChannelSettings.bWaitForHost)
		{
			HandleNoHost(Channel, Now);
		}
		else
		{
			TArray<int32> Drop;
			for (const FNotificationRecord* Record : Waiting)
			{
				Drop.Add(Record->Id);
			}
			for (const int32 Id : Drop)
			{
				BeginDismiss(Id, ENotificationDismissReason::Expired);
			}
		}
		return;
	}

	TArray<FNotificationRecord*> Candidates = Waiting.FilterByPredicate([this, bGamePaused](const FNotificationRecord* Record) { return !IsHeld(*Record, bGamePaused); });
	if (Candidates.Num() == 0)
	{
		return;
	}
	const bool bByPriority = ChannelSettings.bOrderByPriority;
	Candidates.Sort([bByPriority](const FNotificationRecord& A, const FNotificationRecord& B) { return IsBefore(A, B, bByPriority, false); });

	// A beat between one leaving and the next (one-at-a-time banners), unless something critical waits.
	const bool bCriticalWaiting = Candidates[0]->Priority == ENotificationPriority::Critical;
	if (ChannelSettings.Gap > 0.f && !bCriticalWaiting)
	{
		if (bAnyLeaving)
		{
			return;
		}
		if (const double* LastLeave = LastLeaveTimes.Find(Channel))
		{
			if (Now - *LastLeave < ChannelSettings.Gap)
			{
				return;
			}
		}
	}

	const int32 Capacity = Host->GetCapacity(ChannelSettings);
	int32 Occupied = OnScreen.Num();
	int32 Next = 0;
	while (Next < Candidates.Num() && Occupied < Capacity)
	{
		Show(*Candidates[Next++], Host);
		++Occupied;
		if (ChannelSettings.Gap > 0.f && Capacity == 1)
		{
			break;
		}
	}
	if (Next >= Candidates.Num() || bAnyLeaving)
	{
		// Everything shown, or room already on its way.
		return;
	}

	// Full, and something waits: push the least important one off early, or let the oldest go to keep a feed moving.
	const FNotificationRecord& First = *Candidates[Next];
	FNotificationRecord* Victim = nullptr;
	if (ChannelSettings.bAllowPreemption)
	{
		for (FNotificationRecord* Record : OnScreen)
		{
			if (Record->State != ENotificationState::Visible || Record->Priority >= First.Priority || Record->ShownFor < ChannelSettings.MinDisplayTime)
			{
				continue;
			}
			if (!Victim || Record->Priority < Victim->Priority || (Record->Priority == Victim->Priority && Record->Sequence < Victim->Sequence))
			{
				Victim = Record;
			}
		}
		if (Victim)
		{
			if (ChannelSettings.bRequeuePreempted)
			{
				ReturnToQueue(*Victim);
			}
			else
			{
				BeginDismiss(Victim->Id, ENotificationDismissReason::Preempted);
			}
			return;
		}
	}
	if (ChannelSettings.bMakeRoom)
	{
		for (FNotificationRecord* Record : OnScreen)
		{
			if (Record->State != ENotificationState::Visible || Record->ShownFor < ChannelSettings.MinDisplayTime || Record->Priority > First.Priority)
			{
				continue;
			}
			if (!Victim || Record->Sequence < Victim->Sequence)
			{
				Victim = Record;
			}
		}
		if (Victim)
		{
			BeginDismiss(Victim->Id, ENotificationDismissReason::Preempted);
		}
	}
}

bool UNotificationSubsystem::Tick(float DeltaTime)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_NameTBDNotificationsTick);

	if (!GetLocalPlayer())
	{
		return true;
	}

	// Real seconds (a slowed or paused game doesn't slow notifications); a hitch doesn't eat a notification's time.
	const float Delta = FMath::Min(DeltaTime, 0.1f);
	const double Now = FPlatformTime::Seconds();
	const UWorld* World = GetPlayerWorld();
	const bool bGamePaused = World && World->IsPaused();

	Hosts.RemoveAll([](const TWeakObjectPtr<UNotificationHost>& Host) { return !Host.IsValid(); });

	TickHold(Delta);

	TArray<int32> TimedOut;
	TArray<int32> Expired;
	TArray<int32> Stuck;
	const TArray<TSharedRef<FNotificationRecord>> Snapshot = Records;
	for (const TSharedRef<FNotificationRecord>& Record : Snapshot)
	{
		switch (Record->State)
		{
		case ENotificationState::Visible:
		{
			if (!bGamePaused)
			{
				Record->bPostedWhilePaused = false;
			}
			const UNotificationHost* Host = Record->Host.Get();
			if (!Host || !Host->IsLive() || ShouldHide(*Record))
			{
				ReturnToQueue(*Record);
				break;
			}
			if (!IsFrozen(*Record, bGamePaused))
			{
				Record->ShownFor += Delta;
				if (Record->Duration > 0.f)
				{
					Record->Remaining -= Delta;
					if (Record->Remaining <= 0.f)
					{
						TimedOut.Add(Record->Id);
					}
				}
			}
			break;
		}
		case ENotificationState::Pending:
			if (Record->TypeSettings.MaxQueueTime > 0.f && !Record->bShownOnce && Now - Record->QueuedSince > Record->TypeSettings.MaxQueueTime)
			{
				Expired.Add(Record->Id);
			}
			break;
		case ENotificationState::Leaving:
			if (Now - Record->LeavingSince > NotificationSubsystemPrivate::MaxLeaveSeconds)
			{
				Stuck.Add(Record->Id);
			}
			break;
		}
	}

	for (const int32 Id : TimedOut)
	{
		const FNotificationRecord* Record = FindRecord(Id);
		if (!Record || Record->State != ENotificationState::Visible)
		{
			continue;
		}
		if (!Record->Request.TimeoutActionId.IsNone())
		{
			RespondInternal(Id, Record->Request.TimeoutActionId, /*bTimeout=*/true);
		}
		else
		{
			BeginDismiss(Id, ENotificationDismissReason::TimedOut);
		}
	}
	for (const int32 Id : Expired)
	{
		BeginDismiss(Id, ENotificationDismissReason::Expired);
	}
	for (const int32 Id : Stuck)
	{
		FinishRemoval(Id);
	}

	// Every channel with something waiting gets its turn.
	TArray<FGameplayTag> Channels;
	for (const TSharedRef<FNotificationRecord>& Record : Records)
	{
		if (Record->State == ENotificationState::Pending)
		{
			Channels.AddUnique(Record->Channel);
		}
	}
	for (const FGameplayTag& Channel : Channels)
	{
		ScheduleChannel(Channel, bGamePaused, Now);
	}

	UpdateShortcutTarget();
	TickAnnouncements(Delta);

	const TArray<TWeakObjectPtr<UNotificationHost>> HostSnapshot = Hosts;
	for (const TWeakObjectPtr<UNotificationHost>& Host : HostSnapshot)
	{
		if (UNotificationHost* Live = Host.Get())
		{
			Live->TickFromSubsystem(*this);
		}
	}
	return true;
}

// =====================================================================================================
// Actions and keys
// =====================================================================================================

void UNotificationSubsystem::UpdateShortcutTarget()
{
	const UNotificationSettings& Settings = UNotificationSettings::Get();
	const bool bDismissKeys = Settings.DismissGamepadKey.IsValid() || Settings.DismissKeyboardKey.IsValid();

	const FNotificationRecord* Best = nullptr;
	for (const TSharedRef<FNotificationRecord>& Record : Records)
	{
		if (Record->State != ENotificationState::Visible)
		{
			continue;
		}
		const bool bKeyed = NotificationSubsystemPrivate::HasKeyedAction(*Record) || (bDismissKeys && Record->TypeSettings.bAllowDismiss);
		if (!bKeyed)
		{
			continue;
		}
		if (!Best || NotificationSubsystemPrivate::IsBefore(*Record, *Best, true, true))
		{
			Best = &Record.Get();
		}
	}

	const int32 NewTarget = Best ? Best->Id : 0;
	if (NewTarget == ShortcutTargetId)
	{
		return;
	}
	const int32 OldTarget = ShortcutTargetId;
	ShortcutTargetId = NewTarget;
	if (HeldRecordId != 0 && HeldRecordId != NewTarget)
	{
		HeldRecordId = 0;
		HeldFor = 0.f;
	}
	// The key hints move from one card to the other.
	for (const int32 Id : { OldTarget, NewTarget })
	{
		if (const FNotificationRecord* Record = FindRecord(Id))
		{
			if (UNotificationHost* Host = Record->Host.Get())
			{
				Host->RefreshEntry(Id);
			}
		}
	}
}

void UNotificationSubsystem::TickHold(float DeltaTime)
{
	if (HeldRecordId == 0)
	{
		return;
	}
	const FNotificationRecord* Record = FindRecord(HeldRecordId);
	if (!Record || Record->State != ENotificationState::Visible || ShortcutTargetId != HeldRecordId)
	{
		HeldRecordId = 0;
		HeldFor = 0.f;
		return;
	}
	HeldFor += DeltaTime;
	if (HeldFor >= HeldNeeded)
	{
		const int32 Id = HeldRecordId;
		const FName ActionId = HeldActionId;
		HeldRecordId = 0;
		HeldFor = 0.f;
		RespondInternal(Id, ActionId, /*bTimeout=*/false);
	}
}

float UNotificationSubsystem::GetHoldProgress(FNotificationHandle Handle, FName ActionId) const
{
	if (HeldRecordId == 0 || HeldRecordId != Handle.Id || HeldActionId != ActionId || HeldNeeded <= 0.f)
	{
		return 0.f;
	}
	return FMath::Clamp(HeldFor / HeldNeeded, 0.f, 1.f);
}

FKey UNotificationSubsystem::GetDisplayKey(const FNotificationAction& Action) const
{
	// The key for the device in use; nothing when the action has none on it (a hint the player can't press is noise).
	return bUsingGamepad ? Action.GamepadKey : Action.KeyboardKey;
}

FNotificationKeyGlyph UNotificationSubsystem::GetKeyGlyph(const FKey& Key)
{
	FNotificationKeyGlyph Glyph;
	if (const FNotificationKeyGlyph* Found = UNotificationSettings::Get().KeyGlyphs.Find(Key))
	{
		Glyph = *Found;
	}
	if (Glyph.Label.IsEmpty() && Key.IsValid())
	{
		Glyph.Label = Key.GetDisplayName(/*bLongDisplayName=*/false);
	}
	return Glyph;
}

void UNotificationSubsystem::NoteInputDevice(bool bGamepad)
{
	if (bUsingGamepad == bGamepad)
	{
		return;
	}
	bUsingGamepad = bGamepad;
	if (const FNotificationRecord* Record = FindRecord(ShortcutTargetId))
	{
		if (UNotificationHost* Host = Record->Host.Get())
		{
			Host->RefreshEntry(ShortcutTargetId);
		}
	}
}

bool UNotificationSubsystem::HandleKey(const FKey& Key, int32 UserIndex, bool bDown, bool bRepeat)
{
	if (bDown && !bRepeat)
	{
		NoteInputDevice(Key.IsGamepadKey());
	}

	// In split screen each player's keys answer only their own notifications.
	const int32 OwnIndex = GetSlateUserIndex();
	if (OwnIndex >= 0 && UserIndex != OwnIndex)
	{
		return false;
	}

	if (!bDown)
	{
		if (HeldKey.IsValid() && Key == HeldKey)
		{
			HeldKey = FKey();
			HeldRecordId = 0;
			HeldFor = 0.f;
			return true;
		}
		return false;
	}
	if (bRepeat)
	{
		return HeldKey.IsValid() && Key == HeldKey;
	}

	FNotificationRecord* Target = FindRecordMutable(ShortcutTargetId);
	if (!Target || Target->State != ENotificationState::Visible)
	{
		return false;
	}

	const UNotificationSettings& Settings = UNotificationSettings::Get();
	const bool bDismissKey = (Settings.DismissGamepadKey.IsValid() && Key == Settings.DismissGamepadKey)
		|| (Settings.DismissKeyboardKey.IsValid() && Key == Settings.DismissKeyboardKey);
	if (bDismissKey && Target->TypeSettings.bAllowDismiss)
	{
		DismissByPlayer(FNotificationHandle(Target->Id));
		return true;
	}

	for (const FNotificationAction& Action : Target->Request.Actions)
	{
		const bool bMatches = (Action.GamepadKey.IsValid() && Key == Action.GamepadKey) || (Action.KeyboardKey.IsValid() && Key == Action.KeyboardKey);
		if (!bMatches)
		{
			continue;
		}
		if (Action.HoldTime <= 0.f)
		{
			RespondInternal(Target->Id, Action.ActionId, /*bTimeout=*/false);
			return true;
		}
		HeldKey = Key;
		HeldRecordId = Target->Id;
		HeldActionId = Action.ActionId;
		HeldFor = 0.f;
		HeldNeeded = Action.HoldTime;
		return true;
	}
	return false;
}

// =====================================================================================================
// Feedback
// =====================================================================================================

void UNotificationSubsystem::PlaySound(const FNotificationRecord& Record, const FUISoundSlot& Slot)
{
	if (!Preferences.bPlaySounds || !Slot.IsValid())
	{
		return;
	}
	// From the host's widget, so a host on a Diegetic Screen plays where the screen is.
	const UObject* Context = Record.Host.IsValid() ? static_cast<const UObject*>(Record.Host.Get()) : static_cast<const UObject*>(GetPlayerWorld());
	if (!Context)
	{
		return;
	}
	FUISoundSetSettings SetSettings;
	SetSettings.MinTimeBetweenPlays = Record.TypeSettings.MinTimeBetweenSounds;
	SetSettings.bSilentOnFirstEvent = false;
	FUISoundPlayer::Play(Context, SoundRuntimes.FindOrAdd(Record.Request.Type), SetSettings, Slot);
}

void UNotificationSubsystem::Announce(const FNotificationRecord& Record)
{
	if (!Preferences.bScreenReaderAnnouncements || !Record.TypeSettings.bAnnounce)
	{
		return;
	}
	const FNotificationView View = Record.MakeView();
	FText Spoken = View.Title;
	if (!View.Body.IsEmpty())
	{
		Spoken = View.Title.IsEmpty() ? View.Body : FText::Format(LOCTEXT("Announcement", "{0}. {1}"), View.Title, View.Body);
	}
	if (Spoken.IsEmpty())
	{
		return;
	}
	// Platforms cut an announcement off with the next one: they queue, and only the latest few are kept.
	PendingAnnouncements.Add(Spoken);
	if (PendingAnnouncements.Num() > 4)
	{
		PendingAnnouncements.RemoveAt(0);
	}
}

void UNotificationSubsystem::TickAnnouncements(float DeltaTime)
{
	AnnounceWait = FMath::Max(AnnounceWait - DeltaTime, 0.f);
	if (AnnounceWait > 0.f || PendingAnnouncements.Num() == 0)
	{
		return;
	}
	const FString Spoken = PendingAnnouncements[0].ToString();
	PendingAnnouncements.RemoveAt(0);
	UGameplayStatics::AnnounceAccessibleString(Spoken);
	AnnounceWait = 1.f + NotificationSubsystemPrivate::AnnounceSecondsPerWord * static_cast<float>(NotificationUtils::CountWords(Spoken));
}

void UNotificationSubsystem::DumpToLog() const
{
	UE_LOG(LogNameTBDNotifications, Display, TEXT("Notifications: %d live, %d in history (%d unread), %d hosts, %d suppressions, DND %s"),
		Records.Num(), History.Num(), GetUnreadCount(), Hosts.Num(), Suppressions.Num(), Preferences.bDoNotDisturb ? TEXT("on") : TEXT("off"));
	const UWorld* World = GetPlayerWorld();
	const bool bGamePaused = World && World->IsPaused();
	for (const TSharedRef<FNotificationRecord>& Record : Records)
	{
		static const TCHAR* StateNames[] = { TEXT("Pending"), TEXT("Visible"), TEXT("Leaving") };
		const UNotificationHost* Host = Record->Host.Get();

		// Why a waiting one waits, and why a shown one's time stands still.
		FString Why;
		if (Record->State == ENotificationState::Pending)
		{
			const UNotificationHost* Target = FindHostFor(Record->Channel);
			if (!Target)
			{
				Why = TEXT("waiting: no live host shows its channel");
			}
			else if (IsCinematic() && !Record->TypeSettings.bIgnoreSuppression)
			{
				Why = TEXT("waiting: cinematic mode (Hold During Cinematics)");
			}
			else if (IsHeld(*Record, bGamePaused))
			{
				Why = TEXT("waiting: a Hold / Freeze suppression");
			}
			else
			{
				Why = FString::Printf(TEXT("waiting: channel full (%d max on %s)"),
					Target->GetCapacity(UNotificationSettings::Get().FindChannel(Record->Channel)), *Target->GetName());
			}
		}
		else if (Record->State == ENotificationState::Visible && IsFrozen(*Record, bGamePaused))
		{
			Why = TEXT("time stopped (hovered, paused, held key or Freeze)");
		}
		UE_LOG(LogNameTBDNotifications, Display, TEXT("  #%d %-8s %s on %s  priority %d  count %d  time %.1f / %.1f  host %s  \"%s\"  %s"),
			Record->Id, StateNames[static_cast<int32>(Record->State)], *Record->Request.Type.ToString(), *Record->Channel.ToString(),
			static_cast<int32>(Record->Priority), Record->Request.Count, Record->Remaining, Record->Duration,
			Host ? *Host->GetName() : TEXT("-"), *Record->MakeView().Title.ToString(), *Why);
	}
	for (const TWeakObjectPtr<UNotificationHost>& Weak : Hosts)
	{
		if (const UNotificationHost* Host = Weak.Get())
		{
			UE_LOG(LogNameTBDNotifications, Display, TEXT("  host %s%s  live %s  channels %s  showing %d"),
				*Host->GetName(), Host->bIsDefaultHost ? TEXT(" (default)") : TEXT(""), Host->IsLive() ? TEXT("yes") : TEXT("no"),
				*Host->Channels.ToStringSimple(), Host->GetShownCount());
		}
	}
}

// =====================================================================================================
// Console
// =====================================================================================================

namespace NotificationConsole
{
	UNotificationSubsystem* FindFor(UWorld* World)
	{
		if (UNotificationSubsystem* Direct = World ? UNotificationSubsystem::Get(World) : nullptr)
		{
			return Direct;
		}
		// Typed in the editor's Output Log while playing, a command gets the editor's world, which has no player: use the game's.
		if (GEngine)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if ((Context.WorldType == EWorldType::PIE || Context.WorldType == EWorldType::Game) && Context.World())
				{
					if (UNotificationSubsystem* InGame = UNotificationSubsystem::Get(Context.World()))
					{
						return InGame;
					}
				}
			}
		}
		UE_LOG(LogNameTBDNotifications, Warning, TEXT("NameTBD.Notify: no local player to notify - run the command while playing (PIE or a game)."));
		return nullptr;
	}

	/** Says on screen and in the log what a command did, so it is never silent. */
	void Report(const FString& Message)
	{
		UE_LOG(LogNameTBDNotifications, Display, TEXT("%s"), *Message);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Cyan, Message);
		}
	}

	FNotificationRequest Make(const FGameplayTag& Type, const FText& Title, const FText& Body)
	{
		FNotificationRequest Request;
		Request.Type = Type;
		Request.Title = Title;
		Request.Body = Body;
		return Request;
	}

	FAutoConsoleCommandWithWorldAndArgs PostCommand(
		TEXT("NameTBD.Notify.Post"),
		TEXT("Posts a notification to the first local player: NameTBD.Notify.Post <Type tag> <Title words...>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UNotificationSubsystem* Subsystem = FindFor(World);
			if (!Subsystem || Args.Num() == 0)
			{
				return;
			}
			const FGameplayTag Type = FGameplayTag::RequestGameplayTag(FName(*Args[0]), /*ErrorIfNotFound=*/false);
			const FString Title = FString::Join(TArrayView<const FString>(Args).RightChop(1), TEXT(" "));
			Subsystem->PostNotification(Make(Type, FText::FromString(Title.IsEmpty() ? Args[0] : Title), FText::GetEmpty()));
			Report(Type.IsValid() ? FString::Printf(TEXT("NameTBD.Notify.Post: posted a %s notification."), *Type.ToString())
				: FString::Printf(TEXT("NameTBD.Notify.Post: '%s' isn't a gameplay tag - posted with the default type."), *Args[0]));
		}));

	FAutoConsoleCommandWithWorldAndArgs TestCommand(
		TEXT("NameTBD.Notify.Test"),
		TEXT("Posts one of each built-in notification type (toasts, banner, feed with merging, hint, an invite with actions)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			using namespace NotificationTags;
			UNotificationSubsystem* Subsystem = FindFor(World);
			if (!Subsystem)
			{
				return;
			}
			Subsystem->PostNotification(Make(Type_Success, LOCTEXT("TestSaved", "Game saved"), FText::GetEmpty()));
			Subsystem->PostNotification(Make(Type_Achievement, LOCTEXT("TestAchievement", "Achievement Unlocked"), LOCTEXT("TestAchievementBody", "Cartographer - discover every region")));
			Subsystem->PostNotification(Make(Type_Objective, LOCTEXT("TestObjective", "Objective Updated"), LOCTEXT("TestObjectiveBody", "Reach the lighthouse before nightfall")));
			Subsystem->PostNotification(Make(Type_Hint, LOCTEXT("TestHint", "Hold Shift to sprint"), FText::GetEmpty()));
			Subsystem->PostNotification(Make(Type_Warning, LOCTEXT("TestWarning", "Inventory almost full"), LOCTEXT("TestWarningBody", "38 of 40 slots used")));
			for (int32 Index = 0; Index < 3; ++Index)
			{
				FNotificationRequest Ore = Make(Type_Loot, LOCTEXT("TestOre", "Iron Ore"), FText::GetEmpty());
				Ore.StackKey = TEXT("IronOre");
				Subsystem->PostNotification(Ore);
				FNotificationRequest XP = Make(Type_Reward, LOCTEXT("TestXP", "+{Value} XP"), FText::GetEmpty());
				XP.Value = 50.f;
				Subsystem->PostNotification(XP);
			}
			FNotificationRequest Invite = Make(Type_Social, LOCTEXT("TestInvite", "{Player} invited you to a party"), LOCTEXT("TestInviteBody", "Squad - 2 of 4 players"));
			FNotificationTextArg Player;
			Player.Name = TEXT("Player");
			Player.Value = LOCTEXT("TestPlayer", "Ava");
			Invite.TextArgs.Add(Player);
			FNotificationAction Accept;
			Accept.ActionId = TEXT("Accept");
			Accept.Label = LOCTEXT("TestAccept", "Accept");
			Accept.GamepadKey = EKeys::Gamepad_DPad_Right;
			Accept.KeyboardKey = EKeys::Y;
			Accept.HoldTime = 0.5f;
			FNotificationAction Decline;
			Decline.ActionId = TEXT("Decline");
			Decline.Label = LOCTEXT("TestDecline", "Decline");
			Decline.Style = ENotificationActionStyle::Secondary;
			Decline.GamepadKey = EKeys::Gamepad_DPad_Left;
			Decline.KeyboardKey = EKeys::N;
			Invite.Actions = { Accept, Decline };
			Invite.TimeoutActionId = TEXT("Decline");
			Subsystem->PostNotification(Invite);
			Report(TEXT("NameTBD.Notify.Test: posted the test notifications (NameTBD.Notify.Dump lists them and the hosts showing them)."));
		}));

	FAutoConsoleCommandWithWorldAndArgs ClearCommand(
		TEXT("NameTBD.Notify.Clear"),
		TEXT("Removes every notification of the first local player (on screen and waiting)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UNotificationSubsystem* Subsystem = FindFor(World))
			{
				Subsystem->ClearNotifications(FGameplayTagContainer(), true);
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs DumpCommand(
		TEXT("NameTBD.Notify.Dump"),
		TEXT("Logs every live notification of the first local player, and its hosts."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (const UNotificationSubsystem* Subsystem = FindFor(World))
			{
				Subsystem->DumpToLog();
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs DoNotDisturbCommand(
		TEXT("NameTBD.Notify.DND"),
		TEXT("Turns Do Not Disturb on (1) or off (0) for the first local player."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UNotificationSubsystem* Subsystem = FindFor(World))
			{
				FNotificationUserPreferences Changed = Subsystem->GetNotificationPreferences();
				Changed.bDoNotDisturb = Args.Num() > 0 ? FCString::Atoi(*Args[0]) != 0 : !Changed.bDoNotDisturb;
				Subsystem->SetNotificationPreferences(Changed);
			}
		}));
}

#undef LOCTEXT_NAMESPACE
