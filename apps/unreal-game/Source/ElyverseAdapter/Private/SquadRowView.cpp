#include "SquadRowView.h"

#include <iterator>

#include "random.hpp"

namespace
{
const TCHAR* const AttributeNames[] = {
	TEXT("Passing"), TEXT("Technique"), TEXT("First Touch"), TEXT("Dribbling"),
	TEXT("Finishing"), TEXT("Crossing"), TEXT("Tackling"), TEXT("Marking"),
	TEXT("Heading"), TEXT("Vision"), TEXT("Decisions"), TEXT("Positioning"),
	TEXT("Work Rate"), TEXT("Pace"), TEXT("Stamina"), TEXT("Strength"),
};
static_assert(std::size(AttributeNames) == FSquadRowView::AttributeCount);

const TCHAR* const FirstNames[] = {
	TEXT("Jonas"), TEXT("Luca"), TEXT("Mateo"), TEXT("Noah"), TEXT("Elias"), TEXT("Kenji"),
	TEXT("Samuel"), TEXT("Tomas"), TEXT("Rafael"), TEXT("Emil"), TEXT("Oskar"), TEXT("Yusuf"),
};
const TCHAR* const LastNames[] = {
	TEXT("Berger"), TEXT("Costa"), TEXT("Novak"), TEXT("Lindqvist"), TEXT("Moreau"), TEXT("Okafor"),
	TEXT("Rossi"), TEXT("Sato"), TEXT("Varga"), TEXT("Weber"), TEXT("Kowalski"), TEXT("Haddad"),
};
const TCHAR* const Positions[] = {
	TEXT("GK"), TEXT("DR"), TEXT("DC"), TEXT("DL"), TEXT("DM"),
	TEXT("MR"), TEXT("MC"), TEXT("ML"), TEXT("AMC"), TEXT("ST"),
};

template <typename T, int32 N>
T Pick(ElyverseFootball::SimCore::RandomNumberGenerator& Random, T (&Values)[N])
{
	return Values[Random.nextInt(0, N - 1)];
}
}

FString GetSquadAttributeName(const int32 AttributeIndex)
{
	return AttributeNames[AttributeIndex];
}

TArray<FSquadRowView> GenerateSquad(const uint64 Seed, const int32 Count)
{
	using namespace ElyverseFootball::SimCore;
	RandomNumberGenerator Random(deriveSeed(Seed, RandomNumberGeneratorDomain::kGeneration));

	TArray<FSquadRowView> Rows;
	Rows.Reserve(Count);
	for (int32 RowIndex = 0; RowIndex < Count; ++RowIndex)
	{
		FSquadRowView& Row = Rows.AddDefaulted_GetRef();
		Row.Name = FString::Printf(TEXT("%s %s"), Pick(Random, FirstNames), Pick(Random, LastNames));
		Row.Position = Pick(Random, Positions);
		Row.Age = Random.nextInt(16, 38);

		int64 AttributeSum = 0;
		for (int32& Attribute : Row.Attributes)
		{
			Attribute = Random.nextInt(1, 20);
			AttributeSum += Attribute;
		}
		Row.Fitness = Random.nextInt(60, 100);
		Row.ContractYears = Random.nextInt(0, 5);

		// Grows with the cube of the average attribute (2,500 at an average of 1).
		Row.MarketValue = AttributeSum * AttributeSum * AttributeSum * 2500 /
			(FSquadRowView::AttributeCount * FSquadRowView::AttributeCount * FSquadRowView::AttributeCount);
	}
	return Rows;
}
