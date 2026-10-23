#include "SPianoTransport.h"

#include "PianoStyle.h"
#include "PianoWidgets.h"

#include "PianoPlayer.h"
#include "PianoScore.h"

#include "Rendering/DrawElements.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "PianoTransport"

namespace PianoTransportImpl
{
	enum class EIcon : uint8 { Restart, Back, Play, Pause, Forward };

	// Transport glyphs drawn as filled triangles (scanlines) and bars, so they never depend on a font.
	class SIcon : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SIcon) {}
			SLATE_ATTRIBUTE(EIcon, Icon)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs) { Icon = InArgs._Icon; }
		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(16.0, 14.0); }

		virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool) const override
		{
			const FLinearColor Ink = PianoStyle::Hex(TEXT("1A120A"));
			const FVector2f S = Geometry.GetLocalSize();
			auto Triangle = [&](float X0, float X1, bool bRight)
			{
				// filled isosceles triangle pointing left or right, X0..X1 wide, full height
				for (float Y = 0.5f; Y < S.Y; Y += 1.f)
				{
					const float T = 1.f - FMath::Abs(Y - S.Y * 0.5f) / (S.Y * 0.5f);
					const float Tip = bRight ? X0 + (X1 - X0) * T : X1 - (X1 - X0) * T;
					FSlateDrawElement::MakeLines(Out, Layer, Geometry.ToPaintGeometry(), { FVector2f(bRight ? X0 : Tip, Y), FVector2f(bRight ? Tip : X1, Y) },
						ESlateDrawEffect::None, Ink, false, 1.f);
				}
			};
			auto Bar = [&](float X, float W)
			{
				FSlateDrawElement::MakeBox(Out, Layer, Geometry.ToPaintGeometry(FVector2f(W, S.Y), FSlateLayoutTransform(FVector2f(X, 0.f))), PianoStyle::White(), ESlateDrawEffect::None, Ink);
			};
			switch (Icon.Get())
			{
			case EIcon::Restart: Bar(1.f, 2.5f); Triangle(4.f, S.X - 1.f, false); break;
			case EIcon::Back: Triangle(0.f, S.X * 0.5f, false); Triangle(S.X * 0.5f, S.X, false); break;
			case EIcon::Play: Triangle(2.f, S.X - 1.f, true); break;
			case EIcon::Pause: Bar(3.f, 3.5f); Bar(S.X - 6.5f, 3.5f); break;
			case EIcon::Forward: Triangle(0.f, S.X * 0.5f, true); Triangle(S.X * 0.5f, S.X, true); break;
			}
			return Layer;
		}

	private:
		TAttribute<EIcon> Icon;
	};

	static FString Clock(double Ms)
	{
		const int32 Seconds = FMath::Max(0, FMath::FloorToInt32(Ms / 1000.0));
		return FString::Printf(TEXT("%d:%02d"), Seconds / 60, Seconds % 60);
	}

	// Progress through the piece: thin track with a gold fill.
	class SProgress : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SProgress) {}
			SLATE_ATTRIBUTE(float, Value)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs) { Value = InArgs._Value; }
		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(80.0, 5.0); }
		virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool) const override
		{
			const FVector2f S = Geometry.GetLocalSize();
			FSlateDrawElement::MakeBox(Out, Layer, Geometry.ToPaintGeometry(), PianoStyle::Rounded(FLinearColor::White, 2.f), ESlateDrawEffect::None, PianoStyle::Hex(TEXT("0A0807")));
			FSlateDrawElement::MakeBox(Out, Layer + 1, Geometry.ToPaintGeometry(FVector2f(S.X * FMath::Clamp(Value.Get(), 0.f, 1.f), S.Y), FSlateLayoutTransform()),
				PianoStyle::Rounded(FLinearColor::White, 2.f), ESlateDrawEffect::None, PianoStyle::Gold);
			return Layer + 1;
		}

	private:
		TAttribute<float> Value;
	};
}

