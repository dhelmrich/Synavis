// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SpawnTarget.generated.h"

UCLASS()
class SYNAVISUE_API ASpawnTarget : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ASpawnTarget();

	UPROPERTY(BlueprintReadOnly, Category = "Management")
	class UProceduralMeshComponent* ProcMesh;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

};
