#include "SPianoHome.h"

#include "PianoCoach.h"
#include "SPianoSettingsPage.h"
#include "PianoStyle.h"
#include "PianoWidgets.h"

#include "PianoDrills.h"
#include "PianoLibrary.h"

#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "PianoHome"

namespace PianoHomeImpl
{
	static const FButtonStyle& CardStyle()
	{
		static FButtonStyle Style = []()
		{
			FButtonStyle S;
			S.SetNormal(*PianoStyle::Rounded(PianoStyle::PanelRaised, 12.f, PianoStyle::PanelLine, 1.f));
			S.SetHovered(*PianoStyle::Rounded(PianoStyle::PanelHover, 12.f, PianoStyle::GoldDeep, 1.f));
			S.SetPressed(*PianoStyle::Rounded(PianoStyle::PanelLine, 12.f, PianoStyle::Gold, 1.f));
			S.SetNormalPadding(FMargin(0.f));
			S.SetPressedPadding(FMargin(0.f));
			return S;
		}();
		return Style;
	}

	static TSharedRef<SWidget> PageTitle(const FText& Title, const FText& Subtitle)
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Font(PianoStyle::Font(24, TEXT("Bold"))).ColorAndOpacity(PianoStyle::Text).Text(Title)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 4.f, 0.f, 18.f))
			[
				SNew(STextBlock).Font(PianoStyle::Font(11)).ColorAndOpacity(PianoStyle::TextDim).AutoWrapText(true).Text(Subtitle)
			];
	}

	static TSharedRef<SWidget> SectionLabel(const FText& Text)
	{
		return SNew(STextBlock).Font(PianoStyle::Label()).ColorAndOpacity(PianoStyle::TextFaint).Text(Text);
	}
}

void SPianoHome::Construct(const FArguments& InArgs)
{
	FString Page; // -menupage=etudes|diary|library: open on that page (screenshots)
	if (FParse::Value(FCommandLine::Get(), TEXT("-menupage="), Page))
	{
		CurrentPage = Page == TEXT("etudes") ? PageEtudes : Page == TEXT("diary") ? PageDiary : Page == TEXT("library") ? PageLibrary
			: Page == TEXT("settings") ? PageSettings : PageHome;
	}
	Library = InArgs._Library;
	RecentEntries = InArgs._RecentEntries;
	OnPractise = InArgs._OnPractise;
	OnOpenFile = InArgs._OnOpenFile;
	OnContinue = InArgs._OnContinue;
	ContinueLabel = InArgs._ContinueLabel;
	Coach = InArgs._Coach;
	SoundCredit = InArgs._SoundCredit;
	Player = InArgs._Player;
	OnPractiseSection = InArgs._OnPractiseSection;
	SelectedEtude = Library().IndexOfByPredicate([](const FPianoLibraryEntry& E) { return E.File.StartsWith(TEXT("Etudes/")); });

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(PianoStyle::White())
		.BorderBackgroundColor(PianoStyle::Background)
		.Padding(0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[MakeRail()]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(SWidgetSwitcher)
				.WidgetIndex_Lambda([this]() { return CurrentPage; })
				+ SWidgetSwitcher::Slot()[SNew(SScrollBox) + SScrollBox::Slot().Padding(FMargin(40.f, 34.f))[MakeHomePage()]]
				+ SWidgetSwitcher::Slot()[MakeEtudesPage()]
				+ SWidgetSwitcher::Slot()[SNew(SScrollBox) + SScrollBox::Slot().Padding(FMargin(40.f, 34.f))[SAssignNew(DiaryBox, SBox)[MakeDiary()]]]
				+ SWidgetSwitcher::Slot()[SNew(SScrollBox) + SScrollBox::Slot().Padding(FMargin(40.f, 34.f))[MakeLibraryPage()]]
				+ SWidgetSwitcher::Slot()[SNew(SScrollBox) + SScrollBox::Slot().Padding(FMargin(40.f, 34.f))[SNew(SPianoSettingsPage).Player(Player)]]
			]
		]
	];
}

void SPianoHome::Refresh()
{
	if (RecentBox.IsValid())
	{
		RecentBox->SetContent(MakeRecent());
	}
	if (DiaryBox.IsValid())
	{
		DiaryBox->SetContent(MakeDiary());
	}
}

