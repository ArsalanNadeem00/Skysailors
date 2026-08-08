#include "ShipActor.h"
#include "Net/UnrealNetwork.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraShakeBase.h"
#include "Kismet/KismetSystemLibrary.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"

AShipActor::AShipActor()
{
	PrimaryActorTick.bCanEverTick = true;

	// The RootComponent, and the only thing MoveShip actually sweeps for
	// collision (see the class comment) - stands in for the ship's whole
	// silhouette, since the ship is really several separate static meshes
	// and no single one of them is a good collision proxy for all of it.
	// Ignores Pawn so it never blocks/hits player characters - it wraps the
	// whole ship, including walkable areas, so if it didn't ignore Pawn it'd
	// act as an invisible wall keeping players from walking the deck or
	// heading below. ShipMesh/etc.'s own collision (Block Pawn, set below)
	// is what players actually walk on, same as always.
	HazardBox = CreateDefaultSubobject<UBoxComponent>(TEXT("HazardBox"));
	RootComponent = HazardBox;
	HazardBox->SetBoxExtent(FVector(300.f, 150.f, 150.f)); // Placeholder - resize/reposition per ship variant in its Blueprint.
	HazardBox->SetMobility(EComponentMobility::Movable);
	HazardBox->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	HazardBox->SetCollisionResponseToAllChannels(ECR_Block);
	HazardBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	HazardBox->OnComponentHit.AddDynamic(this, &AShipActor::OnShipMeshHit);

	ShipMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ShipMesh"));
	ShipMesh->SetupAttachment(RootComponent);
	ShipMesh->SetMobility(EComponentMobility::Movable);
	ShipMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	ShipMesh->SetCollisionResponseToAllChannels(ECR_Block);

	// Create 4 spawn point components as children of the ship.
	// Default positions are all at the origin - reposition them in the
	// editor/Blueprint after placing the ship in your level.
	for (int32 i = 0; i < 4; i++)
	{
		FName ComponentName = *FString::Printf(TEXT("SpawnPoint_%d"), i);
		USceneComponent* SpawnPoint = CreateDefaultSubobject<USceneComponent>(ComponentName);
		SpawnPoint->SetupAttachment(RootComponent);
		SpawnPoints.Add(SpawnPoint);
	}

	bReplicates = true;

	// Deliberately NOT calling SetReplicateMovement(true): motion is instead
	// conveyed via the small Replicated helm-input set (see MoveShip/the class
	// comment above) and simulated identically on every machine. Leaving
	// built-in transform replication on as well would periodically hard-snap
	// every client to the server's Transform snapshot - which, by the time it
	// arrives, is already stale by however much network latency has passed,
	// so it visibly yanks the ship back before local simulation catches back
	// up. That fight was the remaining source of stutter after MoveShip
	// started running on clients too.
	NetUpdateFrequency = 30.f;
	MinNetUpdateFrequency = 10.f;
}

void AShipActor::BeginPlay()
{
	Super::BeginPlay();
}

void AShipActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AShipActor, ForwardSpeed);
	DOREPLIFETIME(AShipActor, bHelmControlled);
	DOREPLIFETIME(AShipActor, HelmYawInput);
	DOREPLIFETIME(AShipActor, HelmVerticalInput);
	DOREPLIFETIME(AShipActor, ServerLocation);
	DOREPLIFETIME(AShipActor, ServerRotation);
	DOREPLIFETIME(AShipActor, HullIntegrity);
}

void AShipActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Runs on every machine now - see the class comment in ShipActor.h for why.
	MoveShip(DeltaTime);

	if (HasAuthority())
	{
		// Refresh the snapshot clients correct drift against. Replication
		// only actually sends this when it changes and no more often than
		// NetUpdateFrequency, so writing it every tick here is cheap - it
		// just keeps the latest value ready whenever a send is due.
		ServerLocation = GetActorLocation();
		ServerRotation = GetActorRotation();
	}
	else
	{
		CorrectDriftFromServer(DeltaTime);
	}
}

void AShipActor::OnRep_ServerTransform()
{
	bHasServerTransform = true;
}

void AShipActor::CorrectDriftFromServer(float DeltaTime)
{
	if (!bHasServerTransform)
	{
		return;
	}

	const FVector CurrentLocation = GetActorLocation();
	const FRotator CurrentRotation = GetActorRotation();

	const FVector CorrectedLocation = FMath::VInterpTo(CurrentLocation, ServerLocation, DeltaTime, DriftCorrectionSpeed);
	const FRotator CorrectedRotation = FMath::RInterpTo(CurrentRotation, ServerRotation, DeltaTime, DriftCorrectionSpeed);

	// Same non-sweeping move clients already use for their primary motion in
	// MoveShip - clients aren't authoritative over collision, so there's
	// nothing to sweep against here either.
	AddActorWorldOffset(CorrectedLocation - CurrentLocation, /*bSweep=*/false);
	SetActorRotation(CorrectedRotation);
}

