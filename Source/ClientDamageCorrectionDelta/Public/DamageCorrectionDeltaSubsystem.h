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
	// client's predicted projectile hits something.
	//
	// ServerAuthoritative: stores the bone for use by ApplyHitDamage, or applies the
	// delta immediately if the server hit has already been processed.
	// HitResult/BaseDamage/DamageMap/DamageTypeClass are unused in this mode.
	//
	// ClientAuthoritative: applies damage directly from the client data.
	// ClientWithValidation: validates distance and optionally line-of-sight before applying.
	// Both non-server modes require HitResult, BaseDamage, DamageMap, and DamageTypeClass.
	UFUNCTION(BlueprintCallable, Category = "Damage Correction")
	void ReportClientHit(AActor* HitActor, FName BoneName, AController* InstigatorController, const FHitResult& HitResult, float BaseDamage, TSubclassOf<UDamageType> DamageTypeClass);

	// Call this server-side on the authoritative projectile's OnHit instead of ApplyPointDamage.
	// No-op when ValidationMode is not ServerAuthoritative.
	UFUNCTION(BlueprintCallable, Category = "Damage Correction")
	void ApplyHitDamage(AActor* HitActor, FName ServerBoneName, float BaseDamage, AController* InstigatorController, const FHitResult& HitResult, TSubclassOf<UDamageType> DamageTypeClass);

	// Mirrors DamageCorrectionSettings::ValidationMode at runtime.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Correction|Settings")
	EDamageCorrectionValidationMode ValidationMode = EDamageCorrectionValidationMode::ServerAuthoritative;

	// Mirrors DamageCorrectionSettings::bValidateDistance at runtime.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Correction|Settings")
	bool bValidateDistance = true;

	// Mirrors DamageCorrectionSettings::MaxValidationRange at runtime.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Correction|Settings",
	          meta = (ClampMin = "1.0", ForceUnits = "cm"))
	float MaxValidationRange = 150000.f;

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
	void ApplyClientDamage(AActor* HitActor, FName BoneName, AController* InstigatorController, const FHitResult& HitResult, float BaseDamage, TSubclassOf<UDamageType> DamageTypeClass, float Now);

	// Queries IDamageBoneMapInterface on HitActor. Returns nullptr if unimplemented (damage
	// falls back to BaseDamage unchanged via DefaultMultiplier = 1.0 in CalculateDamage).
	UDamageBoneMapDataAsset* ResolveDamageMap(AActor* HitActor) const;

	void PurgeStaleRecords();
};
