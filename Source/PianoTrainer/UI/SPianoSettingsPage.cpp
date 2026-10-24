#include "SPianoSettingsPage.h"

#include "../PianoFileDialog.h"
#include "PianoStyle.h"
#include "PianoWidgets.h"

#include "PianoPlayer.h"

#include "Engine/Engine.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericWindow.h"
#include "GameFramework/GameUserSettings.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "PianoSettingsPage"

namespace PianoSettingsImpl
{
	static UGameUserSettings* Display() { return GEngine ? GEngine->GetGameUserSettings() : nullptr; }

	// window mode as shown: 0 window, 1 borderless full screen, 2 full screen
	static int32 ShownMode(EWindowMode::Type Mode)
	{
		return Mode == EWindowMode::Windowed ? 0 : Mode == EWindowMode::WindowedFullscreen ? 1 : 2;
	}

	static TSharedRef<SWidget> Hint(const TAttribute<FText>& Text)
	{
		return SNew(STextBlock).Font(PianoStyle::Font(9)).ColorAndOpacity(PianoStyle::TextFaint).AutoWrapText(true).Text(Text);
	}

	static TSharedRef<SWidget> Card(const FText& Title, const TSharedRef<SWidget>& Body)
	{
		return SNew(SBorder).BorderImage(PianoStyle::Rounded(PianoStyle::PanelRaised, 10.f, PianoStyle::PanelLine, 1.f)).Padding(FMargin(18.f, 14.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))[PianoUI::SectionLabel(Title)]
				+ SVerticalBox::Slot().AutoHeight()[Body]
			];
	}

	static TSharedRef<SWidget> Switch(TFunction<bool()> Get, TFunction<void()> Toggle)
	{
		return SNew(SPianoSwitch).IsOn_Lambda([Get]() { return Get(); }).OnToggled_Lambda([Toggle]() { Toggle(); });
	}
}

void SPianoSettingsPage::Construct(const FArguments& InArgs)
{
	using namespace PianoSettingsImpl;
	Player = InArgs._Player;
	Settings.Load();
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock).Font(PianoStyle::Font(24, TEXT("Bold"))).ColorAndOpacity(PianoStyle::Text).Text(LOCTEXT("Title", "Settings"))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 4.f, 0.f, 18.f))
		[
			SNew(STextBlock).Font(PianoStyle::Font(11)).ColorAndOpacity(PianoStyle::TextDim).AutoWrapText(true)
			.Text(LOCTEXT("Sub", "Changes apply at once and are kept for next time."))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(0.f, 0.f, 10.f, 0.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[Card(LOCTEXT("Sound", "SOUND"), MakeSound())]
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 12.f, 0.f, 0.f))[Card(LOCTEXT("Playback", "PLAYBACK"), MakePlayback())]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(10.f, 0.f, 0.f, 0.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[Card(LOCTEXT("Midi", "MIDI KEYBOARD"), MakeMidi())]
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 12.f, 0.f, 0.f))[Card(LOCTEXT("Display", "DISPLAY"), MakeDisplay())]
			]
		]
	];
}

void SPianoSettingsPage::Changed()
{
	Settings.Save();
	Settings.Apply(Player.Get());
}

void SPianoSettingsPage::Tick(const FGeometry& Geometry, const double CurrentTime, const float DeltaTime)
{
	SCompoundWidget::Tick(Geometry, CurrentTime, DeltaTime);
	// keyboards come and go; the resolution list depends on the window mode
	const UPianoPlayer* P = Player.Get();
	if (P && P->GetMidiDeviceNames() != ShownDevices && DevicesBox.IsValid())
	{
		ShownDevices = P->GetMidiDeviceNames();
		DevicesBox->SetContent(MakeDevices());
	}
	const UGameUserSettings* Display = PianoSettingsImpl::Display();
	const int32 Mode = Display ? PianoSettingsImpl::ShownMode(Display->GetFullscreenMode()) : 0;
	if (Mode != ShownWindowMode && ResolutionsBox.IsValid())
	{
		ShownWindowMode = Mode;
		ResolutionsBox->SetContent(MakeResolutions());
	}
}

