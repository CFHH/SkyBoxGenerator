#include "SkyBoxWorker.h"
#include "SkyBoxRPC.h"
#include "CoreMinimal.h"
#if (defined _WIN64) || (defined _WIN32)
#include "Windows/WindowsPlatformProcess.h"
#else
#include "Linux/LinuxPlatformProcess.h"
#endif


SkyBoxWorker* SkyBoxWorker::ms_instance = NULL;

SkyBoxWorker* SkyBoxWorker::StartUp()
{
    UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！SkyBoxWorker::StartUp()"));
    if (!ms_instance && FPlatformProcess::SupportsMultithreading())
    {
        ms_instance = new SkyBoxWorker();
    }
    return ms_instance;
}

void SkyBoxWorker::Shutdown()
{
    if (ms_instance)
    {
        ms_instance->EnsureCompletion();
        delete ms_instance;
        ms_instance = NULL;
    }
}

SkyBoxWorker::SkyBoxWorker() : StopTaskCounter(0)
{
    m_thread = FRunnableThread::Create(this, TEXT("SkyBoxWorker"), 0, TPri_Normal);
}

SkyBoxWorker::~SkyBoxWorker()
{
    delete m_thread;
    m_thread = NULL;
}

void SkyBoxWorker::EnsureCompletion()
{
    Stop();
    m_thread->WaitForCompletion();
}

bool SkyBoxWorker::Init()
{
    UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！SkyBoxWorker::Init()"));
    return true;
}

uint32 SkyBoxWorker::Run()
{
    UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！SkyBoxWorker::Run()"));
    FPlatformProcess::Sleep(3.0f);
    SkyBoxServiceImpl::RunServer();
    return 0;
}

void SkyBoxWorker::Stop()
{
    UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！SkyBoxWorker::Stop()"));
    StopTaskCounter.Increment();
    SkyBoxServiceImpl::ShutDownServer();
}