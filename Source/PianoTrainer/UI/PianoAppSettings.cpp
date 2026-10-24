#include "PianoAppSettings.h"

#include "PianoPlayer.h"

#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"

namespace PianoAppSettingsImpl
{
	static const TCHAR* Section = TEXT("PianoTrainer.App");
}

void FPianoAppSettings::Load()
{
	using namespace PianoAppSettingsImpl;
	GConfig->GetString(Section, TEXT("Instrument"), InstrumentPath, GGameUserSettingsIni);
	GConfig->GetInt(Section, TEXT("SampleQuality"), SampleQuality, GGameUserSettingsIni);
	GConfig->GetFloat(Section, TEXT("VolumeDb"), VolumeDb, GGameUserSettingsIni);
	GConfig->GetFloat(Section, TEXT("VelocityCurve"), VelocityCurve, GGameUserSettingsIni);
	GConfig->GetBool(Section, TEXT("PedalInverted"), bPedalInverted, GGameUserSettingsIni);
	GConfig->GetBool(Section, TEXT("Expressive"), bExpressive, GGameUserSettingsIni);
	GConfig->GetBool(Section, TEXT("ScorePedal"), bScorePedal, GGameUserSettingsIni);
	GConfig->GetBool(Section, TEXT("VSync"), bVSync, GGameUserSettingsIni);
}

void FPianoAppSettings::Save() const
{
	using namespace PianoAppSettingsImpl;
	GConfig->SetString(Section, TEXT("Instrument"), *InstrumentPath, GGameUserSettingsIni);
	GConfig->SetInt(Section, TEXT("SampleQuality"), SampleQuality, GGameUserSettingsIni);
	GConfig->SetFloat(Section, TEXT("VolumeDb"), VolumeDb, GGameUserSettingsIni);
	GConfig->SetFloat(Section, TEXT("VelocityCurve"), VelocityCurve, GGameUserSettingsIni);
	GConfig->SetBool(Section, TEXT("PedalInverted"), bPedalInverted, GGameUserSettingsIni);
	GConfig->SetBool(Section, TEXT("Expressive"), bExpressive, GGameUserSettingsIni);
	GConfig->SetBool(Section, TEXT("ScorePedal"), bScorePedal, GGameUserSettingsIni);
	GConfig->SetBool(Section, TEXT("VSync"), bVSync, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

void FPianoAppSettings::Apply(UPianoPlayer* Player) const
{
	if (!Player)
	{
		return;
	}
	int32 Quality = SampleQuality;
	FParse::Value(FCommandLine::Get(), TEXT("-quality="), Quality); // -quality=N: sfizz sample quality for testing
	Player->SetSampleQuality(Quality);
	Player->SetInstrumentVolume(VolumeDb);
	Player->SetVelocityCurve(VelocityCurve);
	Player->SetPedalInverted(bPedalInverted);
	Player->SetExpressive(bExpressive);
	Player->SetScorePedal(bScorePedal);
}

void FPianoAppSettings::ApplyInstrument(UPianoPlayer* Player) const
{
	FString FromCommandLine; // -instrument=<sfz> (tests) wins
	if (Player && !FParse::Value(FCommandLine::Get(), TEXT("-instrument="), FromCommandLine))
	{
		Player->SetInstrumentPath(InstrumentPath); // before the player starts: no reload
	}
}

void FPianoAppSettings::ApplyVSync() const
{
	if (UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr)
	{
		Settings->SetVSyncEnabled(bVSync && !FParse::Param(FCommandLine::Get(), TEXT("novsync")));
		Settings->ApplyNonResolutionSettings();
	}
}
