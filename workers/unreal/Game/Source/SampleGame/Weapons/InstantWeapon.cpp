// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "InstantWeapon.h"

#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "SampleGameCharacter.h"
#include "SampleGameLogging.h"
#include "UnrealNetwork.h"


AInstantWeapon::AInstantWeapon()
{
	BurstInterval = 0.5f;
	BurstCount = 1;
	BurstShotInterval = 0.2f;
	LastBurstTime = 0.0f;
	BurstShotsRemaining = 0;
	MaxRange = 50000.0f;
	ShotBaseDamage = 10.0f;
	HitValidationTolerance = 50.0f;
	DamageTypeClass = UDamageType::StaticClass();  // generic damage type
	HitFXTemplate = nullptr;
	ShotVisualizationDelayTolerance = FTimespan::FromMilliseconds(3000.0f);
	bDrawDebugLineTrace = false;
}

void AInstantWeapon::StartFire()
{
	check(GetNetMode() == NM_Client);

	float Now = UGameplayStatics::GetRealTimeSeconds(GetWorld());
	if (GetWeaponState() == EWeaponState::Idle && Now > LastBurstTime + BurstInterval)
	{
		SetWeaponState(EWeaponState::Firing);

		// Initialize the burst.
		LastBurstTime = Now;
		BurstShotsRemaining = BurstCount;

		// Fire a shot right away.
		DoFire();

		ClearTimerIfRunning();
		if (IsFullyAutomatic())
		{
			GetWorldTimerManager().SetTimer(NextShotTimer, NextShotTimerDelegate, BurstInterval, true);
		}
		else if (IsBurstFire())
		{
			GetWorldTimerManager().SetTimer(NextShotTimer, NextShotTimerDelegate, BurstShotInterval, true);
		}
		else
		{
			StopFiring();
		}
	}
}

void AInstantWeapon::StopFire()
{
	check(GetNetMode() == NM_Client);

	// Can't force stop a burst.
	if (GetWeaponState() == EWeaponState::Firing && !IsBurstFire())
	{
		StopFiring();
	}
}

void AInstantWeapon::BeginPlay()
{
	Super::BeginPlay();

	NextShotTimerDelegate.BindLambda([&]() {
		DoFire();
	});

	GetWorld()->DebugDrawTraceTag = kTraceTag;
}

void AInstantWeapon::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(AInstantWeapon, HitNotify, COND_SkipOwner);
}

void AInstantWeapon::DoFire()
{
	check(GetNetMode() == NM_Client);

	FInstantHitInfo HitInfo;
	if (DoLineTrace(HitInfo))
	{
		ServerDidHit(HitInfo);
		SpawnHitFX(HitInfo);  // Spawn the hit fx locally
	}
	else
	{
		ServerDidMiss(HitInfo);
	}

	if (IsBurstFire())
	{
		--BurstShotsRemaining;
		if (BurstShotsRemaining <= 0)
		{
			StopFiring();
		}
	}
}

bool AInstantWeapon::DoLineTrace(FInstantHitInfo& OutHitInfo)
{
	ASampleGameCharacter* Character = GetOwningCharacter();

	FCollisionQueryParams TraceParams;
	TraceParams.bTraceComplex = true;
	TraceParams.bTraceAsyncScene = true;
	TraceParams.bReturnPhysicalMaterial = false;
	TraceParams.AddIgnoredActor(this);
	TraceParams.AddIgnoredActor(Character);

	if (bDrawDebugLineTrace)
	{
		TraceParams.TraceTag = kTraceTag;
	}

	FHitResult HitResult(ForceInit);
	FVector TraceStart = Character->GetLineTraceStart();
	FVector TraceEnd = TraceStart + Character->GetLineTraceDirection() * MaxRange;

	bool bDidHit = GetWorld()->LineTraceSingleByChannel(
		HitResult,
		TraceStart,
		TraceEnd,
		ECC_WorldStatic,
		TraceParams);

	if (!bDidHit)
	{
		return false;
 	}
	
	OutHitInfo.Location = HitResult.ImpactPoint;
	OutHitInfo.HitActor = HitResult.GetActor();

	return true;
}

