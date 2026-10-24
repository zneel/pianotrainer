#include "SPianoRuler.h"

#include "PianoCoach.h"
#include "PianoStyle.h"

#include "PianoPlayer.h"
#include "PianoScore.h"

#include "Algo/BinarySearch.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"

namespace PianoRulerImpl
{
	static constexpr float Pad = 14.f;
	static constexpr float PlanStrip = 12.f; // plan sections along the top
	static constexpr float EdgeGrab = 6.f;   // px either side of a section edge
	static constexpr float BarSnap = 8.f;    // px: a dragged edge snaps to a bar line this close
}

float SPianoRuler::PlanZone() const
{
	return Coach && Coach->GetPlan() ? PianoRulerImpl::PlanStrip : 0.f;
}

float SPianoRuler::BeatToX(const FGeometry& Geometry, int32 Beat) const
{
	const UPianoPlayer* P = Player.Get();
	if (!P || !P->GetScore().IsValid())
	{
		return 0.f;
	}
	const TArray<double> Beats = P->GetScore()->GetBeatTimes();
	return TimeToX(Geometry, Beats.IsValidIndex(Beat) ? Beats[Beat] : P->GetScore()->GetLengthMs());
}

int32 SPianoRuler::EdgeAtX(const FGeometry& Geometry, float X) const
{
	const FPianoPlan* Plan = Coach ? Coach->GetPlan() : nullptr;
	int32 Best = INDEX_NONE;
	float BestDistance = PianoRulerImpl::EdgeGrab;
	for (int32 I = 0; Plan && I + 1 < Plan->Sections.Num(); ++I)
	{
		const float Distance = FMath::Abs(X - BeatToX(Geometry, Plan->Sections[I + 1].FirstBeat));
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = I;
		}
	}
	return Best;
}

int32 SPianoRuler::SnapBeat(const FGeometry& Geometry, float X) const
{
	const UPianoPlayer* P = Player.Get();
	if (!P || !P->GetScore().IsValid())
	{
		return 0;
	}
	const TSharedPtr<FPianoScore> Score = P->GetScore();
	const TArray<double> Beats = Score->GetBeatTimes();
	const double Ms = XToTime(Geometry, X);
	auto Nearest = [&Beats](double At)
	{
		const int32 B = FMath::Clamp(Algo::UpperBound(Beats, At) - 1, 0, Beats.Num() - 1);
		return Beats.IsValidIndex(B + 1) && Beats[B + 1] - At < At - Beats[B] ? B + 1 : B;
	};
	// a bar line within a few pixels wins
	const int32 Measure = Score->GetMeasureAt(Ms);
	for (const int32 M : { Measure, Measure + 1 })
	{
		if (M < Score->GetMeasures().Num() && FMath::Abs(TimeToX(Geometry, Score->GetMeasureStartMs(M)) - X) < PianoRulerImpl::BarSnap)
		{
			return Nearest(Score->GetMeasureStartMs(M));
		}
	}
	return Nearest(Ms);
}