TSharedRef<SWidget> SPianoSettingsPage::MakeDisplay()
{
	using namespace PianoSettingsImpl;
	using namespace PianoUI;
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			Row(LOCTEXT("Window", "WINDOW"), Segmented({ LOCTEXT("Windowed", "Window"), LOCTEXT("Borderless", "Borderless"), LOCTEXT("Fullscreen", "Full screen") },
				[]() { const UGameUserSettings* D = Display(); return D ? ShownMode(D->GetFullscreenMode()) : 0; },
				[](int32 Index)
				{
					if (UGameUserSettings* D = Display())
					{
						const EWindowMode::Type Mode = Index == 0 ? EWindowMode::Windowed : Index == 1 ? EWindowMode::WindowedFullscreen : EWindowMode::Fullscreen;
						D->SetFullscreenMode(Mode);
						if (Mode != EWindowMode::Windowed)
						{
							D->SetScreenResolution(D->GetDesktopResolution()); // full screen at the screen's own size
						}
						D->ApplyResolutionSettings(false);
						D->SaveSettings();
					}
				}))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 6.f, 0.f, 2.f))
		[
			SNew(STextBlock).Font(PianoStyle::Label()).ColorAndOpacity(PianoStyle::TextDim).Text(LOCTEXT("Resolution", "RESOLUTION"))
		]
		+ SVerticalBox::Slot().AutoHeight()[SAssignNew(ResolutionsBox, SBox)[MakeResolutions()]]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 8.f, 0.f, 0.f))
		[
			Row(LOCTEXT("VSync", "VSYNC"), Switch([this]() { return Settings.bVSync; }, [this]() { Settings.bVSync = !Settings.bVSync; Settings.Save(); Settings.ApplyVSync(); }))
		]
		+ SVerticalBox::Slot().AutoHeight()[Hint(LOCTEXT("VSyncHint", "Keep it on: without it the sheet music judders as it scrolls."))];
}

TSharedRef<SWidget> SPianoSettingsPage::MakeResolutions()
{
	using namespace PianoSettingsImpl;
	TArray<FIntPoint> Sizes;
	const UGameUserSettings* D = Display();
	if (D && D->GetFullscreenMode() == EWindowMode::Windowed)
	{
		UKismetSystemLibrary::GetConvenientWindowedResolutions(Sizes);
	}
	else
	{
		UKismetSystemLibrary::GetSupportedFullscreenResolutions(Sizes);
	}
	// the larger common sizes, largest first
	Sizes = Sizes.FilterByPredicate([](const FIntPoint& S) { return S.X >= 1280 && S.Y >= 720; });
	Sizes.Sort([](const FIntPoint& A, const FIntPoint& B) { return A.X * A.Y > B.X * B.Y; });
	TArray<FIntPoint> Unique;
	for (const FIntPoint& S : Sizes)
	{
		if (!Unique.Contains(S) && Unique.Num() < 10)
		{
			Unique.Add(S);
		}
	}
	TSharedRef<SWrapBox> Chips = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4.0, 4.0));
	for (const FIntPoint& S : Unique)
	{
		Chips->AddSlot()
		[
			PianoUI::Button(FText::FromString(FString::Printf(TEXT("%d × %d"), S.X, S.Y)),
				[S]()
				{
					if (UGameUserSettings* G = Display())
					{
						G->SetScreenResolution(S);
						G->ApplyResolutionSettings(false);
						G->SaveSettings();
					}
				},
				PianoUI::EButton::Chip, TAttribute<bool>::CreateLambda([S]() { const UGameUserSettings* G = Display(); return G && G->GetScreenResolution() == S; }), true, 9)
		];
	}
	return Chips;
}

