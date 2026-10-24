#include "PianoPractice.h"

#include "PianoPhrases.h"
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
	FPianoPlan Plan;
	Plan.Piece = Piece;
	Plan.Title = Title;
	Plan.Created = FPianoDiary::Today();
	const FPianoPhrases Phrases = FPianoPhrases::Analyse(Score);
	if (Phrases.NumBeats == 0)
	{
		return Plan;
	}
	const TArray<int32> Bounds = Phrases.Segment();
	for (int32 I = 0; I + 1 < Bounds.Num(); ++I)
	{
		FPianoSection& S = Plan.Sections.AddDefaulted_GetRef();
		S.FirstBeat = Bounds[I];
		S.EndBeat = Bounds[I + 1];
	}
	Plan.Refresh(Score);
	return Plan;
}

void FPianoPlan::Refresh(const FPianoScore& Score)
{
	const FPianoPhrases Phrases = FPianoPhrases::Analyse(Score);
	const TArray<FPianoScoreNote>& Notes = Score.GetNotes();
	// the notes of each section, beat-relative (a copy has the same notes at the same places)
	TArray<uint32> Signature;
	for (FPianoSection& S : Sections)
	{
		const int32 First = FMath::Clamp(S.FirstBeat, 0, Phrases.NumBeats), End = FMath::Clamp(S.EndBeat, First, Phrases.NumBeats);
		const double StartMs = Phrases.BeatMs.IsValidIndex(First) ? Phrases.BeatMs[First] : 0.0;
		const double EndMs = Phrases.BeatMs.IsValidIndex(End) ? Phrases.BeatMs[End] : Score.GetLengthMs();
		uint32 Hash = GetTypeHash(End - First);
		bool bAny = false;
		for (int32 N = Algo::LowerBoundBy(Notes, StartMs - 1.0, &FPianoScoreNote::OnMs); N < Notes.Num() && Notes[N].OnMs < EndMs - 1.0; ++N)
		{
			if (Notes[N].IsStruck() && !Notes[N].bGrace)
			{
				const int32 Beat = Phrases.BeatAt(Notes[N].OnMs);
				const double Frac = (Notes[N].OnMs - Phrases.BeatMs[Beat]) / FMath::Max(1.0, Phrases.BeatMs[Beat + 1] - Phrases.BeatMs[Beat]);
				Hash = HashCombine(Hash, HashCombine(GetTypeHash(FMath::RoundToInt32((Beat - First + Frac) * 48.0)), GetTypeHash(Notes[N].Pitch)));
				bAny = true;
			}
		}
		Signature.Add(bAny ? Hash : 0u);
		S.Difficulty = float(Phrases.NumBeats > 0 ? Phrases.OnsetsIn(First, End) : 0) / float(FMath::Max(0.25, (EndMs - StartMs) / 1000.0));
	}
	for (int32 I = 0; I < Sections.Num(); ++I)
	{
		Sections[I].RepeatOf = INDEX_NONE;
		for (int32 J = 0; J < I && Signature[I] != 0u; ++J)
		{
			if (Signature[J] == Signature[I] && Sections[J].RepeatOf == INDEX_NONE)
			{
				Sections[I].RepeatOf = J;
				break;
			}
		}
	}
	SyncRepeats();
	Relabel(Score);
}

void FPianoPlan::SyncRepeats()
{
	for (FPianoSection& S : Sections)
	{
		if (Sections.IsValidIndex(S.RepeatOf))
		{
			const FPianoSection& Original = Sections[S.RepeatOf];
			S.Stage = Original.Stage;
			S.LearnedOn = Original.LearnedOn;
			S.MasteredOn = Original.MasteredOn;
			S.Relearns = Original.Relearns;
			S.BestTempo = FMath::Max(S.BestTempo, Original.BestTempo);
		}
	}
}

namespace PianoPracticeImpl
{
	static void ResetProgress(FPianoSection& S)
	{
		S.Stage = EPianoStage::New;
		S.LearnedOn.Reset();
		S.MasteredOn.Reset();
		S.Sessions = 0;
		S.Relearns = 0;
		S.BestTempo = 0.f;
		S.RepeatOf = INDEX_NONE;
	}
}

bool FPianoPlan::MoveEdge(int32 Left, int32 Beat)
{
	using namespace PianoPracticeImpl;
	if (!Sections.IsValidIndex(Left) || !Sections.IsValidIndex(Left + 1))
	{
		return false;
	}
	FPianoSection& A = Sections[Left];
	FPianoSection& B = Sections[Left + 1];
	if (Beat <= A.FirstBeat || Beat >= B.EndBeat)
	{
		return Merge(Left);
	}
	if (Beat == A.EndBeat)
	{
		return false;
	}
	A.EndBeat = B.FirstBeat = Beat;
	ResetProgress(A);
	ResetProgress(B);
	return true;
}

