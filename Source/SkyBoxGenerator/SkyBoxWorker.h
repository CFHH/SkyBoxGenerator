#pragma once
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"


class SkyBoxWorker : public FRunnable
{
public:
    static SkyBoxWorker* StartUp();
    static void Shutdown();
private:
    SkyBoxWorker();
public:
    virtual ~SkyBoxWorker();
    void EnsureCompletion();
public:
    //FRunnable interface
    virtual bool Init();
    virtual uint32 Run();
    virtual void Stop();
private:
    static  SkyBoxWorker* ms_instance;
    FRunnableThread* m_thread;
    FThreadSafeCounter StopTaskCounter;

};