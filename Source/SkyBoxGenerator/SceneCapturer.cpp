#include "SceneCapturer.h"
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


#define CAPTURE_WIDTH 1024
#define CAPTURE_HIGHT 1024
#define CAPTURE_FOV 120.0f
#define CONCURRENT_CAPTURES 6
FOnSkyBoxCaptureDone USceneCapturer::m_OnSkyBoxCaptureDoneDelegate;


USceneCapturer::USceneCapturer(FVTableHelper& Helper)
    : Super(Helper)
    , ImageWrapperModule(FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper")))
    , OutputBitDepth(8)
    , bIsTicking(false)
    , CapturePlayerController(NULL)
    , CaptureGameMode(NULL)
{
    UE_LOG(LogTemp, Warning, TEXT("！！！！！USceneCapturer::USceneCapturer(FVTableHelper& Helper)"));
}

USceneCapturer::USceneCapturer()
    : ImageWrapperModule(FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper")))
    , OutputBitDepth(8)
    , bIsTicking(false)
    , CapturePlayerController(NULL)
    , CaptureGameMode(NULL)
{
    UE_LOG(LogTemp, Warning, TEXT("！！！！！USceneCapturer::USceneCapturer()"));
    FSystemResolution::RequestResolutionChange(CAPTURE_WIDTH, CAPTURE_HIGHT, EWindowMode::Windowed);

    CacheAllPostProcessVolumes();

	CaptureSceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("CaptureSceneComponent"));
	CaptureSceneComponent->AddToRoot();

	for( int CaptureIndex = 0; CaptureIndex < CONCURRENT_CAPTURES; CaptureIndex++ )
	{
		FString LeftCounter = FString::Printf(TEXT("LeftEyeCaptureComponent_%04d"), CaptureIndex);
		USceneCaptureComponent2D* LeftEyeCaptureComponent = CreateDefaultSubobject<USceneCaptureComponent2D>(*LeftCounter);
		LeftEyeCaptureComponent->bTickInEditor = false;
		LeftEyeCaptureComponent->SetComponentTickEnabled(false);
		LeftEyeCaptureComponent->AttachToComponent(CaptureSceneComponent, FAttachmentTransformRules::KeepRelativeTransform);
		InitCaptureComponent(LeftEyeCaptureComponent);
		LeftEyeCaptureComponents.Add( LeftEyeCaptureComponent );
	}

    RenderPasses.Add(ERenderPass::FinalColor);
    RenderPasses.Add(ERenderPass::SceneDepth);
    CurrentRenderPassIndex = 0;
    CaptureStep = ECaptureStep::Reset;
}

USceneCapturer::~USceneCapturer()
{
    UE_LOG(LogTemp, Warning, TEXT("！！！！！USceneCapturer::~USceneCapturer()"));
}

void USceneCapturer::Initialize(int CaptureWidth, int CaptureHeight, float CaptureFov)
{
}

void USceneCapturer::Reset()
{
    UE_LOG(LogTemp, Warning, TEXT("！！！！！USceneCapturer::Reset()"));
    bIsTicking = false;
    EnablePostProcessVolumes();
    for (int CaptureIndex = 0; CaptureIndex < CONCURRENT_CAPTURES; CaptureIndex++)
    {
        USceneCaptureComponent2D* LeftEyeCaptureComponent = LeftEyeCaptureComponents[CaptureIndex];
        LeftEyeCaptureComponent->SetVisibility(false);
        LeftEyeCaptureComponent->SetHiddenInGame(true);
        LeftEyeCaptureComponent->RemoveFromRoot();
    }
    CaptureSceneComponent->RemoveFromRoot();
}

bool USceneCapturer::StartCapture(FVector CapturePosition, FString FileNamePrefix)
{
    UE_LOG(LogTemp, Warning, TEXT("！！！！！USceneCapturer::StartCapture()"));
    if (bIsTicking)
    {
        UE_LOG(LogTemp, Warning, TEXT("！！！！！USceneCapturer::StartCapture(), Already capturing a scene; concurrent captures are not allowed")); 
        return false;
    }
    CapturePlayerController = UGameplayStatics::GetPlayerController(GetWorld(), 0);
    CaptureGameMode = UGameplayStatics::GetGameMode(GetWorld());
    if (CaptureGameMode == NULL || CapturePlayerController == NULL)
    {
        UE_LOG(LogTemp, Warning, TEXT("！！！！！USceneCapturer::StartCapture(), Missing PlayerController or GameMode"));
        return false;
    }
    m_FileNamePrefix = FileNamePrefix;
    CurrentRenderPassIndex = 0;
    CaptureStep = ECaptureStep::Unpause;
    bIsTicking = true;
    OverallStartTime = FDateTime::UtcNow();
    StartTime = OverallStartTime;
    return true;
}

UWorld* USceneCapturer::GetTickableGameObjectWorld() const
{
    if (LeftEyeCaptureComponents.Num() > 0 && !CaptureSceneComponent->IsPendingKill())
    {
        return CaptureSceneComponent->GetWorld();
    }
    return nullptr;
}

void USceneCapturer::Tick( float DeltaTime )
{
	if( !bIsTicking )
		return;

    if (CaptureStep == ECaptureStep::Unpause)
    {
        FlushRenderingCommands();
        CaptureGameMode->ClearPause();
        //GPauseRenderingRealtimeClock = false;
        CaptureStep = ECaptureStep::Pause;
        FlushRenderingCommands();
    }
    else if (CaptureStep == ECaptureStep::Pause)
    {
        FlushRenderingCommands();
		if (!CaptureGameMode)
			return;
        CaptureGameMode->SetPause(CapturePlayerController);
        //GPauseRenderingRealtimeClock = true;
        CaptureStep = ECaptureStep::SetStartPosition;
        FlushRenderingCommands();
    }
    else if (CaptureStep == ECaptureStep::SetStartPosition)
    {
        ENQUEUE_RENDER_COMMAND(SceneCapturer_HeartbeatTickTickables)(
			[](FRHICommandList& RHICmdList)
			{
				TickRenderingTickables();
			});
        FlushRenderingCommands();

        UE_LOG(LogTemp, Warning, TEXT("！！！！！USkyBoxSceneCapturer::Tick(), Processing pass %s"), *GetCurrentRenderPassName());

        FVector Location;
        FRotator Rotation;
        CapturePlayerController->GetPlayerViewPoint(Location, Rotation);
		CaptureSceneComponent->SetWorldLocationAndRotation(Location, FRotator(0.0f, 0.0f, 0.0f));

		for (int32 CaptureIndex = 0; CaptureIndex < CONCURRENT_CAPTURES; CaptureIndex++)
			SetCaptureComponentRequirements(CaptureIndex);

		if (RenderPasses[CurrentRenderPassIndex] != ERenderPass::FinalColor)
			DisableAllPostProcessVolumes();
		else
			EnablePostProcessVolumes();

        CaptureStep = ECaptureStep::SetPosition;
        FlushRenderingCommands();
    }
    else if (CaptureStep == ECaptureStep::SetPosition)
    {
        FlushRenderingCommands();
        LeftEyeCaptureComponents[0]->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, 0.0f), FRotator(0.0f, 0.0f, 0.0f));  //前
        LeftEyeCaptureComponents[1]->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, 0.0f), FRotator(0.0f, 90.0f, 0.0f));  //右
        LeftEyeCaptureComponents[2]->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, 0.0f), FRotator(0.0f, 180.0f, 0.0f));  //后
        LeftEyeCaptureComponents[3]->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, 0.0f), FRotator(0.0f, -90.0f, 0.0f));  //左
        LeftEyeCaptureComponents[4]->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, 0.0f), FRotator(90.0f, 0.0f, 0.0f));  //上
        LeftEyeCaptureComponents[5]->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, 0.0f), FRotator(-90.0f, 0.0f, 0.0f));  //下
        for (int32 CaptureIndex = 0; CaptureIndex < CONCURRENT_CAPTURES; CaptureIndex++)
        {
            LeftEyeCaptureComponents[CaptureIndex]->CaptureSceneDeferred(); //Render the scene to the texture the next time the main view is rendered
        }
        CaptureStep = ECaptureStep::Read;
        FlushRenderingCommands();
    }
	else if (CaptureStep == ECaptureStep::Read)
	{
        FlushRenderingCommands();
        for (int32 CaptureIndex = 0; CaptureIndex < CONCURRENT_CAPTURES; CaptureIndex++)
        {
            CaptureScene(CaptureIndex);
        }
        for (int32 CaptureIndex = 0; CaptureIndex < CONCURRENT_CAPTURES; CaptureIndex++)
        {
            //必须这么搞一下，不然下次设置相同的相对位置会无效，无效的含义的绝对位置没有变
            LeftEyeCaptureComponents[CaptureIndex]->SetRelativeLocationAndRotation(FVector(0.01f, 0.01f, 0.01f), FRotator(0.01f, 0.01f, 0.01f));
        }
        CaptureStep = ECaptureStep::Reset;
        FlushRenderingCommands();

        FDateTime EndTime = FDateTime::UtcNow();
        FTimespan Duration = EndTime - StartTime;
        UE_LOG(LogTemp, Warning, TEXT("！！！！！USceneCapturer::Tick(), pass %s completed, cost time %f seconds"), *GetCurrentRenderPassName(), Duration.GetTotalSeconds());
        StartTime = EndTime;
    }
    else
	{
		CurrentRenderPassIndex++;
		if (CurrentRenderPassIndex < RenderPasses.Num())
        {
			CaptureStep = ECaptureStep::SetStartPosition;
		}
		else
        {
            CaptureGameMode->ClearPause();
            bIsTicking = false;
            EnablePostProcessVolumes();

            FTimespan OverallDuration = FDateTime::UtcNow() - OverallStartTime;
            UE_LOG(LogTemp, Warning, TEXT("！！！！！USceneCapturer::Tick(), finished, cost time %f seconds"), OverallDuration.GetTotalSeconds());

            m_OnSkyBoxCaptureDoneDelegate.Broadcast();
		}
	}
}


