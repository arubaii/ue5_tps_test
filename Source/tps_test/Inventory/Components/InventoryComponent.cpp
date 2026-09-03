// Copyright arubaii. All rights reserved.

#include "Inventory/Components/InventoryComponent.h"
#include "Audio/Cue/AudioCueSet.h"
#include "Audio/Cue/AudioCueLibrary.h"
#include "Inventory/Tags/InventoryAudioTags.h" // adjust path if placed elsewhere


#include "Inventory/ItemLibrary.h"
#include "Inventory/Structs/Rows.h"
#include "Inventory/Fragment/ItemFragment.h"
#include "Inventory/Fragment/ItemFragmentLibrary.h"
#include "Inventory/WorldItemInterface.h"
#include "Utils/BlueprintFunctionLibraries/Array/ArrayDifference.h"

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
#include "UObject/UObjectIterator.h"

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
	//
	// This ran TWICE before — the block was pasted in again below the logging. The second
	// InitializeInventory was harmless (IsDataInitialized guards it) but the second
	// FillDefaultInventory was not: it re-seeded every default entry, and only the
	// occupancy check inside CreateItemInSlot kept that from duplicating the whole
	// starting loadout.
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

	// PASS 2: an empty region matching the item's footprint, anchored at a slot whose
	// SlotType is in the item's CompatibleSlotTypes. On non-grid containers the footprint
	// is always (1,1), so this reduces to the original single-cell check.
	FItemDefinition def;
	if (GetItemStructForRow(ItemToBeCheckedAgainst.RowName, def))
	{
		const FIntPoint size = GetItemFootprintSize(ItemToBeCheckedAgainst.RowName);

		for (int32 i = 0; i < Slots.Num(); ++i)
		{
			const FInventorySlot& candidate = Slots[i];
			FItemInstance slotInstance;
			const bool bEmpty = !GetItem(candidate.ItemID, slotInstance);
			if (!bEmpty)
				continue;

			if (!CanPlaceFootprintAt(i, size))
				continue;

			// Check the WHOLE footprint's slot types, not just the anchor cell.
			if (!IsFootprintSlotTypeCompatible(i, size, def))
				continue;

			Slot = candidate;
			return true;
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

bool UInventoryComponent::WouldMergeSlots(UInventoryComponent* SourceInventory, int32 SourceIndex, int32 DestIndex) const
{
	const UInventoryComponent* src = SourceInventory ? SourceInventory : this;
	if (!src->Slots.IsValidIndex(SourceIndex) || !Slots.IsValidIndex(DestIndex))
		return false;

	FItemInstance srcItem;
	FItemInstance dstItem;
	if (!src->GetItem(src->GetSlot(SourceIndex).ItemID, srcItem))
		return false;
	if (!GetItem(GetSlot(DestIndex).ItemID, dstItem))
		return false;
	if (dstItem.UUID == srcItem.UUID)
		return false;

	return srcItem.HasFragment<FStackableFragment>()
		&& AreItemsTheSame(srcItem, dstItem)
		&& !src->IsItemSizeMax(srcItem)
		&& !IsItemSizeMax(dstItem);
}

bool UInventoryComponent::IsValidSlotSwap(const FInventorySlot& MyInventorySlot,
										  const FInventorySlot& TargetInventorySlot,
										  int32 MyIndex, int32 TargetIndex, UInventoryComponent* SourceInventory) const
{
	// Semantics (matching SwapSlots' call):
	//   MyInventorySlot     = the SOURCE slot (dragged item), living in SourceInventory.
	//   TargetInventorySlot = the DEST slot (this component).
	//   SourceInventory     = the component the dragged item came from (may be 'this').
	//
	// Answers exactly the question SwapSlots asks, in the same order: merge first, then
	// the swap plan. MyInventorySlot/TargetInventorySlot aren't consulted directly —
	// both paths re-resolve everything from MyIndex/TargetIndex themselves, so a slot
	// struct the UI captured a frame ago can't disagree with live state.
	if (WouldMergeSlots(SourceInventory, MyIndex, TargetIndex))
		return true;

	FInventorySwapPlan plan;
	return BuildSwapPlan(SourceInventory, MyIndex, TargetIndex, plan);
}

bool UInventoryComponent::PreviewSlotSwap(UInventoryComponent* SourceInventory, int32 SourceIndex, int32 DestIndex,
										  TArray<int32>& OutDraggedCells, TArray<bool>& OutDraggedCellValid,
										  TArray<int32>& OutDisplacedCells) const
{
	OutDraggedCells.Reset();
	OutDraggedCellValid.Reset();
	OutDisplacedCells.Reset();

	const UInventoryComponent* src = SourceInventory ? SourceInventory : this;
	const bool bSameContainer = (src == this);

	// A merge doesn't move anything: the highlight is just the destination footprint,
	// and a merge is always onto a compatible slot by definition, so every cell is valid.
	if (WouldMergeSlots(SourceInventory, SourceIndex, DestIndex))
	{
		FItemInstance dstItem;
		if (GetItem(GetSlot(DestIndex).ItemID, dstItem))
			OutDraggedCells = GetFootprintCellsForItem(dstItem.UUID);
		else
			OutDraggedCells.Add(DestIndex);

		OutDraggedCellValid.Init(true, OutDraggedCells.Num());
		return true;
	}

	FInventorySwapPlan plan;
	const bool bValid = BuildSwapPlan(SourceInventory, SourceIndex, DestIndex, plan);

	FItemInstance srcItem;
	FItemDefinition srcDef;
	const bool bHaveSrcDef = src->Slots.IsValidIndex(SourceIndex)
		&& src->GetItem(src->GetSlot(SourceIndex).ItemID, srcItem)
		&& GetItemStructForRow(srcItem.RowName, srcDef);

	// The cells the dragged item currently sits on. Under the cover-rule hover
	// resolution a nudge's new footprint overlaps the old one, and re-reporting cells
	// the item is already on makes the highlight read as "nothing is happening" — so
	// those are dropped and only the ground the item is actually gaining lights up.
	TSet<int32> alreadyOccupied;
	if (bSameContainer && plan.DraggedItemID.IsValid())
	{
		for (int32 idx : GetFootprintCellsForItem(plan.DraggedItemID))
			alreadyOccupied.Add(idx);
	}

	// Fills OutDraggedCells/OutDraggedCellValid from the dragged item's planned
	// footprint. Used for BOTH outcomes: every cell true when the plan stands, and a
	// per-cell slot-type check when it doesn't — DraggedNewAnchor is only ever set once
	// planning actually starts, so a refusal before that (bad index, or a hover that
	// resolves back to the item's own anchor) leaves it INDEX_NONE and correctly
	// produces no highlight at all.
	if (Slots.IsValidIndex(plan.DraggedNewAnchor))
	{
		const FIntPoint a = IndexToXY(plan.DraggedNewAnchor);
		for (int32 y = a.Y; y < a.Y + plan.DraggedSize.Y; ++y)
		{
			for (int32 x = a.X; x < a.X + plan.DraggedSize.X; ++x)
			{
				const int32 idx = XYToIndex(x, y);
				if (!Slots.IsValidIndex(idx) || alreadyOccupied.Contains(idx))
					continue;

				// When the whole drop is valid, every cell in it is valid by construction.
				// When it isn't, blame the specific cells when the reason IS specific — an
				// incompatible slot type belongs to one cell. Anything else the planner
				// rejects for (a bystander pushed outside the confine, no room left inside
				// it) is a property of the arrangement rather than of any one cell, so
				// there's nothing to single out and every cell in the attempted footprint
				// reads invalid together.
				const bool bCellValid = bValid
					|| !bHaveSrcDef
					|| srcDef.CompatibleSlotTypes.Contains(Slots[idx].SlotType);

				OutDraggedCells.Add(idx);
				OutDraggedCellValid.Add(bCellValid);
			}
		}
	}

	if (!bValid)
		return false;

	for (const FInventorySwapPlan::FDisplacedItem& displaced : plan.Displaced)
	{
		if (!src->Slots.IsValidIndex(displaced.NewAnchorInSource))
			continue;

		const FIntPoint displacedXY = src->IndexToXY(displaced.NewAnchorInSource);
		for (int32 y = displacedXY.Y; y < displacedXY.Y + displaced.Size.Y; ++y)
			for (int32 x = displacedXY.X; x < displacedXY.X + displaced.Size.X; ++x)
				OutDisplacedCells.Add(src->XYToIndex(x, y));
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

	// Auto sort (class defaults). No-op unless AutoSort is enabled on this component.
	if (bAnyTaken)
		TryAutoSort();

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
			OccupyFootprint(compatibleSlot.SlotIndex, GetItemFootprintSize(incoming.RowName), incoming.UUID);
		BroadcastFootprintForItem(incoming.UUID);
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

	// Clear every cell this item occupies. (Deviation from BP, documented: the BP
	// re-made the slot with SlotType=Generic, which would corrupt typed inventories;
	// RemoveItemFromInventoryAndDatabase preserved SlotType — this follows the correct
	// one, and only ever touches ItemID/bIsAnchor.)
	const TArray<int32> footprintCells = GetFootprintCellsForItem(instance.UUID);
	ClearFootprintForItem(instance.UUID);

	// Spawn the world actor carrying the payload (instance + closure), then remove the
	// instances from this store — the item now lives on the actor. (Old model left the
	// instance in the global DB flagged IsWorldItem=true; the transfer is the
	// per-component equivalent.)
	FItemInstance worldInstance = instance;
	worldInstance.IsWorldItem = true;

	SpawnItemInFrontOfPlayer(def.Class, worldInstance, attached);
	RemoveInstanceAndClosure(instance);

	if (footprintCells.Num() > 0)
	{
		for (int32 idx : footprintCells)
			OnInventoryChanged.Broadcast(idx);
	}
	else
		OnInventoryChanged.Broadcast(slot.SlotIndex);
	RefreshIsInventoryFull();

	// Auto sort (class defaults). No-op unless AutoSort is enabled on this component.
	TryAutoSort();
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

	FItemInstance srcItem;
	if (!src->GetItem(srcSlot.ItemID, srcItem))
	{
		ScreenLog(FString::Printf(TEXT("[SwapSlots] Source slot %d has no resolvable item (ID %s)."),
		                          SourceIndex, *srcSlot.ItemID.ToString()));
		return false;
	}

	// =============================================================================================================
	// CASE 1: stack merge — quantities change in place, footprints never move.
	// =============================================================================================================
	if (WouldMergeSlots(src, SourceIndex, DestIndex))
	{
		MergeAcross(src, srcSlot, this, dstSlot);
		OnInventoryChanged.Broadcast(DestIndex);
		RefreshIsInventoryFull();
		if (src != this)
		{
			src->OnInventoryChanged.Broadcast(SourceIndex);
			src->RefreshIsInventoryFull();
		}
		else
			OnInventoryChanged.Broadcast(SourceIndex);

		// This is a stack merge, not a relocation — its own tag, not SlotSwap/
		// CrossInventorySwap, since a merge sounds (and is) a different action than
		// two items trading places.
		Multicast_PlayInventoryCue(InventoryAudioTags::Merge);
		return true;
	}

	// =============================================================================================================
	// CASE 2: general relocation — covers drop-into-empty-space, a clean 1-for-1 swap
	// (in either size direction), a "shift" move onto its own cells, and multi-item
	// swaps, all through one plan. See BuildSwapPlan's header comment for the confine
	// rules that decide which of those are legal and where displaced items land.
	// =============================================================================================================
	FInventorySwapPlan plan;
	if (!BuildSwapPlan(src, SourceIndex, DestIndex, plan))
	{
		Multicast_PlayInventoryCue(InventoryAudioTags::InvalidSlotSwap);
		return false;
	}

	// Cells each relocating item currently occupies, captured before anything moves —
	// needed for broadcasting the "old" side once each ItemID has moved off of them.
	const TArray<int32> oldSrcCells = src->GetFootprintCellsForItem(srcItem.UUID);
	TArray<TPair<FGuid, TArray<int32>>> oldDisplacedCells;
	oldDisplacedCells.Reserve(plan.Displaced.Num());
	for (const FInventorySwapPlan::FDisplacedItem& displaced : plan.Displaced)
		oldDisplacedCells.Add(TPair<FGuid, TArray<int32>>(displaced.ItemID, GetFootprintCellsForItem(displaced.ItemID)));

	// --- Vacate every footprint involved before writing anything back. ---
	src->ClearFootprintForItem(srcItem.UUID);
	for (const FInventorySwapPlan::FDisplacedItem& displaced : plan.Displaced)
		ClearFootprintForItem(displaced.ItemID);

	// --- Cross-component instance moves (no-ops when src == this). Order matters:
	//     collect closures first, remove from old store, add to new store (preserving
	//     UUIDs so slot refs stay valid), for the dragged item and every displaced item. ---
	if (src != this)
	{
		TArray<FItemInstance> srcClosure;
		src->CollectClosure(srcItem, srcClosure);
		src->RemoveInstanceAndClosure(srcItem);
		AddInstancesPreservingIds(srcClosure);

		for (const FInventorySwapPlan::FDisplacedItem& displaced : plan.Displaced)
		{
			FItemInstance displacedItem;
			if (!GetItem(displaced.ItemID, displacedItem))
				continue;
			TArray<FItemInstance> displacedClosure;
			CollectClosure(displacedItem, displacedClosure);
			RemoveInstanceAndClosure(displacedItem);
			src->AddInstancesPreservingIds(displacedClosure);
		}
	}

	// --- Write the new footprints. SlotType per cell is preserved (footprint writes
	//     only ever touch ItemID/bIsAnchor). ---
	OccupyFootprint(plan.DraggedNewAnchor, plan.DraggedSize, srcItem.UUID);
	for (const FInventorySwapPlan::FDisplacedItem& displaced : plan.Displaced)
		src->OccupyFootprint(displaced.NewAnchorInSource, displaced.Size, displaced.ItemID);

	MarkSlotsDirty();
	if (src != this)
		src->MarkSlotsDirty();

	// --- Broadcast every cell any relocated item touched — its old position(s) and its
	//     new one(s) — on both sides. ---
	TArray<int32> destTouched = GetFootprintCellsForItem(srcItem.UUID);
	for (const TPair<FGuid, TArray<int32>>& pair : oldDisplacedCells)
		destTouched.Append(pair.Value);
	if (src == this)
		destTouched.Append(oldSrcCells);
	for (int32 idx : destTouched)
		OnInventoryChanged.Broadcast(idx);
	RefreshIsInventoryFull();

	if (src != this)
	{
		TArray<int32> srcTouched = oldSrcCells;
		for (const FInventorySwapPlan::FDisplacedItem& displaced : plan.Displaced)
			srcTouched.Append(src->GetFootprintCellsForItem(displaced.ItemID));
		for (int32 idx : srcTouched)
			src->OnInventoryChanged.Broadcast(idx);
		src->RefreshIsInventoryFull();
	}

	// Auto sort (class defaults), CROSS-CONTAINER ONLY. A move inside one container
	// is the player arranging things by hand; re-sorting on top of that would drag
	// the item straight back out from under them. Both sides get the chance, since
	// one gained an item and the other lost one.
	if (src != this)
	{
		TryAutoSort();
		src->TryAutoSort();
	}

	Multicast_PlayInventoryCue(src != this ? InventoryAudioTags::CrossInventorySwap : InventoryAudioTags::SlotSwap);

	return true;
}

void UInventoryComponent::Multicast_PlayInventoryCue_Implementation(FGameplayTag CueTag)
{
	UAudioCueLibrary::PlayCue2D(this, AudioCues, CueTag);
}

void UInventoryComponent::MergeAll(const FInventorySlot& MyItemSlot)
{
	if (!HasAuthority())
	{
		ScreenLog(TEXT("[UInventoryComponent::MergeAll] Called without authority."));
		return;
	}

	FItemInstance destItem;
	if (!GetItem(MyItemSlot.ItemID, destItem))
		return;

	// Nothing to merge for an item that can't stack in the first place.
	if (!destItem.HasFragment<FStackableFragment>())
		return;

	// Repeatedly pull matching stacks into MyItemSlot. Stop once it's full, or once a
	// full pass over the slots finds no further compatible stack to fold in.
	bool bMergedThisPass = true;
	bool bAnyMerged = false;
	while (bMergedThisPass && !IsItemSizeMax(destItem))
	{
		bMergedThisPass = false;

		for (const FInventorySlot& candidate : Slots)
		{
			// Only consider anchor cells: a multi-cell item's shadow cells share its
			// ItemID, and re-checking them would just waste passes re-testing the same
			// item (or, worse, the destination's own footprint).
			if (!candidate.bIsAnchor || candidate.ItemID == MyItemSlot.ItemID)
				continue;

			FItemInstance candidateItem;
			if (!GetItem(candidate.ItemID, candidateItem))
				continue;

			if (!AreItemsTheSame(destItem, candidateItem))
				continue;

			if (!MergeAcross(this, candidate, this, MyItemSlot))
				continue;

			// The destination's quantity just changed (and may now be maxed); re-fetch
			// before asking IsItemSizeMax or trying another candidate.
			if (!GetItem(MyItemSlot.ItemID, destItem))
			{
				if (bAnyMerged)
					Multicast_PlayInventoryCue(InventoryAudioTags::MergeAll);
				return;
			}

			bMergedThisPass = true;
			bAnyMerged = true;

			// A merge can clear the source slot entirely, which reshuffles what "the
			// next candidate" means for a footprint-aware store — restart the scan
			// rather than trust the rest of this range as still meaningful.
			break;
		}
	}

	// One cue for the whole consolidation pass, not one per fold — MergeAll can pull in
	// several stacks in a single call, and a rapid burst of the same sound would read as
	// a glitch rather than "your inventory got tidied."
	if (bAnyMerged)
		Multicast_PlayInventoryCue(InventoryAudioTags::MergeAll);
}

void UInventoryComponent::MergeAllStacks()
{
	if (!HasAuthority())
	{
		ScreenLog(TEXT("[UInventoryComponent::MergeAllStacks] Called without authority."));
		return;
	}

	// Snapshot every distinct item up front. MergeAll mutates Slots as it runs — clearing
	// fully-consumed stacks, changing quantities, potentially reflowing the store — so
	// scanning Slots live while also merging into it would either skip items or revisit
	// the same one repeatedly. IDs are stable across that churn; re-resolve each one's
	// current anchor at the point we actually use it.
	TArray<FGuid> anchorItemIDs;
	TSet<FGuid> seen;
	for (const FInventorySlot& slot : Slots)
	{
		if (!slot.bIsAnchor || !slot.ItemID.IsValid() || seen.Contains(slot.ItemID))
			continue;

		seen.Add(slot.ItemID);
		anchorItemIDs.Add(slot.ItemID);
	}

	for (const FGuid& itemID : anchorItemIDs)
	{
		FItemInstance instance;

		// Not found means an earlier iteration's MergeAll already folded this item's
		// entire quantity into another stack — there's nothing left of it to merge.
		// Not stackable means there's nothing to do for it in the first place.
		if (!GetItem(itemID, instance) || !instance.HasFragment<FStackableFragment>())
			continue;

		const int32 anchorIndex = FindAnchorIndexForItem(itemID);
		if (!Slots.IsValidIndex(anchorIndex))
			continue;

		MergeAll(Slots[anchorIndex]);
	}
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

	FItemInstance sourceInstance;
	if (!GetItem(SplittingItemSlot.ItemID, sourceInstance))
		return;

	// Parity: no empty slot -> nothing happens. Sized to the item's own footprint (almost
	// always 1x1 for splittable/stackable items, but this stays correct if that changes).
	FInventorySlot emptySlot;
	int32 emptyAnchor = INDEX_NONE;
	if (!FindFirstFitForSize(GetItemFootprintSize(sourceInstance.RowName), emptyAnchor))
		return;
	emptySlot = GetSlot(emptyAnchor);

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

	// Auto sort (class defaults). No-op unless AutoSort is enabled on this component.
	TryAutoSort();
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

	// Resolve the footprint BEFORE registering the item, so a bad placement doesn't leave
	// an orphaned instance sitting in the store with nowhere it was actually validated to go.
	const FName effectiveRow = (RowName != NAME_None) ? RowName : Template.RowName;
	const FIntPoint size = GetItemFootprintSize(effectiveRow);

	if (!CanPlaceFootprintAt(SlotIndex, size))
	{
		UE_LOG(LogInventoryComponent, Warning,
		       TEXT("[UInventoryComponent::CreateItemInSlot] Item '%s' (%dx%d) does not fit at slot %d."),
		       *effectiveRow.ToString(), size.X, size.Y, SlotIndex);
		OutInstance = FItemInstance();
		OutItemID = FGuid();
		return false;
	}

	if (!CreateAndRegisterItem(RowName, Quantity, Durability, Template, OutInstance, OutItemID))
		return false;

	Slots[SlotIndex].SlotType = InSlotType; // anchor slot's type; footprint writes never touch SlotType
	OccupyFootprint(SlotIndex, size, OutItemID);

	BroadcastFootprintForItem(OutItemID);
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
	//
	// Only the stored INSTANCE changed here. The old code assigned the caller's whole
	// slot struct over the live one, which clobbered bIsAnchor/SlotType whenever that
	// struct was built by hand or captured before a move — enough to desync a multi-cell
	// item's footprint. Slot layout belongs to the footprint helpers, so re-point the
	// cell through SetSlot (footprint-aware) and leave the rest alone.
	if (Slots.IsValidIndex(ItemSlot.SlotIndex) && Slots[ItemSlot.SlotIndex].ItemID != ItemSlot.ItemID)
		SetSlot(ItemSlot.SlotIndex, ItemSlot);

	// Repaint every cell the item covers, not just the one the caller named: a quantity
	// or durability change has to refresh the whole widget of a multi-cell item.
	const TArray<int32> cells = GetFootprintCellsForItem(ItemSlot.ItemID);
	if (cells.Num() > 0)
	{
		for (int32 idx : cells)
			OnInventoryChanged.Broadcast(idx);
	}
	else
		OnInventoryChanged.Broadcast(ItemSlot.SlotIndex);

	return true;
}

bool UInventoryComponent::RemoveItemFromSlot(const FInventorySlot& InventorySlotToBeRemoved)
{
	const FGuid itemId = InventorySlotToBeRemoved.ItemID;

	// Capture every cell this item occupies BEFORE removing it from the store — a
	// multi-cell item's shadow cells aren't at InventorySlotToBeRemoved.SlotIndex, so
	// clearing only that one index would leave the rest of its footprint stuck occupied.
	const TArray<int32> footprintCells = GetFootprintCellsForItem(itemId);

	RemoveItem(itemId);

	if (footprintCells.Num() > 0)
	{
		ClearFootprintForItem(itemId);
		for (int32 idx : footprintCells)
			OnInventoryChanged.Broadcast(idx);
	}
	else if (Slots.IsValidIndex(InventorySlotToBeRemoved.SlotIndex))
	{
		// Fallback: the passed-in slot didn't resolve to a live footprint (e.g. already
		// cleared elsewhere). Still normalize the one slot given, parity with before.
		Slots[InventorySlotToBeRemoved.SlotIndex].SlotIndex = InventorySlotToBeRemoved.SlotIndex;
		Slots[InventorySlotToBeRemoved.SlotIndex].SlotType = InventorySlotToBeRemoved.SlotType;
		Slots[InventorySlotToBeRemoved.SlotIndex].ItemID.Invalidate();
		Slots[InventorySlotToBeRemoved.SlotIndex].bIsAnchor = false;
		OnInventoryChanged.Broadcast(InventorySlotToBeRemoved.SlotIndex);
	}

	RefreshIsInventoryFull();
	return true;
}

void UInventoryComponent::SetSlot(int32 Index, const FInventorySlot& Slot)
{
	// Parity: SetSlot only transfers the ItemID; SlotIndex/SlotType stay.
	//
	// Now footprint-aware. The old body wrote the ItemID into ONE cell and set
	// bIsAnchor = "is the Guid valid", which was fine while everything was 1x1 and
	// corrupts the grid the moment it isn't: writing a 2x2 item here left three cells
	// unclaimed, and clearing this cell left the other three occupied by a ghost that
	// nothing could ever remove. Callers keep the same one-line convenience; the
	// multi-cell bookkeeping is done for them.
	if (!Slots.IsValidIndex(Index))
		return;

	const FGuid newId = Slot.ItemID;
	const FGuid oldId = Slots[Index].ItemID;
	if (newId == oldId)
		return;

	// Clearing a cell clears the WHOLE footprint of whatever lived in it.
	if (!newId.IsValid())
	{
		if (oldId.IsValid())
			ClearFootprintForItem(oldId);
		else
		{
			Slots[Index].ItemID.Invalidate();
			Slots[Index].bIsAnchor = false;
		}
		return;
	}

	FItemInstance instance;
	const FIntPoint size = GetItem(newId, instance) ? GetItemFootprintSize(instance.RowName) : FIntPoint(1, 1);

	// Neither the item being written nor the cell's current occupant counts as an
	// obstacle — both are leaving as part of this write.
	if (!CanPlaceFootprintIgnoring(Index, size, { newId, oldId }))
	{
		UE_LOG(LogInventoryComponent, Warning,
			   TEXT("[UInventoryComponent::SetSlot] A %dx%d item does not fit at slot %d; slot left untouched."),
			   size.X, size.Y, Index);
		return;
	}

	if (oldId.IsValid())
		ClearFootprintForItem(oldId);
	ClearFootprintForItem(newId); // if it is being moved from elsewhere in this container
	OccupyFootprint(Index, size, newId);
	// No broadcast here — parity: callers (SwapSlots) broadcast themselves.
}

// =====================================================================================
//  Grid / footprint helpers
// =====================================================================================

int32 UInventoryComponent::XYToIndex(int32 X, int32 Y) const
{
	// Guarded: called only from grid-enabled paths in practice, but degrade gracefully
	// (rather than divide-by-zero) if it's ever hit on a non-grid container.
	return GridWidth > 0 ? (Y * GridWidth + X) : X;
}

FIntPoint UInventoryComponent::IndexToXY(int32 Index) const
{
	return GridWidth > 0 ? FIntPoint(Index % GridWidth, Index / GridWidth) : FIntPoint(Index, 0);
}

FIntPoint UInventoryComponent::GetItemFootprintSize(FName RowName) const
{
	// SlotSize only means anything on a grid-enabled container; elsewhere every item is
	// a plain 1x1, regardless of what its definition authors.
	if (!IsGridEnabled())
		return FIntPoint(1, 1);

	FItemDefinition def;
	if (RowName == NAME_None || !GetItemStructForRow(RowName, def))
		return FIntPoint(1, 1);

	return (def.SlotSize.X > 0 && def.SlotSize.Y > 0) ? def.SlotSize : FIntPoint(1, 1);
}

bool UInventoryComponent::CanPlaceFootprintAtIgnoringItem(int32 AnchorIndex, FIntPoint Size, FGuid IgnoreItemID) const
{
	if (!Slots.IsValidIndex(AnchorIndex) || Size.X <= 0 || Size.Y <= 0)
		return false;

	// Non-grid container, or a 1x1 footprint: legacy single-cell check.
	if (!IsGridEnabled() || (Size.X <= 1 && Size.Y <= 1))
	{
		const FInventorySlot& slot = Slots[AnchorIndex];
		return slot.IsEmpty() || (IgnoreItemID.IsValid() && slot.ItemID == IgnoreItemID);
	}

	const FIntPoint anchor = IndexToXY(AnchorIndex);
	if (anchor.X + Size.X > GridWidth || anchor.Y + Size.Y > GridHeight)
		return false; // doesn't fit within grid bounds

	for (int32 y = anchor.Y; y < anchor.Y + Size.Y; ++y)
	{
		for (int32 x = anchor.X; x < anchor.X + Size.X; ++x)
		{
			const int32 idx = XYToIndex(x, y);
			if (!Slots.IsValidIndex(idx))
				return false;

			const FInventorySlot& slot = Slots[idx];
			const bool bFree = slot.IsEmpty() || (IgnoreItemID.IsValid() && slot.ItemID == IgnoreItemID);
			if (!bFree)
				return false;
		}
	}
	return true;
}

bool UInventoryComponent::CanPlaceFootprintAt(int32 AnchorIndex, FIntPoint Size) const
{
	return CanPlaceFootprintAtIgnoringItem(AnchorIndex, Size, FGuid());
}

void UInventoryComponent::OccupyFootprint(int32 AnchorIndex, FIntPoint Size, FGuid ItemID)
{
	// Non-grid container, or a 1x1 footprint: legacy single-cell write.
	if (!IsGridEnabled() || (Size.X <= 1 && Size.Y <= 1))
	{
		if (Slots.IsValidIndex(AnchorIndex))
		{
			Slots[AnchorIndex].ItemID = ItemID;
			Slots[AnchorIndex].bIsAnchor = true;
		}
		return;
	}

	const FIntPoint anchor = IndexToXY(AnchorIndex);
	for (int32 y = anchor.Y; y < anchor.Y + Size.Y; ++y)
	{
		for (int32 x = anchor.X; x < anchor.X + Size.X; ++x)
		{
			const int32 idx = XYToIndex(x, y);
			if (!Slots.IsValidIndex(idx))
				continue;
			Slots[idx].ItemID = ItemID;
			Slots[idx].bIsAnchor = (idx == AnchorIndex);
		}
	}
}

void UInventoryComponent::ClearFootprintForItem(FGuid ItemID)
{
	if (!ItemID.IsValid())
		return;

	// Walks the whole array rather than a known rect: a multi-cell item's shadow cells
	// aren't contiguous in the flat Slots array (only in 2D), so this is the simplest
	// correct way to find every cell it occupies.
	for (FInventorySlot& slot : Slots)
	{
		if (slot.ItemID == ItemID)
		{
			slot.ItemID.Invalidate();
			slot.bIsAnchor = false;
		}
	}
}

TArray<int32> UInventoryComponent::GetFootprintCellsForItem(FGuid ItemID) const
{
	TArray<int32> cells;
	if (!ItemID.IsValid())
		return cells;

	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		if (Slots[i].ItemID == ItemID)
			cells.Add(i);
	}
	return cells;
}

bool UInventoryComponent::FindFirstFitForSize(FIntPoint Size, int32& OutAnchorIndex) const
{
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		if (CanPlaceFootprintAt(i, Size))
		{
			OutAnchorIndex = i;
			return true;
		}
	}
	OutAnchorIndex = INDEX_NONE;
	return false;
}

void UInventoryComponent::BroadcastFootprintForItem(FGuid ItemID)
{
	for (int32 idx : GetFootprintCellsForItem(ItemID))
		OnInventoryChanged.Broadcast(idx);
}

// =====================================================================================
//  Multi-item swap planning
// =====================================================================================

int32 UInventoryComponent::FindAnchorIndexForItem(FGuid ItemID) const
{
	if (!ItemID.IsValid())
		return INDEX_NONE;
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		if (Slots[i].ItemID == ItemID && Slots[i].bIsAnchor)
			return i;
	}
	return INDEX_NONE;
}

