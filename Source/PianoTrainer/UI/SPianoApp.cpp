#include "SPianoApp.h"

#include "PianoCoach.h"
#include "PianoStyle.h"
#include "PianoWidgets.h"
#include "SFallingNotes.h"
#include "SPianoHome.h"
#include "SPianoHud.h"
#include "SPianoKeyboard.h"
#include "SPianoRuler.h"
#include "SPianoSidebar.h"
#include "SPianoTransport.h"
#include "SSheetStrip.h"
#include "../PianoFileDialog.h"

#include "PianoDrills.h"
#include "PianoPlayer.h"
#include "PianoScore.h"

#include "Framework/Application/SlateApplication.h"
#include "Input/DragAndDrop.h"
#include "Misc/ConfigCacheIni.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "PianoApp"

namespace PianoAppImpl
{
	static constexpr float SidebarWidth = 300.f;
	static constexpr float KeyboardAspect = 0.155f; // 3D keyboard height on screen per unit of stage width
}

SPianoApp::SPianoApp() = default;
SPianoApp::~SPianoApp() = default;

void SPianoApp::Construct(const FArguments& InArgs)
{
	Player = InArgs._Player;
	PickPitch = InArgs._PickPitch;
	bMenu = InArgs._StartInMenu;
	Settings.Load();
	GConfig->GetArray(TEXT("PianoTrainer"), TEXT("Recent"), Recent, GGameUserSettingsIni);
	RefreshLibrary();
	Coach = MakeUnique<FPianoCoach>();
	Coach->Init(Player.Get());
	if (UPianoPlayer* P = Player.Get())
	{
		P->SetCountInBars(Settings.CountInBars);
		P->SetInputLatencyMs(Settings.LatencyMs);
	}

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBox).Visibility_Lambda([this]() { return bMenu ? EVisibility::Collapsed : EVisibility::SelfHitTestInvisible; })
			[
				MakePractice()
			]
		]
		+ SOverlay::Slot()
		[
			SAssignNew(Home, SPianoHome)
			.Visibility_Lambda([this]() { return bMenu ? EVisibility::Visible : EVisibility::Collapsed; })
			.Library([this]() -> const TArray<FPianoLibraryEntry>& { return LibraryEntries; })
			.RecentEntries([this]() { return GetRecentEntries(); })
			.OnPractise([this](const FPianoLibraryEntry& Entry) { Practise(Entry); })
			.Coach(Coach.Get())
			.OnPractiseSection([this](const FString& Piece, int32 Section) { PractiseSection(Piece, Section); })
			.OnOpenFile([this]() { ShowOpenDialog(); })
			.OnContinue([this]()
			{
				const UPianoPlayer* P = Player.Get();
				if (P && P->GetScore().IsValid())
				{
					ShowMenu(false);
				}
				else if (Recent.Num() > 0)
				{
					OpenScore(Recent[0]);
				}
			})
			.ContinueLabel([this]()
			{
				const UPianoPlayer* P = Player.Get();
				if (P && P->GetScore().IsValid())
				{
					return GetTitle();
				}
				const FPianoLibraryEntry* E = Recent.Num() > 0 ? LibraryEntries.FindByPredicate([this](const FPianoLibraryEntry& X) { return X.Path == Recent[0]; }) : nullptr;
				return E ? FText::FromString(E->Title) : Recent.Num() > 0 ? FText::FromString(FPaths::GetBaseFilename(Recent[0])) : LOCTEXT("Nothing", "Nothing yet — open a score");
			})
		]
	];
}

