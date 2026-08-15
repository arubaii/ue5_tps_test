// Copyright arubaii. All rights reserved.

#include "Public/ItemSystem/InventoryComponent.h"

#include "Public/ItemSystem/ItemLibrary.h"
#include "Public/ItemSystem/Rows.h"
#include "Private/ItemSystem/WorldItemInterface.h"

#include "Blueprint/UserWidget.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogInventoryComponent, Log, All);

void UInventoryComponent::ScreenLog(const FString& Message, bool bIsError)
{
	// Always log; also print to screen so failures are visible without the Output Log open.
	if (bIsError)
	{
		UE_LOG(LogInventoryComponent, Warning, TEXT("%s"), *Message);
	}
	else
	{
		UE_LOG(LogInventoryComponent, Log, TEXT("%s"), *Message);
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 6.f, bIsError ? FColor::Red : FColor::Green, Message);
	}
}

// =====================================================================================
//  Fast array replication callbacks (client side)
// =====================================================================================

void FInventoryItemList::PostReplicatedAdd(const TArrayView<int32>& AddedIndices, int32 /*FinalSize*/)
{
	if (!OwnerComponent) return;
	TArray<FGuid> Ids;
	for (int32 Index : AddedIndices)
	{
		if (Entries.IsValidIndex(Index))
		{
			Ids.Add(Entries[Index].Instance.UUID);
		}
	}
	OwnerComponent->HandleReplicatedItemsChanged(Ids);
}

void FInventoryItemList::PostReplicatedChange(const TArrayView<int32>& ChangedIndices, int32 /*FinalSize*/)
{
	if (!OwnerComponent) return;
	TArray<FGuid> Ids;
	for (int32 Index : ChangedIndices)
	{
		if (Entries.IsValidIndex(Index))
		{
			Ids.Add(Entries[Index].Instance.UUID);
		}
	}
	OwnerComponent->HandleReplicatedItemsChanged(Ids);
}

void FInventoryItemList::PreReplicatedRemove(const TArrayView<int32>& RemovedIndices, int32 /*FinalSize*/)
{
	if (!OwnerComponent) return;
	TArray<FGuid> Ids;
	for (int32 Index : RemovedIndices)
	{
		if (Entries.IsValidIndex(Index))
		{
			Ids.Add(Entries[Index].Instance.UUID);
		}
	}
	// Broadcast affected slots while the entries are still present, then let the
	// owner rebuild its cache after the removal completes.
	OwnerComponent->HandleReplicatedItemsChanged(Ids);
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
		{
			FillDefaultInventory();
		}
	}
}

bool UInventoryComponent::HasAuthority() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
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
	if (const int32* Found = IdToIndex.Find(ItemID))
	{
		if (InventoryList.Entries.IsValidIndex(*Found) && InventoryList.Entries[*Found].Instance.UUID == ItemID)
		{
			ItemInstance = InventoryList.Entries[*Found].Instance;
			return true;
		}
	}

	// Fallback linear scan (covers cache staleness windows during replication).
	for (const FInventoryItemEntry& Entry : InventoryList.Entries)
	{
		if (Entry.Instance.UUID == ItemID)
		{
			ItemInstance = Entry.Instance;
			return true;
		}
	}

	ItemInstance = FItemInstance();
	return false;
}

bool UInventoryComponent::GetItemStructByID(FGuid ItemID, FItemStructure& ItemStructure) const
{
	// Parity: Guid -> instance -> RowName -> UItemLibrary definition lookup.
	FItemInstance Instance;
	if (!GetItem(ItemID, Instance))
	{
		ItemStructure = FItemStructure();
		return false;
	}
	return GetItemStructForRow(Instance.RowName, ItemStructure);
}

bool UInventoryComponent::ContainsItem(FGuid ItemID) const
{
	FItemInstance Dummy;
	return GetItem(ItemID, Dummy);
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
	FItemInstance Stamped = ItemInstance;
	Stamped.UUID = FGuid::NewGuid();

	const int32 NewIndex = InventoryList.Entries.Add(FInventoryItemEntry(Stamped));
	InventoryList.MarkItemDirty(InventoryList.Entries[NewIndex]);
	IdToIndex.Add(Stamped.UUID, NewIndex);
	return Stamped;
}

bool UInventoryComponent::AddExistingItem(const FItemInstance& ItemInstance)
{
	// Transfer-preserving add: keeps the incoming UUID so Guid links (a weapon's
	// WeaponAttachments map) survive moves between world/actor/components. This is the
	// behavior the old single-global-database model got implicitly; per-component
	// stores need it explicitly.
	if (!ItemInstance.UUID.IsValid())
	{
		return false;
	}

	// Idempotent: if it already exists, overwrite (keeps transfers safe to retry).
	if (ContainsItem(ItemInstance.UUID))
	{
		return UpdateItem(ItemInstance.UUID, ItemInstance);
	}

	const int32 NewIndex = InventoryList.Entries.Add(FInventoryItemEntry(ItemInstance));
	InventoryList.MarkItemDirty(InventoryList.Entries[NewIndex]);
	IdToIndex.Add(ItemInstance.UUID, NewIndex);
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
	FItemInstance Source;
	if (!GetItem(MyItemID, Source))
	{
		DuplicatedItemID = FGuid();
		return false;
	}

	// Parity+fix: the BP copy kept the ORIGINAL UUID inside the duplicated struct and
	// shared attachment instances by Guid. Here the copy gets its own Guid and the
	// attachment closure is deep-copied so duplicates are fully independent.
	FItemInstance Copy = Source;
	Copy.UUID = FGuid::NewGuid();

	TMap<ESlotType, FGuid> RemappedAttachments;
	for (const TPair<ESlotType, FGuid>& Pair : Source.WeaponAttachments)
	{
		FItemInstance AttachSource;
		if (GetItem(Pair.Value, AttachSource))
		{
			FItemInstance AttachCopy = AttachSource;
			AttachCopy.UUID = FGuid::NewGuid();
			AttachCopy.WeaponAttachments.Empty(); // one level deep matches the data model (attachments hold no sub-attachments today)
			AddExistingItem(AttachCopy);
			RemappedAttachments.Add(Pair.Key, AttachCopy.UUID);
		}
		else
		{
			RemappedAttachments.Add(Pair.Key, Pair.Value);
		}
	}
	Copy.WeaponAttachments = RemappedAttachments;

	AddExistingItem(Copy);
	DuplicatedItemID = Copy.UUID;
	return true;
}

