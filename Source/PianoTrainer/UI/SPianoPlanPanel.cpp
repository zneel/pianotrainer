#include "SPianoPlanPanel.h"

#include "PianoCoach.h"
#include "PianoStyle.h"
#include "PianoWidgets.h"

#include "PianoPlayer.h"
#include "PianoScore.h"

#include "Rendering/DrawElements.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "PianoPlanPanel"

namespace PianoPlanPanelImpl
{
	static FLinearColor StageColor(EPianoStage Stage)
	{
		switch (Stage)
		{
		case EPianoStage::Learned: return PianoStyle::Gold;
		case EPianoStage::Mastered: return PianoStyle::Good;
		case EPianoStage::Omniscient: return PianoStyle::Hex(TEXT("7DD3FC"));
		default: return PianoStyle::TextFaint;
		}
	}

	static FText StageName(EPianoStage Stage)
	{
		switch (Stage)
		{
		case EPianoStage::Learned: return LOCTEXT("Learned", "learned");
		case EPianoStage::Mastered: return LOCTEXT("Mastered", "mastered");
		case EPianoStage::Omniscient: return LOCTEXT("Omniscient", "never forgotten");
		default: return LOCTEXT("New", "new");
		}
	}

	// One dot per pass: green clean, red not.
	class SPassDots : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SPassDots) {}
			SLATE_ARGUMENT(FPianoCoach*, Coach)
		SLATE_END_ARGS()
		void Construct(const FArguments& InArgs) { Coach = InArgs._Coach; }
		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(240.0, 12.0); }
		virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool) const override
		{
			if (!Coach)
			{
				return Layer;
			}
			// green right, red not; a pass with one hand alone sits in a ring of that hand's colour
			const TArray<int8>& Dots = Coach->GetDots();
			for (int32 I = 0; I < Dots.Num(); ++I)
			{
				const uint8 Hands = uint8(Dots[I] >> 1) & 3;
				const FLinearColor Result = (Dots[I] & 1) ? PianoStyle::Good : PianoStyle::Bad;
				if (Hands == 1 || Hands == 2)
				{
					FSlateDrawElement::MakeBox(Out, Layer, Geometry.ToPaintGeometry(FVector2f(11.f, 11.f), FSlateLayoutTransform(FVector2f(I * 13.f, 0.f))),
						PianoStyle::Rounded(FLinearColor::White, 5.5f), ESlateDrawEffect::None, PianoStyle::HandColor(Hands));
					FSlateDrawElement::MakeBox(Out, Layer + 1, Geometry.ToPaintGeometry(FVector2f(7.f, 7.f), FSlateLayoutTransform(FVector2f(I * 13.f + 2.f, 2.f))),
						PianoStyle::Rounded(FLinearColor::White, 3.5f), ESlateDrawEffect::None, Result);
				}
				else
				{
					FSlateDrawElement::MakeBox(Out, Layer, Geometry.ToPaintGeometry(FVector2f(11.f, 11.f), FSlateLayoutTransform(FVector2f(I * 13.f, 0.f))),
						PianoStyle::Rounded(FLinearColor::White, 5.5f), ESlateDrawEffect::None, Result);
				}
			}
			return Layer + 1;
		}
	private:
		FPianoCoach* Coach = nullptr;
	};
}

void SPianoPlanPanel::Construct(const FArguments& InArgs)
{
	Player = InArgs._Player;
	Coach = InArgs._Coach;
	Title = InArgs._Title;
	ChildSlot[SAssignNew(Body, SBox)[MakeContent()]];
	Built = Signature();
}

uint32 SPianoPlanPanel::Signature() const
{
	// what the static parts depend on: which plan, its sections and their stages, the next session
	FPianoPlan* Plan = Coach ? Coach->GetPlan() : nullptr;
	if (!Plan)
	{
		const UPianoPlayer* P = Player.Get();
		return P ? uint32(P->GetScoreSerial()) * 31u + 7u : 1u;
	}
	uint32 Hash = GetTypeHash(Plan->Piece);
	Hash = HashCombine(Hash, GetTypeHash(Plan->NextSession));
	for (const FPianoSection& S : Plan->Sections)
	{
		Hash = HashCombine(Hash, HashCombine(GetTypeHash(S.FirstBeat), HashCombine(GetTypeHash(uint8(S.Stage)), GetTypeHash(S.LastPractised))));
	}
	return Hash;
}