TSharedRef<SWidget> SPianoApp::MakeTopBar()
{
	using namespace PianoUI;
	return SNew(SBorder)
		.BorderImage(PianoStyle::White())
		.BorderBackgroundColor(PianoStyle::Panel)
		.Padding(FMargin(12.f, 7.f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				Button(LOCTEXT("Menu", "‹  Menu"), [this]() { ShowMenu(true); }, EButton::Secondary, false, true, 10)
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(FMargin(14.f, 0.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock).Font(PianoStyle::Font(12, TEXT("Bold"))).ColorAndOpacity(PianoStyle::Text).Text(this, &SPianoApp::GetTitle)
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(FMargin(12.f, 0.f, 0.f, 0.f))
				[
					SNew(STextBlock).Font(PianoStyle::Font(10)).ColorAndOpacity(PianoStyle::TextDim).Text(this, &SPianoApp::GetSubtitle)
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 10.f, 0.f))
			[
				SNew(STextBlock).Font(PianoStyle::Font(9)).ColorAndOpacity(PianoStyle::Bad).Text_Lambda([this]() { return FText::FromString(Message); })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 6.f, 0.f))
			[
				Button(LOCTEXT("Open", "Open…"), [this]() { ShowOpenDialog(); }, EButton::Ghost, false, true, 10)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				Button(LOCTEXT("Panel", "Settings"), [this]() { bSidebar = !bSidebar; }, EButton::Ghost, TAttribute<bool>::CreateLambda([this]() { return bSidebar; }), true, 10)
			]
		];
}

TSharedRef<SWidget> SPianoApp::MakePractice()
{
	using namespace PianoAppImpl;
	auto Wood = []() { return SNew(SBox).WidthOverride(12.f)[SNew(SBorder).BorderImage(PianoStyle::Wood())]; };

	TSharedRef<SWidget> StageContent = SNullWidget::NullWidget;
	if (PickPitch)
	{
		// -keyboard3d: transparent, the engine draws the falling notes and the keyboard here
		StageContent = SNew(SBox)
			.HeightOverride_Lambda([this]()
			{
				// without falling notes the stage is just the keyboard's height
				if (bStageFills || !Stage.IsValid())
				{
					return FOptionalSize();
				}
				return FOptionalSize(FMath::Max(80.f, float(Stage->GetCachedGeometry().GetLocalSize().X) * KeyboardAspect));
			});
	}
	else
	{
		// falling notes right above the keyboard, both the same width so every note lands on its key
		StageContent = SNew(SVerticalBox)
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				SNew(SFallingNotes)
				.Visibility_Lambda([this]() { return Settings.bShowHighway ? EVisibility::Visible : EVisibility::Collapsed; })
				.Player(Player)
				.Settings(&Settings)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SAssignNew(Keyboard, SPianoKeyboard).Player(Player)
			];
	}

	TSharedRef<SVerticalBox> Music = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.FillHeight(TAttribute<float>::CreateLambda([this]() { return bStageFills ? 0.55f : 1.f; }))
		[
			SNew(SBox).Visibility_Lambda([this]() { return Settings.bShowSheet ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				SAssignNew(Sheet, SSheetStrip).Player(Player)
			]
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		.Expose(StageSlot)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[Wood()]
			+ SHorizontalBox::Slot().FillWidth(1.f)[SAssignNew(Stage, SBox)[StageContent]]
			+ SHorizontalBox::Slot().AutoWidth()[Wood()]
		];

	TSharedRef<SVerticalBox> Column = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[MakeTopBar()]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			// the count-in and loading notes float over the sheet and the stage
			SNew(SOverlay)
			+ SOverlay::Slot()[Music]
			+ SOverlay::Slot()
			[
				SNew(SPianoHud).Player(Player)
			]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[Wood()]
			+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(SPianoRuler).Player(Player).Coach(Coach.Get())]
			+ SHorizontalBox::Slot().AutoWidth()[Wood()]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SPianoTransport).Player(Player).Title(this, &SPianoApp::GetTitle).Subtitle(this, &SPianoApp::GetSubtitle)
		];

	return SNew(SBorder)
		.BorderImage(PianoStyle::White())
		// with -keyboard3d the engine renders behind the UI and shows through the stage
		.BorderBackgroundColor(PickPitch ? FLinearColor::Transparent : PianoStyle::Background)
		.Padding(0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f)[Column]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox)
				.WidthOverride(SidebarWidth)
				.Visibility_Lambda([this]() { return bSidebar ? EVisibility::Visible : EVisibility::Collapsed; })
				[
					SNew(SPianoSidebar)
					.Player(Player)
					.Settings(&Settings)
					.Entry([this]() { return Current.IsSet() ? &Current.GetValue() : nullptr; })
					.Coach(Coach.Get())
					.Title([this]() { return GetTitle(); })
				]
			]
		];
}

