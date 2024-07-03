// Copyright Dirk Norbert Helmrich, 2023

#include "SynavisDrone.h"

#include "ImageUtils.h"
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
#include "WorldSpawner.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"



THIRD_PARTY_INCLUDES_START
#include <limits>
THIRD_PARTY_INCLUDES_END

inline float GetColor(const FColor& c, uint32 i)
{
  switch (i)
  {
  default:
  case 0:
    return c.R;
  case 1:
    return c.G;
  case 2:
    return c.B;
  case 3:
    return c.A;
  }
}

inline void WriteStringToSave(FString OutputString, FString FileName = "")
{
  if (FileName.IsEmpty())
  {
    auto unixtime = FDateTime::Now().ToUnixTimestamp();
    FileName = FPaths::ProjectDir() + "/Synavisue" + FString::FromInt(unixtime) + ".json";
  }
  UE_LOG(LogTemp, Warning, TEXT("Writing to %s"), *FileName);
  FFileHelper::SaveStringToFile(OutputString, *FileName);
}

inline void JsonToFile(TSharedPtr<FJsonObject> Json, FString FileName = FString())
{
  FString OutputString;
  TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
  FJsonSerializer::Serialize(Json.ToSharedRef(), Writer);
  WriteStringToSave(OutputString, FileName);
}

inline FString GetStringFieldOr(TSharedPtr<FJsonObject> Json, const FString& Field, const FString& Default)
{
  if (Json.IsValid() && Json->HasTypedField<EJson::String>(Field))
  {
    return Json->GetStringField(Field);
  }
  return Default;
}

inline int32 GetIntFieldOr(TSharedPtr<FJsonObject> Json, const FString& Field, int32 Default)
{
  if (Json.IsValid() && Json->HasTypedField<EJson::Number>(Field))
  {
    return Json->GetIntegerField(Field);
  }
  return Default;
}

inline double GetDoubleFieldOr(TSharedPtr<FJsonObject> Json, const FString& Field, double Default)
{
  if (Json.IsValid() && Json->HasTypedField<EJson::Number>(Field))
  {
    return Json->GetNumberField(Field);
  }
  return Default;
}

inline bool GetBoolFieldOr(TSharedPtr<FJsonObject> Json, const FString& Field, bool Default)
{
  if (Json.IsValid() && Json->HasTypedField<EJson::Boolean>(Field))
  {
    return Json->GetBoolField(Field);
  }
  return Default;
}

void ASynavisDrone::AppendToMesh(TSharedPtr<FJsonObject> Jason)
{
  auto* Object = this->GetObjectFromJSON(Jason);
  AActor* Actor = Cast<AActor>(Object);
  if (!Actor)
  {
    return;
  }
  auto procmesh = Actor->FindComponentByClass<UProceduralMeshComponent>();
  if (!procmesh)
  {
    procmesh = NewObject<UProceduralMeshComponent>(Actor);
    procmesh->RegisterComponent();
    procmesh->AttachToComponent(Actor->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
  }
  int section = GetIntFieldOr(Jason, TEXT("section"), procmesh->GetNumSections());
  procmesh->CreateMeshSection(section, Points, Triangles, Normals, UVs, {}, Tangents, false);
}

void ASynavisDrone::ParseInput(FString Descriptor)
{
  double unixtime_start = (RespondWithTiming) ? FPlatformTime::Seconds() : -1;
  if (Descriptor.IsEmpty())
  {
    UE_LOG(LogTemp, Warning, TEXT("Empty Descriptor"));
    SendError("Empty Descriptor");
    return;
  }
  // reinterpret the message as ASCII
  const auto* Data = reinterpret_cast<const char*>(*Descriptor);
  // parse into FString
  FString Message(UTF8_TO_TCHAR(Data));
  // remove line breaks
  Message.ReplaceInline(TEXT("\r"), TEXT(""));
  Message.ReplaceInline(TEXT("\n"), TEXT(""));
  Message.ReplaceInline(TEXT("\\"), TEXT(""));
  Message.ReplaceInline(TEXT("\"{"), TEXT("{"));
  Message.ReplaceInline(TEXT("}\""), TEXT("}"));

  //UE_LOG(LogTemp, Warning, TEXT("M: %s"), *Message);
  if (Message[0] == '{' && Message[Message.Len() - 1] == '}')
  {
    TSharedPtr<FJsonObject> Jason = MakeShareable(new FJsonObject());
    TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Message);
    FJsonSerializer::Deserialize(Reader, Jason);
    JsonCommand(Jason, unixtime_start);
  }
  else
  {
    if (ReceptionName.IsEmpty())
    {
      UE_LOG(LogTemp, Warning, TEXT("Received data is not JSON and we are not waiting for data."));
      SendError("Received data is not JSON and we are not waiting for data.");
    }
    else
    {
      const uint64 size = FCStringAnsi::Strlen(reinterpret_cast<const ANSICHAR*>(*Descriptor));
      UE_LOG(LogTemp, Warning, TEXT("Received data of size %d is not JSON but we are waiting for data."), size);
      const uint8* data = reinterpret_cast<const uint8*>(*Descriptor);
      // length of the data in bytes

      FMemory::Memcpy(ReceptionBuffer + ReceptionBufferOffset, data, size);
      ReceptionBufferOffset += size;
      SendResponse(FString::Printf(TEXT("{\"type\":\"buffer\",\"name\":\"%s\", \"state\":\"transit\"}"), *ReceptionName), unixtime_start);
    }
  }
}

