// Copyright arubaii. All rights reserved.

#include "Item/Component/InventoryComponent.h"

#include "Item/ItemLibrary.h"
#include "Item/Structs/Rows.h"
#include "Item/Fragment/ItemFragment.h"
#include "Item/Fragment/ItemFragmentLibrary.h"
#include "Item/WorldItemInterface.h"
#include "Utils/BlueprintMacroLibraries/ArrayDifference.h"

#include "Blueprint/UserWidget.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/PlayerController.h"

DEFINE_LOG_CATEGORY_STATIC(LogInventoryComponent, Log, All);

void UInventoryComponent::ScreenLog(const FString& Message, bool bIsError)
{
	// Always log; also print to screen so failures are visible without the Output Log open.
	if (bIsError)
	{
		UE_LOG(LogInventoryComponent, Warning, TEXT("%s"), *Message);
	}
	else
		UE_LOG(LogInventoryComponent, Log, TEXT("%s"), *Message);

	if (GEngine)
		GEngine->AddOnScreenDebugMessage(-1, 6.f, bIsError ? FColor::Red : FColor::Green, Message);
}

// =====================================================================================
//  Fast array replication callbacks (client side)
// =====================================================================================

void FInventoryItemList::PostReplicatedAdd(const TArrayView<int32>& AddedIndices, int32 /*FinalSize*/)
{
	if (!OwnerComponent) return;
	TArray<FGuid> ids;
	for (int32 index : AddedIndices)
	{
		if (Entries.IsValidIndex(index))
			ids.Add(Entries[index].Instance.UUID);
	}
	OwnerComponent->HandleReplicatedItemsChanged(ids);
}

void FInventoryItemList::PostReplicatedChange(const TArrayView<int32>& ChangedIndices, int32 /*FinalSize*/)
{
	if (!OwnerComponent) return;
	TArray<FGuid> ids;
	for (int32 index : ChangedIndices)
	{
		if (Entries.IsValidIndex(index))
			ids.Add(Entries[index].Instance.UUID);
	}
	OwnerComponent->HandleReplicatedItemsChanged(ids);
}

void FInventoryItemList::PreReplicatedRemove(const TArrayView<int32>& RemovedIndices, int32 /*FinalSize*/)
{
	if (!OwnerComponent) return;
	TArray<FGuid> ids;
	for (int32 index : RemovedIndices)
	{
		if (Entries.IsValidIndex(index))
			ids.Add(Entries[index].Instance.UUID);
	}
	// Broadcast affected slots while the entries are still present, then let the
	// owner rebuild its cache after the removal completes.
	OwnerComponent->HandleReplicatedItemsChanged(ids);
	OwnerComponent->HandleReplicatedItemsRebuilt();
}

// =====================================================================================
//  Construction / lifecycle / replication registration
// =====================================================================================

UInventoryComponent::UInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	InventoryList.OwnerComponent = this;
}

void UInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// COND_None: replicate to all relevant connections. Correct for co-op and required
	// for shared containers (a chest has no owning connection, so OwnerOnly there would
	// reach nobody). ReplicationScope is reserved for a future privacy pass.
	DOREPLIFETIME(UInventoryComponent, InventoryList);
	DOREPLIFETIME(UInventoryComponent, Slots);
	DOREPLIFETIME(UInventoryComponent, InventoryName);
}

void UInventoryComponent::BeginPlay()
{
	Super::BeginPlay();

	InventoryList.OwnerComponent = this;
	ResolveOwnerAvatar(); // caches Character when resolvable

	// Event graph parity: BeginPlay -> InitializeInventory -> (ShouldFillDefaultInventory ? FillDefaultInventory).
	// Server only: clients receive the result through replication.
	if (HasAuthority())
	{
		InitializeInventory();
		if (ShouldFillDefaultInventory)
			FillDefaultInventory();
	}
	
	// Event graph parity: BeginPlay -> InitializeInventory -> (ShouldFillDefaultInventory ? FillDefaultInventory).
	// Server only: clients receive the result through replication.
	UE_LOG(LogInventoryComponent, Warning, TEXT("[InventoryComponent] BeginPlay on '%s': HasAuthority=%s, NetMode=%d"),
		*GetName(), HasAuthority() ? TEXT("true") : TEXT("false"), (int32)GetOwner()->GetNetMode());

	if (HasAuthority())
	{
		InitializeInventory();
		UE_LOG(LogInventoryComponent, Warning, TEXT("[InventoryComponent] '%s': InitializeInventory done, Slots.Num()=%d, ShouldFillDefaultInventory=%s"),
			*GetName(), Slots.Num(), ShouldFillDefaultInventory ? TEXT("true") : TEXT("false"));

		if (ShouldFillDefaultInventory)
		{
			FillDefaultInventory();
			UE_LOG(LogInventoryComponent, Warning, TEXT("[InventoryComponent] '%s': FillDefaultInventory done."), *GetName());
		}
	}
}

bool UInventoryComponent::HasAuthority() const
{
	const AActor* owner = GetOwner();
	return owner && owner->HasAuthority();
}

// =====================================================================================
//  Instance store — reads (absorbed GI read layer)
// =====================================================================================

bool UInventoryComponent::GetItem(FGuid ItemID, FItemInstance& ItemInstance) const
{
	if (!ItemID.IsValid())
	{
		ItemInstance = FItemInstance();
		return false;
	}

	// Cache hit path.
	if (const int32* found = IdToIndex.Find(ItemID))
	{
		if (InventoryList.Entries.IsValidIndex(*found) && InventoryList.Entries[*found].Instance.UUID == ItemID)
		{
			ItemInstance = InventoryList.Entries[*found].Instance;
			return true;
		}
	}

	// Fallback linear scan (covers cache staleness windows during replication).
	for (const FInventoryItemEntry& entry : InventoryList.Entries)
	{
		if (entry.Instance.UUID == ItemID)
		{
			ItemInstance = entry.Instance;
			return true;
		}
	}

	ItemInstance = FItemInstance();
	return false;
}

bool UInventoryComponent::GetItemStructByID(FGuid ItemID, FItemDefinition& ItemDef) const
{
	// Parity: Guid -> instance -> RowName -> UItemLibrary definition lookup.
	FItemInstance instance;
	if (!GetItem(ItemID, instance))
	{
		ItemDef = FItemDefinition();
		return false;
	}
	return GetItemStructForRow(instance.RowName, ItemDef);
}

bool UInventoryComponent::ContainsItem(FGuid ItemID) const
{
	FItemInstance dummy;
	return GetItem(ItemID, dummy);
}

bool UInventoryComponent::IsValidItemID(FGuid ItemID) const
{
	// Parity: IsValid_Guid AND Map_Contains.
	return ItemID.IsValid() && ContainsItem(ItemID);
}

// =====================================================================================
//  Instance store — mutations (absorbed GI mutation layer; server side)
// =====================================================================================

FItemInstance UInventoryComponent::RegisterNewItem(const FItemInstance& ItemInstance)
{
	// RegisterItem parity: ALWAYS stamps a fresh Guid (the BP version generated a new
	// Guid for every genuinely-new registration).
	FItemInstance stamped = ItemInstance;
	stamped.UUID = FGuid::NewGuid();

	const int32 newIndex = InventoryList.Entries.Add(FInventoryItemEntry(stamped));
	InventoryList.MarkItemDirty(InventoryList.Entries[newIndex]);
	IdToIndex.Add(stamped.UUID, newIndex);
	return stamped;
}

