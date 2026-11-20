// SpawnPacerCore - unit tests for the policy (FSpawnCostModel, FSpawnPacerScheduler): fake jobs, fake costs, no world.
// Run with: Tools\run_tests.py (through the machine queue), filter "SpawnPacer.Policy".

#include "Misc/AutomationTest.h"
#include "SpawnCostModel.h"
#include "SpawnPacerScheduler.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace SpawnPacerTests
{
	constexpr double Tolerance = 1e-9;
	/** Editor and game contexts; one filter (ProductFilter: a project's own tests), as the macro requires. */
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter;

	/** A key for a stage; the class is irrelevant to the policy, any UClass will do. */
	FSpawnCostKey Key(ESpawnPacerStage Stage, FName Component = NAME_None)
	{
		return { UObject::StaticClass(), Stage, Component };
	}

	/** A queue of fake jobs: each has a key and the cost it "measures" when it runs. */
	struct FFakeQueue
	{
		struct FJob
		{
			FSpawnCostKey Key;
			double CostMs = 0.0;
		};
		TArray<FJob> Jobs;
		int32 Next = 0;

		void Add(const FSpawnCostKey& InKey, double CostMs, int32 Count = 1)
		{
			for (int32 Index = 0; Index < Count; ++Index)
			{
				Jobs.Add({ InKey, CostMs });
			}
		}

		FSpawnPacerFrameStats RunFrame(double BudgetMs, FSpawnCostModel& Model)
		{
			return FSpawnPacerScheduler::RunFrame(BudgetMs, Model,
				[this]() { return Jobs.IsValidIndex(Next) ? TOptional<FSpawnCostKey>(Jobs[Next].Key) : TOptional<FSpawnCostKey>(); },
				[this]() { return Jobs[Next++].CostMs; });
		}
	};
}

// The estimator follows RFC 6298 section 2 exactly (values worked out by hand).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpawnPacerEstimatorTest, "SpawnPacer.Policy.EstimatorFollowsRfc6298", SpawnPacerTests::Flags)
bool FSpawnPacerEstimatorTest::RunTest(const FString& Parameters)
{
	using namespace SpawnPacerTests;
	FSpawnCostModel Model(2.0);
	const FSpawnCostKey A = Key(ESpawnPacerStage::Spawn);

	Model.Observe(A, 4.0);                           // first sample: mean 4, deviation 2
	const FSpawnCostModel::FEstimate* Estimate = Model.Find(A);
	if (!TestNotNull(TEXT("estimate after the first sample"), Estimate))
	{
		return false;
	}
	TestEqual(TEXT("first mean"), Estimate->MeanMs, 4.0, Tolerance);
	TestEqual(TEXT("first deviation"), Estimate->DeviationMs, 2.0, Tolerance);
	TestEqual(TEXT("first prediction 4 + 2 x 2"), Model.Predict(A, 99.0), 8.0, Tolerance);

	Model.Observe(A, 2.0);                           // deviation 0.75 x 2 + 0.25 x |4 - 2| = 2; mean 0.875 x 4 + 0.125 x 2 = 3.75
	Estimate = Model.Find(A);                        // found again: Observe may not keep the old pointer valid
	TestEqual(TEXT("second deviation uses the old mean"), Estimate->DeviationMs, 2.0, Tolerance);
	TestEqual(TEXT("second mean"), Estimate->MeanMs, 3.75, Tolerance);
	TestEqual(TEXT("second prediction 3.75 + 2 x 2"), Model.Predict(A, 99.0), 7.75, Tolerance);
	TestEqual(TEXT("samples"), Estimate->Samples, 2);
	return true;
}

// A key never measured is predicted as the cold value (the scheduler passes the budget), and stops being cold.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpawnPacerColdKeyTest, "SpawnPacer.Policy.ColdKeyIsPredictedAsTheBudget", SpawnPacerTests::Flags)
bool FSpawnPacerColdKeyTest::RunTest(const FString& Parameters)
{
	using namespace SpawnPacerTests;
	FSpawnCostModel Model;
	const FSpawnCostKey A = Key(ESpawnPacerStage::Register, TEXT("Part00"));
	const FSpawnCostKey B = Key(ESpawnPacerStage::Register, TEXT("Part01"));
	TestTrue(TEXT("cold"), Model.IsCold(A));
	TestEqual(TEXT("cold prediction"), Model.Predict(A, 2.0), 2.0, Tolerance);
	Model.Observe(A, 0.1);
	TestFalse(TEXT("warm after one sample"), Model.IsCold(A));
	TestTrue(TEXT("a different component is a different key"), Model.IsCold(B));
	return true;
}

