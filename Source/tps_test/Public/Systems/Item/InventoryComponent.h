// Copyright arubaii. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "Public/Systems/Item/InventoryEnums.h"
#include "Public/Systems/Item/Definition.h"
#include "Public/Systems/Item/Instance.h"
#include "Public/Systems/Item/ItemFragment.h"
#include "Public/Systems/Item/ItemFragmentLibrary.h"
#include "InventoryComponent.generated.h"

class UInventoryComponent;
class UUserWidget;

// =====================================================================================
//  Replicated instance store (Fast Array)
//
//  Replaces the BP_GameInstance ItemDatabase (Map<Guid, S_ItemInstance>). Each
//  component owns the instances it holds — top-level items AND their attachment
//  instances (peers referenced by Guid from FItemInstance::WeaponAttachments).
//  FFastArraySerializer replicates only changed entries, scaling to large stashes.
// =====================================================================================

USTRUCT()
struct FInventoryItemEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	FInventoryItemEntry() {}
	explicit FInventoryItemEntry(const FItemInstance& InInstance) : Instance(InInstance) {}

	UPROPERTY()
	FItemInstance Instance;
};

USTRUCT()
struct FInventoryItemList : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FInventoryItemEntry> Entries;

	/** Owning component; set in the component constructor. Not replicated. */
	UPROPERTY(NotReplicated)
	TObjectPtr<UInventoryComponent> OwnerComponent = nullptr;

	// Client-side replication callbacks -> notify the owning component.
	void PostReplicatedAdd(const TArrayView<int32>& AddedIndices, int32 FinalSize);
	void PostReplicatedChange(const TArrayView<int32>& ChangedIndices, int32 FinalSize);
	void PreReplicatedRemove(const TArrayView<int32>& RemovedIndices, int32 FinalSize);

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FInventoryItemEntry, FInventoryItemList>(Entries, DeltaParms, *this);
	}
};

template<>
struct TStructOpsTypeTraits<FInventoryItemList> : public TStructOpsTypeTraitsBase2<FInventoryItemList>
{
	enum { WithNetDeltaSerializer = true };
};

// =====================================================================================
//  Serialization record — what SaveToRecord/LoadFromRecord round-trip.
//  Put one of these (or a map of them) inside your USaveGame subclass.
// =====================================================================================

USTRUCT(BlueprintType)
struct FInventoryRecord
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory Record")
	TArray<FItemInstance> Items;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory Record")
	TArray<FInventorySlot> Slots;
};

/** Config entry for FillDefaultInventory (replaces the hardcoded GEN_Potion/GEN_Hatchet seeding). */
USTRUCT(BlueprintType)
struct FDefaultInventoryEntry
{
	GENERATED_BODY()

	/** Pick the table + row in the editor; only the row name is used at runtime. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Default Item")
	FDataTableRowHandle RowHandle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Default Item")
	int32 Quantity = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Default Item")
	int32 SlotIndex = 0;
};

/**
 * Reserved for future per-connection filtering. The current build replicates
 * inventory data with COND_None (all relevant connections) — correct for a
 * non-competitive co-op game and REQUIRED for shared containers (chests have no
 * owning connection, so OwnerOnly there would reach nobody). If competitive
 * privacy is ever needed, this switch is where owner-only filtering hooks in.
 */
UENUM(BlueprintType)
enum class EInventoryReplicationScope : uint8
{
	AllClients,
	OwnerOnly
};

// Delegates — parity with the BP dispatchers. UI rebinds to these (same shapes).
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInventoryChanged, int32, InventoryIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnItemPickup, FItemInstance, PickedInstance, int32, QuantityTaken);