/**********************************************************************************************************************/
//帮助函数
/**********************************************************************************************************************/
void USceneCapturer::CacheAllPostProcessVolumes()
{
    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), APostProcessVolume::StaticClass(), AllActors);
    for (AActor* pp : AllActors)
    {
        APostProcessVolume * PPVolumeObject = CastChecked<APostProcessVolume>(pp);
        FPostProcessVolumeData PPVolumeData;
        PPVolumeData.Object = PPVolumeObject;
        PPVolumeData.WasEnabled = PPVolumeObject->bEnabled;
        PPVolumeArray.Add(PPVolumeData);
    }
}

void USceneCapturer::EnablePostProcessVolumes()
{
    for (FPostProcessVolumeData &PPVolume : PPVolumeArray)
    {
        PPVolume.Object->bEnabled = PPVolume.WasEnabled;
    }
}

void USceneCapturer::DisableAllPostProcessVolumes()
{
    for (FPostProcessVolumeData &PPVolume : PPVolumeArray)
    {
        PPVolume.Object->bEnabled = false;
    }
}

void USceneCapturer::InitCaptureComponent(USceneCaptureComponent2D* CaptureComponent)
{
    CaptureComponent->SetVisibility(true);
    CaptureComponent->SetHiddenInGame(false);
    CaptureComponent->CaptureStereoPass = EStereoscopicPass::eSSP_FULL;
    CaptureComponent->FOVAngle = CAPTURE_FOV;
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

    CaptureComponent->AddToRoot();
}

