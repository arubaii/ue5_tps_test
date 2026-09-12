// Copyright arubaii. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Templates/Function.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "GameplayTagContainer.h"

#include "DeveloperSettings/InventorySettings.h"
#include "Inventory/Structs/InventoryEnums.h"
#include "Inventory/Structs/Definition.h"
#include "Inventory/Structs/Instance.h"
#include "Inventory/Fragment/ItemFragment.h"
#include "Inventory/Fragment/ItemFragmentLibrary.h"
#include "InventoryComponent.generated.h"

class UInventoryComponent;
class UUserWidget;
class UAudioCueSet;

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
 * A per-index override of what a slot IS, applied by InitializeInventory.
 *
 * Index addresses the inventory's flat slot array (row-major on a grid container), NOT a
 * position in the SlotOverrides array itself.
 */
USTRUCT(BlueprintType)
struct FSlotOverrideData
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Index = -1; 

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	ESlotType Type = ESlotType::Generic; 
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSoftObjectPtr<UTexture2D> BackgroundImage;

	/**
	 * How many grid cells this ONE slot occupies, anchored at Index and growing +X/+Y.
	 * (1,1) leaves it an ordinary cell.
	 *
	 * A 3x1 here makes a single wide slot out of three cells: the two to the right stop
	 * being slots of their own, anything dropped in fills the whole width (and draws with
	 * the 3x1 entry from FItemDefinition::IconByFootprint), and an item whose own SlotSize
	 * doesn't fit inside 3x1 is refused. Item footprints smaller than the slot are fine —
	 * a 1x1 pistol in a 3x1 holster just fills it.
	 *
	 * Ignored on non-grid containers (GridWidth/GridHeight == 0), where every slot is 1x1
	 * by definition. Overrides that run off the row/grid, or that collide with another
	 * override's cells, are rejected at init with a warning and fall back to (1,1).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1"))
	FIntPoint SlotFootprint = FIntPoint(1, 1);
};

/**
 * A rectangle in grid-cell space. Min is INCLUSIVE, Max is EXCLUSIVE.
 *
 * This is the type the swap rules are written against. Every swap has a CONFINE: the
 * smallest rectangle that encloses everything the swap involves (the dragged item where
 * it came from, the dragged item where it was dropped, and the item it was dropped on).
 * Nothing a swap moves is ever allowed to end up outside that rectangle — see
 * BuildSwapPlan for the full rule set.
 *
 * On a non-grid container (GridWidth/GridHeight == 0) IndexToXY maps index -> (index, 0),
 * so every rect below degrades into a span of flat slot indices and the same maths hold
 * without a separate code path.
 */
struct FInventoryGridRect
{
	FIntPoint Min = FIntPoint(0, 0);
	FIntPoint Max = FIntPoint(0, 0); // exclusive
	bool bValid = false;

	FInventoryGridRect() {}
	FInventoryGridRect(FIntPoint InMin, FIntPoint InMaxExclusive)
		: Min(InMin), Max(InMaxExclusive)
		, bValid(InMaxExclusive.X > InMin.X && InMaxExclusive.Y > InMin.Y) {}

	static FInventoryGridRect FromAnchorAndSize(FIntPoint Anchor, FIntPoint Size)
	{
		return FInventoryGridRect(Anchor, Anchor + Size);
	}

	int32 Width()  const { return bValid ? Max.X - Min.X : 0; }
	int32 Height() const { return bValid ? Max.Y - Min.Y : 0; }
	int32 Area()   const { return Width() * Height(); }

	bool ContainsCell(FIntPoint Cell) const
	{
		return bValid && Cell.X >= Min.X && Cell.X < Max.X && Cell.Y >= Min.Y && Cell.Y < Max.Y;
	}

	bool ContainsRect(const FInventoryGridRect& Other) const
	{
		return bValid && Other.bValid
			&& Other.Min.X >= Min.X && Other.Min.Y >= Min.Y
			&& Other.Max.X <= Max.X && Other.Max.Y <= Max.Y;
	}

	/** Grow to the smallest rectangle enclosing both. An invalid rect contributes nothing. */
	void Encapsulate(const FInventoryGridRect& Other)
	{
		if (!Other.bValid)
			return;
		if (!bValid)
		{
			*this = Other;
			return;
		}
		Min.X = FMath::Min(Min.X, Other.Min.X);
		Min.Y = FMath::Min(Min.Y, Other.Min.Y);
		Max.X = FMath::Max(Max.X, Other.Max.X);
		Max.Y = FMath::Max(Max.Y, Other.Max.Y);
	}

	/** Shrink to the overlap with Other; becomes invalid when they do not overlap. */
	void ClampTo(const FInventoryGridRect& Other)
	{
		if (!bValid || !Other.bValid)
		{
			bValid = false;
			return;
		}
		Min.X = FMath::Max(Min.X, Other.Min.X);
		Min.Y = FMath::Max(Min.Y, Other.Min.Y);
		Max.X = FMath::Min(Max.X, Other.Max.X);
		Max.Y = FMath::Min(Max.Y, Other.Max.Y);
		bValid = Max.X > Min.X && Max.Y > Min.Y;
	}

	FString ToString() const
	{
		return bValid
			? FString::Printf(TEXT("[(%d,%d)..(%d,%d)] %dx%d"), Min.X, Min.Y, Max.X - 1, Max.Y - 1, Width(), Height())
			: FString(TEXT("[invalid]"));
	}
};

/**
 * Transient result of planning a slot swap/move before any state is mutated. Computed by
 * BuildSwapPlan and consumed by SwapSlots; never replicated or exposed to Blueprint —
 * it exists only to separate "is this move legal, and what does it actually do" from
 * the mutation itself.
 */
struct FInventorySwapPlan
{
	bool bValid = false;

	/** The item being dragged. */
	FGuid DraggedItemID;

	/** Where the dragged item's anchor ends up, and its footprint size. */
	int32 DraggedNewAnchor = INDEX_NONE;
	FIntPoint DraggedSize = FIntPoint(1, 1);

	/**
	 * The rectangle (in the DESTINATION container's cell space) this swap is confined to.
	 * Purely informational once the plan is built — kept for logging/debug draw so a
	 * rejected or surprising swap can be explained without re-deriving it.
	 */
	FInventoryGridRect Confine;

	struct FDisplacedItem
	{
		FGuid ItemID;
		int32 NewAnchorInSource = INDEX_NONE;
		FIntPoint Size = FIntPoint(1, 1);
	};

	/** Items native to the destination store that get relocated into the source
		store as part of this same swap. Empty for a normal drop-into-empty-space
		or a same-shape "shift" move into free cells. */
	TArray<FDisplacedItem> Displaced;
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


// =====================================================================================
//  Sorting
//
//  A sort only ever rewrites the SLOT LAYOUT. The instance store is never touched, so
//  Guids, attachment links, quantities and fragment state all survive a sort intact —
//  which is why a sort can be re-run, layered on top of an earlier one, or replaced by
//  a different one with no risk to the data model.
// =====================================================================================

UENUM(BlueprintType)
enum class EInventorySortDirection : uint8
{
	Ascending,
	Descending
};

/** What a single pass of a layered sort compares. */
UENUM(BlueprintType)
enum class EInventorySortKey : uint8
{
	/** Footprint size — area first, then width, then height. */
	ItemSize        UMETA(DisplayName = "Item Size"),

	/** A named int/float read off a chosen fragment (FragmentType + ValueName below). */
	FragmentValue   UMETA(DisplayName = "Fragment Value"),

	/** Rank taken from UInventoryComponent::FragmentTypeSortOrder. Items carrying none
	    of the listed fragments are uncategorised and always land at the very end. */
	FragmentType    UMETA(DisplayName = "Fragment Type"),

