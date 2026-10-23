// The scores the app ships with and the ones the player opened: titles for the menu, and for the Chopin Etudes the
// technique each one trains and the drills that suit it.
#pragma once

#include "CoreMinimal.h"
#include "PianoDrills.h"

struct FPianoLibraryEntry
{
	FString Path;        // absolute
	FString File;        // relative to the Scores folder (empty for files elsewhere)
	FString Title;       // "Etude in C major"
	FString Subtitle;    // "Op. 10 No. 1 · Waterfall"
	FString Collection;  // "Chopin · 24 Etudes", "Samples", "Recent"
	FText Focus;         // what it trains
	TArray<EPianoDrill> Drills;
	bool bBuiltIn = false;
};

struct PIANOKIT_API FPianoLibrary
{
	static FString GetScoresDir();
	// Built-in scores (Scores/**), then the recent files that still exist.
	static TArray<FPianoLibraryEntry> Scan(const TArray<FString>& RecentPaths);
	static FString ToScoresRelative(const FString& Path);
	static FString FromScoresRelative(const FString& File);
};
