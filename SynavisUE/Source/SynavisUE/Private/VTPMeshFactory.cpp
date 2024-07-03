// Fill out your copyright notice in the Description page of Project Settings.


#include "VTPMeshFactory.h"

#include "ProceduralMeshComponent.h"
#include "Components/OctreeDynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Misc/Base64.h"

THIRD_PARTY_INCLUDES_START


THIRD_PARTY_INCLUDES_END

#include "Components/OctreeDynamicMeshComponent.h"

#define AVAIL_POINTS 0b00000001
#define AVAIL_NORMALS 0b00000010
#define AVAIL_TANGENTS 0b00000100
#define AVAIL_TRIANGLES 0b00001000
#define AVAIL_UV 0b00001000

#define VALID_GEOM AVAIL_POINTS | AVAIL_NORMALS | AVAIL_TRIANGLES;

VTPMeshFactory::VTPMeshFactory()
{
}

VTPMeshFactory::~VTPMeshFactory()
{
}

UProceduralMeshComponent* VTPMeshFactory::LoadFile(FString inFileName)
{
  return NewObject<UProceduralMeshComponent>();
#if 0
  uint8 ArrayAvailable = 0;
  UProceduralMeshComponent* output = NewObject<UProceduralMeshComponent>();
  tinyxml2::XMLDocument doc; 
  doc.LoadFile(TCHAR_TO_ANSI(*inFileName));

  auto* VTKFile = doc.FirstChild();
  auto* PolyData = VTKFile->FirstChild();
  auto * AppendedData = PolyData->NextSibling();
  auto* Piece = AppendedData->FirstChild();
  int64 NumberOfPoints = Piece->ToElement()->Int64Attribute("NumberOfPoints");
  int64 NumberOfPolys = Piece->ToElement()->Int64Attribute("NumberOfPolys");
  auto* Points = PolyData->FirstChildElement("Points")->FirstChildElement("DataArray");
  auto* PointData = PolyData->FirstChildElement("PointData");
  FString ReadingMode = ANSI_TO_TCHAR(Points->Attribute("Encoding"));
  auto* CellData = PolyData->FirstChildElement("CellData");
  int64 PointOffset = Points->Int64Attribute("offset");
  auto* Polys = PolyData->FirstChildElement("Polys")->FirstChildElement("DataArray");
  auto* PolyOffsets = Polys->NextSiblingElement();
  if (strcmp(Polys->Attribute("Name"),"connectivity") == 0)
  {
    Swap(Polys, PolyOffsets);
  }
  TArray<FVector> MeshPoints, Normals;
  TArray<int> Triangles;
  TArray<FVector2D> UV;
  TArray<FColor> Colors;
  TArray<FProcMeshTangent> Tangents;
  //int64 PolyStart, PolyOffset;
  int64 UVOffset, NormalOffset, TangentOffset;
  if (ReadingMode == "AppendedData")
  {
    for (auto* DataArray = PointData->FirstChild(); DataArray->NextSibling(); DataArray = DataArray->NextSibling())
    {
      if (strcmp(DataArray->ToElement()->Attribute("Name"), "Texture Coordinates"))
      {
        UVOffset = DataArray->ToElement()->Int64Attribute("offset");
        ArrayAvailable |= AVAIL_UV;
      }
      else if (strcmp(DataArray->ToElement()->Attribute("Name"), "Normals"))
      {
        NormalOffset = DataArray->ToElement()->Int64Attribute("offset");
        ArrayAvailable |= AVAIL_NORMALS;
      }
      else if (strcmp(DataArray->ToElement()->Attribute("Name") , "Tangents"))
      {
        TangentOffset = DataArray->ToElement()->Int64Attribute("offset");
        ArrayAvailable |= AVAIL_TANGENTS;
      }
    }
    const char* raw = AppendedData->ToElement()->GetText();

    // Assemble points

    const float* pointpointer = reinterpret_cast<const float*>(raw[PointOffset * sizeof(float)]);
    for (int64 i = 0; i < NumberOfPoints; i += 3)
    {
      MeshPoints.Add(FVector(pointpointer[i], pointpointer[i + 1], pointpointer[i + 2]));
    }
  }
  else if (ReadingMode == "Binary")
  {
    FString Base64Points = ANSI_TO_TCHAR(Points->GetText());
    TArray<uint8> BinaryData;
    FBase64::Decode(Base64Points,BinaryData);
    TArray<float> PointArray(reinterpret_cast<float*>(BinaryData.GetData()),BinaryData.Num() / sizeof(float));
    FString Base64Polys = ANSI_TO_TCHAR(Polys->GetText());
    for(int i = 0; i < PointArray.Num() / 3; ++i)
    {
      MeshPoints.Add(FVector(PointArray[3*i + 0], PointArray[3*i + 1], PointArray[3*i + 2]));
    }
    BinaryData.Empty();
    FBase64::Decode(Base64Polys,BinaryData);
    TArray<int32> PolyArray(reinterpret_cast<int32*>(BinaryData.GetData()), BinaryData.Num() / sizeof(int32));

    BinaryData.Empty();

    auto ArrayFinder = [PointData](const char* Name)-> tinyxml2::XMLElement*
    {
      for (auto* child = PointData->FirstChildElement(); child != nullptr; child = child->NextSiblingElement())
      {
        if (strcmp(child->Attribute("Name"), Name) == 0)
        {
          return child;
        }
      }
      return nullptr;
    };

    tinyxml2::XMLElement* FoundElement = ArrayFinder("Normals");
    if(!FoundElement)
    {
      UE_LOG(LogTemp, Warning, TEXT("Did not find mesh vertex normals"));
    }
    FBase64::Decode(Base64Polys, BinaryData);
    TArray<float> NormalArray(reinterpret_cast<float*>(BinaryData.GetData()), BinaryData.Num() / sizeof(float));
    for(int i = 0; i < NumberOfPoints; ++i)
    {
      Normals.Add({ NormalArray[i * 3 + 0],NormalArray[i * 3 + 1] ,NormalArray[i * 3 + 2] });
    }
    
    output->CreateMeshSection(0,MeshPoints,PolyArray,Normals,UV,Colors,Tangents,false);
  }
  return output;
#endif
}