void SPianoPlanPanel::Tick(const FGeometry& Geometry, const double CurrentTime, const float DeltaTime)
{
	SCompoundWidget::Tick(Geometry, CurrentTime, DeltaTime);
	const uint32 Now = Signature();
	if (Now != Built && Body.IsValid())
	{
		Built = Now;
		Body->SetContent(MakeContent());
	}
}

TSharedRef<SWidget> SPianoPlanPanel::MakeContent()
{
	using namespace PianoUI;
	FPianoPlan* Plan = Coach ? Coach->GetPlan() : nullptr;
	const UPianoPlayer* P = Player.Get();
	if (!P || !P->GetScore().IsValid())
	{
		return SNew(STextBlock).Font(PianoStyle::Font(9)).ColorAndOpacity(PianoStyle::TextDim).AutoWrapText(true).Text(LOCTEXT("NoScore", "Open a piece to plan its practice."));
	}
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	Box->AddSlot().AutoHeight()[SectionLabel(LOCTEXT("PlanLabel", "PRACTICE PLAN · AFTER BERNHARD"))];
	if (!Plan)
	{
		Box->AddSlot().AutoHeight()
		[
			SNew(STextBlock).Font(PianoStyle::Font(9)).ColorAndOpacity(PianoStyle::TextDim).AutoWrapText(true)
			.Text(LOCTEXT("PlanIntro", "Learn the piece in small sections: two bars (one where it is dense), each played into the first beat of the next so they join up. Sessions alternate new sections and joins. The hardest section comes first. Each section: seven times to learn it (or cut it in half), then repeat it right, at most 20 minutes, and stop while it is perfect. Learned today, mastered when it is still perfect another day: each day starts by playing yesterday's sections cold."))
		];
		Box->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(FMargin(0.f, 10.f, 0.f, 0.f))
		[
			Button(LOCTEXT("Make", "Make a plan for this piece"), [this]() { if (Coach) { Coach->MakePlan(Title ? Title().ToString() : FString()); } }, EButton::Primary, false, true, 10)
		];
		return Box;
	}

	// progress
	Box->AddSlot().AutoHeight()
	[
		SNew(STextBlock).Font(PianoStyle::Font(9)).ColorAndOpacity(PianoStyle::TextDim)
		.Text(FText::Format(LOCTEXT("Progress", "{0} sections · {1} learned · {2} mastered"), Plan->Sections.Num(), Plan->Count(EPianoStage::Learned), Plan->Count(EPianoStage::Mastered)))
	];

	// review first: a section learned on an earlier day, played cold before anything new
	const int32 Review = Plan->DueReview(FPianoDiary::Today());
	if (Review != INDEX_NONE)
	{
		Box->AddSlot().AutoHeight().Padding(FMargin(0.f, 8.f, 0.f, 0.f))
		[
			SNew(SBorder).BorderImage(PianoStyle::Rounded(PianoStyle::GoldShade, 8.f, PianoStyle::GoldDeep, 1.f)).Padding(FMargin(12.f, 8.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Font(PianoStyle::Label()).ColorAndOpacity(PianoStyle::Gold).Text(LOCTEXT("ReviewFirst", "FIRST · IS IT STILL THERE?"))
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Font(PianoStyle::Font(11, TEXT("Bold"))).ColorAndOpacity(PianoStyle::Text)
						.Text(FText::Format(LOCTEXT("ReviewWhat", "{0}, learned {1}"), FText::FromString(Plan->Sections[Review].Label), FText::FromString(Plan->Sections[Review].LearnedOn)))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					Button(LOCTEXT("Check", "Play it"), [this, Review]() { if (Coach) { Coach->StartSection(Review); } }, EButton::Primary, false, true, 10)
				]
			]
		];
	}

	// the next session in the schedule
	const TArray<FPianoPlanSession> Schedule = Plan->Schedule();
	const int32 Next = FMath::Clamp(Plan->NextSession, 0, FMath::Max(0, Schedule.Num() - 1));
	if (Schedule.Num() > 0)
	{
		const FString What = Plan->Describe(*P->GetScore(), Schedule[Next]);
		Box->AddSlot().AutoHeight().Padding(FMargin(0.f, 8.f, 0.f, 0.f))
		[
			SNew(SBorder).BorderImage(PianoStyle::Rounded(PianoStyle::PanelRaised, 8.f, PianoStyle::PanelLine, 1.f)).Padding(FMargin(12.f, 8.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Font(PianoStyle::Label()).ColorAndOpacity(PianoStyle::Gold)
						.Text(FText::Format(LOCTEXT("Next", "NEXT · SESSION {0} OF {1}"), Next + 1, Schedule.Num()))
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Font(PianoStyle::Font(11, TEXT("Bold"))).ColorAndOpacity(PianoStyle::Text).Text(FText::FromString(What))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					Button(LOCTEXT("Start", "Start"), [this, Next]() { if (Coach) { Coach->Start(Next); } }, EButton::Primary, false, true, 10)
				]
			]
		];
	}

	// hands separately or together (Bernhard: separately is not obligatory; try together, and if it is not right...)
	Box->AddSlot().AutoHeight().Padding(FMargin(0.f, 8.f, 0.f, 0.f))
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 8.f, 0.f))
		[
			SNew(STextBlock).Font(PianoStyle::Font(8, TEXT("Bold"), 80)).ColorAndOpacity(PianoStyle::TextDim).Text(LOCTEXT("HandsLabel", "HANDS"))
		]
		+ SHorizontalBox::Slot().FillWidth(1.f)
		[
			Segmented({ LOCTEXT("HandsAuto", "Auto"), LOCTEXT("HandsSeparate", "Separate first"), LOCTEXT("HandsTogether", "Together") },
				[this]() { return Coach ? int32(Coach->GetHandsMode()) : 0; },
				[this](int32 Index) { if (Coach) { Coach->SetHandsMode(ECoachHands(Index)); } })
		]
	];
	Box->AddSlot().AutoHeight().Padding(FMargin(0.f, 4.f, 0.f, 0.f))
	[
		SNew(STextBlock).Font(PianoStyle::Font(8)).ColorAndOpacity(PianoStyle::TextFaint).AutoWrapText(true)
		.Text_Lambda([this]()
		{
			const ECoachHands Mode = Coach ? Coach->GetHandsMode() : ECoachHands::Auto;
			return Mode == ECoachHands::Separate ? LOCTEXT("HandsSeparateHint", "Each hand alone, taking turns (three times right, then twice, then once each), then together.")
				: Mode == ECoachHands::Together ? LOCTEXT("HandsTogetherHint", "Both hands from the first time through.")
				: LOCTEXT("HandsAutoHint", "Together first; if that is not right, each hand alone in turns, then together.");
		})
	];

	Box->AddSlot().AutoHeight()[MakeSession()];
	Box->AddSlot().AutoHeight()[Separator()];
	Box->AddSlot().AutoHeight()[SectionLabel(LOCTEXT("Sections", "SECTIONS"))];
	Box->AddSlot().AutoHeight()[MakeSections()];
	return Box;
}

