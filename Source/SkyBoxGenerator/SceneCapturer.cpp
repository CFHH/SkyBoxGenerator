#include "SceneCapturer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "Misc/Paths.h"
#include "UnrealEngine.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/App.h"
#include "GameFramework/Actor.h"
#include "GameFramework/DefaultPawn.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "TextureResource.h"
#include "Engine/BlendableInterface.h"
#include "ImageUtils.h"
#include "CoreMinimal.h"
#include "UObject/ConstructorHelpers.h"
#include "EngineUtils.h"
#include "CoreMinimal.h"
#include "LatentActions.h"
#include "Engine/LatentActionManager.h"
#include "MessageLog/Public/MessageLogModule.h"
#include "Tickable.h"

#define LOCTEXT_NAMESPACE "LogStereoPanorama"

#define CONCURRENT_CAPTURES 6
#define CAPTURE_FOV 120.0f  //30
#define CAPTURE_WIDTH 720
#define CAPTURE_HIGHT 720
FOnSkyBoxCaptureDone USceneCapturer::m_OnSkyBoxCaptureDoneDelegate;


USceneCapturer::USceneCapturer(FVTableHelper& Helper)
    : Super(Helper)
    , ImageWrapperModule(FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper")))
    , bIsTicking(false)
    , CapturePlayerController(NULL)
    , CaptureGameMode(NULL)
    , hAngIncrement(90.0f)  //2
    , vAngIncrement(90.0f)  //15
    , NumberOfHorizontalSteps((int32)(360.0f / hAngIncrement))
    , NumberOfVerticalSteps((int32)(180.0f / vAngIncrement) + 1) /* Need an extra b/c we only grab half of the top & bottom slices */
    , OutputDir(TEXT("I:/UE4Workspace/png/mycapture"))
	, OutputBitDepth(8)
	, bOutputSceneDepth(true)
	, bOutputFinalColor(true)
{
}

USceneCapturer::USceneCapturer()
    : ImageWrapperModule(FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper")))
    , bIsTicking(false)
    , CapturePlayerController(NULL)
    , CaptureGameMode(NULL)
    , hAngIncrement(90.0f)  //2
    , vAngIncrement(90.0f)  //15
    , NumberOfHorizontalSteps((int32)(360.0f / hAngIncrement))
    , NumberOfVerticalSteps((int32)(180.0f / vAngIncrement) + 1) /* Need an extra b/c we only grab half of the top & bottom slices */
    , OutputDir(TEXT("I:/UE4Workspace/png/mycapture"))
    , OutputBitDepth(8)
    , bOutputSceneDepth(true)
    , bOutputFinalColor(true)
{
	// Add a message log category for this plugin
	FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
	FMessageLogInitializationOptions MessageLogOptions;
	MessageLogOptions.bShowPages = true;
	MessageLogOptions.bAllowClear = true;
	MessageLogOptions.MaxPageCount = 10;
	MessageLogModule.RegisterLogListing(StereoPanoramaLogName, LOCTEXT("StereoPanoramaLogLabel", "Panoramic Capture Log"));

	// Cache all PP volumes and current state
	CacheAllPostProcessVolumes();


    {
        //Slicing Technique 2: Each slice is a determined square FOV at a configured preset resolution.
        //                     Strip Width/Strip Height is determined based on hAngIncrement & vAngIncrement
        //                     Just make sure pixels/CAPTURE_FOV >= pixels/hAngIncr && pixels/vAngIncr

        ensure(CAPTURE_FOV >= FMath::Max(hAngIncrement, vAngIncrement)); //配置hAngIncrement是2度，vAngIncrement是15度

        //TODO: ikrimae: Re-do for floating point accuracy
        const FVector2D slicePlaneDim = FVector2D(
            2.0f * FMath::Tan(FMath::DegreesToRadians(hAngIncrement) / 2.0f),
            2.0f * FMath::Tan(FMath::DegreesToRadians(vAngIncrement) / 2.0f));

        const FVector2D capturePlaneDim = FVector2D(
            2.0f * FMath::Tan(FMath::DegreesToRadians(CAPTURE_FOV) / 2.0f),
            2.0f * FMath::Tan(FMath::DegreesToRadians(CAPTURE_FOV) / 2.0f));
    }

    //NOTE: ikrimae: Ensure that the main gameview is > CaptureWidth x CaptureHeight. Bug in UE4 that won't re-alloc scene render targets to the correct size
    //               when the scenecapture component > current window render target. https://answers.unrealengine.com/questions/80531/scene-capture-2d-max-resolution.html
    //TODO: ikrimae: Ensure that r.SceneRenderTargetResizeMethod=2
    FSystemResolution::RequestResolutionChange(CAPTURE_WIDTH, CAPTURE_HIGHT, EWindowMode::Windowed);

	// Creating CaptureSceneComponent to use it as parent scene component.
	// This scene component will hold same world location from camera.
	// Camera rotation will be used following UseCameraRotation settings.
	// Then, angular step turn will be applied to capture components locally to simplify calculation step that finding proper rotation.
	CaptureSceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("CaptureSceneComponent"));
	CaptureSceneComponent->AddToRoot();

    //ConcurrentCaptures的配置是8；如果是skybox，配置6，一帧内获得
	for( int CaptureIndex = 0; CaptureIndex < CONCURRENT_CAPTURES; CaptureIndex++ )
	{
		FString LeftCounter = FString::Printf(TEXT("LeftEyeCaptureComponent_%04d"), CaptureIndex);
		USceneCaptureComponent2D* LeftEyeCaptureComponent = CreateDefaultSubobject<USceneCaptureComponent2D>(*LeftCounter);
		LeftEyeCaptureComponent->bTickInEditor = false;
		LeftEyeCaptureComponent->SetComponentTickEnabled(false);
		LeftEyeCaptureComponent->AttachToComponent(CaptureSceneComponent, FAttachmentTransformRules::KeepRelativeTransform);
		InitCaptureComponent( LeftEyeCaptureComponent, CAPTURE_FOV, CAPTURE_FOV, eSSP_FULL);
		LeftEyeCaptureComponents.Add( LeftEyeCaptureComponent );
	}

	CurrentStep = 0;
	TotalSteps = 0;  //后面赋值NumberOfHorizontalSteps * NumberOfVerticalSteps;
	FrameDescriptors = TEXT( "FrameNumber, GameClock, TimeTaken(s)" LINE_TERMINATOR );
	CaptureStep = ECaptureStep::Reset;

	// populate RenderPasses based on user options
	CurrentRenderPassIndex = 0;
	if (bOutputFinalColor)
	{
		RenderPasses.Add(ERenderPass::FinalColor);
	}

	if (bOutputSceneDepth)
	{
		RenderPasses.Add(ERenderPass::SceneDepth);
	}
}

USceneCapturer::~USceneCapturer()
{
}

void USceneCapturer::Initialize(int CaptureWidth, int CaptureHeight, float CaptureFov)
{
}

void USceneCapturer::Reset()
{
    bIsTicking = false;
    // apply old states on PP volumes
    EnablePostProcessVolumes();

    for (int CaptureIndex = 0; CaptureIndex < CONCURRENT_CAPTURES; CaptureIndex++)
    {
        USceneCaptureComponent2D* LeftEyeCaptureComponent = LeftEyeCaptureComponents[CaptureIndex];

        LeftEyeCaptureComponent->SetVisibility(false);
        LeftEyeCaptureComponent->SetHiddenInGame(true);
        // UE4 cannot serialize an array of subobject pointers, so work around the GC problems
        LeftEyeCaptureComponent->RemoveFromRoot();
    }

    CaptureSceneComponent->RemoveFromRoot();
}

bool USceneCapturer::StartCapture(FVector CapturePosition, FString FileNamePrefix)
{
    FStereoCaptureDoneDelegate EmptyDelegate;
    SetInitialState(0, 0, EmptyDelegate);
    return true;
}

void USceneCapturer::SetInitialState(int32 InStartFrame, int32 InEndFrame, FStereoCaptureDoneDelegate& InStereoCaptureDoneDelegate)
{
    if (bIsTicking)
    {
        FMessageLog(StereoPanoramaLogName).Message(EMessageSeverity::Warning, LOCTEXT("InitialStateWarning_AlreadyCapturing", "Already capturing a scene; concurrent captures are not allowed"));
        return;
    }

    CapturePlayerController = UGameplayStatics::GetPlayerController(GetWorld(), 0); //GWorld
    CaptureGameMode = UGameplayStatics::GetGameMode(GetWorld()); //GWorld

    if (CaptureGameMode == NULL || CapturePlayerController == NULL)
    {
        FMessageLog(StereoPanoramaLogName).Message(EMessageSeverity::Warning, LOCTEXT("InitialStateWarning_MissingGameModeOrPC", "Missing GameMode or PlayerController"));
        return;
    }

    //TotalSteps = NumberOfHorizontalSteps * NumberOfVerticalSteps;
    TotalSteps = CONCURRENT_CAPTURES;

    // Setup starting criteria
    CurrentStep = 0;
    CaptureStep = ECaptureStep::Unpause;
    CurrentRenderPassIndex = 0;

    FDateTime Time = FDateTime::Now();
    Timestamp = FString::Printf(TEXT("%s-%d"), *Time.ToString(), Time.GetMillisecond());
    UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！Timestamp = %s"), *Timestamp);

    //SetStartPosition();

    StartTime = FDateTime::UtcNow();
    OverallStartTime = StartTime;
    bIsTicking = true;

    StereoCaptureDoneDelegate = InStereoCaptureDoneDelegate;

    // open log on error
    if (FMessageLog(StereoPanoramaLogName).NumMessages(EMessageSeverity::Error) > 0)
    {
        FMessageLog(StereoPanoramaLogName).Open();
    }
}

UWorld* USceneCapturer::GetTickableGameObjectWorld() const
{
    // Check SceneCapturer have CaptureComponents and parent scene component is not marked as pending kill.
    if (LeftEyeCaptureComponents.Num() > 0 && !CaptureSceneComponent->IsPendingKill())
    {
        return CaptureSceneComponent->GetWorld();
    }
    return nullptr;
}


//TODO: ikrimae: Come back and actually work out the timings. Trickery b/c SceneCaptureCubes Tick at the end of the frame so we're effectively queuing up the next
//               step (pause, unpause, setposition) for the next frame. FlushRenderingCommands() added haphazardly to test but didn't want to remove them so close to delivery.
//               Think through when we actually need to flush and document.
void USceneCapturer::Tick( float DeltaTime )
{
	if( !bIsTicking )
	{
		return;
	}

    if( CurrentStep < TotalSteps )
	{
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

			// To prevent following process when tick at the time of PIE ends and CaptureGameMode is no longer valid.
			if (!CaptureGameMode)
			{
				return;
			}

            CaptureGameMode->SetPause(CapturePlayerController);
            //GPauseRenderingRealtimeClock = true;
            CaptureStep = ECaptureStep::SetStartPosition;
            FlushRenderingCommands();
        }
        else if (CaptureStep == ECaptureStep::SetStartPosition)
        {
            //SetStartPosition();
            ENQUEUE_RENDER_COMMAND(SceneCapturer_HeartbeatTickTickables)(
				[](FRHICommandList& RHICmdList)
				{
					TickRenderingTickables();
				});

            FlushRenderingCommands();

            FRotator Rotation;
            CapturePlayerController->GetPlayerViewPoint(StartLocation, Rotation);
            // Gathering selected axis information from UseCameraRotation and saving it to FRotator Rotation.
			Rotation = FRotator(0.0f , 0.0f, 0.0f);
            StartRotation = Rotation;

			// Set Designated Rotation and Location for CaptureSceneComponent, using it as parent scene component for capturecomponents.
			CaptureSceneComponent->SetWorldLocationAndRotation(StartLocation, Rotation);
            UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！SetStartPosition (%.1f，%.1f，%.1f)"), StartLocation.X, StartLocation.Y, StartLocation.Z);

			// set capture components settings before capturing and reading
			for (int32 CaptureIndex = 0; CaptureIndex < CONCURRENT_CAPTURES; CaptureIndex++)
			{
				SetCaptureComponentRequirements(CaptureIndex);
			}

			FString CurrentPassName = GetCurrentRenderPassName();
			FString Msg = FString("Processing pass: " + CurrentPassName);
			FMessageLog(StereoPanoramaLogName).Message(EMessageSeverity::Info, FText::FromString(Msg));

			if (RenderPasses[CurrentRenderPassIndex] != ERenderPass::FinalColor)
			{
				DisableAllPostProcessVolumes();
			}
			else
			{
				EnablePostProcessVolumes();
			}

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
                CaptureComponent(CaptureIndex, 0, TEXT("Left"), LeftEyeCaptureComponents[CaptureIndex]);
                CurrentStep++;
            }
            //必须这么搞一下，不然下次设置就无效了
            LeftEyeCaptureComponents[0]->SetRelativeLocationAndRotation(FVector(0.01f, 0.0f, 0.0f), FRotator(0.01f, 0.0f, 0.0f));  //前
            LeftEyeCaptureComponents[1]->SetRelativeLocationAndRotation(FVector(0.01f, 0.0f, 0.0f), FRotator(0.01f, 90.0f, 0.0f));  //右
            LeftEyeCaptureComponents[2]->SetRelativeLocationAndRotation(FVector(0.01f, 0.0f, 0.0f), FRotator(0.01f, 180.0f, 0.0f));  //后
            LeftEyeCaptureComponents[3]->SetRelativeLocationAndRotation(FVector(0.01f, 0.0f, 0.0f), FRotator(0.01f, -90.0f, 0.0f));  //左
            LeftEyeCaptureComponents[4]->SetRelativeLocationAndRotation(FVector(0.01f, 0.0f, 0.0f), FRotator(90.01f, 0.0f, 0.0f));  //上
            LeftEyeCaptureComponents[5]->SetRelativeLocationAndRotation(FVector(0.01f, 0.0f, 0.0f), FRotator(-90.01f, 0.0f, 0.0f));  //下

            CaptureStep = ECaptureStep::SetPosition;
            FlushRenderingCommands();
        }
        else
        {
            //ECaptureStep::Reset:
		}
	}
	else  // 也就是CurrentStep >= TotalSteps
	{
        // ----------------------------------------------------------------------------------------
		// Check if we need to do another render pass
		CurrentRenderPassIndex++;
		if (CurrentRenderPassIndex < RenderPasses.Num())
		{
			// NEXT RENDER PASS - same frame
			CurrentStep = 0;
			CaptureStep = ECaptureStep::SetStartPosition;
		}
		else
		{
			// ----------------------------------------------------------------------------------------
			// NEXT FRAME
			// Dump out how long the process took
			FDateTime EndTime = FDateTime::UtcNow();
			FTimespan Duration = EndTime - StartTime;

			FFormatNamedArguments Args;
			Args.Add(TEXT("Duration"), Duration.GetTotalSeconds());
			Args.Add(TEXT("CurrentFrameCount"), 0);
			FMessageLog(StereoPanoramaLogName).Message(EMessageSeverity::Info, FText::Format(LOCTEXT("FrameDuration", "Duration: {Duration} seconds for frame {CurrentFrameCount}"), Args));

			StartTime = EndTime;

			//NOTE: ikrimae: Since we can't synchronously finish a stereocapture, we have to notify the caller with a function pointer
			//Not sure this is the cleanest way but good enough for now
			//StereoCaptureDoneDelegate.ExecuteIfBound(SphericalLeftEyeAtlas, SphericalRightEyeAtlas);

			// Construct log of saved atlases in csv format
			FrameDescriptors += FString::Printf(TEXT("%d, %g, %g" LINE_TERMINATOR), 0, FApp::GetCurrentTime() - FApp::GetLastTime(), Duration.GetTotalSeconds());

			{
				// ----------------------------------------------------------------------------------------
				// EXIT
				CaptureGameMode->ClearPause();
				//GPauseRenderingRealtimeClock = false;

				FTimespan OverallDuration = FDateTime::UtcNow() - OverallStartTime;

				FrameDescriptors += FString::Printf(TEXT("Duration: %g minutes for frame range [%d,%d] "), OverallDuration.GetTotalMinutes(), 0, 0);

				FFormatNamedArguments ExitArgs;
				ExitArgs.Add(TEXT("Duration"), OverallDuration.GetTotalMinutes());
				ExitArgs.Add(TEXT("StartFrame"), 0);
				ExitArgs.Add(TEXT("EndFrame"), 0);
				FMessageLog(StereoPanoramaLogName).Message(EMessageSeverity::Info, FText::Format(LOCTEXT("CompleteDuration", "Duration: {Duration} minutes for frame range [{StartFrame},{EndFrame}] "), ExitArgs));

				FString FrameDescriptorName = OutputDir / Timestamp / TEXT("Frames.txt");
				FFileHelper::SaveStringToFile(FrameDescriptors, *FrameDescriptorName, FFileHelper::EEncodingOptions::ForceUTF8);

				bIsTicking = false;
                EnablePostProcessVolumes();
                m_OnSkyBoxCaptureDoneDelegate.Broadcast(); //FStereoPanoramaModule::Get()->Cleanup();
			}
		}
	}  // end 也就是CurrentStep >= TotalSteps
}

