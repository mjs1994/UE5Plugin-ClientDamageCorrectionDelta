// Copyright 2026, HRZN Games.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DamageBoneMapDataAsset.generated.h"

USTRUCT(BlueprintType)
struct FBoneDamageEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage")
	FName BoneName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0.0"))
	float Multiplier = 1.f;
};

UCLASS(BlueprintType)
class CLIENTDAMAGECORRECTIONDELTA_API UDamageBoneMapDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage")
	TArray<FBoneDamageEntry> BoneMultipliers;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0.0"))
	float DefaultMultiplier = 1.0f;

	UFUNCTION(BlueprintPure, Category = "Damage")
	float GetMultiplierForBone(FName BoneName) const;

	UFUNCTION(BlueprintPure, Category = "Damage")
	float CalculateDamage(float BaseDamage, FName BoneName) const;
};
