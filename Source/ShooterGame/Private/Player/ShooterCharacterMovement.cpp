// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterGame.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Player/ShooterCharacterMovement.h"

//----------------------------------------------------------------------//
// UPawnMovementComponent
//----------------------------------------------------------------------//
UShooterCharacterMovement::UShooterCharacterMovement(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

#pragma region MovementComponent Overrided Functions

FNetworkPredictionData_Client* UShooterCharacterMovement::GetPredictionData_Client() const
{
	check(PawnOwner != NULL);
	//check(PawnOwner->Role < ROLE_Authority);

	if (!ClientPredictionData)
	{
		UShooterCharacterMovement* MutableThis = const_cast<UShooterCharacterMovement*>(this);

		MutableThis->ClientPredictionData = new FNetworkPredictionData_Client_Shooter(*this);
		MutableThis->ClientPredictionData->MaxSmoothNetUpdateDist = 92.f;
		MutableThis->ClientPredictionData->NoSmoothNetUpdateDist = 140.f;
	}

	return ClientPredictionData;
}

void UShooterCharacterMovement::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);

	MaxWalkSpeedChange = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
}

void UShooterCharacterMovement::OnMovementUpdated(float DeltaTime, const FVector& OldLocation, const FVector& OldVelocity)
{
	Super::OnMovementUpdated(DeltaTime, OldLocation, OldVelocity);

	if (!CharacterOwner)
	{
		return;
	}

	//Set Max Walk Speed
	if (MaxWalkSpeedChange)
	{
		MaxWalkSpeedChange = false;
		MaxWalkSpeed = MyNewMaxWalkSpeed;
	}

	if (bWantsToTeleport)
	{
		ProcessTeleport();
		SetTeleport(false, 0);
	}

	// Wall Run
	if (IsFalling())
	{
		TryWallRun();
	}
}

float UShooterCharacterMovement::GetMaxSpeed() const
{
	float MaxSpeed = Super::GetMaxSpeed();

	const AShooterCharacter* ShooterCharacterOwner = Cast<AShooterCharacter>(PawnOwner);
	if (ShooterCharacterOwner)
	{
		if (MovementMode != MOVE_Custom) {

			if (ShooterCharacterOwner->IsTargeting())
			{
				MaxSpeed *= ShooterCharacterOwner->GetTargetingSpeedModifier();
			}
			if (ShooterCharacterOwner->IsRunning())
			{
				MaxSpeed *= ShooterCharacterOwner->GetRunningSpeedModifier();
			}
			return MaxSpeed;
		}

		switch (CustomMovementMode)
		{
		case CMOVE_WallRun:
			return MaxWallRunSpeed;
			break;
		default:
			UE_LOG(LogTemp, Warning, TEXT("Invalid Movement Mode"));
			return MaxSpeed *= ShooterCharacterOwner->GetTargetingSpeedModifier();
			break;
		}
	}
	return MaxSpeed;
}

bool UShooterCharacterMovement::CanAttemptJump() const
{
	return Super::CanAttemptJump() || IsWallRunning();
}

bool UShooterCharacterMovement::DoJump(bool bReplayingMoves)
{
	bool bWasWallRunning = IsWallRunning();
	if (Super::DoJump(bReplayingMoves))
	{
		if (bWasWallRunning)
		{
			FVector Start = UpdatedComponent->GetComponentLocation();
			FVector CastDelta = UpdatedComponent->GetRightVector() * CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleRadius() * 2;
			FVector End = bWallRunIsRight ? Start + CastDelta : Start - CastDelta;
			FHitResult WallHit;
			LineTraceIgnoringCharacter(Start, End, WallHit);
			Velocity += WallHit.Normal * WallJumpOffForce;
		}
		return true;
	}
	return false;
}

void UShooterCharacterMovement::PhysCustom(float deltaTime, int32 Iterations)
{
	Super::PhysCustom(deltaTime, Iterations);

	switch (CustomMovementMode)
	{
	case CMOVE_WallRun:
		PhysWallRun(deltaTime, Iterations);
		break;
	default:
		UE_LOG(LogTemp, Fatal, TEXT("Invalid Movement Mode"))
	}
}

void UShooterCharacterMovement::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);


	if (IsWallRunning() && GetOwnerRole() == ROLE_SimulatedProxy)
	{
		FVector Start = UpdatedComponent->GetComponentLocation();
		FVector End = Start + UpdatedComponent->GetRightVector() * CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleRadius() * 2;
		FHitResult WallHit;
		LineTraceIgnoringCharacter(Start, End, WallHit);
		bWallRunIsRight = WallHit.IsValidBlockingHit();
	}
}

