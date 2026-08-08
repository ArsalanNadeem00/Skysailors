#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "SteeringWheel.generated.h"

class UStaticMeshComponent;
class UCameraComponent;
class UInputAction;
class AShipActor;
struct FInputActionValue;

/**
 * Stationary interactable placed on the ship's deck. Characters interact with
 * it to hand control of their PlayerController over to this Pawn, which then
 * feeds A/D input into the AShipActor's helm yaw and W/S input into its
 * ascend/descend. The ship's forward speed is always the constant autopilot
 * speed - steering only ever turns and raises/lowers the ship, never controls
 * throttle. Interacting again (or disconnecting) hands control back to the
 * character that engaged the helm.
 *
 * Only one controller can possess this Pawn at a time (enforced by TryEngage),
 * which is what guarantees only one player can steer the ship at once.
 */
UCLASS()
class SHIPGAME_API ASteeringWheel : public APawn
{
	GENERATED_BODY()

public:
	ASteeringWheel();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Steering Wheel")
	UStaticMeshComponent* WheelMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Steering Wheel")
	UCameraComponent* HelmCamera;

	// The ship this wheel steers. Auto-found in BeginPlay (first AShipActor in
	// the level) if left unset, and also used as the attach parent so the
	// wheel rides the deck instead of being left behind as the ship moves.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Steering Wheel")
	AShipActor* ControlledShip;

	// Reuses the same 2D move action characters use: X (A/D) drives yaw, Y
	// (W/S) drives ascend/descend. There is no player throttle control.
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* MoveAction;

	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* InteractAction;

	UFUNCTION(BlueprintPure, Category = "Steering Wheel")
	bool IsOccupied() const { return GetController() != nullptr; }

	// Called by a character trying to take the helm. Returns false if someone
	// is already steering. Server-only - the caller is expected to have
	// already routed this through a Server RPC.
	bool TryEngage(AController* InstigatingController, APawn* InstigatingPawn);

	AShipActor* GetOrFindControlledShip();

protected:
	virtual void BeginPlay() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void UnPossessed() override;

	void Steer(const FInputActionValue& Value);
	void StopSteer();
	void Interact();

	UFUNCTION(Server, Reliable)
	void ServerSetHelmInput(float Yaw, float Vertical);

	UFUNCTION(Server, Reliable)
	void ServerReleaseHelm();

	// The character (or other pawn) to hand control back to when this wheel
	// is released. Only meaningful on the server, alongside the RPCs above.
	UPROPERTY()
	APawn* PreviousPawn;
};
