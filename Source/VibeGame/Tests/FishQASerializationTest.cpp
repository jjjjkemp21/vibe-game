// QA-owned serialization tests for FFishInstance (T-008): SaveGame round trips (the USaveGame archive setup, and the
// SaveGame-flag filter), a reflection lint for replication/SaveGame friendliness, and a network round trip through the
// engine's replication layout (FRepLayout, the path struct properties and RPC parameters use), with the size budget.
// Written by the qa-engineer from docs/specs/fish-system-rules.md and Fish/FishInstance.h (black-box).

#include "FishQATestHelpers.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "UObject/CoreNet.h"
#include "UObject/UnrealType.h"
#include "Net/RepLayout.h"
#include <limits>

namespace FishQA_Ser
{
	using namespace FishQA;

	static TArray<uint8> SaveBytes(FFishInstance& Fish, bool bSaveGameFlag)
	{
		TArray<uint8> Bytes;
		FMemoryWriter Writer(Bytes, /*bIsPersistent*/ true);
		FObjectAndNameAsStringProxyArchive Ar(Writer, /*bInLoadIfFindFails*/ false);
		Ar.ArIsSaveGame = bSaveGameFlag;
		FFishInstance::StaticStruct()->SerializeItem(Ar, &Fish, nullptr);
		return Bytes;
	}

	static FFishInstance LoadBytes(const TArray<uint8>& Bytes, bool bSaveGameFlag)
	{
		FMemoryReader Reader(Bytes, /*bIsPersistent*/ true);
		FObjectAndNameAsStringProxyArchive Ar(Reader, /*bInLoadIfFindFails*/ true);
		Ar.ArIsSaveGame = bSaveGameFlag;
		FFishInstance Fish;
		FFishInstance::StaticStruct()->SerializeItem(Ar, &Fish, nullptr);
		return Fish;
	}

	/** A catch with every field non-default: 3 modifiers, every registered stat plus a data-added one, extreme numbers */
	static TArray<FFishInstance> RichInstances(FAutomationTestBase& Test)
	{
		TArray<FFishInstance> Out;
		FTables Real;
		if (!LoadReal(Test, Real))
		{
			return Out;
		}
		FFishRollContext Context = Ctx(TEXT("CoralSnapper"), 123);
		Context.ForcedRarityId = TEXT("Rare");
		Context.bForceModifiers = true;
		Context.ForcedModifierIds = { TEXT("Feisty"), TEXT("Heavy"), TEXT("Albino") };
		FFishInstance Base;
		FFishRoll::Roll(Real.Get(), Context, Base);
		Base.Stats.Add(FFishStatValue(Tag(QAStat), 42.5f));
		Out.Add(Base);

		FFishInstance Tiny = Base;
		Tiny.WeightKg = 0.001f;
		Tiny.Seed = std::numeric_limits<int32>::min();
		Tiny.Level = 99;
		Tiny.Value = std::numeric_limits<int32>::max();
		Tiny.Xp = 12345;
		Tiny.DifficultyRating = 1.2345f;
		Out.Add(Tiny);

		FFishInstance Huge = Base;
		Huge.WeightKg = 1e6f;
		Huge.Seed = std::numeric_limits<int32>::max();
		Huge.ModifierIds = { TEXT("Mod_2"), TEXT("Mod_10") };
		Out.Add(Huge);
		return Out;
	}

