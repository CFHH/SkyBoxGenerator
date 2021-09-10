#include "SkyBoxSceneCapturer.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/BlendableInterface.h"
#include "Engine/LatentActionManager.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/DefaultPawn.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "ImageUtils.h"
#include "Kismet/GameplayStatics.h"
#include "LatentActions.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "TextureResource.h"
#include "UnrealEngine.h"
#include "UObject/ConstructorHelpers.h"

#define CAPTURE_WIDTH 720
#define CAPTURE_HIGHT 720
#define CAPTURE_FOV 120.0f
#define CONCURRENT_CAPTURES_COUNT 6

FOnSkyBoxCaptureDone USkyBoxSceneCapturer::m_OnSkyBoxCaptureDoneDelegate;

//Adding this ctor hack to fix the 4.8p2 problem with hot reload macros calling empty constructors
//  https://answers.unrealengine.com/questions/228042/48p2-compile-fails-on-class-with-non-default-const.html
USkyBoxSceneCapturer::USkyBoxSceneCapturer(FVTableHelper& Helper)
    : Super(Helper)
    , m_ImageWrapperModule(FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper")))
{
    UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！USkyBoxSceneCapturer::USkyBoxSceneCapturer(FVTableHelper& Helper)"));
    m_CapturePlayerController = nullptr;
    m_CaptureGameMode = nullptr;
    m_IsTicking = false;
}

USkyBoxSceneCapturer::USkyBoxSceneCapturer()
    : m_ImageWrapperModule(FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper")))
{
    UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！USkyBoxSceneCapturer::USkyBoxSceneCapturer()"));
    m_CapturePlayerController = nullptr;
    m_CaptureGameMode = nullptr;
    m_IsTicking = false;

    //Ensure that the main gameview is > CaptureWidth x CaptureHeight. Bug in UE4 that won't re-alloc scene render targets to the correct size
    //  when the scenecapture component > current window render target. https://answers.unrealengine.com/questions/80531/scene-capture-2d-max-resolution.html
    //Ensure that r.SceneRenderTargetResizeMethod=2
    FSystemResolution::RequestResolutionChange(CAPTURE_WIDTH, CAPTURE_HIGHT, EWindowMode::Windowed);

    CacheAllPostProcessVolumes();

    m_SceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("SkyBoxCaptureSceneComponent"));
    m_SceneComponent->AddToRoot();

    for (int CaptureIndex = 0; CaptureIndex < CONCURRENT_CAPTURES_COUNT; CaptureIndex++)
    {
        FString Name = FString::Printf(TEXT("CaptureComponent_%04d"), CaptureIndex);
        USceneCaptureComponent2D* CaptureComponent = CreateDefaultSubobject<USceneCaptureComponent2D>(*Name);
        CaptureComponent->bTickInEditor = false;
        CaptureComponent->SetComponentTickEnabled(false);
        CaptureComponent->AttachToComponent(m_SceneComponent, FAttachmentTransformRules::KeepRelativeTransform);
        Init2DCaptureComponent(CaptureComponent);
        m_2DCaptureComponents.Add(CaptureComponent);
    }
    ////这里设置无效，且会影响后面的设置（若设相同数值）
    //m_2DCaptureComponents[0]->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, 0.0f), FRotator(0.0f, 0.0f, 0.0f));  //前
    //m_2DCaptureComponents[1]->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, 0.0f), FRotator(0.0f, 90.0f, 0.0f));  //右
    //m_2DCaptureComponents[2]->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, 0.0f), FRotator(0.0f, 180.0f, 0.0f));  //后
    //m_2DCaptureComponents[3]->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, 0.0f), FRotator(0.0f, -90.0f, 0.0f));  //左
    //m_2DCaptureComponents[4]->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, 0.0f), FRotator(90.0f, 0.0f, 0.0f));  //上
    //m_2DCaptureComponents[5]->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, 0.0f), FRotator(-90.0f, 0.0f, 0.0f));  //下

    m_RenderPasses.Add(ERenderPass::FinalColor);
    //m_RenderPasses.Add(ERenderPass::SceneDepth);
    m_CurrentRenderPassIndex = 0;
    m_CurrentCaptureStep = ECaptureStep::Unpause;
}

USkyBoxSceneCapturer::~USkyBoxSceneCapturer()
{
    UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！USkyBoxSceneCapturer::~USkyBoxSceneCapturer()"));
}

void USkyBoxSceneCapturer::Initialize(int CaptureWidth, int CaptureHeight, float CaptureFov)
{
    m_CaptureWidth = CaptureWidth;
    m_CaptureHeight = CaptureHeight;
    m_CaptureFov = CaptureFov;
}

void USkyBoxSceneCapturer::Reset()
{
    UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！USkyBoxSceneCapturer::Cleanup()"));
    m_IsTicking = false;
    for (int CaptureIndex = 0; CaptureIndex < CONCURRENT_CAPTURES_COUNT; CaptureIndex++)
    {
        USceneCaptureComponent2D* CaptureComponent = m_2DCaptureComponents[CaptureIndex];
        CaptureComponent->SetVisibility(false);
        CaptureComponent->SetHiddenInGame(true);
        CaptureComponent->RemoveFromRoot();
    }
    m_SceneComponent->RemoveFromRoot();
}

bool USkyBoxSceneCapturer::StartCapture(FVector CapturePosition, FString FileNamePrefix)
{
    UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！USkyBoxSceneCapturer::StartCapture(), WORLD = %d, %d"), GetWorld(), m_SceneComponent->GetWorld());
    if (m_IsTicking)
    {
        UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！USkyBoxSceneCapturer::StartCapture(), Already capturing a scene; concurrent captures are not allowed"));
        return false;
    }

    m_CapturePlayerController = UGameplayStatics::GetPlayerController(GetWorld(), 0);
    m_CaptureGameMode = UGameplayStatics::GetGameMode(GetWorld());
    if (m_CapturePlayerController == NULL || m_CaptureGameMode == NULL)
    {
        UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！USkyBoxSceneCapturer::StartCapture(), Missing PlayerController or GameMode"));
        return false;
    }

    m_CapturePosition = CapturePosition;
    m_FileNamePrefix = FileNamePrefix;
    m_IsTicking = true;
    m_CurrentRenderPassIndex = 0;
    m_CurrentCaptureStep = ECaptureStep::Unpause;
    return true;
}

UWorld* USkyBoxSceneCapturer::GetTickableGameObjectWorld() const
{
    if (m_2DCaptureComponents.Num() > 0 && !m_SceneComponent->IsPendingKill())
    {
        return m_SceneComponent->GetWorld();
    }
    return nullptr;
}

void USkyBoxSceneCapturer::Tick(float DeltaTime)
{
    if (!m_IsTicking)
        return;

    if (m_CurrentCaptureStep == ECaptureStep::Unpause)  //TODO 应该可以直接进入pause
    {
        UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！USkyBoxSceneCapturer::Tick(), ECaptureStep::Unpause, WORLD = %d"), m_SceneComponent->GetWorld());
        FlushRenderingCommands();
        m_CaptureGameMode->ClearPause();
        //GPauseRenderingRealtimeClock = false;
        m_CurrentCaptureStep = ECaptureStep::Pause;
        FlushRenderingCommands();
    }
    else if (m_CurrentCaptureStep == ECaptureStep::Pause)
    {
        UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！USkyBoxSceneCapturer::Tick(), ECaptureStep::Pause, WORLD = %d"), m_SceneComponent->GetWorld());
        FlushRenderingCommands();
        if (!m_CaptureGameMode)  // To prevent following process when tick at the time of PIE ends and m_CaptureGameMode is no longer valid.
            return;
        m_CaptureGameMode->SetPause(m_CapturePlayerController);
        //GPauseRenderingRealtimeClock = true;
        m_CurrentCaptureStep = ECaptureStep::SetStartPosition;
        FlushRenderingCommands();
    }
    else if (m_CurrentCaptureStep == ECaptureStep::SetStartPosition)
    {
        UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！USkyBoxSceneCapturer::Tick(), ECaptureStep::SetStartPosition, WORLD = %d"), m_SceneComponent->GetWorld());
        ENQUEUE_RENDER_COMMAND(SkyBoxSceneCapturer_HeartbeatTickTickables)(
            [](FRHICommandList& RHICmdList)
        {
            TickRenderingTickables();
        });
        FlushRenderingCommands();

        //FVector PlayerLocation;  //TODO 不对
        //FRotator PlayerRotation;
        //m_CapturePlayerController->GetPlayerViewPoint(PlayerLocation, PlayerRotation);
        m_SceneComponent->SetWorldLocationAndRotation(m_CapturePosition, FRotator(0.0f, 0.0f, 0.0f));
        UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！USkyBoxSceneCapturer::Tick(), SetWorldLocation, (%.1f，%.1f，%.1f)"), m_CapturePosition.X, m_CapturePosition.Y, m_CapturePosition.Z);

        for (int CaptureIndex = 0; CaptureIndex < CONCURRENT_CAPTURES_COUNT; CaptureIndex++)
            SetCaptureComponentRequirements(m_2DCaptureComponents[CaptureIndex]);

        if (m_RenderPasses[m_CurrentRenderPassIndex] != ERenderPass::FinalColor)
            DisableAllPostProcessVolumes();
        else
            EnableAllPostProcessVolumes();

        m_CurrentCaptureStep = ECaptureStep::SetPosition;
        FlushRenderingCommands();
    }
    else if (m_CurrentCaptureStep == ECaptureStep::SetPosition)  //TODO 这步可以合并到上一步
    {
        UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！USkyBoxSceneCapturer::Tick(), ECaptureStep::SetPosition, WORLD = %d"), m_SceneComponent->GetWorld());
        FlushRenderingCommands();
        m_2DCaptureComponents[0]->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, 0.0f), FRotator(0.0f, 0.0f, 0.0f));  //前
        m_2DCaptureComponents[1]->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, 0.0f), FRotator(0.0f, 90.0f, 0.0f));  //右
        m_2DCaptureComponents[2]->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, 0.0f), FRotator(0.0f, 180.0f, 0.0f));  //后
        m_2DCaptureComponents[3]->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, 0.0f), FRotator(0.0f, -90.0f, 0.0f));  //左
        m_2DCaptureComponents[4]->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, 0.0f), FRotator(90.0f, 0.0f, 0.0f));  //上
        m_2DCaptureComponents[5]->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, 0.0f), FRotator(-90.0f, 0.0f, 0.0f));  //下
        for (int32 CaptureIndex = 0; CaptureIndex < CONCURRENT_CAPTURES_COUNT; CaptureIndex++)
        {
            m_2DCaptureComponents[CaptureIndex]->CaptureSceneDeferred(); //Render the scene to the texture the next time the main view is rendered
        }
        m_CurrentCaptureStep = ECaptureStep::Read;
        FlushRenderingCommands();
    }
    else if (m_CurrentCaptureStep == ECaptureStep::Read)
    {
        UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！USkyBoxSceneCapturer::Tick(), ECaptureStep::Read, WORLD = %d"), m_SceneComponent->GetWorld());
        FlushRenderingCommands();

        FVector location1 = m_SceneComponent->GetComponentLocation();
        UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！USkyBoxSceneCapturer::Tick(), Read, m_SceneComponent (%.1f，%.1f，%.1f)"),
            location1.X, location1.Y, location1.Z);

        for (int32 CaptureIndex = 0; CaptureIndex < CONCURRENT_CAPTURES_COUNT; CaptureIndex++)
        {
            USceneCaptureComponent2D* CaptureComponent = m_2DCaptureComponents[CaptureIndex];

            FVector location = CaptureComponent->GetComponentLocation();
            FRotator rotation = CaptureComponent->GetComponentRotation();
            UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！USkyBoxSceneCapturer::Tick(), Read, %d, (%.1f，%.1f，%.1f), (%.1f，%.1f，%.1f), WORLD = %d"),
                CaptureIndex, location.X, location.Y, location.Z, rotation.Roll, rotation.Yaw, rotation.Pitch, CaptureComponent->GetWorld());

            FTextureRenderTargetResource* RenderTarget = CaptureComponent->TextureTarget->GameThread_GetRenderTargetResource();
            uint32 CaptureWidth = RenderTarget->GetSizeX();
            uint32 CaptureHeight = RenderTarget->GetSizeY();
            TArray<FLinearColor> SurfaceData;
            FReadSurfaceDataFlags readSurfaceDataFlags = FReadSurfaceDataFlags();
            RenderTarget->ReadLinearColorPixels(SurfaceData, readSurfaceDataFlags);

            if (m_RenderPasses[m_CurrentRenderPassIndex] == ERenderPass::FinalColor)
            {
                FString FileName = FString::Printf(TEXT("%s-COLOR-%d.png"), *m_FileNamePrefix, CaptureIndex);
                UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！USkyBoxSceneCapturer::Tick(), ECaptureStep::Read, %s"), *FileName);
                TArray<FColor> CombinedAtlas8bit;
                for (FLinearColor& Color : SurfaceData)
                {
                    FColor t = Color.Quantize();
                    CombinedAtlas8bit.Add(t);
                }
                TSharedPtr<IImageWrapper> ImageWrapper = m_ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
                ImageWrapper->SetRaw(CombinedAtlas8bit.GetData(), CombinedAtlas8bit.GetAllocatedSize(), CaptureWidth, CaptureHeight, ERGBFormat::BGRA, 8);
                const TArray64<uint8>& ImageData = ImageWrapper->GetCompressed(100);
                FFileHelper::SaveArrayToFile(ImageData, *FileName);
                ImageWrapper.Reset();
            }
            else if (m_RenderPasses[m_CurrentRenderPassIndex] == ERenderPass::SceneDepth)
            {
                FString FileName = FString::Printf(TEXT("%s-DEPTH-%d.png"), *m_FileNamePrefix, CaptureIndex);
                UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！USkyBoxSceneCapturer::Tick(), ECaptureStep::Read, %s"), *FileName);
                for (FLinearColor& Color : SurfaceData)
                {
                    // unpack 32bit scene depth from 4 channels RGBA
                    Color.R = 1.0f - (Color.R + (Color.G / 255.0f) + (Color.B / 65025.0f) + (Color.A / 16581375.0f));	// unpack depth
                    Color.R = FMath::Pow(Color.R, 0.4545f);																// linear to srgb
                    Color.G = Color.R;
                    Color.B = Color.R;
                    Color.A = 1.0f;
                }
                TSharedPtr<IImageWrapper> ImageWrapper = m_ImageWrapperModule.CreateImageWrapper(EImageFormat::EXR);
                ImageWrapper->SetRaw(SurfaceData.GetData(), SurfaceData.GetAllocatedSize(), CaptureWidth, CaptureHeight, ERGBFormat::RGBA, 32);
                const TArray64<uint8>& ImageData = ImageWrapper->GetCompressed((int32)EImageCompressionQuality::Default);
                FFileHelper::SaveArrayToFile(ImageData, *FileName);
                ImageWrapper.Reset();
            }
        }

        FlushRenderingCommands();

        m_CurrentRenderPassIndex++;
        if (m_CurrentRenderPassIndex < m_RenderPasses.Num())
        {
            UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！USkyBoxSceneCapturer::Tick(), NEXT RenderPass！！！！！！！！！！"));
            m_CurrentCaptureStep = ECaptureStep::SetStartPosition;
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！USkyBoxSceneCapturer::Tick(), FINISH！！！！！！！！！！"));
            m_IsTicking = false;
            m_CurrentCaptureStep = ECaptureStep::Unpause;
            m_CaptureGameMode->ClearPause();
            EnableAllPostProcessVolumes();
            m_OnSkyBoxCaptureDoneDelegate.Broadcast();
        }
    }
    else
    {
    }
}


