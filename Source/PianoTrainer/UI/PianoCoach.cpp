#include "PianoCoach.h"

#include "PianoLibrary.h"
#include "PianoPlayer.h"
#include "PianoScore.h"

#define LOCTEXT_NAMESPACE "PianoCoach"

namespace PianoCoachImpl
{
	static constexpr int32 NotesPerUnit = 4;
	static constexpr int32 MaxUnits = 8;     // Bernhard: more than ~8-10 units takes all day
	static constexpr int32 GroupCleanNeeded = 2;
}

void FPianoCoach::Init(UPianoPlayer* InPlayer)
{
	Player = InPlayer;
	Diary.Load();
}

FString FPianoCoach::PieceKey() const
{
	if (!Player || !Player->GetScore().IsValid())
	{
		return FString();
	}
	const FString Path = Player->GetScore()->GetSourcePath();
	const FString File = FPianoLibrary::ToScoresRelative(Path);
	return File.IsEmpty() ? FPaths::ConvertRelativePathToFull(Path) : File;
}

FPianoPlan* FPianoCoach::GetPlan()
{
	const FString Key = PieceKey();
	return Key.IsEmpty() ? nullptr : Diary.FindPlan(Key);
}

FPianoPlan* FPianoCoach::SessionPlan()
{
	return Piece.IsEmpty() ? nullptr : Diary.FindPlan(Piece);
}

FPianoPlan* FPianoCoach::MakePlan(const FString& Title)
{
	if (!Player || !Player->GetScore().IsValid())
	{
		return nullptr;
	}
	FPianoPlan& Plan = Diary.AddPlan(FPianoPlan::Make(*Player->GetScore(), PieceKey(), Title));
	Diary.Save();
	return &Plan;
}

void FPianoCoach::Start(int32 InScheduleIndex)
{
	FPianoPlan* Plan = GetPlan();
	if (!Plan)
	{
		return;
	}
	const TArray<FPianoPlanSession> Schedule = Plan->Schedule();
	if (!Schedule.IsValidIndex(InScheduleIndex))
	{
		return;
	}
	ScheduleIndex = InScheduleIndex;
	Begin(Schedule[InScheduleIndex], true);
}

void FPianoCoach::StartSection(int32 Section)
{
	FPianoPlan* Plan = GetPlan();
	if (!Plan || !Plan->Sections.IsValidIndex(Section))
	{
		return;
	}
	ScheduleIndex = INDEX_NONE;
	Begin({ Section, Section }, false);
}

void FPianoCoach::Begin(const FPianoPlanSession& InSession, bool bScheduled)
{
	const TSharedPtr<FPianoScore> Score = Player ? Player->GetScore() : nullptr;
	if (!GetPlan() || !Score.IsValid())
	{
		return;
	}
	if (Phase != ECoachPhase::Idle)
	{
		Stop();
	}
	FPianoPlan* Plan = GetPlan();
	Piece = Plan->Piece;
	bLogged = false;
	Session = InSession;
	Plan->GetTimes(*Score, Session, StartMs, EndMs, OverlapEndMs);
	Label = Plan->Describe(*Score, Session);
	Passes = Clean = WrongBeforeRight = 0;
	Target = Trial;
	bGotRight = bFatigue = false;
	FatigueMistakes = 0;
	Seconds = 0.f;
	Dots.Reset();

	// a section learned on an earlier day: the first time through tells whether it is mastered
	const FPianoSection& First = Plan->Sections[Session.First];
	// (the first time through on that day: not after practising it today)
	bColdCheck = !Session.IsJoin() && First.Stage == EPianoStage::Learned && First.LearnedOn < FPianoDiary::Today() && First.LastPractised < FPianoDiary::Today();
	// mastered, then a month away: relearn it from scratch (each time it comes back faster)
	const FString MonthAgo = (FDateTime::Now() - FTimespan::FromDays(RelearnAfterDays)).ToString(TEXT("%Y-%m-%d"));
	bRelearn = !Session.IsJoin() && First.Stage >= EPianoStage::Mastered && !First.LastPractised.IsEmpty() && First.LastPractised <= MonthAgo;

	Phase = ECoachPhase::Trial;
	Advice = bColdCheck
		? LOCTEXT("Cold", "You learned this before. Play it once, straight away: if it is perfect, it is mastered.")
		: !Session.IsJoin() && First.Stage >= EPianoStage::Learned && First.LastPractised == FPianoDiary::Today()
		? LOCTEXT("Again", "Already practised today. It settles overnight: tomorrow will tell more than more repetitions now. Another section is a better use of the time.")
		: bRelearn
		? LOCTEXT("Relearn", "A month away: relearn it from scratch. It comes back faster each time; after three times it is never forgotten.")
		: Session.IsJoin()
		? LOCTEXT("Join", "Joining sections: play them as one, with the first beat of what follows.")
		: LOCTEXT("TrialStart", "Play it seven times. If it is learned by then, practise it; if not, it is too big — cut it in half.");

	Player->SetListenMode(false);
	Player->SetWaitMode(false); // at tempo: the aim is the final speed (drills stay as chosen: rhythms are part of it)
	CountInBefore = Player->GetCountInBars();
	Player->SetCountInBars(1);
	LoopWhole();
	Player->Seek(StartMs);
	Player->Play();
	SeenPass = Player->GetPassSerial();
}

