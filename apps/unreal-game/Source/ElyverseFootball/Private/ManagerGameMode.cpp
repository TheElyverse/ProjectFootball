#include "ManagerGameMode.h"

#include "Engine/GameInstance.h"
#include "ManagerUISubsystem.h"

AManagerGameMode::AManagerGameMode()
{
	DefaultPawnClass = nullptr;
}

void AManagerGameMode::BeginPlay()
{
	Super::BeginPlay();
	// Keeps the page's route across map changes, but gives it the new player controller.
	GetGameInstance()->GetSubsystem<UManagerUISubsystem>()->Show(FString());
}