bool UInventoryComponent::BuildItemInstance(FName RowName, int32 Quantity, float Durability,
                                            const FItemInstance& Template, FItemInstance& OutInstance) const
{
	// CreateAndRegisterItem parity, construction half.
	const bool bHasTemplate = Template.RowName != NAME_None;
	const FName EffectiveRow = (RowName != NAME_None) ? RowName : Template.RowName;

	if (bHasTemplate)
	{
		OutInstance = Template;
		// Parity: -1 sentinels mean "keep template value".
		if (Quantity != -1)
		{
			OutInstance.Quantity = Quantity;
		}
		if (Durability != -1.f)
		{
			OutInstance.Durability = Durability;
		}
	}
	else
	{
		FItemStructure Def;
		if (!GetItemStructForRow(EffectiveRow, Def))
		{
			ScreenLog(FString::Printf(
			       TEXT("[UInventoryComponent::BuildItemInstance] Could not find item definition for row name '%s' (check Item Tables prefix mapping and DataTable row struct)."),
			       *EffectiveRow.ToString()));
			OutInstance = FItemInstance();
			return false;
		}
		OutInstance = FItemInstance();
		OutInstance.RowName = EffectiveRow;
		OutInstance.Quantity = Quantity;
		OutInstance.Durability = Durability;
		OutInstance.Modifiers = Def.DefaultModifiers;
		OutInstance.IsWorldItem = false;
	}

	// Parity: BOTH paths stamp the weapon fields from the row — the row's values when
	// the row is a weapon row, DEFAULTS otherwise (the BP Selects fell back to
	// default-constructed structs when GetItemDef found no weapon row).
	FWeaponRow WeaponRow;
	if (GetWeaponRowForName(EffectiveRow, WeaponRow))
	{
		OutInstance.ModifiedWeaponStats = WeaponRow.WeaponStats;
		OutInstance.ModifiedWeaponRecoil = WeaponRow.WeaponRecoil;
		OutInstance.WeaponAttachments = WeaponRow.AttachmentSlots;
	}
	else
	{
		OutInstance.ModifiedWeaponStats = FWeaponStats();
		OutInstance.ModifiedWeaponRecoil = FWeaponRecoil();
		OutInstance.WeaponAttachments.Empty();
	}

	return true;
}

bool UInventoryComponent::CreateAndRegisterItem(FName RowName, int32 Quantity, float Durability,
                                                const FItemInstance& Template, FItemInstance& OutInstance, FGuid& OutItemID)
{
	FItemInstance Built;
	if (!BuildItemInstance(RowName, Quantity, Durability, Template, Built))
	{
		OutInstance = FItemInstance();
		OutItemID = FGuid();
		return false;
	}

	OutInstance = RegisterNewItem(Built);
	OutItemID = OutInstance.UUID;
	return true;
}

// =====================================================================================
//  Slot reads
// =====================================================================================

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
	for (const FInventorySlot& Candidate : Slots)
	{
		if (!Candidate.ItemID.IsValid())
		{
			Slot = Candidate;
			return false;
		}
	}
	Slot = FInventorySlot();
	return true;
}

bool UInventoryComponent::GetNextCompatibleSlot(const FItemInstance& ItemToBeCheckedAgainst, FInventorySlot& Slot) const
{
	// PASS 1 (parity): a slot already holding the same item with room to stack.
	for (const FInventorySlot& Candidate : Slots)
	{
		FItemInstance SlotInstance;
		if (GetItem(Candidate.ItemID, SlotInstance))
		{
			if (AreItemsTheSame(ItemToBeCheckedAgainst, SlotInstance) && !IsItemSizeMax(SlotInstance))
			{
				Slot = Candidate;
				return true;
			}
		}
	}

	// PASS 2 (parity): an empty slot whose SlotType is in the item's CompatibleSlotTypes.
	FItemStructure Def;
	if (GetItemStructForRow(ItemToBeCheckedAgainst.RowName, Def))
	{
		for (const FInventorySlot& Candidate : Slots)
		{
			FItemInstance SlotInstance;
			const bool bEmpty = !GetItem(Candidate.ItemID, SlotInstance);
			if (bEmpty && Def.CompatibleSlotTypes.Contains(Candidate.SlotType))
			{
				Slot = Candidate;
				return true;
			}
		}
	}

	Slot = FInventorySlot();
	return false;
}

bool UInventoryComponent::AreSlotsCompatible(const FInventorySlot& MyInventorySlot, const FInventorySlot& TargetInventorySlot) const
{
	// Parity: does MY slot's item accept the TARGET slot's type?
	FItemStructure Def;
	if (!GetItemStructByID(MyInventorySlot.ItemID, Def))
	{
		return false;
	}
	return Def.CompatibleSlotTypes.Contains(TargetInventorySlot.SlotType);
}

bool UInventoryComponent::AreItemsTheSame(const FItemInstance& MyItem, const FItemInstance& TargetItem) const
{
	// Parity: same RowName AND identical modifier sets AND (durability equal, but only
	// when the definition says the item has durability).
	if (MyItem.RowName != TargetItem.RowName)
	{
		return false;
	}
	if (!AreModifierListsEqual(MyItem.Modifiers, TargetItem.Modifiers))
	{
		return false;
	}

	FItemStructure Def;
	if (GetItemStructForRow(TargetItem.RowName, Def) && Def.HasDurability)
	{
		return MyItem.Durability == TargetItem.Durability;
	}
	return true;
}

