#include "SkyBoxGeneratorCamera.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Kismet/GameplayStatics.h"
// ZZW
//#include <Private/PostProcess/SceneRenderTargets.h>
//#include <SlateApplication.h>
#include <ImageUtils.h>
//#include <FileHelper.h>
#include "Camera/CameraActor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "SkyBoxRPC.h"
#include "SkyBoxWorker.h"

#define CAPTURE_FOV 120.0f


ASkyBoxGeneratorCamera::ASkyBoxGeneratorCamera()
{
    UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！ASkyBoxGeneratorCamera()"));

    //为了在编辑器里执行Tick
	PrimaryActorTick.bCanEverTick = true;

    //创建相机（如果在BluePrint里添加这个组件，无法用代码获取；只能用代码添加，设置相关属性）
    m_CaptureCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("SkyBoxCamera"));
    m_CaptureCameraComponent->SetupAttachment(GetCapsuleComponent());
    m_CaptureCameraComponent->SetRelativeLocation(FVector(0.0f, 0.0f, 0.0f));
    m_CaptureCameraComponent->bUsePawnControlRotation = false;  //如果这个是true，就不能旋转Actor；若不能YAW旋转，蓝图打开actor，取消“用控制器旋转Yaw”的打勾
    m_CaptureCameraComponent->SetFieldOfView(CAPTURE_FOV);  //在编辑器里指定
    m_CaptureCameraComponent->SetAspectRatio(1.0f);
    m_CaptureCameraComponent->SetConstraintAspectRatio(true);

    m_CaptureCameraActor = NULL;
    m_UseActorToCapture = true;  //true使用m_CaptureCameraActor，false使用m_CaptureCameraComponent
    m_UseHighResShot = false; //true使用命令HighResShot高清截图，false使用OnBackBufferReadyToPresent

    m_SixDirection.Push(FRotator(0.0f, 0.0f, 0.0f));  //前
    m_SixDirection.Push(FRotator(0.0f, 90.0f, 0.0f));  //右
    m_SixDirection.Push(FRotator(0.0f, 180.0f, 0.0f));  //后
    m_SixDirection.Push(FRotator(0.0f, -90.0f, 0.0f));  //左
    m_SixDirection.Push(FRotator(90.0f, 0.0f, 0.0f));  //上
    m_SixDirection.Push(FRotator(-90.0f, 0.0f, 0.0f));  //下

    m_current_job = NULL;
    m_CurrentDirection = -1;
    m_CurrentState = CaptureState::Invalid;
}

ASkyBoxGeneratorCamera::~ASkyBoxGeneratorCamera()
{
    UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！~ASkyBoxGeneratorCamera()"));
    //if (m_CaptureCamera != NULL)
    //    m_CaptureCameraActor->Destroy();  //这会导致崩溃，这Actor生命周期应该是由UE4自己管理了
    m_CaptureCameraActor = NULL;
    SkyBoxWorker::Shutdown();
    //FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().RemoveAll(this);
    //FScreenshotRequest::OnScreenshotRequestProcessed().RemoveAll(this);
}

void ASkyBoxGeneratorCamera::BeginPlay()
{
    Super::BeginPlay();
    UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！ASkyBoxGeneratorCamera::BeginPlay()"));

    SetActorLocation(FVector(100.0f, 0.0f, 110.0f));  //设个初始位置

    if (m_UseActorToCapture)
        CreateCaptureCameraActor();
    SetViewTarget();

    if (m_UseHighResShot)
        FScreenshotRequest::OnScreenshotRequestProcessed().AddUObject(this, &ASkyBoxGeneratorCamera::OnScreenshotProcessed_RenderThread);
    else
        FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().AddUObject(this, &ASkyBoxGeneratorCamera::OnBackBufferReady_RenderThread);
    SkyBoxWorker::StartUp();
}

bool ASkyBoxGeneratorCamera::ShouldTickIfViewportsOnly() const
{
    // 为了在编辑器里执行Tick
    return true;
}

void ASkyBoxGeneratorCamera::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);
}