	/**
	 * The item definition's authored DisplayName, compared lexically (case-insensitive,
	 * the same ordering FName::LexicalLess gives). Ascending is A-Z. Items whose
	 * definition has no DisplayName count as "missing" and obey bMissingValuesLast.
	 */
	DisplayName     UMETA(DisplayName = "Display Name"),

	/** Stack count. Descending puts the biggest stacks first. */
	Quantity        UMETA(DisplayName = "Quantity")
};

/** Class-defaults auto sort mode. See UInventoryComponent::AutoSort. */
UENUM(BlueprintType)
enum class EAutoSort : uint8
{
	ByItemSize      UMETA(DisplayName = "By Item Size"),
	ByFragmentType  UMETA(DisplayName = "By Fragment Type")
};

/**
 * One pass of a layered sort.
 *
 * Rules are applied in array order: rule 0 is the primary key, rule 1 breaks its ties,
 * rule 2 breaks those, and so on. That is what "layering" means here — a second sort
 * pass does NOT re-shuffle the whole container and undo the first, it only orders items
 * the earlier passes considered equal. Two rules, [ItemSize, FragmentValue], give you
 * "group by size, then order each size group by the fragment value".
 *
 * Any items still tied after every rule fall back to a fixed tiebreak (row name, then
 * quantity, then Guid) so the same inventory always sorts to the same layout on every
 * machine — important, because the server sorts and the clients only see the result.
 */
USTRUCT(BlueprintType)
struct FInventorySortRule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sort Rule")
	EInventorySortKey Key = EInventorySortKey::ItemSize;

	/**
	 * The fragment the value is read from. The picker lists every FItemFragment child
	 * (MetaStruct), which is the "select a fragment in a dropdown" half of the pair.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sort Rule",
		meta = (MetaStruct = "/Script/tps_test.ItemFragment",
				EditCondition = "Key == EInventorySortKey::FragmentValue"))
	TObjectPtr<UScriptStruct> FragmentType = nullptr;

	/**
	 * Which numeric property on that fragment to sort by — the second dropdown. Filled
	 * from UInventoryComponent::GetFragmentValueOptions (every int/float/bool/enum
	 * value on any fragment, deduplicated). Nested values use a dotted path, e.g.
	 * "AmmoCount.AmmoInMag".
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sort Rule",
		meta = (GetOptions = "GetFragmentValueOptions",
				EditCondition = "Key == EInventorySortKey::FragmentValue"))
	FName ValueName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sort Rule")
	EInventorySortDirection Direction = EInventorySortDirection::Descending;

	/**
	 * Items that don't carry the fragment (or whose fragment has no such value) sort
	 * past the ones that do, in either direction. Turn this off and they lead instead.
	 * Forced ON for EInventorySortKey::FragmentType — uncategorised always goes last.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sort Rule")
	bool bMissingValuesLast = true;
};

/**
 * Transient per-item sort record. Every key a sort needs is resolved ONCE, up front,
 * into RuleValues — so the comparator does no property lookups, no definition lookups
 * and no map searches while sorting (an O(n log n) comparator that reflects into
 * fragments each call gets expensive fast on a big stash).
 *
 * Not a USTRUCT and never replicated: like FInventorySwapPlan, it exists only between
 * "work out the new order" and "write the new order".
 */
struct FInventorySortItem
{
	FGuid ItemID;
	FName RowName;
	int32 Quantity = 0;
	FIntPoint Size = FIntPoint(1, 1);

	/** Authored display name, resolved once with everything else. */
	FName DisplayName;

	/** Parallel to the rule array: the resolved key for each pass, and whether it resolved. */
	TArray<double> RuleValues;
	TArray<bool> bRuleHasValue;

	/**
	 * Parallel to the rule array, meaningful only where Rules[i].Key == DisplayName:
	 * a name key can't be squeezed into a double without losing the ordering, so the
	 * text keys ride alongside the numeric ones rather than being encoded into them.
	 */
	TArray<FName> RuleNameValues;

	/**
	 * Parallel to the rule array, meaningful only where Rules[i].Key == FragmentType:
	 * the intra-category value read off FragmentTypeSortOrder's ValueName for the
	 * category this item landed in (weapons by Damage, consumables by HealAmount, ...).
	 * Compared as a tiebreak immediately after two items land in the same category, so a
	 * FragmentType rule sorts on two levels — category, then this — without needing a
	 * second rule entry.
	 */
	TArray<double> RuleSecondaryValues;
	TArray<bool> bRuleHasSecondaryValue;
};

// =====================================================================================
//  Sub-inventory display
//
//  A VIEW, not a container. The items keep living in the inventory they were always in;
//  a view is a filtered, ordered, gap-free list of pointers back at real slot indices.
//  Nothing here mutates anything, and no instance is ever copied between stores to build
//  one — which is what makes it safe to build a view every frame from UI code.
// =====================================================================================

/**
 * One pass of a sub-inventory view's sort. Used twice by GetSubInventoryByFragment:
 * FirstSortLayer is the primary ordering, SecondSortLayer only ever speaks about items
 * the first layer called equal (two stacks of the same quantity, two items of the same
 * footprint, ...). Both default to None, which leaves the view in slot order.
 *
 * ByFragmentValue reads SortValueName off the view's own FragmentType — there is no
 * separate sort fragment, because filtering on one fragment and sorting on a value
 * from a different one was never a real case.
 */
UENUM(BlueprintType)
enum class ESubInventorySort : uint8
{
	None            UMETA(DisplayName = "No Sorting"),
	FragmentValue   UMETA(DisplayName = "By Fragment Value"),
	DisplayName     UMETA(DisplayName = "By Display Name"),
	ItemSize        UMETA(DisplayName = "By Item Size"),
	Quantity        UMETA(DisplayName = "By Quantity")
};

/** One item in a sub-inventory view. ViewIndex is compact; SourceSlotIndex is real. */
USTRUCT(BlueprintType)
struct FSubInventoryEntry
{
	GENERATED_BODY()

	/** 0..N-1 with no gaps — the position this item takes in the compact display. */
	UPROPERTY(BlueprintReadOnly, Category = "Sub Inventory")
	int32 ViewIndex = INDEX_NONE;

	/** The item's REAL anchor index in SourceInventory. This is what swaps use. */
	UPROPERTY(BlueprintReadOnly, Category = "Sub Inventory")
	int32 SourceSlotIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Sub Inventory")
	FGuid ItemID;

	UPROPERTY(BlueprintReadOnly, Category = "Sub Inventory")
	FItemInstance Item;

	/** Authored footprint, for widgets that want to draw the item at its real size. */
	UPROPERTY(BlueprintReadOnly, Category = "Sub Inventory")
	FIntPoint ItemSize = FIntPoint(1, 1);

	/** The real slot's type — a view slot is only a window onto that slot. */
	UPROPERTY(BlueprintReadOnly, Category = "Sub Inventory")
	ESlotType SlotType = ESlotType::Generic;

	/** The value this entry was sorted on, when the view was built with a sort. */
	UPROPERTY(BlueprintReadOnly, Category = "Sub Inventory")
	float SortValue = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Sub Inventory")
	bool bHasSortValue = false;
};

/** How a fragment filter's Fragments list is combined against an item's fragments. */
UENUM(BlueprintType)
enum class EFragmentFilterMode : uint8
{
	/** Keeps items carrying ANY of the listed fragments (an OR). */
	WidensFilter  UMETA(DisplayName = "Widens Filter"),
	/** Keeps items carrying EVERY listed fragment (an AND). */
	NarrowsFilter UMETA(DisplayName = "Narrows Filter")
};

/**
 * A group of item fragments to filter by, plus how that group is combined against an
 * item's fragments. Declared once here — rather than in a UI class — so both a widget's
 * authored filter dropdown (FragmentFilterOptions on W_InventoryBase) and the runtime
 * query below (FSubInventoryQuery::FragmentFilter) share the same type: the widget
 * resolves a dropdown selection straight into one of these and hands it to the query
 * with no conversion step.
 */
USTRUCT(BlueprintType)
struct FFragmentFilterOption
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Search",
		meta = (MetaStruct = "/Script/tps_test.ItemFragment"))
	TArray<TObjectPtr<UScriptStruct>> Fragments;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Search")
	EFragmentFilterMode FilterMode = EFragmentFilterMode::WidensFilter;

	bool operator==(const FFragmentFilterOption& Other) const
	{
		return FilterMode == Other.FilterMode && Fragments == Other.Fragments;
	}

	friend uint32 GetTypeHash(const FFragmentFilterOption& Key)
	{
		uint32 Hash = GetTypeHash(Key.FilterMode);
		for (const TObjectPtr<UScriptStruct>& Fragment : Key.Fragments)
			Hash = HashCombine(Hash, GetTypeHash(Fragment));
		return Hash;
	}
};