bool UInventoryComponent::IsItemSizeMax(const FItemInstance& ItemInstance) const
{
	// Parity: definition found ? Quantity >= MaxStackSize : false.
	FItemStructure Def;
	if (!GetItemStructForRow(ItemInstance.RowName, Def))
	{
		return false;
	}
	return ItemInstance.Quantity >= Def.MaxStackSize;
}

bool UInventoryComponent::IsValidSlotSwap(const FInventorySlot& MyInventorySlot, const FInventorySlot& TargetInventorySlot,
                                          int32 MyIndex, int32 TargetIndex, UInventoryComponent* SourceInventory) const
{
	// Semantics (matching SwapSlots' call):
	//   MyInventorySlot     = the SOURCE slot (dragged item), living in SourceInventory.
	//   TargetInventorySlot = the DEST slot (this component).
	//   SourceInventory     = the component the dragged item came from (may be 'this').
	const UInventoryComponent* SrcInv = SourceInventory ? SourceInventory : this;

	// Reject dropping a slot onto itself.
	const bool bSameSlot = (SrcInv == this) && (MyIndex == TargetIndex);
	if (bSameSlot)
	{
		return false;
	}

	// The dragged item's definition (resolved from the SOURCE component's store).
	FItemStructure MyDef;
	if (!SrcInv->GetItemStructByID(MyInventorySlot.ItemID, MyDef))
	{
		ScreenLog(FString::Printf(TEXT("[IsValidSlotSwap] Rejected: could not resolve dragged item (ID %s) in source inventory."),
		                          *MyInventorySlot.ItemID.ToString()));
		return false;
	}

	// The dragged item must accept the destination slot's type.
	if (!MyDef.CompatibleSlotTypes.Contains(TargetInventorySlot.SlotType))
	{
		return false;
	}

	// If the destination slot is occupied, the displaced item must fit back into the
	// source slot (two-way swap). The displaced item lives in THIS (destination) store.
	if (TargetInventorySlot.ItemID.IsValid())
	{
		FItemStructure TargetDef;
		if (GetItemStructByID(TargetInventorySlot.ItemID, TargetDef)
			&& !TargetDef.CompatibleSlotTypes.Contains(MyInventorySlot.SlotType))
		{
			return false;
		}
	}

	return true;
}

// =====================================================================================
//  Client entry points -> Server RPCs
// =====================================================================================

void UInventoryComponent::RequestPickupItem(AActor* WorldItemActor)
{
	if (HasAuthority())
	{
		PickupItem(WorldItemActor);
	}
	else
	{
		Server_PickupItem(WorldItemActor);
	}
}

void UInventoryComponent::RequestDropItem(int32 InventoryIndex)
{
	if (HasAuthority())
	{
		DropItem(InventoryIndex);
	}
	else
	{
		Server_DropItem(InventoryIndex);
	}
}

void UInventoryComponent::RequestSlotSwap(UInventoryComponent* SourceInventory, int32 SourceIndex,
                                          UInventoryComponent* DestInventory, int32 DestIndex)
{
	if (HasAuthority())
	{
		UInventoryComponent* Dest = DestInventory ? DestInventory : this;
		Dest->SwapSlots(SourceInventory ? SourceInventory : this, SourceIndex, DestIndex);
	}
	else
	{
		Server_SlotSwap(SourceInventory, SourceIndex, DestInventory, DestIndex);
	}
}

void UInventoryComponent::RequestSplitSlotStack(UInventoryComponent* Inventory, FInventorySlot SplittingItemSlot, int32 SplitQuantity)
{
	if (HasAuthority())
	{
		UInventoryComponent* Target = Inventory ? Inventory : this;
		Target->SplitSlotStack(SplittingItemSlot, SplitQuantity);
	}
	else
	{
		Server_SplitSlotStack(Inventory, SplittingItemSlot, SplitQuantity);
	}
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
	UInventoryComponent* Dest = DestInventory ? DestInventory : this;
	Dest->SwapSlots(SourceInventory ? SourceInventory : this, SourceIndex, DestIndex);
}

void UInventoryComponent::Server_SplitSlotStack_Implementation(UInventoryComponent* Inventory, FInventorySlot SplittingItemSlot, int32 SplitQuantity)
{
	UInventoryComponent* Target = Inventory ? Inventory : this;
	Target->SplitSlotStack(SplittingItemSlot, SplitQuantity);
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
	{
		return -1;
	}
	if (!WorldItemActor->GetClass()->ImplementsInterface(UWorldItemInterface::StaticClass()))
	{
		ScreenLog(FString::Printf(TEXT("[UInventoryComponent::PickupItem] Actor %s does not implement WorldItemInterface."), *WorldItemActor->GetName()));
		return -1;
	}

	const FItemInstance WorldInstance = IWorldItemInterface::Execute_GetWorldItemInstance(WorldItemActor);
	const TArray<FItemInstance> Attached = IWorldItemInterface::Execute_GetWorldItemAttachedInstances(WorldItemActor);

	ItemActor = WorldItemActor; // parity ref

	bool bAnyTaken = false;
	const int32 Overflow = PickupItemInstance(WorldInstance, Attached, bAnyTaken);

	// Old model: the actor read the overflow and destroyed/updated itself (the
	// component's "Destroying Item" event handled database cleanup). New model: the
	// server drives the actor directly through the interface; the actor's replicated
	// instance carries the result to clients.
	if (bAnyTaken)
	{
		if (Overflow <= 0)
		{
			IWorldItemInterface::Execute_OnWorldItemFullyPickedUp(WorldItemActor);
		}
		else
		{
			FItemInstance Reduced = WorldInstance;
			Reduced.Quantity = Overflow;
			IWorldItemInterface::Execute_SetWorldItemInstance(WorldItemActor, Reduced);
		}
	}

	return Overflow;
}

