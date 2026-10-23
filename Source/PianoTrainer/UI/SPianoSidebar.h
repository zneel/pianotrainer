// The settings panel on the right of the practice screen, with two tabs:
//   Practice - guided practice (wait / play along / listen, hands, latency), count-in, Cortot-style drills,
//              the section to loop, and the two hands' note counts.
//   Plan     - the piece's practice plan after Bernhard (SPianoPlanPanel).
//   Visual   - sheet music, falling notes, sparks, colours, note speed; sound credits and MIDI devices.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FPianoCoach;
class UPianoPlayer;
struct FPianoLibraryEntry;
struct FPianoViewSettings;

class SPianoSidebar : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPianoSidebar) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UPianoPlayer>, Player)
		SLATE_ARGUMENT(FPianoViewSettings*, Settings)
		// The library entry being practised (its suggested drills come first), or null.
		SLATE_ARGUMENT(TFunction<const FPianoLibraryEntry*()>, Entry)
		SLATE_ARGUMENT(FPianoCoach*, Coach)
		SLATE_ARGUMENT(TFunction<FText()>, Title)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TSharedRef<SWidget> MakePracticeTab();
	TSharedRef<SWidget> MakeVisualTab();
	TSharedRef<SWidget> MakeDrills();
	TSharedRef<SWidget> MakeSection();
	TSharedRef<SWidget> Switch(TFunction<bool()> Get, TFunction<void()> Toggle);

	TWeakObjectPtr<UPianoPlayer> Player;
	FPianoViewSettings* Settings = nullptr;
	TFunction<const FPianoLibraryEntry*()> Entry;
	FPianoCoach* Coach = nullptr;
	TFunction<FText()> Title;
	int32 Tab = 0;
};
