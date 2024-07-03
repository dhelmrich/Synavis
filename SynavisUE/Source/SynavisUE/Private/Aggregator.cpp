// Fill out your copyright notice in the Description page of Project Settings.


#include "Aggregator.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/GameplayStatics.h"

// Sets default values
AAggregator::AAggregator()
{
  // Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
  PrimaryActorTick.bCanEverTick = true;

}

// Called when the game starts or when spawned
void AAggregator::BeginPlay()
{
  Super::BeginPlay();
  // get resolution of render output
  auto viewportSize = FVector2D(0, 0);
  GEngine->GameViewport->GetViewportSize(viewportSize);
  // create render target
  auto renderTarget = UTexture2D::CreateTransient(viewportSize.X, viewportSize.Y);
  renderTarget->UpdateResource();
  // create scene capture
}

// Called every frame
void AAggregator::Tick(float DeltaTime)
{
  Super::Tick(DeltaTime);

}

void AAggregator::OutlineAllActors()
{
  TArray< AActor* > FoundActors;
  TArray<TPair<FVector2D,FVector2D>> ScreenBounds;
  UGameplayStatics::GetAllActorsOfClass(GetWorld(), AActor::StaticClass(), FoundActors);
  for (auto actor : FoundActors)
  {
    // find out if actor was recently rendered
    if (actor->WasRecentlyRendered(GetWorld()->GetDeltaSeconds()))
    {
      // get bounds
      FVector origin;
      FVector extent;
      actor->GetActorBounds(false, origin, extent);
      // construct outer box points

      // convert to screen space
      FVector2D screenMinimum;
      FVector2D screenMaximum;
      for (auto position : {
        origin + FVector(-extent.X, -extent.Y, -extent.Z),
        origin + FVector(-extent.X, -extent.Y, extent.Z),
        origin + FVector(-extent.X, extent.Y, -extent.Z),
        origin + FVector(-extent.X, extent.Y, extent.Z),
        origin + FVector(extent.X, -extent.Y, -extent.Z),
        origin + FVector(extent.X, -extent.Y, extent.Z),
        origin + FVector(extent.X, extent.Y, -extent.Z),
        origin + FVector(extent.X, extent.Y, extent.Z)
      })
      {
        // get screen position
        FVector2D screenPosition;
        if (UGameplayStatics::ProjectWorldToScreen(GetWorld()->GetFirstPlayerController(), position, screenPosition))
        {
                   // update screen bounds
          screenMinimum.X = FMath::Min(screenMinimum.X, screenPosition.X);
          screenMinimum.Y = FMath::Min(screenMinimum.Y, screenPosition.Y);
          screenMaximum.X = FMath::Max(screenMaximum.X, screenPosition.X);
          screenMaximum.Y = FMath::Max(screenMaximum.Y, screenPosition.Y);
        }
      }
    }
  }
}