void USceneCapturer::DisableUnsupportedPostProcesses(USceneCaptureComponent2D* CaptureComponent)
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

void USceneCapturer::SetCaptureComponentRequirements(int32 CaptureIndex)
{
    // FINAL COLOR
    if (RenderPasses[CurrentRenderPassIndex] == ERenderPass::FinalColor)
    {
        LeftEyeCaptureComponents[CaptureIndex]->bCaptureEveryFrame = false; // true will create bandings, false wont pick up blendables
        LeftEyeCaptureComponents[CaptureIndex]->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
        // Enable bCaptureEveryFrame ONLY when capture is in a PPVolume with blendables, to avoid bandings
        FVector CameraPosition = CapturePlayerController->PlayerCameraManager->GetCameraLocation();
        for (FPostProcessVolumeData &PPVolume : PPVolumeArray)
        {
            FBoxSphereBounds bounds = PPVolume.Object->GetBounds();
            FBox BB = bounds.GetBox();
            if (BB.IsInsideOrOn(CameraPosition))
            {
                //LeftEyeCaptureComponents[CaptureIndex]->PostProcessSettings.WeightedBlendables = ppvolume->Settings.WeightedBlendables;
                if (PPVolume.Object->bEnabled && PPVolume.Object->Settings.WeightedBlendables.Array.Num() > 0)
                {
                    LeftEyeCaptureComponents[CaptureIndex]->bCaptureEveryFrame = true;
                }
            }
        }
    }
    // SCENE DEPTH
    if (RenderPasses[CurrentRenderPassIndex] == ERenderPass::SceneDepth)
    {
        LeftEyeCaptureComponents[CaptureIndex]->bCaptureEveryFrame = false;
        LeftEyeCaptureComponents[CaptureIndex]->CaptureSource = ESceneCaptureSource::SCS_DeviceDepth;
    }
    // All other passes
    if (RenderPasses[CurrentRenderPassIndex] < ERenderPass::FinalColor)
    {
        LeftEyeCaptureComponents[CaptureIndex]->bCaptureEveryFrame = true;                              // "true" for blendable to work
        LeftEyeCaptureComponents[CaptureIndex]->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR; // "SCS_FinalColorLDR" for blendable to work
    }
    DisableUnsupportedPostProcesses(LeftEyeCaptureComponents[CaptureIndex]);
}