bool UInventoryComponent::AddExistingItem(const FItemInstance& ItemInstance)
{
	// Transfer-preserving add: keeps the incoming UUID so Guid links (a weapon's
	// fragment links) survive moves between world/actor/components. This is the
	// behavior the old single-global-database model got implicitly; per-component
	// stores need it explicitly.
	if (!ItemInstance.UUID.IsValid())
		return false;

	// Idempotent: if it already exists, overwrite (keeps transfers safe to retry).
	if (ContainsItem(ItemInstance.UUID))
		return UpdateItem(ItemInstance.UUID, ItemInstance);

	const int32 newIndex = InventoryList.Entries.Add(FInventoryItemEntry(ItemInstance));
	InventoryList.MarkItemDirty(InventoryList.Entries[newIndex]);
	IdToIndex.Add(ItemInstance.UUID, newIndex);
	return true;
}

bool UInventoryComponent::UpdateItem(FGuid ItemID, const FItemInstance& ItemInstance)
{
	// Parity: overwrite iff present.
	for (int32 i = 0; i < InventoryList.Entries.Num(); ++i)
	{
		if (InventoryList.Entries[i].Instance.UUID == ItemID)
		{
			InventoryList.Entries[i].Instance = ItemInstance;
			InventoryList.Entries[i].Instance.UUID = ItemID; // key and field stay in agreement
			InventoryList.MarkItemDirty(InventoryList.Entries[i]);
			return true;
		}
	}
	return false;
}

bool UInventoryComponent::RemoveItem(FGuid ItemID)
{
	for (int32 i = 0; i < InventoryList.Entries.Num(); ++i)
	{
		if (InventoryList.Entries[i].Instance.UUID == ItemID)
		{
			InventoryList.Entries.RemoveAt(i);
			InventoryList.MarkArrayDirty();
			RebuildIdCache();
			return true;
		}
	}
	return false;
}

bool UInventoryComponent::DuplicateItem(FGuid MyItemID, FGuid& DuplicatedItemID)
{
	FItemInstance source;
	if (!GetItem(MyItemID, source))
	{
		DuplicatedItemID = FGuid();
		return false;
	}

	// Parity+fix: the BP copy kept the ORIGINAL UUID inside the duplicated struct and
	// shared attachment instances by Guid. Here the copy gets its own Guid and the
	// attachment closure is deep-copied so duplicates are fully independent.
	FItemInstance copy = source;
	copy.UUID = FGuid::NewGuid();

	// Deep-copy whatever the fragments reference, then let each fragment rewrite its own
	// links to point at the copies. Generic: no knowledge of attachments here.
	TArray<FGuid> sourceRefs;
	source.GatherReferencedItemIds(sourceRefs);

	TMap<FGuid, FGuid> oldToNew;
	for (const FGuid& oldId : sourceRefs)
	{
		FItemInstance refSource;
		if (GetItem(oldId, refSource))
		{
			FItemInstance refCopy = refSource;
			refCopy.UUID = FGuid::NewGuid();
			refCopy.Fragments.Empty(); // one level deep matches the data model today
			AddExistingItem(refCopy);
			oldToNew.Add(oldId, refCopy.UUID);
		}
	}
	copy.RemapReferencedItemIds(oldToNew);

	AddExistingItem(copy);
	DuplicatedItemID = copy.UUID;
	return true;
}

// =====================================================================================
//  Fragments
// =====================================================================================

TArray<FGuid> UInventoryComponent::FindItemsWithFragment(UScriptStruct* FragmentType) const
{
	TArray<FGuid> result;
	if (!FragmentType)
		return result;

	for (const FInventoryItemEntry& entry : InventoryList.Entries)
	{
		if (UItemFragmentLibrary::HasItemFragment(entry.Instance, FragmentType))
			result.Add(entry.Instance.UUID);
	}
	return result;
}

bool UInventoryComponent::SetFragmentInternal(FGuid ItemID, FStructProperty* InProp, const void* InPtr)
{
	if (!HasAuthority())
	{
		ScreenLog(TEXT("[UInventoryComponent::SetItemFragmentById] Called without authority."));
		return false;
	}

	FItemInstance instance;
	if (!GetItem(ItemID, instance))
		return false;

	if (!UItemFragmentLibrary::Generic_SetFragment(instance, InProp, InPtr))
		return false;

	// Push the mutated copy back through the store so it replicates.
	return SetItemInstance(ItemID, instance);
}

float UInventoryComponent::GetInstanceDurability(const FItemInstance& Instance)
{
	if (const FDurabilityFragment* fragment = Instance.GetFragment<FDurabilityFragment>())
		return fragment->Durability;

	return -1.f;
}

void UInventoryComponent::SetInstanceDurability(FItemInstance& Instance, float NewDurability)
{
	if (FDurabilityFragment* fragment = Instance.GetMutableFragment<FDurabilityFragment>())
	{
		fragment->Durability = NewDurability;
		return;
	}

	// The definition authored no durability fragment but a caller supplied a value: add
	// one rather than silently dropping it.
	FDurabilityFragment newFragment;
	newFragment.Durability = NewDurability;
	Instance.Fragments.Add(FInstancedStruct::Make(newFragment));
}

bool UInventoryComponent::BuildItemInstance(FName RowName, int32 Quantity, float Durability,
                                            const FItemInstance& Template, FItemInstance& OutInstance) const
{
	// CreateAndRegisterItem parity, construction half.
	const bool bHasTemplate = Template.RowName != NAME_None;
	const FName effectiveRow = (RowName != NAME_None) ? RowName : Template.RowName;

	if (bHasTemplate)
	{
		OutInstance = Template;
		// Parity: -1 sentinels mean "keep template value".
		if (Quantity != -1)
			OutInstance.Quantity = Quantity;
		if (Durability != -1.f)
			SetInstanceDurability(OutInstance, Durability);
	}
	else
	{
		FItemDefinition def;
		if (!GetItemStructForRow(effectiveRow, def))
		{
			ScreenLog(FString::Printf(
				TEXT(
					"[UInventoryComponent::BuildItemInstance] Could not find item definition for row name '%s' (check Item Tables prefix mapping and DataTable row struct)."),
				*effectiveRow.ToString()));
			OutInstance = FItemInstance();
			return false;
		}
		OutInstance = FItemInstance();
		OutInstance.RowName = effectiveRow;
		OutInstance.Quantity = Quantity;
		OutInstance.IsWorldItem = false;

		// Domain state (weapon, armour, quest, durability...) comes from the definition's
		// authored fragments. The inventory never learns what any of them mean.
		OutInstance.Fragments = def.DefaultFragments;

		if (Durability != -1.f)
			SetInstanceDurability(OutInstance, Durability);
	}

	return true;
}

bool UInventoryComponent::CreateAndRegisterItem(FName RowName, int32 Quantity, float Durability,
                                                const FItemInstance& Template, FItemInstance& OutInstance,
                                                FGuid& OutItemID)
{
	FItemInstance built;
	if (!BuildItemInstance(RowName, Quantity, Durability, Template, built))
	{
		OutInstance = FItemInstance();
		OutItemID = FGuid();
		return false;
	}

	OutInstance = RegisterNewItem(built);
	OutItemID = OutInstance.UUID;
	return true;
}

// =====================================================================================
//  Slot reads
// =====================================================================================

bool UInventoryComponent::GetItemByIndex(int32 Index, FItemInstance& OutItem) const
{
	const FInventorySlot Slot = GetSlot(Index);
	if (!Slot.ItemID.IsValid())
	{
		OutItem = FItemInstance();
		return false;
	}
	return GetItem(Slot.ItemID, OutItem);
}