FText SPianoApp::GetTitle() const
{
	const UPianoPlayer* P = Player.Get();
	if (!P || !P->GetScore().IsValid())
	{
		return FText::GetEmpty();
	}
	// the library knows better names than the files (Humdrum titles don't survive the import)
	const FString File = FPianoLibrary::ToScoresRelative(P->GetScore()->GetSourcePath());
	const FPianoLibraryEntry* E = LibraryEntries.FindByPredicate([&](const FPianoLibraryEntry& X) { return X.Path == P->GetScore()->GetSourcePath() || (!File.IsEmpty() && X.File == File); });
	return FText::FromString(E ? E->Title : P->GetScore()->GetTitle());
}

FText SPianoApp::GetSubtitle() const
{
	const UPianoPlayer* P = Player.Get();
	if (!P)
	{
		return FText::GetEmpty();
	}
	FString Text;
	if (P->GetScore().IsValid())
	{
		const FString File = FPianoLibrary::ToScoresRelative(P->GetScore()->GetSourcePath());
		const FPianoLibraryEntry* E = LibraryEntries.FindByPredicate([&](const FPianoLibraryEntry& X) { return !File.IsEmpty() && X.File == File; });
		Text = E ? E->Subtitle : FString();
	}
	if (P->GetDrill() != EPianoDrill::AsWritten)
	{
		Text += (Text.IsEmpty() ? TEXT("") : TEXT("  ·  ")) + FString::Printf(TEXT("Drill: %s"), *FPianoDrills::GetName(P->GetDrill()).ToString());
	}
	const FPianoStats& S = P->GetStats();
	if (S.Hits + S.Wrong + S.Missed > 0)
	{
		Text += FString::Printf(TEXT("%s%d right · %d wrong · %d missed"), Text.IsEmpty() ? TEXT("") : TEXT("  ·  "), S.Hits, S.Wrong, S.Missed);
	}
	return FText::FromString(Text);
}

void SPianoApp::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (Coach)
	{
		Coach->Tick(InDeltaTime);
	}
	if (const UPianoPlayer* P = Player.Get())
	{
		if (P->GetScoreSerial() != SeenScoreSerial && Sheet.IsValid())
		{
			SeenScoreSerial = P->GetScoreSerial();
			Sheet->OnScoreChanged();
		}
	}
	// the stage fills the space when notes fall on it; otherwise it is just the keyboard
	const bool bFill = Settings.bShowHighway;
	if (StageSlot && bFill != bStageFills)
	{
		bStageFills = bFill;
		if (bFill)
		{
			StageSlot->SetFillHeight(1.f);
		}
		else
		{
			StageSlot->SetAutoHeight();
		}
	}
}

bool SPianoApp::GetStageRect(float& OutLeft, float& OutTop, float& OutRight, float& OutBottom) const
{
	if (!Stage.IsValid() || !PickPitch || bMenu)
	{
		return false;
	}
	const FGeometry& App = GetTickSpaceGeometry();
	const FGeometry& Box = Stage->GetTickSpaceGeometry();
	const FVector2f AppPos = FVector2f(App.GetAbsolutePosition()), AppSize = FVector2f(App.GetAbsoluteSize());
	const FVector2f Pos = FVector2f(Box.GetAbsolutePosition()) - AppPos, Size = FVector2f(Box.GetAbsoluteSize());
	if (AppSize.X <= 1.f || AppSize.Y <= 1.f || Size.X <= 1.f || Size.Y <= 1.f)
	{
		return false;
	}
	OutLeft = Pos.X / AppSize.X;
	OutTop = Pos.Y / AppSize.Y;
	OutRight = (Pos.X + Size.X) / AppSize.X;
	OutBottom = (Pos.Y + Size.Y) / AppSize.Y;
	return true;
}