/**********************************************************************************************************************/
//帮助函数
/**********************************************************************************************************************/
// Cache all PP volumes in scene and save current "enable" state
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

// Apply old state on PP volumes
void USceneCapturer::EnablePostProcessVolumes()
{
    for (FPostProcessVolumeData &PPVolume : PPVolumeArray)
    {
        PPVolume.Object->bEnabled = PPVolume.WasEnabled;
    }
}

// Disable all PP volumes in scene to make sure g-buffer passes work
void USceneCapturer::DisableAllPostProcessVolumes()
{
    for (FPostProcessVolumeData &PPVolume : PPVolumeArray)
    {
        PPVolume.Object->bEnabled = false;
    }
}

void USceneCapturer::InitCaptureComponent(USceneCaptureComponent2D* CaptureComponent, float HFov, float VFov, EStereoscopicPass InStereoPass)
{
    CaptureComponent->SetVisibility(true);
    CaptureComponent->SetHiddenInGame(false);
    CaptureComponent->CaptureStereoPass = InStereoPass;
    CaptureComponent->FOVAngle = FMath::Max(HFov, VFov);
    CaptureComponent->bCaptureEveryFrame = false;
    CaptureComponent->bCaptureOnMovement = false;
    CaptureComponent->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
    DisableUnsupportedPostProcesses(CaptureComponent);

    const FName TargetName = MakeUniqueObjectName(this, UTextureRenderTarget2D::StaticClass(), TEXT("SceneCaptureTextureTarget"));
    CaptureComponent->TextureTarget = NewObject<UTextureRenderTarget2D>(this, TargetName);
    CaptureComponent->TextureTarget->InitCustomFormat(CAPTURE_WIDTH, CAPTURE_HIGHT, PF_FloatRGBA, false);
    CaptureComponent->TextureTarget->ClearColor = FLinearColor::Red;
    CaptureComponent->TextureTarget->TargetGamma = 2.2f;
    CaptureComponent->RegisterComponentWithWorld(GetWorld()); //GWorld

    // UE4 cannot serialize an array of subobject pointers, so add these objects to the root
    CaptureComponent->AddToRoot();
}