TSharedRef<SWidget> SPianoHome::MakeRail()
{
	auto Nav = [this](const FText& Label, int32 Page)
	{
		return SNew(SBox).Padding(FMargin(0.f, 2.f))
			[
				SNew(SButton)
				.ButtonStyle(FCoreStyle::Get(), "NoBorder")
				.IsFocusable(false)
				.OnClicked_Lambda([this, Page]() { CurrentPage = Page; return FReply::Handled(); })
				[
					SNew(SBorder)
					.BorderImage(PianoStyle::Rounded(FLinearColor::White, 8.f))
					.BorderBackgroundColor_Lambda([this, Page]() { return CurrentPage == Page ? PianoStyle::GoldShade : FLinearColor::Transparent; })
					.Padding(FMargin(14.f, 9.f))
					[
						SNew(STextBlock).Font(PianoStyle::Font(12, TEXT("Bold")))
						.ColorAndOpacity_Lambda([this, Page]() { return FSlateColor(CurrentPage == Page ? PianoStyle::Gold : PianoStyle::TextDim); })
						.Text(Label)
					]
				]
			];
	};

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SBox).WidthOverride(236.f)
			[
				SNew(SBorder).BorderImage(PianoStyle::White()).BorderBackgroundColor(PianoStyle::Panel).Padding(FMargin(20.f, 28.f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Font(PianoStyle::Font(16, TEXT("Bold"), 220)).ColorAndOpacity(PianoStyle::Gold).Text(LOCTEXT("Brand", "PIANO TRAINER"))
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 4.f, 0.f, 26.f))
					[
						SNew(STextBlock).Font(PianoStyle::Font(9)).ColorAndOpacity(PianoStyle::TextFaint).Text(LOCTEXT("Tagline", "Learn any piece, a small section at a time."))
					]
					+ SVerticalBox::Slot().AutoHeight()[Nav(LOCTEXT("NavHome", "Home"), PageHome)]
					+ SVerticalBox::Slot().AutoHeight()[Nav(LOCTEXT("NavEtudes", "Études"), PageEtudes)]
					+ SVerticalBox::Slot().AutoHeight()[Nav(LOCTEXT("NavDiary", "Diary"), PageDiary)]
					+ SVerticalBox::Slot().AutoHeight()[Nav(LOCTEXT("NavLibrary", "Library"), PageLibrary)]
					+ SVerticalBox::Slot().AutoHeight()[Nav(LOCTEXT("NavSettings", "Settings"), PageSettings)]
					+ SVerticalBox::Slot().FillHeight(1.f)[SNullWidget::NullWidget]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(FMargin(0.f, 0.f, 0.f, 12.f))
					[
						PianoUI::Button(LOCTEXT("Quit", "Quit"), []() { FPlatformMisc::RequestExit(false, TEXT("Quit from the menu")); }, PianoUI::EButton::Ghost, false, true, 10)
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Font(PianoStyle::Font(8)).ColorAndOpacity(PianoStyle::TextFaint).AutoWrapText(true)
						.Text_Lambda([this]()
						{
							const FText Piano = SoundCredit ? SoundCredit() : FText::GetEmpty();
							return FText::Format(LOCTEXT("Credits", "{0}Études: Chopin first editions, © The Fryderyk Chopin Institute (CC BY 4.0).\nEngraving: Verovio."),
								Piano.IsEmpty() ? FText::GetEmpty() : FText::Format(LOCTEXT("PianoCredit", "{0}.\n"), Piano));
						})
					]
				]
			]
		]
		// walnut edge
		+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(10.f)[SNew(SBorder).BorderImage(PianoStyle::Wood())]];
}

