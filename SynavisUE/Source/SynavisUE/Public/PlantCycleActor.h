// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
 	

#include "Components/StaticMeshComponent.h"
#include "PlantCycleActor.generated.h"

UCLASS()
class SYNAVISUE_API APlantCycleActor : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	APlantCycleActor();

	TArray<UStaticMeshComponent*> MeshRefs;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time")
	float TimeUntilSwitch = 10.f;

	virtual void OnConstruction(const FTransform& Transform) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	float time;
	int lastindex;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

};
