// Falling notes over the 2D keyboard (same width, same key positions): each note is a glowing gold bar (copper for
// the left hand) that falls onto the lit rail above its key and arrives when it is to be played; the hand the app
// plays is dimmer, played notes turn green and missed ones red. Octave lanes and measure lines run through the dark
// stage, and sparks burst where notes land.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

class UPianoPlayer;
struct FPianoViewSettings;

class SFallingNotes : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SFallingNotes) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UPianoPlayer>, Player)
		SLATE_ARGUMENT(const FPianoViewSettings*, Settings)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(400.0, 200.0); }
	virtual void Tick(const FGeometry& Geometry, const double CurrentTime, const float DeltaTime) override;

private:
	struct FSpark
	{
		FVector2f Position = FVector2f::ZeroVector; // x in local units, y up from the rail
		FVector2f Velocity = FVector2f::ZeroVector;
		float Age = 0.f;
		float Life = 0.5f;
		FLinearColor Color = FLinearColor::White;
		float Size = 2.f;
	};
	struct FFlare
	{
		float X = 0.f, Width = 10.f, Age = 0.f;
		FLinearColor Color = FLinearColor::White;
	};

	void Burst(float X, float Width, const FLinearColor& Color, int32 Count);

	TWeakObjectPtr<UPianoPlayer> Player;
	const FPianoViewSettings* Settings = nullptr;
	TArray<FSpark> Sparks;
	TArray<FFlare> Flares;
	TBitArray<> Landed;     // per played note: already burst
	int32 LandedSerial = -1;
	double LastNowMs = 0.0;
	float LastWidth = 0.f;
	FRandomStream Random{ 11 };
};