void ASynavisDrone::JsonCommand(TSharedPtr<FJsonObject> Jason, double unixtime_start)
{
  // check if the message is a geometry message
  if (Jason->HasField(TEXT("type")))
  {
    auto type = Jason->GetStringField(TEXT("type"));
    int pid = GetIntFieldOr(Jason, TEXT("pid"), -1);
    if (LogResponses)
      UE_LOG(LogTemp, Warning, TEXT("Received Message of Type %s"), *type);
    if (type == "geometry")
    {
      Points.Empty();
      Normals.Empty();
      Triangles.Empty();
      UVs.Empty();
      Scalars.Empty();
      Tangents.Empty();
      // this is the partitioned transmission of the geometry
      // We will receive the buffers in chunks and with individual size warnings
      // Here we prompt the World Spawner to create a new geometry container
      WorldSpawner->SpawnObject(Jason);
    }
    else if (type == "directbase64" || type == "appendbase64")
    {
      ParseGeometryFromJson(Jason);
      FString id;
      // check which geometry this message is for
      if (Jason->HasField(TEXT("id")))
      {
        id = Jason->GetStringField(TEXT("id"));
      }
      if (type == TEXT("appendbase64"))
      {
        AppendToMesh(Jason);
      }
      else
      {
        auto* act = WorldSpawner->SpawnProcMesh(Points, Normals, Triangles, Scalars, 0.0, 1.0, UVs, Tangents);
        id = act->GetName();
      }
      SendResponse("{\"type\":\"geometry\",\"name\":\"" + id + "\"}", unixtime_start, pid);
    }
    else if (type == TEXT("filegeometry"))
    {
      // read file name
      auto fname = Jason->GetStringField(TEXT("filename"));
      // open file in binary mode
      auto& file = FPlatformFileManager::Get().GetPlatformFile();
      if (file.FileExists(*fname))
      {
        TArray<uint8> data;
        if (FFileHelper::LoadFileToArray(data, *fname, 0))
        {
          // first data pointer
          auto* ptr = data.GetData();
          // uint64 info on number of points
          uint64 num_points = *reinterpret_cast<uint64*>(ptr);
          uint64 fvector_size = sizeof(FVector);
          ptr += sizeof(uint64);
          // points
          Points.SetNumZeroed(num_points);
          FMemory::Memcpy(Points.GetData(), ptr, num_points * fvector_size);
          ptr += num_points * sizeof(FVector);
          // uint64 info on number of indices
          uint64 num_indices = *reinterpret_cast<uint64*>(ptr);
          ptr += sizeof(uint64);
          // indices
          Triangles.SetNumZeroed(num_indices);
          FMemory::Memcpy(Triangles.GetData(), ptr, num_indices * sizeof(int32));
          ptr += num_indices * sizeof(int32);
          // uint64 info on number of normals
          uint64 num_normals = *reinterpret_cast<uint64*>(ptr);
          ptr += sizeof(uint64);
          // normals
          Normals.SetNumZeroed(num_normals);
          FMemory::Memcpy(Normals.GetData(), ptr, num_normals * sizeof(FVector));
          ptr += num_normals * sizeof(FVector);
          // uint64 info on number of uvs
          uint64 num_uvs = *reinterpret_cast<uint64*>(ptr);
          ptr += sizeof(uint64);
          // uvs
          UVs.SetNumZeroed(num_uvs);
          FMemory::Memcpy(UVs.GetData(), ptr, num_uvs * sizeof(FVector2D));
          ptr += num_uvs * sizeof(FVector2D);
        }
        // create mesh
        if (!Jason->HasField(TEXT("append")) && !Jason->HasField(TEXT("hold")))
        {
          auto mesh = WorldSpawner->SpawnProcMesh(Points, Normals, Triangles, {}, 0.0, 1.0, UVs, {});
          ApplyJSONToObject(mesh, Jason.Get());
        }
      }
      // we consumed the input, delete the file
      file.DeleteFile(*fname);
      if (unixtime_start > 0)
      {
        SendResponse(FString::Printf(TEXT("{\"type\":\"filegeometry\",\"starttime\":%f}"), unixtime_start), unixtime_start, pid);
      }
    }
    else if (type == "parameter")
    {
      auto* Target = this->GetObjectFromJSON(Jason);
      ApplyJSONToObject(Target, Jason.Get());
      SendResponse("{\"type\":\"parameter\",\"name\":\"" + Target->GetName() + "\"}", unixtime_start, pid);
    }
    else if (type == "query")
    {
      if (!Jason->HasField(TEXT("object")))
      {
        if (Jason->HasField(TEXT("spawn")))
        {
          FString spawn = Jason->GetStringField(TEXT("spawn"));
          if (WorldSpawner)
          {
            auto cache = WorldSpawner->GetAssetCacheTemp();

            if (spawn == "any")
            {
              // return names of all available assets
              FString message = "{\"type\":\"query\",\"name\":\"spawn\",\"data\":[";
              TArray<FString> Names = WorldSpawner->GetNamesOfSpawnableTypes();
              for (int i = 0; i < Names.Num(); ++i)
              {
                message += FString::Printf(TEXT("\"%s\""), (*Names[i]));
                if (i < Names.Num() - 1)
                  message += TEXT(",");
              }
              message += "]}";
              this->SendResponse(message, unixtime_start, pid);
            }
            else
            {
              // we are still in query mode, so this must mean that spawn parameters should be listed
              if (cache->HasField(spawn))
              {
                auto asset_json = cache->GetObjectField(spawn);
                // the asset json already contains all info
                // serialize
                FString message;
                TSharedRef<TJsonWriter<TCHAR>> Writer = TJsonWriterFactory<TCHAR>::Create(&message);
                FJsonSerializer::Serialize(asset_json.ToSharedRef(), Writer);
                message = FString::Printf(TEXT("{\"type\":\"query\",\"name\":\"spawn\",\"data\":%s}"), *message);
                this->SendResponse(message, unixtime_start, pid);
              }
            }
          }
        }
        else
        {
          // respond with names of all actors
          FString message = "{\"type\":\"query\",\"name\":\"all\",\"data\":[";
          TArray<AActor*> Actors;
          UGameplayStatics::GetAllActorsOfClass(GetWorld(), AActor::StaticClass(), Actors);
          const auto NumActors = Actors.Num();
          for (auto i = 0; i < NumActors; ++i)
          {
            message += FString::Printf(TEXT("\"%s\""), *Actors[i]->GetName());
            if (i < NumActors - 1)
              message += TEXT(",");
          }
          message += "]}";
          this->SendResponse(message, unixtime_start, pid);
        }
      }
      else if (Jason->HasField(TEXT("property")))
      {
        auto* Target = this->GetObjectFromJSON(Jason);
        if (Target != nullptr)
        {
          FString Name = Target->GetName();
          FString Property = Jason->GetStringField(TEXT("property"));
          // join Name and Property
          Name = FString::Printf(TEXT("%s.%s"), *Name, *Property);
          FString JsonData = GetJSONFromObjectProperty(Target, Property);
          FString message = FString::Printf(TEXT("{\"type\":\"query\",\"name\":\"%s\",\"data\":%s}"), *Name, *JsonData);
          this->SendResponse(message, unixtime_start, pid);
        }
        else
        {
          SendError("query request object not found");
          UE_LOG(LogTemp, Error, TEXT("query request object not found"))
        }
      }
      else
      {
        auto* Target = this->GetObjectFromJSON(Jason);
        if (Target != nullptr)
        {
          FString Name = Target->GetName();
          FString JsonData = ListObjectPropertiesAsJSON(Target);
          FString message = FString::Printf(TEXT("{\"type\":\"query\",\"name\":\"%s\",\"data\":%s}"), *Name, *JsonData);
          this->SendResponse(message, unixtime_start, pid);
        }
        else
        {
          SendError("query request object not found");
          UE_LOG(LogTemp, Error, TEXT("query request object not found"))
        }
      }
    }
    else if (type == "track")
    {
      // this is a request to track a property
      // we need values "object" and "property"
      if (!Jason->HasField(TEXT("object")) || !Jason->HasField(TEXT("property")))
      {
        SendError("track request needs object and property fields");
        UE_LOG(LogTemp, Error, TEXT("track request needs object and property fields"))
      }
      else
      {
        FString ObjectName = Jason->GetStringField(TEXT("object"));
        FString PropertyName = Jason->GetStringField(TEXT("property"));
        auto Object = this->GetObjectFromJSON(Jason);
        if (!Object)
        {
          SendError("track request object not found");
          return;
        }

        // check if we are already tracking this property
        if (this->TransmissionTargets.ContainsByPredicate([Object, PropertyName](const FTransmissionTarget& Target)
          {
            return Target.Object == Object && Target.Property->GetName() == PropertyName;
          }))
        {
          SendError("track request already tracking this property");
          return;
        }

        // check if the property is one of the shortcut properties
        if (PropertyName == "Position" || PropertyName == "Rotation" || PropertyName == "Scale" || PropertyName == "Transform")
        {
          // there is no property to track, but we need to add a transmission target
          TransmissionTargets.Add({ Object, nullptr, EDataTypeIndicator::Transform, FString::Printf(TEXT("%s.%s"), *ObjectName, *PropertyName) });
        }
        else
        {

          auto Property = Object->GetClass()->FindPropertyByName(*PropertyName);

          if (!Property)
          {
            SendError("track request Property not found");
            return;
          }

          this->TransmissionTargets.Add({ Object, Property, this->FindType(Property),
            FString::Printf(TEXT("%s.%s"),*ObjectName,*PropertyName) });
        }
      }
    }
    else if (type == "untrack")
    {
      // this is a request to untrack a property
      // we need values "object" and "property"
      if (!Jason->HasField(TEXT("object")) || !Jason->HasField(TEXT("property")))
      {
        SendError("untrack request needs object and property fields");
        UE_LOG(LogTemp, Error, TEXT("untrack request needs object and property fields"))
      }
      else
      {
        FString ObjectName = Jason->GetStringField(TEXT("object"));
        FString PropertyName = Jason->GetStringField(TEXT("property"));
        auto Object = this->GetObjectFromJSON(Jason);
        if (!Object)
        {
          SendError("untrack request object not found");
          return;
        }
        auto Property = Object->GetClass()->FindPropertyByName(*PropertyName);
        if (!Property)
        {
          SendError("untrack request Property not found");
          return;
        }
        for (int i = 0; i < this->TransmissionTargets.Num(); ++i)
        {
          if (this->TransmissionTargets[i].Object == Object && this->TransmissionTargets[i].Property == Property)
          {
            this->TransmissionTargets.RemoveAt(i);
            break;
          }
        }
      }
    }
    else if (type == "command")
    {
      // received a command
      FString Name = Jason->GetStringField(TEXT("name"));
      if (Name == "reset")
      {
        // reset the geometry
        Points.Empty();
        Normals.Empty();
        Triangles.Empty();
        UVs.Empty();
      }
      else if (Name == "frametime")
      {
        float frametime = GetWorld()->GetDeltaSeconds();
        FString message = FString::Printf(TEXT("{\"type\":\"frametime\",\"value\":%f}"), frametime);
      }
      else if (Name == "cam")
      {
        FString CameraToSwitchTo = Jason->GetStringField(TEXT("camera"));
        if (CameraToSwitchTo == "info")
        {
          UE_LOG(LogActor, Warning, TEXT("Switching to info cam"));
          OnBlueprintSignalling.Broadcast(EBlueprintSignalling::SwitchToInfoCam);
        }
        else if (CameraToSwitchTo == TEXT("scene"))
        {
          UE_LOG(LogActor, Warning, TEXT("Switching to scene cam"));
          OnBlueprintSignalling.Broadcast(EBlueprintSignalling::SwitchToSceneCam);
        }
        else if (CameraToSwitchTo == "dual")
        {
          UE_LOG(LogActor, Warning, TEXT("Switching to dual cam"));
          OnBlueprintSignalling.Broadcast(EBlueprintSignalling::SwitchToBothCams);
        }
      }
      else if (Name == "ignore")
      {
        FString CameraToIgnore = Jason->GetStringField(TEXT("camera"));
        USceneCaptureComponent2D* SceneCapture = (CameraToIgnore == TEXT("scene")) ? SceneCam : InfoCam;
        auto* Object = this->GetObjectFromJSON(Jason);
        if (Object->IsA<AActor>())
        {
          // try get primitive component
          auto* ActorObject = Cast<AActor>(Object);
          SceneCapture->HideActorComponents(ActorObject, true);
        }
        else
        {
          // try get primitive component
          auto* PrimitiveObject = Cast<UPrimitiveComponent>(Object);
          if (PrimitiveObject)
          {
            SceneCapture->HideComponent(PrimitiveObject);
          }
        }
      }
      else if (Name == "Show")
      {
        FString CameraToIgnore = Jason->GetStringField(TEXT("camera"));
        USceneCaptureComponent2D* SceneCapture = (CameraToIgnore == TEXT("scene")) ? SceneCam : InfoCam;
        auto* Object = this->GetObjectFromJSON(Jason);
        if (Object->IsA<AActor>())
        {
          // try get primitive component
          auto* ActorObject = Cast<AActor>(Object);
          SceneCapture->ShowOnlyActorComponents(ActorObject, true);
        }
        else
        {
          // try get primitive component
          auto* PrimitiveObject = Cast<UPrimitiveComponent>(Object);
          if (PrimitiveObject)
          {
            SceneCapture->ShowOnlyComponent(PrimitiveObject);
          }
        }
      }
      else if (Name == "HideAll")
      {
        FString CameraToIgnore = Jason->GetStringField(TEXT("camera"));
        bool Value = Jason->GetBoolField(TEXT("value"));
        USceneCaptureComponent2D* SceneCapture = (CameraToIgnore == TEXT("scene")) ? SceneCam : InfoCam;
        SceneCapture->PrimitiveRenderMode = Value ? ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList : ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
      }
      else if (Name == "RawData")
      {
        this->FrameCaptureTime = GetDoubleFieldOr(Jason, TEXT("framecapturetime"), 10.0);
        this->FrameCaptureCounter = this->FrameCaptureTime;
      }
      else if (Name == "navigate")
      {
        AutoNavigate = false;
        NextLocation = FVector(Jason->GetNumberField(TEXT("x")), Jason->GetNumberField(TEXT("y")), Jason->GetNumberField(TEXT("z")));
      }
      else if (Name == "trace")
      {
        // required fields: start, end
        FVector startpos, endpos;
        if (Jason->HasField(TEXT("start")))
        {
          auto start = Jason->GetObjectField(TEXT("start"));
          startpos = FVector(start->GetNumberField(TEXT("x")), start->GetNumberField(TEXT("y")), start->GetNumberField(TEXT("z")));
        }
        else
        {
          // get the location of the camera
          startpos = SceneCam->GetComponentLocation();
        }
        if (Jason->HasField(TEXT("end")))
        {
          auto end = Jason->GetObjectField(TEXT("end"));
          endpos = FVector(end->GetNumberField(TEXT("x")), end->GetNumberField(TEXT("y")), end->GetNumberField(TEXT("z")));
        }
        else if (Jason->HasField(TEXT("direction")))
        {
          auto direction = Jason->GetObjectField(TEXT("direction"));
          auto direction_vector = FVector(direction->GetNumberField(TEXT("x")), direction->GetNumberField(TEXT("y")), direction->GetNumberField(TEXT("z")));
          endpos = startpos + direction_vector;
        }
        else
        {
          //trace down 1000 units
          endpos = startpos + FVector(0, 0, -1000);
        }
        TArray<FHitResult> Hits;
        FCollisionQueryParams TraceParams(FName(TEXT("Trace")), true, this);
        TraceParams.bTraceComplex = true;
        TraceParams.bReturnPhysicalMaterial = true;
        // trace all in range
        GetWorld()->LineTraceMultiByChannel(Hits, startpos, endpos, ECC_Visibility, TraceParams);
        // create a response by collapsing all hits with distance and name
        FString message = TEXT("{\"type\":\"trace\",\"data\":[");
        for (int i = 0; i < Hits.Num(); ++i)
        {
          message += FString::Printf(TEXT("{\"distance\":%f,\"name\":\"%s\"}"), Hits[i].Distance, *Hits[i].GetActor()->GetName());
          if (i < Hits.Num() - 1)
            message += ",";
        }
        message += TEXT("],");
        // add start and end positions
        message += FString::Printf(TEXT("\"start\":{\"x\":%f,\"y\":%f,\"z\":%f},\"end\":{\"x\":%f,\"y\":%f,\"z\":%f}}"), startpos.X, startpos.Y, startpos.Z, endpos.X, endpos.Y, endpos.Z);
        SendResponse(message, unixtime_start, pid);
      }
    }
    else if (type == "info")
    {
      if (Jason->HasField(TEXT("frametime")))
      {
        const FString Response = FString::Printf(TEXT("{\"type\":\"info\",\"frametime\":%f}"), GetWorld()->GetDeltaSeconds());
        SendResponse(Response, unixtime_start, pid);
      }
      else if (Jason->HasField(TEXT("memory")))
      {
        const FString Response = FString::Printf(TEXT("{\"type\":\"info\",\"memory\":%d}"), FPlatformMemory::GetStats().TotalPhysical);
        SendResponse(Response, unixtime_start, pid);
      }
      else if (Jason->HasField(TEXT("fps")))
      {
        const FString Response = FString::Printf(TEXT("{\"type\":\"info\",\"fps\":%d}"), static_cast<uint32_t>(FPlatformTime::ToMilliseconds(FPlatformTime::Cycles64())));
        SendResponse(Response, unixtime_start, pid);
      }
      else if (Jason->HasField(TEXT("object")))
      {
        FString RequestedObjectName = Jason->GetStringField(TEXT("object"));
        TArray<AActor*> FoundActors;
      }
      else if (Jason->HasField(TEXT("DataChannelSize")))
      {
        int DataChannelSize = Jason->GetIntegerField(TEXT("DataChannelSize"));
        this->DataChannelMaxSize = DataChannelSize;
      }
    }
    else if (type == "console")
    {
      if (Jason->HasField(TEXT("command")))
      {
        FString Command = Jason->GetStringField(TEXT("command"));
        UE_LOG(LogTemp, Warning, TEXT("Console command %s"), *Command);
        auto* Controller = GetWorld()->GetFirstPlayerController();
        if (Controller)
        {
          Controller->ConsoleCommand(Command);
        }
      }
    }
    else if (type == "settings")
    {
      // check for settings subobject and put it into member
      ApplyFromJSON(Jason);
      if (this->DataChannelMaxSize < 1024)
      {
        this->DataChannelMaxSize = 1024;
      }
    }
    else if (type == "append")
    {
      if (Jason->HasField(TEXT("object")))
      {
        UE_LOG(LogTemp, Warning, TEXT("Request to append geometry to object"));
        AppendToMesh(Jason);
      }
    }
    else if (type == "spawn")
    {
      if (Jason->HasField(TEXT("object")) && Jason->GetStringField(TEXT("object")) == "ProceduralMeshComponent")
      {
        UE_LOG(LogTemp, Warning, TEXT("Spawn request for ProceduralMeshComponent"));
        int32 section_index = 0;
        if (Jason->HasField(TEXT("section")))
        {
          section_index = Jason->GetIntegerField(TEXT("section"));
        }
        auto name = this->WorldSpawner->SpawnObject(Jason);
        UProceduralMeshComponent* Mesh = Cast<UProceduralMeshComponent>(WorldSpawner->GetHeldComponent());
        TArray<FColor> Colors;
        if (Scalars.Num() == Points.Num())
        {
          // we have scalars, so we need to convert them to colors
          // just as an example, we use a bluered
          // we can use the same color map for all scalars
          // but we need to know the range of the scalars
          float min = Scalars[0];
          float max = Scalars[0];
          for (auto scalar : Scalars)
          {
            if (scalar < min)
            {
              min = scalar;
            }
            if (scalar > max)
            {
              max = scalar;
            }
          }
          for (auto scalar : Scalars)
          {
            float t = (scalar - min) / (max - min);
            FLinearColor color = FLinearColor::LerpUsingHSV(FLinearColor(1, 0, 0), FLinearColor(0, 0, 1), t);
            Colors.Add(color.ToFColor(false));
          }
        }
        Mesh->CreateMeshSection(section_index, Points, Triangles, Normals, UVs, Colors, Tangents, false);
      }
      else if (this->WorldSpawner)
      {
        auto name = this->WorldSpawner->SpawnObject(Jason);
        SendResponse(FString::Printf(TEXT("{\"type\":\"spawn\",\"name\":\"%s\"}"), *name), unixtime_start, pid);
      }
      else
      {
        UE_LOG(LogTemp, Warning, TEXT("No world spawner available"));
        SendError("No world spawner available");
      }
    }
    else if (type == "texture")
    {

      FString TexData = GetStringFieldOr(Jason, TEXT("data"), "");
      // check if the transmission is direct
      if (!TexData.IsEmpty())
      {
        auto size = FBase64::GetDecodedDataSize(TexData);
        ReceptionBuffer = new uint8[size];
        FBase64::Decode(*TexData, size, ReceptionBuffer);
        ApplyOrStoreTexture(Jason);
      }
      else
      {
        // here we assume that we received a "buffer" in the past
        // if anything is invalid, this should not do anything
        ApplyOrStoreTexture(Jason);
      }
    }
    else if (type == "material")
    {
      // required: object, material, parameter, dtype

      // extract fields
      FString ObjectName = Jason->GetStringField(TEXT("object"));
      FString MaterialSlot = Jason->GetStringField(TEXT("slot"));
      FString ParameterName = Jason->GetStringField(TEXT("parameter"));
      FString dtype = Jason->GetStringField(TEXT("dtype"));
      FString Value = Jason->GetStringField(TEXT("value"));

      auto Object = this->GetObjectFromJSON(Jason);
      auto Instance = this->WorldSpawner->GenerateInstanceFromName(ObjectName, false);

      if (dtype == TEXT("scalar"))
      {
        auto ScalarValue = FCString::Atof(*Value);
        Instance->SetScalarParameterValue(FName(ParameterName), ScalarValue);
      }
      else if (dtype == TEXT("vector"))
      {
        auto VectorValue = FVector(FCString::Atof(*Value), FCString::Atof(*Value), FCString::Atof(*Value));
        Instance->SetVectorParameterValue(FName(ParameterName), VectorValue);
      }
      else
      {
        UE_LOG(LogTemp, Warning, TEXT("Unknown dtype %s"), *dtype);
        SendError("Unknown dtype");
        return;
      }
    }
    else if (type == "buffer")
    {
      FString name;
      if (Jason->HasField(TEXT("start")) && Jason->HasField(TEXT("size")) && Jason->HasField(TEXT("format")))
      {

        name = Jason->GetStringField(TEXT("start"));
        auto Format = Jason->GetStringField(TEXT("format"));
        auto size = Jason->GetIntegerField(TEXT("size"));
        ReceptionBufferSize = size;
        ReceptionFormat = Format;
        ReceptionName = name;
        ReceptionBufferOffset = 0;
        // if the format is binary, we do not need to do anything
        // if the format is base64, we need to decode the data and allocate a buffer
        if (Format == "base64")
        {
          ReceptionBuffer = new uint8[size];
        }
        else if (ReceptionName == "points")
        {
          Points.SetNum(size / sizeof(FVector));
          ReceptionBuffer = reinterpret_cast<uint8*>(Points.GetData());
        }
        else if (ReceptionName == "normals")
        {
          Points.SetNum(size / sizeof(FVector));
          ReceptionBuffer = reinterpret_cast<uint8*>(Normals.GetData());
        }
        else if (ReceptionName == "triangles")
        {
          Triangles.SetNum(size / sizeof(int32));
          ReceptionBuffer = reinterpret_cast<uint8*>(Triangles.GetData());
        }
        else if (ReceptionName == "uvs")
        {
          UVs.SetNum(size / sizeof(FVector2D));
          ReceptionBuffer = reinterpret_cast<uint8*>(UVs.GetData());
        }
        else if (ReceptionName == "texture" || ReceptionName == "custom")
        {
          ReceptionBuffer = new uint8[size];
        }
        else
        {
          UE_LOG(LogTemp, Warning, TEXT("Unknown buffer name %s"), *ReceptionName);
          SendError("Unknown buffer name");
          return;
        }
        SendResponse(FString::Printf(TEXT("{\"type\":\"buffer\",\"name\":\"%s\", \"state\":\"start\"}"), *name), unixtime_start, pid);
      }
      else if (Jason->HasField(TEXT("stop")))
      {
        // compute the size of the output buffer
        auto OutputSize = FBase64::GetDecodedDataSize(reinterpret_cast<char*>(ReceptionBuffer), ReceptionBufferSize);

        uint8* OutputBuffer = nullptr;
        // if we got a base64 buffer, we need to decode it
        if (ReceptionFormat == "base64")
        {
          if (ReceptionName == "points")
          {
            Points.SetNum(OutputSize / sizeof(FVector));
            OutputBuffer = reinterpret_cast<uint8*>(Points.GetData());
          }
          else if (ReceptionName == "normals")
          {
            Normals.SetNum(OutputSize / sizeof(FVector));
            OutputBuffer = reinterpret_cast<uint8*>(Normals.GetData());
          }
          else if (ReceptionName == "triangles")
          {
            Triangles.SetNum(OutputSize / sizeof(int32));
            OutputBuffer = reinterpret_cast<uint8*>(Triangles.GetData());
          }
          else if (ReceptionName == "uvs")
          {
            UVs.SetNum(OutputSize / sizeof(FVector2D));
            OutputBuffer = reinterpret_cast<uint8*>(UVs.GetData());
          }
          else if (ReceptionName == "tangents")
          {
            // here we need to allocate a buffer for the tangents with FVector
            // This is because we do not transmit the fourth component of the tangent
            Tangents.SetNum(OutputSize / sizeof(FVector));
            // parse the vectors into new FProcMeshTangents and copy them into the array
            float* TangentData = reinterpret_cast<float*>(ReceptionBuffer);
            for (int i = 0; i < OutputSize / sizeof(FVector); i++)
            {
              Tangents[i].TangentX = FVector(TangentData[i * 3], TangentData[i * 3 + 1], TangentData[i * 3 + 2]);
              Tangents[i].bFlipTangentY = false;
            }
          }
          else if (ReceptionName == "texture" || ReceptionName == "custom")
          {

            OutputBuffer = new uint8[OutputSize];
          }
          else
          {
            UE_LOG(LogTemp, Warning, TEXT("Unknown buffer name %s"), *ReceptionName);
            SendError("Unknown buffer name");
            return;
          }

          // use built-in unreal functions as long as the in-place decoding does not work
          // Create FString from Reception Buffer

          int32_t EndBuffer = 0;
          // find the end of the base64 string by searching for the first occurence of a non-base64 character
          for (; EndBuffer < ReceptionBufferSize; EndBuffer++)
          {
            // manually check the character against the base64 alphabet
            // break when we find the first character that is part of the base64 alphabet
            // this is because we start from the end of the string
            const char c = reinterpret_cast<const char*>(ReceptionBuffer)[ReceptionBufferSize - EndBuffer - 1];
            if (((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
              || c == '+' || c == '/' || c == '=' || c == '\n' || c == '\r'))
            {
              break;
            }
          }

          // Decode the Base64 String
          // we need to remove the last 6 characters, because they are not part of the base64 string
          // this is because the base64 string is padded with 6 characters
          if (!FBase64::Decode(reinterpret_cast<const ANSICHAR*>(ReceptionBuffer), ReceptionBufferSize - EndBuffer, OutputBuffer))
          {
            UE_LOG(LogTemp, Warning, TEXT("Could not decode base64 string"));
            // for debug purposes, we write the first 20 letters of the string onto log
            //UE_LOG(LogTemp, Warning, TEXT("First 20 letters of string: %s"), *Base64String.Left(20));
            //UE_LOG(LogTemp, Warning, TEXT("Last 20 letters of string: %s"), *Base64String.Right(20));
            SendError("Could not decode base64 string");
            return;
          }
          delete[] ReceptionBuffer;
          ReceptionBuffer = OutputBuffer;
          name = Jason->GetStringField(TEXT("stop"));
          SendResponse(FString::Printf(TEXT("{\"type\":\"buffer\",\"name\":\"%s\", \"state\":\"stop\", \"amount\":%llu}"), *name, ReceptionBufferSize), unixtime_start, pid);
          ReceptionBufferSize = OutputSize;
          //SendResponse(FString::Printf(TEXT("{\"type\":\"buffer\",\"name\":\"%s\", \"state\":\"stop\"}"), *name),unixtime_start, pid);
        }
      }
      else
      {
        SendError("buffer request needs start or stop field");
        return;
      }
    }
    else if (type == "receive")
    {
      // in-thread buffer progression message
      int progress = Jason->GetIntegerField(TEXT("progress"));
      if (progress == -1)
      {
        FTextureRenderTargetResource* Source = nullptr;
        ReceptionName = GetStringFieldOr(Jason, TEXT("camera"), TEXT("scene"));
        if (ReceptionName == TEXT("scene"))
        {
          Source = SceneCam->TextureTarget->GameThread_GetRenderTargetResource();
        }
        else
        {
          Source = InfoCam->TextureTarget->GameThread_GetRenderTargetResource();
        }
        TArray<FColor> CamData;
        FReadSurfaceDataFlags ReadPixelFlags(ERangeCompressionMode::RCM_MinMax);
        ReadPixelFlags.SetLinearToGamma(true);
        if (!Source->ReadPixels(CamData, ReadPixelFlags))
        {
          SendError("Could not read pixels from camera");
          return;
        }
        ReceptionFormat = FBase64::Encode(reinterpret_cast<uint8*>(CamData.GetData()), CamData.Num() * sizeof(FColor));
        if (this->IsInEditor())
        {
          auto OutputString = ReceptionFormat;
          // split every 100th character into a new line
          for (int i = 100; i < OutputString.Len(); i += 100)
          {
            OutputString.InsertAt(i, '\n');
          }
          auto unixtime = FDateTime::Now().ToUnixTimestamp();
          auto FileName = FPaths::ProjectDir() + "/Synavisue" + FString::FromInt(unixtime) + ".json";
          FFileHelper::SaveStringToFile(OutputString, *FileName);
        }
        UE_LOG(LogTemp, Warning, TEXT("Read %d pixels from camera amounting to sizes of %d->%d"), CamData.Num(), CamData.Num() * sizeof(FColor), ReceptionFormat.Len());
        ReceptionBufferSize = 1;
        ReceptionBufferOffset = 0;
        auto BaseLength = ReceptionFormat.Len();
        while (30 * ReceptionBufferSize + (BaseLength / ReceptionBufferSize) > DataChannelMaxSize)
        {
          ReceptionBufferSize++;
        }
        LastProgress = 0;
      }
      else if (progress == -2)
      {
        auto missing_chunk = Jason->GetIntegerField(TEXT("chunk"));
        UE_LOG(LogNet, Warning, TEXT("Received request for missing chunk %d"), missing_chunk);
        if (missing_chunk < 0 || missing_chunk >= ReceptionBufferSize)
        {
          SendError("invalid chunk number");
          return;
        }
        else
        {
          FString Response = TEXT("{\"type\":\"receive\",\"data\":\"");
          auto ChunkSize = ReceptionFormat.Len() / ReceptionBufferSize;
          const auto Lower = ChunkSize * missing_chunk;
          auto Upper = FGenericPlatformMath::Min(ChunkSize * (missing_chunk + 1), (uint64_t)ReceptionFormat.Len());
          if ((ReceptionFormat.Len() - Upper) < ReceptionBufferSize)
          {
            Upper = ReceptionFormat.Len();
          }
          Response += ReceptionFormat.Mid(Lower, Upper - Lower + 1);
          Response += TEXT("\", \"chunk\":\"");
          Response += FString::FromInt(missing_chunk);
          Response += TEXT("/");
          Response += FString::FromInt(ReceptionBufferSize);
          Response += TEXT("\"}");
          SendResponse(Response, unixtime_start, pid);
        }
      }
      else
      {
        LastProgress = progress;
      }
    }
    else if (type == "frame")
    {
      if (this->DataChannelMaxSize < 0)
      {
        SendError("frame was requested but data channel size is not set");
        return;
      }

      FString res = GetStringFieldOr(Jason, TEXT("resolution"), TEXT("base"));
      FString ImageTarget = GetStringFieldOr(Jason, TEXT("camera"), TEXT("scene"));

      if (res == TEXT("base"))
      {
        SendRawFrame(Jason);
      }
      else if (res == TEXT("high"))
      {
        int factor = GetIntFieldOr(Jason, TEXT("factor"), 1);
        // this is delegated to HighResScreenshot
        // we need to set the resolution of the screenshot
        // this is done by setting the console variable
        auto* controller = GetWorld()->GetFirstPlayerController();
        if (controller)
        {
          controller->ConsoleCommand(FString::Printf(TEXT("HighResShot %d"), factor));
        }
      }
    }
    else if (type == TEXT("apply"))
    {
      // this is mostly due to a previous texture buffer transmission
      // we need to apply the texture to the material
      ApplyOrStoreTexture(Jason);
      delete[] ReceptionBuffer;
      ReceptionBuffer = nullptr;
      ReceptionBufferSize = 0;
      ReceptionName = "";
      ReceptionFormat = "";
      ReceptionBufferOffset = 0;
    }
    else if (type == TEXT("schedule"))
    {
      // this is a request to schedule a command
      // a subobject must exist with the json prompt
      auto Prompt = Jason->GetObjectField(TEXT("command"));
      auto time = GetDoubleFieldOr(Jason, TEXT("time"), 0.0);
      auto regular = GetDoubleFieldOr(Jason, TEXT("repeat"), -1.0);
      // save the task
      ScheduledTasks.Add({ time, regular, Prompt });
    }
    else if (ApplicationProcessInput.IsSet())
    {
      UE_LOG(LogTemp, Warning, TEXT("Unknown Type, I am delegating this to custom processing."));
      ApplicationProcessInput.GetValue()(Jason);
    }
  }
  else
  {
    UE_LOG(LogTemp, Warning, TEXT("No type field in JSON"));
    SendError(TEXT("No type field in JSON"));
  }
}


void ASynavisDrone::ParseGeometryFromJson(TSharedPtr<FJsonObject> Jason)
{
  FBase64 Base64;
  // this is the direct transmission of the geometry
  // this means that the properties contain the buffers
  Points.Empty();
  Normals.Empty();
  Triangles.Empty();
  // fetch the geometry from the world
  if (!WorldSpawner)
  {
    SendError(TEXT("No WorldSpawner found"));
    UE_LOG(LogTemp, Error, TEXT("No WorldSpawner found"));
  }
  // pre-allocate the data destination
  TArray<uint8> Dest;
  // determine the maximum size of the data
  uint64_t MaxSize = 0;
  for (auto Field : Jason->Values)
  {
    if (Field.Key == TEXT("type"))
      continue;
    auto& Value = Field.Value;
    if (Value->Type == EJson::String)
    {
      auto Source = Value->AsString();
      if (Base64.GetDecodedDataSize(Source) > MaxSize)
        MaxSize = Source.Len();
    }
  }
  // allocate the destination buffer
  Dest.SetNumUninitialized(MaxSize);
  // get the json property for the points
  auto points = Jason->GetStringField(TEXT("points"));
  // decode the base64 string
  Base64.Decode(points, Dest);
  // copy the data into the points array
  Points.SetNumUninitialized(Dest.Num() / sizeof(FVector), true);
  FMemory::Memcpy(Points.GetData(), Dest.GetData(), Dest.Num());
  auto normals = Jason->GetStringField(TEXT("normals"));
  Dest.Reset(MaxSize);
  Base64.Decode(normals, Dest);
  Normals.SetNumUninitialized(Dest.Num() / sizeof(FVector), true);
  FMemory::Memcpy(Normals.GetData(), Dest.GetData(), Dest.Num());
  if (Normals.Num() != Points.Num())
  {
    SendError("Normals and Points do not match in size");
    UE_LOG(LogTemp, Error, TEXT("Normals and Points do not match in size"));
  }
  auto triangles = Jason->GetStringField(TEXT("triangles"));
  Dest.Reset(MaxSize);
  Base64.Decode(triangles, Dest);
  Triangles.SetNumUninitialized(Dest.Num() / sizeof(int), true);
  FMemory::Memcpy(Triangles.GetData(), Dest.GetData(), Dest.Num());
  UVs.Reset();
  Dest.Reset(MaxSize);
  if (Jason->HasField(TEXT("texcoords")))
  {
    auto uvs = Jason->GetStringField(TEXT("texcoords"));
    Base64.Decode(uvs, Dest);
    UVs.SetNumUninitialized(Dest.Num() / sizeof(FVector2D), true);
    FMemory::Memcpy(UVs.GetData(), Dest.GetData(), Dest.Num());
  }
  if (Jason->HasField(TEXT("scalars")))
  {
    // see if there are scalars
    auto scalars = Jason->GetStringField(TEXT("scalars"));
    if (scalars.Len() > 0)
    {
      Dest.Reset();
      Base64.Decode(scalars, Dest);
      Scalars.SetNumUninitialized(Dest.Num() / sizeof(float), true);
      // for range calculation, we must move through the data manually
      auto ScalarData = reinterpret_cast<float*>(Dest.GetData());
      auto Min = std::numeric_limits<float>::max();
      auto Max = std::numeric_limits<float>::min();
      for (size_t i = 0; i < Dest.Num() / sizeof(float); i++)
      {
        auto Value = ScalarData[i];
        if (Value < Min)
          Min = Value;
        if (Value > Max)
          Max = Value;
        Scalars[i] = Value;
      }
    }
  }
  // see if there are tangents
  FString tangents;
  if (Jason->TryGetStringField(TEXT("tangents"), tangents) || tangents.Len() > 0)
  {
    Dest.Reset();
    Base64.Decode(tangents, Dest);
    Tangents.SetNumUninitialized(Dest.Num() / sizeof(FProcMeshTangent), true);
    FMemory::Memcpy(Tangents.GetData(), Dest.GetData(), Dest.Num());
  }
  else
  {
    // calculate tangents
    Tangents.SetNumUninitialized(Points.Num(), true);
    for (int p = 0; p < Points.Num(); ++p)
    {
      FVector TangentX = FVector::CrossProduct(Normals[p], FVector(0, 0, 1));
      TangentX.Normalize();
      Tangents[p] = FProcMeshTangent(TangentX, false);
    }
  }

}

void ASynavisDrone::SendResponse(FString Descriptor, double StartTime, int PlayerID)
{
  if (StartTime > 0)
  {
    // get the current unix time
    const double CurrentTime = FPlatformTime::Seconds();
    // calculate the time difference
    const int32 TimeDifference = static_cast<int32> ((CurrentTime - StartTime) * 1000);
    // add the time difference to the descriptor by removing the rbrace at the end and adding the time difference
    Descriptor.RemoveAt(Descriptor.Len() - 1);
    Descriptor.Append(FString::Printf(TEXT(", \"processed_time\":%d}"), TimeDifference));
  }
  if (PlayerID >= 0)
  {
    // add the player id to the descriptor by removing the rbrace at the and and adding the player id
    Descriptor.RemoveAt(Descriptor.Len() - 1);
    Descriptor.Append(FString::Printf(TEXT(", \"player_id\":%d}"), PlayerID));
  }
  FString Response(reinterpret_cast<TCHAR*>(TCHAR_TO_UTF8(*Descriptor)));
  // logging the first 20 characters of the response
  if (LogResponses)
    UE_LOG(LogTemp, Warning, TEXT("Sending response: %s"), *Descriptor.Left(20));
  OnPixelStreamingResponse.Broadcast(Response);
}

void ASynavisDrone::SendError(FString Message)
{
  FString Response = FString::Printf(TEXT("{\"type\":\"error\",\"message\":\"%s\"}"), *Message);
  SendResponse(Response);
}

void ASynavisDrone::ResetSynavisState()
{
  TransmissionTargets.Empty();
}

// Sets default values
ASynavisDrone::ASynavisDrone()
{
  // Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
  PrimaryActorTick.bCanEverTick = true;

  // briefly construct the decoding alphabet

#if PLATFORM_WINDOWS
  constexpr char Base64Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  for (int32_t i = 0; i < 256; ++i)
  {
    Base64LookupTable[i] = 0xFF;
  }
  for (int32_t i = 0; i < 64; ++i)
  {
    Base64LookupTable[static_cast<uint32>(Base64Alphabet[i])] = i;
  }
#endif

  CoordinateSource = CreateDefaultSubobject<USceneComponent>(TEXT("Root Component"));
  RootComponent = CoordinateSource;
  InfoCam = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Information Camera"));
  InfoCam->SetupAttachment(RootComponent);
  Flyspace = CreateDefaultSubobject<UBoxComponent>(TEXT("Fly Space"));
  Flyspace->SetBoxExtent({ 100.f,100.f,100.f });

  //Flyspace->SetupAttachment(RootComponent);

  //static ConstructorHelpers::FObjectFinder<UMaterial> Filter(TEXT("Material'/SynavisUE/SegmentationMaterial'"));
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

  static ConstructorHelpers::FObjectFinder<UTextureRenderTarget2D> UHDTarget(TEXT("/Script/Engine.TextureRenderTarget2D'/SynavisUE/UHDScene.UHDScene'"));
  if (UHDTarget.Succeeded())
  {
    UHDSceneTarget = UHDTarget.Object;
  }
  else
  {
    UE_LOG(LogTemp, Error, TEXT("Could not load one of the textures."));
  }

  SceneCam = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Rendering Camera"));
  SceneCam->SetupAttachment(RootComponent);
  InfoCam->SetRelativeLocation({ 0,0,0 });
  SceneCam->SetRelativeLocation({ 0, 0, 0 });

  ParamsObject.AddObjectTypesToQuery(ECollisionChannel::ECC_WorldDynamic);
  ParamsObject.AddObjectTypesToQuery(ECollisionChannel::ECC_WorldStatic);
  ParamsTrace.AddIgnoredActor(this);

  static ConstructorHelpers::FObjectFinder<UTextureRenderTarget2D> SC_128_(TEXT("/Script/Engine.TextureRenderTarget2D'/SynavisUE/TextureBases/scene_128.scene_128'"));
  static ConstructorHelpers::FObjectFinder<UTextureRenderTarget2D> TA_128_(TEXT("/Script/Engine.TextureRenderTarget2D'/SynavisUE/TextureBases/target_128.target_128'"));
  if (SC_128_.Succeeded() && TA_128_.Succeeded()) RenderTargets.Add(128, { SC_128_.Object, TA_128_.Object });
  static ConstructorHelpers::FObjectFinder<UTextureRenderTarget2D> SC_256_(TEXT("/Script/Engine.TextureRenderTarget2D'/SynavisUE/TextureBases/scene_256.scene_256'"));
  static ConstructorHelpers::FObjectFinder<UTextureRenderTarget2D> TA_256_(TEXT("/Script/Engine.TextureRenderTarget2D'/SynavisUE/TextureBases/target_256.target_256'"));
  if (SC_256_.Succeeded() && TA_256_.Succeeded()) RenderTargets.Add(256, { SC_256_.Object, TA_256_.Object });
  static ConstructorHelpers::FObjectFinder<UTextureRenderTarget2D> SC_512_(TEXT("/Script/Engine.TextureRenderTarget2D'/SynavisUE/TextureBases/scene_512.scene_512'"));
  static ConstructorHelpers::FObjectFinder<UTextureRenderTarget2D> TA_512_(TEXT("/Script/Engine.TextureRenderTarget2D'/SynavisUE/TextureBases/target_512.target_512'"));
  if (SC_512_.Succeeded() && TA_512_.Succeeded()) RenderTargets.Add(512, { SC_512_.Object, TA_512_.Object });
  static ConstructorHelpers::FObjectFinder<UTextureRenderTarget2D> SC_1024_(TEXT("/Script/Engine.TextureRenderTarget2D'/SynavisUE/TextureBases/scene_1024.scene_1024'"));
  static ConstructorHelpers::FObjectFinder<UTextureRenderTarget2D> TA_1024_(TEXT("/Script/Engine.TextureRenderTarget2D'/SynavisUE/TextureBases/target_1024.target_1024'"));
  if (SC_1024_.Succeeded() && TA_1024_.Succeeded()) RenderTargets.Add(1024, { SC_1024_.Object, TA_1024_.Object });
  static ConstructorHelpers::FObjectFinder<UTextureRenderTarget2D> SC_2048_(TEXT("/Script/Engine.TextureRenderTarget2D'/SynavisUE/TextureBases/scene_2048.scene_2048'"));
  static ConstructorHelpers::FObjectFinder<UTextureRenderTarget2D> TA_2048_(TEXT("/Script/Engine.TextureRenderTarget2D'/SynavisUE/TextureBases/target_2048.target_2048'"));
  if (SC_2048_.Succeeded() && TA_2048_.Succeeded()) RenderTargets.Add(2048, { SC_2048_.Object, TA_2048_.Object });
  static ConstructorHelpers::FObjectFinder<UTextureRenderTarget2D> SC_4096_(TEXT("/Script/Engine.TextureRenderTarget2D'/SynavisUE/TextureBases/scene_4096.scene_4096'"));
  static ConstructorHelpers::FObjectFinder<UTextureRenderTarget2D> TA_4096_(TEXT("/Script/Engine.TextureRenderTarget2D'/SynavisUE/TextureBases/target_4096.target_4096'"));
  if (SC_4096_.Succeeded() && TA_4096_.Succeeded()) RenderTargets.Add(4096, { SC_4096_.Object, TA_4096_.Object });

  LoadConfig();
  this->SetActorTickEnabled(false);
}

void ASynavisDrone::LoadFromJSON(FString JasonString)
{
  if (JasonString == "")
  {
    FString LevelName = GetLevel()->GetOuter()->GetName();
    FString File = FPaths::GeneratedConfigDir() + UGameplayStatics::GetPlatformName() + TEXT("/") + LevelName + TEXT(".json");
    UE_LOG(LogTemp, Warning, TEXT("Testing for file: %s"), *File);
    if (!FFileHelper::LoadFileToString(JasonString, *File))
    {
      FFileHelper::SaveStringToFile(TEXT("{}"), *File);
      return;
    }
  }
  TSharedPtr<FJsonObject> Jason = MakeShareable(new FJsonObject());
  TSharedRef<TJsonReader<TCHAR>> Reader = FJsonStringReader::Create(JasonString);
  FJsonSerializer::Deserialize(Reader, Jason);
  ApplyFromJSON(Jason);
}

void ASynavisDrone::ApplyFromJSON(TSharedPtr<FJsonObject> Jason)
{
  for (auto Key : Jason->Values)
  {
    if (Key.Key.StartsWith(TEXT("type")))
    {
      // skip network type
      continue;
    }
    auto* prop = GetClass()->FindPropertyByName(FName(*(Key.Key.RightChop(1))));

    if (!prop)
    {
      SendError(FString::Printf(TEXT("Property %s not found."), *Key.Key));
    }
    if (Key.Key.StartsWith(TEXT("f")))
    {
      FNumericProperty* fprop = CastField<FNumericProperty>(prop);
      if (fprop)
      {
        UE_LOG(LogActor, Warning, TEXT("Setting property %s to %f"), *fprop->GetFullName(), Key.Value->AsNumber());
        fprop->SetFloatingPointPropertyValue(fprop->ContainerPtrToValuePtr<void>(this), Key.Value->AsNumber());
      }
    }
    else if (Key.Key.StartsWith(TEXT("i")))
    {
      FNumericProperty* iprop = CastField<FNumericProperty>(prop);
      if (iprop)
      {
        UE_LOG(LogActor, Warning, TEXT("Setting property %s to %d"), *iprop->GetFullName(), (int64)Key.Value->AsNumber());
        iprop->SetIntPropertyValue(iprop->ContainerPtrToValuePtr<void>(this), (int64)Key.Value->AsNumber());
      }
    }
    else if (Key.Key.StartsWith(TEXT("v")))
    {
      if (!LockNavigation && Key.Key.Contains(TEXT("Position")))
      {
        FVector loc(Key.Value->AsObject()->GetNumberField(TEXT("x")),
          Key.Value->AsObject()->GetNumberField(TEXT("y")),
          Key.Value->AsObject()->GetNumberField(TEXT("z")));
        Flyspace->SetWorldLocation(loc);
      }
      else if (!LockNavigation && Key.Key.Contains(TEXT("Orientation")))
      {
        FRotator rot(Key.Value->AsObject()->GetNumberField(TEXT("p")),
          Key.Value->AsObject()->GetNumberField(TEXT("y")),
          Key.Value->AsObject()->GetNumberField(TEXT("r")));
        InfoCam->SetRelativeRotation(rot);
        SceneCam->SetRelativeRotation(rot);
      }
      else
      {
        FStructProperty* vprop = CastField<FStructProperty>(prop);
        if (vprop)
        {
          TSharedPtr<FJsonObject> Values = Key.Value->AsObject();
          FVector* Output = vprop->ContainerPtrToValuePtr<FVector>(this);
          Output->X = Values->GetNumberField(TEXT("x"));
          Output->Y = Values->GetNumberField(TEXT("y"));
          Output->Z = Values->GetNumberField(TEXT("z"));
        }
      }
    }
    else if (Key.Key.StartsWith(TEXT("b")))
    {
      FBoolProperty* bprop = CastField<FBoolProperty>(prop);
      if (bprop)
      {
        UE_LOG(LogActor, Warning, TEXT("Setting property %s to %d"), *bprop->GetFullName(), Key.Value->AsBool());
        bprop->SetPropertyValue(bprop->ContainerPtrToValuePtr<void>(this), Key.Value->AsBool());
      }
    }
  }
}

FTransform ASynavisDrone::FindGoodTransformBelowDrone()
{
  FTransform Output;
  FHitResult Hit;
  FVector Start = GetActorLocation();
  FVector End = Start - FVector(0, 0, 1000);
  Output.SetRotation(UKismetMathLibrary::RotatorFromAxisAndAngle({ 0,0,1 }, FGenericPlatformMath::FRand() * 360.f).Quaternion());
  if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECollisionChannel::ECC_WorldStatic, ParamsTrace))
  {
    Output.SetLocation(Hit.ImpactPoint);
  }
  else
  {
    Output = GetActorTransform();
  }
  return Output;
}

