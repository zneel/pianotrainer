// What the practice screen shows and how it practises, remembered between runs (GameUserSettings.ini).
#pragma once

#include "CoreMinimal.h"

struct FPianoViewSettings
{
	bool bShowSheet = true;
	bool bShowHighway = false; // off by default: the sheet is the main view, falling notes are optional
	bool bSparks = true;
	bool bColorByHand = true;
	float LookaheadMs = 2600.f; // song time the falling notes show ahead, at normal speed
	int32 CountInBars = 0;
	float LatencyMs = 0.f;

	void Load();
	void Save() const;
};
