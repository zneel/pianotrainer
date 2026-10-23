#include "SPianoSidebar.h"

#include "PianoStyle.h"
#include "PianoViewSettings.h"
#include "PianoWidgets.h"
#include "SPianoPlanPanel.h"

#include "PianoDrills.h"
#include "PianoLibrary.h"
#include "PianoPlayer.h"
#include "PianoScore.h"

#include "Misc/ConfigCacheIni.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "PianoSidebar"

namespace PianoSidebarImpl
{
	static const TCHAR* Section = TEXT("PianoTrainer.View");
}

void FPianoViewSettings::Load()
{
	using namespace PianoSidebarImpl;
	GConfig->GetBool(Section, TEXT("Sheet"), bShowSheet, GGameUserSettingsIni);
	GConfig->GetBool(Section, TEXT("Highway"), bShowHighway, GGameUserSettingsIni);
	GConfig->GetBool(Section, TEXT("Sparks"), bSparks, GGameUserSettingsIni);
	GConfig->GetBool(Section, TEXT("ColorByHand"), bColorByHand, GGameUserSettingsIni);
	GConfig->GetFloat(Section, TEXT("LookaheadMs"), LookaheadMs, GGameUserSettingsIni);
	GConfig->GetInt(Section, TEXT("CountInBars"), CountInBars, GGameUserSettingsIni);
	GConfig->GetFloat(Section, TEXT("LatencyMs"), LatencyMs, GGameUserSettingsIni);
}

void FPianoViewSettings::Save() const
{
	using namespace PianoSidebarImpl;
	GConfig->SetBool(Section, TEXT("Sheet"), bShowSheet, GGameUserSettingsIni);
	GConfig->SetBool(Section, TEXT("Highway"), bShowHighway, GGameUserSettingsIni);
	GConfig->SetBool(Section, TEXT("Sparks"), bSparks, GGameUserSettingsIni);
	GConfig->SetBool(Section, TEXT("ColorByHand"), bColorByHand, GGameUserSettingsIni);
	GConfig->SetFloat(Section, TEXT("LookaheadMs"), LookaheadMs, GGameUserSettingsIni);
	GConfig->SetInt(Section, TEXT("CountInBars"), CountInBars, GGameUserSettingsIni);
	GConfig->SetFloat(Section, TEXT("LatencyMs"), LatencyMs, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

void SPianoSidebar::Construct(const FArguments& InArgs)
{
	FString TabName; // -sidebartab=practice|plan|visual: open on that tab (screenshots)
	if (FParse::Value(FCommandLine::Get(), TEXT("-sidebartab="), TabName))
	{
		Tab = TabName == TEXT("plan") ? 1 : TabName == TEXT("visual") ? 2 : 0;
	}
	Player = InArgs._Player;
	Settings = InArgs._Settings;
	Entry = InArgs._Entry;
	Coach = InArgs._Coach;
	Title = InArgs._Title;

	auto TabButton = [this](const FText& Label, int32 Index)
	{
		return PianoUI::Button(Label, [this, Index]() { Tab = Index; }, PianoUI::EButton::Chip, TAttribute<bool>::CreateLambda([this, Index]() { return Tab == Index; }), true, 10);
	};

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(PianoStyle::White())
		.BorderBackgroundColor(PianoStyle::Panel)
		.Padding(FMargin(16.f, 12.f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(SBorder).BorderImage(PianoStyle::Rounded(PianoStyle::Background, 13.f)).Padding(3.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()[TabButton(LOCTEXT("Practice", "Practice"), 0)]
					+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(3.f, 0.f, 0.f, 0.f))[TabButton(LOCTEXT("Plan", "Plan"), 1)]
					+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(3.f, 0.f, 0.f, 0.f))[TabButton(LOCTEXT("Visual", "Visual"), 2)]
				]
			]
			+ SVerticalBox::Slot().FillHeight(1.f).Padding(FMargin(0.f, 6.f, 0.f, 0.f))
			[
				SNew(SWidgetSwitcher)
				.WidgetIndex_Lambda([this]() { return Tab; })
				+ SWidgetSwitcher::Slot()[SNew(SScrollBox) + SScrollBox::Slot()[MakePracticeTab()]]
				+ SWidgetSwitcher::Slot()[SNew(SScrollBox) + SScrollBox::Slot()[SNew(SPianoPlanPanel).Player(Player).Coach(Coach).Title(Title)]]
				+ SWidgetSwitcher::Slot()[SNew(SScrollBox) + SScrollBox::Slot()[MakeVisualTab()]]
			]
		]
	];
}

