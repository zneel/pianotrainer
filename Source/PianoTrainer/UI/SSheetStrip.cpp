#include "SSheetStrip.h"

#include "PianoStyle.h"

#include "PianoPlayer.h"
#include "PianoScore.h"

#include "Algo/BinarySearch.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/Texture2D.h"
#include "Rendering/DrawElements.h"

void SSheetStrip::Construct(const FArguments& InArgs)
{
	Player = InArgs._Player;
	WhiteBrush = *FCoreStyle::Get().GetBrush("GenericWhiteBox");
	SetClipping(EWidgetClipping::ClipToBounds); // the loop shade and tiles reach past the edges as the score scrolls
	OnScoreChanged();
}

const FSlateBrush* SSheetStrip::GetPillBrush(float Radius) const
{
	const int32 Key = FMath::Clamp(FMath::FloorToInt(Radius), 1, 64);
	TSharedPtr<FSlateBrush>& Brush = PillBrushes.FindOrAdd(Key);
	if (!Brush.IsValid())
	{
		Brush = MakeShared<FSlateRoundedBoxBrush>(FLinearColor::White, float(Key));
	}
	return Brush.Get();
}

void SSheetStrip::OnScoreChanged()
{
	UPianoPlayer* P = Player.Get();
	Renderer.SetScore(P ? P->GetScore() : nullptr);
	TileBrushes.Reset();
}

void SSheetStrip::GetMapping(const FGeometry& Geometry, double SongMs, float& OutScale, float& OutOffsetX) const
{
	OutScale = 1.f;
	OutOffsetX = 0.f;
	const UPianoPlayer* P = Player.Get();
	const TSharedPtr<FPianoScore> Score = P ? P->GetScore() : nullptr;
	if (!Score.IsValid() || Score->GetSize().Y <= 0.f)
	{
		return;
	}
	const FVector2f Size = Geometry.GetLocalSize();
	OutScale = Size.Y / Score->GetSize().Y;
	OutOffsetX = Size.X * CursorFraction - Score->TimeToX(SongMs) * OutScale;
}

double SSheetStrip::LocalXToSongMs(const FGeometry& Geometry, float LocalX) const
{
	const UPianoPlayer* P = Player.Get();
	const TSharedPtr<FPianoScore> Score = P ? P->GetScore() : nullptr;
	if (!Score.IsValid())
	{
		return 0.0;
	}
	float Scale, OffsetX;
	GetMapping(Geometry, P->GetSongMs(), Scale, OffsetX);
	return Score->XToTime((LocalX - OffsetX) / Scale);
}