bool UInventoryComponent::IsFootprintInBounds(int32 AnchorIndex, FIntPoint Size) const
{
	if (!Slots.IsValidIndex(AnchorIndex) || Size.X <= 0 || Size.Y <= 0)
		return false;
	if (!IsGridEnabled())
		return true; // a single valid index is trivially "in bounds" on a non-grid container

	const FIntPoint anchor = IndexToXY(AnchorIndex);
	return anchor.X + Size.X <= GridWidth && anchor.Y + Size.Y <= GridHeight;
}

TArray<FGuid> UInventoryComponent::GetItemsOverlappingFootprint(int32 AnchorIndex, FIntPoint Size, FGuid IgnoreItemID) const
{
	TArray<FGuid> found;
	if (!IsFootprintInBounds(AnchorIndex, Size))
		return found;

	if (!IsGridEnabled() || (Size.X <= 1 && Size.Y <= 1))
	{
		const FInventorySlot& slot = Slots[AnchorIndex];
		if (slot.ItemID.IsValid() && slot.ItemID != IgnoreItemID)
			found.AddUnique(slot.ItemID);
		return found;
	}

	const FIntPoint anchor = IndexToXY(AnchorIndex);
	for (int32 y = anchor.Y; y < anchor.Y + Size.Y; ++y)
	{
		for (int32 x = anchor.X; x < anchor.X + Size.X; ++x)
		{
			const int32 idx = XYToIndex(x, y);
			if (!Slots.IsValidIndex(idx))
				continue;
			const FGuid& id = Slots[idx].ItemID;
			if (id.IsValid() && id != IgnoreItemID)
				found.AddUnique(id);
		}
	}
	return found;
}

