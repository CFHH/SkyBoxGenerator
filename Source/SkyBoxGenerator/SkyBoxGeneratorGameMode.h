#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "SkyBoxGeneratorGameMode.generated.h"

UCLASS(minimalapi)
class ASkyBoxGeneratorGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASkyBoxGeneratorGameMode();
    virtual ~ASkyBoxGeneratorGameMode();
    virtual void StartPlay() override;
    virtual void ResetLevel() override;
    virtual bool ShouldTickIfViewportsOnly() const override;
    virtual void Tick(float DeltaSeconds) override;

private:
    void OnBackBufferReady_RenderThread(SWindow& SlateWindow, const FTexture2DRHIRef& BackBuffer);
};