TSharedRef<SWidget> SPianoHome::MakeCard(const FText& Kicker, const FText& Title, const FText& Body, TAttribute<FText> Footer, TFunction<void()> OnClick, bool bFeatured)
{
	return SNew(SBox).MinDesiredHeight(150.f)
		[
			SNew(SButton)
			.ButtonStyle(&PianoHomeImpl::CardStyle())
			.IsFocusable(false)
			.OnClicked_Lambda([OnClick]() { OnClick(); return FReply::Handled(); })
			[
				SNew(SBorder)
				.BorderImage(PianoStyle::Rounded(FLinearColor::White, 12.f))
				.BorderBackgroundColor(bFeatured ? PianoStyle::GoldShade : FLinearColor::Transparent)
				.Padding(FMargin(20.f, 16.f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(PianoStyle::Label()).ColorAndOpacity(PianoStyle::Gold).Text(Kicker)]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 6.f, 0.f, 4.f))[SNew(STextBlock).Font(PianoStyle::Font(16, TEXT("Bold"))).ColorAndOpacity(PianoStyle::Text).Text(Title)]
					+ SVerticalBox::Slot().FillHeight(1.f)[SNew(STextBlock).Font(PianoStyle::Font(10)).ColorAndOpacity(PianoStyle::TextDim).AutoWrapText(true).Text(Body)]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 10.f, 0.f, 0.f))[SNew(STextBlock).Font(PianoStyle::Font(9, TEXT("Bold"))).ColorAndOpacity(PianoStyle::GoldGlow).Text(Footer)]
				]
			]
		];
}

TSharedRef<SWidget> SPianoHome::MakeHomePage()
{
	using namespace PianoHomeImpl;
	TSharedRef<SUniformGridPanel> Grid = SNew(SUniformGridPanel).SlotPadding(FMargin(8.f));
	Grid->AddSlot(0, 0)[MakeCard(LOCTEXT("ContinueK", "CONTINUE"), LOCTEXT("ContinueT", "Back to the piano"),
		LOCTEXT("ContinueB", "Pick up the piece you were practising, where you left it."),
		TAttribute<FText>::CreateLambda([this]() { return ContinueLabel ? ContinueLabel() : FText::GetEmpty(); }), [this]() { if (OnContinue) { OnContinue(); } }, true)];
	Grid->AddSlot(1, 0)[MakeCard(LOCTEXT("EtudesK", "LEARN"), LOCTEXT("EtudesT", "Chopin Études"),
		LOCTEXT("EtudesB", "All 24 of Op. 10 and Op. 25 from the first editions, with practice tools made from the score: outline, chord positions, rhythms, moving accents."),
		LOCTEXT("EtudesF", "24 études"), [this]() { CurrentPage = PageEtudes; })];
	Grid->AddSlot(2, 0)[MakeCard(LOCTEXT("OpenK", "YOUR MUSIC"), LOCTEXT("OpenT", "Open a score"),
		LOCTEXT("OpenB", "MusicXML from MuseScore, MEI, Humdrum or ABC. Drop a file anywhere, or browse."),
		LOCTEXT("OpenF", "Browse…"), [this]() { if (OnOpenFile) { OnOpenFile(); } })];

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[PageTitle(LOCTEXT("HomeTitle", "What shall we play?"), LOCTEXT("HomeSub", "Practise a piece with its score scrolling above the keyboard: the music waits for you, plays the other hand, and loops the hard bars. Make a plan and learn it section by section, the way Bernhard taught on Piano Street."))]
		+ SVerticalBox::Slot().AutoHeight()[Grid]
		+ SVerticalBox::Slot().AutoHeight()[SAssignNew(RecentBox, SBox)[MakeRecent()]];
}

TSharedRef<SWidget> SPianoHome::MakeRecent()
{
	using namespace PianoHomeImpl;
	const TArray<FPianoLibraryEntry> Recent = RecentEntries ? RecentEntries() : TArray<FPianoLibraryEntry>();
	if (Recent.Num() == 0)
	{
		return SNullWidget::NullWidget;
	}
	TSharedRef<SUniformGridPanel> Row = SNew(SUniformGridPanel).SlotPadding(FMargin(8.f));
	for (int32 I = 0; I < FMath::Min(4, Recent.Num()); ++I)
	{
		const FPianoLibraryEntry Entry = Recent[I];
		Row->AddSlot(I, 0)[MakeCard(FText::FromString(Entry.Collection.ToUpper()), FText::FromString(Entry.Title),
			FText::FromString(Entry.Subtitle), LOCTEXT("PractiseF", "Practise"), [this, Entry]() { if (OnPractise) { OnPractise(Entry); } })];
	}
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(8.f, 26.f, 0.f, 4.f))[SectionLabel(LOCTEXT("Recent", "PRACTISED RECENTLY"))]
		+ SVerticalBox::Slot().AutoHeight()[Row];
}