int32 SSheetStrip::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FVector2f Size = AllottedGeometry.GetLocalSize();
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), &WhiteBrush, ESlateDrawEffect::None, PianoStyle::Paper);

	UPianoPlayer* P = Player.Get();
	const TSharedPtr<FPianoScore> Score = P ? P->GetScore() : nullptr;
	if (!Score.IsValid())
	{
		FSlateDrawElement::MakeText(OutDrawElements, LayerId + 1,
			AllottedGeometry.ToPaintGeometry(FVector2f(Size.X, 40.f), FSlateLayoutTransform(FVector2f(24.f, Size.Y * 0.5f - 20.f))),
			FText::FromString(TEXT("Drop a MusicXML / .mxl file here, or press Ctrl+O")), PianoStyle::Font(16), ESlateDrawEffect::None, PianoStyle::TextDim);
		return LayerId + 2;
	}

	const double SongMs = P->GetSongMs();
	float Scale, OffsetX;
	GetMapping(AllottedGeometry, SongMs, Scale, OffsetX);

	// Engraved line, rasterised at the physical pixel height of this widget.
	Renderer.SetPixelHeight(FMath::Max(16, FMath::RoundToInt(Size.Y * AllottedGeometry.Scale)));
	const float TileSlate = float(FPianoSheetRenderer::TileWidth) / Renderer.GetScale() * Scale;
	const int32 FirstTile = FMath::Max(0, FMath::FloorToInt(-OffsetX / TileSlate));
	const int32 LastTile = FMath::Min(Renderer.GetNumTiles() - 1, FMath::FloorToInt((Size.X - OffsetX) / TileSlate));
	for (int32 Tile = FirstTile; Tile <= LastTile; ++Tile)
	{
		UTexture2D* Texture = Renderer.GetTile(Tile);
		if (!Texture)
		{
			continue;
		}
		TSharedPtr<FSlateBrush>& Brush = TileBrushes.FindOrAdd(Tile);
		if (!Brush.IsValid() || Brush->GetResourceObject() != Texture)
		{
			Brush = MakeShared<FSlateBrush>();
			Brush->SetResourceObject(Texture);
			Brush->ImageSize = FVector2D(FPianoSheetRenderer::TileWidth, Renderer.GetPixelHeight());
			Brush->DrawAs = ESlateBrushDrawType::Image;
		}
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
			AllottedGeometry.ToPaintGeometry(FVector2f(TileSlate, Size.Y), FSlateLayoutTransform(FVector2f(OffsetX + Tile * TileSlate, 0.f))),
			Brush.Get(), ESlateDrawEffect::None, FLinearColor::White);
	}
	// Start rendering the next tiles in the background before they scroll in, and drop tiles far behind.
	Renderer.GetTile(LastTile + 1);
	Renderer.GetTile(LastTile + 2);
	Renderer.Trim(FirstTile, 4);
	for (auto It = TileBrushes.CreateIterator(); It; ++It)
	{
		if (FMath::Abs(It.Key() - FirstTile) > 4)
		{
			It.RemoveCurrent();
		}
	}

	// A-B loop region. Its edges sit in the gap between notes - halfway from the last notehead before to the first one
	// at the edge - so no note is cut in two (song time maps onto the notehead itself).
	double LoopStart = 0.0, LoopEnd = 0.0;
	if (P->GetLoop(LoopStart, LoopEnd))
	{
		const TArray<FPianoScoreNote>& AllNotes = Score->GetNotes();
		auto GapBefore = [&](double Ms)
		{
			const int32 At = Algo::LowerBoundBy(AllNotes, Ms - 2.0, &FPianoScoreNote::OnMs);
			float Left = Score->TimeToX(Ms), Right = -TNumericLimits<float>::Max();
			for (int32 I = At; I < AllNotes.Num() && AllNotes[I].OnMs <= Ms + 2.0; ++I)
			{
				Left = AllNotes[I].Head.bIsValid ? FMath::Min(Left, AllNotes[I].Head.Min.X) : Left;
			}
			if (At > 0)
			{
				const double Before = AllNotes[At - 1].OnMs;
				for (int32 I = At - 1; I >= 0 && AllNotes[I].OnMs >= Before - 2.0; --I)
				{
					Right = AllNotes[I].Head.bIsValid ? FMath::Max(Right, AllNotes[I].Head.Max.X) : FMath::Max(Right, Score->TimeToX(Before));
				}
			}
			return Right > -TNumericLimits<float>::Max() && Right < Left ? 0.5f * (Left + Right) : Left - 6.f;
		};
		const float X0 = FMath::Clamp(OffsetX + GapBefore(LoopStart) * Scale, 0.f, Size.X);
		const float X1 = FMath::Clamp(OffsetX + GapBefore(LoopEnd) * Scale, 0.f, Size.X);
		if (X1 > X0)
		{
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 2,
				AllottedGeometry.ToPaintGeometry(FVector2f(X1 - X0, Size.Y), FSlateLayoutTransform(FVector2f(X0, 0.f))),
				&WhiteBrush, ESlateDrawEffect::None, PianoStyle::LoopShade.CopyWithNewOpacity(0.35f));
		}
	}

	// Notehead tints.
	TArray<int32> Upcoming;
	P->GetUpcomingNotes(Upcoming);
	const TArray<FPianoScoreNote>& Notes = Score->GetNotes();
	for (int32 Index = 0; Index < Notes.Num(); ++Index)
	{
		const FPianoScoreNote& Note = Notes[Index];
		if (!Note.Head.bIsValid)
		{
			continue;
		}
		const float X0 = OffsetX + Note.Head.Min.X * Scale;
		const FVector2f HeadSize = Note.Head.GetSize() * Scale;
		if (X0 > Size.X || X0 + HeadSize.X < 0.f)
		{
			continue;
		}
		FLinearColor Color;
		switch (P->GetNoteState(Note.IsStruck() ? Index : Note.TieRoot)) // tied notes show their first note's result
		{
		case EPianoNoteState::Hit:
			Color = PianoStyle::Good;
			break;
		case EPianoNoteState::Missed:
			Color = PianoStyle::Bad;
			break;
		default:
			if (!Upcoming.Contains(Index))
			{
				continue;
			}
			Color = PianoStyle::HandColor(uint8(Note.Hand));
			break;
		}
		const float Pad = FMath::Max(1.f, HeadSize.Y * 0.12f);
		const FVector2f BoxSize = HeadSize + FVector2f(2.f * Pad);
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 3,
			AllottedGeometry.ToPaintGeometry(BoxSize, FSlateLayoutTransform(FVector2f(X0 - Pad, Note.Head.Min.Y * Scale - Pad))),
			GetPillBrush(BoxSize.GetMin() * 0.5f), ESlateDrawEffect::None, Color.CopyWithNewOpacity(0.82f));
	}

	// Cursor; it pulses while wait mode is holding the music.
	const float CursorX = Size.X * CursorFraction;
	float Alpha = 0.55f;
	if (P->IsWaiting())
	{
		Alpha = 0.45f + 0.35f * FMath::Sin(float(FPlatformTime::Seconds()) * 6.f);
	}
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 4,
		AllottedGeometry.ToPaintGeometry(FVector2f(4.f, Size.Y), FSlateLayoutTransform(FVector2f(CursorX - 2.f, 0.f))),
		&WhiteBrush, ESlateDrawEffect::None, PianoStyle::Cursor.CopyWithNewOpacity(Alpha));

	return LayerId + 5;
}

