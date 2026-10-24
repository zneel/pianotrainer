#include "SPianoKeyboard.h"

#include "PianoStyle.h"

#include "PianoPlayer.h"
#include "PianoScore.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Rendering/DrawElements.h"

namespace PianoKeyboardImpl
{
	static int32 WhiteIndex(int32 Pitch)
	{
		// white keys from A0
		static const int32 WhitesBefore[12] = { 0, 1, 1, 2, 2, 3, 4, 4, 5, 5, 6, 6 }; // C..B within an octave
		const int32 Octave = Pitch / 12;
		const int32 InOctave = Pitch % 12;
		return Octave * 7 + WhitesBefore[InOctave] - (/* A0 */ 1 * 7 + 5);
	}
	static constexpr int32 NumWhite = 52;

	// Vertical gradient from Top to Bottom colour across a box of the given size.
	static void Gradient(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const FVector2f& Pos, const FVector2f& Size,
		const FLinearColor& Top, const FLinearColor& Bottom, const FVector4f& Corners = FVector4f(0.f))
	{
		if (Size.X <= 0.f || Size.Y <= 0.f)
		{
			return;
		}
		TArray<FSlateGradientStop> Stops;
		Stops.Emplace(FVector2f(0.f, 0.f), Top);
		Stops.Emplace(Size, Bottom);
		FSlateDrawElement::MakeGradient(Out, Layer, Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(Pos)), MoveTemp(Stops),
			Orient_Horizontal, ESlateDrawEffect::None, Corners);
	}
}

void SPianoKeyboard::Construct(const FArguments& InArgs)
{
	Player = InArgs._Player;
	WhiteBrush = *FCoreStyle::Get().GetBrush("GenericWhiteBox");
	ShadowBrush = FSlateRoundedBoxBrush(FLinearColor::White, 3.f);
}

FVector2D SPianoKeyboard::ComputeDesiredSize(float) const
{
	const float Width = LastWidth > 0.f ? LastWidth : 1600.f;
	return FVector2D(400.0, FeltHeight + Width / PianoKeyboardImpl::NumWhite * KeyAspect + 1.0);
}

bool SPianoKeyboard::IsBlack(int32 Pitch)
{
	const int32 N = Pitch % 12;
	return N == 1 || N == 3 || N == 6 || N == 8 || N == 10;
}

void SPianoKeyboard::KeyX(int32 Pitch, float Width, float& OutLeft, float& OutRight)
{
	using namespace PianoKeyboardImpl;
	const float WhiteW = Width / NumWhite;
	if (!IsBlack(Pitch))
	{
		OutLeft = WhiteIndex(Pitch) * WhiteW;
		OutRight = OutLeft + WhiteW;
		return;
	}
	// black keys sit on the boundary after the white key below them
	const float Boundary = (WhiteIndex(Pitch - 1) + 1) * WhiteW;
	const float BlackW = WhiteW * 0.58f;
	OutLeft = Boundary - BlackW * 0.5f;
	OutRight = Boundary + BlackW * 0.5f;
}

FSlateRect SPianoKeyboard::KeyRect(int32 Pitch, const FVector2f& Size) const
{
	const float KeyH = FMath::Max(1.f, Size.Y - FeltHeight - 1.f);
	float Left = 0.f, Right = 0.f;
	KeyX(Pitch, Size.X, Left, Right);
	return FSlateRect(Left, FeltHeight, Right, FeltHeight + (IsBlack(Pitch) ? KeyH * 0.63f : KeyH));
}

int32 SPianoKeyboard::PitchAt(const FVector2f& Local, const FVector2f& Size) const
{
	for (int32 Pitch = LowestPitch; Pitch <= HighestPitch; ++Pitch)
	{
		if (IsBlack(Pitch) && KeyRect(Pitch, Size).ContainsPoint(Local))
		{
			return Pitch;
		}
	}
	for (int32 Pitch = LowestPitch; Pitch <= HighestPitch; ++Pitch)
	{
		if (!IsBlack(Pitch) && KeyRect(Pitch, Size).ContainsPoint(Local))
		{
			return Pitch;
		}
	}
	return INDEX_NONE;
}