FInventorySlot UInventoryComponent::GetSlot(int32 Index) const
{
	// Parity: valid index -> the slot; otherwise a default slot.
	return Slots.IsValidIndex(Index) ? Slots[Index] : FInventorySlot();
}

TArray<FInventorySlot> UInventoryComponent::GetSlots() const
{
	return Slots;
}

bool UInventoryComponent::GetNextEmptySlot(FInventorySlot& Slot) const
{
	// Parity: first slot whose ItemID is invalid. Return value is "Inventory Full?".
	for (const FInventorySlot& candidate : Slots)
	{
		if (!candidate.ItemID.IsValid())
		{
			Slot = candidate;
			return false;
		}
	}
	Slot = FInventorySlot();
	return true;
}

bool UInventoryComponent::GetNextCompatibleSlot(const FItemInstance& ItemToBeCheckedAgainst, FInventorySlot& Slot) const
{
	// PASS 1: a slot already holding the same item with room to stack
	if (ItemToBeCheckedAgainst.HasFragment<FStackableFragment>())
	// Skip pass if the item isn't stackable
	{
		for (const FInventorySlot& candidate : Slots)
		{
			FItemInstance slotInstance;
			if (GetItem(candidate.ItemID, slotInstance))
			{
				
				if (AreItemsTheSame(ItemToBeCheckedAgainst, slotInstance) && !IsItemSizeMax(slotInstance))
				{
					Slot = candidate;
					return true;
				}
			}
		}
	}

	// PASS 2: an empty slot whose SlotType is in the item's CompatibleSlotTypes.
	FItemDefinition def;
	if (GetItemStructForRow(ItemToBeCheckedAgainst.RowName, def))
	{
		for (const FInventorySlot& candidate : Slots)
		{
			FItemInstance slotInstance;
			const bool bEmpty = !GetItem(candidate.ItemID, slotInstance);
			if (bEmpty && def.CompatibleSlotTypes.Contains(candidate.SlotType))
			{
				Slot = candidate;
				return true;
			}
		}
	}

	Slot = FInventorySlot();
	return false;
}

bool UInventoryComponent::AreSlotsCompatible(const FInventorySlot& MyInventorySlot,
                                             const FInventorySlot& TargetInventorySlot) const
{
	// Parity: does MY slot's item accept the TARGET slot's type?
	FItemDefinition def;
	if (!GetItemStructByID(MyInventorySlot.ItemID, def))
		return false;
	return def.CompatibleSlotTypes.Contains(TargetInventorySlot.SlotType);
}

bool UInventoryComponent::AreItemsTheSame(const FItemInstance& MyItem, const FItemInstance& TargetItem) const
{
	if (MyItem.RowName != TargetItem.RowName)
		return false;

	// Are their fragment arrays identical?
	if (UArrayLibrary::ArraySymmetricDifference(MyItem.Fragments, TargetItem.Fragments).Num() > 0)
		return false;
	
	// Usually stackable items won't have a durability anyway
	if (MyItem.HasFragment<FDurabilityFragment>())
		return GetInstanceDurability(MyItem) == GetInstanceDurability(TargetItem);
	
	return true;
}

bool UInventoryComponent::IsItemSizeMax(const FItemInstance& ItemInstance) const
{
	if (!ItemInstance.HasFragment<FStackableFragment>())
		return true;
	
	int maxStack = ItemInstance.GetFragment<FStackableFragment>()->MaxStackSize;
	return ItemInstance.Quantity >= maxStack;
}

bool UInventoryComponent::IsValidSlotSwap(const FInventorySlot& MyInventorySlot,
                                          const FInventorySlot& TargetInventorySlot,
                                          int32 MyIndex, int32 TargetIndex, UInventoryComponent* SourceInventory) const
{
	// Semantics (matching SwapSlots' call):
	//   MyInventorySlot     = the SOURCE slot (dragged item), living in SourceInventory.
	//   TargetInventorySlot = the DEST slot (this component).
	//   SourceInventory     = the component the dragged item came from (may be 'this').
	const UInventoryComponent* srcInv = SourceInventory ? SourceInventory : this;

	// Reject dropping a slot onto itself.
	const bool bSameSlot = (srcInv == this) && (MyIndex == TargetIndex);
	if (bSameSlot)
		return false;

	// The dragged item's definition (resolved from the SOURCE component's store).
	FItemDefinition myDef;
	if (!srcInv->GetItemStructByID(MyInventorySlot.ItemID, myDef))
	{
		ScreenLog(FString::Printf(
			TEXT("[IsValidSlotSwap] Rejected: could not resolve dragged item (ID %s) in source inventory."),
			*MyInventorySlot.ItemID.ToString()));
		return false;
	}

	// The dragged item must accept the destination slot's type.
	if (!myDef.CompatibleSlotTypes.Contains(TargetInventorySlot.SlotType))
		return false;

	// If the destination slot is occupied, the displaced item must fit back into the
	// source slot (two-way swap). The displaced item lives in THIS (destination) store.
	if (TargetInventorySlot.ItemID.IsValid())
	{
		FItemDefinition targetDef;
		if (GetItemStructByID(TargetInventorySlot.ItemID, targetDef)
			&& !targetDef.CompatibleSlotTypes.Contains(MyInventorySlot.SlotType))
			return false;
	}
	
	return true;
}

// =====================================================================================
//  Client entry points -> Server RPCs
// =====================================================================================

void UInventoryComponent::RequestPickupItem(AActor* WorldItemActor)
{
	if (HasAuthority())
		PickupItem(WorldItemActor);
	else
		Server_PickupItem(WorldItemActor);
}

void UInventoryComponent::RequestDropItem(int32 InventoryIndex)
{
	if (HasAuthority())
		DropItem(InventoryIndex);
	else
		Server_DropItem(InventoryIndex);
}

void UInventoryComponent::RequestSlotSwap(UInventoryComponent* SourceInventory, int32 SourceIndex,
										  UInventoryComponent* DestInventory, int32 DestIndex)
{
	UInventoryComponent* source = SourceInventory ? SourceInventory : this;
	UInventoryComponent* dest = DestInventory ? DestInventory : this;

	if (HasAuthority())
		dest->SwapSlots(source, SourceIndex, DestIndex);
	else
		GetRoutingComponent()->Server_SlotSwap(source, SourceIndex, dest, DestIndex);
}

void UInventoryComponent::RequestSplitSlotStack(UInventoryComponent* Inventory, FInventorySlot SplittingItemSlot,
												int32 SplitQuantity)
{
	UInventoryComponent* target = Inventory ? Inventory : this;

	if (HasAuthority())
		target->SplitSlotStack(SplittingItemSlot, SplitQuantity);
	else
		GetRoutingComponent()->Server_SplitSlotStack(target, SplittingItemSlot, SplitQuantity);
}

void UInventoryComponent::Server_PickupItem_Implementation(AActor* WorldItemActor)
{
	PickupItem(WorldItemActor);
}

void UInventoryComponent::Server_DropItem_Implementation(int32 InventoryIndex)
{
	DropItem(InventoryIndex);
}

void UInventoryComponent::Server_SlotSwap_Implementation(UInventoryComponent* SourceInventory, int32 SourceIndex,
                                                         UInventoryComponent* DestInventory, int32 DestIndex)
{
	UInventoryComponent* dest = DestInventory ? DestInventory : this;
	dest->SwapSlots(SourceInventory ? SourceInventory : this, SourceIndex, DestIndex);
}

void UInventoryComponent::Server_SplitSlotStack_Implementation(UInventoryComponent* Inventory,
                                                               FInventorySlot SplittingItemSlot, int32 SplitQuantity)
{
	UInventoryComponent* target = Inventory ? Inventory : this;
	target->SplitSlotStack(SplittingItemSlot, SplitQuantity);
}