TSharedRef<SWidget> SPianoHome::MakeDiary()
{
	using namespace PianoHomeImpl;
	TSharedRef<SVerticalBox> Page = SNew(SVerticalBox);
	Page->AddSlot().AutoHeight()[PageTitle(LOCTEXT("DiaryTitle", "Practice diary"),
		LOCTEXT("DiarySub", "Every session is written here. Plan your work, then work your plan: small sections, at least three practice days a week, and come back the next day to see what stayed."))];
	if (!Coach)
	{
		return Page;
	}
	const FPianoDiary& Diary = Coach->GetDiary();
	// the library's name for a piece ("Étude in E major · Op. 10 No. 3 · Tristesse"), else the one the plan was made with
	auto NameOf = [this](const FString& Piece, const FString& Fallback)
	{
		const FPianoLibraryEntry* E = Library ? Library().FindByPredicate([&Piece](const FPianoLibraryEntry& X) { return !X.File.IsEmpty() && X.File == Piece; }) : nullptr;
		return E ? (E->Subtitle.IsEmpty() ? E->Title : E->Title + TEXT(" · ") + E->Subtitle) : Fallback;
	};
	const FString Today = FPianoDiary::Today();
	const FDateTime Now = FDateTime::Now();
	const FString ThreeDaysAgo = (Now - FTimespan::FromDays(3)).ToString(TEXT("%Y-%m-%d"));
	const FString MonthAgo = (Now - FTimespan::FromDays(30)).ToString(TEXT("%Y-%m-%d"));
	const FString WeekAgo = (Now - FTimespan::FromDays(7)).ToString(TEXT("%Y-%m-%d"));

	// this week
	const int32 Days = Diary.DaysThisWeek();
	Page->AddSlot().AutoHeight()
	[
		SNew(SBorder).BorderImage(PianoStyle::Rounded(Days >= 3 ? PianoStyle::GoldShade : PianoStyle::PanelRaised, 10.f, PianoStyle::PanelLine, 1.f)).Padding(FMargin(18.f, 12.f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 16.f, 0.f))
			[
				SNew(STextBlock).Font(PianoStyle::Font(26, TEXT("Bold"))).ColorAndOpacity(Days >= 3 ? PianoStyle::Gold : PianoStyle::Text).Text(FText::AsNumber(Days))
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Font(PianoStyle::Font(10)).ColorAndOpacity(PianoStyle::TextDim).AutoWrapText(true)
				.Text(FText::Format(LOCTEXT("Week", "{0}|plural(one=practice day,other=practice days) in the last seven ({1} minutes today). Bernhard's minimum is three sessions a week."), Days, FMath::RoundToInt32(Diary.MinutesOn(Today))))
			]
		]
	];

	// due: next-day checks, play-throughs, month-later relearning
	struct FDue { FString Piece; int32 Section; FText Text; };
	TArray<FDue> Due;
	for (const FPianoPlan& Plan : Diary.GetPlans())
	{
		bool bAllMastered = Plan.Sections.Num() > 0;
		FString Latest;
		for (int32 I = 0; I < Plan.Sections.Num(); ++I)
		{
			const FPianoSection& S = Plan.Sections[I];
			bAllMastered &= S.Stage >= EPianoStage::Mastered;
			Latest = S.LastPractised > Latest ? S.LastPractised : Latest;
			if (S.RepeatOf != INDEX_NONE)
			{
				continue; // a passage that comes back is due with its original
			}
			if (S.Stage == EPianoStage::Learned && !S.LearnedOn.IsEmpty() && S.LearnedOn <= WeekAgo)
			{
				// Bernhard never saw a right-sized section take more than seven days to master
				Due.Add({ Plan.Piece, I, FText::Format(LOCTEXT("DueTooBig", "A week and not yet mastered: {0} of {1} is too big. Cut it in half."), FText::FromString(S.Label), FText::FromString(NameOf(Plan.Piece, Plan.Title))) });
			}
			else if (S.Stage == EPianoStage::Learned && !S.LearnedOn.IsEmpty() && S.LearnedOn < Today)
			{
				Due.Add({ Plan.Piece, I, FText::Format(LOCTEXT("DueCheck", "Is it still there?  {0} of {1}, learned {2}"), FText::FromString(S.Label), FText::FromString(NameOf(Plan.Piece, Plan.Title)), FText::FromString(S.LearnedOn)) });
			}
			else if (S.Stage == EPianoStage::Mastered && !S.LastPractised.IsEmpty() && S.LastPractised < ThreeDaysAgo)
			{
				Due.Add({ Plan.Piece, I, FText::Format(LOCTEXT("DuePlay", "Play it through:  {0} of {1} (mastered)"), FText::FromString(S.Label), FText::FromString(NameOf(Plan.Piece, Plan.Title))) });
			}
		}
		if (bAllMastered && !Latest.IsEmpty() && Latest < MonthAgo)
		{
			Due.Add({ Plan.Piece, 0, FText::Format(LOCTEXT("DueRelearn", "A month away from {0}: relearn it from scratch — each time it comes back faster, until it is never forgotten"), FText::FromString(NameOf(Plan.Piece, Plan.Title))) });
		}
	}
	Page->AddSlot().AutoHeight().Padding(FMargin(0.f, 22.f, 0.f, 6.f))[SectionLabel(LOCTEXT("Due", "DUE TODAY"))];
	if (Due.Num() == 0)
	{
		Page->AddSlot().AutoHeight()[SNew(STextBlock).Font(PianoStyle::Font(10)).ColorAndOpacity(PianoStyle::TextFaint).Text(LOCTEXT("NothingDue", "Nothing due. Make a plan for a piece in the practice screen's Plan tab."))];
	}
	for (int32 I = 0; I < FMath::Min(12, Due.Num()); ++I)
	{
		const FDue Item = Due[I];
		Page->AddSlot().AutoHeight().Padding(FMargin(0.f, 2.f))
		[
			SNew(SButton)
			.ButtonStyle(&CardStyle())
			.IsFocusable(false)
			.OnClicked_Lambda([this, Item]() { if (OnPractiseSection) { OnPractiseSection(Item.Piece, Item.Section); } return FReply::Handled(); })
			[
				SNew(SBox).Padding(FMargin(14.f, 8.f))[SNew(STextBlock).Font(PianoStyle::Font(10, TEXT("Bold"))).ColorAndOpacity(PianoStyle::Text).Text(Item.Text)]
			]
		];
	}

	// plans
	Page->AddSlot().AutoHeight().Padding(FMargin(0.f, 22.f, 0.f, 6.f))[SectionLabel(LOCTEXT("Plans", "PLANS"))];
	for (const FPianoPlan& Plan : Diary.GetPlans())
	{
		const FString Piece = Plan.Piece;
		Page->AddSlot().AutoHeight().Padding(FMargin(0.f, 2.f))
		[
			SNew(SButton)
			.ButtonStyle(&CardStyle())
			.IsFocusable(false)
			.OnClicked_Lambda([this, Piece]() { if (OnPractiseSection) { OnPractiseSection(Piece, INDEX_NONE); } return FReply::Handled(); })
			[
				SNew(SBox).Padding(FMargin(14.f, 8.f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(STextBlock).Font(PianoStyle::Font(11, TEXT("Bold"))).ColorAndOpacity(PianoStyle::Text).Text(FText::FromString(NameOf(Plan.Piece, Plan.Title)))]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(STextBlock).Font(PianoStyle::Font(9)).ColorAndOpacity(PianoStyle::GoldGlow)
						.Text(FText::Format(LOCTEXT("PlanRow", "{0} sections · {1} learned · {2} mastered · session {3} of {4}"), Plan.Sections.Num(),
							Plan.Count(EPianoStage::Learned), Plan.Count(EPianoStage::Mastered), FMath::Min(Plan.NextSession + 1, Plan.Schedule().Num()), Plan.Schedule().Num()))
					]
				]
			]
		];
	}

	// recent sessions
	Page->AddSlot().AutoHeight().Padding(FMargin(0.f, 22.f, 0.f, 6.f))[SectionLabel(LOCTEXT("Sessions", "RECENT SESSIONS"))];
	FString Lines;
	const TArray<FPianoDiaryEntry>& Entries = Diary.GetEntries();
	for (int32 I = Entries.Num() - 1, Shown = 0; I >= 0 && Shown < 15; --I, ++Shown)
	{
		const FPianoDiaryEntry& E = Entries[I];
		const FString Time = E.Minutes < 1.f ? FString::Printf(TEXT("%d s"), FMath::RoundToInt32(E.Minutes * 60.f)) : FString::Printf(TEXT("%.0f min"), E.Minutes);
		Lines += FString::Printf(TEXT("%s   %s · %s   %s · %d of %d right · %d%%   %s\n"), *E.Date, *NameOf(E.Piece, E.Title), *E.What, *Time, E.Clean, E.Passes,
			FMath::RoundToInt32(E.Tempo * 100.f), *E.Result);
	}
	Page->AddSlot().AutoHeight()
	[
		SNew(STextBlock).Font(PianoStyle::Font(10)).ColorAndOpacity(PianoStyle::TextDim).Text(FText::FromString(Lines.IsEmpty() ? FString(TEXT("No sessions yet.")) : Lines))
	];
	return Page;
}

