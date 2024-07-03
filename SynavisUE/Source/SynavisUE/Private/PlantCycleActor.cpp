// Fill out your copyright notice in the Description page of Project Settings.


#include "PlantCycleActor.h"
 	

#include "Kismet/KismetArrayLibrary.h"

// Sets default values
APlantCycleActor::APlantCycleActor()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root Component"));;
	RootComponent->SetRelativeScale3D({0.1f,0.1f,0.1f});
}

void APlantCycleActor::OnConstruction(const FTransform& Transform)
{
  Super::OnConstruction(Transform);
}

#if WITH_EDITOR
void APlantCycleActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
  Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

// Called when the game starts or when spawned
void APlantCycleActor::BeginPlay()
{
	Super::BeginPlay();
	TArray<UActorComponent*> precastrefs;
  GetComponents(UStaticMeshComponent::StaticClass(),precastrefs);
	for(auto* ref : precastrefs)
	{
	  auto* castref = Cast<UStaticMeshComponent>(ref);
		if(castref)
		{
			castref->SetRenderCustomDepth(true);
			//castref->AttachToComponent(RootComponent,EAttachmentRule::KeepWorld);
			castref->CustomDepthStencilValue = 1;
		  MeshRefs.Add(castref);
		}
	}
	lastindex = static_cast<int>(FGenericPlatformMath::SRand()*static_cast<float>(MeshRefs.Num()));
	for(auto* mesh : MeshRefs) mesh->SetVisibility(false);
	MeshRefs[lastindex]->SetVisibility(true);
}

// Called every frame
void APlantCycleActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	time += 2.f*DeltaTime;
	//this->AddActorWorldRotation(FRotator(0.f,0.f,DeltaTime));
	RootComponent->SetWorldRotation(FRotator(0.f,time,0.f));
	if(time > TimeUntilSwitch)
	{
	  MeshRefs[lastindex]->SetVisibility(false);
		lastindex = static_cast<int>(FGenericPlatformMath::SRand()*static_cast<float>(MeshRefs.Num()));
	  MeshRefs[lastindex]->SetVisibility(true);
		time = 0.f;
	}
}

