#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ManagerUISubsystem.generated.h"

class IInputProcessor;
class SWebBrowser;
class UManagerBridge;

// Owns the manager UI for the whole session: one transparent web browser over the game
// viewport with UManagerBridge bound as window.ue.manager. It lives in the game instance, so
// map changes (main menu, world, match day) keep the page and its state. Navigation happens
// in the page: Unreal only says which route to show (window.ui.navigate) and forwards the
// navigation inputs (window.ui.input). Settings come from the
// [/Script/ElyverseFootball.ManagerUISubsystem] section of DefaultGame.ini.
UCLASS(Config = Game)
class ELYVERSEFOOTBALL_API UManagerUISubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Puts the browser over the viewport, invisible, and starts loading the page, for example
	// behind the studio splash. Show does this itself when needed.
	UFUNCTION(BlueprintCallable, Category = "Manager UI")
	void Preload();

	// Shows the page at the route ("/" is the main menu) and gives it the first local
	// player's input. Before the page is ready, it stays invisible and shows the route once
	// it is.
	UFUNCTION(BlueprintCallable, Category = "Manager UI")
	void Show(const FString& Route);

	// Hides the page and gives the input back to the game; the page keeps its state.
	UFUNCTION(BlueprintCallable, Category = "Manager UI")
	void Hide();

	// Called through UManagerBridge once the page takes window.ui calls, again after every
	// reload.
	void HandlePageReady();

private:
	// Shows the current route once the page is ready.
	void Present();
	void CallPage(const TCHAR* Function, const FString& Argument);
	void SetNavigationInput(bool bEnabled);

	// Empty loads Content/ManagerUI/index.html, which apps/manager-ui builds and packaging
	// stages as a loose file; the Vite dev server (http://localhost:5173) gives hot reload
	// inside Unreal.
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
	TSharedPtr<IInputProcessor> NavigationInput;

	// The route Show asked for; unset while the page is hidden.
	TOptional<FString> Route;
	bool bPageReady = false;
};
