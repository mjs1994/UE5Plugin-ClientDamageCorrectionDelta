// Copyright 2026, HRZN Games.

#include "DamageCorrectionDeltaSubsystem.h"
#include "DamageCorrectionSettings.h"
#include "ClientDamageCorrectionDelta.h"
#include "DamageBoneMapInterface.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Pawn.h"
#include "Components/CapsuleComponent.h"

// Snapshots settings from the CDO rather than reading live each call.
// Values are still overridable at runtime via the public UPROPERTY mirrors
// on the subsystem itself (e.g. from Blueprint or test code).
void UDamageCorrectionDeltaSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (const UDamageCorrectionSettings* Settings = GetDefault<UDamageCorrectionSettings>())
	{
		ValidationMode       = Settings->ValidationMode;
		bValidateDistance    = Settings->bValidateDistance;
		MaxValidationRange   = Settings->MaxValidationRange;
		bValidateLineOfSight = Settings->bValidateLineOfSight;
		DamageIgnoreDelay    = Settings->DamageIgnoreDelay;
		RecordTTL            = Settings->RecordTTL;
	}

	GetWorld()->GetTimerManager().SetTimer(CleanupTimerHandle, this, &UDamageCorrectionDeltaSubsystem::PurgeStaleRecords, RecordTTL, true);
}

// Maps are cleared before Super so any in-flight timer tick during teardown
// finds empty containers rather than dangling actor pointers.
void UDamageCorrectionDeltaSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CleanupTimerHandle);
	}

	PendingClientBones.Empty();
	AppliedServerHits.Empty();
	LastHitTime.Empty();

	Super::Deinitialize();
}

// Queries IDamageBoneMapInterface on HitActor. Returns nullptr if unimplemented.
UDamageBoneMapDataAsset* UDamageCorrectionDeltaSubsystem::ResolveDamageMap(AActor* HitActor) const
{
	if (HitActor && HitActor->Implements<UDamageBoneMapInterface>())
	{
		return IDamageBoneMapInterface::Execute_GetDamageBoneMap(HitActor);
	}

	return nullptr;
}

