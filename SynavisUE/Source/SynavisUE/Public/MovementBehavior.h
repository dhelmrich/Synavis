// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "MovementBehavior.generated.h"

/**
 * 
 */
UCLASS()
class SYNAVISUE_API UMovementBehavior : public UObject
{
	GENERATED_BODY()

	public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
    float Speed = 100.0f;
		UFUNCTION(BlueprintCallable, Category = "Movement")
		virtual FTransform GetNextTransform(FTransform currentTransform, float deltaTime);
	
};

UCLASS()
class SYNAVISUE_API ULinearMovementBehavior : public UMovementBehavior
{
  GENERATED_BODY()

  public:

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
    FVector Direction = FVector(1.0f, 0.0f, 0.0f);

    virtual FTransform GetNextTransform(FTransform currentTransform, float deltaTime) override;
};

UCLASS()
class SYNAVISUE_API UOrbitMovementBehavior : public UMovementBehavior
{
  GENERATED_BODY()

  public:

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
    FVector Axis = FVector(0.0f, 0.0f, 1.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
    USceneComponent* Center;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
    float Distance = 100.0f;

    virtual FTransform GetNextTransform(FTransform currentTransform, float deltaTime) override;
};

UCLASS()
class SYNAVISUE_API UMomentumMovementBehavior : public UMovementBehavior
{
  GENERATED_BODY()

  public:

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
    FVector TargetLocation = FVector(0.0f, 0.0f, 0.0f);

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "View")
    float TurnWeight = 0.8f;
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "View")
    float CircleStrength = 0.02f;
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "View")
    float CircleSpeed = 0.3f;

    virtual FTransform GetNextTransform(FTransform currentTransform, float deltaTime) override;
};
