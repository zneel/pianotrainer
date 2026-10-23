// The whole piece as a measure ruler under the stage: measure numbers, the playhead and the loop range (gold).
// Click a measure to jump there; drag across measures to loop them; drag a loop edge to move it.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

class FPianoCoach;
class UPianoPlayer;

class SPianoRuler : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SPianoRuler) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UPianoPlayer>, Player)
		SLATE_ARGUMENT(FPianoCoach*, Coach) // its plan's sections are drawn along the top
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs) { Player = InArgs._Player; Coach = InArgs._Coach; }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(200.0, 34.0); }
	virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;

private:
	enum class EDrag : uint8 { None, Select, LoopStart, LoopEnd };
	float TimeToX(const FGeometry& Geometry, double Ms) const;
	double XToTime(const FGeometry& Geometry, float X) const;
	int32 MeasureAtX(const FGeometry& Geometry, float X) const;
	TWeakObjectPtr<UPianoPlayer> Player;
	FPianoCoach* Coach = nullptr;
	EDrag Drag = EDrag::None;
	int32 DragStartMeasure = 0;
	float DragStartX = 0.f;
};
