// Practice plans after Bernhard's method (Piano Street forum, 2003-08):
//  - a piece is learned in small sections (two bars, one where it is dense), each practised with the first beat of the
//    next bar so the sections join up;
//  - the hardest section first (when one is clearly harder: it takes longest), then new sections and joins
//    alternate: 1, 2, 1-2, 3, 1-3, ... (here a join spans at most four sections);
//  - each day starts by reviewing what was learned before (played cold) before anything new;
//  - a section goes New -> Learned (perfect at the end of a session) -> Mastered (played perfectly straight away on a
//    later day) -> Omniscient (relearned from scratch after a month away, several times: never forgotten);
//  - everything is written to a practice diary (Saved/PianoDiary.json).
// The rules applied during a session (seven repetitions, 20 minutes, "practice starts when you get it right") live in
// the app's coach; this file is the data.
#pragma once

#include "CoreMinimal.h"

class FPianoScore;

enum class EPianoStage : uint8
{
	New,
	Learned,
	Mastered,
	Omniscient,
};

struct FPianoSection
{
	int32 FirstBeat = 0; // quarter-note beats of the piece (FPianoScore::GetBeatTimes)
	int32 EndBeat = 0;   // exclusive
	EPianoStage Stage = EPianoStage::New;
	FString LearnedOn, MasteredOn, LastPractised; // yyyy-mm-dd
	int32 Sessions = 0;
	int32 Relearns = 0;   // times relearned after a month away (towards omniscience)
	float BestTempo = 0.f; // fastest clean tempo (1 = written)
	float Difficulty = 0.f; // notes per second in its densest bar
	FString Label;         // "m. 3–4", for lists that have no score at hand
};

struct FPianoPlanSession
{
	int32 First = 0; // section indices, inclusive
	int32 Last = 0;
	bool IsJoin() const { return Last > First; }
};

struct PIANOKIT_API FPianoPlan
{
	FString Piece;  // Scores-relative file, or an absolute path for other files
	FString Title;
	FString Created;
	int32 NextSession = 0;
	TArray<FPianoSection> Sections;

	// Two-bar sections, one bar where the music is dense (top quarter of notes per second).
	static FPianoPlan Make(const FPianoScore& Score, const FString& Piece, const FString& Title);
	// The hardest section first (if any), then 1, 2, 1-2, 3, 1-3 (up to four sections per join), ...
	TArray<FPianoPlanSession> Schedule() const;
	// The section clearly harder than the rest (half again the median), or INDEX_NONE.
	int32 Hardest() const;
	// A section learned on an earlier day and not played today: review it before new work. INDEX_NONE if none.
	int32 DueReview(const FString& Today) const;
	// Bernhard: "not learned in seven repetitions? Cut it in half." Down to a single beat.
	bool Split(int32 Section);
	// Song time of a session: start, end, and end with the overlap (the first beat after it).
	void GetTimes(const FPianoScore& Score, const FPianoPlanSession& Session, double& OutStartMs, double& OutEndMs, double& OutOverlapEndMs) const;
	FString Describe(const FPianoScore& Score, const FPianoPlanSession& Session) const;
	// Refreshes every section's Label (after making or splitting).
	void Relabel(const FPianoScore& Score);
	int32 Count(EPianoStage AtLeast) const;
};

struct FPianoDiaryEntry
{
	FString Date;   // yyyy-mm-dd
	FString Piece;
	FString Title;
	FString What;   // "m. 3–4", "join m. 1–6"
	float Minutes = 0.f;
	int32 Passes = 0;
	int32 Clean = 0;
	float Tempo = 1.f;
	FString Result; // "learned", "mastered", "too big: split", "stopped"
};

class PIANOKIT_API FPianoDiary
{
public:
	static FString GetPath();
	static FString Today();
	void Load();
	void Save() const;

	FPianoPlan* FindPlan(const FString& Piece);
	FPianoPlan& AddPlan(FPianoPlan&& Plan);
	const TArray<FPianoPlan>& GetPlans() const { return Plans; }
	void Log(const FPianoDiaryEntry& Entry);
	const TArray<FPianoDiaryEntry>& GetEntries() const { return Entries; }
	// Practice days in the last 7 days (Bernhard: at least three sessions a week).
	int32 DaysThisWeek() const;
	float MinutesOn(const FString& Date) const;

private:
	TArray<FPianoPlan> Plans;
	TArray<FPianoDiaryEntry> Entries;
};
