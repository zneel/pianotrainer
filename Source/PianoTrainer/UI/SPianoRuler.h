// The whole piece as a measure ruler under the stage: measure numbers, the playhead and the loop range (gold).
// Click a measure to jump there; drag across measures to loop them; drag a loop edge to move it.
// With a practice plan, its sections run along the top (grey new, gold learned, green mastered; a passage that comes
// back is paler): drag an edge between two sections to move it (it snaps to bar lines), onto the next edge to merge
// the two; double-click a section to split it at that bar.
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
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(200.0, 34.0 + PlanZone()); }
	virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual void OnMouseLeave(const FPointerEvent& Event) override { HoverEdge = INDEX_NONE; }
	virtual FCursorReply OnCursorQuery(const FGeometry& Geometry, const FPointerEvent& Event) const override;

	// Where a plan beat is on screen (desktop pixels, in the plan strip), from the last layout: for tests that drive
	// the ruler with real mouse events.
	FVector2D GetPlanBeatScreenPosition(int32 Beat) const;

private:
	enum class EDrag : uint8 { None, Select, LoopStart, LoopEnd, SectionEdge };
	float PlanZone() const; // height of the plan strip (0 without a plan)
	float TimeToX(const FGeometry& Geometry, double Ms) const;
	double XToTime(const FGeometry& Geometry, float X) const;
	int32 MeasureAtX(const FGeometry& Geometry, float X) const;
	float BeatToX(const FGeometry& Geometry, int32 Beat) const;
	int32 EdgeAtX(const FGeometry& Geometry, float X) const; // index of the section before the edge, or INDEX_NONE
	int32 SnapBeat(const FGeometry& Geometry, float X) const; // nearest beat, or a bar line within a few pixels
	TWeakObjectPtr<UPianoPlayer> Player;
	FPianoCoach* Coach = nullptr;
	EDrag Drag = EDrag::None;
	int32 DragStartMeasure = 0;
	float DragStartX = 0.f;
	int32 DragEdge = INDEX_NONE, DragBeat = INDEX_NONE, HoverEdge = INDEX_NONE;
};