void FPianoCoach::LoopWhole()
{
	Player->SetLoop(StartMs, OverlapEndMs > EndMs ? OverlapEndMs : EndMs);
}

void FPianoCoach::SetTempo(float Tempo)
{
	if (Player)
	{
		Player->SetTempoScale(Tempo);
	}
}

void FPianoCoach::Stop()
{
	if (Phase != ECoachPhase::Idle)
	{
		Finish(Phase == ECoachPhase::TooBig ? TEXT("too big") : TEXT("stopped"));
	}
}

void FPianoCoach::Split()
{
	FPianoPlan* Plan = SessionPlan();
	if (!Plan || Session.IsJoin() || Piece != PieceKey())
	{
		return;
	}
	const int32 Section = Session.First;
	Finish(TEXT("too big: split"));
	if (Plan->Split(Section))
	{
		Plan->Relabel(*Player->GetScore());
		Diary.Save();
	}
	StartSection(Section);
}

void FPianoCoach::Finish(const TCHAR* Result)
{
	FPianoPlan* Plan = SessionPlan();
	if (Plan && Player && !bLogged)
	{
		bLogged = true;
		const FString Today = FPianoDiary::Today();
		for (int32 I = Session.First; I <= Session.Last && Plan->Sections.IsValidIndex(I); ++I)
		{
			Plan->Sections[I].LastPractised = Today;
		}
		FPianoDiaryEntry Entry;
		Entry.Date = Today;
		Entry.Piece = Plan->Piece;
		Entry.Title = Plan->Title;
		Entry.What = Label;
		Entry.Minutes = Seconds / 60.f;
		Entry.Passes = Passes;
		Entry.Clean = Clean;
		Entry.Tempo = Player->GetTempoScale();
		Entry.Result = Result;
		if (Passes > 0 || Seconds > 30.f)
		{
			Diary.Log(Entry);
		}
		Diary.Save();
		Player->Pause();
		Player->ClearLoop();
		Player->SetCountInBars(CountInBefore);
	}
	Phase = ECoachPhase::Idle;
	Units.Reset();
}

void FPianoCoach::StartGroups()
{
	if (!Player || Phase == ECoachPhase::Idle || Phase == ECoachPhase::Groups)
	{
		return;
	}
	using namespace PianoCoachImpl;
	// units of four onsets of the practised hands, each overlapping the next by its first note
	Units.Reset();
	const uint8 Hands = uint8(Player->GetHands());
	double LastOnset = -1.0;
	int32 Count = 0;
	for (const FPianoSessionNote& N : Player->GetPlayNotes())
	{
		if (N.OnMs < StartMs - 0.5 || N.OnMs >= EndMs - 0.5 || N.bTied || N.bGrace || (uint8(N.Hand) & Hands) == 0 || N.OnMs - LastOnset < 25.0)
		{
			continue;
		}
		if (Count % NotesPerUnit == 0)
		{
			Units.Add(N.OnMs);
		}
		LastOnset = N.OnMs;
		++Count;
	}
	if (Units.Num() < 2)
	{
		Advice = LOCTEXT("GroupsSmall", "This section is already small enough for note-groups.");
		return;
	}
	Units.SetNum(FMath::Min(Units.Num(), MaxUnits));
	PhaseBeforeGroups = Phase;
	Phase = ECoachPhase::Groups;
	GroupLength = 1;
	GroupStart = 0;
	GroupClean = 0;
	NextGroup();
}