/**********************************************************************************************************************/
//帮助函数
/**********************************************************************************************************************/

void USkyBoxSceneCapturer::CacheAllPostProcessVolumes()
{
    TArray<AActor*> actors;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), APostProcessVolume::StaticClass(), actors);
    for (AActor* actor : actors)
    {
        APostProcessVolume * ppv = CastChecked<APostProcessVolume>(actor);
        FPostProcessVolumeData ppv_data;
        ppv_data.Object = ppv;
        ppv_data.WasEnabled = ppv->bEnabled;
        m_PPVolumeArray.Add(ppv_data);
    }
}

void USkyBoxSceneCapturer::DisableAllPostProcessVolumes()
{
    for (FPostProcessVolumeData &ppv_data : m_PPVolumeArray)
    {
        ppv_data.Object->bEnabled = false;
    }
}

void USkyBoxSceneCapturer::EnableAllPostProcessVolumes()
{
    for (FPostProcessVolumeData &ppv_data : m_PPVolumeArray)
    {
        ppv_data.Object->bEnabled = ppv_data.WasEnabled;
    }
}

void USkyBoxSceneCapturer::Init2DCaptureComponent(USceneCaptureComponent2D* CaptureComponent)
{
    CaptureComponent->SetVisibility(true);
    CaptureComponent->SetHiddenInGame(false);
    CaptureComponent->CaptureStereoPass = EStereoscopicPass::eSSP_FULL;
    CaptureComponent->FOVAngle = m_CaptureFov;
    CaptureComponent->bCaptureEveryFrame = false;
    CaptureComponent->bCaptureOnMovement = false;
    CaptureComponent->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
    DisableUnsupportedPostProcesses(CaptureComponent);

    const FName TargetName = MakeUniqueObjectName(this, UTextureRenderTarget2D::StaticClass(), TEXT("SceneCaptureTextureTarget"));
    CaptureComponent->TextureTarget = NewObject<UTextureRenderTarget2D>(this, TargetName);
    CaptureComponent->TextureTarget->InitCustomFormat(CAPTURE_WIDTH, CAPTURE_HIGHT, PF_FloatRGBA, false);
    CaptureComponent->TextureTarget->ClearColor = FLinearColor::Red;
    CaptureComponent->TextureTarget->TargetGamma = 2.2f;
    CaptureComponent->RegisterComponentWithWorld(GetWorld());

    //UE4 cannot serialize an array of subobject pointers, so add these objects to the root
    CaptureComponent->AddToRoot();
}