/**
 * Everything that defines a sub-inventory view except which inventory it's built from.
 * This is the SINGLE SOURCE for that parameter list: GetSubInventoryByFragment takes one
 * of these, ToggleSubView (Blueprint, on W_InventoryBase) takes the same struct, and
 * FSubInventoryView stores one so RefreshSubInventoryView can rebuild without the caller
 * re-supplying six arguments. Add a sort option here and every one of those follows
 * automatically — there is no second parameter list anywhere to fall out of sync.
 */
USTRUCT(BlueprintType)
struct FSubInventoryQuery
{
	GENERATED_BODY()

	/**
	 * The fragments items are filtered by, and how they combine (see FFragmentFilterOption).
	 * An empty Fragments list means "everything in the container", same as the old null
	 * FragmentType did — FilterMode is simply irrelevant with nothing to combine.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sub Inventory")
	FFragmentFilterOption FragmentFilter;

	/** Primary ordering. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sub Inventory")
	ESubInventorySort FirstSortLayer = ESubInventorySort::None;

	/** Breaks ties the first layer left — nothing else. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sub Inventory")
	ESubInventorySort SecondSortLayer = ESubInventorySort::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sub Inventory",
		meta = (GetOptions = "GetFragmentValueOptions"))
	FName SortValueName = NAME_None;

	/** Direction of the first layer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sub Inventory")
	EInventorySortDirection SortFirstLayerDirection = EInventorySortDirection::Descending;

	/** Direction of the second layer, independent of the first. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sub Inventory")
	EInventorySortDirection SortSecondLayerDirection = EInventorySortDirection::Descending;
};

/**
 * The result of GetSubInventoryByFragment: every item in one container carrying a given
 * fragment, compacted (no empty slots) and optionally ordered by a fragment value.
 *
 * The query that produced it is carried along, so a widget can rebuild the view from
 * the view itself when OnInventoryChanged fires (RefreshSubInventoryView) instead of
 * caching the arguments separately.
 *
 * SWAP RULE: bAllowIntraInventorySwap is always false. A compact view has no stable
 * relationship between display position and slot position, so "swap slot 3 with slot 5"
 * inside it would mean rearranging cells the player can't see. Moves OUT of the view
 * into another container (and in from one) are the only legal ones — enforced in
 * IsValidSubInventorySwap / RequestSubInventorySwap, not merely by convention in the UI.
 */
USTRUCT(BlueprintType)
struct FSubInventoryView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Sub Inventory")
	TObjectPtr<UInventoryComponent> SourceInventory = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Sub Inventory")
	TArray<FSubInventoryEntry> Entries;

	/** The query that produced this view. Kept for RefreshSubInventoryView. */
	UPROPERTY(BlueprintReadOnly, Category = "Sub Inventory")
	FSubInventoryQuery Query;

	/** Always false. See the struct comment. */
	UPROPERTY(BlueprintReadOnly, Category = "Sub Inventory")
	bool bAllowIntraInventorySwap = false;
};

/**
 * Where one view entry goes in the grid that displays the view.
 *
 * A view is compact in the ARRAY sense — Entries has no holes — but that says nothing
 * about cells. The widget still has to decide which row and column each entry occupies,
 * and if it reuses the item's real slot index for that (index / GridWidth, index %
 * GridWidth, which is what a normal slot does) the view redraws the items exactly where
 * they sit in the full inventory, holes and all. GetSubInventoryLayout hands out
 * positions in ViewIndex order instead, which is the compaction the display needs.
 */
USTRUCT(BlueprintType)
struct FSubInventoryPlacement
{
	GENERATED_BODY()

	/** INDEX_NONE for a filler cell (see SourceSlotIndex) — no entry backs it. */
	UPROPERTY(BlueprintReadOnly, Category = "Sub Inventory")
	int32 ViewIndex = INDEX_NONE;

	/**
	 * Only set (not INDEX_NONE) on a filler cell — one of GetSubInventoryLayout's
	 * bShowEmptySlots additions. It's the real, genuinely empty inventory slot this
	 * placeholder stands for, so it can be fed straight into InitSlot exactly the way a
	 * real entry's SourceSlotIndex is: InitSlot already renders correctly whether or not
	 * the index it's given has an item (RefreshGrid calls it identically either way), so
	 * a filler cell needs no special no-item handling of its own.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Sub Inventory")
	int32 SourceSlotIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Sub Inventory")
	int32 Row = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Sub Inventory")
	int32 Column = 0;

	/** 1 unless the layout was asked to honour footprints. */
	UPROPERTY(BlueprintReadOnly, Category = "Sub Inventory")
	int32 RowSpan = 1;

	UPROPERTY(BlueprintReadOnly, Category = "Sub Inventory")
	int32 ColumnSpan = 1;
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
 *                   Addressed in 2D via GridWidth/GridHeight (SlotIndex = Y * GridWidth + X)
 *                   when those are configured. Every item is currently 1x1, so every
 *                   occupied slot sets FInventorySlot::bIsAnchor = true; that field is
 *                   groundwork for multi-cell items and isn't load-bearing yet.
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
	
	/** Slot type every slot in this inventory is created with (InitializeInventory parity). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Config")
	ESlotType SlotType = ESlotType::Generic;

	/* Override the slot type at a given index */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Config")
	TArray<FSlotOverrideData> SlotOverrides;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Config")
	EInventoryType InventoryType = EInventoryType::Backpack;
	
	/**
	 * Number of slots wide. Slot count in cell units, NOT pixels — pixel sizing is a
	 * widget-side concern. When this and GridHeight are both > 0, InitializeInventory
	 * builds Slots as a GridWidth * GridHeight grid (row-major: SlotIndex = Y * GridWidth + X).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Config")
	int32 GridWidth = 0;

	/** Number of slots tall. See GridWidth. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Config")
	int32 GridHeight = 0;

	/** True when this container is grid-addressed (GridWidth/GridHeight both > 0).
	    Non-grid containers ignore item SlotSize and behave as pure 1x1-per-slot. */
	UFUNCTION(BlueprintPure, Category = "Inventory|Config")
	bool IsGridEnabled() const { return GridWidth > 0 && GridHeight > 0; }
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Config")
	bool ShouldFillDefaultInventory = false;