/**
 * Server-replicated inventory. Direct C++ port of BPC_Inventory with the
 * BP_GameInstance item database absorbed into a per-component replicated store.
 *
 * DATA MODEL
 *   InventoryList : replicated Fast Array of FItemInstance — the source of truth for
 *                   every instance this container holds (incl. attachment instances).
 *   Slots         : replicated slot layout; each slot references an instance by Guid.
 *   IdToIndex     : transient lookup cache (Guid -> entry index), rebuilt on changes.
 *
 * AUTHORITY MODEL
 *   All mutations run on the server. UI on clients calls the Request* entry points on
 *   the LOCAL PLAYER'S component (player-owned components carry the Server RPCs;
 *   chests have no owning connection, so chest operations are routed through the
 *   player's component with the chest passed as a parameter).
 *
 * SERIALIZATION
 *   SaveToRecord / LoadFromRecord round-trip the full state as an FInventoryRecord.
 *   You choose when to call them (extraction commit, stash save, etc.).
 */
UCLASS(ClassGroup=(Inventory), Blueprintable, meta=(BlueprintSpawnableComponent))
class TPS_TEST_API UInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UInventoryComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;

	// ---------------------------------------------------------------------------------
	// Configuration (parity with BPC_Inventory variables)
	// ---------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Inventory|Config")
	FName InventoryName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Config")
	int32 MaxInventorySlots = 0;

	/** Slot type every slot in this inventory is created with (InitializeInventory parity). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Config")
	ESlotType SlotType = ESlotType::Generic;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Config")
	EInventoryType InventoryType = EInventoryType::Backpack;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Config")
	bool ShouldFillDefaultInventory = false;

	/** Replaces the hardcoded GEN_Potion/GEN_Hatchet seeding. Used when ShouldFillDefaultInventory. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Config", meta = (EditCondition = "ShouldFillDefaultInventory"))
	TArray<FDefaultInventoryEntry> DefaultItems;

	/** See enum comment — reserved; current build replicates to all relevant connections. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory|Config")
	EInventoryReplicationScope ReplicationScope = EInventoryReplicationScope::AllClients;

	// ---------------------------------------------------------------------------------
	// State flags & references (parity)
	// ---------------------------------------------------------------------------------

	UPROPERTY(BlueprintReadWrite, Category = "Inventory|State")
	bool IsInventoryFull = false;

	UPROPERTY(BlueprintReadWrite, Category = "Inventory|State")
	bool InventoryOpenedYet = false;

	UPROPERTY(BlueprintReadOnly, Category = "Inventory|State")
	bool IsDataInitialized = false;

	UPROPERTY(BlueprintReadWrite, Category = "Inventory|State")
	bool IsInventoryWidgetInitialized = false;

	/** Parity refs. Character resolves automatically when owner is a Character/PlayerState/Controller. */
	UPROPERTY(BlueprintReadWrite, Category = "Inventory|State")
	TObjectPtr<AActor> ItemActor = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "Inventory|State")
	TObjectPtr<AActor> Character = nullptr;

	// ---------------------------------------------------------------------------------
	// Dispatchers (parity — UI rebinds, same shapes)
	// ---------------------------------------------------------------------------------

	UPROPERTY(BlueprintAssignable, Category = "Inventory|Events")
	FOnInventoryChanged OnInventoryChanged;

	UPROPERTY(BlueprintAssignable, Category = "Inventory|Events")
	FOnItemPickup OnItemPickup;

	// ---------------------------------------------------------------------------------
	// Instance store — the absorbed Game Instance database (UI-facing reads preserved)
	// ---------------------------------------------------------------------------------

	/** GetItem parity: Guid -> instance lookup into this component's store. */
	UFUNCTION(BlueprintPure, Category = "Inventory|Items", meta = (ReturnDisplayName = "Found"))
	bool GetItem(FGuid ItemID, FItemInstance& ItemInstance) const;

	/** GetItemStructByID parity: Guid -> instance -> RowName -> shared definition. */
	UFUNCTION(BlueprintPure, Category = "Inventory|Items", meta = (ReturnDisplayName = "Found"))
	bool GetItemStructByID(FGuid ItemID, FItemStructure& ItemStructure) const;

	UFUNCTION(BlueprintPure, Category = "Inventory|Items", meta = (ReturnDisplayName = "Contains"))
	bool ContainsItem(FGuid ItemID) const;

	/** IsValidItemID parity: non-empty Guid AND present in this store. */
	UFUNCTION(BlueprintPure, Category = "Inventory|Items", meta = (ReturnDisplayName = "IsValid"))
	bool IsValidItemID(FGuid ItemID) const;

	// ---------------------------------------------------------------------------------
	// Slot reads (UI-facing, preserved)
	// ---------------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Inventory|Slots")
	FInventorySlot GetSlot(int32 Index) const;

	UFUNCTION(BlueprintPure, Category = "Inventory|Slots")
	TArray<FInventorySlot> GetSlots() const;

	UFUNCTION(BlueprintPure, Category = "Inventory|Slots", meta = (ReturnDisplayName = "Inventory Full"))
	bool GetNextEmptySlot(FInventorySlot& Slot) const;

	/**
	 * Two-pass search (exact BP parity):
	 *   pass 1 — a slot holding the SAME item (AreItemsTheSame) that is not at max stack;
	 *   pass 2 — an EMPTY slot whose SlotType is in the item's CompatibleSlotTypes.
	 */
	UFUNCTION(BlueprintPure, Category = "Inventory|Slots", meta = (ReturnDisplayName = "Found"))
	bool GetNextCompatibleSlot(const FItemInstance& ItemToBeCheckedAgainst, FInventorySlot& Slot) const;

	/** Can MY slot's item be placed into the target slot's type? (containment test) */
	UFUNCTION(BlueprintPure, Category = "Inventory|Slots", meta = (ReturnDisplayName = "IsCompatible"))
	bool AreSlotsCompatible(const FInventorySlot& MyInventorySlot, const FInventorySlot& TargetInventorySlot) const;

	UFUNCTION(BlueprintPure, Category = "Inventory|Items", meta = (ReturnDisplayName = "Same"))
	bool AreItemsTheSame(const FItemInstance& MyItem, const FItemInstance& TargetItem) const;

	UFUNCTION(BlueprintPure, Category = "Inventory|Items", meta = (ReturnDisplayName = "IsItemSizeMax"))
	bool IsItemSizeMax(const FItemInstance& ItemInstance) const;

	/**
	 * Validation for a drag/drop swap. BP parity: not the same slot, and the dragged
	 * item fits the target slot. Improvement (documented): when the TARGET slot is
	 * occupied, the reverse direction is also checked so a two-way swap can't place
	 * the displaced item into an incompatible slot.
	 */
	UFUNCTION(BlueprintPure, Category = "Inventory|Slots", meta = (ReturnDisplayName = "IsValid"))
	bool IsValidSlotSwap(const FInventorySlot& MyInventorySlot, const FInventorySlot& TargetInventorySlot,
	                     int32 MyIndex, int32 TargetIndex, UInventoryComponent* TargetInventory) const;

	// ---------------------------------------------------------------------------------
	// Client entry points — call these from UI / actors on the LOCAL PLAYER'S component.
	// They run directly with authority, or route a Server RPC otherwise.
	// ---------------------------------------------------------------------------------

	/** Pick up a world item actor (must implement IWorldItemInterface). */
	UFUNCTION(BlueprintCallable, Category = "Inventory|Actions")
	void RequestPickupItem(AActor* WorldItemActor);

	UFUNCTION(BlueprintCallable, Category = "Inventory|Actions")
	void RequestDropItem(int32 InventoryIndex);

	/** Swap/merge between any two inventories (either may be this component, a chest, etc.). */
	UFUNCTION(BlueprintCallable, Category = "Inventory|Actions")
	void RequestSlotSwap(UInventoryComponent* SourceInventory, int32 SourceIndex,
	                     UInventoryComponent* DestInventory, int32 DestIndex);

	UFUNCTION(BlueprintCallable, Category = "Inventory|Actions")
	void RequestSplitSlotStack(UInventoryComponent* Inventory, FInventorySlot SplittingItemSlot, int32 SplitQuantity);

	// ---------------------------------------------------------------------------------
	// Authority operations (parity signatures; run on server)
	// ---------------------------------------------------------------------------------

	/**
	 * PickupItem parity. Takes the payload from the world actor via IWorldItemInterface,
	 * runs the four stacking cases, and returns the overflow quantity left on the ground
	 * (0 = fully picked up; the actor is told to update or destroy itself).
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory|Actions", meta = (ReturnDisplayName = "OverflowQuantity"))
	int32 PickupItem(AActor* WorldItemActor);

	/** Core pickup logic when you already hold the payload (used by PickupItem; exposed for native callers). */
	int32 PickupItemInstance(const FItemInstance& WorldInstance, const TArray<FItemInstance>& AttachedInstances, bool& AnyTaken);

	/** DropItem parity: clear the slot, move the instance (+ attachment closure) onto a spawned world actor. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory|Actions")
	void DropItem(int32 InventoryIndex);

	/**
	 * SwapSlots parity (sanitized semantics — see notes): swap or merge between
	 * SourceInventory[SourceIndex] and this[DestIndex]. Cross-component swaps move the
	 * instances (and their attachment closures) between stores.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory|Actions", meta = (ReturnDisplayName = "Swapped"))
	bool SwapSlots(UInventoryComponent* SourceInventory, int32 SourceIndex, int32 DestIndex);

	/** MergeItems parity: merge MyItemSlot's stack into TargetItemSlot (both on this component). */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory|Actions", meta = (ReturnDisplayName = "Merged"))
	bool MergeItems(const FInventorySlot& MyItemSlot, const FInventorySlot& TargetItemSlot);

	/** SplitSlotStack parity: move SplitQuantity into the next empty slot as a new stack. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory|Actions")
	void SplitSlotStack(FInventorySlot SplittingItemSlot, int32 SplitQuantity);

	/** SwapItemFromInventory parity: spawn a world item of ItemClass at ItemActor's transform carrying ItemInstance. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory|Actions")
	AActor* SwapItemFromInventory(TSubclassOf<AActor> InventoryItemClass, AActor* InItemActor, const FItemInstance& ItemInstance);

	// ---------------------------------------------------------------------------------
	// Setup / persistence
	// ---------------------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory|Setup")
	void InitializeInventory();

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory|Setup")
	void FillDefaultInventory();

	/** Snapshot the full state (instances + slots) into a record you can put in a USaveGame. */
	UFUNCTION(BlueprintCallable, Category = "Inventory|Persistence")
	void SaveToRecord(FInventoryRecord& OutRecord) const;

	/** Restore a snapshot (server only). Replaces current contents and broadcasts a full refresh. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory|Persistence")
	void LoadFromRecord(const FInventoryRecord& Record);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory|Persistence")
	void ClearInventory();

	// ---------------------------------------------------------------------------------
	// Main widget ref (parity, restructured)
	// Old BP walked Owner -> BPC_Interactable -> GetMainWidgetRef (a Blueprint-only
	// chain C++ cannot type). The cache below preserves the capability: the UI (or the
	// character on widget creation) registers the widget once via SetMainWidgetRef.
	// ---------------------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Inventory|UI")
	void SetMainWidgetRef(UUserWidget* Widget);

	UFUNCTION(BlueprintPure, Category = "Inventory|UI")
	UUserWidget* GetMainWidgetRefFromOwner() const;

	// ---------------------------------------------------------------------------------
	// Debug (parity)
	// ---------------------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Inventory|Debug")
	void LogInventory(float PrintDuration = 20.f) const;

	UFUNCTION(BlueprintCallable, Category = "Inventory|Debug")
	void LogInventorySlot(const FInventorySlot& InventorySlot, float Duration = 5.f) const;

	// --- Fast array plumbing (called by FInventoryItemList replication callbacks) ---
	void HandleReplicatedItemsChanged(const TArray<FGuid>& ChangedIds);
	void HandleReplicatedItemsRebuilt();
	
	
	/** 
	 * Authority: overwrite a stored instance (used by systems like weapons to write
	 * ammo/durability back). Broadcasts the owning slot's change. 
	*/
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory|Items")
	bool SetItemInstance(FGuid ItemID, const FItemInstance& NewInstance);
 
	/** Total quantity across all slotted stacks whose RowName matches (ammo pool read). */
	UFUNCTION(BlueprintPure, Category = "Inventory|Items")
	int32 GetTotalQuantityByRowName(FName RowName) const;
 
	/**
	 * Authority: consume up to Amount across stacks of RowName — first stack found,
	 * then the next, and so on; emptied stacks are removed from their slots.
	 * Returns the amount actually consumed (may be less than Amount).
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory|Items")
	int32 ConsumeItemsByRowName(FName RowName, int32 Amount);

	// =================================================================================
	//  Fragments
	// =================================================================================

	/** Read a fragment off a stored item. Null when the item or the fragment is absent. */
	template<typename T>
	const T* FindFragment(FGuid ItemID) const
	{
		FItemInstance Instance;
		return GetItem(ItemID, Instance) ? Instance.GetFragment<T>() : nullptr;
	}

	/**
	 * Read-modify-write a fragment in one step: fetch, mutate, push back, replicate.
	 *
	 * This is the ONLY correct way to change fragment state from C++. GetItem hands back
	 * a COPY, so editing that copy and dropping it does nothing - a mistake that silently
	 * discarded every ammo decrement until it was caught.
	 *
	 *   Inv->ModifyFragment<FWeaponItemFragment>(Id, [](FWeaponItemFragment& S)
	 *   {
	 *       S.AmmoCount.AmmoInMag--;
	 *   });
	 */
	template<typename T>
	bool ModifyFragment(FGuid ItemID, TFunctionRef<void(T&)> Mutator)
	{
		if (!HasAuthority())
		{
			return false;
		}

		FItemInstance Instance;
		if (!GetItem(ItemID, Instance))
		{
			return false;
		}

		T* Fragment = Instance.GetMutableFragment<T>();
		if (!Fragment)
		{
			return false;
		}

		Mutator(*Fragment);
		return SetItemInstance(ItemID, Instance);
	}

	/** Ids of every stored item carrying a fragment of the given type. */
	UFUNCTION(BlueprintPure, Category = "Inventory|Fragments")
	TArray<FGuid> FindItemsWithFragment(
		UPARAM(meta = (MetaStruct = "/Script/tps_test.ItemFragment")) UScriptStruct* FragmentType) const;

	/**
	 * Blueprint: read a fragment from a stored item. The wildcard pin's struct type IS the
	 * query - connect a FWeaponItemFragment variable and you get the weapon fragment. One
	 * node covers every fragment type you will ever add.
	 */
	UFUNCTION(BlueprintCallable, CustomThunk, Category = "Inventory|Fragments",
		meta = (CustomStructureParam = "OutFragment", DisplayName = "Get Item Fragment By Id"))
	bool GetItemFragmentById(FGuid ItemID, int32& OutFragment) const;

	/** Blueprint: write a fragment onto a stored item and replicate it. Authority only. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, CustomThunk, Category = "Inventory|Fragments",
		meta = (CustomStructureParam = "InFragment", DisplayName = "Set Item Fragment By Id"))
	bool SetItemFragmentById(FGuid ItemID, const int32& InFragment);

	DECLARE_FUNCTION(execGetItemFragmentById)
	{
		P_GET_STRUCT(FGuid, ItemID);
		Stack.StepCompiledIn<FStructProperty>(nullptr);
		void* OutPtr = Stack.MostRecentPropertyAddress;
		FStructProperty* OutProp = CastField<FStructProperty>(Stack.MostRecentProperty);
		P_FINISH;
		bool bResult = false;
		P_NATIVE_BEGIN;
		FItemInstance Instance;
		bResult = P_THIS->GetItem(ItemID, Instance)
			&& UItemFragmentLibrary::Generic_GetFragment(Instance.Fragments, OutProp, OutPtr);
		P_NATIVE_END;
		*static_cast<bool*>(RESULT_PARAM) = bResult;
	}

	DECLARE_FUNCTION(execSetItemFragmentById)
	{
		P_GET_STRUCT(FGuid, ItemID);
		Stack.StepCompiledIn<FStructProperty>(nullptr);
		const void* InPtr = Stack.MostRecentPropertyAddress;
		FStructProperty* InProp = CastField<FStructProperty>(Stack.MostRecentProperty);
		P_FINISH;
		bool bResult = false;
		P_NATIVE_BEGIN;
		bResult = P_THIS->SetFragmentInternal(ItemID, InProp, InPtr);
		P_NATIVE_END;
		*static_cast<bool*>(RESULT_PARAM) = bResult;
	}

protected:
	// ---------------------------------------------------------------------------------
	// Replicated data
	// ---------------------------------------------------------------------------------

	UPROPERTY(Replicated)
	FInventoryItemList InventoryList;

	UPROPERTY(ReplicatedUsing = OnRep_Slots)
	TArray<FInventorySlot> Slots;

	UFUNCTION()
	void OnRep_Slots(const TArray<FInventorySlot>& PreviousSlots);

	/** Transient Guid -> InventoryList.Entries index. Rebuilt after mutations/replication. */
	TMap<FGuid, int32> IdToIndex;

	/** Cached main widget (see SetMainWidgetRef). Not replicated — UI is client-local. */
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> MainWidgetRef = nullptr;

	/** Merge SourceSlot's stack into DestSlot across (possibly different) components. */
	bool MergeAcross(UInventoryComponent* SourceInv, const FInventorySlot& SourceSlot,
	                 UInventoryComponent* DestInv, const FInventorySlot& DestSlot) const;

	// ---------------------------------------------------------------------------------
	// Server RPC carriers (routed through player-owned components)
	// ---------------------------------------------------------------------------------

	UFUNCTION(Server, Reliable)
	void Server_PickupItem(AActor* WorldItemActor);

	UFUNCTION(Server, Reliable)
	void Server_DropItem(int32 InventoryIndex);

	UFUNCTION(Server, Reliable)
	void Server_SlotSwap(UInventoryComponent* SourceInventory, int32 SourceIndex,
	                     UInventoryComponent* DestInventory, int32 DestIndex);

	UFUNCTION(Server, Reliable)
	void Server_SplitSlotStack(UInventoryComponent* Inventory, FInventorySlot SplittingItemSlot, int32 SplitQuantity);

	// ---------------------------------------------------------------------------------
	// Internal store ops (the absorbed GI mutation functions; server-side)
	// ---------------------------------------------------------------------------------

	/** RegisterItem parity: stamps a FRESH Guid and adds. Returns the stamped instance. */
	FItemInstance RegisterNewItem(const FItemInstance& ItemInstance);

	/** Transfer-preserving add: keeps the incoming UUID (used by pickup/moves so attachment links survive). */
	bool AddExistingItem(const FItemInstance& ItemInstance);

	/** UpdateItem parity: overwrite the stored instance for this Guid (must exist). */
	bool UpdateItem(FGuid ItemID, const FItemInstance& ItemInstance);

	/** RemoveItem parity: erase from the store. */
	bool RemoveItem(FGuid ItemID);

	/**
	 * DuplicateItem parity+fix: copies under a new Guid. Fixes the BP quirk where the
	 * copy kept the original's UUID field, and deep-copies the attachment closure so
	 * duplicates don't share attachment instances.
	 */
	bool DuplicateItem(FGuid MyItemID, FGuid& DuplicatedItemID);

	/** CreateAndRegisterItem parity: build from row (or template) and register under a fresh Guid. */
	bool CreateAndRegisterItem(FName RowName, int32 Quantity, float Durability,
	                           const FItemInstance& Template, FItemInstance& OutInstance, FGuid& OutItemID);

	/** BuildItemInstance: the construction half of CreateAndRegisterItem (no registration). */
	bool BuildItemInstance(FName RowName, int32 Quantity, float Durability,
	                       const FItemInstance& Template, FItemInstance& OutInstance) const;

	// ---------------------------------------------------------------------------------
	// Internal fused writers (the old private "...InInventoryAndDatabase" trio)
	// ---------------------------------------------------------------------------------

	bool CreateItemInSlot(int32 SlotIndex, ESlotType InSlotType, FName RowName, int32 Quantity,
	                      float Durability, const FItemInstance& Template,
	                      FItemInstance& OutInstance, FGuid& OutItemID);

	bool UpdateItemInSlot(const FInventorySlot& ItemSlot, const FItemInstance& ItemInstance);

	bool RemoveItemFromSlot(const FInventorySlot& InventorySlotToBeRemoved);

	/** Write only the ItemID onto the slot at Index (SetSlot parity — preserves SlotIndex/SlotType). */
	void SetSlot(int32 Index, const FInventorySlot& Slot);

	// ---------------------------------------------------------------------------------
	// Closure helpers (attachment graph)
	// ---------------------------------------------------------------------------------

	/** Root instance + every instance reachable through WeaponAttachments (recursive), from this store. */
	void CollectClosure(const FItemInstance& Root, TArray<FItemInstance>& OutClosure) const;

	/** Remove an instance and its attachment closure from the store. */
	void RemoveInstanceAndClosure(const FItemInstance& Root);

	/** Add instances preserving their UUIDs (transfer receive side). */
	void AddInstancesPreservingIds(const TArray<FItemInstance>& Instances);

	// ---------------------------------------------------------------------------------
	// Misc internals
	// ---------------------------------------------------------------------------------

	void RebuildIdCache();
	void MarkItemDirtyById(FGuid ItemID);
	/** Force the replicated Slots array to replicate after a direct write. */
	void MarkSlotsDirty();
	void BroadcastSlotChangedForItem(FGuid ItemID);
	void BroadcastAllSlots();
	int32 FindSlotIndexByItemId(FGuid ItemID) const;
	void RefreshIsInventoryFull();

	/** Owner's playable avatar: Character owner -> itself; PlayerState/Controller owner -> pawn. */
	AActor* ResolveOwnerAvatar() const;

	/** Spawn a world item actor carrying Instance (+closure) via IWorldItemInterface. */
	AActor* SpawnWorldItem(TSubclassOf<AActor> ItemClass, const FTransform& Transform,
	                       FItemInstance Instance, const TArray<FItemInstance>& AttachedInstances) const;

	/** SpawnItemInFrontOfPlayer parity (spawns at the owner avatar's transform). */
	AActor* SpawnItemInFrontOfPlayer(TSubclassOf<AActor> ItemClass, const FItemInstance& Instance,
	                                 const TArray<FItemInstance>& AttachedInstances);

	/** Definition lookups (via UItemLibrary settings / tables). */
	static bool GetItemStructForRow(FName RowName, FItemStructure& OutStruct);
	static bool GetWeaponRowForName(FName RowName, struct FWeaponRow& OutRow);

	static bool AreModifierListsEqual(const TArray<FModifierPayload>& A, const TArray<FModifierPayload>& B);

	bool HasAuthority() const;

	/** On-screen + log debug message (so failures are visible without the Output Log open). */
	static void ScreenLog(const FString& Message, bool bIsError = true);
	
	/** Durability now lives in a FDurabilityFragment. Returns -1 when the item has none,
	matching the sentinel the create/build functions already use. */
	static float GetInstanceDurability(const FItemInstance& Instance);
 
	/** Write a durability value, adding the fragment if the definition didn't author one. */
	static void SetInstanceDurability(FItemInstance& Instance, float NewDurability);

	/** Shared by the Set Item Fragment By Id thunk: fetch, write, push back through the store. */
	bool SetFragmentInternal(FGuid ItemID, FStructProperty* InProp, const void* InPtr);
	
	UInventoryComponent* GetRoutingComponent();
};