void USkyBoxSceneCapturer::DisableUnsupportedPostProcesses(USceneCaptureComponent2D* CaptureComponent)
{
    CaptureComponent->PostProcessSettings.bOverride_GrainIntensity = true;
    CaptureComponent->PostProcessSettings.GrainIntensity = 0.0f;
    CaptureComponent->PostProcessSettings.bOverride_MotionBlurAmount = true;
    CaptureComponent->PostProcessSettings.MotionBlurAmount = 0.0f;
    CaptureComponent->PostProcessSettings.bOverride_ScreenSpaceReflectionIntensity = true;
    CaptureComponent->PostProcessSettings.ScreenSpaceReflectionIntensity = 0.0f;
    CaptureComponent->PostProcessSettings.bOverride_VignetteIntensity = true;
    CaptureComponent->PostProcessSettings.VignetteIntensity = 0.0f;
    CaptureComponent->PostProcessSettings.bOverride_LensFlareIntensity = true;
    CaptureComponent->PostProcessSettings.LensFlareIntensity = 0.0f;
}

void USkyBoxSceneCapturer::SetCaptureComponentRequirements(USceneCaptureComponent2D* CaptureComponent)
{
    if (m_RenderPasses[m_CurrentRenderPassIndex] == ERenderPass::FinalColor)
    {
        CaptureComponent->bCaptureEveryFrame = false; // true will create bandings, false wont pick up blendables
        CaptureComponent->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
        ////Enable bCaptureEveryFrame ONLY when capture is in a PPVolume with blendables, to avoid bandings
        //FVector CameraPosition = m_CapturePlayerController->PlayerCameraManager->GetCameraLocation();
        //for (FPostProcessVolumeData &ppv_data : m_PPVolumeArray)
        //{
        //    FBoxSphereBounds bounds = ppv_data.Object->GetBounds();
        //    FBox BB = bounds.GetBox();
        //    if (BB.IsInsideOrOn(CameraPosition))
        //    {
        //        //CaptureComponent->PostProcessSettings.WeightedBlendables = ppvolume->Settings.WeightedBlendables;
        //        if (ppv_data.Object->bEnabled && ppv_data.Object->Settings.WeightedBlendables.Array.Num() > 0)
        //        {
        //            CaptureComponent->bCaptureEveryFrame = true;
        //        }
        //    }
        //}
    }
    else if (m_RenderPasses[m_CurrentRenderPassIndex] == ERenderPass::SceneDepth)
    {
        CaptureComponent->bCaptureEveryFrame = false;
        CaptureComponent->CaptureSource = ESceneCaptureSource::SCS_DeviceDepth;
    }
    DisableUnsupportedPostProcesses(CaptureComponent);
}