TSharedRef<SWidget> SPianoSidebar::Switch(TFunction<bool()> Get, TFunction<void()> Toggle)
{
	return SNew(SPianoSwitch)
		.IsOn_Lambda([Get]() { return Get(); })
		.OnToggled_Lambda([Toggle]() { Toggle(); });
}

TSharedRef<SWidget> SPianoSidebar::MakePracticeTab()
{
	using namespace PianoUI;
	auto P = [this]() { return Player.Get(); };
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	TSharedRef<SVerticalBox> Practice = SNew(SVerticalBox);
	Box->AddSlot().AutoHeight()[Practice];

	Practice->AddSlot().AutoHeight()[SectionLabel(LOCTEXT("Guided", "GUIDED PRACTICE"))];
	Practice->AddSlot().AutoHeight()
	[
		Row(LOCTEXT("Mode", "MODE"), Segmented({ LOCTEXT("Wait", "Wait"), LOCTEXT("Along", "Play along"), LOCTEXT("Listen", "Listen") },
			[P]() { const UPianoPlayer* X = P(); return !X ? 0 : X->GetListenMode() ? 2 : X->GetWaitMode() ? 0 : 1; },
			[P](int32 I) { if (UPianoPlayer* X = P()) { X->SetListenMode(I == 2); if (I < 2) { X->SetWaitMode(I == 0); } } }))
	];
	Practice->AddSlot().AutoHeight()
	[
		Row(LOCTEXT("Hand", "HAND"), Segmented({ LOCTEXT("Right", "Right"), LOCTEXT("Left", "Left"), LOCTEXT("Both", "Both") },
			[P]() { const UPianoPlayer* X = P(); return !X ? 2 : X->GetHands() == EPianoHand::Right ? 0 : X->GetHands() == EPianoHand::Left ? 1 : 2; },
			[P](int32 I) { if (UPianoPlayer* X = P()) { X->SetHands(I == 0 ? EPianoHand::Right : I == 1 ? EPianoHand::Left : EPianoHand::Both); } }))
	];
	Practice->AddSlot().AutoHeight()
	[
		Row(LOCTEXT("Echo", "MY KEYS SOUND"), Switch([P]() { const UPianoPlayer* X = P(); return X && X->GetEchoLiveKeys(); },
			[P]() { if (UPianoPlayer* X = P()) { X->SetEchoLiveKeys(!X->GetEchoLiveKeys()); } }))
	];
	Practice->AddSlot().AutoHeight()
	[
		Row(LOCTEXT("Latency", "LATENCY"),
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(110.f)
				[
					SNew(SPianoSlider).Min(0.f).Max(200.f).Step(5.f)
					.Value_Lambda([this]() { return Settings->LatencyMs; })
					.OnChanged_Lambda([this](float V) { Settings->LatencyMs = V; if (UPianoPlayer* X = Player.Get()) { X->SetInputLatencyMs(V); } Settings->Save(); })
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(6.f, 0.f, 0.f, 0.f))
			[
				SNew(SBox).WidthOverride(44.f)
				[
					SNew(STextBlock).Font(PianoStyle::Font(9, TEXT("Bold"))).ColorAndOpacity(PianoStyle::Text)
					.Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("%d ms"), FMath::RoundToInt32(Settings->LatencyMs))); })
				]
			])
	];

	Practice->AddSlot().AutoHeight()[Separator()];
	Practice->AddSlot().AutoHeight()[SectionLabel(LOCTEXT("CountIn", "COUNT-IN"))];
	Practice->AddSlot().AutoHeight()
	[
		Row(LOCTEXT("Before", "BEFORE PLAYING"), Segmented({ LOCTEXT("Off", "Off"), LOCTEXT("OneBar", "1 bar"), LOCTEXT("TwoBars", "2 bars") },
			[this]() { return Settings->CountInBars; },
			[this](int32 I) { Settings->CountInBars = I; if (UPianoPlayer* X = Player.Get()) { X->SetCountInBars(I); } Settings->Save(); }))
	];

	Practice->AddSlot().AutoHeight()[Separator()];
	Practice->AddSlot().AutoHeight()[SectionLabel(LOCTEXT("Drills", "DRILLS · AFTER CORTOT"))];
	Practice->AddSlot().AutoHeight()[MakeDrills()];

	Practice->AddSlot().AutoHeight()[Separator()];
	Practice->AddSlot().AutoHeight()[SectionLabel(LOCTEXT("Section", "SECTION"))];
	Practice->AddSlot().AutoHeight()[MakeSection()];

	Box->AddSlot().AutoHeight()[Separator()];
	Box->AddSlot().AutoHeight()[SectionLabel(LOCTEXT("Tracks", "TRACKS"))];
	for (const EPianoHand Hand : { EPianoHand::Right, EPianoHand::Left })
	{
		Box->AddSlot().AutoHeight().Padding(FMargin(0.f, 3.f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 8.f, 0.f))
			[
				SNew(SBox).WidthOverride(10.f).HeightOverride(10.f)
				[
					SNew(SBorder).BorderImage(PianoStyle::Rounded(FLinearColor::White, 5.f)).BorderBackgroundColor(Hand == EPianoHand::Right ? PianoStyle::Gold : PianoStyle::Copper)
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Font(PianoStyle::Font(9, TEXT("Bold"))).ColorAndOpacity(PianoStyle::Text)
					.Text(Hand == EPianoHand::Right ? LOCTEXT("RightHand", "Right hand") : LOCTEXT("LeftHand", "Left hand"))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Font(PianoStyle::Font(8)).ColorAndOpacity(PianoStyle::TextDim)
					.Text_Lambda([this, Hand]()
					{
						const UPianoPlayer* X = Player.Get();
						int32 Count = 0;
						if (X)
						{
							for (const FPianoSessionNote& N : X->GetPlayNotes())
							{
								Count += (N.Hand == Hand && !N.bTied) ? 1 : 0;
							}
						}
						const bool bYou = X && (uint8(X->GetHands()) & uint8(Hand)) != 0 && !X->GetListenMode();
						return FText::Format(LOCTEXT("TrackInfo", "{0} notes · {1}"), Count, bYou ? LOCTEXT("You", "you play") : LOCTEXT("App", "the piano plays"));
					})
				]
			]
		];
	}
	return Box;
}

