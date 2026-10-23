#include "SPianoRuler.h"

#include "PianoCoach.h"
#include "PianoStyle.h"

#include "PianoPlayer.h"
#include "PianoScore.h"

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"

namespace PianoRulerImpl
{
	static constexpr float Pad = 14.f;
}

float SPianoRuler::TimeToX(const FGeometry& Geometry, double Ms) const
{
	using namespace PianoRulerImpl;
	const UPianoPlayer* P = Player.Get();
	const double Length = P && P->GetScore().IsValid() ? FMath::Max(1.0, P->GetScore()->GetLengthMs()) : 1.0;
	return Pad + float(FMath::Clamp(Ms / Length, 0.0, 1.0)) * (Geometry.GetLocalSize().X - 2.f * Pad);
}

double SPianoRuler::XToTime(const FGeometry& Geometry, float X) const
{
	using namespace PianoRulerImpl;
	const UPianoPlayer* P = Player.Get();
	const double Length = P && P->GetScore().IsValid() ? P->GetScore()->GetLengthMs() : 0.0;
	return Length * FMath::Clamp((X - Pad) / FMath::Max(1.f, Geometry.GetLocalSize().X - 2.f * Pad), 0.f, 1.f);
}

int32 SPianoRuler::MeasureAtX(const FGeometry& Geometry, float X) const
{
	const UPianoPlayer* P = Player.Get();
	return P && P->GetScore().IsValid() ? P->GetScore()->GetMeasureAt(XToTime(Geometry, X)) : 0;
}

int32 SPianoRuler::OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool) const
{
	const FVector2f Size = Geometry.GetLocalSize();
	FSlateDrawElement::MakeBox(Out, Layer, Geometry.ToPaintGeometry(), PianoStyle::White(), ESlateDrawEffect::None, PianoStyle::Hex(TEXT("0A0807")));
	const UPianoPlayer* P = Player.Get();
	const TSharedPtr<FPianoScore> Score = P ? P->GetScore() : nullptr;
	if (!Score.IsValid())
	{
		return Layer;
	}
	const TArray<FPianoScoreMeasure>& Measures = Score->GetMeasures();
	const float Base = Size.Y - 7.f;

	// practice plan: one segment per section along the top (grey new, gold learned, green mastered)
	if (FPianoPlan* Plan = Coach ? Coach->GetPlan() : nullptr)
	{
		const TArray<double> Beats = Score->GetBeatTimes();
		auto BeatMs = [&](int32 Beat) { return Beats.IsValidIndex(Beat) ? Beats[Beat] : Score->GetLengthMs(); };
		for (int32 I = 0; I < Plan->Sections.Num(); ++I)
		{
			const FPianoSection& S = Plan->Sections[I];
			const float X0 = TimeToX(Geometry, BeatMs(S.FirstBeat)) + 1.f, X1 = TimeToX(Geometry, BeatMs(S.EndBeat)) - 1.f;
			const FLinearColor Color = S.Stage == EPianoStage::Mastered ? PianoStyle::Good : S.Stage == EPianoStage::New ? PianoStyle::PanelLine : PianoStyle::GoldDeep;
			FSlateDrawElement::MakeBox(Out, Layer + 1, Geometry.ToPaintGeometry(FVector2f(FMath::Max(1.f, X1 - X0), 3.f), FSlateLayoutTransform(FVector2f(X0, 0.f))),
				PianoStyle::White(), ESlateDrawEffect::None, Color);
		}
	}

	// loop range
	double A = 0.0, B = 0.0;
	if (P->GetLoop(A, B))
	{
		const float X0 = TimeToX(Geometry, A), X1 = TimeToX(Geometry, B);
		FSlateDrawElement::MakeBox(Out, Layer + 1, Geometry.ToPaintGeometry(FVector2f(FMath::Max(2.f, X1 - X0), Size.Y - 4.f), FSlateLayoutTransform(FVector2f(X0, 2.f))),
			PianoStyle::Rounded(FLinearColor::White, 3.f), ESlateDrawEffect::None, PianoStyle::Gold.CopyWithNewOpacity(0.28f));
		for (const float X : { X0, X1 })
		{
			FSlateDrawElement::MakeBox(Out, Layer + 2, Geometry.ToPaintGeometry(FVector2f(3.f, Size.Y - 4.f), FSlateLayoutTransform(FVector2f(X - 1.5f, 2.f))),
				PianoStyle::White(), ESlateDrawEffect::None, PianoStyle::Gold);
		}
	}

	// baseline, measure ticks, numbers spaced so they never collide
	FSlateDrawElement::MakeLines(Out, Layer + 2, Geometry.ToPaintGeometry(), { FVector2f(PianoRulerImpl::Pad, Base), FVector2f(Size.X - PianoRulerImpl::Pad, Base) },
		ESlateDrawEffect::None, PianoStyle::PanelLine, false, 1.f);
	const float Spacing = Measures.Num() > 0 ? (Size.X - 2.f * PianoRulerImpl::Pad) / Measures.Num() : 100.f;
	int32 Every = 1;
	while (Every * Spacing < 30.f && Every < 1024)
	{
		Every *= 2;
	}
	const FSlateFontInfo Font = PianoStyle::Font(8, TEXT("Bold"));
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	for (int32 Index = 0; Index < Measures.Num(); ++Index)
	{
		const float X = TimeToX(Geometry, Measures[Index].OnMs);
		const bool bLabel = (Index % Every) == 0;
		FSlateDrawElement::MakeLines(Out, Layer + 2, Geometry.ToPaintGeometry(), { FVector2f(X, Base), FVector2f(X, Base - (bLabel ? 7.f : 3.f)) },
			ESlateDrawEffect::None, bLabel ? PianoStyle::TextDim : PianoStyle::TextFaint, false, 1.f);
		if (bLabel)
		{
			const FString Text = FString::FromInt(Index + 1);
			const FVector2f TextSize = FVector2f(Measure->Measure(Text, Font));
			FSlateDrawElement::MakeText(Out, Layer + 3, Geometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(FVector2f(X - TextSize.X * 0.5f, 3.f))),
				Text, Font, ESlateDrawEffect::None, PianoStyle::TextDim);
		}
	}

	// playhead
	const float X = TimeToX(Geometry, P->GetSongMs());
	FSlateDrawElement::MakeLines(Out, Layer + 4, Geometry.ToPaintGeometry(), { FVector2f(X, 4.f), FVector2f(X, Size.Y - 2.f) }, ESlateDrawEffect::None, PianoStyle::GoldGlow, true, 2.f);
	FSlateDrawElement::MakeBox(Out, Layer + 5, Geometry.ToPaintGeometry(FVector2f(9.f, 9.f), FSlateLayoutTransform(FVector2f(X - 4.5f, Base - 4.5f))),
		PianoStyle::Rounded(FLinearColor::White, 4.5f), ESlateDrawEffect::None, PianoStyle::GoldGlow);
	return Layer + 5;
}

