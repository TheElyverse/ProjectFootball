#include "ManagerBridge.h"

#include "Engine/GameInstance.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Kismet/KismetSystemLibrary.h"
#include "ManagerUISubsystem.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonWriter.h"
#include "SquadRowView.h"

DEFINE_LOG_CATEGORY_STATIC(LogManagerBridge, Log, All);

FString UManagerBridge::Query(const FString& Name) const
{
	if (Name == TEXT("squad"))
	{
		return SquadJson();
	}
	if (Name == TEXT("messages"))
	{
		return MessagesJson();
	}
	UE_LOG(LogManagerBridge, Warning, TEXT("Unknown manager UI query '%s'"), *Name);
	return FString();
}

void UManagerBridge::Command(const FString& Name)
{
	if (Name == TEXT("ready"))
	{
		GetTypedOuter<UGameInstance>()->GetSubsystem<UManagerUISubsystem>()->HandlePageReady();
		return;
	}
	if (Name == TEXT("quit"))
	{
		// QuitGame ends a Play In Editor session instead of closing the editor.
		UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
		return;
	}
	UE_LOG(LogManagerBridge, Warning, TEXT("Unknown manager UI command '%s'"), *Name);
}

UWorld* UManagerBridge::GetWorld() const
{
	const UGameInstance* GameInstance = GetTypedOuter<UGameInstance>();
	return GameInstance ? GameInstance->GetWorld() : nullptr;
}

FString UManagerBridge::SquadJson() const
{
	FString Json;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);

	Writer->WriteObjectStart();
	Writer->WriteArrayStart(TEXT("attributeNames"));
	for (int32 Index = 0; Index < FSquadRowView::AttributeCount; ++Index)
	{
		Writer->WriteValue(GetSquadAttributeName(Index));
	}
	Writer->WriteArrayEnd();

	Writer->WriteArrayStart(TEXT("rows"));
	for (const FSquadRowView& Row : GenerateSquad(static_cast<uint64>(Seed), PlayerCount))
	{
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("name"), Row.Name);
		Writer->WriteValue(TEXT("position"), Row.Position);
		Writer->WriteValue(TEXT("age"), Row.Age);
		Writer->WriteArrayStart(TEXT("attributes"));
		for (const int32 Attribute : Row.Attributes)
		{
			Writer->WriteValue(Attribute);
		}
		Writer->WriteArrayEnd();
		Writer->WriteValue(TEXT("fitness"), Row.Fitness);
		Writer->WriteValue(TEXT("contractYears"), Row.ContractYears);
		Writer->WriteValue(TEXT("marketValue"), Row.MarketValue);
		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd();
	Writer->WriteObjectEnd();
	Writer->Close();
	return Json;
}

// The UI texts for the game's language. The page bundles English and falls back to it for
// every text missing here, so a language without a catalog sends none.
FString UManagerBridge::MessagesJson() const
{
	const FString Locale = FInternationalization::Get().GetCurrentLanguage()->GetTwoLetterISOLanguageName();
	const FString Path = FPaths::ProjectContentDir() / TEXT("ManagerUI/locales") / Locale + TEXT(".json");
	FString Messages;
	if (!FFileHelper::LoadFileToString(Messages, *Path))
	{
		UE_LOG(LogManagerBridge, Log, TEXT("No manager UI texts for '%s', using English"), *Locale);
		return TEXT("{\"locale\":\"en\",\"messages\":{}}");
	}
	return FString::Printf(TEXT("{\"locale\":\"%s\",\"messages\":%s}"), *Locale, *Messages);
}
