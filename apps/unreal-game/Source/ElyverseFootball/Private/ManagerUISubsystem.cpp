#include "ManagerUISubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "ManagerBridge.h"
#include "Misc/Paths.h"
#include "SWebBrowser.h"
#include "WebBrowserModule.h"

namespace
{
// Turns Slate's navigation keys into the page's nav.* inputs before any widget sees them. The
// browser takes the keyboard focus on every click and then swallows all keys, gamepad buttons
// included, so neither Enhanced Input nor CommonUI would get them. Slate's navigation config
// decides which keys navigate: arrow keys, Enter, Escape and the gamepad's D-pad, left stick
// and face buttons. Only while the browser has the focus, so that in Play In Editor the
// editor's panels keep their keys.
class FNavigationInput final : public IInputProcessor
{
public:
	FNavigationInput(TSharedRef<SWidget> InBrowser, TFunction<void(const TCHAR*)> InSend)
		: Browser(InBrowser), Send(MoveTemp(InSend))
	{
	}

	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}

	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
	{
		if (!IsBrowserFocused())
		{
			return false;
		}
		if (const TCHAR* Input = DirectionInput(SlateApp.GetNavigationDirectionFromKey(InKeyEvent)))
		{
			Send(Input);
			return true;
		}
		if (const TCHAR* Input = ActionInput(SlateApp.GetNavigationActionFromKey(InKeyEvent)))
		{
			// Holding Enter must not confirm again and again.
			if (!InKeyEvent.IsRepeat())
			{
				Send(Input);
			}
			return true;
		}
		return false;
	}

	// The page never saw the key going down, so it does not see it going up either.
	virtual bool HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
	{
		return IsBrowserFocused()
			&& (DirectionInput(SlateApp.GetNavigationDirectionFromKey(InKeyEvent)) != nullptr
				|| ActionInput(SlateApp.GetNavigationActionFromKey(InKeyEvent)) != nullptr);
	}

	virtual bool HandleAnalogInputEvent(FSlateApplication& SlateApp, const FAnalogInputEvent& InAnalogInputEvent) override
	{
		if (!IsBrowserFocused())
		{
			return false;
		}
		const TCHAR* Input = DirectionInput(SlateApp.GetNavigationDirectionFromAnalog(InAnalogInputEvent));
		if (Input == nullptr)
		{
			return false;
		}
		Send(Input);
		return true;
	}

	virtual const TCHAR* GetDebugName() const override { return TEXT("ManagerUINavigationInput"); }

private:
	bool IsBrowserFocused() const
	{
		const TSharedPtr<SWidget> Widget = Browser.Pin();
		return Widget && Widget->HasAnyUserFocusOrFocusedDescendants();
	}

	static const TCHAR* DirectionInput(const EUINavigation Direction)
	{
		switch (Direction)
		{
		case EUINavigation::Up:
			return TEXT("nav.up");
		case EUINavigation::Down:
			return TEXT("nav.down");
		default:
			return nullptr;
		}
	}

	static const TCHAR* ActionInput(const EUINavigationAction Action)
	{
		switch (Action)
		{
		case EUINavigationAction::Accept:
			return TEXT("nav.confirm");
		case EUINavigationAction::Back:
			return TEXT("nav.back");
		default:
			return nullptr;
		}
	}

	TWeakPtr<SWidget> Browser;
	TFunction<void(const TCHAR*)> Send;
};
} // namespace

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
	SetNavigationInput(false);
	UGameViewportClient* Viewport = GetGameInstance()->GetGameViewportClient();
	if (Browser && Viewport)
	{
		Viewport->RemoveViewportWidgetContent(Browser.ToSharedRef());
	}
	Browser.Reset();
	Super::Deinitialize();
}

void UManagerUISubsystem::Preload()
{
	if (Browser)
	{
		return;
	}
	const FString Url = PageUrl.IsEmpty()
		? TEXT("file://") + FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("ManagerUI/index.html"))
		: PageUrl;
	Browser = SNew(SWebBrowser)
		.InitialURL(Url)
		.ShowControls(false)
		.SupportsTransparency(true)
		.BrowserFrameRate(BrowserFrameRate);
	Browser->BindUObject(TEXT("manager"), Bridge);
	// Painted fully transparent instead of hidden, so the browser gets its size and renders
	// the page before it is shown.
	Browser->SetVisibility(EVisibility::HitTestInvisible);
	Browser->SetRenderOpacity(0.0f);
	// Slate content added here stays on the viewport across map changes. Its z-order 0 keeps
	// it below UMG widgets such as the studio splash, which AddToViewport puts at 10 and up.
	GetGameInstance()->GetGameViewportClient()->AddViewportWidgetContent(Browser.ToSharedRef());
}

void UManagerUISubsystem::Show(const FString& InRoute)
{
	Preload();
	bShown = true;
	if (!InRoute.IsEmpty() || Route.IsEmpty())
	{
		Route = InRoute.IsEmpty() ? TEXT("/") : InRoute;
		bNavigate = true;
	}
	Present();
}

void UManagerUISubsystem::Hide()
{
	bShown = false;
	SetNavigationInput(false);
	if (Browser)
	{
		Browser->SetVisibility(EVisibility::Hidden);
	}
	if (APlayerController* Player = GetGameInstance()->GetFirstLocalPlayerController())
	{
		Player->SetInputMode(FInputModeGameOnly());
	}
}

void UManagerUISubsystem::HandlePageReady()
{
	bPageReady = true;
	bNavigate = !Route.IsEmpty();
	Present();
}

void UManagerUISubsystem::Present()
{
	if (!bPageReady || !bShown)
	{
		return;
	}
	if (bNavigate)
	{
		CallPage(TEXT("navigate"), Route);
		bNavigate = false;
	}
	Browser->SetVisibility(EVisibility::Visible);
	Browser->SetRenderOpacity(1.0f);
	SetNavigationInput(true);
	if (APlayerController* Player = GetGameInstance()->GetFirstLocalPlayerController())
	{
		FInputModeUIOnly InputMode;
		InputMode.SetWidgetToFocus(Browser);
		Player->SetInputMode(InputMode);
		Player->SetShowMouseCursor(true);
	}
}

void UManagerUISubsystem::CallPage(const TCHAR* Function, const FString& Argument)
{
	Browser->ExecuteJavascript(
		FString::Printf(TEXT("window.ui.%s(\"%s\")"), Function, *Argument.ReplaceCharWithEscapedChar()));
}

void UManagerUISubsystem::SetNavigationInput(const bool bEnabled)
{
	if (!FSlateApplication::IsInitialized() || bEnabled == NavigationInput.IsValid())
	{
		return;
	}
	if (bEnabled)
	{
		NavigationInput = MakeShared<FNavigationInput>(Browser.ToSharedRef(), [this](const TCHAR* Input) { CallPage(TEXT("input"), Input); });
		// First in line, ahead of CommonUI's own input processors.
		FSlateApplication::Get().RegisterInputPreProcessor(NavigationInput, 0);
	}
	else
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(NavigationInput);
		NavigationInput.Reset();
	}
}
