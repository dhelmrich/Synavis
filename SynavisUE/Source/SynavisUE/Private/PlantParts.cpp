// Fill out your copyright notice in the Description page of Project Settings.


#include "PlantParts.h"
#include "ProceduralMeshComponent.h"

// Sets default values
APlantParts::APlantParts()
{
  // Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
  PrimaryActorTick.bCanEverTick = true;
  // retrieve all the materials
  //static ConstructorHelpers::FObjectFinder<UMaterial> StemMaterialBaseFinder(TEXT("/Script/Engine.Material'/Game/CPlantBox/Stem.Stem'"));
  //if (StemMaterialBaseFinder.Succeeded())
  //{
  //  StemMaterialBase = StemMaterialBaseFinder.Object;
  //}
  //static ConstructorHelpers::FObjectFinder<UMaterial> LeafMaterialBaseFinder(TEXT("/Script/Engine.Material'/Game/CPlantBox/Leaf.Leaf'"));
  //if (LeafMaterialBaseFinder.Succeeded())
  //{
  //  LeafMaterialBase = LeafMaterialBaseFinder.Object;
  //}
  //static ConstructorHelpers::FObjectFinder<UMaterial> RootMaterialBaseFinder(TEXT("/Script/Engine.Material'/Game/CPlantBox/Root.Root'"));
  //if (RootMaterialBaseFinder.Succeeded())
  //{
  //  RootMaterialBase = RootMaterialBaseFinder.Object;
  //}
  RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));
}

void APlantParts::Clear()
{
  Mesh->ClearAllMeshSections();
}

void APlantParts::AddMesh(const TArray<FVector>& Points,
  const TArray<FVector>& Normals,
  const TArray<int>& Triangles,
  const TArray<FVector2D>& UV,
  const TArray<FColor>& VertexColors,
  const TArray<FProcMeshTangent>& Tangents,
  FString MeshRole)
{
  auto MeshSection = Mesh->GetNumSections();
  Mesh->CreateMeshSection(MeshSection,
    Points,
    Triangles,
    Normals,
    UV, VertexColors,
    Tangents, true);
  if (MeshRole == "Stem")
  {
    Mesh->SetMaterial(MeshSection, StemMaterial);
  }
  else if (MeshRole == "Leaf")
  {
    Mesh->SetMaterial(MeshSection, LeafMaterial);
  }
  else if (MeshRole == "Root")
  {
    Mesh->SetMaterial(MeshSection, RootMaterial);
  }
}

void APlantParts::VaryProperty(FString ValRole, FString PropertyName, float Value, float StdDev)
{
  float RandomValue{};
  if (StdDev < 1e-6)
  {
    RandomValue = 0.0;
  }
  else
    RandomValue = FMath::FRandRange(-StdDev, StdDev);
  if (ValRole == "Stem")
  {
    StemMaterial->SetScalarParameterValue(FName(PropertyName), Value + RandomValue);
  }
  else if (ValRole == "Leaf")
  {
    LeafMaterial->SetScalarParameterValue(FName(PropertyName), Value + RandomValue);
  }
  else if (ValRole == "Root")
  {
    RootMaterial->SetScalarParameterValue(FName(PropertyName), Value + RandomValue);
  }
}

// Called when the game starts or when spawned
void APlantParts::BeginPlay()
{
  Super::BeginPlay();
  // creating the dynamic material instances
  StemMaterial = UMaterialInstanceDynamic::Create(StemMaterialBase, this);
  LeafMaterial = UMaterialInstanceDynamic::Create(LeafMaterialBase, this);
  RootMaterial = UMaterialInstanceDynamic::Create(RootMaterialBase, this);
}

// Called every frame
void APlantParts::Tick(float DeltaTime)
{
  Super::Tick(DeltaTime);

}

