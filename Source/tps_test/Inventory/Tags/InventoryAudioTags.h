#pragma once

#include "NativeGameplayTags.h"

/**
 * Native tag registration for every audio cue the inventory system fires.
 *
 * Native rather than ini-defined: these are compiled straight into the plugin's module,
 * so they exist the instant the module loads — no project-side DefaultGameplayTags.ini
 * entry to remember, and nothing that can be left out of a packaged build the way a
 * config file could be. A plugin that owns a fixed set of tags it always needs should
 * own their registration too, not depend on the host project having set them up.
 *
 * FGameplayTag::RequestGameplayTag(TEXT("...")) still works against these — a Blueprint
 * "Make Literal GameplayTag" node typing the same string resolves to the identical tag,
 * since both paths look up the same underlying tag table. The constants here just give
 * C++ call sites a typo-proof, autocompleted way to reference them.
 */
namespace InventoryAudioTags
{
	// --- Slot relocation ---
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(SlotSwap);           // Audio.Inventory.SlotSwap
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(CrossInventorySwap); // Audio.Inventory.CrossInventorySwap

	// --- Drag validity feedback. Fire locally (PlayInventoryCue), never Multicast —
	//     this is one player's cursor state, not an event the rest of the session
	//     should hear about. See IsValidSubInventorySwap's call sites in Blueprint. ---
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(ValidSlotSwap);   // Audio.Inventory.ValidSlotSwap
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(InvalidSlotSwap); // Audio.Inventory.InvalidSlotSwap

	// --- Stacking ---
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Merge);    // Audio.Inventory.Merge — one stack folded into another
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(MergeAll); // Audio.Inventory.MergeAll — a full consolidation pass

	// --- Equipping. Declared for whichever component owns that logic; InventoryComponent
	//     itself doesn't call these today. ---
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Equip);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(EquipArmor);
}