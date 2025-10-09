// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SynavisStreamer.generated.h"


UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class SYNAVISBACKEND_API USynavisStreamer : public UActorComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	USynavisStreamer();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	/** Blueprint-accessible API to configure and control the streamer */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming")
	class UTextureRenderTarget2D* RenderTarget;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming")
	float CaptureFPS = 30.0f;

	// Encoding always uses VP9 when available
	UFUNCTION(BlueprintCallable, Category = "Streaming")
	void StartStreaming();

	UFUNCTION(BlueprintCallable, Category = "Streaming")
	void StopStreaming();

	UFUNCTION(BlueprintCallable, Category = "Streaming")
	void SetRenderTarget(class UTextureRenderTarget2D* InTarget);

	UFUNCTION(BlueprintCallable, Category = "Streaming")
	void SetCaptureFPS(float FPS);

	// no setter: encoding uses VP9 by default

	// Signalling server configuration for WebRTC (ws://host:port)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming|Signalling")
	FString SignallingIP;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming|Signalling")
	int32 SignallingPort = 9000;

	UFUNCTION(BlueprintCallable, Category = "Streaming|Signalling")
	void StartSignalling();

protected:
	// timer callback to capture frames
	void CaptureFrame();

	// Encode a frame to VP9 and send it directly via WebRTC (returns true if encoded+sent)
	bool EncodeFrameToVP9AndSend(const TArray<FColor>& Pixels, int Width, int Height);

	// helper to send bytes via DataConnector
	void SendFrameBytes(const uint8_t* Bytes, size_t Size, const std::string& Name, const std::string& Format);

	// internal state
	bool bStreaming = false;

	// Opaque pimpl for WebRTC internals (defined in cpp)
	struct FWebRTCInternal;
	std::unique_ptr<FWebRTCInternal> WebRTCInternal;
};