TSharedRef<SWidget> SPianoSidebar::MakeDrills()
{
	using namespace PianoUI;
	TSharedRef<SWrapBox> Chips = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4.0, 4.0));
	for (int32 Index = 0; Index < int32(EPianoDrill::Count); ++Index)
	{
		const EPianoDrill Drill = EPianoDrill(Index);
		Chips->AddSlot()
		[
			Button(TAttribute<FText>::CreateLambda([this, Drill]()
				{
					const FPianoLibraryEntry* E = Entry ? Entry() : nullptr;
					const bool bSuggested = E && E->Drills.Contains(Drill);
					return bSuggested ? FText::Format(LOCTEXT("Suggested", "★ {0}"), FPianoDrills::GetName(Drill)) : FPianoDrills::GetName(Drill);
				}),
				[this, Drill]() { if (UPianoPlayer* X = Player.Get()) { X->SetDrill(Drill); } },
				EButton::Chip, TAttribute<bool>::CreateLambda([this, Drill]() { const UPianoPlayer* X = Player.Get(); return X && X->GetDrill() == Drill; }), true, 9)
		];
	}
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock).AutoWrapText(true).Font(PianoStyle::Font(9)).ColorAndOpacity(PianoStyle::TextDim)
			.Text_Lambda([this]()
			{
				const FPianoLibraryEntry* E = Entry ? Entry() : nullptr;
				return E && !E->Focus.IsEmpty() ? E->Focus : LOCTEXT("DrillIntro", "Practise a hard passage in other rhythms and groupings before playing it as written. Loop a few measures, then step through the drills.");
			})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 8.f, 0.f, 0.f))[Chips]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 8.f, 0.f, 0.f))
		[
			SNew(SBorder).BorderImage(PianoStyle::Rounded(PianoStyle::GoldShade, 6.f)).Padding(FMargin(10.f, 7.f))
			[
				SNew(STextBlock).AutoWrapText(true).Font(PianoStyle::Font(9)).ColorAndOpacity(PianoStyle::GoldGlow)
				.Text_Lambda([this]()
				{
					const UPianoPlayer* X = Player.Get();
					if (!X)
					{
						return FText::GetEmpty();
					}
					const FPianoStats& S = X->GetStats();
					if ((X->GetDrill() == EPianoDrill::AccentThree || X->GetDrill() == EPianoDrill::AccentFour) && S.AccentHits > 0 && S.PlainHits > 0)
					{
						return FText::Format(LOCTEXT("AccentStat", "{0}  Your accents: {1} louder (MIDI velocity)."), FPianoDrills::GetHint(X->GetDrill()),
							FText::AsNumber(FMath::RoundToInt32(S.AccentContrast())));
					}
					return FPianoDrills::GetHint(X->GetDrill());
				})
			]
		];
}

