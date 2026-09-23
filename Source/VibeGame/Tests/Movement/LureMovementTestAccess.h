// Lure: test-only access to ULureCharacterMovementComponent internals (the component declares this struct a friend).
// Lets automation tests drive the engine's network path in a standalone world (PerformMovement, MoveAutonomous, the
// replay after a correction, the reply container) and read the climb state. Include it from test files only.

#pragma once

#include "CoreMinimal.h"
#include "Character/LureCharacterMovementComponent.h"

struct FLureMovementTestAccess
{
	/** A new move on the owning client (ReplicateMoveToServer), or a locally controlled authority's move. */
	static void PerformMovement(ULureCharacterMovementComponent& Movement, float DeltaTime)
	{
		Movement.PerformMovement(DeltaTime);
	}

	/** The server's handling of one client move (ServerMove_PerformMovement), and the client's handling of each replayed move. */
	static void MoveAutonomous(ULureCharacterMovementComponent& Movement, float TimeStamp, float DeltaTime, uint8 CompressedFlags, const FVector& Acceleration)
	{
		Movement.MoveAutonomous(TimeStamp, DeltaTime, CompressedFlags, Acceleration);
	}

	/** The owning client's replay of its unacknowledged moves after a correction (TickComponent calls it when bUpdatePosition is set). */
	static bool ReplayUnacknowledgedMoves(ULureCharacterMovementComponent& Movement)
	{
		return Movement.ClientUpdatePositionAfterServerUpdate();
	}

	static void SetAcceleration(ULureCharacterMovementComponent& Movement, const FVector& Acceleration)
	{
		Movement.Acceleration = Acceleration;
	}

	/** The component's reply container (what MoveResponsePacked_ClientReceive deserializes into and ServerSendMoveResponse fills). */
	static FLureMoveResponseDataContainer& ResponseData(ULureCharacterMovementComponent& Movement)
	{
		return Movement.LureMoveResponseData;
	}

	static bool UsesLureResponseData(const ULureCharacterMovementComponent& Movement)
	{
		return &Movement.GetMoveResponseDataContainer() == &Movement.LureMoveResponseData;
	}

	static bool HasClimbPlan(const ULureCharacterMovementComponent& Movement)
	{
		return Movement.bHasClimbPlan;
	}

	static const FLureClimbPlan& ClimbPlan(const ULureCharacterMovementComponent& Movement)
	{
		return Movement.ClimbPlan;
	}

	static bool IsClimbOutRequested(const ULureCharacterMovementComponent& Movement)
	{
		return Movement.bClimbOutRequested;
	}

	static void SetTakeoffFeetHeight(ULureCharacterMovementComponent& Movement, float Height)
	{
		Movement.TakeoffFeetHeight = Height;
	}
};
