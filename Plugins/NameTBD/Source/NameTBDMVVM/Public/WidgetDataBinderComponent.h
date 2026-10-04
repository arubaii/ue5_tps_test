// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Extensions/UIComponent.h"
#include "INotifyFieldValueChanged.h"
#include "Misc/EngineVersionComparison.h"
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

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FWidgetDataTriggerEvent, FName, TriggerName, bool, bActive);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FWidgetDataBinderEvent, UWidgetDataBinderComponent*, Binder);

/**
 * Connects a widget's ViewModel to the gameplay object it describes, and plays Widget Animations when the
 * ViewModel changes, with no glue graphs.
 *
 * Select a widget in the hierarchy, press Add Component, pick Widget Data Binder Component, then fill two
 * tables in Details:
 *  - Field Bindings: a value on the source ("HealthComponent.Health") copied into a ViewModel field.
 *  - Triggers: a condition on a ViewModel field (Is True, Health Percent < 0.25, Changed) that plays or
 *    stops Anim IDs, edge-triggered.
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

	/** Values written into the ViewModel for previewing, instead of a live source. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mock Data", meta = (TitleProperty = "{ViewModelField} = {Value}"))
	TArray<FWidgetDataMockValue> MockValues;

	/**
	 * Writes Mock Values into the ViewModel in the UMG designer, so View Bindings show them if the ViewModel
	 * plugin runs bindings there. Triggers don't play in the designer.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mock Data")
	bool bUseMockDataInDesigner = true;

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
		bool bActive = false;
		bool bHasLastText = false;
		FString LastText;
		/** Set Value: the last value sent, so an unchanged field sends nothing. */
		bool bHasLastValue = false;
		FVector4 LastValue = FVector4(0.0, 0.0, 0.0, 0.0);
		bool bWarned = false;
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

	void ApplyMockValues(UObject* ViewModel);
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

	bool bOwnerConstructed = false;
	bool bHadSource = false;
	bool bSuppressTriggers = false;
	bool bVerifyViewModelNextTick = false;
	bool bAnyPolling = false;
	bool bWarnedNoViewModel = false;

	static TMap<TObjectKey<UWidget>, TWeakObjectPtr<UWidgetDataBinderComponent>> BindersByWidget;
};