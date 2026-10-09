// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Extensions/UIComponent.h"
#include "INotifyFieldValueChanged.h"
#include "Misc/EngineVersionComparison.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/ObjectKey.h"
#include "WidgetDataBindingTypes.h"
#include "WidgetDataValues.h"

#include "WidgetDataBinderComponent.generated.h"

class AActor;
class UUserWidget;
class UWidget;
class UWidgetDataBinderComponent;
class UWidgetDataBinderEventProxy;
class UWidgetAnimationComponent;

/** What the designer's Data Preview can do to a list, to try its animations. */
enum class EWidgetDataListEdit : uint8
{
	AddRow,
	RemoveRow,
	Shuffle,
	Reverse,
	MoveOneUp
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FWidgetDataTriggerEvent, FName, TriggerName, bool, bActive);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FWidgetDataBinderEvent, UWidgetDataBinderComponent*, Binder);

/**
 * Connects a widget's ViewModel to the gameplay object it describes, and plays Widget Animations when the
 * ViewModel changes, with no glue graphs.
 *
 * Select a widget in the hierarchy, press Add Component, pick Widget Data Binder Component, then fill the
 * tables in Details:
 *  - Field Bindings: a value on the source ("HealthComponent.Health") copied into a ViewModel field.
 *  - Triggers: a condition on a ViewModel field (Is True, Health Percent < 0.25, Score > Best Score, Changed,
 *    Went Up / Went Down) that plays or stops Anim IDs, edge-triggered - or a field driving a value track, a
 *    progress, a state machine input, an Animation Parameter, or another field counted to it (a score ticking up).
 *  - List Bindings: a ViewModel array shown as one entry widget per element in a Flow List or panel, matched by
 *    key, so rows arrive, leave and move rather than being rebuilt - a leaderboard.
 * Then try it without playing: Mock Values and Sample Sets, stepped through in the binder's Data Preview, run
 * the triggers and lists in the designer exactly as the game will.
 * Whoever creates the widget hands it its source with one call, Set Widget Data Source (Row, Pawn), and
 * can call it again at any time to point the same widget at another object.
 *
 * The widget itself still only knows its ViewModel: its View Bindings read the ViewModel, and cannot tell a
 * live pawn, a replicated copy or the design-time mock values apart. The binder owns the plumbing - which
 * object, when to read it, rebinding, a destroyed source, edge detection - and none of the meaning: which
 * fields exist and what they mean stays in the ViewModel, and when an animation should play stays in the
 * trigger rows.
 *
 * The ViewModel is, in order of preference:
 *  1. one handed over with Set ViewModel;
 *  2. the one named ViewModel Name in the widget's Viewmodels panel (the Widget Blueprint the binder was
 *     added in, or the selected widget itself when it is a User Widget with that ViewModel), so View
 *     Bindings and the binder share one instance;
 *  3. with Create ViewModel If Missing on, a new ViewModel Class instance, handed to the widget's view when
 *     it has a Manual ViewModel of that name, or kept privately when it has none.
 */
UCLASS(DefaultToInstanced, EditInlineNew, BlueprintType, meta = (DisplayName = "Widget Data Binder Component"))
class NAMETBDMVVM_API UWidgetDataBinderComponent : public UUIComponent
{
	GENERATED_BODY()

public:

	// -------------------------------------------------------------------------------------------------
	// Authored settings
	// -------------------------------------------------------------------------------------------------