FVector2D SPianoRuler::GetPlanBeatScreenPosition(int32 Beat) const
{
	const FGeometry& Geometry = GetTickSpaceGeometry(); // desktop space (paint space is the window's)
	return FVector2D(Geometry.LocalToAbsolute(FVector2f(BeatToX(Geometry, Beat), PianoRulerImpl::PlanStrip * 0.5f)));
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
	const float Top = PlanZone(); // the measure numbers sit under the plan strip

	// practice plan: one bar per section along the top (grey new, gold learned, green mastered; a passage that comes
	// back is paler; the running session bright), and a handle on every edge between two sections
	if (FPianoPlan* Plan = Coach ? Coach->GetPlan() : nullptr)
	{
		const FPianoPlanSession Running = Coach->IsActive() ? Coach->GetSession() : FPianoPlanSession{ -1, -2 };
		for (int32 I = 0; I < Plan->Sections.Num(); ++I)
		{
			const FPianoSection& S = Plan->Sections[I];
			const float X0 = BeatToX(Geometry, S.FirstBeat) + 1.f, X1 = BeatToX(Geometry, S.EndBeat) - 1.f;
			FLinearColor Color = S.Stage == EPianoStage::Mastered ? PianoStyle::Good : S.Stage == EPianoStage::New ? PianoStyle::TextDim : PianoStyle::Gold;
			Color = I >= Running.First && I <= Running.Last ? PianoStyle::GoldGlow : Color;
			FSlateDrawElement::MakeBox(Out, Layer + 1, Geometry.ToPaintGeometry(FVector2f(FMath::Max(1.f, X1 - X0), 7.f), FSlateLayoutTransform(FVector2f(X0, 2.5f))),
				PianoStyle::Rounded(FLinearColor::White, 3.f), ESlateDrawEffect::None, Color.CopyWithNewOpacity(S.RepeatOf != INDEX_NONE ? 0.35f : 0.85f));
		}
		for (int32 I = 0; I + 1 < Plan->Sections.Num(); ++I)
		{
			const bool bDragged = Drag == EDrag::SectionEdge && I == DragEdge;
			const bool bHot = I == HoverEdge || bDragged;
			const float X = BeatToX(Geometry, bDragged ? DragBeat : Plan->Sections[I + 1].FirstBeat);
			FSlateDrawElement::MakeBox(Out, Layer + 2, Geometry.ToPaintGeometry(FVector2f(bHot ? 4.f : 3.f, 12.f), FSlateLayoutTransform(FVector2f(X - (bHot ? 2.f : 1.5f), 0.f))),
				PianoStyle::White(), ESlateDrawEffect::None, bHot ? PianoStyle::GoldGlow : PianoStyle::Hex(TEXT("0A0807")));
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
			FSlateDrawElement::MakeText(Out, Layer + 3, Geometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(FVector2f(X - TextSize.X * 0.5f, 3.f + Top))),
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
	const FVector2f Local = FVector2f(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()));
	const float X = Local.X;
	// a section edge in the plan strip
	if (Local.Y < PlanZone())
	{
		const int32 Edge = EdgeAtX(Geometry, X);
		if (Edge != INDEX_NONE)
		{
			Drag = EDrag::SectionEdge;
			DragEdge = Edge;
			DragBeat = Coach->GetPlan()->Sections[Edge + 1].FirstBeat;
			return FReply::Handled().CaptureMouse(SharedThis(this));
		}
	}
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
	const FVector2f Local = FVector2f(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()));
	if (!HasMouseCapture() || !P || !P->GetScore().IsValid())
	{
		HoverEdge = Local.Y < PlanZone() ? EdgeAtX(Geometry, Local.X) : INDEX_NONE;
		return FReply::Unhandled();
	}
	const TSharedPtr<FPianoScore> Score = P->GetScore();
	const float X = Local.X;
	if (Drag == EDrag::SectionEdge)
	{
		// between the two sections' outer edges; reaching one merges them
		const FPianoPlan* Plan = Coach ? Coach->GetPlan() : nullptr;
		if (Plan && Plan->Sections.IsValidIndex(DragEdge + 1))
		{
			DragBeat = FMath::Clamp(SnapBeat(Geometry, X), Plan->Sections[DragEdge].FirstBeat, Plan->Sections[DragEdge + 1].EndBeat);
		}
		return FReply::Handled();
	}
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
	if (Drag == EDrag::SectionEdge)
	{
		const FPianoPlan* Plan = Coach ? Coach->GetPlan() : nullptr;
		if (Plan && Plan->Sections.IsValidIndex(DragEdge + 1) && DragBeat != Plan->Sections[DragEdge + 1].FirstBeat)
		{
			Coach->MoveSectionEdge(DragEdge, DragBeat);
		}
		Drag = EDrag::None;
		DragEdge = DragBeat = INDEX_NONE;
		return FReply::Handled().ReleaseMouseCapture();
	}
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

FReply SPianoRuler::OnMouseButtonDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event)
{
	const FVector2f Local = FVector2f(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()));
	if (Event.GetEffectingButton() != EKeys::LeftMouseButton || Local.Y >= PlanZone() || !Coach)
	{
		return FReply::Unhandled();
	}
	// split the section here (at the bar line, when one is near)
	Coach->SplitSectionAt(SnapBeat(Geometry, Local.X));
	return FReply::Handled();
}

FCursorReply SPianoRuler::OnCursorQuery(const FGeometry&, const FPointerEvent&) const
{
	return HoverEdge != INDEX_NONE || Drag == EDrag::SectionEdge ? FCursorReply::Cursor(EMouseCursor::ResizeLeftRight) : FCursorReply::Unhandled();
}