	/** Replaces the hardcoded GEN_Potion/GEN_Hatchet seeding. Used when ShouldFillDefaultInventory. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Config", meta = (EditCondition = "ShouldFillDefaultInventory"))
	TArray<FDefaultInventoryEntry> DefaultItems;

	/** See enum comment — reserved; current build replicates to all relevant connections. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory|Config")
	EInventoryReplicationScope ReplicationScope = EInventoryReplicationScope::AllClients;

	// ---------------------------------------------------------------------------------
	// Auto sort (class defaults)
	//
	// When enabled the container re-sorts itself after anything is added or removed
	// (pickup, drop, split, consume, a cross-container swap landing here, a load). It
	// deliberately does NOT re-sort after an intra-container swap: dragging an item
	// somewhere inside its own bag is the player arranging things by hand, and sorting
	// on top of that would snatch the item straight back.
	// ---------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Sorting")
	bool AutoSort = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Sorting",
		meta = (EditCondition = "AutoSort"))
	EAutoSort AutoSortMode = EAutoSort::ByItemSize;

	/**
	 * Adds item size as the FIRST layer of the auto sort, with the fragment-type ranking
	 * as the second. Off, fragment type is the only key and size never enters into it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Sorting",
		meta = (EditCondition = "AutoSort && AutoSortMode == EAutoSort::ByFragmentType"))
	bool AutoSortByItemSizeAswell = false;

	/** Direction of the item-size layer of the auto sort (biggest first by default). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Sorting",
		meta = (EditCondition = "AutoSort"))
	EInventorySortDirection AutoSortSizeDirection = EInventorySortDirection::Descending;

	/**
	 * Fragment -> value name, exactly the same pairing SortByFragmentValue takes as two
	 * separate pins. This is BOTH the category list and the per-category sort key:
	 *
	 *   - CATEGORY ORDER is the order entries were added to the map (map iteration
	 *     order follows insertion order as long as nothing is removed, which is the only
	 *     way this map is ever edited). { FWeaponItemFragment: "Damage", FConsumableFragment:
	 *     "HealAmount" } puts every weapon ahead of every consumable.
	 *   - WITHIN a category, items are then ordered by that category's own ValueName —
	 *     weapons by Damage, consumables by HealAmount — the same as a FragmentValue
	 *     sort layer would, just picked automatically per group instead of once globally.
	 *
	 * An item carrying several listed fragments takes its BEST (earliest-added) category.
	 * An item carrying none of them is uncategorised and is placed after every category,
	 * regardless of sort direction.
	 *
	 * NOTE: Unreal has no way to put metadata on a TMap's KEY, so the key picker here
	 * lists every USTRUCT rather than only fragments — pick FItemFragment children. The
	 * value's dropdown lists every fragment's value names for the same reason the
	 * SortByFragmentValue pin does (see GetFragmentValueOptions) — it can't narrow itself
	 * to the row's own key.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Sorting",
		meta = (EditCondition = "AutoSort && AutoSortMode == EAutoSort::ByFragmentType",
				GetOptions = "GetFragmentValueOptions", ForceInlineRow))
	TMap<TObjectPtr<UScriptStruct>, FName> FragmentTypeSortOrder;

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
	bool GetItemStructByID(FGuid ItemID, FItemDefinition& ItemStructure) const;

	UFUNCTION(BlueprintPure, Category = "Inventory|Items", meta = (ReturnDisplayName = "Contains"))
	bool ContainsItem(FGuid ItemID) const;

	/** IsValidItemID parity: non-empty Guid AND present in this store. */
	UFUNCTION(BlueprintPure, Category = "Inventory|Items", meta = (ReturnDisplayName = "IsValid"))
	bool IsValidItemID(FGuid ItemID) const;

	// ---------------------------------------------------------------------------------
	// Slot reads (UI-facing, preserved)
	// ---------------------------------------------------------------------------------

	/** Convenience: the item instance occupying the slot at Index (empty slot -> default FItemInstance). */
	UFUNCTION(BlueprintPure, Category = "Inventory|Slots", meta = (ReturnDisplayName = "Found"))
	bool GetItemByIndex(int32 Index, FItemInstance& OutItem) const;
	
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
	 * Validation for a drag/drop swap: true when dropping the item at MyIndex (in
	 * TargetInventory, i.e. the SOURCE container) onto TargetIndex of this component
	 * would either merge the two stacks or produce a legal swap.
	 *
	 * This asks BuildSwapPlan the exact question SwapSlots asks, so a green highlight in
	 * the UI and a successful swap on the server can never disagree.
	 */
	UFUNCTION(BlueprintPure, Category = "Inventory|Slots", meta = (ReturnDisplayName = "IsValid"))
	bool IsValidSlotSwap(const FInventorySlot& MyInventorySlot, const FInventorySlot& TargetInventorySlot,
						 int32 MyIndex, int32 TargetIndex, UInventoryComponent* TargetInventory) const;

	/**
	 * Would this drop merge two stacks instead of swapping them? (Same row, same
	 * fragments, both stackable, neither already full.) Shared by SwapSlots and the
	 * validation/preview calls so "what the UI shows" and "what the server does" are
	 * driven by one predicate.
	 */
	UFUNCTION(BlueprintPure, Category = "Inventory|Slots", meta = (ReturnDisplayName = "WouldMerge"))
	bool WouldMergeSlots(UInventoryComponent* SourceInventory, int32 SourceIndex, int32 DestIndex) const;

	/**
	 * Drag preview for the UI: the cells a drop would light up, without mutating anything.
	 * OutDraggedCells are indices in THIS component; OutDisplacedCells are indices in
	 * SourceInventory (displaced items always relocate into the container the dragged
	 * item came from). OutDraggedCellValid is parallel to OutDraggedCells — one bool per
	 * cell, so a single incompatible slot type can redden just that cell instead of the
	 * whole footprint. Displaced cells have no per-cell notion of invalid: if the plan
	 * exists at all, every displaced item already landed somewhere legal.
	 *
	 * Returns false — and empties every array — for a drop that isn't even attemptable
	 * (bad indices, or a hover that resolves back to the item's own anchor and moves
	 * nothing). A drop that WAS attempted but refused still returns false while filling
	 * OutDraggedCells with the attempted footprint, so the UI has something to redden.
	 */
	UFUNCTION(BlueprintPure, Category = "Inventory|Slots", meta = (ReturnDisplayName = "IsValid"))
	bool PreviewSlotSwap(UInventoryComponent* SourceInventory, int32 SourceIndex, int32 DestIndex,
						 TArray<int32>& OutDraggedCells, TArray<bool>& OutDraggedCellValid,
						 TArray<int32>& OutDisplacedCells) const;

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

	// ---------------------------------------------------------------------------------
	// Audio
	// ---------------------------------------------------------------------------------

	/**
	 * Tag -> sound lookup for this inventory's actions. Drop an asset in to give this
	 * inventory sounds; leave it null and every action stays silent rather than erroring.
	 * Different inventories (player backpack vs. a metal footlocker) can point at
	 * different sets without any code change.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Audio")
	TObjectPtr<UAudioCueSet> AudioCues;

	/**
	 * Fires CueTag through AudioCues on every client. Server-only mutations (swaps,
	 * merges, equips) call this at the point they succeed; the multicast is what gets
	 * the trigger from server-authoritative logic onto the clients who need to hear it.
	 * A tag with no entry in AudioCues (or a null AudioCues) plays nothing — this never
	 * needs to be wrapped in a null check at the call site.
	 */
	UFUNCTION(NetMulticast, Unreliable, Category = "Inventory|Audio")
	void Multicast_PlayInventoryCue(FGameplayTag CueTag);
	
	/** Merge the rest of the same items onto this slot until it reaches its max stack */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory|Actions")
	void MergeAll(const FInventorySlot& MyItemSlot);

	/**
	 * Run MergeAll once for every distinct stackable item currently in the inventory, so
	 * every item type ends up as few stacks as possible rather than scattered across
	 * whatever slots picked-up quantities happened to land in.
	 *
	 * "Where valid" — an item is skipped, not errored, if it doesn't carry
	 * FStackableFragment (nothing to merge) or if it was already fully absorbed into
	 * another stack by an earlier item in this same pass (nothing left of it to merge).
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory|Actions")
	void MergeAllStacks();

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

	// =============================================================================================================
	// Slot overrides
	// =============================================================================================================
	
	/** 
	 * @param SlotIndex This should be this index in the inventory, not for the SlotOverride array on the component
	 *
	 * Resolves through oversized slots: asking about a cell that a 3x1 override swallowed
	 * returns that override, so UI that only knows a cell index still gets the right data.
 	 */
	UFUNCTION(BlueprintPure)
	void GetSlotOverrideByIndex(int SlotIndex, 
								FSlotOverrideData& OverrideData, 
								UPARAM(DisplayName = "Found") bool& bFound);