void AShipActor::MoveShip(float DeltaTime)
{
	FVector Delta = GetActorForwardVector() * ForwardSpeed * DeltaTime;

	// Ease the effective yaw/vertical input toward the raw replicated target
	// (zero while not helm-controlled) rather than using HelmYawInput/
	// HelmVerticalInput directly. Those only change value when a new network
	// update arrives, so using them directly would snap the turn/ascend rate
	// from 0 to full the instant that update lands - easing removes that step.
	const float TargetYawInput = bHelmControlled ? HelmYawInput : 0.f;
	const float TargetVerticalInput = bHelmControlled ? HelmVerticalInput : 0.f;
	CurrentYawInput = FMath::FInterpTo(CurrentYawInput, TargetYawInput, DeltaTime, HelmInputSmoothingSpeed);
	CurrentVerticalInput = FMath::FInterpTo(CurrentVerticalInput, TargetVerticalInput, DeltaTime, HelmInputSmoothingSpeed);

	if (!FMath::IsNearlyZero(CurrentYawInput))
	{
		AddActorLocalRotation(FRotator(0.f, HelmYawRate * CurrentYawInput * DeltaTime, 0.f));
	}

	if (!FMath::IsNearlyZero(CurrentVerticalInput))
	{
		Delta += FVector::UpVector * HelmVerticalSpeed * CurrentVerticalInput * DeltaTime;
	}

	// Add in a slice of any in-progress crash knockback (see
	// StartCrashKnockback) on top of the normal forward-cruise Delta above,
	// rather than applying it as one instant jump - this way it's swept for
	// collision each tick exactly like everything else, and clients/server
	// stay in sync via the same per-tick math they already share.
	if (KnockbackTimeRemaining > 0.f)
	{
		const float Step = FMath::Min(DeltaTime, KnockbackTimeRemaining);
		Delta += KnockbackDirection * KnockbackSpeed * Step;
		KnockbackTimeRemaining -= Step;
	}

	if (HasAuthority())
	{
		// Only the server needs to actually sweep for collision - it alone is
		// authoritative over whether the ship's path is blocked. Sweeping
		// HazardBox (the root) is enough - ShipMesh/etc. are attached
		// beneath it and just ride along via attachment, not swept
		// individually (see the class comment). Move via MoveComponent
		// directly (rather than AddActorWorldOffset) so we can pass
		// MOVECOMP_IgnoreBases - the same flag UCharacterMovementComponent
		// uses on its end of this relationship (see UpdateBasedMovement) to
		// keep a carried pawn from colliding with the platform it's riding.
		// HazardBox already ignores the Pawn channel outright (see the
		// constructor), but this also covers anything else based on the
		// ship that isn't a Pawn.
		FHitResult Hit;
		HazardBox->MoveComponent(Delta, HazardBox->GetComponentQuat(), /*bSweep=*/true, &Hit, EMoveComponentFlags::MOVECOMP_IgnoreBases);
	}
	else
	{
		// Clients aren't authoritative over collision, so there's nothing to
		// sweep against here - just move to match the server's math.
		AddActorWorldOffset(Delta, /*bSweep=*/false);
	}

	// Ease the ascend/descend tilt (pitch) and turn tilt (roll, banking into
	// the turn) toward whatever they should currently be: full tilt while the
	// relevant input is held, back to level once released. Interpolating from
	// the current tilt (never reset elsewhere) rather than jumping to a fixed
	// value is what keeps re-pressing quickly after a release smooth - it
	// eases onward from wherever the tilt already is, not from level.
	//
	// Deliberately keyed off TargetYawInput/TargetVerticalInput (the raw,
	// unsmoothed helm command) rather than CurrentYawInput/CurrentVerticalInput
	// above - those only decay asymptotically toward zero after release, so
	// using them here would leave the tilt pinned at full angle for a while
	// after the keys go up instead of starting the recovery immediately.
	const float TargetTiltPitch = !FMath::IsNearlyZero(TargetVerticalInput)
		? FMath::Sign(TargetVerticalInput) * AscendDescendTiltAngle
		: 0.f;
	const float TargetTurnTiltRoll = !FMath::IsNearlyZero(TargetYawInput)
		? FMath::Sign(TargetYawInput) * TurnTiltAngle
		: 0.f;

	if (!FMath::IsNearlyEqual(CurrentTiltPitch, TargetTiltPitch) || !FMath::IsNearlyEqual(CurrentTurnTiltRoll, TargetTurnTiltRoll))
	{
		// Constant-rate (not exponential) ramp, sized off each axis's own
		// full-tilt angle so both pitch and roll always take exactly
		// TiltRecoveryDuration seconds to cross their full range, regardless
		// of how far CurrentTiltPitch/CurrentTurnTiltRoll currently are from
		// their target.
		const float PitchRate = AscendDescendTiltAngle / FMath::Max(TiltRecoveryDuration, KINDA_SMALL_NUMBER);
		const float RollRate = TurnTiltAngle / FMath::Max(TiltRecoveryDuration, KINDA_SMALL_NUMBER);
		CurrentTiltPitch = FMath::FInterpConstantTo(CurrentTiltPitch, TargetTiltPitch, DeltaTime, PitchRate);
		CurrentTurnTiltRoll = FMath::FInterpConstantTo(CurrentTurnTiltRoll, TargetTurnTiltRoll, DeltaTime, RollRate);

		FRotator NewRotation = GetActorRotation();
		NewRotation.Pitch = CurrentTiltPitch;
		NewRotation.Roll = CurrentTurnTiltRoll;
		SetActorRotation(NewRotation);
	}
}

