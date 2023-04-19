// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * Movement component meant for use with Pawns.
 */

#pragma once


#include "ShooterCharacterMovement.generated.h"

UENUM(BlueprintType)
enum ECustomMovementMode
{
	CMOVE_None			UMETA(Hidden),
	CMOVE_WallRun		UMETA(DisplayName = "Wall Run"),
	CMOVE_MAX			UMETA(Hidden),
};

UCLASS()
class UShooterCharacterMovement : public UCharacterMovementComponent
{
	GENERATED_UCLASS_BODY()

		class FSavedMove_Shooter : public FSavedMove_Character
	{
	public:
		typedef FSavedMove_Character Super;

		virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const override;
		virtual void Clear() override;
		virtual uint8 GetCompressedFlags() const override;
		virtual void SetMoveFor(ACharacter* Character, float InDeltaTime, FVector const& NewAccel, class FNetworkPredictionData_Client_Character& ClientData) override;
		virtual void PrepMoveFor(class ACharacter* Character) override;

		/*Saved Variables*/
		//Walk Speed Update
		uint8 Saved_MaxWalkSpeedChange : 1;


		//teleport
		uint8 Saved_bWantsToTeleport : 1;
		float Saved_TeleportDistance;

		//wall jump
		uint8 Saved_bWallRunIsRight : 1;
	};

	class FNetworkPredictionData_Client_Shooter : public FNetworkPredictionData_Client_Character
	{
	public:
		FNetworkPredictionData_Client_Shooter(const UCharacterMovementComponent& ClientMovement);

		typedef FNetworkPredictionData_Client_Character Super;

		virtual FSavedMovePtr AllocateNewMove() override;
	};

public:
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	virtual float GetMaxSpeed() const override;
	virtual bool CanAttemptJump() const override;
	virtual bool DoJump(bool bReplayingMoves) override;

protected:
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	virtual void OnMovementUpdated(float DeltaSeconds, const FVector& OldLocation, const FVector& OldVelocity) override;
	virtual void PhysCustom(float deltaTime, int32 Iterations) override;
	virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;


#pragma region SetMaxWalkSpeedSection
private:
	float MaxWalkSpeedChange;
	float MyNewMaxWalkSpeed = 1500;
public:
	UFUNCTION(BlueprintCallable)
		void SetMaxWalkSpeed(float NewMaxWalkSpeed);
	UFUNCTION(Unreliable, Server, WithValidation)
		void Server_SetMaxWalkSpeed(const float NewMaxWalkSpeed);
#pragma endregion

#pragma region TeleportSection
public:
	bool bWantsToTeleport = false;
	UPROPERTY(Category = "Custom Character Settings", EditAnywhere, BlueprintReadWrite)
		float TeleportDistance = 1000;
private:
	void execSetTeleport(bool wantsToTeleport, float distance);
	void ProcessTeleport();
public:
	UFUNCTION(BlueprintCallable)
		void SetTeleport(bool wantsToTeleport, float distance);
	/*RPC Functions*/
	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable)
		void ServerSetTeleportRPC(bool wantsToTeleport, float distance);
	UFUNCTION(Client, Reliable, BlueprintCallable)
		void ClientSetTeleportRPC(bool wantsToTeleport, float distance);
#pragma endregion


#pragma region WallRunningAndJumpingSection
	UPROPERTY(Category = "Custom Character Settings", EditAnywhere, BlueprintReadWrite)
		float MinEnterWallRunSpeed = 200.f;
	UPROPERTY(Category = "Custom Character Settings", EditAnywhere, BlueprintReadWrite)
		float MaxWallRunSpeed = 800.f;
	UPROPERTY(Category = "Custom Character Settings", EditAnywhere, BlueprintReadWrite)
		float MaxEnterWallRunVerticalSpeed = 200.f;
	UPROPERTY(Category = "Custom Character Settings", EditAnywhere, BlueprintReadWrite)
		float MinEnterWallRunHeight = 100.f;
	UPROPERTY(Category = "Custom Character Settings", EditAnywhere, BlueprintReadWrite)
		float WallRunPullAwayAngle = 30.f;
	UPROPERTY(Category = "Custom Character Settings", EditAnywhere, BlueprintReadWrite)
		float WallAttractionForce = 200.f;
	UPROPERTY(Category = "Custom Character Settings", EditAnywhere, BlueprintReadWrite)
		float WallJumpOffForce = 500.f;
	UPROPERTY(Category = "Custom Character Settings", EditAnywhere, BlueprintReadWrite)
		float WallRunGravityModifier = 0.2f;

	bool bWallRunIsRight;

private:
	bool TryWallRun();
	void PhysWallRun(float deltaTime, int32 Iterations);

	UFUNCTION(BlueprintPure)
		bool IsCustomMovementMode(ECustomMovementMode InCustomMovementMode) const;
	UFUNCTION(BlueprintPure)
		bool IsWallRunning() const { return IsCustomMovementMode(CMOVE_WallRun); }
	UFUNCTION(BlueprintPure)
		bool WallRunningIsRight() const { return bWallRunIsRight; }
#pragma endregion

	//Helpers
private:
	void LineTraceIgnoringCharacter(const FVector& Start, const FVector& End, FHitResult& OutHitResult) const;

};