TSharedRef<SWidget> SPianoHome::MakeEtudesPage()
{
	using namespace PianoHomeImpl;
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	const TArray<FPianoLibraryEntry>& Entries = Library();
	FString LastOpus;
	TSharedPtr<SWrapBox> Row;
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const FPianoLibraryEntry& E = Entries[Index];
		if (!E.File.StartsWith(TEXT("Etudes/")))
		{
			continue;
		}
		const int32 NoAt = E.Subtitle.Find(TEXT(" No."));
		const FString Opus = NoAt != INDEX_NONE ? E.Subtitle.Left(NoAt) : E.Subtitle;
		if (Opus != LastOpus)
		{
			LastOpus = Opus;
			List->AddSlot().AutoHeight().Padding(FMargin(0.f, 14.f, 0.f, 8.f))[SectionLabel(FText::FromString(Opus.ToUpper()))];
			List->AddSlot().AutoHeight()[SAssignNew(Row, SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(8.0, 8.0))];
		}
		// "No. 1 · Waterfall" over "C major"
		const FString Number = NoAt != INDEX_NONE ? E.Subtitle.Mid(NoAt + 1) : E.Subtitle;
		Row->AddSlot()
		[
			SNew(SBox).WidthOverride(176.f).HeightOverride(76.f)
			[
				SNew(SButton)
				.ButtonStyle(&CardStyle())
				.IsFocusable(false)
				.OnClicked_Lambda([this, Index]() { SelectedEtude = Index; return FReply::Handled(); })
				[
					SNew(SBorder)
					.BorderImage(PianoStyle::Rounded(FLinearColor::White, 12.f))
					.BorderBackgroundColor_Lambda([this, Index]() { return SelectedEtude == Index ? PianoStyle::GoldShade : FLinearColor::Transparent; })
					.Padding(FMargin(14.f, 10.f))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(PianoStyle::Font(12, TEXT("Bold"))).ColorAndOpacity(PianoStyle::Text).Text(FText::FromString(Number))]
						+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 3.f, 0.f, 0.f))[SNew(STextBlock).Font(PianoStyle::Font(9)).ColorAndOpacity(PianoStyle::TextDim).Text(FText::FromString(E.Title.Replace(TEXT("Étude in "), TEXT(""))))]
					]
				]
			]
		];
	}

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot().Padding(FMargin(40.f, 34.f, 20.f, 34.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[PageTitle(LOCTEXT("EtudesTitle", "Chopin · Études"), LOCTEXT("EtudesSub", "First editions (Schlesinger, Lemoine), encoded by the Fryderyk Chopin Institute. Pick one to see what it trains."))]
				+ SVerticalBox::Slot().AutoHeight()[List]
			]
		]
		+ SHorizontalBox::Slot().AutoWidth()[MakeEtudeDetail()];
}

