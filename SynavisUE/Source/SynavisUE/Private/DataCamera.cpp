// Fill out your copeyright notice in the Description page of Project Settings.

#include "DataCamera.h"
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
#include "Engine/GameEngine.h"
#include "Slate/SceneViewport.h"
#include "UObject/ConstructorHelpers.h"

// Sets default values for this component's properties
ADataCamera::ADataCamera()
{
  SceneCam = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Rendering Camera"));
  RootComponent = SceneCam;
  InfoCam = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Information Camera"));
  InfoCam->SetupAttachment(RootComponent);

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
    UE_LOG(LogTemp, Error, TEXT("Could not load one of the textures."));
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

  InfoCam->SetRelativeLocation({ 0,0,0 });
  SceneCam->SetRelativeLocation({ 0, 0, 0 });

}


// Called when the game starts
void ADataCamera::BeginPlay()
{
  Super::BeginPlay();
  // Optionally start streaming here, or call StartStreaming() from elsewhere
}

// Called every frame
void ADataCamera::Tick(float DeltaTime)
{
  Super::Tick(DeltaTime);
  FrameCaptureCounter -= DeltaTime;
  FVector Distance = NextLocation - GetActorLocation();
  if (DistanceToLandscape > 0.f)
  {
    Distance.Z = 0;
  }
  if (FGenericPlatformMath::Abs((Distance).Size()) < 50.f)
  {
    NextLocation = UKismetMathLibrary::RandomPointInBoundingBox(Flyspace->GetComponentLocation(), Flyspace->GetScaledBoxExtent());
    if (GEngine)
    {
      //GEngine->AddOnScreenDebugMessage(10,30.f,FColor::Red,FString::Printf(
      //TEXT("L:(%d,%d,%d) - N:(%d,%d,%d) - M:%d/%d"),\
			//GetActorLocation().X,GetActorLocation().Y,GetActorLocation().Z, \
			//NextLocation.X,NextLocation.Y,NextLocation.Z,MeanVelocityLength,FGenericPlatformMath::Abs((GetActorLocation() - NextLocation).Size())));
    }
  }
  else
  {
    xprogress += DeltaTime;
    if (xprogress > 10000.f)
      xprogress = 0;
    FVector Noise = { FGenericPlatformMath::Sin(xprogress * CircleSpeed),FGenericPlatformMath::Cos(xprogress * CircleSpeed),-FGenericPlatformMath::Sin(xprogress * CircleSpeed) };
    Noise = (Noise / Noise.Size()) * CircleStrength;
    Distance = Distance / Distance.Size();
    Velocity = (Velocity * TurnWeight) + (Distance * (1.f - TurnWeight)) + Noise;
    Velocity = Velocity / Velocity.Size();
    SetActorLocation(GetActorLocation() + (Velocity * DeltaTime * MaxVelocity));
    if (!EditorOrientedCamera)
      SetActorRotation(Velocity.ToOrientationRotator());
    if (DistanceToLandscape > 0.f)
    {
      EnsureDistancePreservation();
    }
  }

  // prepare texture for storage
  if (FrameCaptureCounter <= 0.f)
  {
    auto* irtarget = InfoCam->TextureTarget->GameThread_GetRenderTargetResource();
    auto* srtarget = SceneCam->TextureTarget->GameThread_GetRenderTargetResource();
    // Make async semaphore that could potentially halt the execution until the images are processed
    // also check if even needed.

    FrameCaptureCounter = FrameCaptureTime;
    UE_LOG(LogTemp, Display, TEXT("Setting Frame back to %f"), FrameCaptureTime);
  }

  // ...
}

void ADataCamera::EnsureDistancePreservation()
{
  if (!Flyspace) return;

  FVector ActorLocation = GetActorLocation();
  FVector TraceStart = ActorLocation;
  FVector TraceEnd = ActorLocation - FVector(0, 0, 10000.f);

  FHitResult HitResult;
  FCollisionQueryParams Params;
  Params.AddIgnoredActor(this);

  bool bHit = GetWorld()->LineTraceSingleByChannel(
    HitResult,
    TraceStart,
    TraceEnd,
    ECC_Visibility,
    Params
  );

  if (bHit)
  {
    float CurrentDistance = (ActorLocation - HitResult.ImpactPoint).Size();
    float Delta = DistanceToLandscape - CurrentDistance;
    if (FMath::Abs(Delta) > KINDA_SMALL_NUMBER)
    {
      SetActorLocation(ActorLocation + FVector(0, 0, Delta));
    }
  }
}