bool UInventoryComponent::IsItemFullyWithinFootprint(FGuid ItemID, int32 AnchorIndex, FIntPoint Size) const
{
	if (!IsGridEnabled())
		return true;

	const FIntPoint anchor = IndexToXY(AnchorIndex);
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		if (Slots[i].ItemID != ItemID)
			continue;
		const FIntPoint cell = IndexToXY(i);
		if (cell.X < anchor.X || cell.X >= anchor.X + Size.X
			|| cell.Y < anchor.Y || cell.Y >= anchor.Y + Size.Y)
			return false; // a cell of this item sticks out — partial overlap, reject
	}
	return true;
}

// =====================================================================================
//  Confine-box geometry
//
//  Everything below is pure geometry over the live Slots array. It exists so the swap
//  rules can be written in terms of rectangles ("does this item stick out of the box we
//  are swapping inside?") instead of ad-hoc index arithmetic.
// =====================================================================================

FInventoryGridRect UInventoryComponent::GetGridBoundsRect() const
{
	if (IsGridEnabled())
		return FInventoryGridRect(FIntPoint(0, 0), FIntPoint(GridWidth, GridHeight));

	// Non-grid container: IndexToXY maps index -> (index, 0), so the whole container is
	// one row and every rect operation degrades into a span of flat slot indices.
	return FInventoryGridRect(FIntPoint(0, 0), FIntPoint(Slots.Num(), 1));
}

FInventoryGridRect UInventoryComponent::GetFootprintRect(int32 AnchorIndex, FIntPoint Size) const
{
	if (!Slots.IsValidIndex(AnchorIndex) || Size.X <= 0 || Size.Y <= 0)
		return FInventoryGridRect();

	return FInventoryGridRect::FromAnchorAndSize(IndexToXY(AnchorIndex), Size);
}

FInventoryGridRect UInventoryComponent::GetItemBoundsRect(FGuid ItemID) const
{
	FInventoryGridRect rect;
	if (!ItemID.IsValid())
		return rect;

	// Built from the cells the item ACTUALLY occupies rather than from its authored
	// SlotSize: if the two ever disagree, the confine has to describe what is really on
	// the grid, or the rules end up protecting a rectangle the player can't see.
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		if (Slots[i].ItemID != ItemID)
			continue;

		const FIntPoint cell = IndexToXY(i);
		rect.Encapsulate(FInventoryGridRect(cell, cell + FIntPoint(1, 1)));
	}
	return rect;
}

FIntPoint UInventoryComponent::GetItemSizeById(FGuid ItemID) const
{
	FItemInstance instance;
	if (!GetItem(ItemID, instance))
		return FIntPoint(1, 1);

	return GetItemFootprintSize(instance.RowName);
}

bool UInventoryComponent::IsFootprintSlotTypeCompatible(int32 AnchorIndex, FIntPoint Size, const FItemDefinition& Def) const
{
	if (!Slots.IsValidIndex(AnchorIndex))
		return false;

	if (!IsGridEnabled() || (Size.X <= 1 && Size.Y <= 1))
		return Def.CompatibleSlotTypes.Contains(Slots[AnchorIndex].SlotType);

	// Every cell, not just the anchor: on a container using SlotTypeOverrides, testing
	// only the anchor lets a multi-cell item hang over slots it isn't allowed in.
	const FIntPoint anchor = IndexToXY(AnchorIndex);
	for (int32 y = anchor.Y; y < anchor.Y + Size.Y; ++y)
	{
		for (int32 x = anchor.X; x < anchor.X + Size.X; ++x)
		{
			const int32 idx = XYToIndex(x, y);
			if (!Slots.IsValidIndex(idx))
				return false;
			if (!Def.CompatibleSlotTypes.Contains(Slots[idx].SlotType))
				return false;
		}
	}
	return true;
}

bool UInventoryComponent::CanPlaceFootprintIgnoring(int32 AnchorIndex, FIntPoint Size,
													const TArray<FGuid>& IgnoreItemIDs) const
{
	if (!IsFootprintInBounds(AnchorIndex, Size))
		return false;

	if (!IsGridEnabled() || (Size.X <= 1 && Size.Y <= 1))
	{
		const FInventorySlot& slot = Slots[AnchorIndex];
		return slot.IsEmpty() || IgnoreItemIDs.Contains(slot.ItemID);
	}

	const FIntPoint anchor = IndexToXY(AnchorIndex);
	for (int32 y = anchor.Y; y < anchor.Y + Size.Y; ++y)
	{
		for (int32 x = anchor.X; x < anchor.X + Size.X; ++x)
		{
			const int32 idx = XYToIndex(x, y);
			if (!Slots.IsValidIndex(idx))
				return false;

			const FInventorySlot& slot = Slots[idx];
			if (!slot.IsEmpty() && !IgnoreItemIDs.Contains(slot.ItemID))
				return false;
		}
	}
	return true;
}

