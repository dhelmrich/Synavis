// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"


#include "HAL/RunnableThread.h"
#include "HAL/Runnable.h"
#include "Containers/Map.h"
#include "PixelStreamingInputComponent.h"
#include "ProceduralMeshComponent.h"
#include "GenericPlatform/GenericPlatformProcess.h"

#include "SynavisDrone.generated.h"

// callback definition for blueprints
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPixelStreamingResponseCallback, FString, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPixelStreamingDataCallback, TArray<int>, Data);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_SixParams(FPixelStreamingReceptionCallback, const TArray<FVector>&, Points, const TArray<FVector>&, Normals, const TArray<int>&, Triangles, const TArray<FVector2D>&, TexCoords, const TArray<float>&, Values, float, Time);

// forward

class UCameraComponent;
class USceneCaptureComponent2D;
class UBoxComponent;
class UTextureRenderTarget2D;
class AWorldSpawner;

UENUM(BlueprintType)
enum class EBlueprintSignalling : uint8
{
  SwitchToSceneCam = 0,
  SwitchToInfoCam,
  SwitchToBothCams,
};
UENUM(BlueprintType)
enum class EDataTypeIndicator : uint8
{
  Float = 0,
  Int,
  Bool,
  String,
  Vector,
  Rotator,
  Transform,
  None
};

static inline FString PrintFormattedTransform(UObject* Object)
{
  USceneComponent* ComponentIdentity = Cast<USceneComponent>(Object);
  AActor* ActorIdentity = Cast<AActor>(Object);
  if (ActorIdentity)
  {
    ComponentIdentity = ActorIdentity->GetRootComponent();
  }
  if (!ComponentIdentity)
  {
    return FString("");
  }
  else
  {
    const FTransform& Transform = ComponentIdentity->GetComponentTransform();
    return FString::Printf(TEXT("L{%s}R{%s}S{%s}"), *Transform.GetLocation().ToString(), *Transform.GetRotation().ToString(), *Transform.GetScale3D().ToString());

  }
}



USTRUCT(BlueprintType)
struct FTransmissionTarget
{
  GENERATED_BODY()
  UObject* Object;
  FProperty* Property;
  EDataTypeIndicator DataType;
  FString Name;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBlueprintSignallingCallback, EBlueprintSignalling, Signal);

UCLASS(Config = Game)
class SYNAVISUE_API ASynavisDrone : public AActor
{
  GENERATED_BODY()

public:
  UFUNCTION(BlueprintCallable, Category = "Network")
  void ParseInput(FString Descriptor);

  void JsonCommand(TSharedPtr<FJsonObject> Jason, double start = -1);

  void ParseGeometryFromJson(TSharedPtr<FJsonObject> Jason);
  // Sets default values for this actor's properties
  ASynavisDrone();

  UPROPERTY(BlueprintAssignable, Category = "Network")
    FPixelStreamingReceptionCallback OnPixelStreamingGeometry;

  UPROPERTY(BlueprintAssignable, Category = "Network")
    FPixelStreamingResponseCallback OnPixelStreamingResponse;

  UPROPERTY(BlueprintAssignable, Category = "Coupling")
    FBlueprintSignallingCallback OnBlueprintSignalling;

  UFUNCTION(BlueprintCallable, Category = "Network")
    void SendResponse(FString Message, double StartTime = -1.0, int PlayerID = -1);

  UFUNCTION(BlueprintCallable, Category = "Network")
    void SendError(FString Message);

