// Copyright arubaii. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "InventoryEnums.generated.h"

/**
 * Slot typing used by both inventory slots and item compatibility.
 *
 * A SLOT advertises what it *is* (its ESlotType); an ITEM definition advertises
 * which slot types it *fits* (FItemStructure::CompatibleSlotTypes). Compatibility
 * is "does the item's CompatibleSlotTypes contain this slot's SlotType".
 *
 * Replaces the Blueprint enum E_SlotType. Enumerator order preserved exactly so
 * existing serialized byte values continue to line up after you repoint pins.
 */
UENUM(BlueprintType)
enum class ESlotType : uint8
{
	_,
	Generic,
	Weapon,
	Support,
	Attachment_Grip,
	Attachment_Mag
};

/**
 * High-level item classification.
 *
 * Replaces the Blueprint enum E_ItemType.
 *
 * NOTE: The real E_ItemType enumerator list was never captured during the port.
 * The entries below are a PLACEHOLDER — open your E_ItemType asset, confirm the
 * exact names/order, and correct this before relying on it. If it differs, tell
 * me and I'll regenerate. (Order matters: enum values serialize as bytes.)
 */
UENUM(BlueprintType)
enum class EItemType : uint8
{
	Consumable,
	Equippable,
	Readable,
	EmptyInventorySlot UMETA(DisplayName = "Empty InventorySlot")
};

/**
 * Which logical inventory a container represents.
 * Source asset note: "Only one at a time should exist
 * (player/npc can't have two backpacks)."
 *
 * Replaces the Blueprint enum E_InventoryType. Order preserved exactly.
 */
UENUM(BlueprintType)
enum class EInventoryType : uint8
{
	Backpack,
	Stash,
	Armor,
	Shield,
	Weapon,
	Attachment
};

/**
 * Weapon firing mode.
 * Replaces the Blueprint enum E_FireMode. Order preserved exactly.
 * Display names keep the spacing the Blueprint asset used ("Semi Auto", etc.).
 */
UENUM(BlueprintType)
enum class EFireMode : uint8
{
	SemiAuto UMETA(DisplayName = "Semi Auto"),
	Burst    UMETA(DisplayName = "Burst"),
	FullAuto UMETA(DisplayName = "Full Auto")
};

/**
 * How a modifier's value is applied.
 * Replaces the Blueprint enum E_ModifierValueType. Order preserved exactly.
 */
UENUM(BlueprintType)
enum class EModifierValueType : uint8
{
	None,
	Additive,
	Multiplier
};