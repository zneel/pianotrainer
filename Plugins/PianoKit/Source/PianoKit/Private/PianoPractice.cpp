#include "PianoPractice.h"

#include "PianoScore.h"

#include "Algo/BinarySearch.h"
#include "Dom/JsonObject.h"
#include "Misc/DateTime.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace PianoPracticeImpl
{
	static constexpr int32 MaxJoin = 4;

	static int32 BeatOf(const TArray<double>& Beats, double Ms)
	{
		return FMath::Clamp(Algo::LowerBound(Beats, Ms - 1.0), 0, Beats.Num());
	}

	static const TCHAR* StageName(EPianoStage Stage)
	{
		switch (Stage)
		{
		case EPianoStage::Learned: return TEXT("learned");
		case EPianoStage::Mastered: return TEXT("mastered");
		case EPianoStage::Omniscient: return TEXT("omniscient");
		default: return TEXT("new");
		}
	}

	static EPianoStage StageFrom(const FString& Name)
	{
		return Name == TEXT("learned") ? EPianoStage::Learned : Name == TEXT("mastered") ? EPianoStage::Mastered
			: Name == TEXT("omniscient") ? EPianoStage::Omniscient : EPianoStage::New;
	}
}

FPianoPlan FPianoPlan::Make(const FPianoScore& Score, const FString& Piece, const FString& Title)
{
	using namespace PianoPracticeImpl;
	FPianoPlan Plan;
	Plan.Piece = Piece;
	Plan.Title = Title;
	Plan.Created = FPianoDiary::Today();
	const TArray<double> Beats = Score.GetBeatTimes();
	const int32 NumMeasures = Score.GetMeasures().Num();
	if (Beats.Num() == 0 || NumMeasures == 0)
	{
		return Plan;
	}

	// notes per second in each measure (both hands, struck notes)
	TArray<float> Density;
	Density.SetNumZeroed(NumMeasures);
	for (const FPianoScoreNote& N : Score.GetNotes())
	{
		if (N.IsStruck() && !N.bGrace)
		{
			Density[FMath::Clamp(Score.GetMeasureAt(N.OnMs), 0, NumMeasures - 1)] += 1.f;
		}
	}
	for (int32 M = 0; M < NumMeasures; ++M)
	{
		Density[M] /= float(FMath::Max(0.25, (Score.GetMeasureEndMs(M) - Score.GetMeasureStartMs(M)) / 1000.0));
	}
	TArray<float> Sorted = Density;
	Sorted.Sort();
	// dense: in the top quarter and clearly busier than the median (in an even piece no bar stands out)
	const float Dense = FMath::Max(Sorted[FMath::Min(Sorted.Num() - 1, Sorted.Num() * 3 / 4)], Sorted[Sorted.Num() / 2] * 1.25f + KINDA_SMALL_NUMBER);

	for (int32 M = 0; M < NumMeasures;)
	{
		const bool bAlone = Density[M] >= Dense || M + 1 >= NumMeasures || Density[M + 1] >= Dense;
		const int32 Count = bAlone ? 1 : 2;
		FPianoSection& S = Plan.Sections.AddDefaulted_GetRef();
		S.Difficulty = Count > 1 ? FMath::Max(Density[M], Density[M + 1]) : Density[M];
		S.FirstBeat = BeatOf(Beats, Score.GetMeasureStartMs(M));
		S.EndBeat = M + Count < NumMeasures ? BeatOf(Beats, Score.GetMeasureStartMs(M + Count)) : Beats.Num();
		if (S.EndBeat <= S.FirstBeat)
		{
			S.EndBeat = FMath::Min(Beats.Num(), S.FirstBeat + 1);
		}
		M += Count;
	}
	Plan.Relabel(Score);
	return Plan;
}

void FPianoPlan::Relabel(const FPianoScore& Score)
{
	for (int32 I = 0; I < Sections.Num(); ++I)
	{
		Sections[I].Label = Describe(Score, { I, I });
	}
}

