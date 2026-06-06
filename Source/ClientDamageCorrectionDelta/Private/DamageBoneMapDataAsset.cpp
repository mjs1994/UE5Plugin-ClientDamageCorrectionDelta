// Copyright 2026, HRZN Games.

#include "DamageBoneMapDataAsset.h"

/**
 * Return the damage multiplier for the targeted bone.
 */
float UDamageBoneMapDataAsset::GetMultiplierForBone(FName BoneName) const
{
	for (const FBoneDamageEntry& Entry : BoneMultipliers)
	{
		if (Entry.BoneName == BoneName)
		{
			return Entry.Multiplier;
		}
	}
	
	return DefaultMultiplier;
}

/**
 * Calculate the damage based on the bone name via the damage multiplier.
 */
float UDamageBoneMapDataAsset::CalculateDamage(float BaseDamage, FName BoneName) const
{
	return BaseDamage * GetMultiplierForBone(BoneName);
}
