#include "PianoCoach.h"

#include "PianoLibrary.h"
#include "PianoPlayer.h"
#include "PianoScore.h"

#include "Misc/ConfigCacheIni.h"

#define LOCTEXT_NAMESPACE "PianoCoach"

namespace PianoCoachImpl
{
	static constexpr int32 NotesPerUnit = 4;
	static constexpr int32 MaxUnits = 8;     // Bernhard: more than ~8-10 units takes all day
	static constexpr int32 GroupCleanNeeded = 2;
	static const TCHAR* ConfigSection = TEXT("PianoTrainer.Coach");

	static int32 HandIndex(EPianoHand Hand) { return Hand == EPianoHand::Left ? 1 : 0; }
	static FText HandName(EPianoHand Hand) { return Hand == EPianoHand::Left ? LOCTEXT("LeftHand", "Left") : LOCTEXT("RightHand", "Right"); }
}

FPianoCoach::~FPianoCoach()
{
	if (Player)
	{
		Player->OnPassJudged.Remove(PassHandle);
	}
}

void FPianoCoach::Init(UPianoPlayer* InPlayer)
{
	Player = InPlayer;
	// react to a pass at once: a change of hands must come before the next pass's first notes
	PassHandle = Player->OnPassJudged.AddRaw(this, &FPianoCoach::HandlePass);
	Diary.Load();
	int32 Mode = int32(ECoachHands::Auto);
	GConfig->GetInt(PianoCoachImpl::ConfigSection, TEXT("Hands"), Mode, GGameUserSettingsIni);
	HandsMode = ECoachHands(FMath::Clamp(Mode, 0, 2));
}