FString ASynavisDrone::ListObjectPropertiesAsJSON(UObject* Object)
{
  const UClass* Class = Object->GetClass();
  FString OutputString = TEXT("{");
  bool first = true;
  for (TFieldIterator<FProperty> It(Class, EFieldIteratorFlags::IncludeSuper); It; ++It)
  {
    if (!first)
    {
      OutputString += TEXT(",");
    }
    else first = false;
    FProperty* Property = *It;
    auto name = Property->GetName();
    auto type = Property->GetCPPType();
    OutputString += TEXT(" \"") + name + TEXT("\": \"") + type + TEXT("\"");
  }
  return OutputString + TEXT(" }");
}

void ASynavisDrone::StoreCameraBuffer(int BufferNumber, FString NameBase)
{
  // local blueprint-only function call to save the texture target to a file
  // this is useful for debugging purposes
  if (BufferNumber < 0 || BufferNumber >= RenderTargets.Num())
  {
    UE_LOG(LogTemp, Warning, TEXT("Buffer number %d out of range"), BufferNumber);
    return;
  }
  auto* Target = (BufferNumber == 0) ? SceneCamTarget : InfoCamTarget;
  if (!Target)
  {
    UE_LOG(LogTemp, Warning, TEXT("No target found for buffer number %d"), BufferNumber);
    return;
  }
  FTextureRenderTargetResource* Source = Target->GameThread_GetRenderTargetResource();
  TArray<FColor> CamData;
  FReadSurfaceDataFlags ReadPixelFlags(ERangeCompressionMode::RCM_MinMax);
  ReadPixelFlags.SetLinearToGamma(true);
  if (!Source->ReadPixels(CamData, ReadPixelFlags))
  {
    UE_LOG(LogTemp, Warning, TEXT("Could not read pixels from camera"));
    return;
  }
  auto filename = FPaths::ProjectDir() + FString(TEXT("synue_")) + NameBase + FString(TEXT("_")) + FString::FromInt(FDateTime::Now().ToUnixTimestamp()) + FString(TEXT(".bmp"));
  FFileHelper::CreateBitmap(*filename, Target->SizeX, Target->SizeY, CamData.GetData());
  // log success
  UE_LOG(LogTemp, Warning, TEXT("Saved camera buffer %d to file"), BufferNumber);
}

