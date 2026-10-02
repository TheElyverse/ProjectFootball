#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ManagerUISubsystem.generated.h"

class APlayerController;
class SWebBrowser;
class UManagerBridge;

// Owns the manager UI for the whole session: one web browser over the game viewport with
// UManagerBridge bound as window.ue.manager. It lives in the game instance, so map
// changes (main menu, world, match day) keep the page and its state. Settings come from
// the [/Script/ElyverseFootball.ManagerUISubsystem] section of DefaultGame.ini.
UCLASS(Config = Game)
class ELYVERSEFOOTBALL_API UManagerUISubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Puts the UI over the viewport on first use and gives it the player's input.
	void Show(APlayerController* Player);

private:
	// Empty loads the built apps/manager-ui/dist/index.html; the Vite dev server
	// (http://localhost:5173) gives hot reload inside Unreal.
	UPROPERTY(Config)
	FString PageUrl;

	// SWebBrowser renders at 24 fps unless told otherwise.
	UPROPERTY(Config)
	int32 BrowserFrameRate = 60;

	UPROPERTY(Config)
	int64 Seed = 42;

	UPROPERTY(Config)
	int32 PlayerCount = 500;

	UPROPERTY()
	TObjectPtr<UManagerBridge> Bridge;

	TSharedPtr<SWebBrowser> Browser;
};
