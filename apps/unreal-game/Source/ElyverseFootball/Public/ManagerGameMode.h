#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ManagerGameMode.generated.h"

// The game mode of every map while the manager UI is in charge: no pawn, and the UI from
// UManagerUISubsystem over the viewport.
UCLASS()
class ELYVERSEFOOTBALL_API AManagerGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AManagerGameMode();

protected:
	virtual void BeginPlay() override;
};
