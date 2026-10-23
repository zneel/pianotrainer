// The engraved score as one horizontal line scrolling under a fixed cursor (flowkey style).
// Noteheads are tinted as you play: next notes in the hand colour, played notes green, missed notes red.
#pragma once

#include "CoreMinimal.h"
#include "PianoSheetRenderer.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SLeafWidget.h"

class UPianoPlayer;

class SSheetStrip : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SSheetStrip) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UPianoPlayer>, Player)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void OnScoreChanged();

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(800.0, 260.0); }

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

	static constexpr float CursorFraction = 0.25f;

private:
	// Slate units per SVG pixel and the strip's x offset for the current song time.
	void GetMapping(const FGeometry& Geometry, double SongMs, float& OutScale, float& OutOffsetX) const;
	double LocalXToSongMs(const FGeometry& Geometry, float LocalX) const;
	// Rounded-box brushes need a radius no larger than half the box, so keep one per (rounded) notehead radius.
	const FSlateBrush* GetPillBrush(float Radius) const;

	TWeakObjectPtr<UPianoPlayer> Player;
	mutable FPianoSheetRenderer Renderer;
	mutable TMap<int32, TSharedPtr<FSlateBrush>> TileBrushes;
	FSlateBrush WhiteBrush;
	mutable TMap<int32, TSharedPtr<FSlateBrush>> PillBrushes;

	bool bScrubbing = false;
	bool bWasPlayingBeforeScrub = false;
	double ScrubAnchorMs = 0.0;
	float ScrubAnchorX = 0.f;
};