bool UInventoryComponent::FindBestAnchorInRect(FIntPoint Size, const FItemDefinition& Def,
											   const FInventoryGridRect& Confine, FIntPoint IdealAnchorXY,
											   TFunctionRef<bool(int32)> IsCellAvailable,
											   int32& OutAnchorIndex) const
{
	OutAnchorIndex = INDEX_NONE;

	if (!Confine.bValid || Size.X <= 0 || Size.Y <= 0)
		return false;
	if (Confine.Width() < Size.X || Confine.Height() < Size.Y)
		return false; // the item is larger than the box it is allowed to move inside

	int32 bestScore = MAX_int32;

	for (int32 y = Confine.Min.Y; y + Size.Y <= Confine.Max.Y; ++y)
	{
		for (int32 x = Confine.Min.X; x + Size.X <= Confine.Max.X; ++x)
		{
			const int32 anchorIdx = XYToIndex(x, y);
			if (!Slots.IsValidIndex(anchorIdx) || !IsFootprintInBounds(anchorIdx, Size))
				continue;

			bool bFits = true;
			for (int32 cy = y; cy < y + Size.Y && bFits; ++cy)
			{
				for (int32 cx = x; cx < x + Size.X && bFits; ++cx)
				{
					const int32 cellIdx = XYToIndex(cx, cy);
					bFits = Slots.IsValidIndex(cellIdx)
						&& IsCellAvailable(cellIdx)
						&& Def.CompatibleSlotTypes.Contains(Slots[cellIdx].SlotType);
				}
			}
			if (!bFits)
				continue;

			// "Most intuitive" = least movement away from where the item would have gone
			// if it had simply slid into the space the dragged item vacated.
			const int32 score = FMath::Abs(x - IdealAnchorXY.X) + FMath::Abs(y - IdealAnchorXY.Y);
			if (score < bestScore)
			{
				bestScore = score;
				OutAnchorIndex = anchorIdx;
				if (score == 0)
					return true; // exactly where it wanted to go; nothing can beat that
			}
		}
	}

	return OutAnchorIndex != INDEX_NONE;
}

// =====================================================================================
//  Swap planning
//
//  See the header for the rule set this implements. The short version: the dragged item
//  lands exactly where it was dropped, everything it displaces is re-placed inside the
//  confine box (and as close as possible to where it would naturally slide), and if that
//  can't be done the whole swap is rejected rather than flinging an item somewhere else
//  in the container.
// =====================================================================================

bool UInventoryComponent::BuildSwapPlan(UInventoryComponent* SourceInv, int32 SourceIndex, int32 DestIndex,
                                        FInventorySwapPlan& OutPlan) const
{
	OutPlan = FInventorySwapPlan();

	const UInventoryComponent* src = SourceInv ? SourceInv : this;
	const bool bSameContainer = (src == this);

	if (!src->Slots.IsValidIndex(SourceIndex) || !Slots.IsValidIndex(DestIndex))
		return false;

	const FInventorySlot srcSlot = src->GetSlot(SourceIndex);

	FItemInstance srcItem;
	if (!src->GetItem(srcSlot.ItemID, srcItem))
		return false;

	FItemDefinition srcDef;
	if (!GetItemStructForRow(srcItem.RowName, srcDef))
		return false;

	OutPlan.DraggedItemID = srcItem.UUID;

	// The item's real anchor, which is NOT necessarily the cell the drag started on.
	const int32 resolvedSrcAnchor = src->FindAnchorIndexForItem(srcItem.UUID);
	const int32 srcAnchorIndex = (resolvedSrcAnchor != INDEX_NONE) ? resolvedSrcAnchor : SourceIndex;

	const FIntPoint dragSize = GetItemFootprintSize(srcItem.RowName);

	// -------------------------------------------------------------------------------
	// HOVERED CELL -> ANCHOR.
	//
	// A multi-cell item's visual is sized to its whole footprint but lives inside its
	// ANCHOR slot, so it overflows across the cells to its right and below. Those slots
	// never receive OnDragEnter — the overflowing subtree is on top of them — which means
	// the hovered cell for a one-step nudge in +X or +Y arrives one cell further out than
	// the player meant, and the cells in between are simply unreachable. Overflow only
	// ever goes +X/+Y from the anchor, which is exactly why left and up have always
	// worked and right, down, and down-right have not.
	//
	// So for a move inside one container, the hover is read as "slide the item as little
	// as possible so its footprint COVERS this cell" rather than "put the top-left here".
	// Clamping the current anchor into [hovered - size + 1, hovered] does that per axis:
	// the item steps one cell per hover in every direction, and the unreachable cells are
	// never needed because the first reachable cell past them now means one step.
	//
	// Cross-container drops keep top-left semantics: there is no current anchor in this
	// grid to slide from, so the grab offset is the right correction there.
	// -------------------------------------------------------------------------------
	FIntPoint dropAnchorXY = IndexToXY(DestIndex);
	if (IsGridEnabled() && src->IsGridEnabled())
	{
		const FIntPoint hovered     = IndexToXY(DestIndex);
		const FIntPoint srcAnchorXY = src->IndexToXY(srcAnchorIndex);

		if (bSameContainer)
		{
			dropAnchorXY.X = FMath::Clamp(srcAnchorXY.X, hovered.X - dragSize.X + 1, hovered.X);
			dropAnchorXY.Y = FMath::Clamp(srcAnchorXY.Y, hovered.Y - dragSize.Y + 1, hovered.Y);
		}
		else
		{
			// DestIndex is the cell under the CURSOR; the plan needs the item's top-left
			// ANCHOR. Those are the same cell only when the item was picked up by its own
			// top-left, so subtract however far the grabbed cell sat from the anchor.
			dropAnchorXY -= (src->IndexToXY(SourceIndex) - srcAnchorXY);
		}

		// Either path can push a footprint off the edge near a border. Clamping back
		// in-bounds is what makes edge drops feel like they "stick" instead of silently
		// refusing.
		dropAnchorXY.X = FMath::Clamp(dropAnchorXY.X, 0, FMath::Max(0, GridWidth  - dragSize.X));
		dropAnchorXY.Y = FMath::Clamp(dropAnchorXY.Y, 0, FMath::Max(0, GridHeight - dragSize.Y));
	}

	const int32 dropAnchorIndex = (IsGridEnabled() && src->IsGridEnabled())
		? XYToIndex(dropAnchorXY.X, dropAnchorXY.Y)
		: DestIndex;

	if (!Slots.IsValidIndex(dropAnchorIndex))
		return false;

	// A drop that moves nothing is not a swap. Tested on the RESOLVED anchor rather than
	// on the raw indices: under the cover rule several hovered cells map to the item's
	// current anchor (any cell its footprint already contains), and comparing
	// SourceIndex to DestIndex would miss those while also rejecting legitimate moves.
	if (bSameContainer && dropAnchorIndex == srcAnchorIndex)
		return false;

	// Attempt 1: the item lands exactly where it was dropped. Always preferred.
	if (TryPlanDropAt(src, srcAnchorIndex, srcItem, srcDef, dragSize, dropAnchorIndex, OutPlan))
		return true;

	// Attempt 2: let the pair trade places instead.
	return TryPlanTrade(src, srcAnchorIndex, srcItem, srcDef, dragSize, dropAnchorIndex, OutPlan);
}
bool UInventoryComponent::TryPlanDropAt(const UInventoryComponent* Src, int32 SrcAnchorIndex,
                                        const FItemInstance& DraggedItem, const FItemDefinition& DraggedDef,
                                        FIntPoint DragSize, int32 DropAnchorIndex,
                                        FInventorySwapPlan& OutPlan) const
{
	const UInventoryComponent* src = Src ? Src : this;
	const bool bSameContainer = (src == this);

	OutPlan.Displaced.Reset();
	OutPlan.bValid = false;
	OutPlan.DraggedItemID = DraggedItem.UUID;
	OutPlan.DraggedNewAnchor = DropAnchorIndex;
	OutPlan.DraggedSize = DragSize;

	if (!IsFootprintInBounds(DropAnchorIndex, DragSize))
		return false;
	if (!IsFootprintSlotTypeCompatible(DropAnchorIndex, DragSize, DraggedDef))
		return false;

	const FInventoryGridRect destRect = GetFootprintRect(DropAnchorIndex, DragSize);
	OutPlan.Confine = destRect;

	const TArray<FGuid> overlapping = GetItemsOverlappingFootprint(DropAnchorIndex, DragSize, DraggedItem.UUID);
	if (overlapping.Num() == 0)
	{
		OutPlan.bValid = true;
		return true;
	}

	// RULE 3 — confine: dragged item's old footprint, its new footprint, and the partner's.
	FGuid partnerId = GetSlot(DropAnchorIndex).ItemID;
	if (partnerId == DraggedItem.UUID)
		partnerId.Invalidate();

	FInventoryGridRect confine = destRect;
	if (bSameContainer)
		confine.Encapsulate(GetItemBoundsRect(DraggedItem.UUID));
	if (partnerId.IsValid())
		confine.Encapsulate(GetItemBoundsRect(partnerId));

	OutPlan.Confine = confine;

	// RULE 4 — bystanders must sit entirely inside the confine.
	for (const FGuid& id : overlapping)
	{
		if (!confine.ContainsRect(GetItemBoundsRect(id)))
			return false;
	}

	TSet<int32> destFootprintCells;
	{
		const FIntPoint a = IndexToXY(DropAnchorIndex);
		destFootprintCells.Reserve(DragSize.X * DragSize.Y);
		for (int32 y = a.Y; y < a.Y + DragSize.Y; ++y)
			for (int32 x = a.X; x < a.X + DragSize.X; ++x)
				destFootprintCells.Add(XYToIndex(x, y));
	}

	TSet<int32> vacatedInSrc;
	for (int32 idx : src->GetFootprintCellsForItem(DraggedItem.UUID))
		vacatedInSrc.Add(idx);

	TSet<int32> vacatedInDest;
	for (const FGuid& id : overlapping)
		for (int32 idx : GetFootprintCellsForItem(id))
			vacatedInDest.Add(idx);

	TSet<int32> claimed;
	auto IsCellAvailableInSrc = [&](int32 idx) -> bool
	{
		if (claimed.Contains(idx))
			return false;
		if (bSameContainer && destFootprintCells.Contains(idx))
			return false;
		if (vacatedInSrc.Contains(idx))
			return true;
		if (bSameContainer && vacatedInDest.Contains(idx))
			return true;
		return src->Slots.IsValidIndex(idx) && src->Slots[idx].IsEmpty();
	};

	const FIntPoint destAnchorXY = IndexToXY(DropAnchorIndex);
	const FIntPoint srcAnchorXY = src->IndexToXY(SrcAnchorIndex);

	auto IdealAnchorXYFor = [&](const FGuid& Id) -> FIntPoint
	{
		const int32 itemAnchor = FindAnchorIndexForItem(Id);
		const FIntPoint itemAnchorXY = IndexToXY(itemAnchor != INDEX_NONE ? itemAnchor : DropAnchorIndex);
		return srcAnchorXY + (itemAnchorXY - destAnchorXY);
	};

	auto SizeInSrcFor = [&](const FGuid& Id) -> FIntPoint
	{
		FItemInstance instance;
		if (!GetItem(Id, instance))
			return FIntPoint(1, 1);
		return src->GetItemFootprintSize(instance.RowName);
	};

	TArray<FGuid> ordered = overlapping;
	ordered.Sort([this](const FGuid& A, const FGuid& B)
	{
		const FIntPoint sa = GetItemSizeById(A);
		const FIntPoint sb = GetItemSizeById(B);
		return (sa.X * sa.Y) > (sb.X * sb.Y);
	});

	FInventoryGridRect placementConfine;
	if (bSameContainer)
	{
		placementConfine = confine;
	}
	else
	{
		placementConfine = src->GetItemBoundsRect(DraggedItem.UUID);
		for (const FGuid& id : ordered)
		{
			placementConfine.Encapsulate(
				FInventoryGridRect::FromAnchorAndSize(IdealAnchorXYFor(id), SizeInSrcFor(id)));
		}
		placementConfine.ClampTo(src->GetGridBoundsRect());
	}

	OutPlan.Displaced.Reserve(ordered.Num());
	for (const FGuid& id : ordered)
	{
		FItemInstance displacedItem;
		if (!GetItem(id, displacedItem))
			return false;

		FItemDefinition displacedDef;
		if (!GetItemStructForRow(displacedItem.RowName, displacedDef))
			return false;

		const FIntPoint sizeInSrc = SizeInSrcFor(id);

		int32 chosenAnchor = INDEX_NONE;
		if (!src->FindBestAnchorInRect(sizeInSrc, displacedDef, placementConfine,
		                               IdealAnchorXYFor(id), IsCellAvailableInSrc, chosenAnchor))
		{
			OutPlan.Displaced.Reset();
			return false;
		}

		const FIntPoint chosenXY = src->IndexToXY(chosenAnchor);
		for (int32 y = chosenXY.Y; y < chosenXY.Y + sizeInSrc.Y; ++y)
			for (int32 x = chosenXY.X; x < chosenXY.X + sizeInSrc.X; ++x)
				claimed.Add(src->XYToIndex(x, y));

		OutPlan.Displaced.Add({ id, chosenAnchor, sizeInSrc });
	}

	OutPlan.bValid = true;
	return true;
}

