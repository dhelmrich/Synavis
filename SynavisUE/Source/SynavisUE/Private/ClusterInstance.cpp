// Fill out your copyright notice in the Description page of Project Settings.


#include "ClusterInstance.h"

#include "Components/BoxComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "SynavisDrone.h"

// Sets default values
AClusterInstance::AClusterInstance()
{
  // Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
  PrimaryActorTick.bCanEverTick = true;
  SetActorTickEnabled(true);

  RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root Component"));
  SunLookAtBox = CreateDefaultSubobject<UBoxComponent>(TEXT("Box for Sun to Look at"));
  this->SetActorEnableCollision(false);
}

void AClusterInstance::TakeCommand(FJsonObject* Command)
{
  FString CommandName;
  if (Command->TryGetStringField(TEXT("Command"), CommandName))
  {
    if (CommandName == TEXT("Sample"))
    {
      FString PropertyToSample;
      if (Command->TryGetStringField(TEXT("Target"), PropertyToSample))
      {
        // check if actor name was also provided
        FString ActorName;
        if (Command->TryGetStringField(TEXT("FromActor"), ActorName))
        {
          // check if actor exists
          TArray<AActor*> Actors;
          UGameplayStatics::GetAllActorsOfClass(GetWorld(), AActor::StaticClass(), Actors);
          // check if any of the actors match the name
          AActor* FoundActor{nullptr};
          for (auto* Actor : Actors)
          {
            // partial match is enough for us
            if (Actor->GetName().Contains(ActorName))
            {
              FoundActor = Actor;
            }
          }
          if(FoundActor)
          {
            auto* actorClass = FoundActor->GetClass();
            auto* property = actorClass->FindPropertyByName(FName(*PropertyToSample));

          }
        }
        else
        {
          // sample form Synavis Actor
          auto* drone = this->GroundTruth->GetClass();
          FProperty* ToSample = drone->FindPropertyByName(FName(*PropertyToSample));

        }
      }
    }
    else if (CommandName == TEXT("Aggregate"))
    {
      FString PropertyToAggregate;
      if (Command->TryGetStringField(TEXT("Target"), PropertyToAggregate))
      {
        FString ClassName, PropertyName;
        if (!Command->TryGetStringField(TEXT("From"), ClassName))
        {
          UE_LOG(LogActor, Error, TEXT("No class name provided for aggregation"));
          return;
        }
        if(!Command->TryGetStringField(TEXT("Property"), PropertyName))
        {
          UE_LOG(LogActor, Error ,TEXT("No property name provided for aggregation"));
          return;
        }
        if (PropertyToAggregate == TEXT("Count"))
        {
          // aggregate count
        }
        else if (PropertyToAggregate == TEXT("Sum"))
        {
          // aggregate sum
        }
        else if (PropertyToAggregate == TEXT("Average"))
        {
          // aggregate average
        }
        else if (PropertyToAggregate == TEXT("Min"))
        {
          // aggregate min
        }
        else if (PropertyToAggregate == TEXT("Max"))
        {
          // aggregate max
        }
        else if (PropertyToAggregate == TEXT("Variance"))
        {
          // aggregate variance
        }
        else if (PropertyToAggregate == TEXT("StandardDeviation"))
        {
          // aggregate standard deviation
        }
        else if (PropertyToAggregate == TEXT("Histogram"))
        {
          // aggregate histogram
        }
        else if (PropertyToAggregate == TEXT("Percentile"))
        {
          // aggregate percentile
        }
        else if (PropertyToAggregate == TEXT("Median"))
        {
          // aggregate median
        }
        else if (PropertyToAggregate == TEXT("Range"))
        {
          // aggregate range
        }
      }
    }
  }
}