TSharedRef<SWidget> SPianoHome::MakeEtudeDetail()
{
	auto Selected = [this]() -> const FPianoLibraryEntry*
	{
		const TArray<FPianoLibraryEntry>& Entries = Library();
		return Entries.IsValidIndex(SelectedEtude) ? &Entries[SelectedEtude] : nullptr;
	};
	return SNew(SBox).WidthOverride(330.f)
		[
			SNew(SBorder).BorderImage(PianoStyle::White()).BorderBackgroundColor(PianoStyle::Panel).Padding(FMargin(24.f, 34.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Font(PianoStyle::Label()).ColorAndOpacity(PianoStyle::Gold)
					.Text_Lambda([Selected]() { const FPianoLibraryEntry* E = Selected(); return E ? FText::FromString(E->Subtitle.ToUpper()) : FText::GetEmpty(); })
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 6.f, 0.f, 12.f))
				[
					SNew(STextBlock).Font(PianoStyle::Font(20, TEXT("Bold"))).ColorAndOpacity(PianoStyle::Text).AutoWrapText(true)
					.Text_Lambda([Selected]() { const FPianoLibraryEntry* E = Selected(); return E ? FText::FromString(E->Title) : FText::GetEmpty(); })
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Font(PianoStyle::Font(10)).ColorAndOpacity(PianoStyle::TextDim).AutoWrapText(true)
					.Text_Lambda([Selected]() { const FPianoLibraryEntry* E = Selected(); return E ? E->Focus : FText::GetEmpty(); })
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 18.f, 0.f, 6.f))
				[
					PianoHomeImpl::SectionLabel(LOCTEXT("DrillsFor", "TOOLS THAT SUIT THIS ÉTUDE"))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Font(PianoStyle::Font(10)).ColorAndOpacity(PianoStyle::GoldGlow).AutoWrapText(true)
					.Text_Lambda([Selected]()
					{
						const FPianoLibraryEntry* E = Selected();
						TArray<FString> Names;
						if (E)
						{
							for (EPianoDrill D : E->Drills)
							{
								Names.Add(FPianoDrills::GetName(D).ToString());
							}
						}
						return FText::FromString(FString::Join(Names, TEXT("\n")));
					})
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 10.f, 0.f, 0.f))
				[
					SNew(STextBlock).Font(PianoStyle::Font(9)).ColorAndOpacity(PianoStyle::TextFaint).AutoWrapText(true)
					.Text(LOCTEXT("PlanNote", "Make a plan in the practice screen's Plan tab and learn the étude section by section, hardest first. When a spot will not go, the Practice tab has a tool for what is going wrong; the tools are made from the score."))
				]
				+ SVerticalBox::Slot().FillHeight(1.f)[SNullWidget::NullWidget]
				+ SVerticalBox::Slot().AutoHeight()
				[
					PianoUI::Button(LOCTEXT("PractiseIt", "Practise this étude"), [this, Selected]() { if (const FPianoLibraryEntry* E = Selected()) { if (OnPractise) { OnPractise(*E); } } }, PianoUI::EButton::Primary, false, true, 11)
				]
			]
		];
}