	/**
	 * The ViewModel's name in the Widget Blueprint's Viewmodels panel. None: the binder uses its own private
	 * ViewModel of ViewModel Class, which triggers can still watch but View Bindings can't see.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ViewModel", meta = (DisplayName = "ViewModel Name"))
	FName ViewModelName;

	/**
	 * The ViewModel's class. Lists the fields in the ViewModel Field dropdowns, and is what gets created when
	 * the widget has no ViewModel yet.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ViewModel", meta = (DisplayName = "ViewModel Class", MustImplement = "/Script/FieldNotification.NotifyFieldValueChanged"))
	TSubclassOf<UObject> ViewModelClass;

	/**
	 * Creates a ViewModel Class instance when the widget doesn't have one: a Manual ViewModel nobody has set,
	 * or no Viewmodels at all. Each widget gets its own, which is what makes N rows independent.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ViewModel", meta = (DisplayName = "Create ViewModel If Missing"))
	bool bCreateViewModelIfMissing = true;

	/**
	 * The class the source will be, such as your pawn or character Blueprint. Only fills the Source Path and
	 * Event Path dropdowns; any object can be set as the source at runtime.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Source")
	TSubclassOf<UObject> SourceClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Source")
	EWidgetDataSourceLost OnSourceLost = EWidgetDataSourceLost::KeepValues;

	/** Source value -> ViewModel field. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Field Bindings", meta = (TitleProperty = "{SourcePath} -> {ViewModelField}"))
	TArray<FWidgetFieldBinding> FieldBindings;

	/** ViewModel field condition -> play or stop Anim IDs. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Triggers", meta = (TitleProperty = "{ViewModelField} {Condition} -> {Action} {AnimIds}"))
	TArray<FWidgetFieldTrigger> Triggers;

	/** ViewModel array -> one entry widget per element, in a Flow List or panel, matched by Key Field. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "List Bindings", meta = (TitleProperty = "{ViewModelField} -> {ListWidget} ({EntryWidgetClass})"))
	TArray<FWidgetListBinding> ListBindings;

	/** Values written into the ViewModel for previewing, instead of a live source. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mock Data", meta = (TitleProperty = "{ViewModelField} = {Value}"))
	TArray<FWidgetDataMockValue> MockValues;

	/**
	 * Writes Mock Values into the ViewModel in the UMG designer, so View Bindings show them if the ViewModel
	 * plugin runs bindings there. With Run Triggers In Designer, the triggers and List Bindings act on them too.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mock Data")
	bool bUseMockDataInDesigner = true;

	/**
	 * The triggers, counts and List Bindings run in the UMG designer too, on the Mock Values and whatever the Data
	 * Preview (at the top of these Details) shows - a sample set, a value typed there, a list shuffled - so the widget
	 * reacts in the designer exactly as it will in game, without Play In Editor. Nothing they do there is saved.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mock Data")
	bool bRunTriggersInDesigner = true;

	/**
	 * Moments of sample data, each put over the Mock Values: stepped through in the Data Preview (previous / next /
	 * play), so each step's changes animate - a leaderboard reshuffled, a score going up, a row arriving. Apply Sample
	 * Set puts one on at runtime, for testing a screen on its own.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mock Data", meta = (TitleProperty = "Name"))
	TArray<FWidgetDataSampleSet> SampleSets;

	/** The Data Preview's Play: seconds on each sample set. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mock Data", meta = (ClampMin = "0.25", UIMin = "0.5", UIMax = "10", Units = "s"))
	float SampleSetInterval = 2.f;

	/**
	 * Writes Mock Values when the game has set no source (and again when the source is cleared), and runs
	 * the triggers against them. For testing a screen on its own in PIE before any gameplay feeds it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mock Data")
	bool bUseMockDataWithoutSource = false;

	/** A trigger's condition became true (bActive) or false. Changed triggers only report true. */
	UPROPERTY(BlueprintAssignable, Category = "Data Binder")
	FWidgetDataTriggerEvent OnTriggerChanged;

	/** The source was destroyed. On Source Lost has already been applied. */
	UPROPERTY(BlueprintAssignable, Category = "Data Binder")
	FWidgetDataBinderEvent OnSourceLostEvent;

	// -------------------------------------------------------------------------------------------------
	// Runtime
	// -------------------------------------------------------------------------------------------------

	/**
	 * The object Field Bindings read from. Setting another one rebinds: the old source's events are released,
	 * every binding reads the new one, and triggers act on whatever changed. Safe to call before the widget
	 * is constructed; binding then waits for construction.
	 */
	UFUNCTION(BlueprintCallable, Category = "Data Binder")
	void SetSource(UObject* NewSource);

