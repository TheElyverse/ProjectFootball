#include "ManagerUISubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "ManagerBridge.h"
#include "Misc/Paths.h"
#include "SWebBrowser.h"
#include "WebBrowserModule.h"

void UManagerUISubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// SWebBrowser only creates a browser if this module is already loaded, which the
	// WebBrowserWidget plugin would otherwise do.
	IWebBrowserModule::Get();
	Bridge = NewObject<UManagerBridge>(GetGameInstance());
	Bridge->Seed = Seed;
	Bridge->PlayerCount = PlayerCount;
}

void UManagerUISubsystem::Deinitialize()
{
	UGameViewportClient* Viewport = GetGameInstance()->GetGameViewportClient();
	if (Browser && Viewport)
	{
		Viewport->RemoveViewportWidgetContent(Browser.ToSharedRef());
	}
	Browser.Reset();
	Super::Deinitialize();
}

void UManagerUISubsystem::Show(APlayerController* Player)
{
	if (!Browser)
	{
		const FString Url = PageUrl.IsEmpty()
			? TEXT("file://") + FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("ManagerUI/index.html"))
			: PageUrl;
		Browser = SNew(SWebBrowser)
			.InitialURL(Url)
			.ShowControls(false)
			.BrowserFrameRate(BrowserFrameRate);
		Browser->BindUObject(TEXT("manager"), Bridge);
		// Slate content added here stays on the viewport across map changes.
		GetGameInstance()->GetGameViewportClient()->AddViewportWidgetContent(Browser.ToSharedRef());
	}

	if (Player)
	{
		FInputModeUIOnly InputMode;
		InputMode.SetWidgetToFocus(Browser);
		Player->SetInputMode(InputMode);
		Player->SetShowMouseCursor(true);
	}
}