TSharedRef<SWidget> SPianoPlanPanel::MakeSession()
{
	using namespace PianoUI;
	using namespace PianoPlanPanelImpl;
	auto Visible = [this]() { return Coach && (Coach->IsActive() || Coach->GetPhase() == ECoachPhase::Done || Coach->GetPhase() == ECoachPhase::TooBig) ? EVisibility::Visible : EVisibility::Collapsed; };
	auto Tempo = [this](float Value)
	{
		return Button(FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt32(Value * 100.f))), [this, Value]() { if (Coach) { Coach->SetTempo(Value); } }, EButton::Chip,
			TAttribute<bool>::CreateLambda([this, Value]() { const UPianoPlayer* P = Player.Get(); return P && FMath::IsNearlyEqual(P->GetTempoScale(), Value, 0.01f); }), true, 9);
	};
	return SNew(SBox).Padding(FMargin(0.f, 10.f, 0.f, 0.f)).Visibility_Lambda(Visible)
		[
			SNew(SBorder).BorderImage(PianoStyle::Rounded(PianoStyle::GoldShade, 8.f)).Padding(FMargin(12.f, 10.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f)
					[
						SNew(STextBlock).Font(PianoStyle::Font(11, TEXT("Bold"))).ColorAndOpacity(PianoStyle::Text)
						.Text_Lambda([this]() { return Coach ? FText::FromString(Coach->GetLabel()) : FText::GetEmpty(); })
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(STextBlock).Font(PianoStyle::Font(11, TEXT("Bold")))
						.ColorAndOpacity_Lambda([this]() { return FSlateColor(Coach && Coach->GetSeconds() > 18.f * 60.f ? PianoStyle::Bad : PianoStyle::GoldGlow); })
						.Text_Lambda([this]()
						{
							const int32 S = Coach ? FMath::FloorToInt32(Coach->GetSeconds()) : 0;
							return FText::FromString(FString::Printf(TEXT("%d:%02d / 20:00"), S / 60, S % 60));
						})
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 6.f, 0.f, 0.f))
				[
					SNew(STextBlock).Font(PianoStyle::Font(10)).ColorAndOpacity(PianoStyle::GoldGlow).AutoWrapText(true)
					.Text_Lambda([this]() { return Coach ? Coach->GetAdvice() : FText::GetEmpty(); })
				]
				// hands separately: which hand, its streak, the turns so far
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 6.f, 0.f, 0.f))
				[
					SNew(STextBlock).Font(PianoStyle::Font(9, TEXT("Bold")))
					.Visibility_Lambda([this]() { return Coach && (Coach->GetPhase() == ECoachPhase::Hands || !Coach->GetHandTurns().IsEmpty()) ? EVisibility::Visible : EVisibility::Collapsed; })
					.ColorAndOpacity_Lambda([this]() { return FSlateColor(Coach && Coach->GetPhase() == ECoachPhase::Hands ? PianoStyle::HandColor(uint8(Coach->GetHand())) : PianoStyle::TextDim); })
					.Text_Lambda([this]()
					{
						if (!Coach)
						{
							return FText::GetEmpty();
						}
						const FText Turns = FText::FromString(Coach->GetHandTurns().IsEmpty() ? FString() : TEXT("   ") + Coach->GetHandTurns());
						if (Coach->GetPhase() != ECoachPhase::Hands)
						{
							return FText::Format(LOCTEXT("HandsDoneLine", "Hands separately:{0}"), Turns);
						}
						return FText::Format(LOCTEXT("HandsLine", "{0} HAND ALONE{1}"),
							Coach->GetHand() == EPianoHand::Left ? LOCTEXT("LeftCaps", "LEFT") : LOCTEXT("RightCaps", "RIGHT"), Turns);
					})
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 8.f, 0.f, 0.f))[SNew(SPassDots).Coach(Coach)]
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 4.f, 0.f, 0.f))
				[
					SNew(STextBlock).Font(PianoStyle::Font(9)).ColorAndOpacity(PianoStyle::TextDim)
					.Text_Lambda([this]()
					{
						if (Coach && Coach->GetPhase() == ECoachPhase::Hands)
						{
							return FText::Format(LOCTEXT("HandCounts", "right hand {0} times through · left hand {1} · then together"),
								Coach->GetHandPasses(EPianoHand::Right), Coach->GetHandPasses(EPianoHand::Left));
						}
						return Coach ? FText::Format(LOCTEXT("Counts", "{0} times through · {1} right · aim {2}"), Coach->GetPasses(), Coach->GetClean(), Coach->GetTarget()) : FText::GetEmpty();
					})
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 8.f, 0.f, 0.f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 6.f, 0.f))
					[
						SNew(STextBlock).Font(PianoStyle::Font(8, TEXT("Bold"), 80)).ColorAndOpacity(PianoStyle::TextDim).Text(LOCTEXT("Tempo", "TEMPO"))
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.f, 0.f, 3.f, 0.f))[Tempo(0.7f)]
					+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.f, 0.f, 3.f, 0.f))[Tempo(0.85f)]
					+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.f, 0.f, 3.f, 0.f))[Tempo(1.f)]
					+ SHorizontalBox::Slot().AutoWidth()[Tempo(1.1f)]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 8.f, 0.f, 0.f))
				[
					SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4.0, 4.0))
					+ SWrapBox::Slot()
					[
						Button(LOCTEXT("Groups", "Note-groups"), [this]() { if (Coach) { Coach->StartGroups(); } }, EButton::Secondary,
							TAttribute<bool>::CreateLambda([this]() { return Coach && Coach->GetPhase() == ECoachPhase::Groups; }),
							TAttribute<bool>::CreateLambda([this]() { return Coach && Coach->IsActive() && Coach->GetPhase() != ECoachPhase::TooBig; }), 9)
					]
					+ SWrapBox::Slot()
					[
						Button(LOCTEXT("Split", "Cut in half"), [this]() { if (Coach) { Coach->Split(); } }, EButton::Secondary,
							TAttribute<bool>::CreateLambda([this]() { return Coach && Coach->GetPhase() == ECoachPhase::TooBig; }),
							TAttribute<bool>::CreateLambda([this]() { return Coach && !Coach->GetSession().IsJoin() && Coach->GetPhase() != ECoachPhase::Done; }), 9)
					]
					+ SWrapBox::Slot()
					[
						Button(LOCTEXT("End", "End session"), [this]() { if (Coach) { Coach->Stop(); } }, EButton::Ghost, false,
							TAttribute<bool>::CreateLambda([this]() { return Coach && Coach->IsActive(); }), 9)
					]
				]
			]
		];
}