	/** Releases the source. The ViewModel keeps its values (or takes Mock Values with Use Mock Data Without Source). */
	UFUNCTION(BlueprintCallable, Category = "Data Binder")
	void ClearSource();

	UFUNCTION(BlueprintPure, Category = "Data Binder")
	UObject* GetSource() const { return DataSource.Get(); }

	/**
	 * Uses this ViewModel rather than looking one up or creating one, and hands it to the widget's view under
	 * ViewModel Name when there is one. Must be Field Notify (a ViewModel).
	 */
	UFUNCTION(BlueprintCallable, Category = "Data Binder", meta = (DisplayName = "Set ViewModel"))
	void SetViewModel(UObject* NewViewModel);

	/** The ViewModel the binder writes to and watches, once the widget is constructed. */
	UFUNCTION(BlueprintPure, Category = "Data Binder", meta = (DisplayName = "Get ViewModel"))
	UObject* GetViewModel() const { return BoundViewModel.Get(); }

	/** Reads every Field Binding now, whatever its Update mode. What Manual bindings wait for. */
	UFUNCTION(BlueprintCallable, Category = "Data Binder")
	void RefreshFromSource();

	/**
	 * Writes the Mock Values, then sample set Index's values and lists, into the ViewModel: the triggers and List Bindings
	 * act on whatever changed, as on a real change. For testing a screen on its own; the designer's Data Preview steps
	 * through the sets. False when there is no such set or no ViewModel yet.
	 */
	UFUNCTION(BlueprintCallable, Category = "Data Binder")
	bool ApplySampleSet(int32 SetIndex);

	/**
	 * Sets a ViewModel field from text, written as the Details panel writes it (42, 0.25, true, an enum's name, an array
	 * as Unreal exports one). Broadcast as any change, so View Bindings, triggers and List Bindings follow. False when the
	 * field isn't there or the text isn't a value of its type.
	 */
	UFUNCTION(BlueprintCallable, Category = "Data Binder")
	bool SetViewModelFieldFromText(FName FieldName, const FString& ValueText);

	/** A ViewModel field's value as text (an array as its length, "3 rows"). False when there is no such field or no ViewModel. */
	UFUNCTION(BlueprintPure, Category = "Data Binder")
	bool GetViewModelFieldAsText(FName FieldName, FString& OutValueText) const;

	/** The entry widgets List Binding ListIndex shows now, in order. */
	UFUNCTION(BlueprintPure, Category = "Data Binder")
	TArray<UUserWidget*> GetListEntries(int32 ListIndex) const;

	/**
	 * Gives the binder one element of a list to show - what a List Binding does for each entry it makes: each field of the
	 * struct is copied into the ViewModel field of the same name, and Position into the field PositionFieldName (if any).
	 * Kept, and put on again whenever the binder binds a ViewModel. C++: a struct can't be passed generically to Blueprint.
	 */
	void SetItemData(const UScriptStruct* Struct, const void* Data, FName PositionFieldName, int32 Position);

	/** As SetItemData for an object element (the binder's source or ViewModel): only the position. */
	void SetItemPosition(FName PositionFieldName, int32 Position);

	/** The array edits the Data Preview offers on a list's field, to try its animations. False when the field isn't an array. */
	bool EditListForPreview(FName FieldName, EWidgetDataListEdit Edit);

	/** The Data Preview's sample set (INDEX_NONE: only the Mock Values). Applied at once when bound. */
	void SetPreviewSampleSet(int32 SetIndex);
	int32 GetPreviewSampleSet() const { return PreviewSampleSet; }

	/** Bound in the designer (Run Triggers In Designer): what the Data Preview shows and changes. */
	bool IsDesignBound() const { return bDesignBound; }

#if WITH_EDITOR
	/**
	 * The designer's live copies of this binder - the template the Details edit - or this one when it is a live copy. The
	 * Data Preview acts on these.
	 */
	TArray<UWidgetDataBinderComponent*> FindDesignerPreviews() const;

