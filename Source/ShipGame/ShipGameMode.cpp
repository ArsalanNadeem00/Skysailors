#include "ShipGameMode.h"
#include "ShipActor.h"
#include "ShipCharacter.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY(LogShipGameMode);

AShipGameMode::AShipGameMode()
{
	DefaultPawnClass = AShipCharacter::StaticClass();
}

void AShipGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	// Deliberately NOT calling Super::HandleStartingNewPlayer_Implementation -
	// the default implementation calls RestartPlayer, which looks for a
	// PlayerStart and (in the editor, if none exists) falls back to spawning
	// at the last viewport camera location. We spawn directly on the ship instead.
	SpawnPlayerOnShip(NewPlayer);
}

AShipActor* AShipGameMode::GetOrFindShip()
{
	if (IsValid(Ship))
	{
		return Ship;
	}

	// Assumes exactly one ship is placed in the level. If you ever support
	// multiple ships/teams, swap this for a tagged lookup instead.
	TArray<AActor*> FoundShips;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AShipActor::StaticClass(), FoundShips);
	if (FoundShips.Num() > 0)
	{
		Ship = Cast<AShipActor>(FoundShips[0]);
	}
	else
	{
		UE_LOG(LogShipGameMode, Warning,
			TEXT("No ShipActor found in the level - players will not be spawned. Make sure a BP_ShipActor is placed in the level you're playing."));
	}

	return Ship;
}

void AShipGameMode::SpawnPlayerOnShip(APlayerController* NewPlayer)
{
	if (!NewPlayer)
	{
		return;
	}

	AShipActor* FoundShip = GetOrFindShip();
	if (!FoundShip)
	{
		// GetOrFindShip already logged the warning explaining why.
		return;
	}

	const int32 SpawnIndex = NextSpawnIndex % 4;
	NextSpawnIndex++;

	USceneComponent* SpawnPoint = FoundShip->GetSpawnPoint(SpawnIndex);
	const FVector SpawnLocation = SpawnPoint->GetComponentLocation();
	const FRotator SpawnRotation = SpawnPoint->GetComponentRotation();

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	AShipCharacter* NewCharacter = GetWorld()->SpawnActor<AShipCharacter>(
		DefaultPawnClass, SpawnLocation, SpawnRotation, SpawnParams);

	if (NewCharacter)
	{
		NewPlayer->Possess(NewCharacter);
		// No manual AttachToActor call here on purpose - CharacterMovementComponent
		// detects the ship's mesh as the floor underneath and automatically treats
		// it as a "movement base," carrying the character with it each tick. This
		// keeps players free to walk around the deck instead of being locked in place.
	}
	else
	{
		UE_LOG(LogShipGameMode, Warning, TEXT("Failed to spawn AShipCharacter on the ship."));
	}
}