#pragma endregion

bool UShooterCharacterMovement::IsCustomMovementMode(ECustomMovementMode InCustomMovementMode) const
{
	return MovementMode == MOVE_Custom && CustomMovementMode == InCustomMovementMode;
}

void UShooterCharacterMovement::LineTraceIgnoringCharacter(const FVector& Start, const FVector& End, FHitResult& OutHitResult) const
{
	FCollisionQueryParams Ignore;
	ACharacter* Owner = GetCharacterOwner();
	if (Owner) {
		TArray<AActor*> AllChildren;
		Owner->GetAllChildActors(AllChildren);
		Ignore.AddIgnoredActors(AllChildren);
		Ignore.AddIgnoredActor(Owner);
	}
	GetWorld()->LineTraceSingleByProfile(OutHitResult, Start, End, "BlockAll", Ignore);
}

#pragma region WallRunSection
bool UShooterCharacterMovement::TryWallRun()
{
	if (!IsFalling())
		return false;

	if (Velocity.SizeSquared2D() < FMath::Square(MinEnterWallRunSpeed))
		return false;
	//if player cant wall run if he is going very fast down
	//if (Velocity.Z < -MaxEnterWallRunVerticalSpeed)
	//	return false;

	FVector Start = UpdatedComponent->GetComponentLocation();

	//The character will check if wall is one capsule radius away
	FVector LeftEnd = Start - UpdatedComponent->GetRightVector() * CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleRadius() * 2;
	FVector RightEnd = Start + UpdatedComponent->GetRightVector() * CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleRadius() * 2;
	//Height from ground
	FVector DownEnd = Start + FVector::DownVector * (CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + MinEnterWallRunHeight);
	FHitResult Hit;

	//Trace Line Down to check player height
	LineTraceIgnoringCharacter(Start, DownEnd, Hit);
	if (Hit.IsValidBlockingHit())
	{
		return false;
	}

	// Left Cast
	LineTraceIgnoringCharacter(Start, LeftEnd, Hit);
	if (Hit.IsValidBlockingHit() && (Velocity | Hit.Normal) < 0)
	{
		bWallRunIsRight = false;
	} //right cast
	else
	{
		LineTraceIgnoringCharacter(Start, RightEnd, Hit);
		if (Hit.IsValidBlockingHit() && (Velocity | Hit.Normal) < 0)
		{
			bWallRunIsRight = true;
		}
		else
		{
			return false;
		}
	}

	FVector ProjectedVelocity = FVector::VectorPlaneProject(Velocity, Hit.Normal);
	if (ProjectedVelocity.SizeSquared2D() < FMath::Square(MinEnterWallRunSpeed)) return false;

	// Passed all conditions
	Velocity = ProjectedVelocity;
	Velocity.Z = FMath::Clamp(Velocity.Z, 0.f, MaxEnterWallRunVerticalSpeed);
	SetMovementMode(MOVE_Custom, CMOVE_WallRun);
	return true;
}

