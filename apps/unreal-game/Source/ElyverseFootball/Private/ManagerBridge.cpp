#include "ManagerBridge.h"

#include "Engine/GameInstance.h"
#include "Kismet/KismetSystemLibrary.h"
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
	UE_LOG(LogManagerBridge, Warning, TEXT("Unknown manager UI query '%s'"), *Name);
	return FString();
}

void UManagerBridge::Command(const FString& Name)
{
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