void ASkyBoxGeneratorCamera::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if (m_UseActorToCapture && m_CaptureCameraActor == NULL)
        return;

    FScopeLock lock(&m_lock);

    if (m_current_job == NULL)
    {
        m_current_job = SkyBoxServiceImpl::Instance()->GetJob();
        if (m_current_job == NULL)
            return;
        UGameplayStatics::SetGlobalTimeDilation(GetWorld(), 0.0f);
        UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！Get New Job, job_id = %d, scene_id = %d, position = (%.1f, %.1f, %.1f)"),
            m_current_job->JobID(), m_current_job->m_position.scene_id, m_current_job->m_position.x, m_current_job->m_position.y, m_current_job->m_position.z);
        SetCaptureCameraLocation(FVector(m_current_job->m_position.x, m_current_job->m_position.y, m_current_job->m_position.z));
        m_CurrentDirection = 0;
        m_CurrentState = CaptureState::Waiting1;
        SetCaptureCameraRotation(m_SixDirection[m_CurrentDirection]);
        UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！Change Direction, job_id = %d, m_CurrentDirection = %d"), m_current_job->JobID(), m_CurrentDirection);
        return;
    }
    if (m_CurrentState == CaptureState::Waiting1)
    {
        if (m_UseHighResShot)
        {
            //HighResShot实现有bug，字符串中必须用"/"，不能用"\\"
            m_BackBufferFilePath = FString::Printf(TEXT("I:/UE4Workspace/png/SkyBox(%dX%d)_Scene%d_(%.1f，%.1f，%.1f)_%d.png"),
                m_BackBufferSizeX, m_BackBufferSizeY, m_current_job->m_position.scene_id, m_current_job->m_position.x, m_current_job->m_position.y, m_current_job->m_position.z, m_CurrentDirection);
            FString cmd = FString::Printf(TEXT("HighResShot 2048x2048 filename=\"%s\""), *m_BackBufferFilePath);
            UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！%s"), *cmd);
            GEngine->Exec(GetWorld(), *cmd);
        }
        else
        {
            m_CurrentState = CaptureState::Prepared; //给OnBackBufferReadyToPresent的提示
        }
        return;
    }
    if (m_CurrentState == CaptureState::Captured)
    {
        bool ok = SavePNGToFile();
        if (!ok)
        {
            UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！Job Failed, job_id = %d, scene_id = %d, position = (%.1f, %.1f, %.1f)"),
                m_current_job->JobID(), m_current_job->m_position.scene_id, m_current_job->m_position.x, m_current_job->m_position.y, m_current_job->m_position.z);
            m_current_job->SetStatus(skybox::JobStatus::Failed);
            SkyBoxServiceImpl::Instance()->OnJobCompleted(m_current_job);
            m_current_job = NULL;
            m_CurrentDirection = -1;
            m_CurrentState = CaptureState::Invalid;
        }
        else
        {
            m_CurrentState = CaptureState::Saved;
        }
        return;
    }
    if (m_CurrentState == CaptureState::Saved)
    {
        ++m_CurrentDirection;
        if (m_CurrentDirection < m_SixDirection.Num())
        {
            m_CurrentState = CaptureState::Waiting1;
            SetCaptureCameraRotation(m_SixDirection[m_CurrentDirection]);
            UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！Change Direction, job_id = %d, m_CurrentDirection = %d"), m_current_job->JobID(), m_CurrentDirection);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！Job Succeeded, job_id = %d, scene_id = %d, position = (%.1f, %.1f, %.1f)"),
                m_current_job->JobID(), m_current_job->m_position.scene_id, m_current_job->m_position.x, m_current_job->m_position.y, m_current_job->m_position.z);
            m_current_job->SetStatus(skybox::JobStatus::Succeeded);
            SkyBoxServiceImpl::Instance()->OnJobCompleted(m_current_job);
            m_current_job = NULL;
            m_CurrentDirection = -1;
            m_CurrentState = CaptureState::Invalid;
        }
        return;
    }
}

void ASkyBoxGeneratorCamera::CreateCaptureCameraActor()
{
    m_CaptureCameraActor = GetWorld()->SpawnActor<ACameraActor>(GetActorLocation(), GetActorRotation());
    UCameraComponent* camera_component = m_CaptureCameraActor->GetCameraComponent();
    camera_component->bUsePawnControlRotation = false;
    camera_component->SetFieldOfView(CAPTURE_FOV);  //在编辑器里指定
    camera_component->SetAspectRatio(1.0f);
    camera_component->SetConstraintAspectRatio(true);
}

