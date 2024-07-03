// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "GameFramework/Actor.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "ClusterInstance.generated.h"

template <typename T>
struct FActivatedProperty
{
  TFunction<void(const T&)> Updater;
	T NextProperty;
	T LastProperty;
	float TimeProperty;
	float Progress = 0.f;
};


UCLASS(HideCategories=(Transform, Rendering, Replication, Collision, Input, Actor, LOD, Cooking))
class SYNAVISUE_API AClusterInstance : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AClusterInstance();

	//UFUNCTION(BlueprintCallable, Category = "Network")
	void TakeCommand(FJsonObject * Command);
		
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun")
	  float SecondsUntilNextPoint = 5.f;
	FVector NextDirection{0.f,0.f,-1.f};
	FQuat NextRotation;
	FQuat StartPosition;
	bool Initialized = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun")
	class UBoxComponent* SunLookAtBox;

	//TArray<FActivatedProperty> PropertyList;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;



public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	float xprogress = 0.f;

	ADirectionalLight* Sun;
	ASkyLight* Ambient;
	AVolumetricCloud* Clouds;
	ASkyAtmosphere* Atmosphere;
	class ASynavisDrone* GroundTruth;

};
