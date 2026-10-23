// Runs practice sessions from a piece's plan with the rules Bernhard taught on Piano Street:
//  - the seven-times test: play the section seven times; not learned by then -> it is too big, cut it in half;
//  - "practice starts when you get it right": once a pass is clean, repeat it right at least as many times as it went
//    wrong before (and at least seven times);
//  - stop while the repetitions are perfect: the last one must be right (fatigue mistakes get ingrained); when
//    mistakes come back twice after it was right, the next clean pass ends the session;
//  - one session per section per day: learning settles overnight;
//  - at most 20 minutes per section (a timer): not there by then -> too big, cut it in half;
//  - stages: learned at the end of a good session; mastered when, on a later day, the first time through is perfect;
//    omniscient after relearning it, a month or more after it was last played, three times;
//  - repeated note-groups: the section's notes in units of four (overlapping by one note), practised as 1, 2, 3, ...
//    then 1-2, 2-3, ... then longer groups until the first and last group overlap, then the whole section; a group
//    set is finished in one go (the 20-minute limit does not apply to it);
//  - aim at the final tempo or faster.
// Every session goes into the practice diary.
#pragma once

#include "CoreMinimal.h"
#include "PianoPractice.h"

class UPianoPlayer;

enum class ECoachPhase : uint8
{
	Idle,
	Trial,   // the first seven times through
	Ingrain, // got it right: now repeat it right
	Groups,  // repeated note-groups
	Done,    // stop: the last repetition was perfect
	TooBig,  // not learned in seven / in 20 minutes: cut it in half
};

class FPianoCoach
{
public:
	static constexpr int32 Trial = 7;
	static constexpr float SessionMinutes = 20.f;

	void Init(UPianoPlayer* InPlayer);

	// The plan of the piece now loaded (none until made).
	FPianoPlan* GetPlan();
	FPianoPlan* MakePlan(const FString& Title);
	const FPianoDiary& GetDiary() const { return Diary; }

	void Start(int32 ScheduleIndex);
	void StartSection(int32 Section);
	void Stop();
	void Split();          // cut the session's (single) section in half and start again on the first half
	void StartGroups();    // repeated note-groups inside the session
	void SetTempo(float Tempo);
	void Tick(float DeltaSeconds);

	bool IsActive() const { return Phase != ECoachPhase::Idle; }
	ECoachPhase GetPhase() const { return Phase; }
	const FText& GetAdvice() const { return Advice; }
	const FString& GetLabel() const { return Label; }
	float GetSeconds() const { return Seconds; }
	int32 GetPasses() const { return Passes; }
	int32 GetClean() const { return Clean; }
	int32 GetTarget() const { return Target; }
	const TArray<int8>& GetDots() const { return Dots; } // 1 clean pass, 0 not (latest last)
	FPianoPlanSession GetSession() const { return Session; }
	bool IsColdCheck() const { return bColdCheck; }
	bool IsRelearn() const { return bRelearn; }
	static constexpr int32 RelearnsForOmniscience = 3;
	static constexpr int32 RelearnAfterDays = 30;

private:
	void Begin(const FPianoPlanSession& InSession, bool bScheduled);
	void LoopWhole();
	void OnPass(bool bCleanPass);
	void Finish(const TCHAR* Result);
	void NextGroup();
	FString PieceKey() const;
	FPianoPlan* SessionPlan(); // the plan of the piece the session is on (the player may have moved on)

	UPianoPlayer* Player = nullptr;
	FPianoDiary Diary;

	ECoachPhase Phase = ECoachPhase::Idle;
	FString Piece; // the session's piece
	FPianoPlanSession Session;
	int32 ScheduleIndex = INDEX_NONE;
	FString Label;
	FText Advice;
	double StartMs = 0.0, EndMs = 0.0, OverlapEndMs = 0.0;
	int32 SeenPass = 0;
	int32 Passes = 0, Clean = 0, WrongBeforeRight = 0, Target = Trial;
	bool bGotRight = false;
	bool bColdCheck = false;
	bool bFatigue = false;
	int32 FatigueMistakes = 0; // mistakes after it was right
	bool bRelearn = false; // a month or more since the section was last played
	bool bLogged = true;   // the session is in the diary
	int32 CountInBefore = 0;
	float Seconds = 0.f;
	TArray<int8> Dots;

	// repeated note-groups: unit start times, current group length and position
	TArray<double> Units;
	int32 GroupLength = 1, GroupStart = 0, GroupClean = 0;
	ECoachPhase PhaseBeforeGroups = ECoachPhase::Trial;
};