// The core rule: once a job has run, a job whose prediction doesn't fit what is left is never started.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpawnPacerFitTest, "SpawnPacer.Policy.NeverStartsAJobThatWontFit", SpawnPacerTests::Flags)
bool FSpawnPacerFitTest::RunTest(const FString& Parameters)
{
	using namespace SpawnPacerTests;
	FSpawnCostModel Model;
	const FSpawnCostKey A = Key(ESpawnPacerStage::Spawn);
	Model.Seed(A, 0.6, 0.0);
	FFakeQueue Queue;
	Queue.Add(A, 0.6, 5);

	const FSpawnPacerFrameStats Stats = Queue.RunFrame(2.0, Model);   // 0.6 x 3 = 1.8; a 4th would make 2.4
	TestEqual(TEXT("jobs"), Stats.Jobs, 3);
	TestEqual(TEXT("spent"), Stats.SpentMs, 1.8, Tolerance);
	TestTrue(TEXT("stopped on the budget"), Stats.bStoppedOnBudget);
	TestEqual(TEXT("the 4th job is still at the head"), Queue.Next, 3);
	TestEqual(TEXT("nothing forced"), Stats.ForcedJobs, 0);
	return true;
}

// Progress: the frame's first job always runs, even if it is predicted above the budget, and that is counted.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpawnPacerForcedTest, "SpawnPacer.Policy.FirstJobAlwaysRunsAndIsCountedForced", SpawnPacerTests::Flags)
bool FSpawnPacerForcedTest::RunTest(const FString& Parameters)
{
	using namespace SpawnPacerTests;
	FSpawnCostModel Model;
	const FSpawnCostKey Big = Key(ESpawnPacerStage::Finish);
	Model.Seed(Big, 5.0, 0.0);
	FFakeQueue Queue;
	Queue.Add(Big, 5.0, 2);

	const FSpawnPacerFrameStats Stats = Queue.RunFrame(2.0, Model);
	TestEqual(TEXT("one job"), Stats.Jobs, 1);
	TestEqual(TEXT("forced"), Stats.ForcedJobs, 1);
	TestTrue(TEXT("stopped"), Stats.bStoppedOnBudget);
	return true;
}

// A cold job runs only as the frame's first job (it is predicted as the whole budget, and refused after a first job).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpawnPacerColdJobTest, "SpawnPacer.Policy.ColdJobRunsOnlyFirst", SpawnPacerTests::Flags)
bool FSpawnPacerColdJobTest::RunTest(const FString& Parameters)
{
	using namespace SpawnPacerTests;
	const FSpawnCostKey Warm = Key(ESpawnPacerStage::Spawn);
	const FSpawnCostKey Cold = Key(ESpawnPacerStage::Construct);
	{
		FSpawnCostModel Model;
		Model.Seed(Warm, 0.5, 0.0);
		FFakeQueue Queue;
		Queue.Add(Warm, 0.5);
		Queue.Add(Cold, 0.3);
		const FSpawnPacerFrameStats Stats = Queue.RunFrame(2.0, Model);   // 0.5 + 2.0 (cold) > 2.0
		TestEqual(TEXT("warm first: only the warm job"), Stats.Jobs, 1);
		TestEqual(TEXT("no cold job ran"), Stats.ColdJobs, 0);
	}
	{
		FSpawnCostModel Model;
		Model.Seed(Warm, 0.5, 0.0);
		FFakeQueue Queue;
		Queue.Add(Cold, 0.3);
		Queue.Add(Warm, 0.5);
		const FSpawnPacerFrameStats Stats = Queue.RunFrame(2.0, Model);   // cold runs first (0.3), then 0.3 + 0.5 fits
		TestEqual(TEXT("cold first: both"), Stats.Jobs, 2);
		TestEqual(TEXT("one cold job"), Stats.ColdJobs, 1);
		TestEqual(TEXT("not forced (prediction equals the budget)"), Stats.ForcedJobs, 0);
		TestFalse(TEXT("learned"), Model.IsCold(Cold));
	}
	{
		// The rule is explicit, not a side effect of the prediction: after a first job that measured 0 ms,
		// 0 + budget > budget is false, and the cold job must still wait for the next frame.
		FSpawnCostModel Model;
		Model.Seed(Warm, 0.0, 0.0);
		FFakeQueue Queue;
		Queue.Add(Warm, 0.0);
		Queue.Add(Cold, 5.0);
		const FSpawnPacerFrameStats Stats = Queue.RunFrame(2.0, Model);
		TestEqual(TEXT("0 ms first: only the warm job"), Stats.Jobs, 1);
		TestTrue(TEXT("0 ms first: stopped before the cold job"), Stats.bStoppedOnBudget);
	}
	return true;
}

