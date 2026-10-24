// The menu: a left rail (Home, Etudes, Diary, Library) and pages:
//   Home     - continue, the Chopin Etudes, open a score, and the pieces practised recently
//   Etudes   - Chopin Op. 10 and Op. 25: each with its key, nickname, what it trains and the drills that suit it
//   Diary    - practice days this week, what is due (sections to check or play through, pieces to relearn), each
//              piece's plan progress, and the latest sessions (Bernhard: keep a practice diary)
//   Library  - the bundled samples, recent files, open a file
//   Settings - display, sound (the piano samples), MIDI keyboard, playback (SPianoSettingsPage); and Quit
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FPianoCoach;
class SBox;
class UPianoPlayer;
struct FPianoLibraryEntry;

class SPianoHome : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPianoHome) {}
		SLATE_ARGUMENT(TFunction<const TArray<FPianoLibraryEntry>&()>, Library)
		// most recent first
		SLATE_ARGUMENT(TFunction<TArray<FPianoLibraryEntry>()>, RecentEntries)
		SLATE_ARGUMENT(TFunction<void(const FPianoLibraryEntry&)>, OnPractise)
		SLATE_ARGUMENT(TFunction<void()>, OnOpenFile)
		SLATE_ARGUMENT(TFunction<void()>, OnContinue)
		SLATE_ARGUMENT(TFunction<FText()>, ContinueLabel)
		SLATE_ARGUMENT(FPianoCoach*, Coach)
		SLATE_ARGUMENT(TFunction<FText()>, SoundCredit) // the piano samples in use
		SLATE_ARGUMENT(TWeakObjectPtr<UPianoPlayer>, Player)
		SLATE_ARGUMENT(TFunction<void(const FString& Piece, int32 Section)>, OnPractiseSection)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	// Rebuilds the parts that change while the app runs (recent pieces).
	void Refresh();
	void ShowSettings() { CurrentPage = PageSettings; }

private:
	enum EPage { PageHome, PageEtudes, PageDiary, PageLibrary, PageSettings };
	TSharedRef<SWidget> MakeRail();
	TSharedRef<SWidget> MakeHomePage();
	TSharedRef<SWidget> MakeEtudesPage();
	TSharedRef<SWidget> MakeLibraryPage();
	TSharedRef<SWidget> MakeRecent();
	TSharedRef<SWidget> MakeDiary();
	TSharedRef<SWidget> MakeCard(const FText& Kicker, const FText& Title, const FText& Body, TAttribute<FText> Footer, TFunction<void()> OnClick, bool bFeatured = false);
	TSharedRef<SWidget> MakeEtudeDetail();

	TFunction<const TArray<FPianoLibraryEntry>&()> Library;
	TFunction<TArray<FPianoLibraryEntry>()> RecentEntries;
	TFunction<void(const FPianoLibraryEntry&)> OnPractise;
	TFunction<void()> OnOpenFile, OnContinue;
	TFunction<FText()> ContinueLabel;
	TSharedPtr<SBox> RecentBox;
	TSharedPtr<SBox> DiaryBox;
	FPianoCoach* Coach = nullptr;
	TFunction<FText()> SoundCredit;
	TWeakObjectPtr<UPianoPlayer> Player;
	TFunction<void(const FString&, int32)> OnPractiseSection;
	int32 CurrentPage = PageHome;
	int32 SelectedEtude = INDEX_NONE;
};