bool FPianoPlan::SplitAt(int32 Beat)
{
	using namespace PianoPracticeImpl;
	const int32 Index = Sections.IndexOfByPredicate([Beat](const FPianoSection& S) { return S.FirstBeat < Beat && Beat < S.EndBeat; });
	if (Index == INDEX_NONE)
	{
		return false;
	}
	FPianoSection Second = Sections[Index];
	Sections[Index].EndBeat = Beat;
	Second.FirstBeat = Beat;
	ResetProgress(Sections[Index]);
	ResetProgress(Second);
	Sections.Insert(Second, Index + 1);
	return true;
}

bool FPianoPlan::Merge(int32 Left)
{
	using namespace PianoPracticeImpl;
	if (!Sections.IsValidIndex(Left) || !Sections.IsValidIndex(Left + 1))
	{
		return false;
	}
	Sections[Left].EndBeat = Sections[Left + 1].EndBeat;
	ResetProgress(Sections[Left]);
	Sections.RemoveAt(Left + 1);
	return true;
}

void FPianoPlan::AdoptProgress(const FPianoPlan& Old)
{
	for (FPianoSection& S : Sections)
	{
		if (const FPianoSection* Same = Old.Sections.FindByPredicate([&S](const FPianoSection& X) { return X.FirstBeat == S.FirstBeat && X.EndBeat == S.EndBeat; }))
		{
			S.Stage = Same->Stage;
			S.LearnedOn = Same->LearnedOn;
			S.MasteredOn = Same->MasteredOn;
			S.LastPractised = Same->LastPractised;
			S.Sessions = Same->Sessions;
			S.Relearns = Same->Relearns;
			S.BestTempo = Same->BestTempo;
		}
	}
	NextSession = 0;
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
	// among the sections learned on their own (not the copies)
	TArray<float> Sorted;
	int32 Best = INDEX_NONE;
	for (int32 I = 0; I < Sections.Num(); ++I)
	{
		if (Sections[I].RepeatOf == INDEX_NONE)
		{
			Sorted.Add(Sections[I].Difficulty);
			Best = Best == INDEX_NONE || Sections[I].Difficulty > Sections[Best].Difficulty ? I : Best;
		}
	}
	if (Sorted.Num() < 3)
	{
		return INDEX_NONE;
	}
	Sorted.Sort();
	return Sections[Best].Difficulty > Sorted[Sorted.Num() / 2] * 1.5f ? Best : INDEX_NONE;
}

int32 FPianoPlan::DueReview(const FString& Today) const
{
	return Sections.IndexOfByPredicate([&Today](const FPianoSection& S)
	{
		return S.RepeatOf == INDEX_NONE && S.Stage == EPianoStage::Learned && !S.LearnedOn.IsEmpty() && S.LearnedOn < Today && S.LastPractised < Today;
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
		if (I != Hard && Sections[I].RepeatOf == INDEX_NONE) // a copy is learned with its original
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

bool FPianoPlan::Split(int32 Section, const FPianoScore& Score)
{
	if (!Sections.IsValidIndex(Section) || Sections[Section].EndBeat - Sections[Section].FirstBeat < 2)
	{
		return false;
	}
	const FPianoSection& S = Sections[Section];
	const int32 At = FPianoPhrases::Analyse(Score).BestSplit(S.FirstBeat, S.EndBeat);
	return SplitAt(At != INDEX_NONE ? At : (S.FirstBeat + S.EndBeat) / 2);
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
	FString From = bStartsAtBar ? FString::FromInt(M0 + 1) : FString::Printf(TEXT("%d.%d"), M0 + 1, First - StartOfMeasure(M0) + 1);
	FString To = bEndsAtBar ? FString::FromInt(M1 + 1) : FString::Printf(TEXT("%d.%d"), M1 + 1, Last - StartOfMeasure(M1) + 1);
	if (M0 == M1 && !(bStartsAtBar && bEndsAtBar))
	{
		// inside one bar: "42.4", "42.1–42.3"
		From = FString::Printf(TEXT("%d.%d"), M0 + 1, First - StartOfMeasure(M0) + 1);
		To = FString::Printf(TEXT("%d.%d"), M1 + 1, Last - StartOfMeasure(M1) + 1);
	}
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
			double RepeatOf = -1.0;
			S->TryGetNumberField(TEXT("repeatOf"), RepeatOf);
			X.RepeatOf = int32(RepeatOf);
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
			S->SetNumberField(TEXT("repeatOf"), X.RepeatOf);
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
