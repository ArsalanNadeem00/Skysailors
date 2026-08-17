#include "CannonProjectile.h"
#include "EnemyShip.h"
#include "ShipActor.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "Net/UnrealNetwork.h"

ACannonProjectile::ACannonProjectile()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	CollisionComponent = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionComponent"));
	CollisionComponent->InitSphereRadius(15.f);
	CollisionComponent->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	CollisionComponent->OnComponentHit.AddDynamic(this, &ACannonProjectile::OnHit);
	RootComponent = CollisionComponent;

	ProjectileMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ProjectileMesh"));
	ProjectileMesh->SetupAttachment(RootComponent);
	ProjectileMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	ProjectileMovement->UpdatedComponent = CollisionComponent;
	ProjectileMovement->InitialSpeed = 4000.f;
	ProjectileMovement->MaxSpeed = 4000.f;
	ProjectileMovement->bRotationFollowsVelocity = true;
	ProjectileMovement->bShouldBounce = false;
	ProjectileMovement->ProjectileGravityScale = 0.f;
}

void ACannonProjectile::BeginPlay()
{
	Super::BeginPlay();

	SetLifeSpan(LifeSpanSeconds);

	if (FireSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, FireSound, GetActorLocation());
	}
}

void ACannonProjectile::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACannonProjectile, ProjectileMeshAsset);
	DOREPLIFETIME(ACannonProjectile, FireSound);
	DOREPLIFETIME(ACannonProjectile, ImpactEffect);
	DOREPLIFETIME(ACannonProjectile, ImpactSound);
}

void ACannonProjectile::ConfigureAmmo(float InDamage, UStaticMesh* InProjectileMesh, USoundBase* InFireSound, UNiagaraSystem* InImpactEffect, USoundBase* InImpactSound)
{
	Damage = InDamage;
	FireSound = InFireSound;
	ImpactEffect = InImpactEffect;
	ImpactSound = InImpactSound;

	ProjectileMeshAsset = InProjectileMesh;
	OnRep_ProjectileMeshAsset();
}

void ACannonProjectile::OnRep_ProjectileMeshAsset()
{
	if (ProjectileMesh)
	{
		ProjectileMesh->SetStaticMesh(ProjectileMeshAsset);
	}
}

void ACannonProjectile::OnHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	// Cosmetic and self-contained - every machine that locally detects this
	// hit (server and clients both simulate the same motion, see the class
	// comment) plays its own copy, and disabling collision here stops this
	// same instance generating repeat hit events on this machine before the
	// server's Destroy() has had a chance to replicate down.
	CollisionComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (ImpactEffect)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, ImpactEffect, Hit.ImpactPoint, Hit.ImpactNormal.Rotation());
	}
	if (ImpactSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, ImpactSound, Hit.ImpactPoint);
	}

	if (HasAuthority())
	{
		if (AEnemyShip* Enemy = Cast<AEnemyShip>(OtherActor))
		{
			Enemy->ApplyDamage(Damage);
		}
		else if (bDamagesPlayerShip)
		{
			if (AShipActor* Ship = Cast<AShipActor>(OtherActor))
			{
				Ship->ApplyDamage(Damage);
			}
		}
		Destroy();
	}
}
