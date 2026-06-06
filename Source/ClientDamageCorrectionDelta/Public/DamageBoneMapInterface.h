// Copyright 2026, HRZN Games.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "DamageBoneMapInterface.generated.h"

class UDamageBoneMapDataAsset;

UINTERFACE(MinimalAPI, BlueprintType)
class UDamageBoneMapInterface : public UInterface
{
	GENERATED_BODY()
};

// Implement on any Pawn that should provide its own bone damage map to the correction
// subsystem - e.g. zombies, humans, armoured enemies each returning a different DA.
// When the hit actor implements this interface the subsystem can query it directly
// instead of requiring the caller to supply the asset on every damage call.
class CLIENTDAMAGECORRECTIONDELTA_API IDamageBoneMapInterface
{
	GENERATED_BODY()

public:
	// Returns the bone damage map for this pawn. Return nullptr to fall back to the
	// map passed explicitly in the damage call (preserves backward compatibility).
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Damage Correction")
	UDamageBoneMapDataAsset* GetDamageBoneMap() const;
};