// =====================================================================================
//  Pickup (parity: the four stacking cases; one slot per call, remainder = overflow)
// =====================================================================================

int32 UInventoryComponent::PickupItem(AActor* WorldItemActor)
{
	if (!HasAuthority())
	{
		ScreenLog(TEXT("[UInventoryComponent::PickupItem] Called without authority; use RequestPickupItem."));
		return -1;
	}
	if (!WorldItemActor)
		return -1;
	if (!WorldItemActor->GetClass()->ImplementsInterface(UWorldItemInterface::StaticClass()))
	{
		ScreenLog(FString::Printf(
			TEXT("[UInventoryComponent::PickupItem] Actor %s does not implement WorldItemInterface."),
			*WorldItemActor->GetName()));
		return -1;
	}

	const FItemInstance worldInstance = IWorldItemInterface::Execute_GetWorldItemInstance(WorldItemActor);
	const TArray<FItemInstance> attached = IWorldItemInterface::Execute_GetWorldItemAttachedInstances(WorldItemActor);

	ItemActor = WorldItemActor; // parity ref

	bool bAnyTaken = false;
	const int32 overflow = PickupItemInstance(worldInstance, attached, bAnyTaken);

	// Old model: the actor read the overflow and destroyed/updated itself (the
	// component's "Destroying Item" event handled database cleanup). New model: the
	// server drives the actor directly through the interface; the actor's replicated
	// instance carries the result to clients.
	if (bAnyTaken)
	{
		if (overflow <= 0)
			IWorldItemInterface::Execute_OnWorldItemFullyPickedUp(WorldItemActor);
		else
		{
			FItemInstance reduced = worldInstance;
			reduced.Quantity = overflow;
			IWorldItemInterface::Execute_SetWorldItemInstance(WorldItemActor, reduced);
		}
	}

	return overflow;
}

int32 UInventoryComponent::PickupItemInstance(const FItemInstance& WorldInstance,
                                              const TArray<FItemInstance>& AttachedInstances, bool& AnyTaken)
{
	AnyTaken = false;
	
	if (!WorldInstance.IsValid() || WorldInstance.Quantity < 1)
		return FMath::Max(WorldInstance.Quantity, 0);

	// Verify the incoming item's row resolves to a definition before proceeding.
	FItemDefinition incomingDef;
	if (!GetItemStructForRow(WorldInstance.RowName, incomingDef))
	{
		ScreenLog(FString::Printf(
			TEXT("[UInventoryComponent::PickupItemInstance] Row name '%s' not found"),
			*WorldInstance.RowName.ToString()));
		return WorldInstance.Quantity; // take nothing, leave it on the ground
	}

	const int32 pickupQuantity = WorldInstance.Quantity;
	
	FInventorySlot compatibleSlot;
	
	// Is the inventory full? 
	if (!GetNextCompatibleSlot(WorldInstance, compatibleSlot))
		return pickupQuantity;
	
	FItemInstance compatibleItem;
	const bool slotOccupied = GetItem(compatibleSlot.ItemID, compatibleItem);
	
	
	if (slotOccupied && AreItemsTheSame(WorldInstance, compatibleItem))
	{
		FItemDefinition def;
		if (!GetItemStructForRow(compatibleItem.RowName, def))
			return pickupQuantity;
		
		// SlotOccupied guarantees the existence of a stackable fragment since the compatible slot would be empty if this weren't the case
		const int32 compatibleMaxStack = compatibleItem.GetFragment<FStackableFragment>()->MaxStackSize; 
		const int32 compatibleQuantity = compatibleItem.Quantity;
		const int32 sum = pickupQuantity + compatibleQuantity;

		if (sum <= compatibleMaxStack)
		{
			// Case: fits entirely into the existing stack.
			compatibleItem.Quantity = sum;
			UpdateItemInSlot(compatibleSlot, compatibleItem);
			AnyTaken = true;
			OnItemPickup.Broadcast(WorldInstance, pickupQuantity);
			return 0;
		}

		// Case: fills the existing stack to max; remainder returns to the ground.
		const int32 toBeAdded = compatibleMaxStack - compatibleQuantity;
		compatibleItem.Quantity = compatibleMaxStack;
		UpdateItemInSlot(compatibleSlot, compatibleItem);

		const int32 overflow = pickupQuantity - toBeAdded;
		AnyTaken = toBeAdded > 0;
		if (AnyTaken)
			OnItemPickup.Broadcast(WorldInstance, toBeAdded);
		return overflow;
	}

	// ---- Empty-slot cases (pass 2 of GetNextCompatibleSlot guarantees compatibility) ----
	FItemDefinition pickupDef;
	if (!GetItemStructForRow(WorldInstance.RowName, pickupDef))
		return pickupQuantity;
	
	bool isStackable = WorldInstance.HasFragment<FStackableFragment>();
	
	const int32 pickupMaxStack = isStackable ? WorldInstance.GetFragment<FStackableFragment>()->MaxStackSize : 0;
	
	if (pickupQuantity <= pickupMaxStack || !isStackable)
	{
		// Case: whole pickup fits in the empty slot. TRANSFER the instance — keep its
		// UUID (parity with the BP writing the ground item's Guid into the slot) so the
		// attachment Guid links keep resolving; the closure travels with it.
		FItemInstance incoming = WorldInstance;
		incoming.IsWorldItem = false;
		AddExistingItem(incoming);
		AddInstancesPreservingIds(AttachedInstances);

		if (Slots.IsValidIndex(compatibleSlot.SlotIndex))
		{
			Slots[compatibleSlot.SlotIndex].ItemID = incoming.UUID; // SlotType preserved
			Slots[compatibleSlot.SlotIndex].bIsAnchor = true;
		}
		OnInventoryChanged.Broadcast(compatibleSlot.SlotIndex);
		RefreshIsInventoryFull();

		AnyTaken = true;
		OnItemPickup.Broadcast(incoming, pickupQuantity);

		return 0;
	}

	// Case: pickup exceeds one stack — create a NEW full stack (fresh Guid, parity with
	// CreateItemInInventoryAndDatabase) in the slot; the remainder stays on the ground.
	FItemInstance templateInstance = WorldInstance;
	templateInstance.Quantity = pickupMaxStack;
	templateInstance.IsWorldItem = false;

	FItemInstance created;
	FGuid createdId;
	CreateItemInSlot(compatibleSlot.SlotIndex, compatibleSlot.SlotType, WorldInstance.RowName,
	                 /*Quantity*/ -1, /*Durability*/ -1.f, templateInstance, created, createdId);

	AnyTaken = true;
	OnItemPickup.Broadcast(created, pickupMaxStack);
	return pickupQuantity - pickupMaxStack;
}

// =====================================================================================
//  Drop / spawn
// =====================================================================================

