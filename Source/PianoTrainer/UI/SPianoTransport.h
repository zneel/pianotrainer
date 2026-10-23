// The bottom bar: walnut end caps around brushed-metal plates with the piece and its progress, the measure counter,
// transport buttons (restart / measure back / play-pause / measure forward), the speed knob and the loop lamp.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class UPianoPlayer;

class SPianoTransport : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPianoTransport) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UPianoPlayer>, Player)
		SLATE_ATTRIBUTE(FText, Title)
		SLATE_ATTRIBUTE(FText, Subtitle)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	void StepMeasure(int32 Delta);
	void ToggleLoop();
	TWeakObjectPtr<UPianoPlayer> Player;
};