	/** QA-108: the USaveGame archive setup (FObjectAndNameAsStringProxyArchive, as SaveGameToMemory) round-trips every field */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQASaveRoundTripTest, "Project.Fish.QA.Save.StructRoundTrip", FISH_QA_FLAGS)
	bool FFishQASaveRoundTripTest::RunTest(const FString& Parameters)
	{
		for (FFishInstance& Fish : RichInstances(*this))
		{
			const FFishInstance Loaded = LoadBytes(SaveBytes(Fish, false), false);
			TestTrue(FString::Printf(TEXT("round trip identical: %s"), *Fish.ToString()), Same(Fish, Loaded));
			TestEqual(TEXT("GetStat(QA stat) survives"), Loaded.GetStat(Tag(QAStat)), Fish.GetStat(Tag(QAStat)));
			TestEqual(TEXT("HasModifier survives"), Loaded.HasModifier(Fish.ModifierIds[0]), true);
		}
		return true;
	}

	/** QA-109: with the SaveGame filter on (ArIsSaveGame), every field still round-trips, so every field is UPROPERTY(SaveGame) */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQASaveGameFlagTest, "Project.Fish.QA.Save.SaveGameFlagRoundTrip", FISH_QA_FLAGS)
	bool FFishQASaveGameFlagTest::RunTest(const FString& Parameters)
	{
		for (FFishInstance& Fish : RichInstances(*this))
		{
			const FFishInstance Loaded = LoadBytes(SaveBytes(Fish, true), true);
			TestTrue(FString::Printf(TEXT("SaveGame-filtered round trip identical: %s"), *Fish.ToString()), Same(Fish, Loaded));
		}
		return true;
	}

	static void LintStruct(FAutomationTestBase& Test, const UStruct* Struct, const FString& Path, int32& OutProperties)
	{
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			const FProperty* Property = *It;
			const FString Where = Path + TEXT(".") + Property->GetName();
			++OutProperties;
			Test.TestTrue(Where + TEXT(" is UPROPERTY(SaveGame)"), Property->HasAnyPropertyFlags(CPF_SaveGame));
			Test.TestFalse(Where + TEXT(" is not Transient"), Property->HasAnyPropertyFlags(CPF_Transient));
			Test.TestFalse(Where + TEXT(" is not NotReplicated"), Property->HasAnyPropertyFlags(CPF_RepSkip));
			Test.TestNull(*(Where + TEXT(" is not a TMap (doesn't replicate)")), CastField<FMapProperty>(Property));
			Test.TestNull(*(Where + TEXT(" is not a TSet (doesn't replicate)")), CastField<FSetProperty>(Property));
			Test.TestNull(*(Where + TEXT(" is not a UObject pointer")), CastField<FObjectPropertyBase>(Property));
			Test.TestNull(*(Where + TEXT(" is not FText (display text lives in the rows)")), CastField<FTextProperty>(Property));
			const FProperty* Inner = Property;
			if (const FArrayProperty* Array = CastField<FArrayProperty>(Property))
			{
				Inner = Array->Inner;
			}
			if (const FStructProperty* StructProperty = CastField<FStructProperty>(Inner))
			{
				if (StructProperty->Struct != FGameplayTag::StaticStruct())
				{
					LintStruct(Test, StructProperty->Struct, Where, OutProperties);
				}
			}
		}
	}

	/** QA-110: FFishInstance (recursively) is SaveGame- and replication-friendly */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQASaveLintTest, "Project.Fish.QA.Save.ReflectionLint", FISH_QA_FLAGS)
	bool FFishQASaveLintTest::RunTest(const FString& Parameters)
	{
		int32 Properties = 0;
		LintStruct(*this, FFishInstance::StaticStruct(), TEXT("FFishInstance"), Properties);
		TestTrue(FString::Printf(TEXT("the lint saw the record's fields (%d)"), Properties), Properties >= 10);
		return true;
	}

	/** QA-111 (P2): loading never recomputes the record: changed tables don't change a saved catch */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQASaveNoRecomputeTest, "Project.Fish.QA.Save.LoadDoesNotRecompute", FISH_QA_FLAGS)
	bool FFishQASaveNoRecomputeTest::RunTest(const FString& Parameters)
	{
		FTables Real;
		if (!LoadReal(*this, Real))
		{
			return false;
		}
		FFishInstance Fish;
		FFishRoll::Roll(Real.Get(), Ctx(TEXT("Bonefish"), 9), Fish);
		const TArray<uint8> Bytes = SaveBytes(Fish, true);
		Row<FFishSpeciesRow>(Real.Species, TEXT("Bonefish"))->BaseValuePerKg *= 10.0f; // a later rebalance
		Row<FFishSpeciesRow>(Real.Species, TEXT("Bonefish"))->BaseStats[0].Value += 50.0f;
		const FFishInstance Loaded = LoadBytes(Bytes, true);
		TestTrue(TEXT("the loaded catch is the saved catch"), Same(Fish, Loaded));
		FFishInstance Rerolled;
		FFishRoll::Roll(Real.Get(), Ctx(TEXT("Bonefish"), 9), Rerolled);
		TestTrue(TEXT("(sanity) a reroll with the new data does differ"), Rerolled.Value != Loaded.Value);
		return true;
	}

	/** QA-112 (P2): a full cooler (20 catches) round-trips through both archive setups */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQASaveCoolerTest, "Project.Fish.QA.Save.CoolerArrayRoundTrip", FISH_QA_FLAGS)
	bool FFishQASaveCoolerTest::RunTest(const FString& Parameters)
	{
		FTables Real;
		if (!LoadReal(*this, Real))
		{
			return false;
		}
		TArray<FFishInstance> Cooler;
		for (int32 i = 0; i < 20; ++i)
		{
			FFishRollContext Context = Ctx(i % 2 ? TEXT("Bonefish") : TEXT("CoralSnapper"), 1000 + i);
			Context.Luck = float(i % 11);
			FFishInstance Fish;
			FFishRoll::Roll(Real.Get(), Context, Fish);
			Cooler.Add(Fish);
		}
		Cooler.Add(FFishInstance()); // an empty slot
		for (bool bSaveGameFlag : { false, true })
		{
			TArray<uint8> Bytes;
			{
				FMemoryWriter Writer(Bytes, true);
				FObjectAndNameAsStringProxyArchive Ar(Writer, false);
				Ar.ArIsSaveGame = bSaveGameFlag;
				int32 Count = Cooler.Num();
				Ar << Count;
				for (FFishInstance& Fish : Cooler)
				{
					FFishInstance::StaticStruct()->SerializeItem(Ar, &Fish, nullptr);
				}
			}
			TArray<FFishInstance> Loaded;
			{
				FMemoryReader Reader(Bytes, true);
				FObjectAndNameAsStringProxyArchive Ar(Reader, true);
				Ar.ArIsSaveGame = bSaveGameFlag;
				int32 Count = 0;
				Ar << Count;
				Loaded.SetNum(Count);
				for (FFishInstance& Fish : Loaded)
				{
					FFishInstance::StaticStruct()->SerializeItem(Ar, &Fish, nullptr);
				}
			}
			TestEqual(TEXT("count"), Loaded.Num(), Cooler.Num());
			int32 Mismatches = 0;
			for (int32 i = 0; i < FMath::Min(Loaded.Num(), Cooler.Num()); ++i)
			{
				Mismatches += Same(Loaded[i], Cooler[i]) ? 0 : 1;
			}
			TestEqual(FString::Printf(TEXT("cooler round trip (SaveGame flag %d)"), bSaveGameFlag), Mismatches, 0);
			TestFalse(TEXT("the empty slot stays empty"), Loaded.Last().IsValid());
		}
		return true;
	}

	/** Serializes through the engine's replication layout for the struct and reads it back */
	static bool NetRoundTrip(FAutomationTestBase& Test, FFishInstance& Fish, FFishInstance& OutLoaded, int64& OutBits)
	{
		const TSharedPtr<FRepLayout> Layout = FRepLayout::CreateFromStruct(FFishInstance::StaticStruct(), nullptr, ECreateRepLayoutFlags::None);
		if (!Test.TestTrue(TEXT("FRepLayout for FFishInstance"), Layout.IsValid()))
		{
			return false;
		}
		FNetBitWriter Writer(nullptr, 64 * 1024 * 8);
		bool bHasUnmapped = false;
		Layout->SerializePropertiesForStruct(FFishInstance::StaticStruct(), Writer, nullptr, &Fish, bHasUnmapped);
		Test.TestFalse(TEXT("writer did not overflow"), Writer.IsError());
		Test.TestFalse(TEXT("no unmapped references"), bHasUnmapped);
		OutBits = Writer.GetNumBits();
		FNetBitReader Reader(nullptr, Writer.GetData(), Writer.GetNumBits());
		bool bReadUnmapped = false;
		Layout->SerializePropertiesForStruct(FFishInstance::StaticStruct(), Reader, nullptr, &OutLoaded, bReadUnmapped);
		Test.TestFalse(TEXT("reader did not overflow"), Reader.IsError());
		return true;
	}

	/** QA-114: FFishInstance survives a network round trip through FRepLayout (property replication path) */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQANetRoundTripTest, "Project.Fish.QA.Net.RepLayoutRoundTrip", FISH_QA_FLAGS)
	bool FFishQANetRoundTripTest::RunTest(const FString& Parameters)
	{
		static_assert(!TStructOpsTypeTraits<FFishInstance>::WithNetSerializer, "FFishInstance now has a custom NetSerialize: add a NetSerialize round-trip test (QA-115)");
		for (FFishInstance& Fish : RichInstances(*this))
		{
			FFishInstance Loaded;
			int64 Bits = 0;
			if (NetRoundTrip(*this, Fish, Loaded, Bits))
			{
				TestTrue(FString::Printf(TEXT("net round trip identical (%lld bits): %s"), Bits, *Fish.ToString()), Same(Fish, Loaded));
			}
		}
		FFishInstance Empty;
		FFishInstance LoadedEmpty;
		int64 Bits = 0;
		if (NetRoundTrip(*this, Empty, LoadedEmpty, Bits))
		{
			TestTrue(TEXT("an empty slot round-trips"), Same(Empty, LoadedEmpty));
		}
		return true;
	}

	/** QA-116: a typical catch (2 modifiers, the starter stats) replicates in <= 256 bytes (Q26 budget); the size is reported */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishQANetSizeTest, "Project.Fish.QA.Net.SizeBudget", FISH_QA_FLAGS)
	bool FFishQANetSizeTest::RunTest(const FString& Parameters)
	{
		FTables Real;
		if (!LoadReal(*this, Real))
		{
			return false;
		}
		FFishRollContext Context = Ctx(TEXT("CoralSnapper"), 5);
		Context.bForceModifiers = true;
		Context.ForcedModifierIds = { TEXT("Feisty"), TEXT("Heavy") };
		FFishInstance Fish;
		FFishRoll::Roll(Real.Get(), Context, Fish);
		FFishInstance Loaded;
		int64 Bits = 0;
		if (NetRoundTrip(*this, Fish, Loaded, Bits))
		{
			const int64 Bytes = (Bits + 7) / 8;
			AddInfo(FString::Printf(TEXT("typical FFishInstance (%d stats, %d modifiers): %lld bits = %lld bytes on the wire"), Fish.Stats.Num(), Fish.ModifierIds.Num(), Bits, Bytes));
			// Breakdown for the report: the same catch without modifiers, without stats, and an empty slot
			FFishInstance NoMods = Fish;
			NoMods.ModifierIds.Reset();
			FFishInstance NoStats = NoMods;
			NoStats.Stats.Reset();
			FFishInstance Empty;
			for (TPair<const TCHAR*, FFishInstance*> Variant : { TPair<const TCHAR*, FFishInstance*>(TEXT("no modifiers"), &NoMods),
				TPair<const TCHAR*, FFishInstance*>(TEXT("no modifiers, no stats"), &NoStats), TPair<const TCHAR*, FFishInstance*>(TEXT("empty slot"), &Empty) })
			{
				FFishInstance VariantLoaded;
				int64 VariantBits = 0;
				NetRoundTrip(*this, *Variant.Value, VariantLoaded, VariantBits);
				AddInfo(FString::Printf(TEXT("  %s: %lld bytes"), Variant.Key, (VariantBits + 7) / 8));
			}
			TestTrue(FString::Printf(TEXT("<= 256 bytes per typical instance (got %lld)"), Bytes), Bytes <= 256);
		}
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