int32 FPianoPlan::Hardest() const
{
	if (Sections.Num() < 3)
	{
		return INDEX_NONE;
	}
	TArray<float> Sorted;
	int32 Best = 0;
	for (int32 I = 0; I < Sections.Num(); ++I)
	{
		Sorted.Add(Sections[I].Difficulty);
		Best = Sections[I].Difficulty > Sections[Best].Difficulty ? I : Best;
	}
	Sorted.Sort();
	return Sections[Best].Difficulty > Sorted[Sorted.Num() / 2] * 1.5f ? Best : INDEX_NONE;
}

int32 FPianoPlan::DueReview(const FString& Today) const
{
	return Sections.IndexOfByPredicate([&Today](const FPianoSection& S)
	{
		return S.Stage == EPianoStage::Learned && !S.LearnedOn.IsEmpty() && S.LearnedOn < Today && S.LastPractised < Today;
	});
}

TArray<FPianoPlanSession> FPianoPlan::Schedule() const
{
	using namespace PianoPracticeImpl;
	TArray<FPianoPlanSession> Out;
	// Bernhard: start with the hardest bars, they hold the technique of the piece and take longest to cook
	const int32 Hard = Hardest();
	if (Hard != INDEX_NONE)
	{
		Out.Add({ Hard, Hard });
	}
	for (int32 I = 0; I < Sections.Num(); ++I)
	{
		if (I != Hard)
		{
			Out.Add({ I, I });
		}
		if (I > 0)
		{
			Out.Add({ FMath::Max(0, I - MaxJoin + 1), I });
		}
	}
	return Out;
}

bool FPianoPlan::Split(int32 Section)
{
	if (!Sections.IsValidIndex(Section) || Sections[Section].EndBeat - Sections[Section].FirstBeat < 2)
	{
		return false;
	}
	FPianoSection Second = Sections[Section];
	const int32 Mid = (Sections[Section].FirstBeat + Sections[Section].EndBeat) / 2;
	Sections[Section].EndBeat = Mid;
	Second.FirstBeat = Mid;
	Second.Stage = EPianoStage::New;
	Second.LearnedOn.Reset();
	Second.MasteredOn.Reset();
	Sections[Section].Stage = EPianoStage::New;
	Sections.Insert(Second, Section + 1);
	return true;
}

void FPianoPlan::GetTimes(const FPianoScore& Score, const FPianoPlanSession& Session, double& OutStartMs, double& OutEndMs, double& OutOverlapEndMs) const
{
	const TArray<double> Beats = Score.GetBeatTimes();
	auto TimeOfBeat = [&](int32 Beat) { return Beats.IsValidIndex(Beat) ? Beats[Beat] : Score.GetLengthMs(); };
	OutStartMs = Sections.IsValidIndex(Session.First) ? TimeOfBeat(Sections[Session.First].FirstBeat) : 0.0;
	const int32 End = Sections.IsValidIndex(Session.Last) ? Sections[Session.Last].EndBeat : Beats.Num();
	OutEndMs = TimeOfBeat(End);
	// with the first beat of what follows, so the sections join up
	OutOverlapEndMs = FMath::Min(Score.GetLengthMs(), TimeOfBeat(End + 1));
}

FString FPianoPlan::Describe(const FPianoScore& Score, const FPianoPlanSession& Session) const
{
	using namespace PianoPracticeImpl;
	if (!Sections.IsValidIndex(Session.First) || !Sections.IsValidIndex(Session.Last))
	{
		return FString();
	}
	const TArray<double> Beats = Score.GetBeatTimes();
	auto StartOfMeasure = [&](int32 Measure) { return BeatOf(Beats, Score.GetMeasureStartMs(Measure)); };
	auto MeasureOf = [&](int32 Beat) { return Score.GetMeasureAt(Beats.IsValidIndex(Beat) ? Beats[Beat] : Score.GetLengthMs() - 1.0); };
	// "3" for a whole bar, "3.2" when a split section starts or ends inside it
	const int32 First = Sections[Session.First].FirstBeat;
	const int32 Last = Sections[Session.Last].EndBeat - 1;
	const int32 M0 = MeasureOf(First), M1 = MeasureOf(Last);
	const bool bStartsAtBar = First == StartOfMeasure(M0);
	const bool bEndsAtBar = Last + 1 >= Beats.Num() || Last + 1 == StartOfMeasure(MeasureOf(Last + 1));
	const FString From = bStartsAtBar ? FString::FromInt(M0 + 1) : FString::Printf(TEXT("%d.%d"), M0 + 1, First - StartOfMeasure(M0) + 1);
	const FString To = bEndsAtBar ? FString::FromInt(M1 + 1) : FString::Printf(TEXT("%d.%d"), M1 + 1, Last - StartOfMeasure(M1) + 1);
	const FString Range = From == To ? FString::Printf(TEXT("m. %s"), *From) : FString::Printf(TEXT("m. %s–%s"), *From, *To);
	return Session.IsJoin() ? FString::Printf(TEXT("join %s"), *Range) : Range;
}

