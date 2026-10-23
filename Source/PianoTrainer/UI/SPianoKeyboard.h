// 88-key keyboard with real proportions: lights the next keys to play in the hand colour, the keys you hold
// (green when right, red when wrong) and the accompaniment the app is playing (keys go down like a player piano).
// Keys can also be clicked.
#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SLeafWidget.h"

class UPianoPlayer;

class SPianoKeyboard : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SPianoKeyboard) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UPianoPlayer>, Player)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	// Height follows the width so keys keep their shape (white key length = KeyAspect x width).
	virtual FVector2D ComputeDesiredSize(float) const override;

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

	// Left / right edge of a key at a given keyboard width (shared with the falling notes above it).
	static void KeyX(int32 Pitch, float Width, float& OutLeft, float& OutRight);
	static bool IsBlackKey(int32 Pitch) { return IsBlack(Pitch); }

	static constexpr int32 LowestPitch = 21;  // A0
	static constexpr int32 HighestPitch = 108; // C8
	static constexpr float KeyAspect = 5.4f;  // a real white key is ~6.4x as long as wide; a little shorter reads better on screen
	static constexpr float FeltHeight = 6.f;

private:
	static bool IsBlack(int32 Pitch);
	// Key rectangle in local units.
	FSlateRect KeyRect(int32 Pitch, const FVector2f& Size) const;
	int32 PitchAt(const FVector2f& Local, const FVector2f& Size) const;

	TWeakObjectPtr<UPianoPlayer> Player;
	FSlateBrush WhiteBrush;
	FSlateBrush ShadowBrush;
	mutable float LastWidth = 0.f;
	int32 MouseNote = INDEX_NONE;
};
