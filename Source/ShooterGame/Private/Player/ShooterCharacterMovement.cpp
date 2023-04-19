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
}

float UShooterCharacterMovement::GetMaxSpeed() const
{
	float MaxSpeed = Super::GetMaxSpeed();

	const AShooterCharacter* ShooterCharacterOwner = Cast<AShooterCharacter>(PawnOwner);
	if (ShooterCharacterOwner)
	{
		if (ShooterCharacterOwner->IsTargeting())
		{
			MaxSpeed *= ShooterCharacterOwner->GetTargetingSpeedModifier();
		}
		if (ShooterCharacterOwner->IsRunning())
		{
			MaxSpeed *= ShooterCharacterOwner->GetRunningSpeedModifier();
		}
	}

	return MaxSpeed;
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

	//if they are the same let super handle
	return Super::CanCombineWith(NewMove, Character, MaxDelta);
}

void UShooterCharacterMovement::FSavedMove_Shooter::Clear()
{
	Super::Clear();

	Saved_MaxWalkSpeedChange = false;
	Saved_bWantsToTeleport = false;
	Saved_TeleportDistance = 0;
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
	}
}

void UShooterCharacterMovement::FSavedMove_Shooter::PrepMoveFor(ACharacter* Character)
{
	Super::PrepMoveFor(Character);

	UShooterCharacterMovement* CharacterMovement = Cast<UShooterCharacterMovement>(Character->GetCharacterMovement());
	if (CharacterMovement)
	{
		CharacterMovement->execSetTeleport(Saved_bWantsToTeleport, Saved_TeleportDistance);
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
