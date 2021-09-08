#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Interface.h"
#include "SkyBoxGameInstance.generated.h"

UCLASS()
class SKYBOXGENERATOR_API USkyBoxGameInstance : public UGameInstance
{
	GENERATED_BODY()
	
public:
    void AddObjct(IGameShutDownNotify* obj);
    virtual void Shutdown() override;

private:
    TArray <IGameShutDownNotify*> m_objects;
};