	/** Takes the template's authored settings (the designer doesn't copy a component's edits until it compiles) and binds again. */
	void RefreshDesignerPreviewFrom(const UWidgetDataBinderComponent* Template);
#endif

	/** The Widget Data Binder Component on Widget, or null. */
	UFUNCTION(BlueprintPure, Category = "Data Binder")
	static UWidgetDataBinderComponent* FindWidgetDataBinder(const UWidget* Widget);

	/**
	 * Sets the source of every Widget Data Binder in Widget: on Widget itself and, when it is a User Widget,
	 * on any widget in its own hierarchy. Nested User Widgets are not entered, since they usually describe
	 * something else (each squad row its own pawn). Returns how many binders were set.
	 *
	 * The one call a screen needs to point a row at a pawn: Create Widget, then Set Widget Data Source.
	 */
	UFUNCTION(BlueprintCallable, Category = "Data Binder", meta = (DisplayName = "Set Widget Data Source", DefaultToSelf = "Widget"))
	static int32 SetWidgetDataSource(UWidget* Widget, UObject* NewSource);

	// Dropdowns. Must stay UFUNCTIONs for GetOptions.
	UFUNCTION()
	TArray<FString> GetViewModelFieldOptions() const;

	UFUNCTION()
	TArray<FString> GetSourcePathOptions() const;

	UFUNCTION()
	TArray<FString> GetEventPathOptions() const;

	UFUNCTION()
	TArray<FString> GetAnimationWidgetOptions() const;

	/** The ViewModel Class's array fields. */
	UFUNCTION()
	TArray<FString> GetListFieldOptions() const;

	/** The members of the elements of the ViewModel Class's array fields - what can tell elements apart. */
	UFUNCTION()
	TArray<FString> GetListKeyFieldOptions() const;

	virtual void BeginDestroy() override;

protected:

	// UUIComponent
#if UE_VERSION_OLDER_THAN(5, 6, 0)
	virtual bool OnInitialize() override;
#else
	virtual void OnInitialize() override;
	virtual void OnPreConstruct(bool bIsDesignTime) override;
#endif
	virtual void OnConstruct() override;
	virtual void OnDestruct() override;

private:

	friend class UWidgetDataBinderEventProxy;

	/** Per Field Binding, rebuilt whenever the ViewModel or the source changes. */
	struct FBindingState
	{
		FWidgetDataPath SourcePath;
		EWidgetDataUpdateMode Mode = EWidgetDataUpdateMode::Poll;
		const FProperty* TargetProperty = nullptr;
		TUniquePtr<FWidgetDataScratch> Scratch;
		TWeakObjectPtr<UObject> NotifyObject;
		UE::FieldNotification::FFieldId NotifyField;
		FDelegateHandle NotifyHandle;
		float PollElapsed = 0.f;
		bool bWarnedRead = false;
		bool bWarnedConvert = false;
	};

	/** Per trigger. */
	struct FTriggerState
	{
		FWidgetDataPath FieldPath;
		FWidgetDataPath CompareFieldPath;
		bool bActive = false;
		bool bHasLastText = false;
		FString LastText;
		/** Set Value: the last value sent, so an unchanged field sends nothing. */
		bool bHasLastValue = false;
		FVector4 LastValue = FVector4(0.0, 0.0, 0.0, 0.0);
		/** Went Up / Went Down, Count Into Field: the number last seen. */
		bool bHasLastNumber = false;
		double LastNumber = 0.0;
		/** Count Into Field: the field counted, and where the count is. */
		const FProperty* CountProperty = nullptr;
		bool bCounting = false;
		bool bHasCountShown = false;
		double CountFrom = 0.0;
		double CountTarget = 0.0;
		double CountShown = 0.0;
		float CountElapsed = 0.f;
		bool bWarned = false;
	};

