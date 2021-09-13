#pragma once
#include "CoreMinimal.h"
#include "Materials/Material.h"
#include "Tickable.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/PostProcessVolume.h"
#include "SceneCapturer.generated.h"


class IImageWrapperModule;

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
	Reset,
    SetStartPosition,
	SetPosition,
	Read,
	Pause,
	Unpause
};

enum class ERenderPass : uint8
{
	WorldNormal,
	AO,
	BaseColor,
	Metallic,
	Roughness,
	FinalColor,
	SceneDepth
};

DECLARE_MULTICAST_DELEGATE(FOnSkyBoxCaptureDone);


UCLASS()
class USceneCapturer : public UObject, public FTickableGameObject
{
    GENERATED_BODY()
public:
    static FOnSkyBoxCaptureDone& OnSkyBoxCaptureDone() { return m_OnSkyBoxCaptureDoneDelegate; }
    USceneCapturer(FVTableHelper& Helper);
    USceneCapturer();
    virtual ~USceneCapturer();
    
public:
	//~ FTickableGameObject interface
    virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Always; }
    virtual bool IsTickableWhenPaused() const override { return bIsTicking; }
    virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(USkyBoxSceneCapturer, STATGROUP_Tickables); }
    virtual UWorld* GetTickableGameObjectWorld() const override;
    virtual void Tick(float DeltaTime) override;

public:
    void Initialize(int inCaptureWidth, int inCaptureHeight, float inCaptureFov);
    void Reset();
    bool StartCapture(FVector CapturePosition, FString FileNamePrefix);

private:
    void CacheAllPostProcessVolumes();
    void DisableAllPostProcessVolumes();
    void EnablePostProcessVolumes();

    void InitCaptureComponent(USceneCaptureComponent2D* CaptureComponent);
    void DisableUnsupportedPostProcesses(USceneCaptureComponent2D* CaptureComponent);
    void SetCaptureComponentRequirements(int32 CaptureIndex);
	void CaptureScene(int32 CaptureIndex);
    FString GetCurrentRenderPassName();

private:
    static FOnSkyBoxCaptureDone m_OnSkyBoxCaptureDoneDelegate;
    IImageWrapperModule& ImageWrapperModule;
    const int32 OutputBitDepth;

    UPROPERTY(Transient)
        TArray <FPostProcessVolumeData> PPVolumeArray;

    UPROPERTY(Transient)
        USceneComponent* CaptureSceneComponent;

    TArray<USceneCaptureComponent2D*> LeftEyeCaptureComponents;

    TArray<ERenderPass> RenderPasses;

    int CurrentRenderPassIndex;
    ECaptureStep CaptureStep;

    FString m_FileNamePrefix;


    bool bIsTicking;
    APlayerController* CapturePlayerController;
    AGameModeBase* CaptureGameMode;

	FDateTime OverallStartTime;
	FDateTime StartTime;

	FString Timestamp;

    const FString OutputDir;
};
