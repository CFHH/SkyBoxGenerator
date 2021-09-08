#pragma once

#include "CoreMinimal.h"
#include "Engine/PostProcessVolume.h"
#include "Tickable.h"
#include "SkyBoxSceneCapturer.generated.h"

class IImageWrapperModule;
class APlayerController;
class AGameModeBase;
class USceneCaptureComponent2D;


USTRUCT()
struct FPostProcessVolumeData
{
    GENERATED_BODY()
    UPROPERTY()
    APostProcessVolume* Object;
    bool WasEnabled;
};

enum class ECaptureStep : uint8
{
    Unpause,
    Pause,
    SetStartPosition,
    SetPosition,
    Read,
    Reset,
};

enum class ERenderPass : uint8
{
    FinalColor,
    SceneDepth,
};

DECLARE_MULTICAST_DELEGATE(FOnSkyBoxCaptureDone);


UCLASS()
class USkyBoxSceneCapturer : public UObject, public FTickableGameObject
{
    GENERATED_BODY()

public:
    USkyBoxSceneCapturer(FVTableHelper& Helper);
    USkyBoxSceneCapturer();
    virtual ~USkyBoxSceneCapturer();

    static FOnSkyBoxCaptureDone& OnSkyBoxCaptureDone() { return m_OnSkyBoxCaptureDoneDelegate; }

public: //FTickableGameObject
    virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Always; }
    virtual bool IsTickableWhenPaused() const override { return m_IsTicking; }
    virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(USkyBoxSceneCapturer, STATGROUP_Tickables); }
    virtual UWorld* GetTickableGameObjectWorld() const override;
    virtual void Tick(float DeltaTime) override;

public:
    void Initialize(int CaptureWidth, int CaptureHeight, float CaptureFov);
    bool StartCapture(FVector CapturePosition, FString FileNamePrefix);
    void Reset();
    void Cleanup();

private:
    void CacheAllPostProcessVolumes();
    void DisableAllPostProcessVolumes();
    void EnableAllPostProcessVolumes();

    void Init2DCaptureComponent(USceneCaptureComponent2D* CaptureComponent);
    void DisableUnsupportedPostProcesses(USceneCaptureComponent2D* CaptureComponent);
    void SetCaptureComponentRequirements(USceneCaptureComponent2D* CaptureComponent);

private:
    IImageWrapperModule& m_ImageWrapperModule;

    int m_CaptureWidth;
    int m_CaptureHeight;
    float m_CaptureFov;
    static FOnSkyBoxCaptureDone m_OnSkyBoxCaptureDoneDelegate;

    UPROPERTY(Transient)
    TArray <FPostProcessVolumeData> m_PPVolumeArray;

    UPROPERTY(Transient)
    USceneComponent* m_SceneComponent;

    TArray<USceneCaptureComponent2D*> m_2DCaptureComponents;

    TArray<ERenderPass> m_RenderPasses;

    int m_CurrentRenderPassIndex;
    ECaptureStep m_CurrentCaptureStep;

    FVector m_CapturePosition;
    FString m_FileNamePrefix;

    APlayerController* m_CapturePlayerController;
    AGameModeBase* m_CaptureGameMode;
    bool m_IsTicking;
};