FReply SSheetStrip::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	UPianoPlayer* P = Player.Get();
	if (!P || !P->GetScore().IsValid() || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	bScrubbing = true;
	bWasPlayingBeforeScrub = P->IsPlaying();
	P->Pause();
	ScrubAnchorMs = P->GetSongMs();
	ScrubAnchorX = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()).X;
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SSheetStrip::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	UPianoPlayer* P = Player.Get();
	if (!bScrubbing || !P || !P->GetScore().IsValid())
	{
		return FReply::Unhandled();
	}
	// Drag the paper: moving the mouse left moves forward in the piece.
	const TSharedPtr<FPianoScore> Score = P->GetScore();
	float Scale, OffsetX;
	GetMapping(MyGeometry, ScrubAnchorMs, Scale, OffsetX);
	const float LocalX = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()).X;
	const float StripX = Score->TimeToX(ScrubAnchorMs) - (LocalX - ScrubAnchorX) / Scale;
	P->Seek(Score->XToTime(StripX));
	return FReply::Handled();
}

FReply SSheetStrip::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	UPianoPlayer* P = Player.Get();
	if (!bScrubbing)
	{
		return FReply::Unhandled();
	}
	bScrubbing = false;
	const float LocalX = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()).X;
	if (P && FMath::Abs(LocalX - ScrubAnchorX) < 3.f)
	{
		// a click (no drag): jump to the clicked spot
		float Scale, OffsetX;
		GetMapping(MyGeometry, ScrubAnchorMs, Scale, OffsetX);
		P->Seek(P->GetScore()->XToTime((LocalX - OffsetX) / Scale));
	}
	if (P && bWasPlayingBeforeScrub)
	{
		P->Play();
	}
	return FReply::Handled().ReleaseMouseCapture();
}

FReply SSheetStrip::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	UPianoPlayer* P = Player.Get();
	if (!P || !P->GetScore().IsValid())
	{
		return FReply::Unhandled();
	}
	P->Seek(P->GetSongMs() - MouseEvent.GetWheelDelta() * 400.0);
	return FReply::Handled();
}