// Called when the game starts or when spawned
void AClusterInstance::BeginPlay()
{
  Super::BeginPlay();
  Sun = Cast<ADirectionalLight>(UGameplayStatics::GetActorOfClass(GetWorld(),
    ADirectionalLight::StaticClass()));
  Ambient = Cast<ASkyLight>(UGameplayStatics::GetActorOfClass(GetWorld(),
    ASkyLight::StaticClass()));
  Clouds = Cast<AVolumetricCloud>(UGameplayStatics::GetActorOfClass(GetWorld(),
    AVolumetricCloud::StaticClass()));
  Atmosphere = Cast<ASkyAtmosphere>(UGameplayStatics::GetActorOfClass(GetWorld(),
    ASkyAtmosphere::StaticClass()));
  GroundTruth = Cast<ASynavisDrone>(UGameplayStatics::GetActorOfClass(GetWorld(),
    ASynavisDrone::StaticClass()));

  FString CommandLine = FCommandLine::Get();
  TArray<FString> Arguments;
  CommandLine.ParseIntoArrayWS(Arguments);

  float UniverseSize = static_cast<float>(FCString::Atoi(*FGenericPlatformMisc::GetEnvironmentVariable(TEXT("OMPI_UNIVERSE_SIZE"))));
  float CommSize = static_cast<float>(FCString::Atoi(*FGenericPlatformMisc::GetEnvironmentVariable(TEXT("OMPI_COMM_WORLD_SIZE"))));
  float RelativeRankToJob = static_cast<float>(FCString::Atoi(*FGenericPlatformMisc::GetEnvironmentVariable(TEXT("OMPI_COMM_WORLD_NODE_RANK"))));
  float MPIProcessRank = static_cast<float>(FCString::Atoi(*FGenericPlatformMisc::GetEnvironmentVariable(TEXT("OMPI_COMM_WORLD_RANK"))));

  const int MaxX = 874;
  const int MinX = -346;
  const int MinY = -848;
  const int MaxY = 102;
  const float MinZ = 130.f;
  const float MaxZ = 400.f;
  if (UniverseSize == 0 || CommSize == 0)
  {
    UE_LOG(LogTemp, Error, TEXT("MPI Comm size could not be detected, assuming we are running on a single node."));

  }
  else
  {


    auto hindex2xy = [](int hindex, int N) {
      TPair<int, int> positions[] = { TPair<int,int>(0,0),TPair<int,int>(0,1),TPair<int,int>(1,0),TPair<int,int>(1,1) };
      TPair<int, int> tmp = positions[hindex & 3];
      hindex = (hindex >> 2);
      int x = tmp.Key;
      int y = tmp.Value;
      for (int n = 4; n < N; n *= 2)
      {
        int n2 = n / 2;
        switch (hindex & 3)
        {
        case 0:
          std::swap(x, y);
          break;
        case 1:
          y += n2;
          break;
        case 2:
          x += n2;
          y += n2;
          break;
        case 3:
          int savy = y;
          y = (n2 - 1) - x;
          x = (n2 - 1) - savy;
          x += n2;
          break;
        }
        hindex >>= 2;
      }
      return TPair<int, int>(x, y);
    };
    int maxnumber = (int)CommSize;
    int N;
    for (N = 0; N * N < maxnumber; ++N);
    float hilbertminx = -1.f, hilbertminy = -1.f, hilbertmaxx = CommSize * CommSize * CommSize, hilbertmaxy = CommSize * CommSize * CommSize;
    TArray<TPair<int, int>> Points;
    for (int ts = 0; ts < N * N - 1; ++ts)
    {
      TPair<int, int> xy = hindex2xy(ts, N * N);
      Points.Add(xy);
      hilbertminx = std::min(hilbertminx, (float)xy.Key);
      hilbertminy = std::min(hilbertminy, (float)xy.Value);
      hilbertmaxx = std::max(hilbertmaxx, (float)xy.Key);
      hilbertmaxy = std::max(hilbertmaxy, (float)xy.Value);
    }
    auto x = Points[(int)MPIProcessRank].Key / hilbertmaxx;
    auto y = Points[(int)MPIProcessRank].Value / hilbertmaxy;
    GroundTruth->SetActorLocation(FVector(x * (MaxX - MinX) + MinX, y * (MaxY - MinY) + MinY, FGenericPlatformMath::FRand() * (MaxZ - MinZ) + MinZ));


    auto* AtmosphereComponent = Cast<USkyAtmosphereComponent>(Atmosphere->GetComponentByClass(USkyAtmosphereComponent::StaticClass()));
    AtmosphereComponent->SetMieScatteringScale(MPIProcessRank / CommSize);
    AtmosphereComponent->SetRayleighScatteringScale(0.2f * MPIProcessRank / CommSize);
    auto* SunComponent = Cast<UDirectionalLightComponent>(Sun->GetComponentByClass(UDirectionalLightComponent::StaticClass()));
    SunComponent->SetTemperature(6500.f);
  }

  //if(!(Sun && Ambient && Clouds && Atmosphere))
  //{
  //  this->SetActorTickEnabled(false);
  //}
}

// Called every frame
void AClusterInstance::Tick(float DeltaTime)
{
  Super::Tick(DeltaTime);
  xprogress += DeltaTime;
  if (!Initialized || xprogress > SecondsUntilNextPoint)
  {
    xprogress = 0.f;
    StartPosition = Sun->GetActorRotation().Quaternion();
    auto newpos = UKismetMathLibrary::RandomPointInBoundingBox(
      SunLookAtBox->GetComponentLocation(),
      SunLookAtBox->GetScaledBoxExtent());
    UE_LOG(LogTemp, Warning, TEXT("%f,%f,%f"), newpos.X, newpos.Y, newpos.Z);
    NextRotation = UKismetMathLibrary::FindLookAtRotation(
      RootComponent->GetComponentLocation(), newpos).Quaternion();
    Initialized = true;
  }
  Sun->SetActorRotation(FQuat::Slerp(StartPosition, NextRotation, xprogress / SecondsUntilNextPoint));
}

