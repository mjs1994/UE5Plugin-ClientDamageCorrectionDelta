// Copyright 2026, HRZN Games.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DamageBoneMapDataAsset.h"
#include "DamageCorrectionDeltaSubsystem.generated.h"

// Client-reported bone waiting for the server hit to arrive.
USTRUCT()
struct FPendingClientBone
{
	GENERATED_BODY()

	FName BoneName;
	TWeakObjectPtr<AController> Instigator;
	float ReportedTime = 0.f;
};

// Server hit that was applied before the client bone RPC arrived.
// Kept so ReportClientHit can compute and apply the delta.
USTRUCT()
struct FAppliedServerHit
{
	GENERATED_BODY()

	FName ServerBoneName;
	float BaseDamage    = 0.f;
	float AppliedDamage = 0.f;
	TWeakObjectPtr<AController> Instigator;
	FHitResult HitResult;
	TObjectPtr<UDamageBoneMapDataAsset> DamageMap;
	TSubclassOf<UDamageType> DamageTypeClass;
	float AppliedTime = 0.f;
};

UCLASS()
class CLIENTDAMAGECORRECTIONDELTA_API UDamageCorrectionDeltaSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Call this on the server (via a Server RPC in your projectile Blueprint) when the
	// client's predicted projectile hits something. Stores the bone for use by ApplyHitDamage,
	// or applies the delta immediately if the server hit has already been processed.
	UFUNCTION(BlueprintCallable, Category = "Damage Correction")
	void ReportClientHit(AActor* HitActor, FName BoneName, AController* InstigatorController);

	// Call this server-side on the authoritative projectile's OnHit instead of ApplyPointDamage.
	// Handles deduplication, picks the best bone (client-reported if already received, else server),
	// computes final damage via DamageMap, and calls ApplyPointDamage internally.
	// If no client bone has arrived yet it applies server-bone damage and buffers the record
	// so ReportClientHit can apply the delta when the RPC arrives.
	UFUNCTION(BlueprintCallable, Category = "Damage Correction")
	void ApplyHitDamage(AActor* HitActor, FName ServerBoneName, float BaseDamage,
	                    AController* InstigatorController, const FHitResult& HitResult,
	                    UDamageBoneMapDataAsset* DamageMap, TSubclassOf<UDamageType> DamageTypeClass);

	// Minimum seconds between damage applications on the same actor.
	// Guards against bullet entry/exit wound double-hits.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Correction|Settings",
	          meta = (ClampMin = "0.0", ClampMax = "1.0", ForceUnits = "s"))
	float DamageIgnoreDelay = 0.05f;

	// Seconds before an unmatched pending record is discarded.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Correction|Settings",
	          meta = (ClampMin = "1.0", ForceUnits = "s"))
	float RecordTTL = 5.0f;

private:
	TMap<TWeakObjectPtr<AActor>, FPendingClientBone> PendingClientBones;
	TMap<TWeakObjectPtr<AActor>, FAppliedServerHit>  AppliedServerHits;
	TMap<TWeakObjectPtr<AActor>, float>              LastHitTime;
	FTimerHandle CleanupTimerHandle;

	void PurgeStaleRecords();
};