void AShipActor::SetHelmControlled(bool bControlled)
{
	bHelmControlled = bControlled;
	if (!bControlled)
	{
		HelmYawInput = 0.f;
		HelmVerticalInput = 0.f;
	}
}

void AShipActor::SetHelmYawInput(float YawInput)
{
	HelmYawInput = FMath::Clamp(YawInput, -1.f, 1.f);
}

void AShipActor::SetHelmVerticalInput(float VerticalInput)
{
	// MoveShip derives the tilt target from HelmVerticalInput each tick
	// and eases toward it, so this just needs to store the clamped input.
	HelmVerticalInput = FMath::Clamp(VerticalInput, -1.f, 1.f);
}

USceneComponent* AShipActor::GetSpawnPoint(int32 Index) const
{
	if (SpawnPoints.IsValidIndex(Index))
	{
		return SpawnPoints[Index];
	}
	return RootComponent;
}

void AShipActor::OnShipMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	// Only the server sweeps for collision (see MoveShip's HasAuthority()
	// branch), so this only ever meaningfully fires there - clients move
	// without sweeping and can't generate hits at all.
	if (!HasAuthority())
	{
		return;
	}

	static const FName CrashTag(TEXT("crash"));
	const bool bIsCrashCollider = (OtherActor && OtherActor->ActorHasTag(CrashTag)) || (OtherComp && OtherComp->ComponentHasTag(CrashTag));
	if (!bIsCrashCollider)
	{
		return;
	}

	ApplyCrashDamage();
}

void AShipActor::ApplyCrashDamage()
{
	if (bHullDestroyed)
	{
		return;
	}

	// Debounce: HazardBox sweeps every tick, so staying wedged against
	// the same crash-tagged obstacle would otherwise re-hit (and re-damage)
	// every tick instead of once per actual crash.
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now - LastCrashDamageTime < HullDamageCooldown)
	{
		return;
	}
	LastCrashDamageTime = Now;

	HullIntegrity = FMath::Max(HullIntegrity - HullDamagePerCrash, 0.f);

	MulticastOnCrash();

	if (HullIntegrity <= 0.f)
	{
		bHullDestroyed = true;
		MulticastOnHullDestroyed();
	}
}

void AShipActor::MulticastOnCrash_Implementation()
{
	StartCrashKnockback();

	if (!CrashCameraShake)
	{
		return;
	}

	// Runs on every connected client (plus the server/host) - each machine
	// only shakes its own local player's camera(s), which is what makes this
	// affect every player's active camera regardless of who's steering.
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (PC && PC->IsLocalController() && PC->PlayerCameraManager)
		{
			PC->PlayerCameraManager->StartCameraShake(CrashCameraShake);
		}
	}
}

void AShipActor::StartCrashKnockback()
{
	// Full length, not half-extent - BoxExtent is a half-extent, so the
	// ship's actual length along its forward/X axis is double that.
	const float ShipLength = HazardBox ? HazardBox->GetScaledBoxExtent().X * 2.f : 0.f;
	const float KnockbackDistance = ShipLength * CrashKnockbackLengthMultiplier;

	KnockbackDirection = -GetActorForwardVector();
	KnockbackSpeed = CrashKnockbackDuration > 0.f ? KnockbackDistance / CrashKnockbackDuration : 0.f;
	KnockbackTimeRemaining = CrashKnockbackDuration;
}

void AShipActor::MulticastOnHullDestroyed_Implementation()
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC || !PC->IsLocalController())
		{
			continue;
		}

		if (PC->PlayerCameraManager)
		{
			PC->PlayerCameraManager->StartCameraFade(0.f, 1.f, DestroyedFadeOutDuration, FLinearColor::Black, /*bFadeAudio=*/false, /*bHoldWhenFinished=*/true);
		}

		// Placeholder "ship destroyed" behavior - quit after the fade
		// finishes rather than cutting straight to black and closing.
		FTimerHandle QuitTimer;
		FTimerDelegate QuitDelegate = FTimerDelegate::CreateUObject(this, &AShipActor::QuitGameForPlayer, PC);
		GetWorldTimerManager().SetTimer(QuitTimer, QuitDelegate, DestroyedFadeOutDuration, false);
	}
}

void AShipActor::QuitGameForPlayer(APlayerController* PC)
{
	if (PC)
	{
		UKismetSystemLibrary::QuitGame(this, PC, EQuitPreference::Quit, false);
	}
}