FProperty* FindPropertyThatHasName(UObject* Object, FString Name)
{
  for (TFieldIterator<FProperty> It(Object->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
  {
    FProperty* Property = *It;
    if (Property->GetName() == Name)
    {
      return Property;
    }
  }
  return nullptr;
}

void ASynavisDrone::ApplyJSONToObject(UObject* Object, FJsonObject* JSON)
{
  // received a parameter update
  FString Name = JSON->GetStringField(TEXT("property"));

  USceneComponent* ComponentIdentity = Cast<USceneComponent>(Object);
  AActor* ActorIdentity = Cast<AActor>(Object);
  auto* Property = Object->GetClass()->FindPropertyByName(*Name);
  if (!Property && VagueMatchProperties)
  {
    Property = FindPropertyThatHasName(Object, Name);
  }
  if (ActorIdentity)
  {
    ComponentIdentity = ActorIdentity->GetRootComponent();
  }

  // Find out whether one of the shortcut properties was updated
  if (ComponentIdentity)
  {
    if (Name == TEXT("position"))
    {
      if (JSON->HasField(TEXT("x")) && JSON->HasField(TEXT("y")) && JSON->HasField(TEXT("z")))
      {
        ComponentIdentity->SetWorldLocation(FVector(JSON->GetNumberField(TEXT("x")), JSON->GetNumberField(TEXT("y")), JSON->GetNumberField(TEXT("z"))));
        return;
      }
    }
    else if (Name == TEXT("orientation"))
    {
      if (JSON->HasField(TEXT("p")) && JSON->HasField(TEXT("y")) && JSON->HasField(TEXT("r")))
      {
        ComponentIdentity->SetWorldRotation(FRotator(JSON->GetNumberField(TEXT("p")), JSON->GetNumberField(TEXT("y")), JSON->GetNumberField(TEXT("r"))));
        return;
      }
    }
    else if (Name == TEXT("scale"))
    {
      if (JSON->HasField(TEXT("x")) && JSON->HasField(TEXT("y")) && JSON->HasField(TEXT("z")))
      {
        ComponentIdentity->SetWorldScale3D(FVector(JSON->GetNumberField(TEXT("x")), JSON->GetNumberField(TEXT("y")), JSON->GetNumberField(TEXT("z"))));
        return;
      }
    }
    else if (Name == TEXT("visibility"))
    {
      if (JSON->HasField(TEXT("value")))
        ComponentIdentity->SetVisibility(JSON->GetBoolField(TEXT("value")));
      return;
    }
  }
  if (Property)
  {
    if (Property->IsA(FIntProperty::StaticClass()))
    {
      auto* IntProperty = CastField<FIntProperty>(Property);
      IntProperty->SetPropertyValue_InContainer(Object, JSON->GetIntegerField(TEXT("value")));
    }
    else if (Property->IsA(FFloatProperty::StaticClass()))
    {
      auto* FloatProperty = CastField<FFloatProperty>(Property);
      FloatProperty->SetPropertyValue_InContainer(Object, JSON->GetNumberField(TEXT("value")));
    }
    else if (Property->IsA(FBoolProperty::StaticClass()))
    {
      auto* BoolProperty = CastField<FBoolProperty>(Property);
      BoolProperty->SetPropertyValue_InContainer(Object, JSON->GetBoolField(TEXT("value")));
    }
    else if (Property->IsA(FStrProperty::StaticClass()))
    {
      auto* StringProperty = CastField<FStrProperty>(Property);
      StringProperty->SetPropertyValue_InContainer(Object, JSON->GetStringField(TEXT("value")));
    }
    // check if property is a vector
    else if (Property->IsA(FStructProperty::StaticClass()))
    {
      auto* StructProperty = CastField<FStructProperty>(Property);
      // check if the struct is a vector via the JSON
      if (JSON->HasField(TEXT("x")) && JSON->HasField(TEXT("y")) && JSON->HasField(TEXT("z")))
      {
        auto* VectorValue = StructProperty->ContainerPtrToValuePtr<FVector>(Object);
        if (VectorValue)
        {
          VectorValue->X = JSON->GetNumberField(TEXT("x"));
          VectorValue->Y = JSON->GetNumberField(TEXT("y"));
          VectorValue->Z = JSON->GetNumberField(TEXT("z"));
        }
      }
    }
  }
  else
  {
    UE_LOG(LogTemp, Warning, TEXT("Property %s not found"), *Name);
    SendResponse(FString::Printf(TEXT("{\"type\":\"error\",\"message\":\"Property not found\", \"properties\":%s}"), *ListObjectPropertiesAsJSON(Object)));
  }
}

UObject* ASynavisDrone::GetObjectFromJSON(TSharedPtr<FJsonObject> JSON)
{
  FString Name = JSON->GetStringField(TEXT("object"));
  TArray<AActor*> FoundActors;
  UGameplayStatics::GetAllActorsOfClass(GetWorld(), AActor::StaticClass(), FoundActors);
  // iterate over all actors
  for (auto* Actor : FoundActors)
  {
    // check if the actor has the same name as the JSON object
    FString ActorName = Actor->GetName();
    if (!VagueMatchProperties && ActorName == Name)
    {
      return Actor;
    }
    else if (VagueMatchProperties && ActorName.Contains(Name))
    {
      return Actor;
    }
  }
  // if no actor was found, check if the object is a component
  if (Name.Contains(TEXT(".")))
  {
    FString ActorName = Name.Left(Name.Find(TEXT(".")));
    FString ComponentName = Name.Right(Name.Len() - Name.Find(TEXT(".")) - 1);
    // iterate over all actors
    for (auto* Actor : FoundActors)
    {
      // check if the actor has the same name as the JSON object
      if (Actor->GetName() == ActorName)
      {
        // iterate over all components
        for (auto* Component : Actor->GetComponents())
        {
          // check if the component has the same name as the JSON object
          if (Component->GetName() == ComponentName)
          {
            return Component;
          }
        }
      }
    }
  }
  return nullptr;
}

FString ASynavisDrone::GetJSONFromObjectProperty(UObject* Object, FString PropertyName)
{
  USceneComponent* ComponentIdentity = Cast<USceneComponent>(Object);
  AActor* ActorIdentity = Cast<AActor>(Object);
  auto* Property = Object->GetClass()->FindPropertyByName(*PropertyName);
  if (ActorIdentity)
  {
    ComponentIdentity = ActorIdentity->GetRootComponent();
  }
  // Find out whether one of the shortcut properties was requested
  if (ComponentIdentity)
  {
    if (PropertyName == "position")
    {
      FVector Position = ComponentIdentity->GetComponentLocation();
      return FString::Printf(TEXT("{\"x\":%f,\"y\":%f,\"z\":%f}"), Position.X, Position.Y, Position.Z);
    }
    else if (PropertyName == "orientation")
    {
      FRotator Orientation = ComponentIdentity->GetComponentRotation();
      return FString::Printf(TEXT("{\"p\":%f,\"y\":%f,\"r\":%f}"), Orientation.Pitch, Orientation.Yaw, Orientation.Roll);
    }
    else if (PropertyName == "scale")
    {
      FVector Scale = ComponentIdentity->GetComponentScale();
      return FString::Printf(TEXT("{\"x\":%f,\"y\":%f,\"z\":%f}"), Scale.X, Scale.Y, Scale.Z);
    }
    else if (PropertyName == "visibility")
    {
      return FString::Printf(TEXT("{\"value\":%s}"), ComponentIdentity->IsVisible() ? TEXT("true") : TEXT("false"));
    }
  }
  if (Property)
  {
    // find out whether the property is a vector
    const auto StructProperty = CastField<FStructProperty>(Property);
    const auto FloatProperty = CastField<FFloatProperty>(Property);
    const auto IntProperty = CastField<FIntProperty>(Property);
    const auto BoolProperty = CastField<FBoolProperty>(Property);
    const auto StringProperty = CastField<FStrProperty>(Property);
    if (StructProperty)
    {
      // check if the struct is a vector via the JSON
      if (StructProperty->Struct->GetFName() == "Vector")
      {
        auto* VectorValue = StructProperty->ContainerPtrToValuePtr<FVector>(Object);
        if (VectorValue)
        {
          return FString::Printf(TEXT("{\"x\":%f,\"y\":%f,\"z\":%f}"), VectorValue->X, VectorValue->Y, VectorValue->Z);
        }
      }
    }
    else if (FloatProperty)
    {
      auto* FloatValue = FloatProperty->ContainerPtrToValuePtr<float>(Object);
      if (FloatValue)
      {
        return FString::Printf(TEXT("{\"value\":%f}"), *FloatValue);
      }
    }
    else if (IntProperty)
    {
      auto* IntValue = IntProperty->ContainerPtrToValuePtr<int>(Object);
      if (IntValue)
      {
        return FString::Printf(TEXT("{\"value\":%d}"), *IntValue);
      }
    }
    else if (BoolProperty)
    {
      auto* BoolValue = BoolProperty->ContainerPtrToValuePtr<bool>(Object);
      if (BoolValue)
      {
        return FString::Printf(TEXT("{\"value\":%s}"), *BoolValue ? TEXT("true") : TEXT("false"));
      }
    }
    else if (StringProperty)
    {
      auto* StringValue = StringProperty->ContainerPtrToValuePtr<FString>(Object);
      if (StringValue)
      {
        return FString::Printf(TEXT("{\"value\":\"%s\"}"), **StringValue);
      }
    }
    else
    {
      UE_LOG(LogTemp, Warning, TEXT("Property %s not vector, float, bool, or string"), *PropertyName);
      SendResponse(TEXT("{\"type\":\"error\",\"message\":\"Property not vector, float, bool, or string\"}"));
      return TEXT("{}");
    }
  }
  UE_LOG(LogTemp, Warning, TEXT("Property %s not found"), *PropertyName);
  SendResponse(TEXT("{\"type\":\"error\",\"message\":\"Property not found\"}"));
  return TEXT("{}");
}

void ASynavisDrone::UpdateCamera()
{
  if (CallibratedPostprocess)
  {
    CallibratedPostprocess->SetScalarParameterValue(TEXT("DistanceScale"), DistanceScale);
    CallibratedPostprocess->SetScalarParameterValue(TEXT("BlackDistance"), BlackDistance);
    CallibratedPostprocess->SetScalarParameterValue(TEXT("Mode"), (float)RenderMode);
    CallibratedPostprocess->SetVectorParameterValue(TEXT("BinScale"), (FLinearColor)BinScale);
  }
  SceneCam->TextureTarget = SceneCamTarget;
  InfoCam->TextureTarget = InfoCamTarget;
}

void ASynavisDrone::SendRawFrame(TSharedPtr<FJsonObject> Jason, bool bFreezeID)
{

  if (this->DataChannelMaxSize < 0)
  {
    return;
  }

  FString ImageTarget = GetStringFieldOr(Jason, TEXT("camera"), TEXT("scene"));

  auto* rtarget = (ImageTarget == TEXT("scene"))
    ? SceneCam->TextureTarget->GameThread_GetRenderTargetResource() : InfoCam->TextureTarget->GameThread_GetRenderTargetResource();
  auto* CameraTarget = (ImageTarget == TEXT("scene")) ? SceneCam : InfoCam;

  union
  {
    struct
    {
      float fov{ 3.f };
      int width{};
      int height{};
      int id{};
      float pos[3]{ 0.f, 0.f, 0.f };
      float rot[3]{ 0.f, 0.f, 0.f };
    } data{};
    uint8_t rawdata[sizeof(data)];
  } package;
  package.data.fov = CameraTarget->FOVAngle;
  package.data.width = CameraTarget->TextureTarget->GetSurfaceWidth();
  package.data.height = CameraTarget->TextureTarget->GetSurfaceHeight();
  package.data.pos[0] = CameraTarget->GetComponentLocation().X;
  package.data.pos[1] = CameraTarget->GetComponentLocation().Y;
  package.data.pos[2] = CameraTarget->GetComponentLocation().Z;
  package.data.rot[0] = CameraTarget->GetComponentRotation().Pitch;
  package.data.rot[1] = CameraTarget->GetComponentRotation().Yaw;
  package.data.rot[2] = CameraTarget->GetComponentRotation().Roll;
  package.data.id = (bFreezeID) ? LastTransmissionID : this->GetTransmissionID();
  FString packageString = FBase64::Encode(package.rawdata, sizeof(package.rawdata));

  TArray<FColor> CData;

  FReadSurfaceDataFlags ReadPixelFlags(RCM_UNorm);
  ReadPixelFlags.SetLinearToGamma(true);
  rtarget->ReadPixels(CData, ReadPixelFlags);

  // print transmission ID as hex 
  FString TransmissionID = FString::Printf(TEXT("%x"), this->GetTransmissionID());

  FString RenderTargetString = FBase64::Encode(reinterpret_cast<uint8*>(CData.GetData()), CData.Num() * sizeof(FColor));

  FString Base = TEXT("{\"t\":\"f\",\"m\":\"") + packageString + TEXT("\",\"c\":\"");
  FString End = TEXT("\"}");
  int numChunks = 1;
  // lambda for the length of an int in digits
  const auto intlen = [](int i) -> int
    {
      int len = 0;
      while (i > 0)
      {
        i /= 10;
        len++;
      }
      return len;
    };
  while (3 + (2 * intlen(numChunks)) + Base.Len() * numChunks + End.Len() * numChunks + RenderTargetString.Len() / numChunks > DataChannelMaxSize)
  {
    numChunks++;
  }
  // make a task in the game thread to send the chunks
  FGraphEventRef Task = FFunctionGraphTask::CreateAndDispatchWhenReady(
    [RenderTargetString = std::move(RenderTargetString), Base = std::move(Base), End = std::move(End),
    SendResponse = std::bind(&ASynavisDrone::SendResponse, this, std::placeholders::_1, -1.0, -1),
    Delay = this->DataChannelBufferDelay, numChunks
    ]()
    {
      for (int i = 0; i < numChunks; i++)
      {
        int start = i * RenderTargetString.Len() / numChunks;
        int end = (i + 1) * RenderTargetString.Len() / numChunks;
        FString Chunk = Base + FString::FromInt(i) + TEXT("/") + FString::FromInt(numChunks) + TEXT("\",\"d\":\"") + RenderTargetString.Mid(start, end - start) + End;
        SendResponse(Chunk);
        // we need to wait a bit, otherwise the chunks might jam
        FPlatformProcess::Sleep(Delay);
      }
    }, TStatId(), nullptr, ENamedThreads::AnyBackgroundHiPriTask);
}

const bool ASynavisDrone::IsInEditor() const
{
#ifdef WITH_EDITOR
  return true;
#else
  return false;
#endif
}

void ASynavisDrone::SetCameraResolution(int Resolution)
{
  if (RenderTargets.Contains(Resolution))
  {
    InfoCamTarget = RenderTargets[Resolution].Key;
    SceneCamTarget = RenderTargets[Resolution].Value;
    UpdateCamera();
  }
  else
  {
    UE_LOG(LogTemp, Warning, TEXT("Resolution %d not found"), Resolution);
  }
}

int32_t ASynavisDrone::GetDecodedSize(char* Source, int32_t Length)
{
  // calculate the size of the decoded string
  int32_t Padding = 0;
  for (int32_t i = Length - 1; Source[i] == '='; --i)
  {
    ++Padding;
  }
  return ((Length * 3) / 4) - Padding;
}

int ASynavisDrone::GetTransmissionID()
{
  return ++LastTransmissionID;
}

// Called when the game starts or when spawned
void ASynavisDrone::BeginPlay()
{
  this->SetActorTickEnabled(false);
  Super::BeginPlay();

  InfoCam->AttachToComponent(RootComponent, FAttachmentTransformRules::SnapToTargetIncludingScale);
  SceneCam->AttachToComponent(RootComponent, FAttachmentTransformRules::SnapToTargetIncludingScale);

  SpaceOrigin = Flyspace->GetComponentLocation();
  SpaceExtend = Flyspace->GetScaledBoxExtent();

  Flyspace->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
  LoadFromJSON();
  auto* world = GetWorld();
  CallibratedPostprocess = UMaterialInstanceDynamic::Create(PostProcessMat, this);
  InfoCam->AddOrUpdateBlendable(CallibratedPostprocess, 1.f);
  CallibratedPostprocess->SetScalarParameterValue(TEXT("DistanceScale"), DistanceScale);
  CallibratedPostprocess->SetScalarParameterValue(TEXT("BlackDistance"), BlackDistance);
  CallibratedPostprocess->SetScalarParameterValue(TEXT("Mode"), (float)RenderMode);
  CallibratedPostprocess->SetVectorParameterValue(TEXT("BinScale"), (FLinearColor)BinScale);
  UE_LOG(LogTemp, Warning, TEXT("L:(%f,%f,%f) - E:(%f,%f,%f)"), SpaceOrigin.X, SpaceOrigin.Y, SpaceOrigin.Z, SpaceExtend.X, SpaceExtend.Y, SpaceExtend.Z);
  NextLocation = UKismetMathLibrary::RandomPointInBoundingBox(Flyspace->GetComponentLocation(), Flyspace->GetScaledBoxExtent());
  FrameCaptureCounter = FrameCaptureTime;

  SceneCam->PostProcessSettings.AutoExposureBias = this->AutoExposureBias;
  if (Rain)
  {
    // TODO do not assume that the only NiagaraActor is the rain
    ANiagaraActor* RainActor = Cast<ANiagaraActor>(UGameplayStatics::GetActorOfClass(world, ANiagaraActor::StaticClass()));
    if (RainActor)
    {
      RainActor->GetNiagaraComponent()->Activate(true);
      //RainActor->GetNiagaraComponent()->SetIntParameter(TEXT("SpawnRate"),RainParticlesPerSecond);
      RainActor->GetNiagaraComponent()->SetVariableInt(FName("SpawnRate"), RainParticlesPerSecond);
    }
  }

  TArray<AActor*> Found;
  TArray<USceneComponent*> PlantInstances;
  UGameplayStatics::GetAllActorsOfClass(world, APawn::StaticClass(), Found);
  CollisionFilter.AddIgnoredActors(Found);
  Found.Empty();

  auto* Sun = Cast<ADirectionalLight>(UGameplayStatics::GetActorOfClass(GetWorld(),
    ADirectionalLight::StaticClass()));
  auto* Ambient = Cast<ASkyLight>(UGameplayStatics::GetActorOfClass(GetWorld(),
    ASkyLight::StaticClass()));
  auto* Clouds = Cast<AVolumetricCloud>(UGameplayStatics::GetActorOfClass(GetWorld(),
    AVolumetricCloud::StaticClass()));
  auto* Atmosphere = Cast<ASkyAtmosphere>(UGameplayStatics::GetActorOfClass(GetWorld(),
    ASkyAtmosphere::StaticClass()));

  if (Sun)
  {
    Sun->GetLightComponent()->SetIntensity(DirectionalIntensity);
    Sun->GetLightComponent()->SetIndirectLightingIntensity(DirectionalIndirectIntensity);

  }
  if (Ambient)
  {
    Ambient->GetLightComponent()->SetIntensity(AmbientIntensity);
    Ambient->GetLightComponent()->SetVolumetricScatteringIntensity(AmbientVolumeticScattering);
  }

  UGameplayStatics::GetAllActorsOfClass(world, AInstancedFoliageActor::StaticClass(), Found);
  CollisionFilter.AddIgnoredActors(Found);
  for (auto* FoilageActor : Found)
  {
    CollisionFilter.AddIgnoredActor(FoilageActor);
    FoilageActor->GetRootComponent()->GetChildrenComponents(false, PlantInstances);
    for (auto* PlantInstance : PlantInstances)
    {
      UInstancedStaticMeshComponent* RenderMesh = Cast<UInstancedStaticMeshComponent>(PlantInstance);
      if (RenderMesh)
      {
        RenderMesh->SetRenderCustomDepth(true);
        RenderMesh->SetCustomDepthStencilValue(1);
        RenderMesh->SetForcedLodModel(0);
      }
    }
  }
  if (DistanceToLandscape > 0.f)
  {
    auto* landscape = UGameplayStatics::GetActorOfClass(world, ALandscape::StaticClass());
    if (landscape)
    {
      FVector origin, extend;
      landscape->GetActorBounds(true, origin, extend, true);
      LowestLandscapeBound = origin.Z - (extend.Z + 100.f);
      EnsureDistancePreservation();
    }
    else
    {
      DistanceToLandscape = -1.f;
    }
  }
  this->SetActorTickEnabled(true);

  FQuat q1, q2;
  auto concatenate_transformat = q1 * q2;
  auto reverse_second_transform = q1 * (q2.Inverse());
  auto lerp_const_ang_velo = FQuat::Slerp(q1, q2, 0.5f);

  if (AdjustFocalDistance)
  {
    SceneCam->PostProcessSettings.DepthOfFieldBladeCount = 5;
    SceneCam->PostProcessSettings.DepthOfFieldDepthBlurAmount = 5.0f;
    SceneCam->PostProcessSettings.DepthOfFieldDepthBlurRadius = 2.0f;
    SceneCam->PostProcessSettings.DepthOfFieldFstop = 5.0f;
    SceneCam->PostProcessSettings.DepthOfFieldMinFstop = 2.0f;
    InfoCam->PostProcessSettings.DepthOfFieldBladeCount = 5;
    InfoCam->PostProcessSettings.DepthOfFieldDepthBlurAmount = 5.0f;
    InfoCam->PostProcessSettings.DepthOfFieldDepthBlurRadius = 2.0f;
    InfoCam->PostProcessSettings.DepthOfFieldFstop = 5.0f;
    InfoCam->PostProcessSettings.DepthOfFieldMinFstop = 2.0f;
  }

  this->WorldSpawner = Cast<AWorldSpawner>(UGameplayStatics::GetActorOfClass(world, AWorldSpawner::StaticClass()));
  if (WorldSpawner)
  {
    WorldSpawner->ReceiveStreamingCommunicatorRef(this);
  }

  auto* Controller = GetWorld()->GetFirstPlayerController();
  if (Controller)
  {
    Controller->ConsoleCommand(TEXT("Log LogPixelStreaming off"));
  }

}

void ASynavisDrone::PostInitializeComponents()
{
  Super::PostInitializeComponents();
  auto* MatInst = UMaterialInstanceDynamic::Create(PostProcessMat, InfoCam);
  InfoCam->AddOrUpdateBlendable(MatInst);
}

EDataTypeIndicator ASynavisDrone::FindType(FProperty* Property)
{
  if (!Property)
  {
    return EDataTypeIndicator::Transform;
  }
  if (Property->IsA(FFloatProperty::StaticClass()))
  {
    return EDataTypeIndicator::Float;
  }
  else if (Property->IsA(FIntProperty::StaticClass()))
  {
    return EDataTypeIndicator::Int;
  }
  else if (Property->IsA(FBoolProperty::StaticClass()))
  {
    return EDataTypeIndicator::Bool;
  }
  else if (Property->IsA(FStrProperty::StaticClass()))
  {
    return EDataTypeIndicator::String;
  }
  else if (Property->IsA(FStructProperty::StaticClass()))
  {
    if (Property->ContainerPtrToValuePtr<FVector>(this))
    {
      return EDataTypeIndicator::Vector;
    }
    else if (Property->ContainerPtrToValuePtr<FRotator>(this))
    {
      return EDataTypeIndicator::Rotator;
    }
    else if (Property->ContainerPtrToValuePtr<FTransform>(this))
    {
      return EDataTypeIndicator::Transform;
    }
  }
  return EDataTypeIndicator::None;
}

void ASynavisDrone::ApplyOrStoreTexture(TSharedPtr<FJsonObject> Json)
{
  // require fields: dimension, name, format
  // optional fields: data
  int x, y;
  TSharedPtr<FJsonObject> dimension = Json->GetObjectField(TEXT("dimension"));
  x = dimension->GetIntegerField(TEXT("x"));
  y = dimension->GetIntegerField(TEXT("y"));
  FString target = GetStringFieldOr(Json, TEXT("target"), "Diffuse");
  FString name = GetStringFieldOr(Json, TEXT("name"), "Instance");
  UTexture2D* Texture = WorldSpawner->CreateTexture2DFromData(ReceptionBuffer, this->ReceptionBufferSize, x, y);
  UMaterialInstanceDynamic* MatInst = WorldSpawner->GenerateInstanceFromName(name, false);
  MatInst->SetTextureParameterValue(*target, Texture);

  if (Json->HasField(TEXT("object")))
  {
    auto object = GetObjectFromJSON(Json);
    const int index = GetIntFieldOr(Json, TEXT("index"), 0);
    UPrimitiveComponent* MaterialCarryingIdentity;
    if (object->IsA<AActor>())
    {
      MaterialCarryingIdentity = Cast<UPrimitiveComponent>(Cast<AActor>(object)->FindComponentByClass(UPrimitiveComponent::StaticClass()));
    }
    else if (object->IsA<UPrimitiveComponent>())
    {
      MaterialCarryingIdentity = Cast<UPrimitiveComponent>(object);
    }
    else
    {
      return;
    }
    MaterialCarryingIdentity->SetMaterial(index, MatInst);
  }

}

void ASynavisDrone::EnsureDistancePreservation()
{
  TArray<FHitResult> MHits;
  if (GetWorld()->LineTraceMultiByObjectType(MHits, GetActorLocation(), FVector(GetActorLocation().X, GetActorLocation().Y, LowestLandscapeBound), ActorFilter, CollisionFilter))
  {
    for (auto Hit : MHits)
    {
      if (Hit.GetActor()->GetClass() == ALandscape::StaticClass())
      {
        SetActorLocation(FVector(GetActorLocation().X, GetActorLocation().Y, Hit.ImpactPoint.Z + DistanceToLandscape));
      }
    }
  }
}

void ASynavisDrone::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
  Super::EndPlay(EndPlayReason);
  if (WorldSpawner)
  {
    WorldSpawner->ReceiveStreamingCommunicatorRef(nullptr);
  }
  SendResponse(FString("{\"type\":\"closed\""));

}

// Called every frame
void ASynavisDrone::Tick(float DeltaTime)
{
  Super::Tick(DeltaTime);
  // fetch unix time
  const int32 Now = static_cast<int32>(FDateTime::UtcNow().ToUnixTimestamp());
  if (BindPawnToCamera)
    UGameplayStatics::GetPlayerPawn(GetWorld(), 0)->SetActorLocation(GetActorLocation());

  FrameCaptureCounter -= DeltaTime;
  FVector Distance = NextLocation - GetActorLocation();
  if (DistanceToLandscape > 0.f)
  {
    Distance.Z = 0;
  }
  if (FGenericPlatformMath::Abs((Distance).Size()) < 50.f)
  {
    if (AutoNavigate)
      NextLocation = UKismetMathLibrary::RandomPointInBoundingBox(Flyspace->GetComponentLocation(), Flyspace->GetScaledBoxExtent());
    if (IsInEditor() && PrintScreenNewPosition)
    {
      GEngine->AddOnScreenDebugMessage(10, 30.f, FColor::Red, FString::Printf(
        TEXT("L:(%d,%d,%d) - N:(%d,%d,%d) - M:%d/%d"), \
        GetActorLocation().X, GetActorLocation().Y, GetActorLocation().Z, \
        NextLocation.X, NextLocation.Y, NextLocation.Z, MeanVelocityLength, FGenericPlatformMath::Abs((GetActorLocation() - NextLocation).Size())));
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
    {
      SetActorRotation(Velocity.ToOrientationRotator());
      // get the player controller
      APlayerController* PlayerController = UGameplayStatics::GetPlayerController(GetWorld(), 0);
      if (PlayerController)
      {
        // set its rotation
        PlayerController->SetControlRotation(Velocity.ToOrientationRotator());
      }
    }
    if (DistanceToLandscape > 0.f)
    {
      EnsureDistancePreservation();
    }
  }
  FHitResult Hit;
  if (GetWorld()->LineTraceSingleByObjectType(Hit, GetActorLocation(), GetActorLocation() + GetActorForwardVector() * 2000.f, ParamsObject, ParamsTrace))
  {
    TargetFocalLength = FGenericPlatformMath::Min(2000.f, FGenericPlatformMath::Max(0.f, Hit.Distance));
  }
  else
  {
    TargetFocalLength = 2000.f;
  }
  if (FGenericPlatformMath::Abs(TargetFocalLength - FocalLength) > 0.1f)
  {
    FocalLength = (TargetFocalLength - FocalLength) * FocalRate * DeltaTime;
    SceneCam->PostProcessSettings.DepthOfFieldFocalDistance = FocalLength;
    InfoCam->PostProcessSettings.DepthOfFieldFocalDistance = FocalLength;
  }
  if (TransmissionTargets.Num() > 0)
  {
    // rtp timestamp has only 32 bits



    FString Data = FString::Printf(TEXT("{\"type\":\"track\",\"time\":%n,\"data\":{"), Now);

    for (auto i = 0; i < TransmissionTargets.Num(); ++i)
    {
      auto& Target = TransmissionTargets[i];
      if (IsValid(Target.Object))
      {
        Data.Append(FString::Printf(TEXT("\"%s\":"), *Target.Name));
        // read the property
        switch (Target.DataType)
        {
        case EDataTypeIndicator::Float:
          Data.Append(FString::Printf(TEXT("%f"), Target.Property->ContainerPtrToValuePtr<float>(Target.Object)));
          break;
        case EDataTypeIndicator::Int:
          Data.Append(FString::Printf(TEXT("%d"), Target.Property->ContainerPtrToValuePtr<int>(Target.Object)));
          break;
        case EDataTypeIndicator::Bool:
          Data.Append(FString::Printf(TEXT("%s"), Target.Property->ContainerPtrToValuePtr<bool>(Target.Object) ? TEXT("true") : TEXT("false")));
          break;
        case EDataTypeIndicator::String:
          Data.Append(FString::Printf(TEXT("\"%s\""), **Target.Property->ContainerPtrToValuePtr<FString>(Target.Object)));
          break;
        case EDataTypeIndicator::Transform:
          Data.Append(PrintFormattedTransform(Target.Object));
          break;
        case EDataTypeIndicator::Vector:
          Data.Append(FString::Printf(TEXT("[%f,%f,%f]"), Target.Property->ContainerPtrToValuePtr<FVector>(Target.Object)->X, Target.Property->ContainerPtrToValuePtr<FVector>(Target.Object)->Y, Target.Property->ContainerPtrToValuePtr<FVector>(Target.Object)->Z));
          break;
        case EDataTypeIndicator::Rotator:
          Data.Append(FString::Printf(TEXT("[%f,%f,%f]"), Target.Property->ContainerPtrToValuePtr<FRotator>(Target.Object)->Pitch, Target.Property->ContainerPtrToValuePtr<FRotator>(Target.Object)->Yaw, Target.Property->ContainerPtrToValuePtr<FRotator>(Target.Object)->Roll));
          break;
        }
        if (i < TransmissionTargets.Num() - 1)
          Data.Append(TEXT(","));
      }
    }
    Data.Append(TEXT("}}"));
    SendResponse(Data);
  }

  // prepare texture for storage
  if (FrameCaptureTime > 0.f && FrameCaptureCounter <= 0.f)
  {
    SendRawFrame(nullptr, false);
    FrameCaptureCounter = FrameCaptureTime;
  }

  if (LastProgress >= 0 && ReceptionBufferOffset < ReceptionBufferSize)
  {
    // ReceptionBufferOffset is our chunk, LastProgress is last received chucnk
    FString Response = TEXT("{\"type\":\"receive\",\"data\":\"");
    auto ChunkSize = ReceptionFormat.Len() / ReceptionBufferSize;
    const auto Lower = ChunkSize * ReceptionBufferOffset;
    auto Upper = FGenericPlatformMath::Min(ChunkSize * (ReceptionBufferOffset + 1), (uint64_t)ReceptionFormat.Len());
    if ((ReceptionFormat.Len() - Upper) < ReceptionBufferSize)
    {
      Upper = ReceptionFormat.Len();
      LastProgress = -1;
    }
    const auto chunk = ReceptionFormat.Mid(Lower, Upper - Lower);
    Response += chunk;
    Response += TEXT("\", \"chunk\":\"");
    Response += FString::FromInt(ReceptionBufferOffset);
    Response += TEXT("/");
    Response += FString::FromInt(ReceptionBufferSize);
    Response += TEXT("\"}");
    ReceptionBufferOffset++;
    if (LogResponses)
    {
      UE_LOG(LogActor, Warning, TEXT("Sending chunk %d of %d"), ReceptionBufferOffset, ReceptionBufferSize);
      UE_LOG(LogActor, Warning, TEXT("First and last 20 charactes of chunk %d: %s"), ReceptionBufferOffset, *(chunk.Mid(0, 20) + TEXT("...") + chunk.Mid(chunk.Len() - 20, 20)))
    }
    SendResponse(Response);
  }

  for (auto& Task : ScheduledTasks)
  {
    Task.Get<0>() -= DeltaTime;
    if (Task.Get<0>() <= 0.0)
    {
      JsonCommand(Task.Get<2>(), -1);
      if (Task.Get<1>() > 0)
      {
        Task.Get<0>() = Task.Get<1>();
      }
      else
      {
        Task.Get<0>() = -1;
      }
    }
  }
  ScheduledTasks.RemoveAll([](const TTuple<double, double, TSharedPtr<FJsonObject>>& Task) { return Task.Get<0>() <= 0.0; });
}
