#include "PhysicalMaterial_Gameplay.h"

UPhysicalMaterial_Gameplay* UPhysicalMaterial_Gameplay::Resolve(UPhysicalMaterial* PhysMaterial)
{
	return Cast<UPhysicalMaterial_Gameplay>(PhysMaterial);
}