// Disable screen space post processes we cannot use while capturing
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

// setup capture component based on current render pass
void USceneCapturer::SetCaptureComponentRequirements(int32 CaptureIndex)
{
    // FINAL COLOR
    if (RenderPasses[CurrentRenderPassIndex] == ERenderPass::FinalColor)
    {
        LeftEyeCaptureComponents[CaptureIndex]->bCaptureEveryFrame = false;			// true will create bandings, false wont pick up blendables
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
        LeftEyeCaptureComponents[CaptureIndex]->bCaptureEveryFrame = true;										// "true" for blendable to work
        LeftEyeCaptureComponents[CaptureIndex]->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;			// "SCS_FinalColorLDR" for blendable to work
    }

    DisableUnsupportedPostProcesses(LeftEyeCaptureComponents[CaptureIndex]);
}

void USceneCapturer::CaptureComponent(int32 CurrentHorizontalStep, int32 CurrentVerticalStep, FString Folder, USceneCaptureComponent2D* CaptureComponent)
{
    TArray<FLinearColor> SurfaceData;

    uint32 targetWidth = 0;
    uint32 targetHeight = 0;

    {
        SCOPE_CYCLE_COUNTER(STAT_SPReadStrip);

        FTextureRenderTargetResource* RenderTarget = CaptureComponent->TextureTarget->GameThread_GetRenderTargetResource();
        targetWidth = RenderTarget->GetSizeX();
        targetHeight = RenderTarget->GetSizeY();

        FReadSurfaceDataFlags readSurfaceDataFlags = FReadSurfaceDataFlags();
        RenderTarget->ReadLinearColorPixels(SurfaceData, readSurfaceDataFlags);
    }

    // SceneDepth pass only
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
        TArray<FColor> CombinedAtlas8bit;
        for (FLinearColor& Color : SurfaceData)
        {
            FColor t = Color.Quantize();
            CombinedAtlas8bit.Add(t);
        }

        FString TickString = FString::Printf(TEXT("_%05d_%04d_%04d"), 0, CurrentHorizontalStep, CurrentVerticalStep);
        FString CaptureName = OutputDir / Timestamp / Folder / TickString + TEXT(".png");

        // write
        TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
        ImageWrapper->SetRaw(CombinedAtlas8bit.GetData(), CombinedAtlas8bit.GetAllocatedSize(), targetWidth, targetHeight, ERGBFormat::BGRA, 8);
        const TArray64<uint8>& ImageData = ImageWrapper->GetCompressed(100);
        FFileHelper::SaveArrayToFile(ImageData, *CaptureName);
        ImageWrapper.Reset();
    }
    else
    {
        FString TickString = FString::Printf(TEXT("_%05d_%04d_%04d"), 0, CurrentHorizontalStep, CurrentVerticalStep);
        FString CaptureName = OutputDir / Timestamp / Folder / TickString + TEXT(".exr");

        // write
        TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::EXR);
        ImageWrapper->SetRaw(SurfaceData.GetData(), SurfaceData.GetAllocatedSize(), targetWidth, targetHeight, ERGBFormat::RGBA, 32);
        const TArray64<uint8>& ImageData = ImageWrapper->GetCompressed((int32)EImageCompressionQuality::Default);
        FFileHelper::SaveArrayToFile(ImageData, *CaptureName);
        ImageWrapper.Reset();
    }
}

// Output current render pass name as string
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

#undef LOCTEXT_NAMESPACE