// Nothing to do: nothing happens.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpawnPacerEmptyTest, "SpawnPacer.Policy.EmptyQueueDoesNothing", SpawnPacerTests::Flags)
bool FSpawnPacerEmptyTest::RunTest(const FString& Parameters)
{
	using namespace SpawnPacerTests;
	FSpawnCostModel Model;
	FFakeQueue Queue;
	const FSpawnPacerFrameStats Stats = Queue.RunFrame(2.0, Model);
	TestEqual(TEXT("jobs"), Stats.Jobs, 0);
	TestEqual(TEXT("spent"), Stats.SpentMs, 0.0, Tolerance);
	TestFalse(TEXT("not stopped"), Stats.bStoppedOnBudget);
	return true;
}

// Many tiny jobs fill the budget and stop there, never above it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpawnPacerTinyJobsTest, "SpawnPacer.Policy.ManyTinyJobsStopAtTheBudget", SpawnPacerTests::Flags)
bool FSpawnPacerTinyJobsTest::RunTest(const FString& Parameters)
{
	using namespace SpawnPacerTests;
	FSpawnCostModel Model;
	const FSpawnCostKey Tiny = Key(ESpawnPacerStage::Acquire);
	Model.Seed(Tiny, 0.002, 0.0);
	FFakeQueue Queue;
	Queue.Add(Tiny, 0.002, 1000);
	const FSpawnPacerFrameStats Stats = Queue.RunFrame(0.5, Model);   // 250 x 0.002 = 0.5 (249 with rounding)
	TestTrue(TEXT("249 or 250 jobs"), Stats.Jobs == 249 || Stats.Jobs == 250);
	TestTrue(TEXT("never above the budget"), Stats.SpentMs <= 0.5 + Tolerance);
	TestTrue(TEXT("stopped"), Stats.bStoppedOnBudget);
	return true;
}

// A job that costs more than predicted is reported as spent (an overrun is visible, never hidden), and the model learns.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpawnPacerOverrunTest, "SpawnPacer.Policy.OverrunIsReportedAndLearned", SpawnPacerTests::Flags)
bool FSpawnPacerOverrunTest::RunTest(const FString& Parameters)
{
	using namespace SpawnPacerTests;
	FSpawnCostModel Model;
	const FSpawnCostKey A = Key(ESpawnPacerStage::Spawn);
	Model.Seed(A, 0.5, 0.0);
	FFakeQueue Queue;
	Queue.Add(A, 1.5);   // predicted 0.5, measures 1.5
	Queue.Add(A, 0.5);
	const FSpawnPacerFrameStats Stats = Queue.RunFrame(2.0, Model);
	// After 1.5: deviation 0.25 x 1.0 = 0.25, mean 0.875 x 0.5 + 0.125 x 1.5 = 0.625, prediction 1.125; 1.5 + 1.125 > 2.
	TestEqual(TEXT("one job"), Stats.Jobs, 1);
	TestEqual(TEXT("spent is what it measured"), Stats.SpentMs, 1.5, Tolerance);
	TestEqual(TEXT("predicted is what it predicted"), Stats.PredictedMs, 0.5, Tolerance);
	const FSpawnCostModel::FEstimate* Estimate = Model.Find(A);
	if (!TestNotNull(TEXT("estimate"), Estimate))
	{
		return false;
	}
	TestEqual(TEXT("learned mean"), Estimate->MeanMs, 0.625, Tolerance);
	TestEqual(TEXT("learned prediction"), Model.Predict(A, 2.0), 1.125, Tolerance);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