TSharedRef<SWidget> SPianoSidebar::MakeSection()
{
	using namespace PianoUI;
	auto NumMeasures = [this]() { const UPianoPlayer* X = Player.Get(); return X && X->GetScore().IsValid() ? FMath::Max(1, X->GetScore()->GetMeasures().Num()) : 1; };
	auto LoopMeasures = [this](int32& OutFirst, int32& OutLast)
	{
		const UPianoPlayer* X = Player.Get();
		double A = 0.0, B = 0.0;
		if (!X || !X->GetScore().IsValid() || !X->GetLoop(A, B))
		{
			return false;
		}
		OutFirst = X->GetScore()->GetMeasureAt(A + 1.0);
		OutLast = X->GetScore()->GetMeasureAt(B - 1.0);
		return true;
	};
	auto SetLoopMeasures = [this](int32 First, int32 Last)
	{
		UPianoPlayer* X = Player.Get();
		if (!X || !X->GetScore().IsValid())
		{
			return;
		}
		const TSharedPtr<FPianoScore> Score = X->GetScore();
		First = FMath::Clamp(First, 0, Score->GetMeasures().Num() - 1);
		Last = FMath::Clamp(Last, First, Score->GetMeasures().Num() - 1);
		X->SetLoop(Score->GetMeasureStartMs(First), Score->GetMeasureEndMs(Last));
	};
	auto MeasureRow = [=](const FText& Label, bool bStart)
	{
		return Row(Label,
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(110.f)
				[
					SNew(SPianoSlider).Min(1.f).Max_Lambda([=]() { return float(NumMeasures()); }).Step(1.f)
					.Value_Lambda([=]()
					{
						int32 First = 0, Last = 0;
						return LoopMeasures(First, Last) ? float((bStart ? First : Last) + 1) : (bStart ? 1.f : float(NumMeasures()));
					})
					.OnChanged_Lambda([=](float V)
					{
						int32 First = 0, Last = NumMeasures() - 1;
						LoopMeasures(First, Last);
						(bStart ? First : Last) = FMath::RoundToInt32(V) - 1;
						SetLoopMeasures(FMath::Min(First, Last), FMath::Max(First, Last));
					})
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(6.f, 0.f, 0.f, 0.f))
			[
				SNew(SBox).WidthOverride(30.f)
				[
					SNew(STextBlock).Font(PianoStyle::Font(9, TEXT("Bold"))).ColorAndOpacity(PianoStyle::Text)
					.Text_Lambda([=]()
					{
						int32 First = 0, Last = 0;
						return LoopMeasures(First, Last) ? FText::AsNumber((bStart ? First : Last) + 1) : FText::FromString(TEXT("—"));
					})
				]
			]);
	};
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			Row(LOCTEXT("LoopSection", "LOOP SECTION"), Switch([LoopMeasures]() { int32 A, B; return LoopMeasures(A, B); },
				[this, LoopMeasures, SetLoopMeasures]()
				{
					UPianoPlayer* X = Player.Get();
					int32 A, B;
					if (!X || !X->GetScore().IsValid())
					{
						return;
					}
					if (LoopMeasures(A, B))
					{
						X->ClearLoop();
					}
					else
					{
						const int32 Here = X->GetScore()->GetMeasureAt(X->GetSongMs() + 1.0);
						SetLoopMeasures(Here, Here + 3); // four measures from here
					}
				}))
		]
		+ SVerticalBox::Slot().AutoHeight()[MeasureRow(LOCTEXT("From", "FROM MEASURE"), true)]
		+ SVerticalBox::Slot().AutoHeight()[MeasureRow(LOCTEXT("To", "TO MEASURE"), false)];
}

