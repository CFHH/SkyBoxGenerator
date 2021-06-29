#include "SkyBoxGeneratorGameMode.h"
#include "SkyBoxGeneratorHUD.h"
#include "SkyBoxGeneratorCharacter.h"
#include "SkyBoxGeneratorCamera.h"
#include "UObject/ConstructorHelpers.h"

ASkyBoxGeneratorGameMode::ASkyBoxGeneratorGameMode()
	: Super()
{
    UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！ASkyBoxGeneratorGameMode()"));

	// set default pawn class to our Blueprinted character
	//static ConstructorHelpers::FClassFinder<APawn> PlayerPawnClassFinder(TEXT("/Game/FirstPersonCPP/Blueprints/FirstPersonCharacter"));
    static ConstructorHelpers::FClassFinder<APawn> PlayerPawnClassFinder(TEXT("/Game/Blueprints/BP_SkyBoxGeneratorCamera"));
	DefaultPawnClass = PlayerPawnClassFinder.Class;

	// use our custom HUD class
	HUDClass = ASkyBoxGeneratorHUD::StaticClass();

    // 为了在编辑器里执行Tick
    PrimaryActorTick.bCanEverTick = true;
}

ASkyBoxGeneratorGameMode::~ASkyBoxGeneratorGameMode()
{
    UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！~ASkyBoxGeneratorGameMode()"));
    //FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().RemoveAll(this);
}

void ASkyBoxGeneratorGameMode::StartPlay()
{
    UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！ASkyBoxGeneratorGameMode::StartPlay()"));
    Super::StartPlay();
    //FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().AddUObject(this, &ASkyBoxGeneratorGameMode::OnBackBufferReady_RenderThread);
}

void ASkyBoxGeneratorGameMode::ResetLevel()
{
    Super::ResetLevel();
}

bool ASkyBoxGeneratorGameMode::ShouldTickIfViewportsOnly() const
{
    // 为了在编辑器里执行Tick
    return true;
}

void ASkyBoxGeneratorGameMode::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
}

void ASkyBoxGeneratorGameMode::OnBackBufferReady_RenderThread(SWindow& SlateWindow, const FTexture2DRHIRef& BackBuffer)
{
}
