#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Interface.h"
#include "SkyBoxGeneratorCamera.generated.h"

class UCameraComponent;
class SkyBoxJob;
class ACameraActor;
class USceneCapturer;
class USkyBoxSceneCapturer;

#define SCENE_CAPTURE_CLASS USceneCapturer

UCLASS(config = Game)
class SKYBOXGENERATOR_API /*拷贝后要改这个宏*/ ASkyBoxGeneratorCamera : public ACharacter, public IGameShutDownNotify
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
    UCameraComponent* m_CaptureCameraComponent;

    ACameraActor* m_CaptureCameraActor;
    bool m_UseActorToCapture;
    bool m_UseHighResShot;
    bool m_UsePanoramicCapture;
    SCENE_CAPTURE_CLASS* m_SceneCapturerObject;

public:
	ASkyBoxGeneratorCamera();
    ~ASkyBoxGeneratorCamera();

protected:
	virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason);
    virtual void ShutDown(); /*IGameShutDownNotify*/

public:
    virtual void Tick(float DeltaTime) override;
    virtual bool ShouldTickIfViewportsOnly() const override;
    virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
private:
    void CreateCaptureCameraActor();
    void SetViewTarget();
    void SetCaptureCameraLocation(FVector location);
    void SetCaptureCameraRotation(FRotator rotation);
    void OnScreenshotProcessed_RenderThread();
    void OnBackBufferReady_RenderThread(SWindow& SlateWindow, const FTexture2DRHIRef& BackBuffer);
    void CaptureBackBufferToPNG(const FTexture2DRHIRef& BackBuffer);
    bool SavePNGToFile();
    void OnSkyBoxCaptureDone_RenderThread();
    TArray<FRotator> m_SixDirection;
    enum CaptureState
    {
        Invalid = 0,
        //使用USceneCapturer
        Waiting_v2,
        //其他
        Waiting1,
        Prepared,
        Captured,
        Saved,
    };
    FCriticalSection m_lock;
    SkyBoxJob* m_current_job;
    int32 m_CurrentDirection;
    CaptureState m_CurrentState;
    TArray<FColor> m_BackBufferData;
    uint32 m_BackBufferSizeX;
    uint32 m_BackBufferSizeY;
    FString m_BackBufferFilePath;
};
