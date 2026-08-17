#include "EnemyShip.h"
#include "ShipActor.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "CollisionShape.h"
#include "NiagaraFunctionLibrary.h"

TArray<TWeakObjectPtr<AEnemyShip>> AEnemyShip::ActiveEnemyShips;

AEnemyShip::AEnemyShip()
{
	PrimaryActorTick.bCanEverTick = true;

	// Same setup as AShipActor::HazardBox - see the property comment for why.
	HazardBox = CreateDefaultSubobject<UBoxComponent>(TEXT("HazardBox"));
	RootComponent = HazardBox;
	HazardBox->SetBoxExtent(FVector(300.f, 150.f, 150.f)); // Placeholder - resize/reposition per enemy ship variant in its Blueprint.
	HazardBox->SetMobility(EComponentMobility::Movable);
	HazardBox->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	HazardBox->SetCollisionResponseToAllChannels(ECR_Block);
	HazardBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);

	ShipMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ShipMesh"));
	ShipMesh->SetupAttachment(RootComponent);
	ShipMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Makes colliding into the player's AShipActor trigger its existing
	// crash-tag hull-damage path for free - see the property comment.
	Tags.Add(FName("crash"));

	bReplicates = true;

	// Deliberately NOT calling SetReplicateMovement(true) - same reasoning
	// as AShipActor's constructor: motion is instead conveyed via the small
	// Replicated state above and simulated identically on every machine.
	SetReplicateMovement(false);

	NetUpdateFrequency = 30.f;
	MinNetUpdateFrequency = 10.f;
}

void AEnemyShip::BeginPlay()
{
	Super::BeginPlay();

	ActiveEnemyShips.Add(this);

	if (AShipActor* Ship = GetOrFindControlledShip())
	{
		FlankSign = FMath::Sign(FVector::DotProduct(GetActorLocation() - Ship->GetActorLocation(), Ship->GetActorRightVector()));
		if (FMath::IsNearlyZero(FlankSign))
		{
			FlankSign = 1.f;
		}
	}
}

void AEnemyShip::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	ActiveEnemyShips.RemoveAll([this](const TWeakObjectPtr<AEnemyShip>& Ptr)
	{
		return !Ptr.IsValid() || Ptr.Get() == this;
	});

	Super::EndPlay(EndPlayReason);
}

void AEnemyShip::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AEnemyShip, CurrentState);
	DOREPLIFETIME(AEnemyShip, FlankSign);
	DOREPLIFETIME(AEnemyShip, HullIntegrity);
	DOREPLIFETIME(AEnemyShip, ServerLocation);
	DOREPLIFETIME(AEnemyShip, ServerRotation);
}

void AEnemyShip::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!ControlledShip)
	{
		return;
	}

	if (HasAuthority())
	{
		TryTransitionToCircling();
	}

	UpdateFlightState(DeltaTime);

	if (HasAuthority())
	{
		// Refresh the snapshot clients correct drift against - same as
		// AShipActor::Tick, see its comment for why writing this every tick
		// is cheap even though replication itself is rate-limited.
		ServerLocation = GetActorLocation();
		ServerRotation = GetActorRotation();
	}
	else
	{
		CorrectDriftFromServer(DeltaTime);
	}
}

AShipActor* AEnemyShip::GetOrFindControlledShip()
{
	if (IsValid(ControlledShip))
	{
		return ControlledShip;
	}

	// Assumes exactly one ship is placed in the level, matching AMount/
	// APuddle/ASteeringWheel's own lookup for the same reason.
	TArray<AActor*> FoundShips;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AShipActor::StaticClass(), FoundShips);
	if (FoundShips.Num() > 0)
	{
		ControlledShip = Cast<AShipActor>(FoundShips[0]);
	}

	return ControlledShip;
}

