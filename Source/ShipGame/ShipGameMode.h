#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Logging/LogMacros.h"
#include "ShipGameMode.generated.h"

class AShipActor;

DECLARE_LOG_CATEGORY_EXTERN(LogShipGameMode, Log, All);

UCLASS()
class SHIPGAME_API AShipGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AShipGameMode();

protected:
	/** Overriding this instead of PostLogin/RestartPlayer skips Unreal's
	 *  default PlayerStart lookup (and its editor-only "spawn at camera
	 *  location" fallback) entirely, so only one pawn is ever spawned per
	 *  player - directly on the ship. */
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;

	// Cached reference to the ship placed in the level. Don't read this
	// directly - call GetOrFindShip(), since for the local/listen-server
	// player, HandleStartingNewPlayer can fire before GameMode::BeginPlay
	// has finished, so a BeginPlay-only lookup can miss even though the
	// ship is already in the level.
	UPROPERTY(BlueprintReadOnly, Category = "Ship")
	AShipActor* Ship;

	// Round-robins through the ship's 4 spawn points as players join.
	int32 NextSpawnIndex = 0;

	void SpawnPlayerOnShip(APlayerController* NewPlayer);

	// Returns the cached Ship if valid, otherwise searches the level again
	// and caches whatever it finds.
	AShipActor* GetOrFindShip();
};