TSharedRef<SWidget> SPianoPlanPanel::MakeSections()
{
	using namespace PianoPlanPanelImpl;
	FPianoPlan* Plan = Coach ? Coach->GetPlan() : nullptr;
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	if (!Plan)
	{
		return List;
	}
	const int32 Hardest = Plan->Hardest();
	for (int32 I = 0; I < Plan->Sections.Num(); ++I)
	{
		const FPianoSection& S = Plan->Sections[I];
		const FString Best = (S.BestTempo > 0.f ? FString::Printf(TEXT(" · best %d%%"), FMath::RoundToInt32(S.BestTempo * 100.f)) : FString())
			+ (I == Hardest ? TEXT(" · hardest") : TEXT(""));
		List->AddSlot().AutoHeight().Padding(FMargin(0.f, 1.f))
		[
			SNew(SButton)
			.ButtonStyle(FCoreStyle::Get(), "NoBorder")
			.IsFocusable(false)
			.OnClicked_Lambda([this, I]() { if (Coach) { Coach->StartSection(I); } return FReply::Handled(); })
			[
				SNew(SBorder)
				.BorderImage(PianoStyle::Rounded(FLinearColor::White, 6.f))
				.BorderBackgroundColor_Lambda([this, I]()
				{
					const bool bCurrent = Coach && Coach->IsActive() && Coach->GetSession().First <= I && I <= Coach->GetSession().Last;
					return bCurrent ? PianoStyle::GoldShade : PianoStyle::PanelRaised;
				})
				.Padding(FMargin(10.f, 5.f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 8.f, 0.f))
					[
						SNew(SBox).WidthOverride(8.f).HeightOverride(8.f)
						[
							SNew(SBorder).BorderImage(PianoStyle::Rounded(FLinearColor::White, 4.f)).BorderBackgroundColor(StageColor(S.Stage))
						]
					]
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(STextBlock).Font(PianoStyle::Font(10, TEXT("Bold"))).ColorAndOpacity(PianoStyle::Text).Text(FText::FromString(S.Label))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock).Font(PianoStyle::Font(8)).ColorAndOpacity(StageColor(S.Stage))
						.Text(FText::Format(LOCTEXT("Row", "{0}{1}"), StageName(S.Stage), FText::FromString(Best)))
					]
				]
			]
		];
	}
	return List;
}

#undef LOCTEXT_NAMESPACE