// Entry point for the client bone RPC. Behaviour is mode-dependent:
//
//   ClientAuthoritative    - damage applied immediately, no server involvement needed.
//   ClientWithValidation   - distance and optional LoS checks run first; hit is
//                            discarded silently on failure so exploits don't surface
//                            as feedback to the client.
//   ServerAuthoritative    - bone is queued or paired with a buffered server hit.
//                            The switch falls to default+break so the existing
//                            delta logic below runs without duplication.
void UDamageCorrectionDeltaSubsystem::ReportClientHit(AController* InstigatorController, const FHitResult& HitResult, float BaseDamage, TSubclassOf<UDamageType> DamageTypeClass)
{
	AActor* HitActor = HitResult.GetActor();
	const FName BoneName = HitResult.BoneName;

	if (!HitActor || BoneName.IsNone())
	{
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();

	UE_LOG(LogClientDamageCorrectionDelta, Warning, TEXT("[CDD] [t=%.4f] CLIENT HIT reported: actor=%s bone=%s mode=%d"), Now, *GetNameSafe(HitActor), *BoneName.ToString(), (int32)ValidationMode);

	switch (ValidationMode)
	{
		case EDamageCorrectionValidationMode::ClientAuthoritative:
			ApplyClientDamage(HitActor, BoneName, InstigatorController, HitResult, BaseDamage, DamageTypeClass, Now);
			return;

		case EDamageCorrectionValidationMode::ClientWithValidation:
		{
			APawn* InstigatorPawn = InstigatorController ? InstigatorController->GetPawn() : nullptr;
			if (!InstigatorPawn)
			{
				UE_LOG(LogClientDamageCorrectionDelta, Warning, TEXT("[CDD] [t=%.4f] REJECTED (no instigator pawn): actor=%s bone=%s"), Now, *GetNameSafe(HitActor), *BoneName.ToString());
				return;
			}

			const FVector EyeLocation = InstigatorPawn->GetPawnViewLocation();
			const FVector ImpactPoint = HitResult.ImpactPoint.IsZero() ? HitActor->GetActorLocation() : FVector(HitResult.ImpactPoint);
			const float   Distance    = FVector::Dist(EyeLocation, ImpactPoint);

			if (bValidateDistance && Distance > MaxValidationRange)
			{
				UE_LOG(LogClientDamageCorrectionDelta, Warning, TEXT("[CDD] [t=%.4f] REJECTED (out of range): actor=%s bone=%s dist=%.0fcm max=%.0fcm"), Now, *GetNameSafe(HitActor), *BoneName.ToString(), Distance, MaxValidationRange);
				return;
			}

			if (bValidateLineOfSight)
			{
				// Use the capsule center as the LoS target: skeletal mesh pose is not
				// guaranteed to match the client's, but the capsule position is always
				// authoritative. Falls back to actor origin if no capsule is present.
				FVector LoSTarget = HitActor->GetActorLocation();
				if (UCapsuleComponent* Capsule = HitActor->FindComponentByClass<UCapsuleComponent>())
				{
					LoSTarget = Capsule->GetComponentLocation();
				}

				FHitResult TraceResult;
				FCollisionQueryParams QueryParams(FName(TEXT("CDDValidation")), false);
				QueryParams.AddIgnoredActor(InstigatorPawn);
				QueryParams.AddIgnoredActor(HitActor);

				const bool bBlocked = GetWorld()->LineTraceSingleByChannel(TraceResult, EyeLocation, LoSTarget, ECC_Visibility, QueryParams);

				if (bBlocked)
				{
					UE_LOG(LogClientDamageCorrectionDelta, Warning, TEXT("[CDD] [t=%.4f] REJECTED (no line of sight, blocked by %s): actor=%s bone=%s"), Now, *GetNameSafe(TraceResult.GetActor()), *GetNameSafe(HitActor), *BoneName.ToString());
					return;
				}
			}

			UE_LOG(LogClientDamageCorrectionDelta, Warning, TEXT("[CDD] [t=%.4f] VALIDATED: actor=%s bone=%s dist=%.0fcm"), Now, *GetNameSafe(HitActor), *BoneName.ToString(), Distance);

			ApplyClientDamage(HitActor, BoneName, InstigatorController, HitResult, BaseDamage, DamageTypeClass, Now);
			return;
		}

		default: // ServerAuthoritative - fall through to delta logic below.
			break;
	}

	// Server already applied damage: consume the oldest buffered record (FIFO) and top up
	// with the delta between what the server bone earned and what the client bone earns.
	if (TArray<FAppliedServerHit>* Queue = AppliedServerHits.Find(HitActor))
	{
		FAppliedServerHit ServerHit = (*Queue)[0];
		Queue->RemoveAt(0);

		if (Queue->IsEmpty())
		{
			AppliedServerHits.Remove(HitActor);
		}

		const float ClientDamage = ServerHit.DamageMap ? ServerHit.DamageMap->CalculateDamage(ServerHit.BaseDamage, BoneName) : ServerHit.BaseDamage;
		const float Delta = FMath::Max(0.f, ClientDamage - ServerHit.AppliedDamage);

		UE_LOG(LogClientDamageCorrectionDelta, Warning, TEXT("[CDD] [t=%.4f] LATE CLIENT: actor=%s server_bone=%s client_bone=%s server_dmg=%.1f client_dmg=%.1f delta=%.1f"), Now, *GetNameSafe(HitActor), *ServerHit.ServerBoneName.ToString(), *BoneName.ToString(), ServerHit.AppliedDamage, ClientDamage, Delta);

		if (Delta > 0.f)
		{
			AController* BestInstigator = InstigatorController ? InstigatorController : ServerHit.Instigator.Get();
			APawn* DamageCauser = BestInstigator ? BestInstigator->GetPawn() : nullptr;
			const FVector HitFromDirection = (!ServerHit.HitResult.TraceStart.IsZero()) ? (ServerHit.HitResult.ImpactPoint - ServerHit.HitResult.TraceStart).GetSafeNormal() : -ServerHit.HitResult.ImpactNormal;

			UGameplayStatics::ApplyPointDamage(HitActor, Delta, HitFromDirection, ServerHit.HitResult, BestInstigator, DamageCauser, ServerHit.DamageTypeClass);

			UE_LOG(LogClientDamageCorrectionDelta, Warning, TEXT("[CDD] [t=%.4f] DELTA APPLIED: actor=%s +%.1f"), Now, *GetNameSafe(HitActor), Delta);
		}

		return;
	}

	// Client RPC arrived before the server hit - park it so ApplyHitDamage can pair
	// it in arrival order. Multiple queued entries can build up if the server is slow
	// or misses shots; FIFO ensures each server hit consumes the right client report.
	FPendingClientBone Report;
	Report.BoneName     = BoneName;
	Report.Instigator   = InstigatorController;
	Report.ReportedTime = Now;
	PendingClientBones.FindOrAdd(HitActor).Add(MoveTemp(Report));
}

// In non-ServerAuthoritative modes the call is forwarded to ReportClientHit so callers
// don't need to branch on mode. The dedup guard uses a flat per-actor timestamp rather
// than per-instigator tracking, which is intentional: two clients hitting the same actor
// within DamageIgnoreDelay is treated as one event. Key LastHitTime on (actor, instigator)
// pairs if you need per-instigator dedup.
void UDamageCorrectionDeltaSubsystem::ApplyHitDamage(float BaseDamage, AController* InstigatorController, const FHitResult& HitResult, TSubclassOf<UDamageType> DamageTypeClass)
{
	AActor* HitActor          = HitResult.GetActor();
	const FName ServerBoneName = HitResult.BoneName;

	if (!HitActor)
	{
		return;
	}

	if (ValidationMode != EDamageCorrectionValidationMode::ServerAuthoritative)
	{
		ReportClientHit(InstigatorController, HitResult, BaseDamage, DamageTypeClass);
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();

	if (const float* LastTime = LastHitTime.Find(HitActor))
	{
		if ((Now - *LastTime) < DamageIgnoreDelay)
		{
			UE_LOG(LogClientDamageCorrectionDelta, Warning, TEXT("[CDD] [t=%.4f] SUPPRESSED: actor=%s server_bone=%s - hit again after %.4fs (threshold %.4fs)"), Now, *GetNameSafe(HitActor), *ServerBoneName.ToString(), Now - *LastTime, DamageIgnoreDelay);
			return;
		}
	}

	LastHitTime.Add(HitActor, Now);

	UDamageBoneMapDataAsset* ResolvedMap  = ResolveDamageMap(HitActor);
	const FVector HitFromDirection        = (!HitResult.TraceStart.IsZero()) ? (HitResult.ImpactPoint - HitResult.TraceStart).GetSafeNormal() : -HitResult.ImpactNormal;

	if (TArray<FPendingClientBone>* Queue = PendingClientBones.Find(HitActor))
	{
		FPendingClientBone ClientReport = (*Queue)[0];
		Queue->RemoveAt(0);
		if (Queue->IsEmpty())
		{
			PendingClientBones.Remove(HitActor);
		}

		const FName BestBone        = ClientReport.BoneName;
		AController* BestInstigator = ClientReport.Instigator.IsValid() ? ClientReport.Instigator.Get() : InstigatorController;
		APawn* DamageCauser         = BestInstigator ? BestInstigator->GetPawn() : nullptr;
		const float FinalDamage     = ResolvedMap ? ResolvedMap->CalculateDamage(BaseDamage, BestBone) : BaseDamage;

		UE_LOG(LogClientDamageCorrectionDelta, Warning, TEXT("[CDD] [t=%.4f] SERVER HIT (client-first): actor=%s server_bone=%s client_bone=%s final_dmg=%.1f"), Now, *GetNameSafe(HitActor), *ServerBoneName.ToString(), *BestBone.ToString(), FinalDamage);

		UGameplayStatics::ApplyPointDamage(HitActor, FinalDamage, HitFromDirection, HitResult, BestInstigator, DamageCauser, DamageTypeClass);
	}
	else
	{
		const float FinalDamage = ResolvedMap ? ResolvedMap->CalculateDamage(BaseDamage, ServerBoneName) : BaseDamage;
		APawn* DamageCauser     = InstigatorController ? InstigatorController->GetPawn() : nullptr;

		UE_LOG(LogClientDamageCorrectionDelta, Warning, TEXT("[CDD] [t=%.4f] SERVER HIT (server-first, awaiting client): actor=%s server_bone=%s dmg=%.1f"), Now, *GetNameSafe(HitActor), *ServerBoneName.ToString(), FinalDamage);

		UGameplayStatics::ApplyPointDamage(HitActor, FinalDamage, HitFromDirection, HitResult, InstigatorController, DamageCauser, DamageTypeClass);

		// Store the resolved map so the delta calculation in ReportClientHit has the
		// correct asset even when the caller passed nullptr and the interface was queried.
		FAppliedServerHit Record;
		Record.ServerBoneName  = ServerBoneName;
		Record.BaseDamage      = BaseDamage;
		Record.AppliedDamage   = FinalDamage;
		Record.Instigator      = InstigatorController;
		Record.HitResult       = HitResult;
		Record.DamageMap       = ResolvedMap;
		Record.DamageTypeClass = DamageTypeClass;
		Record.AppliedTime     = Now;
		AppliedServerHits.FindOrAdd(HitActor).Add(MoveTemp(Record));
	}
}

// Shared damage application for client-authoritative modes. Now is passed in from
// the caller so the dedup check and log timestamp are consistent with the outer
// function rather than sampling the clock a second time mid-frame.
void UDamageCorrectionDeltaSubsystem::ApplyClientDamage(AActor* HitActor, FName BoneName, AController* InstigatorController, const FHitResult& HitResult, float BaseDamage, TSubclassOf<UDamageType> DamageTypeClass, float Now)
{
	if (const float* LastTime = LastHitTime.Find(HitActor))
	{
		if ((Now - *LastTime) < DamageIgnoreDelay)
		{
			UE_LOG(LogClientDamageCorrectionDelta, Warning, TEXT("[CDD] [t=%.4f] SUPPRESSED (client): actor=%s bone=%s - hit again after %.4fs (threshold %.4fs)"), Now, *GetNameSafe(HitActor), *BoneName.ToString(), Now - *LastTime, DamageIgnoreDelay);
			return;
		}
	}

	LastHitTime.Add(HitActor, Now);

	UDamageBoneMapDataAsset* ResolvedMap  = ResolveDamageMap(HitActor);
	const float FinalDamage               = ResolvedMap ? ResolvedMap->CalculateDamage(BaseDamage, BoneName) : BaseDamage;
	APawn* DamageCauser                   = InstigatorController ? InstigatorController->GetPawn() : nullptr;
	const FVector HitFromDirection        = (!HitResult.TraceStart.IsZero()) ? (HitResult.ImpactPoint - HitResult.TraceStart).GetSafeNormal() : -HitResult.ImpactNormal;

	UE_LOG(LogClientDamageCorrectionDelta, Warning, TEXT("[CDD] [t=%.4f] APPLY (client): actor=%s bone=%s dmg=%.1f"), Now, *GetNameSafe(HitActor), *BoneName.ToString(), FinalDamage);

	UGameplayStatics::ApplyPointDamage(HitActor, FinalDamage, HitFromDirection, HitResult, InstigatorController, DamageCauser, DamageTypeClass);
}

// Runs on a RecordTTL-interval timer. Dead-actor keys are evicted first because
// their weak pointers are already invalid - no point iterating the inner array.
// Stale entries in valid-actor queues are pruned individually so a slow-arriving
// RPC for one shot doesn't evict legitimate pending entries for later shots on
// the same actor.
void UDamageCorrectionDeltaSubsystem::PurgeStaleRecords()
{
	const float Now = GetWorld()->GetTimeSeconds();

	for (auto It = PendingClientBones.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
			continue;
		}

		It.Value().RemoveAll([Now, this](const FPendingClientBone& Entry) { return (Now - Entry.ReportedTime) >= RecordTTL; });

		if (It.Value().IsEmpty())
		{
			It.RemoveCurrent();
		}
	}

	for (auto It = AppliedServerHits.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
			continue;
		}

		It.Value().RemoveAll([Now, this](const FAppliedServerHit& Entry) { return (Now - Entry.AppliedTime) >= RecordTTL; });

		if (It.Value().IsEmpty())
		{
			It.RemoveCurrent();
		}
	}

	for (auto It = LastHitTime.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || (Now - It.Value()) >= RecordTTL)
		{
			It.RemoveCurrent();
		}
	}
}
