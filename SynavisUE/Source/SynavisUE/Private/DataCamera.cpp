// Fill out your copyright notice in the Description page of Project Settings.


#include "DataCamera.h"

#include "SynavisDrone.h"
#include "Components/BoxComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/CameraComponent.h"
#include "Engine/Scene.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Components/BoxComponent.h"
#include "DSP/PassiveFilter.h"
#include "Kismet/KismetMathLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Blueprint/UserWidget.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Async/Async.h"
#include "InstancedFoliageActor.h"
#include "Landscape.h"
#include "Misc/FileHelper.h"
#include "Dom/JsonObject.h"
#include "Engine/LevelStreaming.h"
#include "Serialization/JsonReader.h"
#include "NiagaraActor.h"
#include "NiagaraComponent.h"

// Sets default values for this component's properties
UDataCamera::UDataCamera()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;
	static ConstructorHelpers::FObjectFinder<UMaterial> Filter(TEXT("Material'/SynavisUE/SteeringMaterial.SteeringMaterial'"));
	if (Filter.Succeeded())
	{
		PostProcessMat = Filter.Object;

	}
	static ConstructorHelpers::FObjectFinder<UTextureRenderTarget2D> InfoTarget(TEXT("TextureRenderTarget2D'/SynavisUE/SceneTarget.SceneTarget'"));
	if (InfoTarget.Succeeded())
	{
		InfoCamTarget = InfoTarget.Object;
	}
	else
	{
		UE_LOG(LogTemp,Error,TEXT("Could not load one of the textures."));
	}
	static ConstructorHelpers::FObjectFinder<UTextureRenderTarget2D> SceneTarget(TEXT("TextureRenderTarget2D'/SynavisUE/InfoTarget.InfoTarget'"));
	if (SceneTarget.Succeeded())
	{
		SceneCamTarget = SceneTarget.Object;
  }
  else
  {
    UE_LOG(LogTemp, Error, TEXT("Could not load one of the textures."));
  }

	InfoCam = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Information Camera"));
	InfoCam->SetupAttachment(this);
	SceneCam = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Rendering Camera"));
	SceneCam->SetupAttachment(this);
	InfoCam->SetRelativeLocation({0,0,0});
	SceneCam->SetRelativeLocation({0, 0, 0});
	// ...
}


void UDataCamera::EnsureDistancePreservation()
{
}

// Called when the game starts
void UDataCamera::BeginPlay()
{
	Super::BeginPlay();

	// ...


	FVector v1, v2;

	v1*v2;

	v1|v2;
	v1^v2;
	v1[0];

	FQuat q1, q2;


	FQuat::Slerp(q1,q2,0.5f);


	
}


// Called every frame
void UDataCamera::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	FrameCaptureCounter -= DeltaTime;
	FVector Distance = NextLocation - GetComponentLocation();
	if(DistanceToLandscape > 0.f)
	{
		Distance.Z = 0;
	}
	if(FGenericPlatformMath::Abs((Distance).Size()) < 50.f)
	{
	  NextLocation = UKismetMathLibrary::RandomPointInBoundingBox(Flyspace->GetComponentLocation(),Flyspace->GetScaledBoxExtent());
		if(GEngine)
		{
			//GEngine->AddOnScreenDebugMessage(10,30.f,FColor::Red,FString::Printf(
		  //TEXT("L:(%d,%d,%d) - N:(%d,%d,%d) - M:%d/%d"),\
		  //GetActorLocation().X,GetActorLocation().Y,GetActorLocation().Z, \
			//NextLocation.X,NextLocation.Y,NextLocation.Z,MeanVelocityLength,FGenericPlatformMath::Abs((GetActorLocation() - NextLocation).Size())));
	  }
	}
	else
	{
		xprogress+= DeltaTime;
		if(xprogress > 10000.f)
		  xprogress = 0;
		FVector Noise = {FGenericPlatformMath::Sin(xprogress*CircleSpeed),FGenericPlatformMath::Cos(xprogress*CircleSpeed),- FGenericPlatformMath::Sin(xprogress*CircleSpeed)};
		Noise = (Noise / Noise.Size()) * CircleStrength;
		Distance = Distance / Distance.Size();
		Velocity = (Velocity * TurnWeight) + (Distance*(1.f-TurnWeight)) + Noise;
		Velocity = Velocity / Velocity.Size();
	  SetWorldLocation(GetComponentLocation() +  (Velocity*DeltaTime*MaxVelocity));
		if(!EditorOrientedCamera)
		  SetWorldRotation(Velocity.ToOrientationRotator());
		if(DistanceToLandscape > 0.f)
		{
		  EnsureDistancePreservation();
		}
	}

	// prepare texture for storage
	if(FrameCaptureCounter <= 0.f)
	{
		auto *irtarget = InfoCam->TextureTarget->GameThread_GetRenderTargetResource();
		auto *srtarget = SceneCam->TextureTarget->GameThread_GetRenderTargetResource();
		// Make async semaphore that could potentially halt the execution until the images are processed
		// also check if even needed.
    
		FrameCaptureCounter = FrameCaptureTime;
		UE_LOG(LogTemp, Display, TEXT("Setting Frame back to %f"),FrameCaptureTime);
	}

	// ...
}

void UDataCamera::OnComponentCreated()
{
	SceneCam->RegisterComponent();
	InfoCam->RegisterComponent();
	Flyspace->RegisterComponent();
  Super::OnComponentCreated();
}