int32 UInventoryComponent::PickupItemInstance(const FItemInstance& WorldInstance, const TArray<FItemInstance>& AttachedInstances, bool& bAnyTaken)
{
	bAnyTaken = false;

	// Parity guard: valid instance with at least one unit. (BP's unconnected branch fell
	// through returning 0; returning the full quantity here is the safe correction — an
	// invalid pickup takes nothing and must not tell the actor to destroy itself.)
	if (!WorldInstance.IsValid() || WorldInstance.Quantity < 1)
	{
		return FMath::Max(WorldInstance.Quantity, 0);
	}

	const int32 PickupQuantity = WorldInstance.Quantity;

	FInventorySlot CompatibleSlot;
	if (!GetNextCompatibleSlot(WorldInstance, CompatibleSlot))
	{
		// No slot (inventory full for this item): everything stays on the ground. Expected outcome, no log.
		return PickupQuantity;
	}

	FItemInstance CompatibleItem;
	const bool bSlotOccupied = GetItem(CompatibleSlot.ItemID, CompatibleItem);

	if (bSlotOccupied && AreItemsTheSame(WorldInstance, CompatibleItem))
	{
		// ---- Stack-onto-existing cases (the "Is Overflow Greater Than Compatible Max Stack" composite) ----
		FItemStructure Def;
		if (!GetItemStructForRow(CompatibleItem.RowName, Def))
		{
			return PickupQuantity;
		}
		const int32 CompatibleMaxStack = Def.MaxStackSize;
		const int32 CompatibleQuantity = CompatibleItem.Quantity;
		const int32 Sum = PickupQuantity + CompatibleQuantity;

		if (Sum <= CompatibleMaxStack)
		{
			// Case: fits entirely into the existing stack.
			CompatibleItem.Quantity = Sum;
			UpdateItemInSlot(CompatibleSlot, CompatibleItem);
			bAnyTaken = true;
			OnItemPickup.Broadcast(WorldInstance, PickupQuantity);
			return 0;
		}

		// Case: fills the existing stack to max; remainder returns to the ground.
		const int32 ToBeAdded = CompatibleMaxStack - CompatibleQuantity;
		CompatibleItem.Quantity = CompatibleMaxStack;
		UpdateItemInSlot(CompatibleSlot, CompatibleItem);

		const int32 Overflow = PickupQuantity - ToBeAdded;
		bAnyTaken = ToBeAdded > 0;
		if (bAnyTaken)
		{
			OnItemPickup.Broadcast(WorldInstance, ToBeAdded);
		}
		return Overflow;
	}

	// ---- Empty-slot cases (pass 2 of GetNextCompatibleSlot guarantees compatibility) ----
	FItemStructure PickupDef;
	if (!GetItemStructForRow(WorldInstance.RowName, PickupDef))
	{
		return PickupQuantity;
	}
	const int32 PickupMaxStack = PickupDef.MaxStackSize;

	if (PickupQuantity <= PickupMaxStack)
	{
		// Case: whole pickup fits in the empty slot. TRANSFER the instance — keep its
		// UUID (parity with the BP writing the ground item's Guid into the slot) so the
		// attachment Guid links keep resolving; the closure travels with it.
		FItemInstance Incoming = WorldInstance;
		Incoming.IsWorldItem = false;
		AddExistingItem(Incoming);
		AddInstancesPreservingIds(AttachedInstances);

		if (Slots.IsValidIndex(CompatibleSlot.SlotIndex))
		{
			Slots[CompatibleSlot.SlotIndex].ItemID = Incoming.UUID; // SlotType preserved
		}
		OnInventoryChanged.Broadcast(CompatibleSlot.SlotIndex);
		RefreshIsInventoryFull();

		bAnyTaken = true;
		OnItemPickup.Broadcast(Incoming, PickupQuantity);
		return 0;
	}

	// Case: pickup exceeds one stack — create a NEW full stack (fresh Guid, parity with
	// CreateItemInInventoryAndDatabase) in the slot; the remainder stays on the ground.
	FItemInstance Template = WorldInstance;
	Template.Quantity = PickupMaxStack;
	Template.IsWorldItem = false;

	FItemInstance Created;
	FGuid CreatedId;
	CreateItemInSlot(CompatibleSlot.SlotIndex, CompatibleSlot.SlotType, WorldInstance.RowName,
	                 /*Quantity*/ -1, /*Durability*/ -1.f, Template, Created, CreatedId);

	bAnyTaken = true;
	OnItemPickup.Broadcast(Created, PickupMaxStack);
	return PickupQuantity - PickupMaxStack;
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
	const FInventorySlot Slot = GetSlot(InventoryIndex);
	FItemInstance Instance;
	if (!GetItem(Slot.ItemID, Instance) || Instance.RowName == NAME_None)
	{
		return;
	}

	FItemStructure Def;
	if (!GetItemStructForRow(Instance.RowName, Def))
	{
		ScreenLog(FString::Printf(TEXT("[UInventoryComponent::DropItem] Could not find definition for row name '%s'."), *Instance.RowName.ToString()));
		return;
	}

	// Gather the attachment closure BEFORE removing anything from the store.
	TArray<FItemInstance> Closure;
	CollectClosure(Instance, Closure);
	TArray<FItemInstance> Attached = Closure;
	Attached.RemoveAll([&Instance](const FItemInstance& I) { return I.UUID == Instance.UUID; });

	// Clear the slot. (Deviation from BP, documented: the BP re-made the slot with
	// SlotType=Generic, which would corrupt typed inventories; RemoveItemFromInventory-
	// AndDatabase preserved SlotType — this follows the correct one.)
	if (Slots.IsValidIndex(Slot.SlotIndex))
	{
		Slots[Slot.SlotIndex].ItemID.Invalidate();
	}

	// Spawn the world actor carrying the payload (instance + closure), then remove the
	// instances from this store — the item now lives on the actor. (Old model left the
	// instance in the global DB flagged IsWorldItem=true; the transfer is the
	// per-component equivalent.)
	FItemInstance WorldInstance = Instance;
	WorldInstance.IsWorldItem = true;

	SpawnItemInFrontOfPlayer(Def.Class, WorldInstance, Attached);
	RemoveInstanceAndClosure(Instance);

	OnInventoryChanged.Broadcast(Slot.SlotIndex);
	RefreshIsInventoryFull();
}

