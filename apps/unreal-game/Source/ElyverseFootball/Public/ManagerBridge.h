#pragma once

#include "CoreMinimal.h"
#include "ManagerBridge.generated.h"

// Everything the manager UI (apps/manager-ui) can ask the game or tell it to do. Bound as
// window.ue.manager; Unreal lower-cases bound names, so JavaScript calls
// window.ue.manager.query(name) and window.ue.manager.command(name).
UCLASS()
class ELYVERSEFOOTBALL_API UManagerBridge : public UObject
{
	GENERATED_BODY()

public:
	// The view model for the named query as JSON, in the shapes declared in
	// apps/manager-ui/src/ue/bridge.ts; empty for an unknown name.
	UFUNCTION()
	FString Query(const FString& Name) const;

	// "ready" tells UManagerUISubsystem that the page takes window.ui calls, "quit" ends the
	// game.
	UFUNCTION()
	void Command(const FString& Name);

	// The bridge lives in the game instance, so commands act on its current world.
	virtual UWorld* GetWorld() const override;

	// Until the world simulation exists, the squad is generated from these.
	int64 Seed = 42;
	int32 PlayerCount = 500;

private:
	FString SquadJson() const;
	FString MessagesJson() const;
};
