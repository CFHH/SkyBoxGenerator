#include "SkyBoxGeneratorCamera.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Kismet/GameplayStatics.h"
// ZZW
//#include <Private/PostProcess/SceneRenderTargets.h>
//#include <SlateApplication.h>
#include <ImageUtils.h>
//#include <FileHelper.h>
#include "SkyBoxRPC.h"
#include "SkyBoxWorker.h"


ASkyBoxGeneratorCamera::ASkyBoxGeneratorCamera()
{
    UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！ASkyBoxGeneratorCamera()"));

    //为了在编辑器里执行Tick
	PrimaryActorTick.bCanEverTick = true;

    //创建相机（如果在BluePrint里添加这个组件，无法用代码获取；只能用代码添加，设置相关属性）
    SkyBoxCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("SkyBoxCamera"));
    SkyBoxCamera->SetupAttachment(GetCapsuleComponent());
    SkyBoxCamera->SetRelativeLocation(FVector(0.0f, 0.0f, 0.0f));
    SkyBoxCamera->bUsePawnControlRotation = false;  //如果这个是true，就不能旋转Actor
    SkyBoxCamera->SetFieldOfView(90.0f);
    SkyBoxCamera->SetAspectRatio(1.0f);
    SkyBoxCamera->SetConstraintAspectRatio(true);
    //APlayerController* OurPlayerController = UGameplayStatics::GetPlayerController(this, 0);
    //OurPlayerController->SetViewTarget(this);

    //SetActorRotation(FRotator(0.0f, 90.0f, 0.0f));  //测试
    //SkyBoxCamera->SetRelativeRotation(FRotator(0.0f, 90.0f, 0.0f));  //测试

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
    SkyBoxWorker::Shutdown();
    //FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().RemoveAll(this);
}

void ASkyBoxGeneratorCamera::BeginPlay()
{
    Super::BeginPlay();
    SetActorLocation(FVector(100.0f, 0.0f, 110.0f));

    UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！ASkyBoxGeneratorCamera::BeginPlay()"));
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

    FScopeLock lock(&m_lock);

    if (m_current_job == NULL)
    {
        m_current_job = SkyBoxServiceImpl::Instance()->GetJob();
        if (m_current_job == NULL)
            return;
        UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！Get New Job, job_id = %d, scene_id = %d, position = (%.1f, %.1f, %.1f)"),
            m_current_job->JobID(), m_current_job->m_position.scene_id, m_current_job->m_position.x, m_current_job->m_position.y, m_current_job->m_position.z);
        SetActorLocation(FVector(m_current_job->m_position.x, m_current_job->m_position.y, m_current_job->m_position.z));
        m_CurrentDirection = 0;
        m_CurrentState = CaptureState::Waiting1;
        SetActorRotation(m_SixDirection[m_CurrentDirection]);
        //SkyBoxCamera->SetRelativeRotation(m_SixDirection[m_CurrentDirection]);
        UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！Change Direction, job_id = %d, m_CurrentDirection = %d"), m_current_job->JobID(), m_CurrentDirection);
        return;
    }
    if (m_CurrentState == CaptureState::Waiting1)
    {
        m_CurrentState = CaptureState::Prepared;
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
            SetActorRotation(m_SixDirection[m_CurrentDirection]);
            //SkyBoxCamera->SetRelativeRotation(m_SixDirection[m_CurrentDirection]);
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
    /*m_BackBufferFilePath = FString::Printf(TEXT("G:\\UE4Workspace\\png\\BACK(%dX%d)_%d__%04d-%02d-%02d_%02d-%02d-%02d_%d.png"),
        m_BackBufferSizeX, m_BackBufferSizeY, m_CurrentDirection, Time.GetYear(), Time.GetMonth(), Time.GetDay(), Time.GetHour(), Time.GetMinute(), Time.GetSecond(), Time.GetMillisecond());*/
    m_BackBufferFilePath = FString::Printf(TEXT("G:\\UE4Workspace\\png\\SkyBox(%dX%d)_Scene%d_(%.1f，%.1f，%.1f)_%d.png"),
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

