// Copyright 2026, HRZN Games.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/DamageType.h"
#include "DamageCorrectionDeltaDamageType.generated.h"

// Damage type used exclusively for the bone-mismatch delta correction.

UCLASS(BlueprintType, Blueprintable)
class CLIENTDAMAGECORRECTIONDELTA_API UDamageCorrectionDeltaDamageType : public UDamageType
{
	GENERATED_BODY()
};