void UShooterCharacterMovement::PhysWallRun(float deltaTime, int32 Iterations)
{
	if (deltaTime < MIN_TICK_TIME)
	{
		return;
	}
	if (!CharacterOwner || (!CharacterOwner->Controller && !bRunPhysicsWithNoController && !HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() && (CharacterOwner->GetLocalRole() != ROLE_SimulatedProxy)))
	{
		Acceleration = FVector::ZeroVector;
		Velocity = FVector::ZeroVector;
		return;
	}

	bJustTeleported = false;
	float remainingTime = deltaTime;

	// Perform the move
	while ((remainingTime >= MIN_TICK_TIME) && (Iterations < MaxSimulationIterations) && CharacterOwner && (CharacterOwner->Controller || bRunPhysicsWithNoController || (CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)))
	{
		Iterations++;
		bJustTeleported = false;
		const float timeTick = GetSimulationTimeStep(remainingTime, Iterations);
		remainingTime -= timeTick;
		const FVector OldLocation = UpdatedComponent->GetComponentLocation();

		FVector Start = UpdatedComponent->GetComponentLocation();
		FVector CastDelta = UpdatedComponent->GetRightVector() * CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleRadius() * 2;
		FVector End = bWallRunIsRight ? Start + CastDelta : Start - CastDelta;
		FVector Direction = GetOwner()->GetActorForwardVector();

		FHitResult WallHit;
		LineTraceIgnoringCharacter(Start, End, WallHit);

		//Calculate Dot product between wall and player direction
		float SinPullAwayAngle = FMath::Sin(FMath::DegreesToRadians(WallRunPullAwayAngle));
		bool bWantsToPullAway = (WallHit.Normal | Direction) > SinPullAwayAngle;

		//is player turned away from wall
		if (!WallHit.IsValidBlockingHit() || bWantsToPullAway)
		{
			SetMovementMode(MOVE_Falling);
			StartNewPhysics(remainingTime, Iterations);
			return;
		}

		// Project Acceleration To Wall
		Acceleration = FVector::VectorPlaneProject(Acceleration, WallHit.Normal);
		Acceleration.Z = 0.f;

		// Apply acceleration
		CalcVelocity(timeTick, 0.f, false, GetMaxBrakingDeceleration());
		Velocity = FVector::VectorPlaneProject(Velocity, WallHit.Normal);
		Velocity.Z += GetGravityZ() * WallRunGravityModifier * timeTick;

		//if Velocity2D is less then MinWallSpeed or Z is less then MinVerticalSpeed
		if (Velocity.SizeSquared2D() < FMath::Square(MinEnterWallRunSpeed) || Velocity.Z < -MaxEnterWallRunVerticalSpeed)
		{
			SetMovementMode(MOVE_Falling);
			StartNewPhysics(remainingTime, Iterations);
			return;
		}

		const FVector Delta = timeTick * Velocity; // dx = v * dt
		//if it is almost at end of frame, end frame
		const bool bZeroDelta = Delta.IsNearlyZero();
		if (bZeroDelta)
		{
			remainingTime = 0.f;
		}
		else
		{
			FHitResult Hit;
			SafeMoveUpdatedComponent(Delta, UpdatedComponent->GetComponentQuat(), true, Hit);
			//Pull to Wall
			FVector WallAttractionDelta = -WallHit.Normal * WallAttractionForce * timeTick;
			SafeMoveUpdatedComponent(WallAttractionDelta, UpdatedComponent->GetComponentQuat(), true, Hit);
		}
		if (UpdatedComponent->GetComponentLocation() == OldLocation)
		{
			remainingTime = 0.f;
			break;
		}
		Velocity = (UpdatedComponent->GetComponentLocation() - OldLocation) / timeTick; // v = dx / dt
	}


	FVector Start = UpdatedComponent->GetComponentLocation();
	FVector CastDelta = UpdatedComponent->GetRightVector() * CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleRadius() * 2;
	FVector End = bWallRunIsRight ? Start + CastDelta : Start - CastDelta;
	FVector Down = Start + FVector::DownVector * (CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + MinEnterWallRunHeight * .5f);

	FHitResult FloorHit, WallHit;
	LineTraceIgnoringCharacter(Start, End, WallHit);
	LineTraceIgnoringCharacter(Start, Down, FloorHit);

	if (FloorHit.IsValidBlockingHit() || !WallHit.IsValidBlockingHit() || Velocity.SizeSquared2D() < FMath::Square(MinEnterWallRunSpeed))
	{
		SetMovementMode(MOVE_Falling);
	}
}
#pragma endregion

#pragma region WalkSpeedSection

void UShooterCharacterMovement::SetMaxWalkSpeed(float NewMaxWalkSpeed)
{
	if (PawnOwner->IsLocallyControlled())
	{
		MyNewMaxWalkSpeed = NewMaxWalkSpeed;
		Server_SetMaxWalkSpeed(NewMaxWalkSpeed);
	}

	MaxWalkSpeedChange = true;
}

bool UShooterCharacterMovement::Server_SetMaxWalkSpeed_Validate(const float NewMaxWalkSpeed)
{
	if (NewMaxWalkSpeed < 0.f || NewMaxWalkSpeed > 2000.f)
		return false;
	else
		return true;
}

void UShooterCharacterMovement::Server_SetMaxWalkSpeed_Implementation(const float NewMaxWalkSpeed)
{
	MyNewMaxWalkSpeed = NewMaxWalkSpeed;
}

#pragma endregion

#pragma region TeleportSection

void UShooterCharacterMovement::ProcessTeleport()
{
	FVector Direction = GetOwner()->GetActorForwardVector();
	FVector TargetLocation = GetOwner()->GetActorLocation() + Direction * TeleportDistance; // move 10 meters forward

	FHitResult HitResult;
	SafeMoveUpdatedComponent(TargetLocation - GetActorLocation(), GetOwner()->GetActorRotation(), true, HitResult, ETeleportType::TeleportPhysics);

	execSetTeleport(false, 0);
}