AActor* UInventoryComponent::SpawnItemInFrontOfPlayer(TSubclassOf<AActor> ItemClass, const FItemInstance& Instance,
                                                      const TArray<FItemInstance>& AttachedInstances)
{
	// Parity: spawned at the owner's transform (the BP used GetOwner()->GetTransform();
	// with the component on a PlayerState the avatar pawn is the equivalent anchor).
	const AActor* Avatar = ResolveOwnerAvatar();
	const FTransform SpawnTransform = Avatar ? Avatar->GetActorTransform() : FTransform::Identity;
	return SpawnWorldItem(ItemClass, SpawnTransform, Instance, AttachedInstances);
}

AActor* UInventoryComponent::SwapItemFromInventory(TSubclassOf<AActor> InventoryItemClass, AActor* InItemActor, const FItemInstance& ItemInstance)
{
	if (!HasAuthority() || !InItemActor)
	{
		return nullptr;
	}

	// Parity: spawn a replacement world item at the existing actor's transform with the
	// given instance (IsRuntimeSpawn was true on the BP spawn; the interface impl sets it).
	// The BP then fired a dispatcher on the Character — a Blueprint-only reference C++
	// cannot make; the caller receives the spawned actor and can raise any
	// character-side event itself.
	FItemInstance WorldInstance = ItemInstance;
	WorldInstance.IsWorldItem = true;

	TArray<FItemInstance> Attached;
	FItemInstance StoreCopy;
	if (GetItem(ItemInstance.UUID, StoreCopy))
	{
		TArray<FItemInstance> Closure;
		CollectClosure(StoreCopy, Closure);
		Attached = Closure;
		Attached.RemoveAll([&ItemInstance](const FItemInstance& I) { return I.UUID == ItemInstance.UUID; });
	}

	return SpawnWorldItem(InventoryItemClass, InItemActor->GetActorTransform(), WorldInstance, Attached);
}

AActor* UInventoryComponent::SpawnWorldItem(TSubclassOf<AActor> ItemClass, const FTransform& Transform,
                                            FItemInstance Instance, const TArray<FItemInstance>& AttachedInstances)
{
	UWorld* World = GetWorld();
	if (!World || !*ItemClass)
	{
		ScreenLog(TEXT("[UInventoryComponent::SpawnWorldItem] Missing world or item class; cannot spawn."));
		return nullptr;
	}

	AActor* Spawned = World->SpawnActorDeferred<AActor>(ItemClass, Transform, GetOwner(),
	                                                    Cast<APawn>(ResolveOwnerAvatar()),
	                                                    ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Spawned)
	{
		return nullptr;
	}

	if (Spawned->GetClass()->ImplementsInterface(UWorldItemInterface::StaticClass()))
	{
		IWorldItemInterface::Execute_SetWorldItemPayload(Spawned, Instance, AttachedInstances);
	}
	else
	{
		UE_LOG(LogInventoryComponent, Warning,
		       TEXT("[UInventoryComponent::SpawnWorldItem] Spawned actor %s does not implement WorldItemInterface; payload not delivered."),
		       *Spawned->GetName());
	}

	Spawned->FinishSpawning(Transform);
	return Spawned;
}

// =====================================================================================
//  Swap / merge / split
// =====================================================================================