FReply SPianoRuler::OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	UPianoPlayer* P = Player.Get();
	if (!P || !P->GetScore().IsValid() || Event.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	const float X = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()).X;
	double A = 0.0, B = 0.0;
	Drag = EDrag::Select;
	if (P->GetLoop(A, B))
	{
		if (FMath::Abs(X - TimeToX(Geometry, A)) < 7.f) { Drag = EDrag::LoopStart; }
		else if (FMath::Abs(X - TimeToX(Geometry, B)) < 7.f) { Drag = EDrag::LoopEnd; }
	}
	DragStartMeasure = MeasureAtX(Geometry, X);
	DragStartX = X;
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SPianoRuler::OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event)
{
	UPianoPlayer* P = Player.Get();
	if (!HasMouseCapture() || !P || !P->GetScore().IsValid())
	{
		return FReply::Unhandled();
	}
	const TSharedPtr<FPianoScore> Score = P->GetScore();
	const float X = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()).X;
	const int32 Here = MeasureAtX(Geometry, X);
	double A = 0.0, B = 0.0;
	P->GetLoop(A, B);
	if (Drag == EDrag::Select && FMath::Abs(X - DragStartX) > 8.f)
	{
		const int32 First = FMath::Min(DragStartMeasure, Here), Last = FMath::Max(DragStartMeasure, Here);
		P->SetLoop(Score->GetMeasureStartMs(First), Score->GetMeasureEndMs(Last));
	}
	else if (Drag == EDrag::LoopStart)
	{
		P->SetLoop(FMath::Min(Score->GetMeasureStartMs(Here), B - 50.0), B);
	}
	else if (Drag == EDrag::LoopEnd)
	{
		P->SetLoop(A, FMath::Max(Score->GetMeasureEndMs(Here), A + 50.0));
	}
	return FReply::Handled();
}

FReply SPianoRuler::OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event)
{
	UPianoPlayer* P = Player.Get();
	if (!HasMouseCapture())
	{
		return FReply::Unhandled();
	}
	const float X = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()).X;
	if (P && P->GetScore().IsValid())
	{
		double A = 0.0, B = 0.0;
		if (Drag == EDrag::Select && FMath::Abs(X - DragStartX) <= 8.f)
		{
			P->Seek(P->GetScore()->GetMeasureStartMs(MeasureAtX(Geometry, X))); // click: jump to that measure
		}
		else if (P->GetLoop(A, B))
		{
			P->Seek(A);
		}
	}
	Drag = EDrag::None;
	return FReply::Handled().ReleaseMouseCapture();
}
