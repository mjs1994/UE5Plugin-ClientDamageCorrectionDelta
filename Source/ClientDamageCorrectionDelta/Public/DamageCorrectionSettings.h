// Copyright 2026, HRZN Games.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "DamageCorrectionSettings.generated.h"

UENUM(BlueprintType)
enum class EDamageCorrectionValidationMode : uint8
{
	// Damage is applied by the server's authoritative projectile via ApplyHitDamage.
	// The client bone RPC corrects the bone only. Use when server-side collision is reliable.
	ServerAuthoritative  UMETA(DisplayName = "Server Authoritative"),

	// Damage is applied directly from the client bone RPC. ApplyHitDamage is a no-op.
	// Use when server-side projectile collision is unreliable.
	// Client is fully trusted - not recommended for PvP.
	ClientAuthoritative  UMETA(DisplayName = "Client Authoritative"),

	// Damage is applied from the client bone RPC, but the server first checks that the
	// hit was geometrically plausible: distance within MaxValidationRange and optionally
	// a line-of-sight trace. Rejected hits are discarded and logged.
	// Line-of-sight traces can prevent bullet penetration simulations from functioning, so test thoroughly.
	ClientWithValidation UMETA(DisplayName = "Client With Validation"),
};

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Damage Correction Delta"))
class CLIENTDAMAGECORRECTIONDELTA_API UDamageCorrectionSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UDamageCorrectionSettings();

	virtual FName GetCategoryName() const override { return FName("Plugins"); }

	// Controls how damage authority is determined.
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Damage Correction Delta")
	EDamageCorrectionValidationMode ValidationMode = EDamageCorrectionValidationMode::ServerAuthoritative;

	// [ClientWithValidation only] Rejects hits where the distance from the instigator's eye
	// to the impact point exceeds MaxValidationRange. Catches impossible-range fake RPCs.
	// Safe to leave enabled alongside bullet penetration - it only measures distance, not geometry.
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Damage Correction Delta", meta = (EditCondition = "ValidationMode == EDamageCorrectionValidationMode::ClientWithValidation"))
	bool bValidateDistance = true;

	// [ClientWithValidation only] Maximum distance in cm between the instigator's eye and
	// the impact point. Default is 150,000 cm (1,500 m).
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Damage Correction Delta", meta = (EditCondition = "ValidationMode == EDamageCorrectionValidationMode::ClientWithValidation && bValidateDistance", ClampMin = "1.0", ForceUnits = "cm"))
	float MaxValidationRange = 150000.f;

	// [ClientWithValidation only] Rejects hits where a server-side ECC_Visibility trace
	// from the instigator's eye to the impact point is blocked by world geometry.
	// Disable if using bullet penetration - penetrable surfaces will block the trace and 
	// cause false rejections on otherwise legitimate shots.
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Damage Correction Delta", meta = (EditCondition = "ValidationMode == EDamageCorrectionValidationMode::ClientWithValidation"))
	bool bValidateLineOfSight = true;

	// Minimum seconds between registering repeated hits on the same actor from the same side.
	// Suppresses bullet exit wounds even when client and server physics run in different frames.
	// 0.05s covers one frame at 30 fps with margin, well below any realistic inter-shot interval.
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Damage Correction Delta", meta = (ClampMin = "0.0", ClampMax = "1.0", ForceUnits = "s"))
	float DamageIgnoreDelay = 0.05f;

	// Seconds an unmatched client bone report is kept before being discarded.
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Damage Correction Delta", meta = (ClampMin = "1.0", ForceUnits = "s"))
	float RecordTTL = 5.0f;
};