bool UInventoryComponent::SwapSlots(UInventoryComponent* SourceInventory, int32 SourceIndex, int32 DestIndex)
{
	if (!HasAuthority())
	{
		ScreenLog(TEXT("[UInventoryComponent::SwapSlots] Called without authority; use RequestSlotSwap."));
		return false;
	}

	// Src = the inventory the dragged item came from; 'this' = the destination inventory.
	UInventoryComponent* Src = SourceInventory ? SourceInventory : this;

	if (!Src->Slots.IsValidIndex(SourceIndex) || !Slots.IsValidIndex(DestIndex))
	{
		ScreenLog(FString::Printf(TEXT("[SwapSlots] Bad indices: SourceIndex=%d (src has %d), DestIndex=%d (dest has %d)."),
		                          SourceIndex, Src->Slots.Num(), DestIndex, Slots.Num()));
		return false;
	}

	const FInventorySlot SrcSlot = Src->GetSlot(SourceIndex);
	const FInventorySlot DstSlot = GetSlot(DestIndex);

	// Validation gate. The source item's definition must be resolved against the SOURCE
	// component (that's where the dragged item's instance lives) — pass Src so the
	// cross-component lookup checks the right store.
	if (!IsValidSlotSwap(SrcSlot, DstSlot, SourceIndex, DestIndex, Src))
	{
		// IsValidSlotSwap already screen-logs the specific rejection reason.
		return false;
	}

	FItemInstance SrcItem;
	FItemInstance DstItem;
	const bool bSrcHas = Src->GetItem(SrcSlot.ItemID, SrcItem);
	const bool bDstHas = GetItem(DstSlot.ItemID, DstItem);

	if (!bSrcHas)
	{
		ScreenLog(FString::Printf(TEXT("[SwapSlots] Source slot %d has no resolvable item (ID %s)."),
		                          SourceIndex, *SrcSlot.ItemID.ToString()));
		return false;
	}

	// Same item + neither at max -> merge instead of swap (parity).
	if (bDstHas && AreItemsTheSame(SrcItem, DstItem)
		&& !Src->IsItemSizeMax(SrcItem) && !IsItemSizeMax(DstItem))
	{
		MergeAcross(Src, SrcSlot, this, DstSlot);
	}
	else if (Src == this)
	{
		// --- SAME component: just exchange the two slots' ItemIDs (instances already live here). ---
		Slots[SourceIndex].ItemID = DstSlot.ItemID;
		Slots[DestIndex].ItemID = SrcSlot.ItemID;
		MarkSlotsDirty();
	}
	else
	{
		// --- CROSS component: move the instances (and their attachment closures) between
		//     stores, THEN write the slot references. Order matters: collect closures first,
		//     remove from old store, add to new store (preserving UUIDs so slot refs stay valid),
		//     then point each slot at the id that now lives in that component's store. ---
		TArray<FItemInstance> SrcClosure;
		Src->CollectClosure(SrcItem, SrcClosure);

		TArray<FItemInstance> DstClosure;
		if (bDstHas)
		{
			CollectClosure(DstItem, DstClosure);
		}

		// Move source item -> this component.
		Src->RemoveInstanceAndClosure(SrcItem);
		AddInstancesPreservingIds(SrcClosure);

		// Move dest item (if any) -> source component.
		if (bDstHas)
		{
			RemoveInstanceAndClosure(DstItem);
			Src->AddInstancesPreservingIds(DstClosure);
		}

		// Write slot references: dest slot now holds the source item; source slot holds the
		// dest item (or empty). SlotType on each slot is preserved (we only touch ItemID).
		Slots[DestIndex].ItemID = SrcSlot.ItemID;
		Src->Slots[SourceIndex].ItemID = bDstHas ? DstSlot.ItemID : FGuid();

		MarkSlotsDirty();
		Src->MarkSlotsDirty();
	}

	// Broadcast changes on both sides.
	OnInventoryChanged.Broadcast(DestIndex);
	RefreshIsInventoryFull();
	if (Src != this)
	{
		Src->OnInventoryChanged.Broadcast(SourceIndex);
		Src->RefreshIsInventoryFull();
	}
	else
	{
		OnInventoryChanged.Broadcast(SourceIndex);
	}

	return true;
}

bool UInventoryComponent::MergeItems(const FInventorySlot& MyItemSlot, const FInventorySlot& TargetItemSlot)
{
	if (!HasAuthority())
	{
		return false;
	}
	return MergeAcross(this, MyItemSlot, this, TargetItemSlot);
}

bool UInventoryComponent::MergeAcross(UInventoryComponent* SourceInv, const FInventorySlot& SourceSlot,
                                      UInventoryComponent* DestInv, const FInventorySlot& DestSlot)
{
	if (!SourceInv || !DestInv)
	{
		return false;
	}

	FItemInstance SourceItem;
	FItemInstance DestItem;
	if (!SourceInv->GetItem(SourceSlot.ItemID, SourceItem) || !DestInv->GetItem(DestSlot.ItemID, DestItem))
	{
		return false;
	}

	// Parity: MergeItems math — sum, cap at max, remainder back to source (or source removed).
	if (!AreItemsTheSame(SourceItem, DestItem))
	{
		return false;
	}

	FItemStructure Def;
	if (!GetItemStructForRow(DestItem.RowName, Def))
	{
		return false;
	}

	const int32 ItemSum = SourceItem.Quantity + DestItem.Quantity;
	const int32 NewTargetQty = FMath::Min(ItemSum, Def.MaxStackSize);
	const int32 NewSourceQty = ItemSum - NewTargetQty;

	DestItem.Quantity = NewTargetQty;
	DestInv->UpdateItemInSlot(DestSlot, DestItem);

	if (NewSourceQty == 0)
	{
		SourceInv->RemoveItemFromSlot(SourceSlot);
	}
	else
	{
		SourceItem.Quantity = NewSourceQty;
		SourceInv->UpdateItemInSlot(SourceSlot, SourceItem);
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
	FInventorySlot EmptySlot;
	const bool bInventoryFull = GetNextEmptySlot(EmptySlot);
	if (bInventoryFull)
	{
		return;
	}

	FItemInstance SourceInstance;
	if (!GetItem(SplittingItemSlot.ItemID, SourceInstance))
	{
		return;
	}

	// Safety clamp (documented addition): the BP had no guard; splitting >= the whole
	// stack would drive the source negative. Reject out-of-range splits.
	if (SplitQuantity < 1 || SplitQuantity >= SourceInstance.Quantity)
	{
		UE_LOG(LogInventoryComponent, Warning, TEXT("[UInventoryComponent::SplitSlotStack] Invalid SplitQuantity %d for stack of %d."),
		       SplitQuantity, SourceInstance.Quantity);
		return;
	}

	// Parity: source keeps the remainder...
	SourceInstance.Quantity -= SplitQuantity;
	UpdateItemInSlot(SplittingItemSlot, SourceInstance);

	// ...and a NEW item (fresh Guid, template = source so modifiers/durability copy)
	// lands in the empty slot with the split quantity.
	FItemInstance Created;
	FGuid CreatedId;
	CreateItemInSlot(EmptySlot.SlotIndex, EmptySlot.SlotType, SourceInstance.RowName,
	                 SplitQuantity, /*Durability*/ -1.f, SourceInstance, Created, CreatedId);
}

// =====================================================================================
//  Fused writers (the old private "...InInventoryAndDatabase" trio)
// =====================================================================================

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
	{
		return false;
	}

	Slots[SlotIndex].SlotIndex = SlotIndex;
	Slots[SlotIndex].SlotType = InSlotType;
	Slots[SlotIndex].ItemID = OutItemID;

	OnInventoryChanged.Broadcast(SlotIndex);
	RefreshIsInventoryFull();
	return true;
}