void UShooterCharacterMovement::SetTeleport(bool wantsToTeleport, float distance)
{
	if (bWantsToTeleport != wantsToTeleport || TeleportDistance != distance)
	{
		execSetTeleport(wantsToTeleport, distance);

		if (!GetOwner() || !GetPawnOwner())
			return;

		if (!GetOwner()->HasAuthority() && GetPawnOwner()->IsLocallyControlled())
		{
			ServerSetTeleportRPC(wantsToTeleport, distance);
		}
		else if (GetOwner()->HasAuthority() && !GetPawnOwner()->IsLocallyControlled())
		{
			ClientSetTeleportRPC(wantsToTeleport, distance);
		}
	}
}

void UShooterCharacterMovement::execSetTeleport(bool wantsToTeleport, float distance)
{
	bWantsToTeleport = wantsToTeleport;
	TeleportDistance = distance;
}

void UShooterCharacterMovement::ClientSetTeleportRPC_Implementation(bool wantsToTeleport, float distance)
{
	execSetTeleport(wantsToTeleport, distance);
}

bool UShooterCharacterMovement::ServerSetTeleportRPC_Validate(bool wantsToTeleport, float distance)
{
	return true;
}

void UShooterCharacterMovement::ServerSetTeleportRPC_Implementation(bool wantsToTeleport, float distance)
{
	execSetTeleport(wantsToTeleport, distance);
}
#pragma endregion

#pragma region FSavedMove
bool UShooterCharacterMovement::FSavedMove_Shooter::CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const
{
	//Cast and save to our custom FSavedMove
	FSavedMove_Shooter* NewShooterMove = static_cast<FSavedMove_Shooter*>(NewMove.Get());

	//If these two moves are not equal they cant be combined	
	if (Saved_MaxWalkSpeedChange != NewShooterMove->Saved_MaxWalkSpeedChange)
		return false;
	if (Saved_bWantsToTeleport != NewShooterMove->Saved_bWantsToTeleport)
		return false;
	if (Saved_TeleportDistance != NewShooterMove->Saved_TeleportDistance)
		return false;
	if (Saved_bWallRunIsRight != NewShooterMove->Saved_bWallRunIsRight)
		return false;

	//if they are the same let super handle
	return Super::CanCombineWith(NewMove, Character, MaxDelta);
}

void UShooterCharacterMovement::FSavedMove_Shooter::Clear()
{
	Super::Clear();

	Saved_MaxWalkSpeedChange = false;
	Saved_bWantsToTeleport = false;
	Saved_TeleportDistance = 0;
	Saved_bWallRunIsRight = 0;
}

uint8 UShooterCharacterMovement::FSavedMove_Shooter::GetCompressedFlags() const
{
	//let super run 
	uint8 Result = Super::GetCompressedFlags();
	//we have 4 available flags
	if (Saved_MaxWalkSpeedChange)
		Result |= FLAG_Custom_0;

	return Result;
}

void UShooterCharacterMovement::FSavedMove_Shooter::SetMoveFor(ACharacter* Character, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData)
{
	Super::SetMoveFor(Character, InDeltaTime, NewAccel, ClientData);

	UShooterCharacterMovement* CharacterMovement = Cast<UShooterCharacterMovement>(Character->GetCharacterMovement());

	if (CharacterMovement)
	{
		Saved_MaxWalkSpeedChange = CharacterMovement->MaxWalkSpeedChange;
		Saved_bWantsToTeleport = CharacterMovement->bWantsToTeleport;
		Saved_TeleportDistance = CharacterMovement->TeleportDistance;
		Saved_bWallRunIsRight = CharacterMovement->bWallRunIsRight;
	}
}

void UShooterCharacterMovement::FSavedMove_Shooter::PrepMoveFor(ACharacter* Character)
{
	Super::PrepMoveFor(Character);

	UShooterCharacterMovement* CharacterMovement = Cast<UShooterCharacterMovement>(Character->GetCharacterMovement());
	if (CharacterMovement)
	{
		CharacterMovement->execSetTeleport(Saved_bWantsToTeleport, Saved_TeleportDistance);
		CharacterMovement->bWallRunIsRight = Saved_bWallRunIsRight;
	}
}
#pragma endregion

#pragma region FNetworkPrediction
UShooterCharacterMovement::FNetworkPredictionData_Client_Shooter::FNetworkPredictionData_Client_Shooter(const UCharacterMovementComponent& ClientMovement)
	: Super(ClientMovement)
{
}

FSavedMovePtr UShooterCharacterMovement::FNetworkPredictionData_Client_Shooter::AllocateNewMove()
{
	return FSavedMovePtr(new FSavedMove_Shooter());
}
#pragma endregion
