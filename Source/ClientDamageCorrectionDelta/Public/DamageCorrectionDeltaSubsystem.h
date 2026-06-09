// Copyright 2026, HRZN Games.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DamageBoneMapDataAsset.h"
#include "DamageCorrectionSettings.h"
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
// Stored to ensure ReportClientHit can apply the delta.
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
	// client's predicted projectile hits something. HitActor and BoneName are inferred
	// from HitResult.GetActor() and HitResult.BoneName.
	//
	// ServerAuthoritative: stores the bone for use by ApplyHitDamage, or applies the
	// delta immediately if the server hit has already been processed.
	//
	// ClientAuthoritative: applies damage directly from the client data.
	// ClientWithValidation: validates distance and optionally line-of-sight before applying.
	UFUNCTION(BlueprintCallable, Category = "Damage Correction")
	void ReportClientHit(AController* InstigatorController, const FHitResult& HitResult, float BaseDamage, TSubclassOf<UDamageType> DamageTypeClass);

	// Call this server-side on the authoritative projectile's OnHit instead of ApplyPointDamage.
	// HitActor and BoneName are inferred from HitResult.GetActor() and HitResult.BoneName.
	// In ServerAuthoritative mode, applies damage and buffers the record for client delta.
	// In all other modes, forwards directly to ReportClientHit so callers never need to branch on mode.
	UFUNCTION(BlueprintCallable, Category = "Damage Correction")
	void ApplyHitDamage(AController* InstigatorController, const FHitResult& HitResult, float BaseDamage, TSubclassOf<UDamageType> DamageTypeClass);

	// Mirrors DamageCorrectionSettings::ValidationMode at runtime.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Correction|Settings")
	EDamageCorrectionValidationMode ValidationMode = EDamageCorrectionValidationMode::ServerAuthoritative;

	// Mirrors DamageCorrectionSettings::bValidateDistance at runtime.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Correction|Settings")
	bool bValidateDistance = true;

	// Mirrors DamageCorrectionSettings::MaxValidationRange at runtime.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Correction|Settings", meta = (ClampMin = "1.0", ForceUnits = "cm"))
	float MaxValidationRange = 150000.0f;

	// Mirrors DamageCorrectionSettings::bValidateLineOfSight at runtime.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Correction|Settings")
	bool bValidateLineOfSight = true;

	// Minimum seconds between damage applications on the same actor.
	// Guards against bullet entry/exit wound double-hits.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Correction|Settings", meta = (ClampMin = "0.0", ClampMax = "1.0", ForceUnits = "s"))
	float DamageIgnoreDelay = 0.05f;

	// Seconds before an unmatched pending record is discarded.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Correction|Settings", meta = (ClampMin = "1.0", ForceUnits = "s"))
	float RecordTTL = 5.0f;

private:
	TMap<TWeakObjectPtr<AActor>, TArray<FPendingClientBone>> PendingClientBones;
	TMap<TWeakObjectPtr<AActor>, TArray<FAppliedServerHit>>  AppliedServerHits;
	TMap<TWeakObjectPtr<AActor>, float>                      LastHitTime;
	FTimerHandle CleanupTimerHandle;

	// Shared damage application used by ClientAuthoritative and ClientWithValidation after
	// any validation has already passed.
	void ApplyClientDamage(AController* InstigatorController, const FHitResult& HitResult, float BaseDamage, TSubclassOf<UDamageType> DamageTypeClass, float Now);

	// Queries IDamageBoneMapInterface on HitActor. Returns nullptr if unimplemented (damage
	// falls back to BaseDamage unchanged via DefaultMultiplier = 1.0 in CalculateDamage).
	UDamageBoneMapDataAsset* ResolveDamageMap(AActor* HitActor) const;

	void PurgeStaleRecords();
};