bool UInventoryComponent::TryPlanTrade(const UInventoryComponent* Src, int32 SrcAnchorIndex,
                                       const FItemInstance& DraggedItem, const FItemDefinition& DraggedDef,
                                       FIntPoint DragSize, int32 DropAnchorIndex,
                                       FInventorySwapPlan& OutPlan) const
{
	const UInventoryComponent* src = Src ? Src : this;

	OutPlan.Displaced.Reset();
	OutPlan.bValid = false;

	// Cross-container drops never need this: the two items live in different arrays, so a
	// partner can always take the vacated space without fighting the dragged item for it.
	if (src != this || !IsGridEnabled())
		return false;

	if (!IsFootprintInBounds(DropAnchorIndex, DragSize))
		return false;

	// Exactly one item under the drop — a trade between two items is the only thing this
	// fallback knows how to reason about. More than one and we stay refused.
	const TArray<FGuid> overlapping = GetItemsOverlappingFootprint(DropAnchorIndex, DragSize, DraggedItem.UUID);
	if (overlapping.Num() != 1)
		return false;

	const FGuid partnerId = overlapping[0];

	FItemInstance partnerItem;
	if (!GetItem(partnerId, partnerItem))
		return false;

	FItemDefinition partnerDef;
	if (!GetItemStructForRow(partnerItem.RowName, partnerDef))
		return false;

	const FIntPoint partnerSize = GetItemFootprintSize(partnerItem.RowName);
	const int32 partnerAnchor = FindAnchorIndexForItem(partnerId);
	if (partnerAnchor == INDEX_NONE)
		return false;

	const FInventoryGridRect draggedBounds = GetItemBoundsRect(DraggedItem.UUID);
	const FInventoryGridRect partnerBounds = GetItemBoundsRect(partnerId);

	FInventoryGridRect confine = GetFootprintRect(DropAnchorIndex, DragSize);
	confine.Encapsulate(draggedBounds);
	confine.Encapsulate(partnerBounds);
	OutPlan.Confine = confine;

	if (!confine.ContainsRect(partnerBounds))
		return false;

	// Only the two items are moving, so only their cells (plus genuinely empty ones) are
	// in play. Nothing outside the confine is ever considered.
	TSet<int32> freed;
	for (int32 idx : GetFootprintCellsForItem(DraggedItem.UUID))
		freed.Add(idx);
	for (int32 idx : GetFootprintCellsForItem(partnerId))
		freed.Add(idx);

	TSet<int32> draggedClaim;
	auto IsCellFree = [&](int32 idx) -> bool
	{
		if (freed.Contains(idx))
			return true;
		return Slots.IsValidIndex(idx) && Slots[idx].IsEmpty();
	};
	auto IsCellFreeForPartner = [&](int32 idx) -> bool
	{
		return !draggedClaim.Contains(idx) && IsCellFree(idx);
	};

	const FIntPoint dropXY = IndexToXY(DropAnchorIndex);
	const FIntPoint srcAnchorXY = IndexToXY(SrcAnchorIndex);

	// Candidate landing spots for the DRAGGED item, nearest to where it was actually
	// dropped first — the player's intent still wins, it just bends when it has to.
	struct FCandidate { int32 Anchor; int32 Score; };
	TArray<FCandidate> candidates;

	for (int32 y = confine.Min.Y; y + DragSize.Y <= confine.Max.Y; ++y)
	{
		for (int32 x = confine.Min.X; x + DragSize.X <= confine.Max.X; ++x)
		{
			const int32 anchorIdx = XYToIndex(x, y);
			if (!Slots.IsValidIndex(anchorIdx) || !IsFootprintInBounds(anchorIdx, DragSize))
				continue;
			if (!IsFootprintSlotTypeCompatible(anchorIdx, DragSize, DraggedDef))
				continue;

			// It has to still read as "I swapped with THAT item": the dragged item's new
			// footprint must land on ground the partner currently occupies.
			const FInventoryGridRect candidateRect = FInventoryGridRect::FromAnchorAndSize(FIntPoint(x, y), DragSize);
			bool bTouchesPartner = false;
			for (int32 cy = y; cy < y + DragSize.Y && !bTouchesPartner; ++cy)
				for (int32 cx = x; cx < x + DragSize.X && !bTouchesPartner; ++cx)
					bTouchesPartner = (Slots[XYToIndex(cx, cy)].ItemID == partnerId);
			if (!bTouchesPartner)
				continue;

			candidates.Add({ anchorIdx, FMath::Abs(x - dropXY.X) + FMath::Abs(y - dropXY.Y) });
		}
	}

	candidates.Sort([](const FCandidate& A, const FCandidate& B) { return A.Score < B.Score; });

	for (const FCandidate& candidate : candidates)
	{
		// Reserve the dragged item's cells, then see whether the partner fits in what is
		// left of the confine — preferring the spot the dragged item is vacating.
		draggedClaim.Reset();
		const FIntPoint candXY = IndexToXY(candidate.Anchor);
		bool bDraggedFits = true;
		for (int32 y = candXY.Y; y < candXY.Y + DragSize.Y && bDraggedFits; ++y)
		{
			for (int32 x = candXY.X; x < candXY.X + DragSize.X && bDraggedFits; ++x)
			{
				const int32 idx = XYToIndex(x, y);
				bDraggedFits = IsCellFree(idx);
				if (bDraggedFits)
					draggedClaim.Add(idx);
			}
		}
		if (!bDraggedFits)
			continue;

		int32 partnerNewAnchor = INDEX_NONE;
		if (!FindBestAnchorInRect(partnerSize, partnerDef, confine, srcAnchorXY,
		                          IsCellFreeForPartner, partnerNewAnchor))
			continue;

		// Refuse an arrangement where nothing actually moved.
		if (candidate.Anchor == SrcAnchorIndex && partnerNewAnchor == partnerAnchor)
			continue;

		OutPlan.DraggedItemID = DraggedItem.UUID;
		OutPlan.DraggedNewAnchor = candidate.Anchor;
		OutPlan.DraggedSize = DragSize;
		OutPlan.Displaced.Add({ partnerId, partnerNewAnchor, partnerSize });
		OutPlan.bValid = true;
		return true;
	}

	return false;
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
	// Build empty slots
	for (int32 i = 0; i < slotCount; ++i)
	{
		FInventorySlot newSlot;
		newSlot.SlotIndex = i;
		
		
		// Slot overrides
		const FSlotOverrideData* foundOverride = SlotOverrides.FindByPredicate([i](const FSlotOverrideData& Item)
		{
			return Item.Index == i;
		});
		
		newSlot.SlotType = foundOverride ? foundOverride->Type : SlotType; 
		
		newSlot.ItemID.Invalidate();
		newSlot.bIsAnchor = false; // empty slot; nothing anchored here yet
		Slots.Add(newSlot);
	}
	RefreshIsInventoryFull();
}

