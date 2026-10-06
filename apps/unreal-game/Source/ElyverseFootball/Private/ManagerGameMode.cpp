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
	GetGameInstance()->GetSubsystem<UManagerUISubsystem>()->Show(TEXT("/"));
}