void UInventoryComponent::DropItem(int32 InventoryIndex)
{
	if (!HasAuthority())
	{
		ScreenLog(TEXT("[UInventoryComponent::DropItem] Called without authority; use RequestDropItem."));
		return;
	}

	// Parity: slot -> instance; RowName == None (no item) -> early out.
	const FInventorySlot slot = GetSlot(InventoryIndex);
	FItemInstance instance;
	if (!GetItem(slot.ItemID, instance) || instance.RowName == NAME_None)
		return;

	FItemDefinition def;
	if (!GetItemStructForRow(instance.RowName, def))
	{
		ScreenLog(FString::Printf(
			TEXT("[UInventoryComponent::DropItem] Could not find definition for row name '%s'."),
			*instance.RowName.ToString()));
		return;
	}

	// Gather the attachment closure BEFORE removing anything from the store.
	TArray<FItemInstance> closure;
	CollectClosure(instance, closure);
	TArray<FItemInstance> attached = closure;
	attached.RemoveAll([&instance](const FItemInstance& I) { return I.UUID == instance.UUID; });

	// Clear the slot. (Deviation from BP, documented: the BP re-made the slot with
	// SlotType=Generic, which would corrupt typed inventories; RemoveItemFromInventory-
	// AndDatabase preserved SlotType — this follows the correct one.)
	if (Slots.IsValidIndex(slot.SlotIndex))
	{
		Slots[slot.SlotIndex].ItemID.Invalidate();
		Slots[slot.SlotIndex].bIsAnchor = false;
	}

	// Spawn the world actor carrying the payload (instance + closure), then remove the
	// instances from this store — the item now lives on the actor. (Old model left the
	// instance in the global DB flagged IsWorldItem=true; the transfer is the
	// per-component equivalent.)
	FItemInstance worldInstance = instance;
	worldInstance.IsWorldItem = true;

	SpawnItemInFrontOfPlayer(def.Class, worldInstance, attached);
	RemoveInstanceAndClosure(instance);

	OnInventoryChanged.Broadcast(slot.SlotIndex);
	RefreshIsInventoryFull();
}

AActor* UInventoryComponent::SpawnItemInFrontOfPlayer(TSubclassOf<AActor> ItemClass, const FItemInstance& Instance,
                                                      const TArray<FItemInstance>& AttachedInstances)
{
	// Parity: spawned at the owner's transform (the BP used GetOwner()->GetTransform();
	// with the component on a PlayerState the avatar pawn is the equivalent anchor).
	const AActor* avatar = ResolveOwnerAvatar();
	const FTransform spawnTransform = avatar ? avatar->GetActorTransform() : FTransform::Identity;
	return SpawnWorldItem(ItemClass, spawnTransform, Instance, AttachedInstances);
}

AActor* UInventoryComponent::SwapItemFromInventory(TSubclassOf<AActor> InventoryItemClass, AActor* InItemActor,
                                                   const FItemInstance& ItemInstance)
{
	if (!HasAuthority() || !InItemActor)
		return nullptr;

	// Parity: spawn a replacement world item at the existing actor's transform with the
	// given instance (IsRuntimeSpawn was true on the BP spawn; the interface impl sets it).
	// The BP then fired a dispatcher on the Character — a Blueprint-only reference C++
	// cannot make; the caller receives the spawned actor and can raise any
	// character-side event itself.
	FItemInstance worldInstance = ItemInstance;
	worldInstance.IsWorldItem = true;

	TArray<FItemInstance> attached;
	FItemInstance storeCopy;
	if (GetItem(ItemInstance.UUID, storeCopy))
	{
		TArray<FItemInstance> closure;
		CollectClosure(storeCopy, closure);
		attached = closure;
		attached.RemoveAll([&ItemInstance](const FItemInstance& I) { return I.UUID == ItemInstance.UUID; });
	}

	return SpawnWorldItem(InventoryItemClass, InItemActor->GetActorTransform(), worldInstance, attached);
}

AActor* UInventoryComponent::SpawnWorldItem(TSubclassOf<AActor> ItemClass, const FTransform& Transform,
                                            FItemInstance Instance, const TArray<FItemInstance>& AttachedInstances) const 
{
	UWorld* world = GetWorld();
	if (!world || !*ItemClass)
	{
		ScreenLog(TEXT("[UInventoryComponent::SpawnWorldItem] Missing world or item class; cannot spawn."));
		return nullptr;
	}

	AActor* spawned = world->SpawnActorDeferred<AActor>(ItemClass, Transform, GetOwner(),
	                                                    Cast<APawn>(ResolveOwnerAvatar()),
	                                                    ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!spawned)
		return nullptr;

	if (spawned->GetClass()->ImplementsInterface(UWorldItemInterface::StaticClass()))
		IWorldItemInterface::Execute_SetWorldItemPayload(spawned, Instance, AttachedInstances);
	else
	{
		UE_LOG(LogInventoryComponent, Warning,
		       TEXT(
			       "[UInventoryComponent::SpawnWorldItem] Spawned actor %s does not implement WorldItemInterface; "
		  "payload not delivered."
		       ),
		       *spawned->GetName());
	}

	spawned->FinishSpawning(Transform);
	return spawned;
}

// =====================================================================================
//  Swap / merge / split
// =====================================================================================

bool UInventoryComponent::SwapSlots(UInventoryComponent* SourceInventory, int32 SourceIndex, int32 DestIndex)
{

	// =============================================================================================================
	// Validation
	// =============================================================================================================
	
	
	if (!HasAuthority())
	{
		ScreenLog(TEXT("[UInventoryComponent::SwapSlots] Called without authority; use RequestSlotSwap."));
		return false;
	}

	// Src = the inventory the dragged item came from; 'this' = the destination inventory.
	UInventoryComponent* src = SourceInventory ? SourceInventory : this;

	if (!src->Slots.IsValidIndex(SourceIndex) || !Slots.IsValidIndex(DestIndex))
	{
		ScreenLog(FString::Printf(
			TEXT("[SwapSlots] Bad indices: SourceIndex=%d (src has %d), DestIndex=%d (dest has %d)."),
			SourceIndex, src->Slots.Num(), DestIndex, Slots.Num()));
		return false;
	}

	const FInventorySlot srcSlot = src->GetSlot(SourceIndex);
	const FInventorySlot dstSlot = GetSlot(DestIndex);

	// Check if the source item is valid (not an empty slot) AND if the destination slot is compatible
	if (!IsValidSlotSwap(srcSlot, dstSlot, SourceIndex, DestIndex, src))
		return false;

	FItemInstance srcItem;
	FItemInstance dstItem;
	const bool srcHasItem = src->GetItem(srcSlot.ItemID, srcItem);
	const bool dstHasItem = GetItem(dstSlot.ItemID, dstItem);

	if (!srcHasItem)
	{
		ScreenLog(FString::Printf(TEXT("[SwapSlots] Source slot %d has no resolvable item (ID %s)."),
		                          SourceIndex, *srcSlot.ItemID.ToString()));
		return false;
	}

	// =============================================================================================================
	// Cases
	// =============================================================================================================
	
	// CASE 1: Source item and destination item are the same and the destination item is not at max size
	bool sameItemAndNotMaxed = dstHasItem && AreItemsTheSame(srcItem, dstItem) 
						       && !src->IsItemSizeMax(srcItem) && !IsItemSizeMax(dstItem);
	if (srcItem.HasFragment<FStackableFragment>() && sameItemAndNotMaxed)
			MergeAcross(src, srcSlot, this, dstSlot);
	
	// CASE 2: Empty slot in the same inventory
	else if (src == this)
	{
		Slots[SourceIndex].ItemID = dstSlot.ItemID;
		Slots[SourceIndex].bIsAnchor = dstSlot.ItemID.IsValid();
		Slots[DestIndex].ItemID = srcSlot.ItemID;
		Slots[DestIndex].bIsAnchor = srcSlot.ItemID.IsValid();
		MarkSlotsDirty();
	}
	
	// CASE 3: Cross inventory
	else
	{
		// --- CROSS component: move the instances (and their attachment closures) between
		//     stores, THEN write the slot references. Order matters: collect closures first,
		//     remove from old store, add to new store (preserving UUIDs so slot refs stay valid),
		//     then point each slot at the id that now lives in that component's store. ---
		TArray<FItemInstance> srcClosure;
		src->CollectClosure(srcItem, srcClosure);

		TArray<FItemInstance> dstClosure;
		if (dstHasItem)
			CollectClosure(dstItem, dstClosure);

		// Move source item -> this component.
		src->RemoveInstanceAndClosure(srcItem);
		AddInstancesPreservingIds(srcClosure);

		// Move dest item (if any) -> source component.
		if (dstHasItem)
		{
			RemoveInstanceAndClosure(dstItem);
			src->AddInstancesPreservingIds(dstClosure);
		}

		// Write slot references: dest slot now holds the source item; source slot holds the
		// dest item (or empty). SlotType on each slot is preserved (we only touch ItemID).
		Slots[DestIndex].ItemID = srcSlot.ItemID;
		Slots[DestIndex].bIsAnchor = srcSlot.ItemID.IsValid();
		src->Slots[SourceIndex].ItemID = dstHasItem ? dstSlot.ItemID : FGuid();
		src->Slots[SourceIndex].bIsAnchor = dstHasItem;

		MarkSlotsDirty();
		src->MarkSlotsDirty();
	}

	// Broadcast changes on both sides.
	OnInventoryChanged.Broadcast(DestIndex);
	RefreshIsInventoryFull();
	if (src != this)
	{
		src->OnInventoryChanged.Broadcast(SourceIndex);
		src->RefreshIsInventoryFull();
	}
	else
		OnInventoryChanged.Broadcast(SourceIndex);

	return true;
}