void UInventoryComponent::FillDefaultInventory()
{
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

	// Auto sort (class defaults). No-op unless AutoSort is enabled on this component.
	TryAutoSort();
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

	// Auto sort (class defaults). No-op unless AutoSort is enabled on this component.
	TryAutoSort();
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
	// Coarse/cell-level check: "is there any single empty cell at all." For a grid
	// container this can read false (not full) even when no remaining region is big
	// enough for a particular multi-cell item — that's a per-item question with no
	// single right generic answer, so IsInventoryFull stays the same "any room at all"
	// signal it always was.
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

	// Auto sort (class defaults). No-op unless AutoSort is enabled on this component.
	TryAutoSort();

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

void UInventoryComponent::GetSlotOverrideByIndex(
	int32 SlotIndex, 
	FSlotOverrideData& OverrideData, 
	UPARAM(DisplayName = "Found") bool& bFound)
{
	
	for (const FSlotOverrideData& slotOverride : SlotOverrides)
	{
		if (slotOverride.Index == SlotIndex)
		{
			OverrideData = slotOverride;
			bFound = true;
			return;
		}
	}
	
	bFound = false;
	return;

}

// =====================================================================================
//  Sorting — reading values out of fragments
//
//  Fragments are FInstancedStructs: the inventory has no compile-time knowledge of what
//  any of them contain, which is the whole point of the fragment model and also the
//  reason sorting has to go through reflection. Everything below is the property system
//  reading a named int/float out of an arbitrary struct — no fragment type is ever named
//  in this file, so a fragment added next year is sortable the day it compiles.
// =====================================================================================

namespace
{
	/** How deep into nested structs the value dropdown looks ("AmmoCount.AmmoInMag"). */
	constexpr int32 GFragmentValueSearchDepth = 2;

	/** Is this property something we can turn into a single number to sort on? */
	bool IsSortableProperty(const FProperty* Prop)
	{
		// Enums come through as byte/int properties and are genuinely useful keys
		// (rarity, tier, ammo type), so they're deliberately NOT filtered out — their
		// declaration order becomes their sort order, which is what an author expects.
		return Prop && (Prop->IsA<FNumericProperty>() || Prop->IsA<FBoolProperty>());
	}

	/** Collect "Name" / "Outer.Inner" paths for every sortable property on a struct. */
	void CollectSortableValuePaths(const UScriptStruct* Struct, const FString& Prefix, int32 Depth,
	                               TArray<FString>& OutPaths)
	{
		if (!Struct)
			return;

		for (TFieldIterator<FProperty> it(Struct); it; ++it)
		{
			const FProperty* prop = *it;
			const FString path = Prefix.IsEmpty() ? prop->GetName() : Prefix + TEXT(".") + prop->GetName();

			if (IsSortableProperty(prop))
			{
				OutPaths.AddUnique(path);
				continue;
			}

			if (const FStructProperty* structProp = CastField<FStructProperty>(prop))
			{
				if (Depth > 0)
					CollectSortableValuePaths(structProp->Struct, path, Depth - 1, OutPaths);
			}
		}
	}

	/** Walk a (possibly dotted) property path and read the value at the end of it. */
	bool ResolveValuePath(const UScriptStruct* Struct, const void* Memory, const FString& Path, double& OutValue)
	{
		if (!Struct || !Memory || Path.IsEmpty())
			return false;

		FString head = Path;
		FString tail;
		Path.Split(TEXT("."), &head, &tail); // leaves head == Path when there's no dot

		const FProperty* prop = Struct->FindPropertyByName(FName(*head));
		if (!prop)
		{
			// The name came out of a dropdown or was typed by hand; don't fail on case.
			for (TFieldIterator<FProperty> it(Struct); it; ++it)
			{
				if (it->GetName().Equals(head, ESearchCase::IgnoreCase))
				{
					prop = *it;
					break;
				}
			}
		}
		if (!prop)
			return false;

		const void* valuePtr = prop->ContainerPtrToValuePtr<void>(Memory);
		if (!valuePtr)
			return false;

		if (!tail.IsEmpty())
		{
			const FStructProperty* structProp = CastField<FStructProperty>(prop);
			return structProp && ResolveValuePath(structProp->Struct, valuePtr, tail, OutValue);
		}

		if (const FNumericProperty* numeric = CastField<FNumericProperty>(prop))
		{
			OutValue = numeric->IsFloatingPoint()
				? numeric->GetFloatingPointPropertyValue(valuePtr)
				: static_cast<double>(numeric->GetSignedIntPropertyValue(valuePtr));
			return true;
		}

		if (const FBoolProperty* boolProp = CastField<FBoolProperty>(prop))
		{
			OutValue = boolProp->GetPropertyValue(valuePtr) ? 1.0 : 0.0;
			return true;
		}

		return false;
	}

	/**
	 * Item size as one comparable number: area dominates, width breaks area ties, height
	 * breaks those. A 2x3 and a 3x2 have the same area and are only separated by the
	 * width term — which is what makes a size sort produce columns of like-shaped items
	 * rather than a jumble of everything that happens to cover six cells.
	 */
	double EncodeSizeKey(FIntPoint Size)
	{
		return static_cast<double>(Size.X * Size.Y) * 1000000.0
			+ static_cast<double>(Size.X) * 1000.0
			+ static_cast<double>(Size.Y);
	}

	/** Component-wise Guid ordering — an arbitrary but identical answer on every machine. */
	bool CompareGuids(const FGuid& A, const FGuid& B)
	{
		if (A.A != B.A) return A.A < B.A;
		if (A.B != B.B) return A.B < B.B;
		if (A.C != B.C) return A.C < B.C;
		return A.D < B.D;
	}
}

bool UInventoryComponent::GetFragmentValue(const FItemInstance& Instance, const UScriptStruct* FragmentType,
                                           FName ValueName, double& OutValue)
{
	if (ValueName == NAME_None)
		return false;

	const FString path = ValueName.ToString();

	for (const FInstancedStruct& fragment : Instance.Fragments)
	{
		const UScriptStruct* fragmentStruct = fragment.GetScriptStruct();
		if (!fragmentStruct)
			continue;

		// A null FragmentType means "whichever fragment happens to carry this value" —
		// used by the sub-inventory view when only a value name was given.
		if (FragmentType && fragmentStruct != FragmentType && !fragmentStruct->IsChildOf(FragmentType))
			continue;

		if (ResolveValuePath(fragmentStruct, fragment.GetMemory(), path, OutValue))
			return true;
	}

	return false;
}

bool UInventoryComponent::GetItemFragmentValue(FGuid ItemID, UScriptStruct* FragmentType, FName ValueName,
                                               float& OutValue) const
{
	OutValue = 0.f;

	FItemInstance instance;
	if (!GetItem(ItemID, instance))
		return false;

	double value = 0.0;
	if (!GetFragmentValue(instance, FragmentType, ValueName, value))
		return false;

	OutValue = static_cast<float>(value);
	return true;
}

TArray<UScriptStruct*> UInventoryComponent::GetSortableFragmentTypes()
{
	TArray<UScriptStruct*> result;

	UScriptStruct* baseStruct = FItemFragment::StaticStruct();
	if (!baseStruct)
		return result;

	for (TObjectIterator<UScriptStruct> it; it; ++it)
	{
		UScriptStruct* candidate = *it;
		if (candidate && candidate != baseStruct && candidate->IsChildOf(baseStruct))
			result.Add(candidate);
	}

	result.Sort([](const UScriptStruct& A, const UScriptStruct& B)
	{
		return A.GetName() < B.GetName();
	});
	return result;
}

TArray<FString> UInventoryComponent::GetFragmentValueOptionsForFragment(UScriptStruct* FragmentType)
{
	TArray<FString> options;
	CollectSortableValuePaths(FragmentType, FString(), GFragmentValueSearchDepth, options);
	options.Sort();
	return options;
}

TArray<FString> UInventoryComponent::GetFragmentValueOptions()
{
	TArray<FString> options;
	for (UScriptStruct* fragmentType : GetSortableFragmentTypes())
		CollectSortableValuePaths(fragmentType, FString(), GFragmentValueSearchDepth, options);

	options.Sort();
	return options;
}

// =====================================================================================
//  Sorting — building and applying an order
// =====================================================================================

bool UInventoryComponent::ResolveFragmentTypeRank(const FItemInstance& Instance, int32& OutRank, double& OutValue,
                                                  bool& bOutHasValue) const
{
	OutRank = INDEX_NONE;
	OutValue = 0.0;
	bOutHasValue = false;

	// The map's value is now a ValueName (FragmentType : ValueName, the same pairing
	// SortByFragmentValue takes as two pins) rather than an authored rank int, so
	// category order comes from the map's OWN iteration order — which follows insertion
	// order as long as nothing is removed, the only way this map is ever edited from the
	// editor. Walking in that order and taking the FIRST fragment the item carries is
	// exactly "the earliest-added category wins", i.e. the best (lowest) rank.
	int32 index = 0;
	for (const TPair<TObjectPtr<UScriptStruct>, FName>& pair : FragmentTypeSortOrder)
	{
		if (pair.Key && UItemFragmentLibrary::HasItemFragment(Instance, pair.Key))
		{
			OutRank = index;
			// The intra-category tiebreak: this category's own ValueName, read off the
			// very fragment that earned the item its rank — weapons by Damage,
			// consumables by HealAmount, exactly as a FragmentValue rule would read it.
			bOutHasValue = GetFragmentValue(Instance, pair.Key, pair.Value, OutValue);
			return true;
		}
		++index;
	}

	return false;
}

void UInventoryComponent::BuildSortItem(const FItemInstance& Instance, const TArray<FInventorySortRule>& Rules,
                                        FInventorySortItem& OutItem) const
{
	OutItem = FInventorySortItem();
	OutItem.ItemID = Instance.UUID;
	OutItem.RowName = Instance.RowName;
	OutItem.Quantity = Instance.Quantity;
	OutItem.Size = GetItemFootprintSize(Instance.RowName);

	// The definition lookup happens once per ITEM and only if some rule actually wants a
	// name — the whole point of this struct is that the comparator never touches a table.
	const bool bNeedsDisplayName = Rules.ContainsByPredicate([](const FInventorySortRule& Rule)
	{
		return Rule.Key == EInventorySortKey::DisplayName;
	});

	if (bNeedsDisplayName)
	{
		FItemDefinition definition;
		if (GetItemStructForRow(Instance.RowName, definition))
			OutItem.DisplayName = definition.DisplayName;
	}

	OutItem.RuleValues.Reserve(Rules.Num());
	OutItem.bRuleHasValue.Reserve(Rules.Num());
	OutItem.RuleNameValues.Reserve(Rules.Num());
	OutItem.RuleSecondaryValues.Reserve(Rules.Num());
	OutItem.bRuleHasSecondaryValue.Reserve(Rules.Num());

	for (const FInventorySortRule& rule : Rules)
	{
		double value = 0.0;
		bool bHasValue = false;
		FName nameValue = NAME_None;
		double secondaryValue = 0.0;
		bool bHasSecondaryValue = false;

		switch (rule.Key)
		{
		case EInventorySortKey::ItemSize:
			value = EncodeSizeKey(OutItem.Size);
			bHasValue = true;
			break;

		case EInventorySortKey::FragmentValue:
			bHasValue = GetFragmentValue(Instance, rule.FragmentType, rule.ValueName, value);
			break;

		case EInventorySortKey::FragmentType:
		{
			int32 rank = INDEX_NONE;
			bHasValue = ResolveFragmentTypeRank(Instance, rank, secondaryValue, bHasSecondaryValue);
			value = static_cast<double>(rank);
			break;
		}

		case EInventorySortKey::DisplayName:
			// An unnamed definition is treated as a missing value rather than as the empty
			// string, so bMissingValuesLast decides where it lands instead of it silently
			// winning every ascending comparison.
			nameValue = OutItem.DisplayName;
			bHasValue = !nameValue.IsNone();
			break;

		case EInventorySortKey::Quantity:
			value = static_cast<double>(OutItem.Quantity);
			bHasValue = true;
			break;
		}

		OutItem.RuleValues.Add(value);
		OutItem.bRuleHasValue.Add(bHasValue);
		OutItem.RuleNameValues.Add(nameValue);
		OutItem.RuleSecondaryValues.Add(secondaryValue);
		OutItem.bRuleHasSecondaryValue.Add(bHasSecondaryValue);
	}
}

void UInventoryComponent::CollectSortableItems(const TArray<FInventorySortRule>& Rules,
                                               TArray<FInventorySortItem>& OutItems) const
{
	OutItems.Reset();

	// Walk SLOTS, not the instance store: the store also holds attachment instances,
	// which occupy no cell and must not be handed a position in the layout.
	TSet<FGuid> seen;
	for (const FInventorySlot& slot : Slots)
	{
		if (!slot.ItemID.IsValid() || seen.Contains(slot.ItemID))
			continue;

		FItemInstance instance;
		if (!GetItem(slot.ItemID, instance))
			continue;

		seen.Add(slot.ItemID);

		FInventorySortItem item;
		BuildSortItem(instance, Rules, item);
		OutItems.Add(MoveTemp(item));
	}
}

bool UInventoryComponent::CompareSortItems(const FInventorySortItem& A, const FInventorySortItem& B,
                                           const TArray<FInventorySortRule>& Rules)
{
	for (int32 ruleIndex = 0; ruleIndex < Rules.Num(); ++ruleIndex)
	{
		if (!A.RuleValues.IsValidIndex(ruleIndex) || !B.RuleValues.IsValidIndex(ruleIndex))
			break;

		const FInventorySortRule& rule = Rules[ruleIndex];
		const bool bAHas = A.bRuleHasValue[ruleIndex];
		const bool bBHas = B.bRuleHasValue[ruleIndex];

		// "Uncategorised goes last" is a property of the CATEGORY, not of the ordering,
		// so a descending fragment-type sort still leaves unranked items at the end
		// rather than promoting them to the front.
		const bool bMissingLast = (rule.Key == EInventorySortKey::FragmentType) ? true : rule.bMissingValuesLast;

		if (bAHas != bBHas)
			return bMissingLast ? bAHas : bBHas;

		if (!bAHas)
			continue; // neither has it — this layer says nothing, fall through

		// Names are compared as names. They carry no numeric key, so this layer has to
		// resolve here rather than falling into the double comparison below (where every
		// name-sorted item would look equal and the layer would do nothing at all).
		if (rule.Key == EInventorySortKey::DisplayName)
		{
			const FName aName = A.RuleNameValues.IsValidIndex(ruleIndex) ? A.RuleNameValues[ruleIndex] : NAME_None;
			const FName bName = B.RuleNameValues.IsValidIndex(ruleIndex) ? B.RuleNameValues[ruleIndex] : NAME_None;

			if (aName != bName)
			{
				return (rule.Direction == EInventorySortDirection::Descending)
					? bName.LexicalLess(aName)
					: aName.LexicalLess(bName);
			}
			continue;
		}

		const double a = A.RuleValues[ruleIndex];
		const double b = B.RuleValues[ruleIndex];
		if (!FMath::IsNearlyEqual(a, b))
			return (rule.Direction == EInventorySortDirection::Descending) ? a > b : a < b;

		// Same category (FragmentType rule only): break the tie with that category's own
		// value — weapons by Damage, consumables by HealAmount — before falling through
		// to the next rule. This is what makes a single FragmentType rule a two-level
		// sort (group, then value) without needing a second rule entry for it.
		if (rule.Key == EInventorySortKey::FragmentType)
		{
			const bool bAHasSecondary = A.bRuleHasSecondaryValue.IsValidIndex(ruleIndex) && A.bRuleHasSecondaryValue[ruleIndex];
			const bool bBHasSecondary = B.bRuleHasSecondaryValue.IsValidIndex(ruleIndex) && B.bRuleHasSecondaryValue[ruleIndex];

			if (bAHasSecondary != bBHasSecondary)
				return bAHasSecondary; // has that category's value > doesn't, either direction

			if (bAHasSecondary)
			{
				const double sa = A.RuleSecondaryValues[ruleIndex];
				const double sb = B.RuleSecondaryValues[ruleIndex];
				if (!FMath::IsNearlyEqual(sa, sb))
					return (rule.Direction == EInventorySortDirection::Descending) ? sa > sb : sa < sb;
			}
		}
	}

	// Fixed tiebreak. Without one, items the rules consider equal would keep whatever
	// order the slot walk happened to produce — which differs between a client that
	// picked items up in one order and a server that replicated them in another, and
	// makes a sort look non-deterministic to exactly one player.
	if (A.RowName != B.RowName)
		return A.RowName.LexicalLess(B.RowName);
	if (A.Quantity != B.Quantity)
		return A.Quantity > B.Quantity;
	return CompareGuids(A.ItemID, B.ItemID);
}

TArray<FInventorySortRule> UInventoryComponent::BuildLayeredRules(const FInventorySortRule& NewRule,
                                                                  bool bLayerOnExistingSort) const
{
	TArray<FInventorySortRule> rules;

	if (bLayerOnExistingSort)
	{
		rules = ActiveSortRules;

		// Re-applying a key that's already a layer updates that layer in place. Clicking
		// "sort by damage" twice should flip nothing but the direction you asked for,
		// not append a second damage pass that can never do anything.
		const int32 existing = rules.IndexOfByPredicate([&NewRule](const FInventorySortRule& Rule)
		{
			return Rule.Key == NewRule.Key
				&& Rule.FragmentType == NewRule.FragmentType
				&& Rule.ValueName == NewRule.ValueName;
		});

		if (existing != INDEX_NONE)
		{
			rules[existing] = NewRule;
			return rules;
		}
	}

	rules.Add(NewRule);
	return rules;
}

bool UInventoryComponent::RebuildLayoutFromOrder(const TArray<FGuid>& OrderedItemIDs)
{
	// All-or-nothing. Slot overrides can make a container that an arbitrary order won't
	// re-pack into (a typed slot that only one item may occupy), and a sort that gave up
	// halfway would leave instances in the store with no cell referencing them — items
	// that exist, replicate, and are invisible and unreachable forever.
	const TArray<FInventorySlot> snapshot = Slots;

	for (FInventorySlot& slot : Slots)
	{
		slot.ItemID.Invalidate();
		slot.bIsAnchor = false;
	}

	for (const FGuid& itemId : OrderedItemIDs)
	{
		FItemInstance instance;
		if (!GetItem(itemId, instance))
			continue;

		const FIntPoint size = GetItemFootprintSize(instance.RowName);

		FItemDefinition def;
		const bool bHasDef = GetItemStructForRow(instance.RowName, def);

		int32 anchorIndex = INDEX_NONE;
		for (int32 i = 0; i < Slots.Num(); ++i)
		{
			if (!CanPlaceFootprintAt(i, size))
				continue;
			if (bHasDef && !IsFootprintSlotTypeCompatible(i, size, def))
				continue;

			anchorIndex = i;
			break;
		}

		if (anchorIndex == INDEX_NONE)
		{
			ScreenLog(FString::Printf(
				TEXT("[UInventoryComponent::RebuildLayoutFromOrder] '%s' (%dx%d) had nowhere to go while sorting "
				     "'%s'; the previous layout has been restored."),
				*instance.RowName.ToString(), size.X, size.Y, *InventoryName.ToString()));

			Slots = snapshot;
			return false;
		}

		OccupyFootprint(anchorIndex, size, itemId);
	}

	MarkSlotsDirty();
	BroadcastAllSlots();
	RefreshIsInventoryFull();
	return true;
}

bool UInventoryComponent::SortInventory(const TArray<FInventorySortRule>& Rules)
{
	if (!HasAuthority())
	{
		ScreenLog(TEXT("[UInventoryComponent::SortInventory] Called without authority; use RequestSortInventory."));
		return false;
	}

	// A sort broadcasts OnInventoryChanged for every cell, and an auto-sort hook hanging
	// off that would call straight back in here. One exit point below keeps the flag
	// honest without needing a scope guard.
	if (bIsSorting)
		return false;
	bIsSorting = true;

	ActiveSortRules = Rules;

	bool bResult = true;

	TArray<FInventorySortItem> items;
	CollectSortableItems(Rules, items);

	// Nothing to reorder. An empty rule set is a legal no-op too — it just records that
	// there is no sort in effect.
	if (items.Num() > 1)
	{
		items.StableSort([&Rules](const FInventorySortItem& A, const FInventorySortItem& B)
		{
			return CompareSortItems(A, B, Rules);
		});

		TArray<FGuid> order;
		order.Reserve(items.Num());
		for (const FInventorySortItem& item : items)
			order.Add(item.ItemID);

		bResult = RebuildLayoutFromOrder(order);
	}

	bIsSorting = false;
	return bResult;
}

bool UInventoryComponent::SortByItemSize(EInventorySortDirection Direction, bool bLayerOnExistingSort)
{
	FInventorySortRule rule;
	rule.Key = EInventorySortKey::ItemSize;
	rule.Direction = Direction;

	return SortInventory(BuildLayeredRules(rule, bLayerOnExistingSort));
}

bool UInventoryComponent::SortByDisplayName(EInventorySortDirection Direction, bool bLayerOnExistingSort)
{
	FInventorySortRule rule;
	rule.Key = EInventorySortKey::DisplayName;
	rule.Direction = Direction;

	return SortInventory(BuildLayeredRules(rule, bLayerOnExistingSort));
}

bool UInventoryComponent::SortByFragmentValue(UScriptStruct* FragmentType, FName ValueName,
                                              EInventorySortDirection Direction, bool bLayerOnExistingSort)
{
	FInventorySortRule rule;
	rule.Key = EInventorySortKey::FragmentValue;
	rule.FragmentType = FragmentType;
	rule.ValueName = ValueName;
	rule.Direction = Direction;

	return SortInventory(BuildLayeredRules(rule, bLayerOnExistingSort));
}

void UInventoryComponent::ClearSort()
{
	// Only forgets the layer stack — the items stay exactly where the last sort left
	// them. Nothing to replicate: the layout is already whatever it is.
	ActiveSortRules.Reset();
}

TArray<FInventorySortRule> UInventoryComponent::BuildAutoSortRules() const
{
	TArray<FInventorySortRule> rules;

	// Size is the first layer either when it IS the mode, or when the fragment-type mode
	// explicitly asks for it as well.
	if (AutoSortMode == EAutoSort::ByItemSize
		|| (AutoSortMode == EAutoSort::ByFragmentType && AutoSortByItemSizeAswell))
	{
		FInventorySortRule sizeRule;
		sizeRule.Key = EInventorySortKey::ItemSize;
		sizeRule.Direction = AutoSortSizeDirection;
		rules.Add(sizeRule);
	}

	if (AutoSortMode == EAutoSort::ByFragmentType)
	{
		FInventorySortRule typeRule;
		typeRule.Key = EInventorySortKey::FragmentType;
		typeRule.Direction = EInventorySortDirection::Ascending; // rank 0 first
		typeRule.bMissingValuesLast = true;                      // uncategorised at the end
		rules.Add(typeRule);
	}

	return rules;
}

bool UInventoryComponent::AutoSortNow()
{
	return SortInventory(BuildAutoSortRules());
}

void UInventoryComponent::TryAutoSort()
{
	if (!AutoSort || bIsSorting || !HasAuthority())
		return;

	SortInventory(BuildAutoSortRules());
}

// =====================================================================================
//  Sorting — client entry points
// =====================================================================================

void UInventoryComponent::RequestSortByDisplayName(EInventorySortDirection Direction, bool bLayerOnExistingSort)
{
	FInventorySortRule rule;
	rule.Key = EInventorySortKey::DisplayName;
	rule.Direction = Direction;

	RequestSortInventory(BuildLayeredRules(rule, bLayerOnExistingSort));
}

void UInventoryComponent::RequestSortInventory(const TArray<FInventorySortRule>& Rules)
{
	// Record the chain locally as well as sending it. The client is where layering is
	// decided (it's the one that knows the player pressed "size" and then "damage"), so
	// its copy of ActiveSortRules has to stay in step; the server is handed the whole
	// resolved chain and never has to reason about layers at all.
	ActiveSortRules = Rules;

	if (HasAuthority())
		SortInventory(Rules);
	else
		GetRoutingComponent()->Server_SortInventory(this, Rules);
}

void UInventoryComponent::RequestSortByItemSize(EInventorySortDirection Direction, bool bLayerOnExistingSort)
{
	FInventorySortRule rule;
	rule.Key = EInventorySortKey::ItemSize;
	rule.Direction = Direction;

	RequestSortInventory(BuildLayeredRules(rule, bLayerOnExistingSort));
}

void UInventoryComponent::RequestSortByFragmentValue(UScriptStruct* FragmentType, FName ValueName,
                                                     EInventorySortDirection Direction, bool bLayerOnExistingSort)
{
	FInventorySortRule rule;
	rule.Key = EInventorySortKey::FragmentValue;
	rule.FragmentType = FragmentType;
	rule.ValueName = ValueName;
	rule.Direction = Direction;

	RequestSortInventory(BuildLayeredRules(rule, bLayerOnExistingSort));
}

void UInventoryComponent::RequestAutoSort()
{
	RequestSortInventory(BuildAutoSortRules());
}

void UInventoryComponent::Server_SortInventory_Implementation(UInventoryComponent* Inventory,
                                                              const TArray<FInventorySortRule>& Rules)
{
	UInventoryComponent* target = Inventory ? Inventory : this;
	target->SortInventory(Rules);
}

// =====================================================================================
//  Sub-inventory displays
// =====================================================================================

namespace
{
	/**
	 * One sub-view sort layer expressed as an ordinary sort rule. Returns false for a
	 * layer that can't say anything — No Sorting, or a fragment-value layer with nothing
	 * to read — so the caller can just drop it instead of carrying a rule that compares
	 * every item as equal.
	 */
	bool MakeSubViewSortRule(ESubInventorySort Layer, UScriptStruct* FragmentType, FName ValueName,
	                         EInventorySortDirection Direction, FInventorySortRule& OutRule)
	{
		OutRule = FInventorySortRule();

		switch (Layer)
		{
		case ESubInventorySort::FragmentValue:
			if (!FragmentType || ValueName.IsNone())
				return false;
			OutRule.Key = EInventorySortKey::FragmentValue;
			OutRule.FragmentType = FragmentType;
			OutRule.ValueName = ValueName;
			break;

		case ESubInventorySort::DisplayName:
			OutRule.Key = EInventorySortKey::DisplayName;
			break;

		case ESubInventorySort::ItemSize:
			OutRule.Key = EInventorySortKey::ItemSize;
			break;

		case ESubInventorySort::Quantity:
			OutRule.Key = EInventorySortKey::Quantity;
			break;

		default:
			return false;
		}

		OutRule.Direction = Direction;
		OutRule.bMissingValuesLast = true;
		return true;
	}

	/**
	 * Whether Instance satisfies Filter: an empty Fragments list always passes ("everything"),
	 * WidensFilter is an OR (any named fragment is enough), NarrowsFilter is an AND (every
	 * named fragment must be present).
	 */
	bool MatchesFragmentFilter(const FItemInstance& Instance, const FFragmentFilterOption& Filter)
	{
		if (Filter.Fragments.Num() == 0)
			return true;

		if (Filter.FilterMode == EFragmentFilterMode::WidensFilter)
		{
			for (const TObjectPtr<UScriptStruct>& fragment : Filter.Fragments)
			{
				if (fragment && UItemFragmentLibrary::HasItemFragment(Instance, fragment))
					return true;
			}
			return false;
		}

		// NarrowsFilter
		for (const TObjectPtr<UScriptStruct>& fragment : Filter.Fragments)
		{
			if (fragment && !UItemFragmentLibrary::HasItemFragment(Instance, fragment))
				return false;
		}
		return true;
	}
}

FSubInventoryView UInventoryComponent::GetSubInventoryByFragment(const FSubInventoryQuery& Query) const
{
	// Filtering on a group has no single "the" fragment to read a sort value off, so the
	// first named fragment stands in for it — same rule ResolveFilterKey's callers rely
	// on when a widget's dropdown entry gets handed straight to this query.
	UScriptStruct* const sortFragmentType = Query.FragmentFilter.Fragments.Num() > 0
		? Query.FragmentFilter.Fragments[0]
		: nullptr;

	FSubInventoryView view;
	view.SourceInventory = const_cast<UInventoryComponent*>(this);
	view.Query = Query;
	view.bAllowIntraInventorySwap = false;

	// The two layers become an ordinary rule chain and go through the same comparator the
	// real sort uses. A second layer identical to the first is dropped: it could only ever
	// re-ask a question the first layer already answered.
	TArray<FInventorySortRule> rules;
	FInventorySortRule rule;

	if (MakeSubViewSortRule(Query.FirstSortLayer, sortFragmentType, Query.SortValueName,
		Query.SortFirstLayerDirection, rule))
	{
		rules.Add(rule);
	}

	// A second layer on the same key is dropped even when its direction differs: it would
	// only ever re-ask a question the first layer already answered, and answering it the
	// other way round can't reorder items the first layer called equal.
	if (Query.SecondSortLayer != Query.FirstSortLayer
		&& MakeSubViewSortRule(Query.SecondSortLayer, sortFragmentType, Query.SortValueName,
			Query.SortSecondLayerDirection, rule))
	{
		rules.Add(rule);
	}

	TArray<FSubInventoryEntry> entries;
	TArray<FInventorySortItem> sortItems;
	TSet<FGuid> seen;

	for (int32 slotIndex = 0; slotIndex < Slots.Num(); ++slotIndex)
	{
		const FInventorySlot& slot = Slots[slotIndex];
		if (!slot.ItemID.IsValid() || seen.Contains(slot.ItemID))
			continue;

		FItemInstance instance;
		if (!GetItem(slot.ItemID, instance))
			continue;

		// Marked seen before the filter, so a multi-cell item that fails it isn't
		// re-tested once per cell it covers.
		seen.Add(slot.ItemID);

		if (!MatchesFragmentFilter(instance, Query.FragmentFilter))
			continue;

		FSubInventoryEntry entry;
		const int32 anchorIndex = FindAnchorIndexForItem(slot.ItemID);
		entry.SourceSlotIndex = (anchorIndex != INDEX_NONE) ? anchorIndex : slotIndex;
		entry.ItemID = instance.UUID;
		entry.Item = instance;
		entry.ItemSize = GetItemFootprintSize(instance.RowName);
		entry.SlotType = Slots.IsValidIndex(entry.SourceSlotIndex) ? Slots[entry.SourceSlotIndex].SlotType : slot.SlotType;

		FInventorySortItem sortItem;
		if (rules.Num() > 0)
		{
			BuildSortItem(instance, rules, sortItem);

			// SortValue is for display ("42 damage", "x12"), so it reports the primary
			// layer's value in the units the player would recognise rather than the packed
			// key the comparator sorts on.
			entry.bHasSortValue = sortItem.bRuleHasValue.IsValidIndex(0) && sortItem.bRuleHasValue[0];
			if (entry.bHasSortValue)
			{
				switch (rules[0].Key)
				{
				case EInventorySortKey::ItemSize:
					entry.SortValue = static_cast<float>(entry.ItemSize.X * entry.ItemSize.Y);
					break;
				case EInventorySortKey::Quantity:
					entry.SortValue = static_cast<float>(instance.Quantity);
					break;
				case EInventorySortKey::DisplayName:
					entry.SortValue = 0.f;
					entry.bHasSortValue = false; // nothing numeric to report
					break;
				default:
					entry.SortValue = static_cast<float>(sortItem.RuleValues[0]);
					break;
				}
			}
		}

		entries.Add(MoveTemp(entry));
		sortItems.Add(MoveTemp(sortItem));
	}

	if (rules.Num() > 0 && entries.Num() > 1)
	{
		// Sort an index array and permute once: FSubInventoryEntry carries a whole
		// FItemInstance, and a comparator that moves those around is paying for the copy
		// on every swap of a list the UI rebuilds on each inventory change.
		TArray<int32> order;
		order.Reserve(entries.Num());
		for (int32 i = 0; i < entries.Num(); ++i)
			order.Add(i);

		order.StableSort([&sortItems, &rules](int32 A, int32 B)
		{
			return CompareSortItems(sortItems[A], sortItems[B], rules);
		});

		TArray<FSubInventoryEntry> ordered;
		ordered.Reserve(entries.Num());
		for (int32 index : order)
			ordered.Add(MoveTemp(entries[index]));

		entries = MoveTemp(ordered);
	}

	// Compact by construction: positions are handed out only after filtering and
	// ordering, so the view never contains a gap for an empty or filtered-out slot.
	for (int32 i = 0; i < entries.Num(); ++i)
		entries[i].ViewIndex = i;

	view.Entries = MoveTemp(entries);
	return view;
}

bool UInventoryComponent::RefreshSubInventoryView(const FSubInventoryView& View, FSubInventoryView& OutView) const
{
	const UInventoryComponent* source = View.SourceInventory ? View.SourceInventory.Get() : this;

	OutView = source->GetSubInventoryByFragment(View.Query);
	return OutView.Entries.Num() > 0;
}

void UInventoryComponent::GetSubInventoryLayout(const FSubInventoryView& View, int32 Columns, bool bPackByFootprint,
                                                bool bShowEmptySlots, TArray<FSubInventoryPlacement>& OutPlacements)
{
	OutPlacements.Reset();
	OutPlacements.Reserve(View.Entries.Num());

	const int32 columns = FMath::Max(1, Columns);

	// Cell index -> taken, in the same (row * columns + column) numbering both branches
	// below place into. Built either way so the empty-slot pass at the bottom can skip
	// past real content regardless of which packing mode produced it.
	TArray<bool> occupied;

	if (!bPackByFootprint)
	{
		occupied.Init(true, View.Entries.Num());

		for (int32 i = 0; i < View.Entries.Num(); ++i)
		{
			FSubInventoryPlacement placement;
			placement.ViewIndex = i;
			placement.Row = i / columns;
			placement.Column = i % columns;
			OutPlacements.Add(placement);
		}
	}
	else
	{
		// First-fit over a cell grid that grows downwards — the same shape of walk the real
		// inventory uses to place an item, minus the slot-type rules, which a view has no
		// business enforcing because it isn't moving anything.
		auto Fits = [&occupied, columns](int32 Row, int32 Column, int32 Width, int32 Height)
		{
			if (Column + Width > columns)
				return false;

			for (int32 y = 0; y < Height; ++y)
			{
				for (int32 x = 0; x < Width; ++x)
				{
					const int32 cell = (Row + y) * columns + (Column + x);
					if (occupied.IsValidIndex(cell) && occupied[cell])
						return false;
				}
			}
			return true;
		};

		for (int32 i = 0; i < View.Entries.Num(); ++i)
		{
			const FSubInventoryEntry& entry = View.Entries[i];
			const int32 width = FMath::Clamp(entry.ItemSize.X, 1, columns);
			const int32 height = FMath::Max(1, entry.ItemSize.Y);

			int32 placedRow = 0;
			int32 placedColumn = 0;
			bool bPlaced = false;

			// Terminates: a row past everything already marked is always empty, and an empty
			// row fits any item narrow enough for the panel — which width was clamped to be.
			for (int32 row = 0; !bPlaced; ++row)
			{
				for (int32 column = 0; column + width <= columns; ++column)
				{
					if (!Fits(row, column, width, height))
						continue;

					placedRow = row;
					placedColumn = column;
					bPlaced = true;
					break;
				}
			}

			const int32 needed = (placedRow + height) * columns;
			if (occupied.Num() < needed)
				occupied.AddZeroed(needed - occupied.Num());

			for (int32 y = 0; y < height; ++y)
				for (int32 x = 0; x < width; ++x)
					occupied[(placedRow + y) * columns + (placedColumn + x)] = true;

			FSubInventoryPlacement placement;
			placement.ViewIndex = i;
			placement.Row = placedRow;
			placement.Column = placedColumn;
			placement.RowSpan = height;
			placement.ColumnSpan = width;
			OutPlacements.Add(placement);
		}
	}

	if (!bShowEmptySlots)
		return;

	const UInventoryComponent* const source = View.SourceInventory.Get();
	if (!source)
		return;

	// One placement per REAL empty slot (ItemID invalid) — never a filtered-out occupied
	// slot, since binding InitSlot to that index would show the item the filter or search
	// was asked to hide rather than a true empty cell. Each carries its real slot index in
	// SourceSlotIndex so the caller can feed it straight into InitSlot, same as a real entry.
	int32 cell = 0;
	for (int32 slotIndex = 0; slotIndex < source->Slots.Num(); ++slotIndex)
	{
		if (source->Slots[slotIndex].ItemID.IsValid())
			continue;

		while (occupied.IsValidIndex(cell) && occupied[cell])
			++cell;

		FSubInventoryPlacement placement;
		placement.SourceSlotIndex = slotIndex;
		placement.Row = cell / columns;
		placement.Column = cell % columns;
		OutPlacements.Add(placement);

		++cell;
	}
}

bool UInventoryComponent::GetSubInventoryPlacement(const FSubInventoryView& View, int32 ViewIndex, int32 Columns,
                                                   bool bPackByFootprint, FSubInventoryPlacement& OutPlacement)
{
	OutPlacement = FSubInventoryPlacement();

	if (!View.Entries.IsValidIndex(ViewIndex))
		return false;

	// A 1x1 view doesn't need the packer run to answer this, and that's the common case
	// for a widget asking about itself inside a ForEach.
	if (!bPackByFootprint)
	{
		const int32 columns = FMath::Max(1, Columns);
		OutPlacement.ViewIndex = ViewIndex;
		OutPlacement.Row = ViewIndex / columns;
		OutPlacement.Column = ViewIndex % columns;
		return true;
	}

	// bShowEmptySlots is irrelevant here: this call only ever resolves a real Entry
	// (ViewIndex indexes View.Entries), and empty filler cells aren't entries — they only
	// exist in GetSubInventoryLayout's output. Passing false costs nothing since the
	// padding pass never touches placements at valid entry indices.
	TArray<FSubInventoryPlacement> placements;
	GetSubInventoryLayout(View, Columns, bPackByFootprint, false, placements);

	if (!placements.IsValidIndex(ViewIndex))
		return false;

	OutPlacement = placements[ViewIndex];
	return true;
}

bool UInventoryComponent::GetSubInventoryEntry(const FSubInventoryView& View, int32 ViewIndex,
                                               FSubInventoryEntry& OutEntry) const
{
	if (!View.Entries.IsValidIndex(ViewIndex))
	{
		OutEntry = FSubInventoryEntry();
		return false;
	}

	OutEntry = View.Entries[ViewIndex];
	return true;
}

bool UInventoryComponent::IsValidSubInventorySwap(const FSubInventoryView& View, int32 ViewIndex,
                                                  UInventoryComponent* OtherInventory, int32 OtherIndex,
                                                  bool bDraggingOutOfView) const
{
	FSubInventoryEntry entry;
	if (!GetSubInventoryEntry(View, ViewIndex, entry))
		return false;

	UInventoryComponent* source = View.SourceInventory;
	if (!source || !OtherInventory)
		return false;

	// THE sub-inventory rule. A compact view's positions are display positions with no
	// stable relationship to cells, so a move that starts and ends in the same container
	// would be rearranging slots the player can't see. Rejected here rather than only in
	// the widget, so a mis-wired drag can't get through to SwapSlots.
	if (source == OtherInventory)
		return false;

	if (bDraggingOutOfView)
	{
		// Out of the view: the view's item is the source, the other container the dest.
		return OtherInventory->IsValidSlotSwap(source->GetSlot(entry.SourceSlotIndex),
		                                       OtherInventory->GetSlot(OtherIndex),
		                                       entry.SourceSlotIndex, OtherIndex, source);
	}

	// Into the view, dropped on top of this entry: the roles are reversed.
	return source->IsValidSlotSwap(OtherInventory->GetSlot(OtherIndex),
	                               source->GetSlot(entry.SourceSlotIndex),
	                               OtherIndex, entry.SourceSlotIndex, OtherInventory);
}

bool UInventoryComponent::PreviewSubInventorySwap(const FSubInventoryView& View, int32 ViewIndex,
                                                  UInventoryComponent* OtherInventory, int32 OtherIndex,
                                                  bool bDraggingOutOfView, TArray<int32>& OutDraggedCells,
                                                  TArray<bool>& OutDraggedCellValid,
                                                  TArray<int32>& OutDisplacedCells) const
{
	OutDraggedCells.Reset();
	OutDraggedCellValid.Reset();
	OutDisplacedCells.Reset();

	FSubInventoryEntry entry;
	if (!GetSubInventoryEntry(View, ViewIndex, entry))
		return false;

	UInventoryComponent* source = View.SourceInventory;
	if (!source || !OtherInventory || source == OtherInventory)
		return false;

	if (bDraggingOutOfView)
	{
		return OtherInventory->PreviewSlotSwap(source, entry.SourceSlotIndex, OtherIndex,
		                                       OutDraggedCells, OutDraggedCellValid, OutDisplacedCells);
	}

	return source->PreviewSlotSwap(OtherInventory, OtherIndex, entry.SourceSlotIndex,
	                               OutDraggedCells, OutDraggedCellValid, OutDisplacedCells);
}

void UInventoryComponent::RequestSubInventorySwap(const FSubInventoryView& View, int32 ViewIndex,
                                                  UInventoryComponent* OtherInventory, int32 OtherIndex,
                                                  bool bDraggingOutOfView)
{
	FSubInventoryEntry entry;
	if (!GetSubInventoryEntry(View, ViewIndex, entry))
		return;

	UInventoryComponent* source = View.SourceInventory;
	if (!source || !OtherInventory || source == OtherInventory)
		return; // intra-inventory moves are disabled in a sub-inventory view

	if (bDraggingOutOfView)
		RequestSlotSwap(source, entry.SourceSlotIndex, OtherInventory, OtherIndex);
	else
		RequestSlotSwap(OtherInventory, OtherIndex, source, entry.SourceSlotIndex);
}

void UInventoryComponent::RequestSubInventoryAdd(const FSubInventoryView& View, UInventoryComponent* SourceInventory,
                                                 int32 SourceIndex)
{
	UInventoryComponent* target = View.SourceInventory;
	if (!target || !SourceInventory || target == SourceInventory)
		return;

	if (!SourceInventory->Slots.IsValidIndex(SourceIndex))
		return;

	FItemInstance incoming;
	if (!SourceInventory->GetItem(SourceInventory->GetSlot(SourceIndex).ItemID, incoming))
		return;

	// A compact view has no empty cells to aim at, so "dropped on the view's background"
	// has to resolve to a real destination. GetNextCompatibleSlot is the same two-pass
	// search pickup uses, so a dropped stack merges with a matching one when it can.
	FInventorySlot destSlot;
	if (!target->GetNextCompatibleSlot(incoming, destSlot))
	{
		ScreenLog(FString::Printf(TEXT("[UInventoryComponent::RequestSubInventoryAdd] '%s' has no room for '%s'."),
		                          *target->InventoryName.ToString(), *incoming.RowName.ToString()));
		return;
	}

	RequestSlotSwap(SourceInventory, SourceIndex, target, destSlot.SlotIndex);
}