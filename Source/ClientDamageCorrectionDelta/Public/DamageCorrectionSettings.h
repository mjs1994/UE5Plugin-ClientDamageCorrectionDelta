// Copyright 2026, HRZN Games.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "DamageCorrectionSettings.generated.h"

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Damage Correction Delta"))
class CLIENTDAMAGECORRECTIONDELTA_API UDamageCorrectionSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UDamageCorrectionSettings();

	virtual FName GetCategoryName() const override { return FName("Plugins"); }

	// Minimum seconds between registering repeated hits on the same actor from the same side.
	// Suppresses bullet exit wounds even when client and server physics run in different frames.
	// 0.05s covers one frame at 30 fps with margin, well below any realistic inter-shot interval.
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Damage Correction Delta", meta = (ClampMin = "0.0", ClampMax = "1.0", ForceUnits = "s"))
	float DamageIgnoreDelay = 0.05f;

	// Seconds an unmatched client bone report is kept before being discarded.
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Damage Correction Delta", meta = (ClampMin = "1.0", ForceUnits = "s"))
	float RecordTTL = 5.0f;
};