void USceneCapturer::CaptureScene(int32 CaptureIndex)
{
    USceneCaptureComponent2D* CaptureComponent = LeftEyeCaptureComponents[CaptureIndex];
    FTextureRenderTargetResource* RenderTarget = CaptureComponent->TextureTarget->GameThread_GetRenderTargetResource();
    uint32 TargetWidth = RenderTarget->GetSizeX();
    uint32 TargetHeight = RenderTarget->GetSizeY();

    TArray<FLinearColor> SurfaceData;
    FReadSurfaceDataFlags readSurfaceDataFlags = FReadSurfaceDataFlags();
    RenderTarget->ReadLinearColorPixels(SurfaceData, readSurfaceDataFlags);

    if (RenderPasses[CurrentRenderPassIndex] == ERenderPass::SceneDepth)
    {
        // unpack 32bit scene depth from 4 channels RGBA
        for (FLinearColor& Color : SurfaceData)
        {
            Color.R = 1.0f - (Color.R + (Color.G / 255.0f) + (Color.B / 65025.0f) + (Color.A / 16581375.0f));	// unpack depth
            Color.R = FMath::Pow(Color.R, 0.4545f);																// linear to srgb
            Color.G = Color.R;
            Color.B = Color.R;
            Color.A = 1.0f;
        }
    }

    if (OutputBitDepth == 8 && RenderPasses[CurrentRenderPassIndex] != ERenderPass::SceneDepth)
    {
        FString FileName = FString::Printf(TEXT("%s-%s-%d.png"), *m_FileNamePrefix, *GetCurrentRenderPassName(), CaptureIndex);
        TArray<FColor> CombinedAtlas8bit;
        for (FLinearColor& Color : SurfaceData)
        {
            FColor t = Color.Quantize();
            CombinedAtlas8bit.Add(t);
        }
        TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
        ImageWrapper->SetRaw(CombinedAtlas8bit.GetData(), CombinedAtlas8bit.GetAllocatedSize(), TargetWidth, TargetHeight, ERGBFormat::BGRA, 8);
        const TArray64<uint8>& ImageData = ImageWrapper->GetCompressed(100);
        FFileHelper::SaveArrayToFile(ImageData, *FileName);
        ImageWrapper.Reset();
    }
    else
    {
        FString FileName = FString::Printf(TEXT("%s-%s-%d.exr"), *m_FileNamePrefix, *GetCurrentRenderPassName(), CaptureIndex);
        TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::EXR);
        ImageWrapper->SetRaw(SurfaceData.GetData(), SurfaceData.GetAllocatedSize(), TargetWidth, TargetHeight, ERGBFormat::RGBA, 32);
        const TArray64<uint8>& ImageData = ImageWrapper->GetCompressed((int32)EImageCompressionQuality::Default);
        FFileHelper::SaveArrayToFile(ImageData, *FileName);
        ImageWrapper.Reset();
    }
}

FString USceneCapturer::GetCurrentRenderPassName()
{
    FString RenderPassString;
    switch (RenderPasses[CurrentRenderPassIndex])
    {
    case ERenderPass::FinalColor: RenderPassString = "FinalColor"; break;
    case ERenderPass::WorldNormal: RenderPassString = "WorldNormal"; break;
    case ERenderPass::AO: RenderPassString = "AO"; break;
    case ERenderPass::BaseColor: RenderPassString = "BaseColor"; break;
    case ERenderPass::Metallic: RenderPassString = "Metallic"; break;
    case ERenderPass::Roughness: RenderPassString = "Roughness"; break;
    case ERenderPass::SceneDepth: RenderPassString = "SceneDepth"; break;
    default: RenderPassString = ""; break;
    }
    return RenderPassString;
}