bool UInventoryComponent::UpdateItemInSlot(const FInventorySlot& ItemSlot, const FItemInstance& ItemInstance)
{
	// Parity: only updates an instance that exists.
	if (!ContainsItem(ItemSlot.ItemID))
	{
		return false;
	}
	if (!UpdateItem(ItemSlot.ItemID, ItemInstance))
	{
		return false;
	}

	// Parity: the BP wrote the slot back and broadcast OnInventoryChanged. (The BP's
	// success path fell off without reaching a return node — it always reported false;
	// returning true here is the obvious correction, and no caller depended on it.)
	if (Slots.IsValidIndex(ItemSlot.SlotIndex))
	{
		Slots[ItemSlot.SlotIndex] = ItemSlot;
	}
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
	}

	OnInventoryChanged.Broadcast(InventorySlotToBeRemoved.SlotIndex);
	RefreshIsInventoryFull();
	return true;
}

void UInventoryComponent::SetSlot(int32 Index, const FInventorySlot& Slot)
{
	// Parity: SetSlot only transfers the ItemID; SlotIndex/SlotType stay.
	if (Slots.IsValidIndex(Index))
	{
		Slots[Index].ItemID = Slot.ItemID;
		// No broadcast here — parity: callers (SwapSlots) broadcast themselves.
	}
}

// =====================================================================================
//  Setup / persistence
// =====================================================================================

