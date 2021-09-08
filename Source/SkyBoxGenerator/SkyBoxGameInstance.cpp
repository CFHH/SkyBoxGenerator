#include "SkyBoxGameInstance.h"


void USkyBoxGameInstance::AddObjct(IGameShutDownNotify* obj)
{
    m_objects.Add(obj);
}

void USkyBoxGameInstance::Shutdown()
{
    Super::Shutdown();
    
    for (IGameShutDownNotify* obj  : m_objects)
    {
        obj->ShutDown();
    }
}