  UFUNCTION(BlueprintCallable, Category = "Network")
    void ResetSynavisState();

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "View")
    USceneCaptureComponent2D* InfoCam;
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "View")
    USceneCaptureComponent2D* SceneCam;

  UFUNCTION(BlueprintCallable, Category = "Actor")
    void LoadFromJSON(FString JasonString = "");
  void ApplyFromJSON(TSharedPtr<FJsonObject> Jason);	

  UFUNCTION(BlueprintCallable, Category = "Actor")
    FTransform FindGoodTransformBelowDrone();

  UFUNCTION(BlueprintCallable, Category = "Actor")
    FString ListObjectPropertiesAsJSON(UObject* Object);

  UFUNCTION(BlueprintCallable, Category = "View")
    void StoreCameraBuffer(int BufferNumber, FString NameBase);

  void ApplyJSONToObject(UObject* Object, FJsonObject* JSON);

  UObject* GetObjectFromJSON(TSharedPtr<FJsonObject> JSON);

  FString GetJSONFromObjectProperty(UObject* Object, FString PropertyName);

  void AppendToMesh(TSharedPtr<FJsonObject> Jason);

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Actor")
    USceneComponent* CoordinateSource;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "View")
    UBoxComponent* Flyspace;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "View")
    UTextureRenderTarget2D* InfoCamTarget;
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "View")
    UTextureRenderTarget2D* SceneCamTarget;

  UPROPERTY(VisibleAnywhere, BlueprintReadWRite, Category = "Network")
    bool RespondWithTiming = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadWRite, Category = "Network")
    bool VagueMatchProperties = false;

  UPROPERTY(VisibleAnywhere, BlueprintReadWRite, Category = "Debug")
    bool LogResponses = false;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "View")
    UTextureRenderTarget2D* UHDSceneTarget;

  UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "View")
    float MaxVelocity = 10.f;

  UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "View")
    float DistanceToLandscape = -1.f;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Network")
    int DataChannelMaxSize = 32767;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Network")
    float DataChannelBufferDelay = 0.1f;

  UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "View")
    float TurnWeight = 0.8f;
  UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "View")
    float CircleStrength = 0.02f;
  UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "View")
    float CircleSpeed = 0.3f;

  UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Time")
    float FrameCaptureTime = 10.f;

  UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "View")
    int RenderMode = 3;

  UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "View")
    float DistanceScale = 2000.f;

  UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "View")
    float BlackDistance = 0.f;

  UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "View")
    float DirectionalIntensity = 10.0f;

  UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "View")
    float DirectionalIndirectIntensity = 0.485714f;

  UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "View")
    float AmbientIntensity = 1.0f;

  UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "View")
    float AmbientVolumeticScattering = 1.0f;

  UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Camera")
    float FocalRate = 10.f;

  UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Camera")
    float MaxFocus = 2000.f;

  UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Camera")
    bool AdjustFocalDistance = true;

  UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "View")
    class UUserWidget* Overlay;
  UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "View")
    FVector BinScale {};

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "View")
    bool LockNavigation = false;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "View")
    bool EditorOrientedCamera = false;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "View")
    bool AutoNavigate = true;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "View")
    bool Rain = false;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "View")
    int RainParticlesPerSecond{ 10000 };

  UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Network")
    int ConfiguredPort = 50121;

  UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Time")
    float FlightProximityTriggerDistance = 10.f;

  UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Time")
    bool PrintScreenNewPosition = false;

  UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "View")
    bool BindPawnToCamera = false;

  UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Time")
    float AutoExposureBias = 0.413f;

  UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "View")
    int MaxFPS = -1.f;

  UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "View")
    FVector NextLocation;

  UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Network")
    int RawDataResolution = 256;

  UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Network")
    UPixelStreamingInput* RemoteInput;

  UFUNCTION(BlueprintCallable, Category = "View")
    void UpdateCamera();

  UFUNCTION(BlueprintCallable, Category = "View")
    void SendFrame(){ SendRawFrame(nullptr,false); }
    void SendRawFrame(TSharedPtr<FJsonObject> Data = nullptr, bool bFreezeID = false);

  UFUNCTION(BlueprintCallable, Category = "Network")
    const bool IsInEditor() const;

  UPROPERTY(BlueprintReadWrite, Category = "Network")
    TArray<FVector> Points;
  UPROPERTY(BlueprintReadWrite, Category = "Network")
    TArray<FVector> Normals;
  UPROPERTY(BlueprintReadWrite, Category = "Network")
    TArray<int32> Triangles;
  UPROPERTY(BlueprintReadWrite, Category = "Network")
    TArray<FVector2D> UVs;
  UPROPERTY(BlueprintReadWrite, Category = "Network")
    TArray<float> Scalars;
  UPROPERTY(BlueprintReadWrite, Category = "Network")
    TArray<FProcMeshTangent> Tangents;

  UFUNCTION(BlueprintCallable, Category = "Camera")
    void SetCameraResolution(int Resolution);

  TOptional<TFunction<void(TSharedPtr<FJsonObject>)>> ApplicationProcessInput;

  FCriticalSection Mutex;
  bool CalculatedMaximumInOffThread = false;
  float LastComputedMaximum = 0.f;
  UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "View")
    float CurrentDivider = 10000.f;

  UPROPERTY()
    TArray<FTransmissionTarget> TransmissionTargets;

  int32_t GetDecodedSize(char* Source, int32_t Length);

  const uint8* GetBufferLocation() const { return ReceptionBuffer; }
  const uint64 GetBufferSize() const { return ReceptionBufferSize; }

  UFUNCTION(BlueprintCallable, Category = "Network")
    int GetTransmissionID();

protected:
  // Called when the game starts or when spawned
  virtual void BeginPlay() override;
  virtual void PostInitializeComponents() override;
  EDataTypeIndicator FindType(FProperty* Property);

  // scheduled tasks
  TArray<TTuple<double, double, TSharedPtr<FJsonObject>>> ScheduledTasks;

  void ApplyOrStoreTexture(TSharedPtr<FJsonObject> Json);

  AWorldSpawner* WorldSpawner;

  TMap<int, TPair<UTextureRenderTarget2D*, UTextureRenderTarget2D*>> RenderTargets;
  int LastTransmissionID = 100;
  FVector Velocity;
  FVector SpaceOrigin;
  FVector SpaceExtend;
  float MeanVelocityLength = 0;
  uint64_t SampleSize = 0;
  UMaterial* PostProcessMat;
  float LowestLandscapeBound;
  class UMaterialInstanceDynamic* CallibratedPostprocess{ nullptr };

  float FocalLength;
  float TargetFocalLength;
  FCollisionObjectQueryParams ParamsObject;
  FCollisionQueryParams ParamsTrace;

  float xprogress = 0.f;
  float FrameCaptureCounter;

  FJsonObject JsonConfig;
  int LastProgress = -1;
  FString ReceptionName;
  FString ReceptionFormat;
  uint8* ReceptionBuffer; // this is normally a reinterpret of the below
  uint64_t ReceptionBufferSize;
  uint64_t ReceptionBufferOffset;
  unsigned int PointCount = 0;
  unsigned int TriangleCount = 0;

  uint8 Base64LookupTable[256];

  FCollisionObjectQueryParams ActorFilter;
  FCollisionQueryParams CollisionFilter;
  void EnsureDistancePreservation();

  void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
  // Called every frame
  virtual void Tick(float DeltaTime) override;

};