	/** Per List Binding: the keys shown and their entries, in order. Kept across rebinding, so entries are reused. */
	struct FListState
	{
		FWidgetDataPath FieldPath;
		TArray<FString> Keys;
		TArray<TWeakObjectPtr<UUserWidget>> Entries;
		bool bFilled = false;
		bool bWarned = false;
		bool bWarnedNoEntryBinder = false;
	};

	void SetBinderOwner(UWidget* NewOwner);

	/** Finds or creates the ViewModel, watches it, then binds the source. bFromConstruct allows one frame of grace for the view to create its ViewModel. */
	void BindAll(bool bFromConstruct);
	void UnbindAll();

	/** bKeepTriggerStates: carry each trigger's active state over, so a switch of ViewModel doesn't lose track of animations it started. */
	void AttachViewModel(UObject* ViewModel, bool bKeepTriggerStates = false);
	void DetachViewModel();

	void BindSource();
	void UnbindSource();

	/** Reads one Field Binding from the source and writes it into the ViewModel if it changed. */
	void PushBinding(int32 BindingIndex);

	/** Writes the Mock Values, all but those of the fields in Skip (a sample set's own). */
	void ApplyMockValues(UObject* ViewModel, const TSet<FName>* Skip = nullptr);

	/** The fields sample set SetIndex writes. */
	TSet<FName> GetSampleSetFields(int32 SetIndex) const;
	void ApplyDesignTimeMock();

	void HandleViewModelFieldChanged(UObject* Object, UE::FieldNotification::FFieldId FieldId);
	void HandleSourceFieldChanged(UObject* Object, UE::FieldNotification::FFieldId FieldId, int32 BindingIndex);
	void HandleSourceEvent(int32 BindingIndex);
	void HandleSourceLost();

	UFUNCTION()
	void HandleSourceActorDestroyed(AActor* DestroyedActor);

	/** bInitial: the evaluation right after binding, where a rising edge only acts with Evaluate On Bind. */
	void EvaluateTrigger(int32 TriggerIndex, bool bInitial);
	void EvaluateAllTriggers(bool bInitial);
	bool EvaluateCondition(const FWidgetFieldTrigger& Trigger, const FWidgetDataValue& Value, bool bCurrentlyActive) const;
	void FireTrigger(int32 TriggerIndex, bool bBecameActive);
	void PlayOrStop(const FWidgetFieldTrigger& Trigger, bool bPlay);

	/** Set Value: the field's value - raw, up to four channels - as the value of the trigger's Value Name. */
	void SetAnimationValue(const FWidgetFieldTrigger& Trigger, const FVector4& Value, bool bSnap);

	/** Set State Machine Input: the field's value onto the input named Input Name of the machine the trigger reaches. */
	void SetMachineInput(const FWidgetFieldTrigger& Trigger, double Value);

	/** Set Animation Parameter: the field's value as the parameter named Parameter Name, on the widget the trigger reaches. */
	void SetAnimationParameter(const FWidgetFieldTrigger& Trigger, const FWidgetDataValue& Value);

	/** Count Into Field: writes Number into the trigger's Count Field - a number, or text written out. */
	void WriteCount(int32 TriggerIndex, double Number);

	/** Advances every count running. True while any still is. */
	bool TickCounts(float DeltaTime);

	/** Shows List Binding ListIndex's array: entries added, removed, moved and given their elements. bInitial: nothing animates. */
	void ReconcileList(int32 ListIndex, bool bInitial);
	void ReconcileAllLists(bool bInitial);

	/** Hands element Position of a list (at ElementPtr, of type Inner) to its entry's Widget Data Binder. */
	void PushListElement(int32 ListIndex, UUserWidget* Entry, const FProperty* Inner, const void* ElementPtr, int32 Position);

	/** Copies the item data and position (SetItemData) into the ViewModel. */
	void ApplyItemData();

	/** Writes sample set SetIndex's values and lists into ViewModel. */
	void ApplySampleSetValues(UObject* ViewModel, int32 SetIndex);

