// Copyright 2026, HRZN Games.

#include "DamageCorrectionDeltaSubsystem.h"
#include "DamageCorrectionSettings.h"
#include "ClientDamageCorrectionDelta.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Pawn.h"

void UDamageCorrectionDeltaSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (const UDamageCorrectionSettings* Settings = GetDefault<UDamageCorrectionSettings>())
	{
		DamageIgnoreDelay = Settings->DamageIgnoreDelay;
		RecordTTL         = Settings->RecordTTL;
	}

	GetWorld()->GetTimerManager().SetTimer(CleanupTimerHandle, this, &UDamageCorrectionDeltaSubsystem::PurgeStaleRecords, RecordTTL, true);
}

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

void UDamageCorrectionDeltaSubsystem::ReportClientHit(AActor* HitActor, FName BoneName, AController* InstigatorController)
{
	if (!HitActor || BoneName.IsNone())
	{
		return;
	}

	UE_LOG(LogClientDamageCorrectionDelta, Warning, TEXT("[CDD] CLIENT BONE: %s → %s"), *GetNameSafe(HitActor), *BoneName.ToString());

	// If the server already applied damage for this actor, compute and apply the delta now.
	if (FAppliedServerHit* ServerHit = AppliedServerHits.Find(HitActor))
	{
		const float ClientDamage = ServerHit->DamageMap
			? ServerHit->DamageMap->CalculateDamage(ServerHit->BaseDamage, BoneName)
			: ServerHit->BaseDamage;
		const float Delta = FMath::Max(0.f, ClientDamage - ServerHit->AppliedDamage);

		if (ServerHit->ServerBoneName != BoneName)
		{
			UE_LOG(LogClientDamageCorrectionDelta, Warning, TEXT("[CDD] LATE DELTA: server=%s(%.1f) client=%s(%.1f) delta=%.1f on %s"),
				*ServerHit->ServerBoneName.ToString(), ServerHit->AppliedDamage,
				*BoneName.ToString(), ClientDamage, Delta, *GetNameSafe(HitActor));
		}

		if (Delta > 0.f)
		{
			AController* BestInstigator = InstigatorController ? InstigatorController : ServerHit->Instigator.Get();
			APawn* DamageCauser         = BestInstigator ? BestInstigator->GetPawn() : nullptr;
			const FVector HitFromDirection = (!ServerHit->HitResult.TraceStart.IsZero())
				? (ServerHit->HitResult.ImpactPoint - ServerHit->HitResult.TraceStart).GetSafeNormal()
				: -ServerHit->HitResult.ImpactNormal;

			UGameplayStatics::ApplyPointDamage(HitActor, Delta, HitFromDirection, ServerHit->HitResult,
				BestInstigator, DamageCauser, ServerHit->DamageTypeClass);

			UE_LOG(LogClientDamageCorrectionDelta, Warning, TEXT("[CDD] DELTA +%.1f applied to %s"), Delta, *GetNameSafe(HitActor));
		}

		AppliedServerHits.Remove(HitActor);
		return;
	}

	// Server hasn't fired yet — store bone for when ApplyHitDamage runs.
	FPendingClientBone Report;
	Report.BoneName     = BoneName;
	Report.Instigator   = InstigatorController;
	Report.ReportedTime = GetWorld()->GetTimeSeconds();
	PendingClientBones.Add(HitActor, MoveTemp(Report));
}

void UDamageCorrectionDeltaSubsystem::ApplyHitDamage(AActor* HitActor, FName ServerBoneName, float BaseDamage,
	AController* InstigatorController, const FHitResult& HitResult,
	UDamageBoneMapDataAsset* DamageMap, TSubclassOf<UDamageType> DamageTypeClass)
{
	if (!HitActor)
	{
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();
	if (const float* LastTime = LastHitTime.Find(HitActor))
	{
		if ((Now - *LastTime) < DamageIgnoreDelay)
		{
			UE_LOG(LogClientDamageCorrectionDelta, Warning, TEXT("[CDD] SUPPRESSED: %s hit again after %.4fs (threshold %.4fs)"),
				*GetNameSafe(HitActor), Now - *LastTime, DamageIgnoreDelay);
			return;
		}
	}
	LastHitTime.Add(HitActor, Now);

	const FVector HitFromDirection = (!HitResult.TraceStart.IsZero())
		? (HitResult.ImpactPoint - HitResult.TraceStart).GetSafeNormal()
		: -HitResult.ImpactNormal;

	if (FPendingClientBone* ClientReport = PendingClientBones.Find(HitActor))
	{
		// Client bone arrived first — use it directly, full correct damage, no delta needed.
		if (ClientReport->BoneName != ServerBoneName)
		{
			UE_LOG(LogClientDamageCorrectionDelta, Warning, TEXT("[CDD] BONE OVERRIDE: server=%s → client=%s on %s"),
				*ServerBoneName.ToString(), *ClientReport->BoneName.ToString(), *GetNameSafe(HitActor));
		}

		const FName BestBone         = ClientReport->BoneName;
		AController* BestInstigator  = ClientReport->Instigator.IsValid() ? ClientReport->Instigator.Get() : InstigatorController;
		APawn* DamageCauser          = BestInstigator ? BestInstigator->GetPawn() : nullptr;
		const float FinalDamage      = DamageMap ? DamageMap->CalculateDamage(BaseDamage, BestBone) : BaseDamage;

		UE_LOG(LogClientDamageCorrectionDelta, Warning, TEXT("[CDD] APPLY (client bone): %s bone=%s dmg=%.1f"),
			*GetNameSafe(HitActor), *BestBone.ToString(), FinalDamage);

		UGameplayStatics::ApplyPointDamage(HitActor, FinalDamage, HitFromDirection, HitResult,
			BestInstigator, DamageCauser, DamageTypeClass);

		PendingClientBones.Remove(HitActor);
	}
	else
	{
		// No client bone yet — apply with server bone and buffer the record so
		// ReportClientHit can apply the delta when the RPC arrives.
		const float FinalDamage      = DamageMap ? DamageMap->CalculateDamage(BaseDamage, ServerBoneName) : BaseDamage;
		APawn* DamageCauser          = InstigatorController ? InstigatorController->GetPawn() : nullptr;

		UE_LOG(LogClientDamageCorrectionDelta, Warning, TEXT("[CDD] APPLY (server bone, awaiting client): %s bone=%s dmg=%.1f"),
			*GetNameSafe(HitActor), *ServerBoneName.ToString(), FinalDamage);

		UGameplayStatics::ApplyPointDamage(HitActor, FinalDamage, HitFromDirection, HitResult,
			InstigatorController, DamageCauser, DamageTypeClass);

		FAppliedServerHit Record;
		Record.ServerBoneName = ServerBoneName;
		Record.BaseDamage     = BaseDamage;
		Record.AppliedDamage  = FinalDamage;
		Record.Instigator     = InstigatorController;
		Record.HitResult      = HitResult;
		Record.DamageMap      = DamageMap;
		Record.DamageTypeClass = DamageTypeClass;
		Record.AppliedTime    = Now;
		AppliedServerHits.Add(HitActor, MoveTemp(Record));
	}
}

void UDamageCorrectionDeltaSubsystem::PurgeStaleRecords()
{
	const float Now = GetWorld()->GetTimeSeconds();

	for (auto It = PendingClientBones.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || (Now - It.Value().ReportedTime) >= RecordTTL)
		{
			It.RemoveCurrent();
		}
	}

	for (auto It = AppliedServerHits.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || (Now - It.Value().AppliedTime) >= RecordTTL)
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