int32 FPianoPlan::Count(EPianoStage AtLeast) const
{
	return Sections.FilterByPredicate([AtLeast](const FPianoSection& S) { return uint8(S.Stage) >= uint8(AtLeast); }).Num();
}

FString FPianoDiary::GetPath()
{
	FString Path;
	if (FParse::Value(FCommandLine::Get(), TEXT("-diary="), Path)) // tests keep their own diary
	{
		return Path;
	}
	return FPaths::ProjectSavedDir() / TEXT("PianoDiary.json");
}

FString FPianoDiary::Today()
{
	return FDateTime::Now().ToString(TEXT("%Y-%m-%d"));
}

void FPianoDiary::Load()
{
	using namespace PianoPracticeImpl;
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(Text, *GetPath()) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		return;
	}
	Plans.Reset();
	for (const TSharedPtr<FJsonValue>& Value : Root->GetArrayField(TEXT("plans")))
	{
		const TSharedPtr<FJsonObject> O = Value->AsObject();
		FPianoPlan& P = Plans.AddDefaulted_GetRef();
		P.Piece = O->GetStringField(TEXT("piece"));
		P.Title = O->GetStringField(TEXT("title"));
		P.Created = O->GetStringField(TEXT("created"));
		P.NextSession = int32(O->GetNumberField(TEXT("next")));
		for (const TSharedPtr<FJsonValue>& SV : O->GetArrayField(TEXT("sections")))
		{
			const TSharedPtr<FJsonObject> S = SV->AsObject();
			FPianoSection& X = P.Sections.AddDefaulted_GetRef();
			X.FirstBeat = int32(S->GetNumberField(TEXT("first")));
			X.EndBeat = int32(S->GetNumberField(TEXT("end")));
			X.Stage = StageFrom(S->GetStringField(TEXT("stage")));
			X.LearnedOn = S->GetStringField(TEXT("learned"));
			X.MasteredOn = S->GetStringField(TEXT("mastered"));
			X.LastPractised = S->GetStringField(TEXT("last"));
			X.Sessions = int32(S->GetNumberField(TEXT("sessions")));
			X.Relearns = int32(S->GetNumberField(TEXT("relearns")));
			X.BestTempo = float(S->GetNumberField(TEXT("best")));
			double Difficulty = 0.0;
			S->TryGetNumberField(TEXT("difficulty"), Difficulty);
			X.Difficulty = float(Difficulty);
			X.Label = S->GetStringField(TEXT("label"));
		}
	}
	Entries.Reset();
	for (const TSharedPtr<FJsonValue>& Value : Root->GetArrayField(TEXT("entries")))
	{
		const TSharedPtr<FJsonObject> O = Value->AsObject();
		FPianoDiaryEntry& E = Entries.AddDefaulted_GetRef();
		E.Date = O->GetStringField(TEXT("date"));
		E.Piece = O->GetStringField(TEXT("piece"));
		E.Title = O->GetStringField(TEXT("title"));
		E.What = O->GetStringField(TEXT("what"));
		E.Minutes = float(O->GetNumberField(TEXT("minutes")));
		E.Passes = int32(O->GetNumberField(TEXT("passes")));
		E.Clean = int32(O->GetNumberField(TEXT("clean")));
		E.Tempo = float(O->GetNumberField(TEXT("tempo")));
		E.Result = O->GetStringField(TEXT("result"));
	}
}

