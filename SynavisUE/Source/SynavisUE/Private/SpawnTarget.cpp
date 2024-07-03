// Fill out your copyright notice in the Description page of Project Settings.


#include "SpawnTarget.h"

#include "ProceduralMeshComponent.h"

// Sets default values
ASpawnTarget::ASpawnTarget()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	ProcMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("RootComponent"));
	RootComponent = ProcMesh;
}

// Called when the game starts or when spawned
void ASpawnTarget::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void ASpawnTarget::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

