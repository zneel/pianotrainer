// The sidebar's Plan tab: the piece's practice plan after Bernhard (sections, the session schedule with joins,
// learned / mastered stages) and the running session (seven-times test, repetitions, 20-minute timer, advice).
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FPianoCoach;
class SBox;
class UPianoPlayer;

class SPianoPlanPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPianoPlanPanel) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UPianoPlayer>, Player)
		SLATE_ARGUMENT(FPianoCoach*, Coach)
		SLATE_ARGUMENT(TFunction<FText()>, Title) // the piece's name
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& Geometry, const double CurrentTime, const float DeltaTime) override;

private:
	TSharedRef<SWidget> MakeContent();
	TSharedRef<SWidget> MakeSession();
	TSharedRef<SWidget> MakeSections();
	uint32 Signature() const;

	TWeakObjectPtr<UPianoPlayer> Player;
	FPianoCoach* Coach = nullptr;
	TFunction<FText()> Title;
	TSharedPtr<SBox> Body;
	uint32 Built = 0;
};