void FPianoCoach::NextGroup()
{
	using namespace PianoCoachImpl;
	// a group ends on the first note of the unit after it (the overlap), or at the section's end
	const int32 EndUnit = GroupStart + GroupLength;
	const double From = Units[GroupStart] - 30.0;
	const double To = Units.IsValidIndex(EndUnit) ? Units[EndUnit] + 60.0 : (OverlapEndMs > EndMs ? OverlapEndMs : EndMs);
	Player->SetLoop(FMath::Max(0.0, From), To);
	Player->Seek(FMath::Max(0.0, From));
	if (!Player->IsPlaying())
	{
		Player->Play();
	}
	SeenPass = Player->GetPassSerial();
	FString Group;
	for (int32 U = GroupStart; U < EndUnit; ++U)
	{
		Group += FString::FromInt(U + 1);
	}
	Advice = FText::Format(LOCTEXT("Group", "Repeated note-groups: group {0} of the {1} units, twice right, then the next."), FText::FromString(Group), Units.Num());
}

void FPianoCoach::OnPass(bool bCleanPass)
{
	FPianoPlan* Plan = SessionPlan();
	if (!Plan)
	{
		return;
	}
	Dots.Add(bCleanPass ? 1 : 0);
	if (Dots.Num() > 24)
	{
		Dots.RemoveAt(0);
	}

	if (Phase == ECoachPhase::Groups)
	{
		using namespace PianoCoachImpl;
		GroupClean = bCleanPass ? GroupClean + 1 : 0;
		if (GroupClean < GroupCleanNeeded)
		{
			return;
		}
		GroupClean = 0;
		if (GroupStart + GroupLength < Units.Num())
		{
			++GroupStart;
		}
		else if (2 * (GroupLength + 1) <= Units.Num())
		{
			// longer groups, until the first and the last would overlap: then the whole section
			++GroupLength;
			GroupStart = 0;
		}
		else
		{
			// every group done: back to the whole section
			Phase = PhaseBeforeGroups;
			LoopWhole();
			Player->Seek(StartMs);
			SeenPass = Player->GetPassSerial();
			Advice = LOCTEXT("GroupsDone", "All the note-groups are done. Now the whole section again.");
			return;
		}
		NextGroup();
		return;
	}

	++Passes;
	const FPianoPass& Pass = Player->GetLastPass();
	if (bCleanPass)
	{
		++Clean;
		FPianoSection& First = Plan->Sections[Session.First];
		if (!Session.IsJoin())
		{
			First.BestTempo = FMath::Max(First.BestTempo, Pass.Tempo);
		}
	}

	if (bColdCheck && Passes == 1)
	{
		FPianoSection& S = Plan->Sections[Session.First];
		if (bCleanPass)
		{
			S.Stage = EPianoStage::Mastered;
			S.MasteredOn = FPianoDiary::Today();
			Advice = LOCTEXT("Mastered", "Perfect straight away: mastered. From now on just play it through two or three times a week.");
			Phase = ECoachPhase::Done;
			Finish(TEXT("mastered"));
			Phase = ECoachPhase::Done;
			return;
		}
		Advice = LOCTEXT("NotYet", "Not yet mastered: practise it again from scratch, it will go faster than the first time.");
		bColdCheck = false;
	}

	if (!bGotRight)
	{
		if (bCleanPass)
		{
			bGotRight = true;
			Target = FMath::Max(Trial, WrongBeforeRight);
			Phase = ECoachPhase::Ingrain;
			Advice = FText::Format(LOCTEXT("GotRight", "Right! Practice starts now: play it right {0} times (at least as often as it went wrong)."), Target);
		}
		else
		{
			++WrongBeforeRight;
			if (Passes >= Trial)
			{
				Phase = ECoachPhase::TooBig;
				Advice = Session.IsJoin()
					? LOCTEXT("JoinTooBig", "Seven times and not right yet: practise the sections on their own first, then join them.")
					: LOCTEXT("TooBig", "Seven times and not learned: this chunk is too big. Cut it in half.");
				Player->Pause();
			}
			else
			{
				Advice = FText::Format(LOCTEXT("Trial", "Time {0} of seven. Find the notes, the fingering, the movement."), Passes);
			}
		}
		return;
	}

	if (bCleanPass)
	{
		if (Clean >= Target)
		{
			// stop while it is perfect: the last repetition is the one that stays
			FPianoSection& First = Plan->Sections[Session.First];
			if (!Session.IsJoin() && First.Stage == EPianoStage::New)
			{
				First.Stage = EPianoStage::Learned;
				First.LearnedOn = FPianoDiary::Today();
			}
			else if (bRelearn && ++First.Relearns >= RelearnsForOmniscience)
			{
				First.Stage = EPianoStage::Omniscient;
			}
			for (int32 I = Session.First; I <= Session.Last; ++I)
			{
				Plan->Sections[I].Sessions += 1;
			}
			if (ScheduleIndex != INDEX_NONE && Plan->NextSession <= ScheduleIndex)
			{
				Plan->NextSession = ScheduleIndex + 1;
			}
			Advice = Session.IsJoin()
				? LOCTEXT("JoinDone", "Joined, and the last time was perfect. Stop here.")
				: LOCTEXT("Learned", "Learned — and the last repetition was perfect. Stop here; tomorrow, see if it is still there.");
			Finish(TEXT("learned"));
			Phase = ECoachPhase::Done;
			return;
		}
		if (FatigueMistakes >= 2)
		{
			// stop at the best rendition: the last repetitions are the ones that stay
			Advice = LOCTEXT("StopTired", "Stopped on a clean repetition: mistakes were coming back, and tired repetitions get ingrained. Same section tomorrow.");
			Finish(TEXT("stopped clean: tired"));
			Phase = ECoachPhase::Done;
			return;
		}
		Advice = bFatigue
			? LOCTEXT("Recovered", "Good. Keep it clean.")
			: FText::Format(LOCTEXT("Ingrain", "{0} of {1} right."), Clean, Target);
		bFatigue = false;
	}
	else
	{
		// mistakes coming back after it was right: fatigue
		bFatigue = true;
		++FatigueMistakes;
		Advice = FatigueMistakes >= 2
			? LOCTEXT("Tired", "Mistakes are coming back: play it once more perfectly and stop there for today.")
			: LOCTEXT("Fatigue", "A mistake after it was right: concentrate. If mistakes keep coming, play it once more perfectly and stop for today.");
	}
}

void FPianoCoach::Tick(float DeltaSeconds)
{
	if (!Player || Phase == ECoachPhase::Idle)
	{
		return;
	}
	if (Piece != PieceKey())
	{
		Finish(TEXT("stopped")); // another piece was opened: the session ends here
		return;
	}
	if (Phase == ECoachPhase::Done || Phase == ECoachPhase::TooBig)
	{
		return;
	}
	if (Player->IsPlaying() && Phase != ECoachPhase::Groups)
	{
		Seconds += DeltaSeconds; // a group set is finished in one go, whatever it takes
	}
	if (Player->GetPassSerial() != SeenPass)
	{
		SeenPass = Player->GetPassSerial();
		OnPass(Player->GetLastPass().IsClean());
	}
	if ((Phase == ECoachPhase::Trial || Phase == ECoachPhase::Ingrain) && Seconds >= SessionMinutes * 60.f)
	{
		Phase = ECoachPhase::TooBig;
		Advice = LOCTEXT("Timer", "Twenty minutes, and not there yet: more time would be wasted. Cut it in half and try again.");
		Player->Pause();
	}
}

#undef LOCTEXT_NAMESPACE