void FPianoCoach::SetHandsMode(ECoachHands Mode, bool bSave)
{
	HandsMode = Mode;
	if (bSave)
	{
		GConfig->SetInt(PianoCoachImpl::ConfigSection, TEXT("Hands"), int32(Mode), GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

int32 FPianoCoach::CountNotes(EPianoHand InHand) const
{
	int32 Count = 0;
	for (const FPianoSessionNote& N : Player->GetPlayNotes())
	{
		Count += N.OnMs >= StartMs - 0.5 && N.OnMs < EndMs - 0.5 && !N.bTied && !N.bGrace && (uint8(N.Hand) & uint8(InHand)) != 0 ? 1 : 0;
	}
	return Count;
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

	// hands separately or together: only for a single section with notes in both hands, practised with both
	SessionHands = Hand = Player->GetHands();
	bHandsDone = false;
	HandTurns.Reset();
	bBothHands = !Session.IsJoin() && SessionHands == EPianoHand::Both && CountNotes(EPianoHand::Right) > 0 && CountNotes(EPianoHand::Left) > 0;

	Player->SetListenMode(false);
	Player->SetWaitMode(false); // at tempo: the aim is the final speed (drills stay as chosen: rhythms are part of it)
	CountInBefore = Player->GetCountInBars();
	Player->SetCountInBars(1);
	if (bBothHands && HandsMode == ECoachHands::Separate && !bColdCheck)
	{
		StartHands(LOCTEXT("SeparateFirst", "Hands separately first, then together."));
		return;
	}
	LoopWhole();
	Player->Seek(StartMs);
	Player->Play();
	SeenPass = Player->GetPassSerial();
}

void FPianoCoach::PlayHands(EPianoHand Hands)
{
	Hand = Hands;
	Player->SetHands(Hands);
	LoopWhole();
	Player->Seek(StartMs);
	Player->CountInAgain(StartMs); // a bar of clicks to change hands
	SeenPass = Player->GetPassSerial();
	TurnClean = TurnPasses = 0;
	TurnSeconds = 0.f;
}

void FPianoCoach::StartHands(const FText& Why)
{
	using namespace PianoCoachImpl;
	bHandsDone = true;
	Phase = ECoachPhase::Hands;
	TurnTarget = FirstTurn;
	HandPasses[0] = HandPasses[1] = 0;
	HandGotRight[0] = HandGotRight[1] = false;
	HandSecure[0] = HandSecure[1] = false;
	HandTurns.Reset();
	// the busier hand first
	FirstHand = CountNotes(EPianoHand::Left) > CountNotes(EPianoHand::Right) ? EPianoHand::Left : EPianoHand::Right;
	PlayHands(FirstHand);
	Advice = FText::Format(LOCTEXT("HandsStart", "{0} {1} hand alone: right {2} times in a row, then the other hand. Half a minute at most on a hand."),
		Why, HandName(FirstHand), TurnTarget);
}

void FPianoCoach::OnHandPass(bool bCleanPass)
{
	using namespace PianoCoachImpl;
	const int32 H = HandIndex(Hand);
	++HandPasses[H];
	++TurnPasses;
	TurnClean = bCleanPass ? TurnClean + 1 : 0;
	HandGotRight[H] |= bCleanPass;
	if (!HandGotRight[H] && HandPasses[H] >= Trial)
	{
		Phase = ECoachPhase::TooBig;
		Advice = FText::Format(LOCTEXT("HandTooBig", "The {0} hand alone, seven times and not right: this chunk is too big. Cut it in half."), HandName(Hand).ToLower());
		Player->Pause();
		return;
	}

	// the turn ends at its aim, or after half a minute on this hand (when the section is too long for the aim in that
	// time, one clean pass to finish the turn will do)
	const bool bAim = TurnClean >= TurnTarget;
	const bool bTime = TurnSeconds >= HandTurnSeconds;
	if (!bAim && !bTime)
	{
		Advice = bCleanPass
			? FText::Format(LOCTEXT("HandRight", "{0} hand: {1} of {2} right in a row."), HandName(Hand), TurnClean, TurnTarget)
			: FText::Format(LOCTEXT("HandAgain", "{0} hand: again, until it is right {1} times in a row."), HandName(Hand), TurnTarget);
		return;
	}
	const float PassSeconds = TurnSeconds / float(FMath::Max(1, TurnPasses));
	HandSecure[H] = bAim || (TurnClean >= 1 && PassSeconds * TurnTarget > HandTurnSeconds);
	HandTurns += FString::Printf(TEXT("%s%s%d%s"), HandTurns.IsEmpty() ? TEXT("") : TEXT(" "), H == 0 ? TEXT("R") : TEXT("L"), TurnClean, HandSecure[H] ? TEXT("") : TEXT("~"));

	const EPianoHand Other = Hand == EPianoHand::Right ? EPianoHand::Left : EPianoHand::Right;
	if (Hand != FirstHand)
	{
		// a round is complete: both right at this aim -> a shorter aim; once each -> together
		if (HandSecure[0] && HandSecure[1])
		{
			if (TurnTarget == 1)
			{
				Phase = ECoachPhase::Trial;
				Passes = Clean = WrongBeforeRight = 0;
				Target = Trial;
				bGotRight = bFatigue = false;
				FatigueMistakes = 0;
				PlayHands(EPianoHand::Both);
				Advice = LOCTEXT("HandsJoin", "Both hands are right on their own: now together. Seven times to get it right.");
				return;
			}
			--TurnTarget;
		}
		HandSecure[0] = HandSecure[1] = false;
	}
	PlayHands(Other);
	Advice = bAim
		? FText::Format(LOCTEXT("HandNext", "Now the {0} hand alone: right {1} times in a row."), HandName(Other).ToLower(), TurnTarget)
		: FText::Format(LOCTEXT("HandRest", "Half a minute on that hand: now the {0} hand, so neither tires. Right {1} times in a row."), HandName(Other).ToLower(), TurnTarget);
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
		Player->SetHands(SessionHands);
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
	Dots.Add(int8((bCleanPass ? 1 : 0) | (uint8(Player->GetHands()) << 1)));
	if (Dots.Num() > 18)
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
	if (Phase == ECoachPhase::Hands)
	{
		OnHandPass(bCleanPass);
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
		if (bBothHands && HandsMode != ECoachHands::Together)
		{
			// Bernhard: only if hands together fails the next day do you go back to the hands-separate steps
			StartHands(LOCTEXT("NotYetHands", "Not yet mastered: the hands separately again, then together - it goes faster than the first time."));
			return;
		}
	}
	else if (Passes == 1 && !bCleanPass && bBothHands && !bHandsDone && HandsMode == ECoachHands::Auto)
	{
		// together first: not right -> each hand on its own (mistakes made together go into hand memory)
		StartHands(LOCTEXT("AutoHands", "Not right together: each hand on its own first, then together."));
		return;
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

void FPianoCoach::HandlePass()
{
	if (!Player || Phase == ECoachPhase::Idle || Phase == ECoachPhase::Done || Phase == ECoachPhase::TooBig || Piece != PieceKey()
		|| Player->GetPassSerial() == SeenPass)
	{
		return;
	}
	SeenPass = Player->GetPassSerial();
	OnPass(Player->GetLastPass().IsClean());
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
	if (Player->IsPlaying() && Phase == ECoachPhase::Hands && Player->GetCountInLeft() == 0)
	{
		TurnSeconds += DeltaSeconds;
	}
	HandlePass();
	if ((Phase == ECoachPhase::Trial || Phase == ECoachPhase::Ingrain || Phase == ECoachPhase::Hands) && Seconds >= SessionMinutes * 60.f)
	{
		Phase = ECoachPhase::TooBig;
		Advice = LOCTEXT("Timer", "Twenty minutes, and not there yet: more time would be wasted. Cut it in half and try again.");
		Player->Pause();
	}
}

#undef LOCTEXT_NAMESPACE
