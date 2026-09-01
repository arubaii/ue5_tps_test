#include "InventoryAudioTags.h"

namespace InventoryAudioTags
{
	UE_DEFINE_GAMEPLAY_TAG(SlotSwap,            "Audio.Inventory.SlotSwap");
	UE_DEFINE_GAMEPLAY_TAG(CrossInventorySwap,  "Audio.Inventory.CrossInventorySwap");

	UE_DEFINE_GAMEPLAY_TAG(ValidSlotSwap,       "Audio.Inventory.ValidSlotSwap");
	UE_DEFINE_GAMEPLAY_TAG(InvalidSlotSwap,     "Audio.Inventory.InvalidSlotSwap");

	UE_DEFINE_GAMEPLAY_TAG(Merge,               "Audio.Inventory.Merge");
	UE_DEFINE_GAMEPLAY_TAG(MergeAll,            "Audio.Inventory.MergeAll");

	UE_DEFINE_GAMEPLAY_TAG(Equip,               "Audio.Inventory.Equip");
	UE_DEFINE_GAMEPLAY_TAG(EquipArmor,          "Audio.Inventory.EquipArmor");
}