	/**
	 * How many cells the SLOT at this index spans on its own — (1,1) unless an override
	 * widened it. This is what the UI should size and span a slot widget by, not the
	 * item's footprint: an empty 3x1 slot is still 3x1.
	 */
	UFUNCTION(BlueprintPure, Category = "Inventory|Slots")
	FIntPoint GetSlotFootprintSize(int32 SlotIndex) const;

	/**
	 * True when this cell was swallowed by an oversized slot anchored elsewhere. Such a
	 * cell is not a slot: it never takes an item of its own and never draws a widget.
	 */
	UFUNCTION(BlueprintPure, Category = "Inventory|Slots")
	bool IsSlotShadowCell(int32 SlotIndex) const;

	/**
	 * The index of the slot that owns this cell — itself for an ordinary slot, the anchor
	 * for a cell inside an oversized one. Use it to turn "the cell the cursor is over"
	 * into "the slot the player means".
	 */
	UFUNCTION(BlueprintPure, Category = "Inventory|Slots")
	int32 GetOwningSlotIndex(int32 SlotIndex) const;

	/**
	 * The region an item of ItemSize would ACTUALLY occupy if anchored here.
	 *
	 *   ordinary slot   -> ItemSize (unchanged; the pre-existing behaviour)
	 *   oversized slot  -> the slot's own footprint, because the item fills it
	 *   won't fit       -> (0,0), which every footprint primitive treats as "no"
	 *
	 * Idempotent, so it is safe for a caller that already holds an effective size to pass
	 * it back through. Every placement path funnels through this, which is why adding a
	 * slot footprint didn't need each of those paths rewritten.
	 */
	UFUNCTION(BlueprintPure, Category = "Inventory|Slots")
	FIntPoint GetEffectiveFootprintAt(int32 AnchorIndex, FIntPoint ItemSize) const;

	/**
	 * Re-stamps SlotFootprint/OwningSlotIndex (and SlotType) across Slots from the
	 * authored SlotOverrides, leaving item occupancy alone. Called by InitializeInventory
	 * and again after LoadFromRecord, so a save written before an override was authored
	 * (or with a since-changed one) still comes back with the current slot layout.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory|Setup")
	void ApplySlotOverridesToSlots();

	// =================================================================================
	//  Sorting
	//
	//  Sorting writes Slots, so it is authority-only, exactly like SwapSlots. UI calls
	//  the Request* wrappers below; they run directly on the server or route a Server
	//  RPC through the player's component, the same route RequestSlotSwap takes.
	//
	//  LAYERING. The component remembers the rules of the last sort in ActiveSortRules.
	//  Passing bLayerOnExistingSort APPENDS the new rule to that list rather than
	//  replacing it, so:
	//
	//      SortByItemSize(Descending, bLayer = false)                  -> [Size]
	//      SortByFragmentValue(Weapon, "Damage", Desc, bLayer = true)  -> [Size, Damage]
	//
	//  is one two-key sort (size groups, damage-ordered within each group), not two
	//  sorts where the second undoes the first. Calling the same key again with
	//  bLayer = true replaces that layer in place instead of stacking a contradictory
	//  copy of it, so hammering a UI button can't grow the rule list without bound.
	// =================================================================================

	/**
	 * Order by footprint: area first, then width, then height. Descending (big items
	 * first) packs a grid better, which is why it's the default.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory|Sorting", meta = (ReturnDisplayName = "Sorted"))
	bool SortByItemSize(EInventorySortDirection Direction = EInventorySortDirection::Descending,
	                    bool bLayerOnExistingSort = false);

	/**
	 * Order by a numeric value on a fragment: pick the fragment (FragmentType), then the
	 * int/float on it (ValueName — dotted paths like "AmmoCount.AmmoInMag" reach nested
	 * values). Items without that fragment/value keep their relative order and are
	 * pushed to the end.
	 *
	 * Defaults to layering, because "sort by value" is nearly always the refinement
	 * pass on top of a coarser one.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory|Sorting", meta = (ReturnDisplayName = "Sorted"))
	bool SortByFragmentValue(UPARAM(meta = (MetaStruct = "/Script/tps_test.ItemFragment")) UScriptStruct* FragmentType,
	                         UPARAM(meta = (GetOptions = "GetFragmentValueOptions")) FName ValueName,
	                         EInventorySortDirection Direction = EInventorySortDirection::Descending,
	                         bool bLayerOnExistingSort = true);

	/**
	 * Order by the item definition's DisplayName. Ascending is A-Z, which is why it's the
	 * default here and nowhere else — nobody asks for an alphabetical list backwards.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory|Sorting", meta = (ReturnDisplayName = "Sorted"))
	bool SortByDisplayName(EInventorySortDirection Direction = EInventorySortDirection::Ascending,
	                       bool bLayerOnExistingSort = false);

	/** The general form: apply an explicit rule chain. Both functions above funnel here. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory|Sorting", meta = (ReturnDisplayName = "Sorted"))
	bool SortInventory(const TArray<FInventorySortRule>& Rules);

	/** Run the class-defaults auto sort now, whatever AutoSortMode is set to. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory|Sorting", meta = (ReturnDisplayName = "Sorted"))
	bool AutoSortNow();

	/** Forget the layer stack. The layout is left exactly as it is. */
	UFUNCTION(BlueprintCallable, Category = "Inventory|Sorting")
	void ClearSort();

	UFUNCTION(BlueprintPure, Category = "Inventory|Sorting")
	TArray<FInventorySortRule> GetActiveSortRules() const { return ActiveSortRules; }

	/** The rule chain AutoSortNow would run, built from the class-defaults settings. */
	UFUNCTION(BlueprintPure, Category = "Inventory|Sorting")
	TArray<FInventorySortRule> BuildAutoSortRules() const;

	// --- Client entry points (call these from UI) ---

	UFUNCTION(BlueprintCallable, Category = "Inventory|Sorting")
	void RequestSortByItemSize(EInventorySortDirection Direction = EInventorySortDirection::Descending,
	                           bool bLayerOnExistingSort = false);

	UFUNCTION(BlueprintCallable, Category = "Inventory|Sorting")
	void RequestSortByFragmentValue(UPARAM(meta = (MetaStruct = "/Script/tps_test.ItemFragment")) UScriptStruct* FragmentType,
	                                UPARAM(meta = (GetOptions = "GetFragmentValueOptions")) FName ValueName,
	                                EInventorySortDirection Direction = EInventorySortDirection::Descending,
	                                bool bLayerOnExistingSort = true);

	UFUNCTION(BlueprintCallable, Category = "Inventory|Sorting")
	void RequestSortByDisplayName(EInventorySortDirection Direction = EInventorySortDirection::Ascending,
	                              bool bLayerOnExistingSort = false);

	UFUNCTION(BlueprintCallable, Category = "Inventory|Sorting")
	void RequestSortInventory(const TArray<FInventorySortRule>& Rules);

	UFUNCTION(BlueprintCallable, Category = "Inventory|Sorting")
	void RequestAutoSort();

	// --- Fragment value reflection (what fills the two dropdowns) ---

	/**
	 * Every sortable value on every fragment type in the project, as "Name" or
	 * "Outer.Inner" paths, deduplicated and alphabetised. This is what the ValueName
	 * dropdowns are populated from.
	 *
	 * Unreal's GetOptions can't see a sibling pin's value, so this list can't narrow
	 * itself to the fragment you picked in the pin next to it — it offers every value
	 * name that exists on any fragment, and a name that doesn't exist on the chosen
	 * fragment simply resolves to "no value" (that item sorts to the end) rather than
	 * failing. GetFragmentValueOptionsForFragment gives the filtered list for UI that
	 * builds its own dropdown, and is what a details customisation would call.
	 */
	UFUNCTION(BlueprintPure, Category = "Inventory|Sorting")
	static TArray<FString> GetFragmentValueOptions();