int32 SPianoKeyboard::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	using namespace PianoKeyboardImpl;
	const FVector2f Size = AllottedGeometry.GetLocalSize();
	LastWidth = Size.X;

	// Case and felt.
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), &WhiteBrush, ESlateDrawEffect::None, PianoStyle::Hex(TEXT("0A0705")));
	// the rail the notes land on: a lit gold edge over dark felt
	Gradient(OutDrawElements, LayerId + 1, AllottedGeometry, FVector2f(0.f, 0.f), FVector2f(Size.X, FeltHeight), PianoStyle::Hex(TEXT("FFD27A")), PianoStyle::Hex(TEXT("4A2A0C")));

	// What each key should show: next keys to play (hand colour), accompaniment the app is sounding.
	UPianoPlayer* P = Player.Get();
	const TSharedPtr<FPianoScore> Score = P ? P->GetScore() : nullptr;
	uint8 Upcoming[128] = {};   // 0 none, 1 right hand, 2 left hand
	bool Accompaniment[128] = {};
	if (Score.IsValid())
	{
		TArray<int32> UpcomingNotes;
		P->GetUpcomingNotes(UpcomingNotes);
		const TArray<FPianoScoreNote>& Notes = Score->GetNotes();
		for (int32 Index : UpcomingNotes)
		{
			Upcoming[Notes[Index].Pitch & 127] = uint8(Notes[Index].Hand);
		}
		// what the app sounds, in the timing it plays (drills re-time notes)
		const double Now = P->GetSongMs();
		const uint8 Hands = uint8(P->GetHands());
		const bool bListen = P->GetListenMode();
		for (const FPianoSessionNote& Note : P->GetPlayNotes())
		{
			if (Note.OnMs > Now)
			{
				break;
			}
			// listening: the chosen hand(s); practising: the other hand
			if (Now < Note.OffMs && !Note.bTied && (bListen ? (uint8(Note.Hand) & Hands) != 0 : (uint8(Note.Hand) & Hands) == 0))
			{
				Accompaniment[Note.Pitch & 127] = true;
			}
		}
	}

	// Colour and press state of a key: Tint/Amount blend over the natural key colours.
	struct FKeyLook { FLinearColor Tint = FLinearColor::White; float Amount = 0.f; bool bDown = false; };
	auto Look = [&](int32 Pitch) -> FKeyLook
	{
		FKeyLook L;
		if (P && P->IsKeyHeld(Pitch))
		{
			L.bDown = true;
			switch (P->GetPressResult(Pitch))
			{
			case 1: L.Tint = PianoStyle::Good; L.Amount = 0.9f; break;
			case 0: L.Tint = PianoStyle::Bad; L.Amount = 0.9f; break;
			default: L.Tint = PianoStyle::Hex(TEXT("F2B33D")); L.Amount = 0.6f; break; // not judged (listen mode / in flight)
			}
			return L;
		}
		const float SinceWrong = P ? P->GetWrongFlash(Pitch) : 1000.f;
		if (SinceWrong < 0.35f)
		{
			L.Tint = PianoStyle::Bad;
			L.Amount = 0.9f * (1.f - SinceWrong / 0.35f);
			return L;
		}
		if (Upcoming[Pitch] != 0)
		{
			L.Tint = PianoStyle::HandColor(Upcoming[Pitch]);
			L.Amount = 0.75f;
			return L;
		}
		if (Accompaniment[Pitch])
		{
			L.Tint = PianoStyle::Hex(TEXT("E9B865"));
			L.Amount = 0.4f;
			L.bDown = true;
		}
		return L;
	};
	auto Blend = [](const FLinearColor& Base, const FKeyLook& L, float Shade = 1.f)
	{
		return FMath::Lerp(Base, L.Tint * Shade, L.Amount).CopyWithNewOpacity(1.f);
	};

	// White keys: shaded body, a front lip that gets shorter when the key is down, a rounded bottom.
	const float WhiteW = Size.X / NumWhite;
	for (int32 Pitch = LowestPitch; Pitch <= HighestPitch; ++Pitch)
	{
		if (IsBlack(Pitch))
		{
			continue;
		}
		const FSlateRect R = KeyRect(Pitch, Size);
		const FKeyLook L = Look(Pitch);
		const float W = R.GetSize().X - 1.f;
		const float H = R.GetSize().Y;
		const float Lip = L.bDown ? FMath::Max(2.f, H * 0.015f) : FMath::Max(5.f, H * 0.045f);
		const FLinearColor BodyTop = Blend(PianoStyle::Hex(L.bDown ? TEXT("D9D2C4") : TEXT("E9E3D6")), L, 0.92f);
		const FLinearColor BodyBottom = Blend(PianoStyle::Hex(TEXT("FBF8F1")), L);
		Gradient(OutDrawElements, LayerId + 2, AllottedGeometry, FVector2f(R.Left, R.Top), FVector2f(W, H - Lip), BodyTop, BodyBottom);
		Gradient(OutDrawElements, LayerId + 2, AllottedGeometry, FVector2f(R.Left, R.Bottom - Lip), FVector2f(W, Lip),
			Blend(PianoStyle::Hex(TEXT("CFC7B6")), L, 0.75f), Blend(PianoStyle::Hex(TEXT("ABA290")), L, 0.6f), FVector4f(0.f, 0.f, 3.f, 3.f));
		if (Pitch % 12 == 0 && WhiteW >= 14.f)
		{
			const FString Label = FString::Printf(TEXT("C%d"), Pitch / 12 - 1);
			const int32 FontSize = FMath::Clamp(FMath::RoundToInt(WhiteW * 0.3f), 7, 11);
			FSlateDrawElement::MakeText(OutDrawElements, LayerId + 3,
				AllottedGeometry.ToPaintGeometry(FVector2f(W, 16.f), FSlateLayoutTransform(FVector2f(R.Left + W * 0.5f - FontSize * 0.6f, R.Bottom - Lip - FontSize - 8.f))),
				Label, PianoStyle::Font(FontSize), ESlateDrawEffect::None, PianoStyle::Hex(TEXT("9AA1AF")));
		}
	}

	// Gaps between white keys: explicit lines, because a 1-unit gap disappears at fractional key widths.
	for (int32 Index = 1; Index < NumWhite; ++Index)
	{
		const float X = FMath::RoundToFloat(Index * WhiteW) - 0.5f;
		TArray<FVector2f> Points{ FVector2f(X, FeltHeight), FVector2f(X, Size.Y - 1.f) };
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 3, AllottedGeometry.ToPaintGeometry(), MoveTemp(Points),
			ESlateDrawEffect::None, PianoStyle::Hex(TEXT("6B6254")), false, 1.f);
	}

	// Shadow cast by the felt / key cover onto the keys.
	Gradient(OutDrawElements, LayerId + 3, AllottedGeometry, FVector2f(0.f, FeltHeight), FVector2f(Size.X, 10.f),
		FLinearColor(0.f, 0.f, 0.f, 0.35f), FLinearColor(0.f, 0.f, 0.f, 0.f));

	// Black keys: drop shadow, body and a lighter front bevel that shrinks when the key is down.
	for (int32 Pitch = LowestPitch; Pitch <= HighestPitch; ++Pitch)
	{
		if (!IsBlack(Pitch))
		{
			continue;
		}
		const FSlateRect R = KeyRect(Pitch, Size);
		const FKeyLook L = Look(Pitch);
		const FVector2f KeySize = R.GetSize();
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 4,
			AllottedGeometry.ToPaintGeometry(KeySize + FVector2f(2.f, 3.f), FSlateLayoutTransform(FVector2f(R.Left + 1.f, R.Top))),
			&ShadowBrush, ESlateDrawEffect::None, FLinearColor(0.f, 0.f, 0.f, 0.38f));
		const float Bevel = L.bDown ? KeySize.Y * 0.05f : KeySize.Y * 0.11f;
		Gradient(OutDrawElements, LayerId + 5, AllottedGeometry, FVector2f(R.Left, R.Top), FVector2f(KeySize.X, KeySize.Y - Bevel),
			Blend(PianoStyle::Hex(TEXT("100D0B")), L, 0.85f), Blend(PianoStyle::Hex(TEXT("2E2924")), L), FVector4f(0.f, 0.f, 1.5f, 1.5f));
		Gradient(OutDrawElements, LayerId + 5, AllottedGeometry, FVector2f(R.Left + 1.f, R.Bottom - Bevel), FVector2f(KeySize.X - 2.f, Bevel),
			Blend(PianoStyle::Hex(TEXT("575048")), L, 1.1f), Blend(PianoStyle::Hex(TEXT("26211D")), L, 0.8f), FVector4f(0.f, 0.f, 2.f, 2.f));
	}
	return LayerId + 6;
}

FReply SPianoKeyboard::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	UPianoPlayer* P = Player.Get();
	if (!P || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	MouseNote = PitchAt(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()), MyGeometry.GetLocalSize());
	if (MouseNote != INDEX_NONE)
	{
		P->NoteOn(MouseNote, 96);
	}
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SPianoKeyboard::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	UPianoPlayer* P = Player.Get();
	if (!P || !HasMouseCapture())
	{
		return FReply::Unhandled();
	}
	const int32 Pitch = PitchAt(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()), MyGeometry.GetLocalSize());
	if (Pitch != MouseNote)
	{
		if (MouseNote != INDEX_NONE)
		{
			P->NoteOff(MouseNote);
		}
		MouseNote = Pitch;
		if (MouseNote != INDEX_NONE)
		{
			P->NoteOn(MouseNote, 96);
		}
	}
	return FReply::Handled();
}

FReply SPianoKeyboard::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	UPianoPlayer* P = Player.Get();
	if (P && MouseNote != INDEX_NONE)
	{
		P->NoteOff(MouseNote);
	}
	MouseNote = INDEX_NONE;
	return FReply::Handled().ReleaseMouseCapture();
}