float SPianoApp::GetKeyboardTop() const
{
	float L, T, R, B;
	if (!GetStageRect(L, T, R, B))
	{
		return 0.6f;
	}
	const FVector2f AppSize = FVector2f(GetTickSpaceGeometry().GetAbsoluteSize());
	const float StageW = (R - L) * AppSize.X, StageH = (B - T) * AppSize.Y;
	const float KeyH = FMath::Min(StageH * 0.45f, StageW * PianoAppImpl::KeyboardAspect);
	return B - KeyH / AppSize.Y;
}

void SPianoApp::ShowMenu(bool bShow)
{
	bMenu = bShow;
	if (bShow)
	{
		if (UPianoPlayer* P = Player.Get())
		{
			P->Pause();
		}
		if (Home.IsValid())
		{
			Home->Refresh();
		}
	}
	FSlateApplication::Get().SetKeyboardFocus(SharedThis(this), EFocusCause::SetDirectly);
}

void SPianoApp::RefreshLibrary()
{
	LibraryEntries = FPianoLibrary::Scan(Recent);
}

void SPianoApp::RememberRecent(const FString& Path)
{
	const FString Full = FPaths::ConvertRelativePathToFull(Path);
	Recent.Remove(Full);
	Recent.Insert(Full, 0);
	Recent.SetNum(FMath::Min(Recent.Num(), 12));
	GConfig->SetArray(TEXT("PianoTrainer"), TEXT("Recent"), Recent, GGameUserSettingsIni);
	GConfig->SetString(TEXT("PianoTrainer"), TEXT("LastScore"), *Full, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

bool SPianoApp::OpenScore(const FString& Path)
{
	UPianoPlayer* P = Player.Get();
	if (!P)
	{
		return false;
	}
	FString Error;
	if (!P->LoadScore(Path, Error))
	{
		Message = Error;
		return false;
	}
	Message.Reset();
	const FString File = FPianoLibrary::ToScoresRelative(Path);
	const FPianoLibraryEntry* Entry = LibraryEntries.FindByPredicate([&](const FPianoLibraryEntry& E) { return E.Path == Path || (!File.IsEmpty() && E.File == File); });
	Current = Entry ? TOptional<FPianoLibraryEntry>(*Entry) : TOptional<FPianoLibraryEntry>();
	RememberRecent(Path);
	bMenu = false;
	return true;
}

FPianoLibraryEntry SPianoApp::EntryForPiece(const FString& Piece) const
{
	if (const FPianoLibraryEntry* E = LibraryEntries.FindByPredicate([&Piece](const FPianoLibraryEntry& X) { return X.File == Piece || X.Path == Piece; }))
	{
		return *E;
	}
	FPianoLibraryEntry Entry;
	Entry.Path = FPaths::IsRelative(Piece) ? FPianoLibrary::FromScoresRelative(Piece) : Piece;
	Entry.File = FPianoLibrary::ToScoresRelative(Entry.Path);
	Entry.Title = FPaths::GetBaseFilename(Entry.Path);
	Entry.Drills = { EPianoDrill::LongShort, EPianoDrill::BlockChords, EPianoDrill::AccentFour };
	return Entry;
}

void SPianoApp::PractiseSection(const FString& Piece, int32 Section)
{
	Practise(EntryForPiece(Piece), [this, Section]() { if (Coach) { Coach->StartSection(Section); } });
}

void SPianoApp::Practise(const FPianoLibraryEntry& Entry, TFunction<void()> AfterLoad)
{
	UPianoPlayer* P = Player.Get();
	if (!P)
	{
		return;
	}
	Current = Entry;
	ShowMenu(false);
	Message.Reset();
	const FString Path = Entry.Path;
	TWeakPtr<SPianoApp> Weak = SharedThis(this);
	P->LoadScoreAsync(Path, [Weak, Path, AfterLoad](bool bOk, const FString& Error)
	{
		if (TSharedPtr<SPianoApp> Self = Weak.Pin())
		{
			if (bOk)
			{
				Self->RememberRecent(Path);
				if (UPianoPlayer* X = Self->Player.Get())
				{
					X->SetWaitMode(true);
					X->SetListenMode(false);
				}
				if (AfterLoad)
				{
					AfterLoad();
				}
			}
			else
			{
				Self->Message = Error;
			}
		}
	});
}

TArray<FPianoLibraryEntry> SPianoApp::GetRecentEntries() const
{
	TArray<FPianoLibraryEntry> Out;
	for (const FString& Path : Recent)
	{
		if (const FPianoLibraryEntry* E = LibraryEntries.FindByPredicate([&Path](const FPianoLibraryEntry& X) { return X.Path == Path; }))
		{
			Out.Add(*E);
		}
		else if (FPaths::FileExists(Path))
		{
			FPianoLibraryEntry& Entry = Out.AddDefaulted_GetRef();
			Entry.Path = Path;
			Entry.Title = FPaths::GetBaseFilename(Path);
			Entry.Collection = TEXT("Recent");
			Entry.Drills = { EPianoDrill::LongShort, EPianoDrill::BlockChords, EPianoDrill::AccentFour };
		}
	}
	return Out;
}

void SPianoApp::ShowOpenDialog()
{
	FString Path;
	if (PianoFileDialog::OpenScore(Path))
	{
		FPianoLibraryEntry Entry;
		Entry.Path = Path;
		Entry.File = FPianoLibrary::ToScoresRelative(Path);
		Entry.Title = FPaths::GetBaseFilename(Path);
		Entry.Drills = { EPianoDrill::LongShort, EPianoDrill::BlockChords, EPianoDrill::AccentFour };
		Practise(Entry);
	}
	FSlateApplication::Get().SetKeyboardFocus(SharedThis(this), EFocusCause::SetDirectly);
}

int32 SPianoApp::PickAt(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) const
{
	if (!PickPitch || bMenu)
	{
		return INDEX_NONE;
	}
	// this widget covers the whole viewport: local units x DPI scale = viewport pixels
	const FVector2D Local = FVector2D(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()));
	return PickPitch(Local * MyGeometry.Scale);
}

int32 SPianoApp::ComputerKeyToPitch(const FKey& Key) const
{
	static const TPair<FKey, int32> Map[] = {
		{ EKeys::A, 0 }, { EKeys::W, 1 }, { EKeys::S, 2 }, { EKeys::E, 3 }, { EKeys::D, 4 }, { EKeys::F, 5 },
		{ EKeys::T, 6 }, { EKeys::G, 7 }, { EKeys::Y, 8 }, { EKeys::H, 9 }, { EKeys::U, 10 }, { EKeys::J, 11 },
		{ EKeys::K, 12 }, { EKeys::O, 13 }, { EKeys::L, 14 }, { EKeys::P, 15 }, { EKeys::Semicolon, 16 },
	};
	for (const TPair<FKey, int32>& Entry : Map)
	{
		if (Entry.Key == Key)
		{
			return 60 + ComputerOctave * 12 + Entry.Value;
		}
	}
	return INDEX_NONE;
}

FReply SPianoApp::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	UPianoPlayer* P = Player.Get();
	if (!P)
	{
		return FReply::Unhandled();
	}
	const FKey Key = InKeyEvent.GetKey();

	if (InKeyEvent.IsControlDown() || InKeyEvent.IsCommandDown())
	{
		if (Key == EKeys::O)
		{
			ShowOpenDialog();
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}
	if (Key == EKeys::Escape)
	{
		ShowMenu(!bMenu || !P->GetScore().IsValid());
		return FReply::Handled();
	}
	if (Key == EKeys::LeftShift)
	{
		P->SetSustainPedal(true); // computer-keyboard sustain pedal
		return FReply::Handled();
	}

	const int32 Pitch = ComputerKeyToPitch(Key);
	if (Pitch != INDEX_NONE)
	{
		if (!InKeyEvent.IsRepeat() && !ComputerKeysDown.Contains(Key))
		{
			ComputerKeysDown.Add(Key, Pitch);
			P->NoteOn(Pitch, 96);
		}
		return FReply::Handled();
	}
	if (bMenu)
	{
		return FReply::Unhandled();
	}

	if (Key == EKeys::SpaceBar) { P->TogglePlay(); }
	else if (Key == EKeys::BackSpace) { P->Restart(); }
	else if (Key == EKeys::Up) { P->SetTempoScale(P->GetTempoScale() + 0.05f); }
	else if (Key == EKeys::Down) { P->SetTempoScale(P->GetTempoScale() - 0.05f); }
	else if (Key == EKeys::Left) { P->Seek(P->GetSongMs() - 2000.0); }
	else if (Key == EKeys::Right) { P->Seek(P->GetSongMs() + 2000.0); }
	else if (Key == EKeys::One) { P->SetHands(EPianoHand::Right); }
	else if (Key == EKeys::Two) { P->SetHands(EPianoHand::Left); }
	else if (Key == EKeys::Three) { P->SetHands(EPianoHand::Both); }
	else if (Key == EKeys::Q) { P->SetWaitMode(!P->GetWaitMode()); }
	else if (Key == EKeys::V) { P->SetListenMode(!P->GetListenMode()); }
	else if (Key == EKeys::M) { P->SetEchoLiveKeys(!P->GetEchoLiveKeys()); }
	else if (Key == EKeys::Z) { ComputerOctave = FMath::Max(-3, ComputerOctave - 1); }
	else if (Key == EKeys::X) { ComputerOctave = FMath::Min(3, ComputerOctave + 1); }
	else { return FReply::Unhandled(); }
	return FReply::Handled();
}

FReply SPianoApp::OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::LeftShift)
	{
		if (UPianoPlayer* P = Player.Get())
		{
			P->SetSustainPedal(false);
		}
		return FReply::Handled();
	}
	int32 Pitch = INDEX_NONE;
	if (ComputerKeysDown.RemoveAndCopyValue(InKeyEvent.GetKey(), Pitch))
	{
		if (UPianoPlayer* P = Player.Get())
		{
			P->NoteOff(Pitch);
		}
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SPianoApp::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	TSharedPtr<FExternalDragOperation> Op = DragDropEvent.GetOperationAs<FExternalDragOperation>();
	return (Op.IsValid() && Op->HasFiles()) ? FReply::Handled() : FReply::Unhandled();
}