	/** The sortable values on ONE fragment type. Empty for a null/invalid struct. */
	UFUNCTION(BlueprintPure, Category = "Inventory|Sorting")
	static TArray<FString> GetFragmentValueOptionsForFragment(
		UPARAM(meta = (MetaStruct = "/Script/tps_test.ItemFragment")) UScriptStruct* FragmentType);

	/** Every FItemFragment child struct — the contents of the fragment dropdown. */
	UFUNCTION(BlueprintPure, Category = "Inventory|Sorting")
	static TArray<UScriptStruct*> GetSortableFragmentTypes();

	/** Read one numeric fragment value off a stored item. */
	UFUNCTION(BlueprintPure, Category = "Inventory|Fragments", meta = (ReturnDisplayName = "Found"))
	bool GetItemFragmentValue(FGuid ItemID,
	                          UPARAM(meta = (MetaStruct = "/Script/tps_test.ItemFragment")) UScriptStruct* FragmentType,
	                          UPARAM(meta = (GetOptions = "GetFragmentValueOptions")) FName ValueName,
	                          float& OutValue) const;

	/** Native form of the above, against an instance you already hold. */
	static bool GetFragmentValue(const FItemInstance& Instance, const UScriptStruct* FragmentType,
	                             FName ValueName, double& OutValue);

	// =================================================================================
	//  Sub-inventory displays
	//
	//  Build a view with GetSubInventoryByFragment, then drive the display straight off
	//  FSubInventoryView::Entries — one widget per entry, in ViewIndex order, no empty
	//  slots. Each entry carries the item's REAL SourceSlotIndex, so an existing
	//  W_InventorySlot initialised with (View.SourceInventory, Entry.SourceSlotIndex)
	//  behaves exactly like a normal slot: previews, merges and swaps all keep working
	//  through the code they already use, and nothing in the swap planner needs to learn
	//  that sub-inventories exist.
	//
	//  The one rule a view adds is that a move must CROSS containers. Route drags
	//  through RequestSubInventorySwap and that rule is enforced server-side rather than
	//  just visually.
	// =================================================================================

	/**
	 * Every item in this container matching Query.FragmentFilter, compacted into a
	 * gap-free list. Leave FragmentFilter.Fragments empty for "everything". Otherwise
	 * FragmentFilter.FilterMode decides how the list is combined: WidensFilter keeps an
	 * item carrying ANY of the named fragments, NarrowsFilter keeps only items carrying
	 * EVERY one of them.
	 *
	 * SORTING. Two layers of the same enum. FirstSortLayer is the ordering; SecondSortLayer
	 * is consulted only where the first found two items equal — quantity then name, size
	 * then damage, and so on. Leave both at No Sorting for slot order.
	 *
	 * SortValueName is only read when one of the layers is By Fragment Value, and it is
	 * read off the first fragment in FragmentFilter.Fragments: filtering on a group and
	 * sorting on a value from some other, unnamed fragment isn't a case worth a pin, so
	 * there's no separate sort fragment — an empty Fragments list means no value to read.
	 *
	 * Each layer has its own direction, so "biggest stacks first, ties alphabetically"
	 * is one call. On By Display Name, Ascending is A-Z.
	 *
	 * Only anchored, slotted items appear. Attachment instances live in the store but
	 * occupy no slot, and a sub-inventory is a display of what's IN the bag; listing a
	 * scope that is fitted to a rifle as a peer of the rifle would be a lie the player
	 * could then try to drag.
	 *
	 * Takes FSubInventoryQuery rather than six loose parameters so nothing else that needs
	 * "the same six things GetSubInventoryByFragment takes" — ToggleSubView being the case
	 * that matters — has to re-declare that list; it takes the same struct instead.
	 */
	UFUNCTION(BlueprintPure, Category = "Inventory|SubInventory")
	FSubInventoryView GetSubInventoryByFragment(const FSubInventoryQuery& Query) const;

	/**
	 * Field-by-field equality for FSubInventoryQuery, without hand-writing the comparison.
	 * Backed by UScriptStruct::CompareScriptStruct, which compares every property
	 * reflectively — a pointer field compares by identity, an enum by value, and so on —
	 * so this stays correct if a field is ever added to or removed from the struct with
	 * no edit needed here. Used by ToggleSubView's Blueprint graph to tell "same query
	 * already showing" from "different query, re-target the view".
	 */
	UFUNCTION(BlueprintPure, Category = "Inventory|SubInventory",
		meta = (DisplayName = "Equal (Sub Inventory Query)", CompactNodeTitle = "==", Keywords = "== equal"))
	static bool Equal_SubInventoryQuery(const FSubInventoryQuery& A, const FSubInventoryQuery& B)
	{
		return FSubInventoryQuery::StaticStruct()->CompareScriptStruct(&A, &B, PPF_None);
	}

	/**
	 * Grid positions for a view, handed out in ViewIndex order — the missing half of
	 * "compact". Feed the result into the GridSlot of each widget you spawn instead of
	 * letting the slot place itself from its real inventory index.
	 *
	 * Columns is the width of the panel you're filling (clamped to at least 1).
	 *
	 * bPackByFootprint = false gives every entry one cell, so the view is a plain
	 * left-to-right list and a 2x2 item just draws bigger than its cell. True runs the
	 * same first-fit walk the inventory itself uses, so multi-cell items get real spans
	 * and the rows stay flush; that's what you want when the view is meant to look like
	 * a bag rather than a list.
	 *
	 * bShowEmptySlots pads the result after packing: once every entry has a placement, it
	 * keeps handing out placements for the source inventory's genuinely empty slots —
	 * ItemID invalid, not merely absent from this (possibly filtered) view — each carrying
	 * that real slot's index in SourceSlotIndex so it can be fed into InitSlot exactly like
	 * a real entry. A filtered-out slot that actually holds an item never becomes a filler:
	 * that would leak an item the filter was asked to hide. The result is the same total
	 * slot count the regular inventory grid shows only when nothing is filtered; a search
	 * or fragment filter legitimately shrinks it, since a hidden item's slot is neither
	 * packed content nor a true empty.
	 */
	UFUNCTION(BlueprintPure, Category = "Inventory|SubInventory")
	static void GetSubInventoryLayout(const FSubInventoryView& View, int32 Columns, bool bPackByFootprint,
	                                  bool bShowEmptySlots, TArray<FSubInventoryPlacement>& OutPlacements);

	/**
	 * One entry's position, for widgets that place themselves inside a ForEach. Only
	 * resolves real entries (ViewIndex indexes View.Entries) — the empty filler cells
	 * GetSubInventoryLayout's bShowEmptySlots adds have no entry behind them, so there is
	 * nothing for this function to look up for those; it has no bShowEmptySlots pin.
	 */
	UFUNCTION(BlueprintPure, Category = "Inventory|SubInventory", meta = (ReturnDisplayName = "Found"))
	static bool GetSubInventoryPlacement(const FSubInventoryView& View, int32 ViewIndex, int32 Columns,
	                                     bool bPackByFootprint, FSubInventoryPlacement& OutPlacement);

	/** Re-run the query a view was built from (bind this to OnInventoryChanged). */
	UFUNCTION(BlueprintPure, Category = "Inventory|SubInventory", meta = (ReturnDisplayName = "Has Items"))
	bool RefreshSubInventoryView(const FSubInventoryView& View, FSubInventoryView& OutView) const;

