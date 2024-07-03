// Copyright Dirk Norbert Helmrich, 2023

#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "GameFramework/Actor.h"
#include "WorldSpawner.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSpawnProcMesh, UProceduralMeshComponent*, ProcMesh);


USTRUCT(BlueprintType)
struct FObjectSpawnInstance
{
  GENERATED_BODY()
  FString Name, ClassName;
  FVector Location;
  UObject* Object;
  UClass* Class;
  int Identity;
};

class UProceduralMeshComponent;
class UMaterial;
class UMaterialInstanceDynamic;
struct FStreamableHandle;
class ASynavisDrone;

UCLASS()
class SYNAVISUE_API AWorldSpawner : public AActor
{
  GENERATED_BODY()

public:
  // Sets default values for this actor's properties
  AWorldSpawner();

  UFUNCTION(BlueprintCallable, Category = "Field", meta = (AutoCreateRefTerm = "Tangents, TexCoords"))
  AActor* SpawnProcMesh(TArray<FVector> Points, TArray<FVector> Normals, TArray<int> Triangles,
    TArray<float> Scalars, float Min, float Max,
    TArray<FVector2D> TexCoords, TArray<FProcMeshTangent> Tangents);

  UPROPERTY(EditAnywhere, Category = "Field")
  class UBoxComponent* CropField;

  UPROPERTY(EditAnywhere, Category = "Coupling")
  UMaterial* DefaultMaterial;

  UPROPERTY(BlueprintReadOnly, Category = "Management")
  TArray<FObjectSpawnInstance> SpawnedObjects;

  UPROPERTY(BlueprintReadOnly, Category = "Management")
  bool AllowDefaultParameters = false;

  UFUNCTION(BlueprintPure, Category = "Coupling")
  TArray<FString> GetNamesOfSpawnableTypes();

  UPROPERTY(BlueprintAssignable, Category = "Management")
  FSpawnProcMesh OnSpawnProcMesh;

  UFUNCTION()
  void ReceiveStreamingCommunicatorRef();

  FString PrepareContainerGeometry(TSharedPtr<FJsonObject> Description);

  UFUNCTION()
  AActor* GetHeldActor() { return this->HeldActor; }
  UFUNCTION()
  USceneComponent* GetHeldComponent() { return this->HeldComponent; }

  //FString GetUniqueName(FString BaseName);

  UPROPERTY()
  TArray<USceneComponent*> SubComponents;

  UPROPERTY(EditAnywhere)
    double RandomScaleMin = 0.9;

UPROPERTY(EditAnywhere)
double RandomScaleMax = 1.1;

  // a function that returns a StaticClass from a name
  UClass* GetClassFromName(FString ClassName);

  UTexture2D* CreateTexture2DFromData(uint8* Data, uint64 Size, int Width, int Height);
  UMaterialInstanceDynamic* GenerateInstanceFromName(FString InstanceName, bool NewOnly = true);

  // This function spawns a pre-registered object from a JSON description
  // The description must contain a "ClassName" field that contains the name of the class to spawn
  // The description must adhere with the internal spawn parameter map
  // @return The name of the spawned object, this should also just appear in the scene.
  FString SpawnObject(TSharedPtr<FJsonObject> Description);

  //UPROPERTY(EditAnywhere, Category = "Field")
  UPROPERTY()
  TMap<FString, UMaterialInstanceDynamic*> MaterialInstances;

  void ReceiveStreamingCommunicatorRef(ASynavisDrone* inDroneRef);


  const FJsonObject* GetAssetCacheTemp() const { return AssetCache.Get(); }

  UFUNCTION(BlueprintCallable)
    AActor* SampleBoxMesh();

  UFUNCTION(BlueprintCallable)
    FTransform GetTransformInCropField();

protected:
  // Called when the game starts or when spawned
  virtual void BeginPlay() override;

  UPROPERTY()
  ASynavisDrone* DroneRef;
  void MessageToClient(FString Message);

  TSharedPtr<FJsonObject> AssetCache;

  TArray<TSharedPtr<FStreamableHandle>> StreamableHandles;

  UPROPERTY()
  AActor* HeldActor;
  UPROPERTY()
  USceneComponent* HeldComponent;

public:
  // Called every frame
  virtual void Tick(float DeltaTime) override;

};