	/** Imports Text into Field of ViewModel and writes it (broadcast when it changed). */
	bool WriteFieldFromText(UObject* ViewModel, FName FieldName, const FString& ValueText, bool bWarn);

	/** Builds the value of list field ArrayProperty from sample rows into ArrayValue, reusing ViewModel's current objects by key. */
	void BuildSampleArray(UObject* ViewModel, const FArrayProperty* ArrayProperty, const TArray<FString>& Rows, void* ArrayValue);

	/** At design time: binds the ViewModel with the Mock Values (and the preview's sample set) and runs the triggers and lists on it. */
	void BindForDesigner();

	/** At design time: keeps the designer repainting for a while, as something plays that a core ticker drives. */
	void KeepDesignerAwake(float Seconds);

	/**
	 * What owns a trigger's Anim IDs: outwards from its Animation Widget to the first Widget Animation Component on a
	 * widget, or widget that is an animation host itself - the same search Play Widget Animation makes. One output is
	 * set when it returns true; false logs a warning.
	 */
	bool FindAnimationOwner(const FWidgetFieldTrigger& Trigger, UWidgetAnimationComponent*& OutComponent, IWidgetAnimHost*& OutHost) const;

	/** The User Widget whose view holds ViewModel Name: the selected widget if it is one with that ViewModel, else the Widget Blueprint the binder is in. */
	UUserWidget* FindViewModelHost() const;
	UObject* FindViewModelInView() const;
	UObject* CreateViewModel();

	/** Hands ViewModel to the widget's view under ViewModel Name, so View Bindings read it. True when the view now holds it. */
	bool PushViewModelToView(UObject* ViewModel, bool bWarnOnFailure);
	UWidget* ResolveAnimationWidget(FName WidgetPath) const;

	/** The Widget Blueprint the component belongs to, for the dropdowns: the live one at runtime, the class default in the editor. */
	UUserWidget* GetOptionsRoot() const;

	void EnsureTicker();
	void StopTicker();
	bool TickBinder(float DeltaTime);

	FString DescribeForLog() const;

	TWeakObjectPtr<UWidget> BinderOwner;
	TWeakObjectPtr<UObject> DataSource;
	TWeakObjectPtr<UObject> ExplicitViewModel;
	TWeakObjectPtr<UObject> BoundViewModel;
	TWeakObjectPtr<AActor> WatchedActor;

	/** A ViewModel this binder created. Held here too, since a private one has no view holding it. */
	UPROPERTY(Transient)
	TObjectPtr<UObject> OwnedViewModel;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UWidgetDataBinderEventProxy>> EventProxies;

	TArray<FBindingState> BindingStates;
	TArray<FTriggerState> TriggerStates;
	TArray<TPair<UE::FieldNotification::FFieldId, FDelegateHandle>> ViewModelHandles;

	FTSTicker::FDelegateHandle TickerHandle;

	TArray<FListState> ListStates;

	/** The entries List Bindings made, kept alive with their lists. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UUserWidget>> ListEntryWidgets;

	/** SetItemData: one list element this binder shows. */
	UPROPERTY(Transient)
	FInstancedStruct ItemData;

	FName ItemPositionField;
	int32 ItemPosition = INDEX_NONE;

	/** The Data Preview's sample set, at design time. */
	int32 PreviewSampleSet = INDEX_NONE;

	/** Bound by BindForDesigner. */
	bool bDesignBound = false;

	/** Count Into Field: some count is running. */
	bool bAnyCounting = false;

	double DesignerAwakeUntil = 0.0;

	bool bOwnerConstructed = false;
	bool bHadSource = false;
	bool bSuppressTriggers = false;
	bool bVerifyViewModelNextTick = false;
	bool bAnyPolling = false;
	bool bWarnedNoViewModel = false;

	static TMap<TObjectKey<UWidget>, TWeakObjectPtr<UWidgetDataBinderComponent>> BindersByWidget;
};