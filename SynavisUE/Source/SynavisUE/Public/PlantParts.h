// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PlantParts.generated.h"

UCLASS()
class SYNAVISUE_API APlantParts : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	APlantParts();

	void Clear();

	UFUNCTION(BlueprintCallable, Category = "Stem")
	int32 AddMesh(const TArray<FVector>& Points,
	                 const TArray<FVector>& Normals,
									 const TArray<int>& Triangles,
									 const TArray<FVector2D>& UV,
									 const TArray<FColor>& VertexColors,
									 const TArray<FProcMeshTangent>& Tangents,
									 int32 organType = 3,
                   int slot = -1);

	void VaryProperty(FString ValRole, FString PropertyName, float Value, float StdDev = 0.0);

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category= "Actor")
	class UProceduralMeshComponent* Mesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Stem")
	UMaterialInstanceDynamic* StemMaterial;
	UMaterial* StemMaterialBase;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Leaf")
	UMaterialInstanceDynamic* LeafMaterial;
	UMaterial* LeafMaterialBase;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Root")
	UMaterialInstanceDynamic* RootMaterial;
	UMaterial* RootMaterialBase;

};