FReply SPianoApp::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	TSharedPtr<FExternalDragOperation> Op = DragDropEvent.GetOperationAs<FExternalDragOperation>();
	if (Op.IsValid() && Op->HasFiles() && Op->GetFiles().Num() > 0)
	{
		FPianoLibraryEntry Entry;
		Entry.Path = Op->GetFiles()[0];
		Entry.File = FPianoLibrary::ToScoresRelative(Entry.Path);
		Entry.Title = FPaths::GetBaseFilename(Entry.Path);
		Entry.Drills = { EPianoDrill::LongShort, EPianoDrill::BlockChords, EPianoDrill::AccentFour };
		Practise(Entry);
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SPianoApp::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FReply Reply = FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse);
	UPianoPlayer* P = Player.Get();
	const int32 Pitch = MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton ? PickAt(MyGeometry, MouseEvent) : INDEX_NONE;
	if (P && Pitch != INDEX_NONE)
	{
		MouseNote = Pitch;
		P->NoteOn(Pitch, 96);
		Reply.CaptureMouse(SharedThis(this));
	}
	return Reply;
}

FReply SPianoApp::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	UPianoPlayer* P = Player.Get();
	if (!P || !HasMouseCapture())
	{
		return FReply::Unhandled();
	}
	// glissando across the 3D keys
	const int32 Pitch = PickAt(MyGeometry, MouseEvent);
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

FReply SPianoApp::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	UPianoPlayer* P = Player.Get();
	if (MouseNote != INDEX_NONE && P)
	{
		P->NoteOff(MouseNote);
	}
	MouseNote = INDEX_NONE;
	return HasMouseCapture() ? FReply::Handled().ReleaseMouseCapture() : FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