TSharedRef<SWidget> SPianoSettingsPage::MakeSound()
{
	using namespace PianoSettingsImpl;
	using namespace PianoUI;
	auto Status = [this]()
	{
		const UPianoPlayer* P = Player.Get();
		if (!P)
		{
			return FText::GetEmpty();
		}
		if (!P->GetPluginError().IsEmpty())
		{
			return FText::Format(LOCTEXT("PluginFailed", "{0} did not load ({1}); the piano samples play instead."),
				FText::FromString(FPaths::GetBaseFilename(P->GetInstrumentPath())), FText::FromString(P->GetPluginError()));
		}
		switch (P->GetSoundState())
		{
		case UPianoPlayer::ESoundState::Loading: return LOCTEXT("Loading", "Loading the piano…");
		case UPianoPlayer::ESoundState::Ready: return FText::FromString(P->GetSoundCredit());
		default: return LOCTEXT("NoPiano", "No piano samples: a plain tone plays for now.");
		}
	};
	const TArray<FString> Folders = UPianoPlayer::GetInstrumentFolders();
	TSharedRef<SWrapBox> Found = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4.0, 4.0));
	TArray<FString> Instruments = UPianoPlayer::FindInstruments();
	const TArray<FString> Plugins = UPianoPlayer::FindPlugins();
	Instruments.Append(Plugins);
	if (const UPianoPlayer* P = Player.Get(); P && !P->GetInstrumentPath().IsEmpty())
	{
		Instruments.AddUnique(P->GetInstrumentPath()); // chosen from elsewhere
	}
	for (const FString& Path : Instruments)
	{
		Found->AddSlot()
		[
			Button(FText::FromString(FPaths::GetBaseFilename(Path)), [this, Path]()
				{
					Settings.InstrumentPath = Path;
					Settings.Save();
					if (UPianoPlayer* P = Player.Get())
					{
						P->SetInstrumentPath(Path);
					}
				},
				EButton::Chip, TAttribute<bool>::CreateLambda([this, Path]() { const UPianoPlayer* P = Player.Get(); return P && P->GetInstrumentPath() == Path; }), true, 9)
		];
	}
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock).Font(PianoStyle::Font(11, TEXT("Bold"))).ColorAndOpacity(PianoStyle::Text).AutoWrapText(true).Text_Lambda(Status)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 4.f, 0.f, 0.f))
		[
			Hint(FText::Format(LOCTEXT("Where", "The piano is Piano in 162 by Ivy Audio, a free Steinway Model B library (ivyaudio.com). It may not be passed on, so the app does not include it: download the SFZ version, unzip it into {0} and it is found, or choose its .sfz below."),
				FText::FromString(Folders.Num() > 1 ? Folders[1] : Folders[0])))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 8.f, 0.f, 0.f))[Found]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 6.f, 0.f, 0.f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.f, 0.f, 6.f, 0.f))
			[
				Button(LOCTEXT("Choose", "Choose an instrument…"), [this]()
				{
					FString Path;
					if (PianoFileDialog::OpenInstrument(Path))
					{
						Settings.InstrumentPath = Path;
						Settings.Save();
						if (UPianoPlayer* P = Player.Get())
						{
							P->SetInstrumentPath(Path);
						}
					}
				}, EButton::Secondary, false, true, 10)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.f, 0.f, 6.f, 0.f))
			[
				SNew(SBox).Visibility_Lambda([this]() { const UPianoPlayer* P = Player.Get(); return P && P->IsPluginPlaying() ? EVisibility::Visible : EVisibility::Collapsed; })
				[
					// the plug-in's own window: its instruments, presets, room...
					Button(LOCTEXT("PluginWindow", "Open its window"), [this]()
					{
						UPianoPlayer* P = Player.Get();
						const TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(AsShared());
						if (P && !P->OpenInstrumentWindow(Window && Window->GetNativeWindow() ? Window->GetNativeWindow()->GetOSWindowHandle() : nullptr))
						{
							PluginWindowNote = LOCTEXT("NoPluginWindow", "This plug-in has no window of its own.");
						}
					}, EButton::Primary, false, true, 10)
				]
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				Button(LOCTEXT("LookAgain", "Look again"), [this]()
				{
					Settings.InstrumentPath.Reset();
					Settings.Save();
					if (UPianoPlayer* P = Player.Get())
					{
						P->SetInstrumentPath(FString());
					}
				}, EButton::Ghost, false, true, 10)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 6.f, 0.f, 0.f))
		[
			Hint(TAttribute<FText>::CreateLambda([this, NumPlugins = Plugins.Num()]()
			{
				return !PluginWindowNote.IsEmpty() ? PluginWindowNote : FText::Format(LOCTEXT("PluginsHint",
					"VST 3 instruments on this computer are listed too ({0} found): what you choose in a plug-in's own window (its instrument, preset, room) is kept for next time. A plug-in runs inside the app, so add only ones you trust."),
					FText::AsNumber(NumPlugins));
			}))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 10.f, 0.f, 0.f))
		[
			Row(LOCTEXT("Volume", "VOLUME"),
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(140.f)
					[
						SNew(SPianoSlider).Min(-24.f).Max(6.f).Step(1.f)
						.Value_Lambda([this]() { return Settings.VolumeDb; })
						.OnChanged_Lambda([this](float V) { Settings.VolumeDb = V; Changed(); })
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(8.f, 0.f, 0.f, 0.f))
				[
					SNew(SBox).WidthOverride(48.f)
					[
						SNew(STextBlock).Font(PianoStyle::Font(9, TEXT("Bold"))).ColorAndOpacity(PianoStyle::Text)
						.Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("%+.0f dB"), Settings.VolumeDb)); })
					]
				])
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			Row(LOCTEXT("Quality", "SAMPLE QUALITY"), Segmented({ LOCTEXT("Standard", "Standard"), LOCTEXT("High", "High"), LOCTEXT("Best", "Best") },
				[this]() { return Settings.SampleQuality >= 8 ? 2 : Settings.SampleQuality >= 4 ? 1 : 0; },
				[this](int32 Index) { Settings.SampleQuality = Index == 2 ? 8 : Index == 1 ? 4 : 2; Changed(); }))
		]
		+ SVerticalBox::Slot().AutoHeight()[Hint(LOCTEXT("QualityHint", "How the samples are pitched between recorded notes. High and Best (windowed sinc) take two to four times the processor time: if fast pedalled pieces crackle, use Standard."))];
}