void FPianoDiary::Save() const
{
	using namespace PianoPracticeImpl;
	TArray<TSharedPtr<FJsonValue>> PlanList;
	for (const FPianoPlan& P : Plans)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("piece"), P.Piece);
		O->SetStringField(TEXT("title"), P.Title);
		O->SetStringField(TEXT("created"), P.Created);
		O->SetNumberField(TEXT("next"), P.NextSession);
		TArray<TSharedPtr<FJsonValue>> Sections;
		for (const FPianoSection& X : P.Sections)
		{
			TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
			S->SetNumberField(TEXT("first"), X.FirstBeat);
			S->SetNumberField(TEXT("end"), X.EndBeat);
			S->SetStringField(TEXT("stage"), StageName(X.Stage));
			S->SetStringField(TEXT("learned"), X.LearnedOn);
			S->SetStringField(TEXT("mastered"), X.MasteredOn);
			S->SetStringField(TEXT("last"), X.LastPractised);
			S->SetNumberField(TEXT("sessions"), X.Sessions);
			S->SetNumberField(TEXT("relearns"), X.Relearns);
			S->SetNumberField(TEXT("best"), FMath::RoundToInt32(X.BestTempo * 100.f) / 100.0);
			S->SetNumberField(TEXT("difficulty"), FMath::RoundToInt32(X.Difficulty * 100.f) / 100.0);
			S->SetStringField(TEXT("label"), X.Label);
			Sections.Add(MakeShared<FJsonValueObject>(S));
		}
		O->SetArrayField(TEXT("sections"), Sections);
		PlanList.Add(MakeShared<FJsonValueObject>(O));
	}
	TArray<TSharedPtr<FJsonValue>> EntryList;
	for (const FPianoDiaryEntry& E : Entries)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("date"), E.Date);
		O->SetStringField(TEXT("piece"), E.Piece);
		O->SetStringField(TEXT("title"), E.Title);
		O->SetStringField(TEXT("what"), E.What);
		O->SetNumberField(TEXT("minutes"), FMath::RoundToInt32(E.Minutes * 10.f) / 10.0);
		O->SetNumberField(TEXT("passes"), E.Passes);
		O->SetNumberField(TEXT("clean"), E.Clean);
		O->SetNumberField(TEXT("tempo"), FMath::RoundToInt32(E.Tempo * 100.f) / 100.0);
		O->SetStringField(TEXT("result"), E.Result);
		EntryList.Add(MakeShared<FJsonValueObject>(O));
	}
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetArrayField(TEXT("plans"), PlanList);
	Root->SetArrayField(TEXT("entries"), EntryList);
	FString Text;
	FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Text));
	FFileHelper::SaveStringToFile(Text, *GetPath(), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

FPianoPlan* FPianoDiary::FindPlan(const FString& Piece)
{
	return Plans.FindByPredicate([&Piece](const FPianoPlan& P) { return P.Piece == Piece; });
}

FPianoPlan& FPianoDiary::AddPlan(FPianoPlan&& Plan)
{
	Plans.RemoveAll([&Plan](const FPianoPlan& P) { return P.Piece == Plan.Piece; });
	return Plans.Add_GetRef(MoveTemp(Plan));
}

void FPianoDiary::Log(const FPianoDiaryEntry& Entry)
{
	Entries.Add(Entry);
	if (Entries.Num() > 2000)
	{
		Entries.RemoveAt(0, Entries.Num() - 2000);
	}
}

int32 FPianoDiary::DaysThisWeek() const
{
	TSet<FString> Days;
	const FDateTime Now = FDateTime::Now();
	for (int32 Back = 0; Back < 7; ++Back)
	{
		const FString Day = (Now - FTimespan::FromDays(Back)).ToString(TEXT("%Y-%m-%d"));
		if (Entries.ContainsByPredicate([&Day](const FPianoDiaryEntry& E) { return E.Date == Day; }))
		{
			Days.Add(Day);
		}
	}
	return Days.Num();
}

float FPianoDiary::MinutesOn(const FString& Date) const
{
	float Minutes = 0.f;
	for (const FPianoDiaryEntry& E : Entries)
	{
		Minutes += E.Date == Date ? E.Minutes : 0.f;
	}
	return Minutes;
}
