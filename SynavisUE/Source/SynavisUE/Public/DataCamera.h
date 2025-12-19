// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "DataCamera.generated.h"

// Forward declarations
class UCameraComponent;
class USceneCaptureComponent2D;
class UBoxComponent;
class UTextureRenderTarget2D;
class FRenderTextureTargetResource;
UCLASS(Blueprintable)
class  ADataCamera : public APawn
{
	GENERATED_BODY()

public:	
	// Sets default values for this pawn's properties
	ADataCamera();


	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "View")
	USceneCaptureComponent2D* InfoCam;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "View")
	USceneCaptureComponent2D* SceneCam;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "View")
	UBoxComponent* Flyspace;
		
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "View")
	UTextureRenderTarget2D* InfoCamTarget;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "View")
	UTextureRenderTarget2D* SceneCamTarget;
		
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "View")
	float MaxVelocity = 10.f;
		
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "View")
	float DistanceToLandscape = -1.f;
		
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "View")
	float TurnWeight = 0.8f;
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "View")
	float CircleStrength = 0.02f;
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "View")
	float CircleSpeed = 0.3f;

	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Time")
	float FrameCaptureTime = 10.f;

	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "View")
	int RenderMode = 2;
	
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "View")
	float DistanceScale = 2000.f;
	
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "View")
	float BlackDistance = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "View")
	FVector BinScale{};

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "View")
	bool LockNavigation = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "View")
	bool EditorOrientedCamera = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "View")
	bool Rain = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "View")
	int RainParticlesPerSecond{10000};

  bool bIsStreaming = false;

	FVector NextLocation;
	FVector Velocity;
	FVector SpaceOrigin;
	FVector SpaceExtend;
	float MeanVelocityLength = 0;
	uint64_t SampleSize = 0;
	UMaterial* PostProcessMat;
	float LowestLandscapeBound;
	class UMaterialInstanceDynamic* CallibratedPostprocess;

	float xprogress = 0.f;
	float FrameCaptureCounter;
	
	FCollisionObjectQueryParams ActorFilter;
	FCollisionQueryParams CollisionFilter;

	UFUNCTION(BlueprintCallable)
	void EnsureDistancePreservation();


protected:
	// Called when the game starts
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

private:
	// Add private members for your streaming implementation (e.g., a pointer to your streamer logic)
	// Example:
	// TSharedPtr<FMyCustomStreamer> Streamer;
};