void AInstantWeapon::NotifyClientsOfHit(const FInstantHitInfo& HitInfo)
{
	check(GetNetMode() < NM_Client);

	HitNotify.HitActor = HitInfo.HitActor;
	HitNotify.Location = HitInfo.Location;
	HitNotify.Timestamp = FDateTime::UtcNow();
}

void AInstantWeapon::SpawnHitFX(const FInstantHitInfo& HitInfo)
{
	if (GetNetMode() < NM_Client || HitFXTemplate == nullptr)
	{
		return;
	}

	UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), HitFXTemplate, HitInfo.Location, FRotator::ZeroRotator, true);
}

bool AInstantWeapon::ValidateHit(const FInstantHitInfo& HitInfo)
{
	check(GetNetMode() < NM_Client);

	if (HitInfo.HitActor == nullptr)
	{
		return false;
	}

	// Get the bounding box of the actor we hit.
	const FBox HitBox = HitInfo.HitActor->GetComponentsBoundingBox();

	// Calculate the extent of the box along all 3 axes an add a tolerance factor.
	FVector BoxExtent = 0.5 * (HitBox.Max - HitBox.Min) + (HitValidationTolerance * FVector::OneVector);
	FVector BoxCenter = (HitBox.Max + HitBox.Min) * 0.5;

	// Avoid precision errors for really thin objects.
	BoxExtent.X = FMath::Max(20.0f, BoxExtent.X);
	BoxExtent.Y = FMath::Max(20.0f, BoxExtent.Y);
	BoxExtent.Z = FMath::Max(20.0f, BoxExtent.Z);

	// Check whether the hit is within the box + tolerance.
	if (FMath::Abs(HitInfo.Location.X - BoxCenter.X) > BoxExtent.X ||
		FMath::Abs(HitInfo.Location.Y - BoxCenter.Y) > BoxExtent.Y ||
		FMath::Abs(HitInfo.Location.Z - BoxCenter.Z) > BoxExtent.Z)
	{
		return false;
	}

	return true;
}

bool AInstantWeapon::ServerDidHit_Validate(const FInstantHitInfo& HitInfo)
{
	return true;
}

void AInstantWeapon::ServerDidHit_Implementation(const FInstantHitInfo& HitInfo)
{
	bool bDoNotifyHit = false;

	if (HitInfo.HitActor == nullptr)
	{
		bDoNotifyHit = true;
	}
	else
	{
		if (ValidateHit(HitInfo))
		{

			FDamageEvent DmgEvent;
			DmgEvent.DamageTypeClass = DamageTypeClass;

			HitInfo.HitActor->TakeDamage(ShotBaseDamage, DmgEvent, GetOwningCharacter()->GetController(), this);
			bDoNotifyHit = true;
		}
		else
		{
			UE_LOG(LogSampleGame, Verbose, TEXT("%s server: rejected hit of actor %s"), *this->GetName(), *HitInfo.HitActor->GetName());
		}
	}

	if (bDoNotifyHit)
	{
		NotifyClientsOfHit(HitInfo);
	}
}

bool AInstantWeapon::ServerDidMiss_Validate(const FInstantHitInfo& HitInfo)
{
	return true;
}

void AInstantWeapon::ServerDidMiss_Implementation(const FInstantHitInfo& HitInfo)
{
	UE_LOG(LogSampleGame, Verbose, TEXT("Shot missed"));
}

void AInstantWeapon::OnRep_HitNotify()
{
	// This check is in place to ignore when this method gets called on initial checkout of the actor.
	if (FDateTime::UtcNow() < HitNotify.Timestamp + ShotVisualizationDelayTolerance)
	{
		SpawnHitFX(HitNotify);
	}
}

void AInstantWeapon::ClearTimerIfRunning()
{
	check(GetNetMode() == NM_Client);

	if (NextShotTimer.IsValid())
	{
		GetWorldTimerManager().ClearTimer(NextShotTimer);
	}
}

void AInstantWeapon::StopFiring()
{
	check(GetNetMode() == NM_Client);

	SetWeaponState(EWeaponState::Idle);
	ClearTimerIfRunning();
}