void SPianoTransport::Construct(const FArguments& InArgs)
{
	using namespace PianoTransportImpl;
	Player = InArgs._Player;
	TAttribute<FText> Subtitle = InArgs._Subtitle;
	TAttribute<FText> Title = InArgs._Title;

	auto TransportButton = [this](EIcon Icon, TFunction<void()> OnClick, TAttribute<EIcon> Dynamic = TAttribute<EIcon>())
	{
		return SNew(SBox).Padding(FMargin(2.f, 0.f))
			[
				SNew(SButton)
				.ButtonStyle(FCoreStyle::Get(), "NoBorder")
				.IsFocusable(false)
				.OnClicked_Lambda([OnClick]() { OnClick(); return FReply::Handled(); })
				[
					SNew(SBorder)
					.BorderImage(PianoStyle::Rounded(FLinearColor::White, 5.f))
					.BorderBackgroundColor(PianoStyle::Hex(TEXT("D8D0C2")))
					.Padding(FMargin(12.f, 8.f))
					[
						SNew(SIcon).Icon(Dynamic.IsSet() ? Dynamic : TAttribute<EIcon>(Icon))
					]
				]
			];
	};
	auto Wood = []()
	{
		return SNew(SBox).WidthOverride(16.f)[SNew(SBorder).BorderImage(PianoStyle::Wood())];
	};

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(PianoStyle::White())
		.BorderBackgroundColor(PianoStyle::Hex(TEXT("100D0B")))
		.Padding(0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[Wood()]
			// the piece and where we are in it
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(8.f, 7.f, 4.f, 7.f))
			[
				SNew(SPianoPlate).Padding(FMargin(18.f, 8.f)).ContentHAlign(HAlign_Fill)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Font(PianoStyle::Font(14, TEXT("Bold"))).ColorAndOpacity(PianoStyle::Text).Text(Title)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 1.f, 0.f, 7.f))
					[
						SNew(STextBlock).Font(PianoStyle::Font(10)).ColorAndOpacity(PianoStyle::TextDim).Text(Subtitle)
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SProgress).Value_Lambda([this]()
						{
							const UPianoPlayer* P = Player.Get();
							return P && P->GetScore().IsValid() ? float(P->GetSongMs() / FMath::Max(1.0, P->GetScore()->GetLengthMs())) : 0.f;
						})
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 3.f, 0.f, 0.f))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.f)
						[
							SNew(STextBlock).Font(PianoStyle::Font(9, TEXT("Bold"))).ColorAndOpacity(PianoStyle::TextDim)
							.Text_Lambda([this]() { const UPianoPlayer* P = Player.Get(); return FText::FromString(Clock(P ? P->GetSongMs() : 0.0)); })
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(STextBlock).Font(PianoStyle::Font(9, TEXT("Bold"))).ColorAndOpacity(PianoStyle::TextDim)
							.Text_Lambda([this]()
							{
								const UPianoPlayer* P = Player.Get();
								return FText::FromString(Clock(P && P->GetScore().IsValid() ? P->GetScore()->GetLengthMs() : 0.0));
							})
						]
					]
				]
			]
			// measure counter
			+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(4.f, 7.f))
			[
				SNew(SPianoPlate).Label(LOCTEXT("Measure", "MEASURE"))
				[
					SNew(SPianoCounter).Value_Lambda([this]()
					{
						const UPianoPlayer* P = Player.Get();
						return P && P->GetScore().IsValid() ? P->GetScore()->GetMeasureAt(P->GetSongMs()) + 1 : 0;
					})
				]
			]
			// transport
			+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(4.f, 7.f))
			[
				SNew(SPianoPlate).Padding(FMargin(12.f, 10.f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()[TransportButton(EIcon::Restart, [this]() { if (UPianoPlayer* P = Player.Get()) { P->Restart(); } })]
					+ SHorizontalBox::Slot().AutoWidth()[TransportButton(EIcon::Back, [this]() { StepMeasure(-1); })]
					+ SHorizontalBox::Slot().AutoWidth()[TransportButton(EIcon::Play, [this]() { if (UPianoPlayer* P = Player.Get()) { P->TogglePlay(); } },
						TAttribute<EIcon>::CreateLambda([this]() { const UPianoPlayer* P = Player.Get(); return P && P->IsPlaying() ? EIcon::Pause : EIcon::Play; }))]
					+ SHorizontalBox::Slot().AutoWidth()[TransportButton(EIcon::Forward, [this]() { StepMeasure(1); })]
				]
			]
			// speed
			+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(4.f, 7.f))
			[
				SNew(SPianoPlate).Label(LOCTEXT("Speed", "SPEED")).Padding(FMargin(14.f, 6.f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SPianoKnob).Min(0.25f).Max(2.f).Default(1.f)
						.Value_Lambda([this]() { const UPianoPlayer* P = Player.Get(); return P ? P->GetTempoScale() : 1.f; })
						.OnChanged_Lambda([this](float V) { if (UPianoPlayer* P = Player.Get()) { P->SetTempoScale(V); } })
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(8.f, 0.f, 0.f, 0.f))
					[
						SNew(SBox).WidthOverride(40.f)
						[
							SNew(STextBlock).Font(PianoStyle::Font(11, TEXT("Bold"))).ColorAndOpacity(PianoStyle::Text)
							.Text_Lambda([this]() { const UPianoPlayer* P = Player.Get(); return FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt32((P ? P->GetTempoScale() : 1.f) * 100.f))); })
						]
					]
				]
			]
			// loop
			+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(4.f, 7.f, 8.f, 7.f))
			[
				SNew(SPianoPlate).Label(LOCTEXT("Loop", "LOOP")).Padding(FMargin(14.f, 6.f))
				[
					SNew(SButton)
					.ButtonStyle(FCoreStyle::Get(), "NoBorder")
					.IsFocusable(false)
					.OnClicked_Lambda([this]() { ToggleLoop(); return FReply::Handled(); })
					[
						SNew(SBorder)
						.BorderImage(PianoStyle::Rounded(FLinearColor::White, 4.f))
						.BorderBackgroundColor_Lambda([this]() { double A, B; const UPianoPlayer* P = Player.Get(); return P && P->GetLoop(A, B) ? PianoStyle::Gold : PianoStyle::Hex(TEXT("0A0807")); })
						.Padding(FMargin(10.f, 5.f))
						[
							SNew(STextBlock).Font(PianoStyle::Font(9, TEXT("Bold")))
							.ColorAndOpacity_Lambda([this]() { double A, B; const UPianoPlayer* P = Player.Get(); return FSlateColor(P && P->GetLoop(A, B) ? PianoStyle::Hex(TEXT("1A120A")) : PianoStyle::TextDim); })
							.Text_Lambda([this]() { double A, B; const UPianoPlayer* P = Player.Get(); return P && P->GetLoop(A, B) ? LOCTEXT("On", "ON") : LOCTEXT("Off", "OFF"); })
						]
					]
				]
			]
			+ SHorizontalBox::Slot().AutoWidth()[Wood()]
		]
	];
}

void SPianoTransport::StepMeasure(int32 Delta)
{
	UPianoPlayer* P = Player.Get();
	if (!P || !P->GetScore().IsValid())
	{
		return;
	}
	const TSharedPtr<FPianoScore> Score = P->GetScore();
	const int32 Here = Score->GetMeasureAt(P->GetSongMs() + 1.0);
	P->Seek(Score->GetMeasureStartMs(FMath::Clamp(Here + Delta, 0, Score->GetMeasures().Num() - 1)));
}

void SPianoTransport::ToggleLoop()
{
	UPianoPlayer* P = Player.Get();
	if (!P || !P->GetScore().IsValid())
	{
		return;
	}
	double A = 0.0, B = 0.0;
	if (P->GetLoop(A, B))
	{
		P->ClearLoop();
		return;
	}
	// loop the measure we are in (drag on the ruler for more)
	const TSharedPtr<FPianoScore> Score = P->GetScore();
	const int32 Here = Score->GetMeasureAt(P->GetSongMs() + 1.0);
	P->SetLoop(Score->GetMeasureStartMs(Here), Score->GetMeasureEndMs(Here));
	P->Seek(Score->GetMeasureStartMs(Here));
}

#undef LOCTEXT_NAMESPACE