TSharedRef<SWidget> SPianoSettingsPage::MakeDevices()
{
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	const UPianoPlayer* P = Player.Get();
	const TArray<FString> Names = P ? P->GetMidiDeviceNames() : TArray<FString>();
	if (Names.Num() == 0)
	{
		List->AddSlot().AutoHeight()
		[
			SNew(STextBlock).Font(PianoStyle::Font(10)).ColorAndOpacity(PianoStyle::TextDim).AutoWrapText(true)
			.Text(LOCTEXT("NoKeyboard", "No MIDI keyboard found. Plug one in (USB or a MIDI interface): it is picked up within a few seconds. Until then the computer keys play (A W S E D F … = C4 upwards)."))
		];
	}
	for (int32 Device = 0; Device < Names.Num(); ++Device)
	{
		List->AddSlot().AutoHeight().Padding(FMargin(0.f, 2.f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 8.f, 0.f))
			[
				SNew(SBox).WidthOverride(10.f).HeightOverride(10.f)
				[
					SNew(SBorder).BorderImage(PianoStyle::Rounded(FLinearColor::White, 5.f))
					.BorderBackgroundColor_Lambda([this, Device]()
					{
						const UPianoPlayer* X = Player.Get();
						return X && X->GetMidiDeviceIdleSeconds(Device) < 0.4 ? PianoStyle::Good : PianoStyle::PanelLine;
					})
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Font(PianoStyle::Font(10, TEXT("Bold"))).ColorAndOpacity(PianoStyle::Text).Text(FText::FromString(Names[Device]))
			]
		];
	}
	return List;
}

TSharedRef<SWidget> SPianoSettingsPage::MakeMidi()
{
	using namespace PianoSettingsImpl;
	using namespace PianoUI;
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[SAssignNew(DevicesBox, SBox)[MakeDevices()]]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(FMargin(0.f, 6.f, 0.f, 4.f))
		[
			Button(LOCTEXT("Rescan", "Look for keyboards again"), [this]() { if (UPianoPlayer* P = Player.Get()) { P->RescanMidiDevices(); } }, EButton::Ghost, false, true, 9)
		]
		+ SVerticalBox::Slot().AutoHeight()[Hint(LOCTEXT("Lights", "A light shows while a keyboard sends notes or the pedal."))]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 8.f, 0.f, 0.f))
		[
			Row(LOCTEXT("Touch", "TOUCH"), Segmented({ LOCTEXT("Light", "Light"), LOCTEXT("Normal", "Normal"), LOCTEXT("Heavy", "Heavy") },
				[this]() { return Settings.VelocityCurve < 0.9f ? 0 : Settings.VelocityCurve > 1.1f ? 2 : 1; },
				[this](int32 Index) { Settings.VelocityCurve = Index == 0 ? 0.7f : Index == 2 ? 1.45f : 1.f; Changed(); }))
		]
		+ SVerticalBox::Slot().AutoHeight()[Hint(LOCTEXT("TouchHint", "Light: soft playing sounds fuller (for a stiff keyboard). Heavy: it takes more weight to play loud."))]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 8.f, 0.f, 0.f))
		[
			Row(LOCTEXT("Pedal", "SUSTAIN PEDAL"), Segmented({ LOCTEXT("PedalNormal", "Normal"), LOCTEXT("PedalReversed", "Reversed") },
				[this]() { return Settings.bPedalInverted ? 1 : 0; },
				[this](int32 Index) { Settings.bPedalInverted = Index == 1; Changed(); }))
		]
		+ SVerticalBox::Slot().AutoHeight()[Hint(LOCTEXT("PedalHint", "If notes sustain when your foot is up, choose Reversed."))];
}

TSharedRef<SWidget> SPianoSettingsPage::MakePlayback()
{
	using namespace PianoSettingsImpl;
	using namespace PianoUI;
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			Row(LOCTEXT("Expressive", "PLAY EXPRESSIVELY"), Switch([this]() { return Settings.bExpressive; }, [this]() { Settings.bExpressive = !Settings.bExpressive; Changed(); }))
		]
		+ SVerticalBox::Slot().AutoHeight()[Hint(LOCTEXT("ExpressiveHint", "When the app plays: the score's dynamics and hairpins, the melody voiced above the accompaniment, accents and staccato. Off: every note alike."))]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 8.f, 0.f, 0.f))
		[
			Row(LOCTEXT("ScorePedal", "PEDAL FROM THE SCORE"), Switch([this]() { return Settings.bScorePedal; }, [this]() { Settings.bScorePedal = !Settings.bScorePedal; Changed(); }))
		]
		+ SVerticalBox::Slot().AutoHeight()[Hint(LOCTEXT("ScorePedalHint", "The app pedals where the score says Ped., until you use your own pedal (listening, always)."))];
}

#undef LOCTEXT_NAMESPACE
