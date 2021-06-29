// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "SkyBoxGeneratorHUD.generated.h"

UCLASS()
class ASkyBoxGeneratorHUD : public AHUD
{
	GENERATED_BODY()

public:
	ASkyBoxGeneratorHUD();

	/** Primary draw call for the HUD */
	virtual void DrawHUD() override;

private:
	/** Crosshair asset pointer */
	class UTexture2D* CrosshairTex;

};