void AEnemyShip::UpdateFlightState(float DeltaTime)
{
	FVector SeekDirection;
	if (CurrentState == EEnemyShipState::ApproachingFlank)
	{
		if (!bReachedFlank && GetRelativeForwardDistanceFromShip() >= 0.f)
		{
			bReachedFlank = true;
		}

		// Once this ship has drawn level with (or past) the player ship's
		// beam, stop actively re-seeking the flank point and just hold the
		// current heading instead. SteerTowards never lets this ship slow
		// down or loiter (it always thrusts forward at ForwardSpeed), so
		// continuing to re-target a fixed abeam point forever would make it
		// overshoot and loop straight back around that point - an orbit
		// that happens entirely inside ApproachingFlank, before
		// TryTransitionToCircling's FlankPastDistanceThreshold check ever
		// gets a chance to matter. Flying straight instead is what actually
		// lets it cover real distance past the ship for that threshold to
		// measure.
		if (bReachedFlank)
		{
			SeekDirection = GetActorForwardVector();
		}
		else
		{
			// Lead pursuit, not plain pursuit - see ComputeLeadTarget. The
			// flank point is rigidly attached to the player ship (see
			// ComputeFlankTargetWorld), so its velocity is just the player
			// ship's own.
			const FVector FlankTarget = ComputeLeadTarget(ComputeFlankTargetWorld(), GetShipVelocityEstimate());
			SeekDirection = (FlankTarget - GetActorLocation()).GetSafeNormal();
		}
	}
	else
	{
		AdvanceOrbitAngle(DeltaTime);

		// Extra lead while this ship is heading in roughly the same
		// direction as the player ship - i.e. tailing it from behind,
		// trying to catch up - see ChaseLeadTimeBoost's comment for why
		// that specific geometry needs more than the baseline OrbitLeadTime.
		// Clamped to >= 0 so heading away from/across the player ship's
		// direction of travel never *reduces* the lead below baseline.
		const float ChaseAlignment = FMath::Max(FVector::DotProduct(GetActorForwardVector(), ControlledShip->GetActorForwardVector()), 0.f);
		const float EffectiveLeadTime = OrbitLeadTime + ChaseLeadTimeBoost * ChaseAlignment;

		// Analytic lead, not ComputeLeadTarget's linear-velocity
		// extrapolation - see OrbitLeadTime's comment for why a target
		// moving in a circle needs this distinction. Evaluates the ring's
		// exact closed-form position EffectiveLeadTime seconds in the
		// future - both the angle (advanced by this ship's own known
		// angular rate) and the player ship's position (extrapolated
		// forward by its own estimated velocity) - rather than a
		// straight-line guess that diverges from the true curved path.
		const FTransform ShipTransform = ControlledShip->GetRootComponent()->GetComponentTransform();
		const float FutureAngle = OrbitAngle + FMath::DegreesToRadians(OrbitAngularSpeed) * FlankSign * EffectiveLeadTime;
		const FVector FutureShipLocation = ShipTransform.GetLocation() + GetShipVelocityEstimate() * EffectiveLeadTime;
		const FVector LeadOrbitTarget = EvaluateOrbitRing(FutureAngle, FutureShipLocation, ShipTransform.GetRotation());

		SeekDirection = (LeadOrbitTarget - GetActorLocation()).GetSafeNormal();
	}

	float AvoidanceWeight = 0.f;
	const FVector AvoidanceDirection = ComputeAvoidanceDirection(AvoidanceWeight);
	const FVector SeparationDirection = ComputeSeparationDirection();

	// Weighted sum, not a hard priority branch - obstacle/ship avoidance is
	// meant to be extra steering forces blended in every tick, not a
	// discrete state (see the class comment). Scaling AvoidanceWeight by
	// proximity (see ComputeAvoidanceDirection) gives priority-override
	// behavior - avoidance dominates when close, fades to negligible when
	// clear - without needing a branch.
	const FVector Steering = SeekDirection * SeekWeight + AvoidanceDirection * AvoidanceWeight + SeparationDirection * SeparationWeight;
	FVector DesiredDirection = Steering.GetSafeNormal();
	if (DesiredDirection.IsNearlyZero())
	{
		DesiredDirection = GetActorForwardVector();
	}

	SteerTowards(DesiredDirection, DeltaTime);
}