// Simple wrapper for blueprint exposure
bool UInventoryComponent::MergeItems(const FInventorySlot& MyItemSlot, const FInventorySlot& TargetItemSlot)
{
	if (!HasAuthority())
		return false;
	
	return MergeAcross(this, MyItemSlot, this, TargetItemSlot);
}

bool UInventoryComponent::MergeAcross(UInventoryComponent* SourceInv, const FInventorySlot& SourceSlot,
									  UInventoryComponent* DestInv, const FInventorySlot& DestSlot) const
{
	if (!SourceInv || !DestInv)
		return false;

	FItemInstance sourceItem;
	FItemInstance destItem;
	if (!SourceInv->GetItem(SourceSlot.ItemID, sourceItem) || !DestInv->GetItem(DestSlot.ItemID, destItem))
		return false;

	// Parity: MergeItems math — sum, cap at max, remainder back to source (or source removed).
	if (!AreItemsTheSame(sourceItem, destItem))
		return false;

	// No stackable fragment means the item does not stack at all, so there is nothing to
	// merge. MergeItems is Blueprint-callable, so this cannot be assumed away.
	const FStackableFragment* stackable = destItem.GetFragment<FStackableFragment>();
	if (!stackable)
		return false;

	const int32 defMaxStack = stackable->MaxStackSize;

	const int32 itemSum = sourceItem.Quantity + destItem.Quantity;
	const int32 newTargetQty = FMath::Min(itemSum, defMaxStack);
	const int32 newSourceQty = itemSum - newTargetQty;

	destItem.Quantity = newTargetQty;
	DestInv->UpdateItemInSlot(DestSlot, destItem);

	if (newSourceQty == 0)
		SourceInv->RemoveItemFromSlot(SourceSlot);
	else
	{
		sourceItem.Quantity = newSourceQty;
		SourceInv->UpdateItemInSlot(SourceSlot, sourceItem);
	}

	return true;
}

void UInventoryComponent::SplitSlotStack(FInventorySlot SplittingItemSlot, int32 SplitQuantity)
{
	if (!HasAuthority())
	{
		ScreenLog(TEXT("[UInventoryComponent::SplitSlotStack] Called without authority; use RequestSplitSlotStack."));
		return;
	}

	// Parity: no empty slot -> nothing happens.
	FInventorySlot emptySlot;
	const bool bInventoryFull = GetNextEmptySlot(emptySlot);
	if (bInventoryFull)
		return;

	FItemInstance sourceInstance;
	if (!GetItem(SplittingItemSlot.ItemID, sourceInstance))
		return;

	// Safety clamp (documented addition): the BP had no guard; splitting >= the whole
	// stack would drive the source negative. Reject out-of-range splits.
	if (SplitQuantity < 1 || SplitQuantity >= sourceInstance.Quantity)
	{
		UE_LOG(LogInventoryComponent, Warning,
		       TEXT("[UInventoryComponent::SplitSlotStack] Invalid SplitQuantity %d for stack of %d."),
		       SplitQuantity, sourceInstance.Quantity);
		return;
	}

	// Parity: source keeps the remainder...
	sourceInstance.Quantity -= SplitQuantity;
	UpdateItemInSlot(SplittingItemSlot, sourceInstance);

	// ...and a NEW item (fresh Guid, template = source so the fragments copy)
	// lands in the empty slot with the split quantity.
	FItemInstance created;
	FGuid createdId;
	CreateItemInSlot(emptySlot.SlotIndex, emptySlot.SlotType, sourceInstance.RowName,
	                 SplitQuantity, /*Durability*/ -1.f, sourceInstance, created, createdId);
}


bool UInventoryComponent::CreateItemInSlot(int32 SlotIndex, ESlotType InSlotType, FName RowName, int32 Quantity,
                                           float Durability, const FItemInstance& Template,
                                           FItemInstance& OutInstance, FGuid& OutItemID)
{
	// Parity: invalid slot index -> nothing.
	if (!Slots.IsValidIndex(SlotIndex))
	{
		UE_LOG(LogInventoryComponent, Warning,
		       TEXT("[UInventoryComponent::CreateItemInSlot] Slot index %d is out of range (have %d slots)."),
		       SlotIndex, Slots.Num());
		OutInstance = FItemInstance();
		OutItemID = FGuid();
		return false;
	}

	if (!CreateAndRegisterItem(RowName, Quantity, Durability, Template, OutInstance, OutItemID))
		return false;

	Slots[SlotIndex].SlotIndex = SlotIndex;
	Slots[SlotIndex].SlotType = InSlotType;
	Slots[SlotIndex].ItemID = OutItemID;
	Slots[SlotIndex].bIsAnchor = true; // every item is 1x1 today, so its one cell is its anchor

	OnInventoryChanged.Broadcast(SlotIndex);
	RefreshIsInventoryFull();
	return true;
}

bool UInventoryComponent::UpdateItemInSlot(const FInventorySlot& ItemSlot, const FItemInstance& ItemInstance)
{
	// Parity: only updates an instance that exists.
	if (!ContainsItem(ItemSlot.ItemID))
		return false;
	if (!UpdateItem(ItemSlot.ItemID, ItemInstance))
		return false;

	// Parity: the BP wrote the slot back and broadcast OnInventoryChanged. (The BP's
	// success path fell off without reaching a return node — it always reported false;
	// returning true here is the obvious correction, and no caller depended on it.)
	if (Slots.IsValidIndex(ItemSlot.SlotIndex))
		Slots[ItemSlot.SlotIndex] = ItemSlot;
	OnInventoryChanged.Broadcast(ItemSlot.SlotIndex);
	return true;
}

bool UInventoryComponent::RemoveItemFromSlot(const FInventorySlot& InventorySlotToBeRemoved)
{
	// Parity: remove from store, clear the slot PRESERVING its SlotType, broadcast.
	RemoveItem(InventorySlotToBeRemoved.ItemID);

	if (Slots.IsValidIndex(InventorySlotToBeRemoved.SlotIndex))
	{
		Slots[InventorySlotToBeRemoved.SlotIndex].SlotIndex = InventorySlotToBeRemoved.SlotIndex;
		Slots[InventorySlotToBeRemoved.SlotIndex].SlotType = InventorySlotToBeRemoved.SlotType;
		Slots[InventorySlotToBeRemoved.SlotIndex].ItemID.Invalidate();
		Slots[InventorySlotToBeRemoved.SlotIndex].bIsAnchor = false;
	}

	OnInventoryChanged.Broadcast(InventorySlotToBeRemoved.SlotIndex);
	RefreshIsInventoryFull();
	return true;
}

