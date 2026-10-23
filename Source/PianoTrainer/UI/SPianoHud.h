// Overlay on the music: the count-in beat numbers and a note while a score is being engraved.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class UPianoPlayer;

class SPianoHud : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPianoHud) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UPianoPlayer>, Player)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TWeakObjectPtr<UPianoPlayer> Player;
};