FVector AEnemyShip::ComputeFlankTargetWorld() const
{
	const FVector LocalOffset(FlankForwardOffset, FlankSign * FlankLateralOffset, FlankHeightOffset);
	const FTransform ShipTransform = ControlledShip->GetRootComponent()->GetComponentTransform();

	// Rotation + translation only, NOT ShipTransform.TransformPosition -
	// that also folds in the component's scale, which would silently
	// stretch these documented-as-absolute-cm offsets by however the ship
	// happens to be scaled in its Blueprint (e.g. a non-uniform scale on a
	// ship silhouette that's longer than it is wide). See
	// ComputeOrbitTargetWorld's comment - same fix, same reasoning.
	return ShipTransform.GetLocation() + ShipTransform.GetRotation().RotateVector(LocalOffset);
}

void AEnemyShip::AdvanceOrbitAngle(float DeltaTime)
{
	if (!bOrbitInitialized)
	{
		// Seed from this ship's current position expressed in the player
		// ship's LOCAL frame (not world space) - has to match the space
		// EvaluateOrbitRing works in, or the very first orbit target would
		// jump relative to wherever this ship actually is at the moment of
		// transition.
		const FTransform ShipTransform = ControlledShip->GetRootComponent()->GetComponentTransform();
		const FVector LocalPos = ShipTransform.GetRotation().UnrotateVector(GetActorLocation() - ShipTransform.GetLocation());
		OrbitAngle = FMath::Atan2(LocalPos.Y, LocalPos.X);
		bOrbitInitialized = true;
	}

	OrbitAngle += FMath::DegreesToRadians(OrbitAngularSpeed) * FlankSign * DeltaTime;
}

FVector AEnemyShip::EvaluateOrbitRing(float Angle, const FVector& ShipLocation, const FQuat& ShipRotation) const
{
	// Deliberately ShipRotation.RotateVector, not
	// FTransform::TransformPosition - the latter also scales by the
	// component's own scale, which would turn this into an ellipse
	// (stretched/squashed unevenly on different sides) whenever the ship's
	// root component has non-uniform scale in its Blueprint, independent
	// of whether the ship is even moving. OrbitRadius is documented as an
	// absolute cm radius, so it must stay unscaled.
	const FVector LocalOffset(OrbitRadius * FMath::Cos(Angle), OrbitRadius * FMath::Sin(Angle), OrbitHeightOffset);
	return ShipLocation + ShipRotation.RotateVector(LocalOffset);
}

FVector AEnemyShip::GetShipVelocityEstimate() const
{
	return ControlledShip->GetActorForwardVector() * ControlledShip->ForwardSpeed;
}

FVector AEnemyShip::ComputeLeadTarget(const FVector& TargetWorld, const FVector& TargetVelocity) const
{
	const float Distance = FVector::Dist(GetActorLocation(), TargetWorld);
	const float TimeToIntercept = ForwardSpeed > KINDA_SMALL_NUMBER
		? FMath::Min(Distance / ForwardSpeed, MaxLeadTime)
		: 0.f;

	return TargetWorld + TargetVelocity * TimeToIntercept;
}

FVector AEnemyShip::ComputeAvoidanceDirection(float& OutWeight) const
{
	OutWeight = 0.f;

	const FVector Start = GetActorLocation();
	const FVector End = Start + GetActorForwardVector() * AvoidanceTraceDistance;
	const FCollisionShape Sphere = FCollisionShape::MakeSphere(AvoidanceTraceRadius);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	FHitResult Hit;
	// ECC_GameTraceChannel1 is DefaultEngine.ini's "ShipBlock" custom
	// channel - obstacles (e.g. BP_FloatingRock) opt in by setting their
	// response to Block on it in their Blueprint; every actor ignores it by
	// default, so this never reacts to players, projectiles, or other enemy
	// ships (see ComputeSeparationDirection for ship-to-ship avoidance).
	const bool bBlocked = GetWorld()->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_GameTraceChannel1, Sphere, QueryParams);
	if (!bBlocked)
	{
		return FVector::ZeroVector;
	}

	OutWeight = BaseAvoidanceWeight * (1.f - Hit.Distance / AvoidanceTraceDistance);
	return FVector::VectorPlaneProject(Hit.Normal, GetActorForwardVector()).GetSafeNormal();
}