void ASkyBoxGeneratorCamera::SetViewTarget()
{
    AActor* target = this;
    if (m_UseActorToCapture)
    {
        target = m_CaptureCameraActor;
        APlayerController* player_controller = UGameplayStatics::GetPlayerController(this, 0);
        player_controller->SetViewTarget(target);
    }


    ////PlayerController的各种方法
    //APlayerController* player_controller1 = UGameplayStatics::GetPlayerController(this, 0);
    //APlayerController* player_controller2 = UGameplayStatics::GetPlayerController(GetWorld(), 0);
    //APlayerController* player_controller3 = GetWorld()->GetFirstPlayerController();
    //APlayerController* player_controller4 = GEngine->GetFirstLocalPlayerController(GetWorld());
    //APlayerController* player_controller5 = GetWorld()->GetFirstLocalPlayerFromController()->GetPlayerController(GetWorld());
    //player_controller1->SetViewTarget(target);

    //for (FConstPlayerControllerIterator itr = GetWorld()->GetPlayerControllerIterator(); itr; ++itr)
    //{
    //    APlayerController* pc = itr->Get();
    //    pc->SetViewTarget(target);
    //}
}

void ASkyBoxGeneratorCamera::SetCaptureCameraLocation(FVector location)
{
    if (m_UseActorToCapture)
    {
        m_CaptureCameraActor->SetActorLocation(location);
    }
    else
    {
        SetActorLocation(location);
    }
}

void ASkyBoxGeneratorCamera::SetCaptureCameraRotation(FRotator rotation)
{
    if (m_UseActorToCapture)
    {
        m_CaptureCameraActor->SetActorRotation(rotation);
    }
    else
    {
        SetActorRotation(rotation);
        //m_CaptureCameraComponent->SetRelativeRotation(rotation);
    }
}

void ASkyBoxGeneratorCamera::OnScreenshotProcessed_RenderThread()
{
    FScopeLock lock(&m_lock);
    m_CurrentState = CaptureState::Saved;
}

void ASkyBoxGeneratorCamera::OnBackBufferReady_RenderThread(SWindow& SlateWindow, const FTexture2DRHIRef& BackBuffer)
{
    CaptureBackBufferToPNG(BackBuffer);
}

void ASkyBoxGeneratorCamera::CaptureBackBufferToPNG(const FTexture2DRHIRef& BackBuffer)
{
    FScopeLock lock(&m_lock);

    if (m_CurrentState != CaptureState::Prepared)
        return;
    if (m_BackBufferData.Num() != 0)
        return;
    UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！CAPTURE, job_id = %d, m_CurrentDirection = %d"), m_current_job->JobID(), m_CurrentDirection);
    FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
    FIntRect Rect(0, 0, BackBuffer->GetSizeX(), BackBuffer->GetSizeY());
    RHICmdList.ReadSurfaceData(BackBuffer, Rect, m_BackBufferData, FReadSurfaceDataFlags(RCM_UNorm));
    for (FColor& Color : m_BackBufferData)
    {
        Color.A = 255;
    }
    m_BackBufferSizeX = BackBuffer->GetSizeX();
    m_BackBufferSizeY = BackBuffer->GetSizeY();

    FDateTime Time = FDateTime::Now();
    /*m_BackBufferFilePath = FString::Printf(TEXT("I:\\UE4Workspace\\png\\BACK(%dX%d)_%d__%04d-%02d-%02d_%02d-%02d-%02d_%d.png"),
        m_BackBufferSizeX, m_BackBufferSizeY, m_CurrentDirection, Time.GetYear(), Time.GetMonth(), Time.GetDay(), Time.GetHour(), Time.GetMinute(), Time.GetSecond(), Time.GetMillisecond());*/
    m_BackBufferFilePath = FString::Printf(TEXT("I:\\UE4Workspace\\png\\SkyBox(%dX%d)_Scene%d_(%.1f，%.1f，%.1f)_%d.png"),
        m_BackBufferSizeX, m_BackBufferSizeY, m_current_job->m_position.scene_id, m_current_job->m_position.x, m_current_job->m_position.y, m_current_job->m_position.z, m_CurrentDirection);
    m_CurrentState = CaptureState::Captured;
}

bool ASkyBoxGeneratorCamera::SavePNGToFile()
{
    if (m_BackBufferData.Num() == 0)
        return false;
    UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！SAVE, job_id = %d, position = (%.1f, %.1f, %.1f), m_CurrentDirection = %d"),
        m_current_job->JobID(), m_current_job->m_position.x, m_current_job->m_position.y, m_current_job->m_position.z, m_CurrentDirection);
    TArray<uint8> CompressedBitmap;
    FImageUtils::CompressImageArray(m_BackBufferSizeX, m_BackBufferSizeY, m_BackBufferData, CompressedBitmap);
    bool Success = FFileHelper::SaveArrayToFile(CompressedBitmap, *m_BackBufferFilePath);
    if (!Success)
    {
        UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！SavePNGToFile() FAIL %s"), *m_BackBufferFilePath);
        return false;
    }
    m_BackBufferData.Reset();
    return true;
}

