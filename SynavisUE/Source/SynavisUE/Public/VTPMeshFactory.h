// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

/**
 * 
 */
class SYNAVISUE_API VTPMeshFactory
{
public:
	VTPMeshFactory();
	~VTPMeshFactory();

	class UProceduralMeshComponent* LoadFile(FString inFileName);
};