FVector AEnemyShip::ComputeSeparationDirection() const
{
	FVector Repulsion = FVector::ZeroVector;
	const FVector MyLocation = GetActorLocation();

	for (const TWeakObjectPtr<AEnemyShip>& Other : ActiveEnemyShips)
	{
		AEnemyShip* OtherShip = Other.Get();
		if (!OtherShip || OtherShip == this)
		{
			continue;
		}

		const FVector Offset = MyLocation - OtherShip->GetActorLocation();
		const float Distance = Offset.Size();
		if (Distance > 0.f && Distance < SeparationRadius)
		{
			Repulsion += Offset.GetSafeNormal() * (SeparationRadius - Distance) / SeparationRadius;
		}
	}

	return Repulsion.GetSafeNormal();
}

void AEnemyShip::SteerTowards(const FVector& DesiredDirection, float DeltaTime)
{
	const FRotator DesiredRotation = DesiredDirection.Rotation();
	const FRotator NewRotation = FMath::RInterpConstantTo(GetActorRotation(), DesiredRotation, DeltaTime, TurnRateDegreesPerSec);
	SetActorRotation(NewRotation);

	const FVector Delta = GetActorForwardVector() * ForwardSpeed * DeltaTime;

	if (HasAuthority())
	{
		// Only the server needs to actually sweep for collision - same
		// reasoning and MOVECOMP_IgnoreBases flag as AShipActor::MoveShip.
		FHitResult Hit;
		HazardBox->MoveComponent(Delta, HazardBox->GetComponentQuat(), /*bSweep=*/true, &Hit, EMoveComponentFlags::MOVECOMP_IgnoreBases);
	}
	else
	{
		AddActorWorldOffset(Delta, /*bSweep=*/false);
	}
}

void AEnemyShip::TryTransitionToCircling()
{
	if (CurrentState != EEnemyShipState::ApproachingFlank)
	{
		return;
	}

	if (GetRelativeForwardDistanceFromShip() < -FlankPastDistanceThreshold)
	{
		CurrentState = EEnemyShipState::Circling;
	}
}

float AEnemyShip::GetRelativeForwardDistanceFromShip() const
{
	return FVector::DotProduct(
		GetActorLocation() - ControlledShip->GetActorLocation(),
		ControlledShip->GetActorForwardVector());
}

void AEnemyShip::OnRep_ServerTransform()
{
	bHasServerTransform = true;
}

void AEnemyShip::CorrectDriftFromServer(float DeltaTime)
{
	if (!bHasServerTransform)
	{
		return;
	}

	const FVector CurrentLocation = GetActorLocation();
	const FRotator CurrentRotation = GetActorRotation();

	const FVector CorrectedLocation = FMath::VInterpTo(CurrentLocation, ServerLocation, DeltaTime, DriftCorrectionSpeed);
	const FRotator CorrectedRotation = FMath::RInterpTo(CurrentRotation, ServerRotation, DeltaTime, DriftCorrectionSpeed);

	AddActorWorldOffset(CorrectedLocation - CurrentLocation, /*bSweep=*/false);
	SetActorRotation(CorrectedRotation);
}

void AEnemyShip::ApplyDamage(float Amount)
{
	if (bDestroyed)
	{
		return;
	}

	HullIntegrity = FMath::Max(HullIntegrity - Amount, 0.f);

	if (HullIntegrity <= 0.f)
	{
		bDestroyed = true;
		MulticastPlayDeathEffect();
		Destroy();
	}
}

void AEnemyShip::MulticastPlayDeathEffect_Implementation()
{
	if (DeathEffect)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, DeathEffect, GetActorLocation(), GetActorRotation());
	}
}