	UFUNCTION(BlueprintPure, Category = "Inventory|SubInventory", meta = (ReturnDisplayName = "Found"))
	bool GetSubInventoryEntry(const FSubInventoryView& View, int32 ViewIndex, FSubInventoryEntry& OutEntry) const;

	/**
	 * Would this drag be legal? False for anything that stays inside the view's own
	 * container — that's the disabled intra-inventory case — and otherwise the same
	 * question IsValidSlotSwap answers, asked with the entry's real slot index.
	 *
	 * bDraggingOutOfView: true when the item is being taken FROM the view and dropped on
	 * OtherInventory[OtherIndex]; false when something is being dropped INTO the view
	 * on top of the entry at ViewIndex.
	 */
	UFUNCTION(BlueprintPure, Category = "Inventory|SubInventory", meta = (ReturnDisplayName = "IsValid"))
	bool IsValidSubInventorySwap(const FSubInventoryView& View, int32 ViewIndex,
	                             UInventoryComponent* OtherInventory, int32 OtherIndex,
	                             bool bDraggingOutOfView = true) const;

	/** PreviewSlotSwap for a view entry — cells are reported in real slot indices. */
	UFUNCTION(BlueprintPure, Category = "Inventory|SubInventory", meta = (ReturnDisplayName = "IsValid"))
	bool PreviewSubInventorySwap(const FSubInventoryView& View, int32 ViewIndex,
	                             UInventoryComponent* OtherInventory, int32 OtherIndex, bool bDraggingOutOfView,
	                             TArray<int32>& OutDraggedCells, TArray<bool>& OutDraggedCellValid,
	                             TArray<int32>& OutDisplacedCells) const;

	/** Perform the drag validated above. Silently refuses a same-container move. */
	UFUNCTION(BlueprintCallable, Category = "Inventory|SubInventory")
	void RequestSubInventorySwap(const FSubInventoryView& View, int32 ViewIndex,
	                             UInventoryComponent* OtherInventory, int32 OtherIndex,
	                             bool bDraggingOutOfView = true);

	/**
	 * Drop onto the view's empty space rather than onto one of its items: move
	 * SourceInventory[SourceIndex] into the view's container, into the first slot that
	 * will take it. Still cross-container only.
	 */
	UFUNCTION(BlueprintCallable, Category = "Inventory|SubInventory")
	void RequestSubInventoryAdd(const FSubInventoryView& View, UInventoryComponent* SourceInventory, int32 SourceIndex);
	
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

	/** Sorting RPC. Chests have no owning connection, so this carries the target
	    component the same way Server_SlotSwap does. */
	UFUNCTION(Server, Reliable)
	void Server_SortInventory(UInventoryComponent* Inventory, const TArray<FInventorySortRule>& Rules);

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
	// Grid / footprint helpers (multi-cell item placement)
	//
	// These are what make FItemDefinition::SlotSize do anything. On a non-grid container
	// (GridWidth or GridHeight == 0) they all degrade to the old single-cell behavior —
	// SlotSize is simply ignored there, exactly as before this feature existed.
	//
	// They are also where SLOT footprints (FSlotOverrideData::SlotFootprint) are honoured.
	// Each primitive below runs the Size it was handed through GetEffectiveFootprintAt
	// first, so callers keep passing the item's authored size and get slot-aware behaviour
	// for free — that expansion happening in one place, at the bottom, is the reason a
	// 3x1 slot works identically for pickup, drop, swap, sort and default-fill without any
	// of those paths knowing slot footprints exist.
	// ---------------------------------------------------------------------------------

	/** Cell coordinate <-> flat SlotIndex (row-major). Safe even when GridWidth is 0. */
	FIntPoint IndexToXY(int32 Index) const;
	int32 XYToIndex(int32 X, int32 Y) const;

	/** The row's authored footprint, or (1,1) when unresolved or this container isn't grid-enabled. */
	FIntPoint GetItemFootprintSize(FName RowName) const;

	/** Does the WxH footprint anchored at AnchorIndex fit entirely in-bounds and empty? */
	bool CanPlaceFootprintAt(int32 AnchorIndex, FIntPoint Size) const;

	/**
	 * As above, but cells currently occupied by IgnoreItemID also count as available.
	 * Used during a swap so an item's own (about-to-be-vacated) cells don't block its
	 * own new placement.
	 */
	bool CanPlaceFootprintAtIgnoringItem(int32 AnchorIndex, FIntPoint Size, FGuid IgnoreItemID) const;

	/**
	 * Write ItemID into every cell of the footprint anchored at AnchorIndex; only
	 * AnchorIndex itself gets bIsAnchor = true. Falls back to a single-cell write on
	 * non-grid containers or genuinely 1x1 items.
	 */
	void OccupyFootprint(int32 AnchorIndex, FIntPoint Size, FGuid ItemID);

	/** Clear every cell (anchor + shadow cells) currently holding ItemID. */
	void ClearFootprintForItem(FGuid ItemID);

	/** Every Slots index currently holding ItemID (anchor + shadow cells). */
	TArray<int32> GetFootprintCellsForItem(FGuid ItemID) const;

	/** First anchor index where Size fits; false (OutAnchorIndex = INDEX_NONE) if nowhere fits. */
	bool FindFirstFitForSize(FIntPoint Size, int32& OutAnchorIndex) const;

	/** OnInventoryChanged for every cell ItemID currently occupies. */
	void BroadcastFootprintForItem(FGuid ItemID);

	// ---------------------------------------------------------------------------------
	// Multi-item swap planning
	//
	// These separate "is this move legal, and what does it actually do" (BuildSwapPlan)
	// from carrying it out (SwapSlots). See BuildSwapPlan for the confine rules that
	// govern which swaps are legal and where displaced items are allowed to land.
	// ---------------------------------------------------------------------------------

	/** The anchor index of the item currently occupying ItemID, or INDEX_NONE. */
	int32 FindAnchorIndexForItem(FGuid ItemID) const;

	/** Does the WxH footprint anchored at AnchorIndex sit entirely within grid bounds? */
	bool IsFootprintInBounds(int32 AnchorIndex, FIntPoint Size) const;

	/** Every distinct item (other than IgnoreItemID) with at least one cell inside the
		given footprint. */
	TArray<FGuid> GetItemsOverlappingFootprint(int32 AnchorIndex, FIntPoint Size, FGuid IgnoreItemID) const;

	/** True iff every cell ItemID occupies falls inside the given footprint rectangle. */
	bool IsItemFullyWithinFootprint(FGuid ItemID, int32 AnchorIndex, FIntPoint Size) const;

	// ---------------------------------------------------------------------------------
	// Confine-box geometry (see FInventoryGridRect)
	// ---------------------------------------------------------------------------------

	/** The whole container as a rect. Non-grid containers report a single row of Slots.Num(). */
	FInventoryGridRect GetGridBoundsRect() const;

	/** The rect a WxH footprint anchored at AnchorIndex covers. Invalid for a bad index/size. */
	FInventoryGridRect GetFootprintRect(int32 AnchorIndex, FIntPoint Size) const;

	/**
	 * Bounding rect of every cell ItemID actually occupies right now (invalid if it
	 * occupies none). Derived from the live Slots array rather than from the item's
	 * authored SlotSize, so a mis-authored size can never make the confine lie about
	 * where an item really is.
	 */
	FInventoryGridRect GetItemBoundsRect(FGuid ItemID) const;

	/** Footprint size of a stored item, resolved through its definition. (1,1) if unknown. */
	FIntPoint GetItemSizeById(FGuid ItemID) const;

	/**
	 * Does EVERY cell of the footprint accept this definition's slot types? The old code
	 * only tested the anchor cell, which let a multi-cell item hang over slots it isn't
	 * allowed in on containers that use SlotTypeOverrides.
	 */
	bool IsFootprintSlotTypeCompatible(int32 AnchorIndex, FIntPoint Size, const FItemDefinition& Def) const;