TSharedRef<SWidget> SPianoHome::MakeLibraryPage()
{
	using namespace PianoHomeImpl;
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	for (const FPianoLibraryEntry& E : Library())
	{
		if (E.File.StartsWith(TEXT("Etudes/")))
		{
			continue;
		}
		const FPianoLibraryEntry Copy = E;
		List->AddSlot().AutoHeight().Padding(FMargin(0.f, 3.f))
		[
			SNew(SButton)
			.ButtonStyle(&CardStyle())
			.IsFocusable(false)
			.OnClicked_Lambda([this, Copy]() { if (OnPractise) { OnPractise(Copy); } return FReply::Handled(); })
			[
				SNew(SBox).Padding(FMargin(16.f, 10.f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(STextBlock).Font(PianoStyle::Font(12, TEXT("Bold"))).ColorAndOpacity(PianoStyle::Text).Text(FText::FromString(E.Title))]
					+ SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock).Font(PianoStyle::Font(9)).ColorAndOpacity(PianoStyle::TextFaint).Text(FText::FromString(E.Collection + (E.Subtitle.IsEmpty() ? FString() : TEXT(" · ") + E.Subtitle)))]
				]
			]
		];
	}
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[PageTitle(LOCTEXT("LibraryTitle", "Library"), LOCTEXT("LibrarySub", "Samples that come with the app and the scores you opened. Drop a MusicXML file on the window to open it."))]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(FMargin(0.f, 0.f, 0.f, 14.f))
		[
			PianoUI::Button(LOCTEXT("Browse", "Open a score…"), [this]() { if (OnOpenFile) { OnOpenFile(); } }, PianoUI::EButton::Primary, false, true, 12)
		]
		+ SVerticalBox::Slot().AutoHeight()[List];
}

#undef LOCTEXT_NAMESPACE
