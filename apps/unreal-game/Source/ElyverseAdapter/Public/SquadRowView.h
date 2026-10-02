#pragma once

#include "CoreMinimal.h"

// One row of the squad table, already projected to the values the UI shows (the 1-20
// attribute scale of docs/implementation-plan.md section 8.1). Stands in for the
// application layer's query models until those exist.
struct FSquadRowView
{
	static constexpr int32 AttributeCount = 16;

	FString Name;
	FString Position;
	int32 Age = 0;
	int32 Attributes[AttributeCount] = {};
	int32 Fitness = 0;
	int32 ContractYears = 0;
	int64 MarketValue = 0;
};

ELYVERSEADAPTER_API FString GetSquadAttributeName(int32 AttributeIndex);

// A synthetic squad until the world simulation exists, drawn from sim-core's generation
// stream: the same seed and count always give the same rows.
ELYVERSEADAPTER_API TArray<FSquadRowView> GenerateSquad(uint64 Seed, int32 Count);