	/** CanPlaceFootprintAt, but any number of items count as "not in the way". */
	bool CanPlaceFootprintIgnoring(int32 AnchorIndex, FIntPoint Size, const TArray<FGuid>& IgnoreItemIDs) const;

	/**
	 * Does a region line up with slot boundaries? Either the anchor is an oversized slot
	 * and the region is exactly that slot, or every cell in the region is a plain 1x1
	 * slot. What this rules out is the half-overlap: a 2x1 item covering one cell of
	 * someone else's 3x1 weapon slot, which would leave that slot permanently unusable
	 * with no way for the player to see why.
	 */
	bool DoesRegionRespectSlotBoundaries(int32 AnchorIndex, FIntPoint Size) const;

	/**
	 * The best anchor for a Size footprint inside Confine: of every position whose cells
	 * all pass IsCellAvailable and whose slot types all accept Def, the one closest to
	 * IdealAnchorXY (an exact match wins outright and returns immediately). False when
	 * the confine has no room at all — which is a rejection, never a licence to look
	 * outside the confine.
	 *
	 * OutSize reports the region actually claimed, which is NOT Size when the winning
	 * anchor turned out to be an oversized slot — the caller needs that to reserve the
	 * right cells and to write a plan that matches what OccupyFootprint will do.
	 */
	bool FindBestAnchorInRect(FIntPoint Size, const FItemDefinition& Def, const FInventoryGridRect& Confine,
							  FIntPoint IdealAnchorXY, TFunctionRef<bool(int32)> IsCellAvailable,
							  int32& OutAnchorIndex, FIntPoint& OutSize) const;

	/**
	 * Validates and plans moving the item found at SourceIndex (in SourceInv, or this
	 * component if null) onto DestIndex of this component, including every item that move
	 * displaces. Pure/read-only — mutates nothing. Returns false (OutPlan.bValid = false)
	 * if the move is illegal for any reason.
	 *
	 * THE RULES, in the order they are applied:
	 *
	 *  1. The dragged item lands EXACTLY where it was dropped. The drop position is the
	 *     player's intent; nothing below is allowed to nudge it.
	 *  2. Its footprint must be in bounds and every cell it covers must accept its slot
	 *     type, or the swap is rejected.
	 *  3. The CONFINE is the smallest rect enclosing: the dragged item's current
	 *     footprint, its footprint at the drop position, and the footprint of the item it
	 *     was dropped onto (the swap partner, which may be LARGER than the dragged item —
	 *     that is the "drop a 1x1 onto a 2x2 and have them trade places" case).
	 *  4. Any OTHER item caught under the drop footprint must sit entirely inside the
	 *     confine. One cell poking out and the whole swap is rejected, rather than
	 *     shoving a bystander somewhere the player never asked for.
	 *  5. Every displaced item is then re-placed INSIDE the confine, as close as possible
	 *     to the position it would take if it simply slid into the space the dragged item
	 *     vacated (for a straight one-for-one swap that is exactly the dragged item's old
	 *     anchor, which is why a clean swap reads as two items trading places). Cells
	 *     freed by the swap itself count as free; the dragged item's destination cells
	 *     never do.
	 *  6. If any displaced item has no legal position inside the confine, the swap is
	 *     rejected. Items are NEVER relocated outside the confine to make a move fit.
	 *
	 * Cross-container drops have no shared coordinate space, so there the confine is the
	 * source-side equivalent: the rect enclosing the cells the dragged item vacates plus
	 * the positions the displaced items would slide into, clamped to the container.
	 */
	bool BuildSwapPlan(UInventoryComponent* SourceInv, int32 SourceIndex, int32 DestIndex, FInventorySwapPlan& OutPlan) const;
	
	/**
	 * The strict drop: the dragged item lands exactly at DropAnchorIndex, and everything
	 * it displaces is re-placed inside the confine. This is BuildSwapPlan's first choice
	 * and the only one that honours the drop position exactly.
	 */
	bool TryPlanDropAt(const UInventoryComponent* Src, int32 SrcAnchorIndex, const FItemInstance& DraggedItem,
					   const FItemDefinition& DraggedDef, FIntPoint DragSize, int32 DropAnchorIndex,
					   FInventorySwapPlan& OutPlan) const;

	/**
	 * The fallback when a strict drop can't be satisfied: two items trade places inside
	 * the confine. Needed because dropping a SMALL item onto a LARGE one often leaves the
	 * large item nowhere contiguous to go if the small one is pinned to the exact drop
	 * cell — dropping a 2x1 onto the top row of a 2x2 in a 2x3 confine, say. The dragged
	 * item is allowed to settle at the nearest position to the drop that lets the pair
	 * swap, and must still land overlapping the item it was dropped on.
	 */
	bool TryPlanTrade(const UInventoryComponent* Src, int32 SrcAnchorIndex, const FItemInstance& DraggedItem,
					  const FItemDefinition& DraggedDef, FIntPoint DragSize, int32 DropAnchorIndex,
					  FInventorySwapPlan& OutPlan) const;

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
	static bool GetItemStructForRow(FName RowName, FItemDefinition& OutStruct);
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
	
	// ---------------------------------------------------------------------------------
	// Sorting internals
	// ---------------------------------------------------------------------------------

	/**
	 * The rules of the most recent sort, and therefore the stack a layered sort adds to.
	 * Deliberately NOT replicated: it's a UI-level memory of "what did I ask for", kept
	 * in step on both ends because the Request* wrappers record it locally and send the
	 * whole resolved chain to the server rather than asking the server to remember.
	 */
	UPROPERTY(Transient)
	TArray<FInventorySortRule> ActiveSortRules;

	/** Re-entrancy guard: a sort broadcasts slot changes, and a listener must not sort. */
	bool bIsSorting = false;

	/** Every anchored, slotted item, with each rule's key resolved once. */
	void CollectSortableItems(const TArray<FInventorySortRule>& Rules, TArray<FInventorySortItem>& OutItems) const;

	/**
	 * One item's sort record. Split out of CollectSortableItems so a sub-inventory view
	 * can resolve keys for the handful of items it shows and then reuse CompareSortItems,
	 * rather than carrying a second comparator that drifts from this one.
	 */
	void BuildSortItem(const FItemInstance& Instance, const TArray<FInventorySortRule>& Rules,
	                   FInventorySortItem& OutItem) const;

	/** Rule-by-rule comparison, then the fixed tiebreak. A strict weak ordering. */
	static bool CompareSortItems(const FInventorySortItem& A, const FInventorySortItem& B,
	                             const TArray<FInventorySortRule>& Rules);

	/**
	 * FragmentTypeSortOrder resolved for one item: OutRank is that item's best (earliest
	 * -added) category, INDEX_NONE if it matches none. When it does match, OutValue is
	 * the category's own ValueName read off that same fragment (bOutHasValue false if
	 * the fragment has no such value) — the intra-category tiebreak.
	 */
	bool ResolveFragmentTypeRank(const FItemInstance& Instance, int32& OutRank, double& OutValue,
	                             bool& bOutHasValue) const;

	/**
	 * Re-place every item, in the given order, into the first position that fits it
	 * (row-major, slot-type checked). All-or-nothing: if any item can't be placed the
	 * previous layout is restored untouched, because a sort that half-completed would
	 * leave items in the store with no slot pointing at them.
	 */
	bool RebuildLayoutFromOrder(const TArray<FGuid>& OrderedItemIDs);

	/** ActiveSortRules + NewRule (replacing a matching layer rather than stacking it). */
	TArray<FInventorySortRule> BuildLayeredRules(const FInventorySortRule& NewRule, bool bLayerOnExistingSort) const;

	/** Auto sort hook. Called after adds/removes; no-op unless AutoSort is on. */
	void TryAutoSort();

	UInventoryComponent* GetRoutingComponent();
};