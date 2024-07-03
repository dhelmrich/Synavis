// Fill out your copyright notice in the Description page of Project Settings.


#include "MovementBehavior.h"
#include <Kismet\KismetMathLibrary.h>

FTransform UMovementBehavior::GetNextTransform(FTransform currentTransform, float deltaTime)
{
  return currentTransform;
}

FTransform ULinearMovementBehavior::GetNextTransform(FTransform currentTransform, float deltaTime)
{
  FVector newLocation = currentTransform.GetLocation() + currentTransform.GetRotation().GetForwardVector() * Speed * deltaTime;
  FTransform newTransform = FTransform(currentTransform.GetRotation(), newLocation, currentTransform.GetScale3D());
  return newTransform;
}

FTransform UOrbitMovementBehavior::GetNextTransform(FTransform currentTransform, float deltaTime)
{
  FVector newLocation = currentTransform.GetLocation();
  FVector currentCenterLocation = Center->GetComponentLocation();
  // compute new location from Axis and Distance, Assume positive angle direction
  newLocation = currentCenterLocation + (newLocation - currentCenterLocation).RotateAngleAxis(Speed * deltaTime, Axis);
  FRotator newRotation = UKismetMathLibrary::FindLookAtRotation(newLocation, currentCenterLocation);
  return FTransform(newRotation, newLocation, currentTransform.GetScale3D());
}

FTransform UMomentumMovementBehavior::GetNextTransform(FTransform currentTransform, float deltaTime)
{
  // current distance to target
  FVector Distance = TargetLocation - currentTransform.GetLocation();
  Distance.Normalize();
  FVector Forward = currentTransform.GetRotation().GetForwardVector();
  // scaled by Speed
  FVector Velocity = (Forward * TurnWeight) + ((1.0 - TurnWeight) * Distance);
  Velocity *= Speed * deltaTime;
  // new rotation at (1- TurnWeight) between current and lookat
  auto newRotation = UKismetMathLibrary::FindLookAtRotation(currentTransform.GetLocation(), TargetLocation);
  newRotation = FMath::Lerp(currentTransform.GetRotation().Rotator(), newRotation, TurnWeight);
  return FTransform(newRotation, currentTransform.GetLocation() + Velocity, currentTransform.GetScale3D());
}