void UInventoryComponent::InitializeInventory()
{
	// Parity: guarded by IsDataInitialized; builds MaxInventorySlots slots of SlotType.
	if (IsDataInitialized)
	{
		return;
	}
	IsDataInitialized = true;

	Slots.Reset(MaxInventorySlots);
	for (int32 i = 0; i < MaxInventorySlots; ++i)
	{
		FInventorySlot NewSlot;
		NewSlot.SlotIndex = i;
		NewSlot.SlotType = SlotType;
		NewSlot.ItemID.Invalidate();
		Slots.Add(NewSlot);
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
	{
		return;
	}

	for (const FDefaultInventoryEntry& Entry : DefaultItems)
	{
		if (!Slots.IsValidIndex(Entry.SlotIndex))
		{
			UE_LOG(LogInventoryComponent, Warning, TEXT("[UInventoryComponent::FillDefaultInventory] Slot index %d out of range for row '%s'."),
			       Entry.SlotIndex, *Entry.RowHandle.RowName.ToString());
			continue;
		}

		FItemInstance Created;
		FGuid CreatedId;
		CreateItemInSlot(Entry.SlotIndex, Slots[Entry.SlotIndex].SlotType, Entry.RowHandle.RowName,
		                 Entry.Quantity, /*Durability*/ -1.f, FItemInstance(), Created, CreatedId);
	}
}

void UInventoryComponent::SaveToRecord(FInventoryRecord& OutRecord) const
{
	OutRecord.Items.Reset(InventoryList.Entries.Num());
	for (const FInventoryItemEntry& Entry : InventoryList.Entries)
	{
		OutRecord.Items.Add(Entry.Instance);
	}
	OutRecord.Slots = Slots;
}

void UInventoryComponent::LoadFromRecord(const FInventoryRecord& Record)
{
	if (!HasAuthority())
	{
		UE_LOG(LogInventoryComponent, Warning, TEXT("[UInventoryComponent::LoadFromRecord] Called without authority; ignored."));
		return;
	}

	InventoryList.Entries.Reset(Record.Items.Num());
	for (const FItemInstance& Instance : Record.Items)
	{
		InventoryList.Entries.Add(FInventoryItemEntry(Instance));
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
	{
		return;
	}

	InventoryList.Entries.Reset();
	InventoryList.MarkArrayDirty();
	for (FInventorySlot& Slot : Slots)
	{
		Slot.ItemID.Invalidate();
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

	TArray<FGuid> Pending;
	TSet<FGuid> Visited;

	OutClosure.Add(Root);
	Visited.Add(Root.UUID);
	for (const TPair<ESlotType, FGuid>& Pair : Root.WeaponAttachments)
	{
		Pending.Add(Pair.Value);
	}

	while (Pending.Num() > 0)
	{
		const FGuid Id = Pending.Pop();
		if (!Id.IsValid() || Visited.Contains(Id))
		{
			continue;
		}
		Visited.Add(Id);

		FItemInstance Instance;
		if (GetItem(Id, Instance))
		{
			OutClosure.Add(Instance);
			for (const TPair<ESlotType, FGuid>& Pair : Instance.WeaponAttachments)
			{
				Pending.Add(Pair.Value);
			}
		}
	}
}

void UInventoryComponent::RemoveInstanceAndClosure(const FItemInstance& Root)
{
	TArray<FItemInstance> Closure;
	CollectClosure(Root, Closure);
	for (const FItemInstance& Instance : Closure)
	{
		RemoveItem(Instance.UUID);
	}
}

void UInventoryComponent::AddInstancesPreservingIds(const TArray<FItemInstance>& Instances)
{
	for (const FItemInstance& Instance : Instances)
	{
		AddExistingItem(Instance);
	}
}

// =====================================================================================
//  Replication plumbing / cache / broadcasts
// =====================================================================================

void UInventoryComponent::HandleReplicatedItemsChanged(const TArray<FGuid>& ChangedIds)
{
	RebuildIdCache();
	for (const FGuid& Id : ChangedIds)
	{
		BroadcastSlotChangedForItem(Id);
	}
}

void UInventoryComponent::HandleReplicatedItemsRebuilt()
{
	RebuildIdCache();
}

void UInventoryComponent::OnRep_Slots(const TArray<FInventorySlot>& PreviousSlots)
{
	const int32 MaxCount = FMath::Max(PreviousSlots.Num(), Slots.Num());
	for (int32 i = 0; i < MaxCount; ++i)
	{
		const bool bHadOld = PreviousSlots.IsValidIndex(i);
		const bool bHasNew = Slots.IsValidIndex(i);
		if (!bHadOld || !bHasNew)
		{
			OnInventoryChanged.Broadcast(i);
			continue;
		}
		const FInventorySlot& Old = PreviousSlots[i];
		const FInventorySlot& New = Slots[i];
		if (Old.ItemID != New.ItemID || Old.SlotType != New.SlotType || Old.SlotIndex != New.SlotIndex)
		{
			OnInventoryChanged.Broadcast(i);
		}
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
	{
		RefreshIsInventoryFull();
	}
}

void UInventoryComponent::MarkItemDirtyById(FGuid ItemID)
{
	for (FInventoryItemEntry& Entry : InventoryList.Entries)
	{
		if (Entry.Instance.UUID == ItemID)
		{
			InventoryList.MarkItemDirty(Entry);
			return;
		}
	}
}

void UInventoryComponent::BroadcastSlotChangedForItem(FGuid ItemID)
{
	const int32 SlotIndex = FindSlotIndexByItemId(ItemID);
	if (SlotIndex != INDEX_NONE)
	{
		OnInventoryChanged.Broadcast(SlotIndex);
	}
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
	{
		return INDEX_NONE;
	}
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		if (Slots[i].ItemID == ItemID)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

void UInventoryComponent::RefreshIsInventoryFull()
{
	FInventorySlot Dummy;
	IsInventoryFull = GetNextEmptySlot(Dummy);
}

AActor* UInventoryComponent::ResolveOwnerAvatar() const
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}

	AActor* Avatar = Owner;
	if (const APlayerState* PS = Cast<APlayerState>(Owner))
	{
		Avatar = PS->GetPawn();
	}
	else if (const AController* Controller = Cast<AController>(Owner))
	{
		Avatar = Controller->GetPawn();
	}

	// Cache the parity ref for BP reads.
	if (Avatar)
	{
		const_cast<UInventoryComponent*>(this)->Character = Avatar;
	}
	return Avatar;
}

// =====================================================================================
//  Definition lookups
// =====================================================================================

bool UInventoryComponent::GetItemStructForRow(FName RowName, FItemStructure& OutStruct)
{
	if (RowName == NAME_None)
	{
		OutStruct = FItemStructure();
		return false;
	}
	return UItemLibrary::GetItemStructTyped(RowName, OutStruct);
}

bool UInventoryComponent::GetWeaponRowForName(FName RowName, FWeaponRow& OutRow)
{
	// Mirrors UItemLibrary::GetTableForRowName (private there): the row-name prefix
	// selects the table via the UItemTableSettings map; the row copies only when the
	// table's row struct actually is FWeaponRow (GetItemDef's exact-match behavior).
	OutRow = FWeaponRow();
	if (RowName == NAME_None)
	{
		return false;
	}

	const UItemTableSettings* Settings = GetDefault<UItemTableSettings>();
	if (!Settings)
	{
		return false;
	}

	const FString RowStr = RowName.ToString();
	UDataTable* Table = nullptr;
	for (const TPair<FString, TSoftObjectPtr<UDataTable>>& Pair : Settings->PrefixToTable)
	{
		if (RowStr.StartsWith(Pair.Key))
		{
			Table = Pair.Value.LoadSynchronous();
			break;
		}
	}
	if (!Table || !Table->GetRowStruct() || !Table->GetRowStruct()->IsChildOf(FWeaponRow::StaticStruct()))
	{
		return false;
	}

	const FWeaponRow* Found = Table->FindRow<FWeaponRow>(RowName, TEXT("InventoryComponent::GetWeaponRowForName"), false);
	if (!Found)
	{
		return false;
	}
	OutRow = *Found;
	return true;
}

bool UInventoryComponent::AreModifierListsEqual(const TArray<FModifierPayload>& A, const TArray<FModifierPayload>& B)
{
	// Parity with the BP "unique items between arrays == 0" macro: order-insensitive
	// multiset equality on (Tag, Value, Color).
	if (A.Num() != B.Num())
	{
		return false;
	}

	TArray<FModifierPayload> Remaining = B;
	for (const FModifierPayload& Mod : A)
	{
		const int32 FoundIndex = Remaining.IndexOfByPredicate([&Mod](const FModifierPayload& Other)
		{
			return Other.ModifierRowName == Mod.ModifierRowName
				&& Other.Value == Mod.Value
				&& Other.Color == Mod.Color;
		});
		if (FoundIndex == INDEX_NONE)
		{
			return false;
		}
		Remaining.RemoveAtSwap(FoundIndex);
	}
	return true;
}

// =====================================================================================
//  Debug (parity)
// =====================================================================================

void UInventoryComponent::LogInventory(float PrintDuration) const
{
	for (const FInventorySlot& Slot : Slots)
	{
		LogInventorySlot(Slot, PrintDuration);
	}
}

void UInventoryComponent::LogInventorySlot(const FInventorySlot& InventorySlot, float Duration) const
{
	FItemInstance Instance;
	const bool bFound = GetItem(InventorySlot.ItemID, Instance);

	const FString Message = FString::Printf(
		TEXT("[%s] Slot %d (%s): %s"),
		*InventoryName.ToString(),
		InventorySlot.SlotIndex,
		*UEnum::GetValueAsString(InventorySlot.SlotType),
		bFound
			? *FString::Printf(TEXT("%s x%d (Dur %.1f, ID %s)"),
			                   *Instance.RowName.ToString(), Instance.Quantity, Instance.Durability,
			                   *Instance.UUID.ToString(EGuidFormats::DigitsWithHyphens))
			: TEXT("empty"));

	UE_LOG(LogInventoryComponent, Log, TEXT("%s"), *Message);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, Duration, FColor::Cyan, Message);
	}
}