TSharedRef<SWidget> SPianoSidebar::MakeVisualTab()
{
	using namespace PianoUI;
	auto Setting = [this](bool FPianoViewSettings::* Member)
	{
		return Switch([this, Member]() { return Settings->*Member; }, [this, Member]() { Settings->*Member = !(Settings->*Member); Settings->Save(); });
	};
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[SectionLabel(LOCTEXT("View", "VIEW"))]
		+ SVerticalBox::Slot().AutoHeight()[Row(LOCTEXT("Sheet", "SHEET MUSIC"), Setting(&FPianoViewSettings::bShowSheet))]
		+ SVerticalBox::Slot().AutoHeight()[Row(LOCTEXT("Falling", "FALLING NOTES"), Setting(&FPianoViewSettings::bShowHighway))]
		+ SVerticalBox::Slot().AutoHeight()[Row(LOCTEXT("Sparks", "SPARKS"), Setting(&FPianoViewSettings::bSparks))]
		+ SVerticalBox::Slot().AutoHeight()[Row(LOCTEXT("ByHand", "COLOUR BY HAND"), Setting(&FPianoViewSettings::bColorByHand))]
		+ SVerticalBox::Slot().AutoHeight()
		[
			Row(LOCTEXT("NoteSpeed", "NOTES FALL FOR"),
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(100.f)
					[
						SNew(SPianoSlider).Min(1200.f).Max(5000.f).Step(100.f)
						.Value_Lambda([this]() { return Settings->LookaheadMs; })
						.OnChanged_Lambda([this](float V) { Settings->LookaheadMs = V; Settings->Save(); })
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(6.f, 0.f, 0.f, 0.f))
				[
					SNew(SBox).WidthOverride(40.f)
					[
						SNew(STextBlock).Font(PianoStyle::Font(9, TEXT("Bold"))).ColorAndOpacity(PianoStyle::Text)
						.Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("%.1f s"), Settings->LookaheadMs / 1000.f)); })
					]
				])
		]
		+ SVerticalBox::Slot().AutoHeight()[Separator()]
		+ SVerticalBox::Slot().AutoHeight()[SectionLabel(LOCTEXT("Sound", "SOUND & INPUT"))]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock).AutoWrapText(true).Font(PianoStyle::Font(9)).ColorAndOpacity(PianoStyle::TextDim)
			.Text_Lambda([this]()
			{
				const UPianoPlayer* X = Player.Get();
				if (!X)
				{
					return FText::GetEmpty();
				}
				const FString Credit = X->GetSoundState() == UPianoPlayer::ESoundState::Loading ? FString(TEXT("Loading the piano…")) : X->GetSoundCredit();
				const TArray<FString>& Devices = X->GetMidiDeviceNames();
				const FString Input = Devices.Num() > 0 ? FString::Printf(TEXT("MIDI: %s"), *FString::Join(Devices, TEXT(", ")))
					: FString(TEXT("No MIDI keyboard: play A W S E D F T G Y H U J K on the computer keyboard (Z / X octave, Left Shift pedal)."));
				return FText::FromString(Credit + TEXT("\n\n") + Input);
			})
		];
}

#undef LOCTEXT_NAMESPACE