void UInventoryComponent::SetSlot(int32 Index, const FInventorySlot& Slot)
{
	// Parity: SetSlot only transfers the ItemID; SlotIndex/SlotType stay. bIsAnchor is
	// derived from the ItemID here (every item is still 1x1) rather than copied from
	// Slot, since Slot may be a caller-side struct that predates the field.
	if (Slots.IsValidIndex(Index))
	{
		Slots[Index].ItemID = Slot.ItemID;
		Slots[Index].bIsAnchor = Slot.ItemID.IsValid();
		// No broadcast here — parity: callers (SwapSlots) broadcast themselves.
	}
}

// =============================================================================================================
// Setup / Persistence
// =============================================================================================================

void UInventoryComponent::InitializeInventory()
{
	// Guarded by IsDataInitialized; builds Slots of SlotType.
	//
	// Grid-aware: when GridWidth and GridHeight are both configured (> 0), Slots is
	// sized to GridWidth * GridHeight and addressed row-major (SlotIndex = Y * GridWidth
	// + X), keeping MaxInventorySlots synced to the product for anything still reading
	// it as a flat count. When the grid dims are left at 0, this falls back to the old
	// flat MaxInventorySlots behavior unchanged.
	if (IsDataInitialized)
		return;
	IsDataInitialized = true;

	const bool bUseGrid = GridWidth > 0 && GridHeight > 0;
	const int32 slotCount = bUseGrid ? (GridWidth * GridHeight) : MaxInventorySlots;

	if (bUseGrid)
		MaxInventorySlots = slotCount;

	Slots.Reset(slotCount);
	for (int32 i = 0; i < slotCount; ++i)
	{
		FInventorySlot newSlot;
		newSlot.SlotIndex = i;
		newSlot.SlotType = SlotType;
		newSlot.ItemID.Invalidate();
		newSlot.bIsAnchor = false; // empty slot; nothing anchored here yet
		Slots.Add(newSlot);
	}
	RefreshIsInventoryFull();
}

void UInventoryComponent::FillDefaultInventory()
{
	// Parity note: the BP hardcoded GEN_Potion x5 -> slot 0 and GEN_Hatchet x1 -> slot 3.
	// That seeding is now the DefaultItems config array (recreate those two entries in
	// the editor for identical behavior). One deviation, documented: the BP re-made the
	// seeded slots with SlotType=Generic; this preserves each slot's configured type.
	if (!ShouldFillDefaultInventory)
		return;

	for (const FDefaultInventoryEntry& entry : DefaultItems)
	{
		if (!Slots.IsValidIndex(entry.SlotIndex))
		{
			UE_LOG(LogInventoryComponent, Warning,
			       TEXT("[UInventoryComponent::FillDefaultInventory] Slot index %d out of range for row '%s'."),
			       entry.SlotIndex, *entry.RowHandle.RowName.ToString());
			continue;
		}

		FItemInstance created;
		FGuid createdId;
		CreateItemInSlot(entry.SlotIndex, Slots[entry.SlotIndex].SlotType, entry.RowHandle.RowName,
		                 entry.Quantity, /*Durability*/ -1.f, FItemInstance(), created, createdId);
	}
}

void UInventoryComponent::SaveToRecord(FInventoryRecord& OutRecord) const
{
	OutRecord.Items.Reset(InventoryList.Entries.Num());
	for (const FInventoryItemEntry& entry : InventoryList.Entries)
	{
		OutRecord.Items.Add(entry.Instance);
	}
	OutRecord.Slots = Slots;
}

void UInventoryComponent::LoadFromRecord(const FInventoryRecord& Record)
{
	if (!HasAuthority())
	{
		UE_LOG(LogInventoryComponent, Warning,
		       TEXT("[UInventoryComponent::LoadFromRecord] Called without authority; ignored."));
		return;
	}

	InventoryList.Entries.Reset(Record.Items.Num());
	for (const FItemInstance& instance : Record.Items)
	{
		InventoryList.Entries.Add(FInventoryItemEntry(instance));
	}
	InventoryList.MarkArrayDirty();

	Slots = Record.Slots;
	IsDataInitialized = true;

	RebuildIdCache();
	BroadcastAllSlots();
	RefreshIsInventoryFull();
}

void UInventoryComponent::ClearInventory()
{
	if (!HasAuthority())
		return;

	InventoryList.Entries.Reset();
	InventoryList.MarkArrayDirty();
	for (FInventorySlot& slot : Slots)
	{
		slot.ItemID.Invalidate();
		slot.bIsAnchor = false;
	}

	RebuildIdCache();
	BroadcastAllSlots();
	RefreshIsInventoryFull();
}

// =====================================================================================
//  Main widget ref (parity, restructured — see header)
// =====================================================================================

void UInventoryComponent::SetMainWidgetRef(UUserWidget* Widget)
{
	MainWidgetRef = Widget;
}

UUserWidget* UInventoryComponent::GetMainWidgetRefFromOwner() const
{
	return MainWidgetRef;
}

// =====================================================================================
//  Closure helpers (attachment graph)
// =====================================================================================

void UInventoryComponent::CollectClosure(const FItemInstance& Root, TArray<FItemInstance>& OutClosure) const
{
	OutClosure.Reset();

	TArray<FGuid> pending;
	TSet<FGuid> visited;

	OutClosure.Add(Root);
	visited.Add(Root.UUID);

	// Fragments declare which other instances they reference (a weapon's fitted
	// attachments, say). The inventory walks those links without knowing what any
	// particular fragment type means.
	TArray<FGuid> rootRefs;
	Root.GatherReferencedItemIds(rootRefs);
	pending.Append(rootRefs);

	while (pending.Num() > 0)
	{
		const FGuid id = pending.Pop();
		if (!id.IsValid() || visited.Contains(id))
			continue;
		visited.Add(id);

		FItemInstance instance;
		if (GetItem(id, instance))
		{
			OutClosure.Add(instance);

			TArray<FGuid> refs;
			instance.GatherReferencedItemIds(refs);
			pending.Append(refs);
		}
	}
}

void UInventoryComponent::RemoveInstanceAndClosure(const FItemInstance& Root)
{
	TArray<FItemInstance> closure;
	CollectClosure(Root, closure);
	for (const FItemInstance& instance : closure)
	{
		RemoveItem(instance.UUID);
	}
}

void UInventoryComponent::AddInstancesPreservingIds(const TArray<FItemInstance>& Instances)
{
	for (const FItemInstance& instance : Instances)
	{
		AddExistingItem(instance);
	}
}

// =====================================================================================
//  Replication plumbing / cache / broadcasts
// =====================================================================================

void UInventoryComponent::HandleReplicatedItemsChanged(const TArray<FGuid>& ChangedIds)
{
	RebuildIdCache();
	for (const FGuid& id : ChangedIds)
	{
		BroadcastSlotChangedForItem(id);
	}
}

void UInventoryComponent::HandleReplicatedItemsRebuilt()
{
	RebuildIdCache();
}

void UInventoryComponent::OnRep_Slots(const TArray<FInventorySlot>& PreviousSlots)
{
	const int32 maxCount = FMath::Max(PreviousSlots.Num(), Slots.Num());
	for (int32 i = 0; i < maxCount; ++i)
	{
		const bool bHadOld = PreviousSlots.IsValidIndex(i);
		const bool bHasNew = Slots.IsValidIndex(i);
		if (!bHadOld || !bHasNew)
		{
			OnInventoryChanged.Broadcast(i);
			continue;
		}
		const FInventorySlot& oldSlot = PreviousSlots[i];
		const FInventorySlot& newSlot = Slots[i];
		if (oldSlot.ItemID != newSlot.ItemID || oldSlot.SlotType != newSlot.SlotType
			|| oldSlot.SlotIndex != newSlot.SlotIndex || oldSlot.bIsAnchor != newSlot.bIsAnchor)
			OnInventoryChanged.Broadcast(i);
	}
	RefreshIsInventoryFull();
}

void UInventoryComponent::RebuildIdCache()
{
	IdToIndex.Reset();
	for (int32 i = 0; i < InventoryList.Entries.Num(); ++i)
	{
		IdToIndex.Add(InventoryList.Entries[i].Instance.UUID, i);
	}
}

void UInventoryComponent::MarkSlotsDirty()
{
	// Slots is a plain replicated TArray (ReplicatedUsing=OnRep_Slots). Writing an element
	// dirties the whole array for replication; this call is a no-op marker for clarity and
	// a hook if we later move to push-model networking. On the server, also run the local
	// change handling so listen-server/host UI updates immediately.
	if (GetOwnerRole() == ROLE_Authority)
		RefreshIsInventoryFull();
}

void UInventoryComponent::MarkItemDirtyById(FGuid ItemID)
{
	for (FInventoryItemEntry& entry : InventoryList.Entries)
	{
		if (entry.Instance.UUID == ItemID)
		{
			InventoryList.MarkItemDirty(entry);
			return;
		}
	}
}

void UInventoryComponent::BroadcastSlotChangedForItem(FGuid ItemID)
{
	const int32 slotIndex = FindSlotIndexByItemId(ItemID);
	if (slotIndex != INDEX_NONE)
		OnInventoryChanged.Broadcast(slotIndex);
}

void UInventoryComponent::BroadcastAllSlots()
{
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		OnInventoryChanged.Broadcast(i);
	}
}

int32 UInventoryComponent::FindSlotIndexByItemId(FGuid ItemID) const
{
	if (!ItemID.IsValid())
		return INDEX_NONE;
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		if (Slots[i].ItemID == ItemID)
			return i;
	}
	return INDEX_NONE;
}

void UInventoryComponent::RefreshIsInventoryFull()
{
	FInventorySlot dummy;
	IsInventoryFull = GetNextEmptySlot(dummy);
}

AActor* UInventoryComponent::ResolveOwnerAvatar() const
{
	AActor* owner = GetOwner();
	if (!owner)
		return nullptr;

	AActor* avatar = owner;
	if (const APlayerState* ps = Cast<APlayerState>(owner))
		avatar = ps->GetPawn();
	else if (const AController* controller = Cast<AController>(owner))
		avatar = controller->GetPawn();

	// Cache the parity ref for BP reads.
	if (avatar)
		const_cast<UInventoryComponent*>(this)->Character = avatar;
	return avatar;
}

// =====================================================================================
//  Definition lookups
// =====================================================================================

bool UInventoryComponent::GetItemStructForRow(FName RowName, FItemDefinition& OutStruct)
{
	if (RowName == NAME_None)
	{
		OutStruct = FItemDefinition();
		return false;
	}
	return UItemLibrary::GetItemDefinition(RowName, OutStruct);
}


// =====================================================================================
//  Debug (parity)
// =====================================================================================

void UInventoryComponent::LogInventory(float PrintDuration) const
{
	for (const FInventorySlot& slot : Slots)
	{
		LogInventorySlot(slot, PrintDuration);
	}
}

void UInventoryComponent::LogInventorySlot(const FInventorySlot& InventorySlot, float Duration) const
{
	FItemInstance instance;
	const bool bFound = GetItem(InventorySlot.ItemID, instance);

	const FString message = FString::Printf(
		TEXT("[%s] Slot %d (%s): %s"),
		*InventoryName.ToString(),
		InventorySlot.SlotIndex,
		*UEnum::GetValueAsString(InventorySlot.SlotType),
		bFound
			? *FString::Printf(TEXT("%s x%d ( ID %s)"),
			                   *instance.RowName.ToString(), instance.Quantity,
			                   *instance.UUID.ToString(EGuidFormats::DigitsWithHyphens))
			: TEXT("empty"));

	UE_LOG(LogInventoryComponent, Log, TEXT("%s"), *message);
	if (GEngine)
		GEngine->AddOnScreenDebugMessage(-1, Duration, FColor::Cyan, message);
}

bool UInventoryComponent::SetItemInstance(FGuid ItemID, const FItemInstance& NewInstance)
{
	if (!HasAuthority())
	{
		ScreenLog(TEXT("[UInventoryComponent::SetItemInstance] Called without authority; ignored."));
		return false;
	}

	if (!UpdateItem(ItemID, NewInstance))
	{
		ScreenLog(FString::Printf(
			TEXT("[UInventoryComponent::SetItemInstance] Item id %s not found in this inventory."),
			*ItemID.ToString()));
		return false;
	}

	BroadcastSlotChangedForItem(ItemID);
	return true;
}

int32 UInventoryComponent::GetTotalQuantityByRowName(FName RowName) const
{
	if (RowName == NAME_None)
		return 0;

	int32 total = 0;
	for (const FInventorySlot& slot : Slots)
	{
		FItemInstance instance;
		if (GetItem(slot.ItemID, instance) && instance.RowName == RowName)
			total += instance.Quantity;
	}
	return total;
}

int32 UInventoryComponent::ConsumeItemsByRowName(FName RowName, int32 Amount)
{
	if (!HasAuthority())
	{
		ScreenLog(TEXT("[UInventoryComponent::ConsumeItemsByRowName] Called without authority; ignored."));
		return 0;
	}
	if (RowName == NAME_None || Amount <= 0)
		return 0;

	int32 remaining = Amount;

	// First stack found, then the next, and so on (slot order), until satisfied.
	// Iterate over a copy of the slot list since removals mutate Slots.
	const TArray<FInventorySlot> slotsSnapshot = Slots;
	for (const FInventorySlot& slot : slotsSnapshot)
	{
		if (remaining <= 0)
			break;

		FItemInstance instance;
		if (!GetItem(slot.ItemID, instance) || instance.RowName != RowName)
			continue;

		const int32 take = FMath::Min(instance.Quantity, remaining);
		instance.Quantity -= take;
		remaining -= take;

		if (instance.Quantity <= 0)
			RemoveItemFromSlot(slot); // clears the slot (SlotType preserved) + broadcasts
		else
		{
			UpdateItem(slot.ItemID, instance);
			OnInventoryChanged.Broadcast(slot.SlotIndex);
		}
	}

	return Amount - remaining;
}

UInventoryComponent* UInventoryComponent::GetRoutingComponent()
{
	const AActor* owner = GetOwner();
	if (owner && owner->GetNetConnection())
		return this;

	const UWorld* world = GetWorld();
	if (!world)
		return this;

	if (const APlayerController* pc = world->GetFirstPlayerController())
	{
		if (const APlayerState* ps = pc->PlayerState)
		{
			if (UInventoryComponent* comp = ps->FindComponentByClass<UInventoryComponent>())
				return comp;
		}
		if (const APawn* pawn = pc->GetPawn())
		{
			if (UInventoryComponent* comp = pawn->FindComponentByClass<UInventoryComponent>())
				return comp;
		}
	}
	return this;
}