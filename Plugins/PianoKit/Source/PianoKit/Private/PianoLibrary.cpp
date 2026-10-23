#include "PianoLibrary.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "PianoLibrary"

namespace PianoLibraryImpl
{
	struct FEtude
	{
		const TCHAR* File;
		const TCHAR* Number;
		const TCHAR* Key;
		const TCHAR* Nickname;
		FText Focus;
		TArray<EPianoDrill> Drills;
	};

	using D = EPianoDrill;

	// Chopin, Etudes Op. 10 and Op. 25 (first editions, encoded by the Fryderyk Chopin Institute; Scores/Etudes/Chopin).
	static const TArray<FEtude>& Etudes()
	{
		static const TArray<FEtude> List = {
			{ TEXT("010-1b-Sm-001.krn"), TEXT("Op. 10 No. 1"), TEXT("C major"), TEXT("Waterfall"), LOCTEXT("e10_1", "Right-hand arpeggios spanning the keyboard over a bass in octaves: open the hand, keep the wrist moving."), { D::Outline, D::BlockChords, D::AccentFour, D::LongShort, D::Impulses } },
			{ TEXT("010-1b-Sm-002.krn"), TEXT("Op. 10 No. 2"), TEXT("A minor"), TEXT("Chromatic"), LOCTEXT("e10_2", "A chromatic line for fingers 3-4-5 while the thumb and index play chords: independence of the weak fingers."), { D::Outline, D::AccentThree, D::AccentFour, D::LongShort, D::ShortLong } },
			{ TEXT("010-1b-Sm-003.krn"), TEXT("Op. 10 No. 3"), TEXT("E major"), TEXT("Tristesse"), LOCTEXT("e10_3", "A singing melody voiced above the accompaniment in the same hand; the middle section in double notes."), { D::BlockChords, D::LongShort } },
			{ TEXT("010-1b-Sm-004.krn"), TEXT("Op. 10 No. 4"), TEXT("C-sharp minor"), TEXT("Torrent"), LOCTEXT("e10_4", "Semiquaver runs passed between the hands at full speed: evenness and drive."), { D::Outline, D::LongShort, D::ShortLong, D::Impulses, D::AccentFour } },
			{ TEXT("010-1b-Sm-005.krn"), TEXT("Op. 10 No. 5"), TEXT("G-flat major"), TEXT("Black Keys"), LOCTEXT("e10_5", "Right-hand triplets almost only on black keys: lightness and a supple thumb."), { D::Impulses, D::AccentThree, D::LongShort } },
			{ TEXT("010-1b-Sm-006.krn"), TEXT("Op. 10 No. 6"), TEXT("E-flat minor"), TEXT("Lament"), LOCTEXT("e10_6", "A slow melody over a restless inner voice: polyphony and legato in one hand."), { D::AccentFour, D::BlockChords } },
			{ TEXT("010-1b-Sm-007.krn"), TEXT("Op. 10 No. 7"), TEXT("C major"), TEXT("Toccata"), LOCTEXT("e10_7", "Alternating double notes in the right hand: a light rotation of the forearm."), { D::LongShort, D::ShortLong, D::AccentThree } },
			{ TEXT("010-1b-Sm-008.krn"), TEXT("Op. 10 No. 8"), TEXT("F major"), TEXT("Sunshine"), LOCTEXT("e10_8", "Brilliant right-hand figuration over a left-hand melody: speed with a light touch."), { D::Outline, D::LongShort, D::Impulses, D::AccentFour } },
			{ TEXT("010-1b-Sm-009.krn"), TEXT("Op. 10 No. 9"), TEXT("F minor"), TEXT(""), LOCTEXT("e10_9", "Wide broken chords in the left hand under an agitated melody: stretch and flexibility."), { D::BlockChords, D::Impulses } },
			{ TEXT("010-1b-Sm-010.krn"), TEXT("Op. 10 No. 10"), TEXT("A-flat major"), TEXT(""), LOCTEXT("e10_10", "The same figure under ever-changing accents and articulation: control of every finger."), { D::AccentThree, D::AccentFour, D::LongShort } },
			{ TEXT("010-1b-Sm-011.krn"), TEXT("Op. 10 No. 11"), TEXT("E-flat major"), TEXT("Arpeggio"), LOCTEXT("e10_11", "Wide chords rolled in both hands with the melody on top: very large stretches."), { D::BlockChords } },
			{ TEXT("010-1b-Sm-012.krn"), TEXT("Op. 10 No. 12"), TEXT("C minor"), TEXT("Revolutionary"), LOCTEXT("e10_12", "Torrents in the left hand under heroic octaves: left-hand velocity and endurance."), { D::Outline, D::LongShort, D::ShortLong, D::Impulses } },
			{ TEXT("025-1b-LE-001.krn"), TEXT("Op. 25 No. 1"), TEXT("A-flat major"), TEXT("Aeolian Harp"), LOCTEXT("e25_1", "Rippling arpeggios with the melody in the top fingers: voicing inside the figure."), { D::Outline, D::BlockChords, D::AccentFour } },
			{ TEXT("025-1b-LE-002.krn"), TEXT("Op. 25 No. 2"), TEXT("F minor"), TEXT("The Bees"), LOCTEXT("e25_2", "Whispering triplets against the left hand's broader beat: evenness and cross-rhythm."), { D::Impulses, D::AccentThree, D::LongShort } },
			{ TEXT("025-1b-LE-003.krn"), TEXT("Op. 25 No. 3"), TEXT("F major"), TEXT("The Horseman"), LOCTEXT("e25_3", "A galloping rhythm of grace notes and staccato in both hands: rhythmic precision."), { D::AccentFour, D::LongShort } },
			{ TEXT("025-1b-LE-004.krn"), TEXT("Op. 25 No. 4"), TEXT("A minor"), TEXT("Paganini"), LOCTEXT("e25_4", "Left-hand leaps against off-beat right-hand chords: aim and syncopation."), { D::Impulses, D::BlockChords } },
			{ TEXT("025-1b-LE-005.krn"), TEXT("Op. 25 No. 5"), TEXT("E minor"), TEXT("Wrong Note"), LOCTEXT("e25_5", "Biting semitone clashes in a dotted rhythm: crisp, even attacks."), { D::LongShort, D::AccentThree } },
			{ TEXT("025-1b-LE-006.krn"), TEXT("Op. 25 No. 6"), TEXT("G-sharp minor"), TEXT("Thirds"), LOCTEXT("e25_6", "Trills and scales in double thirds in the right hand: legato double notes."), { D::LongShort, D::ShortLong, D::Impulses } },
			{ TEXT("025-1b-LE-007.krn"), TEXT("Op. 25 No. 7"), TEXT("C-sharp minor"), TEXT("Cello"), LOCTEXT("e25_7", "A cello-like melody in the left hand in dialogue with the right: cantabile in the bass."), { D::BlockChords } },
			{ TEXT("025-1b-LE-008.krn"), TEXT("Op. 25 No. 8"), TEXT("D-flat major"), TEXT("Sixths"), LOCTEXT("e25_8", "Running double sixths: a supple hand and smooth fingering changes."), { D::LongShort, D::Impulses } },
			{ TEXT("025-1b-LE-009.krn"), TEXT("Op. 25 No. 9"), TEXT("G-flat major"), TEXT("Butterfly"), LOCTEXT("e25_9", "Octaves and staccato in quick alternation: wrist lightness."), { D::Impulses, D::AccentFour } },
			{ TEXT("025-1b-LE-010.krn"), TEXT("Op. 25 No. 10"), TEXT("B minor"), TEXT("Octaves"), LOCTEXT("e25_10", "Legato octaves in both hands: endurance from a loose arm."), { D::Impulses, D::LongShort } },
			{ TEXT("025-1b-LE-011.krn"), TEXT("Op. 25 No. 11"), TEXT("A minor"), TEXT("Winter Wind"), LOCTEXT("e25_11", "Cascades of right-hand figuration over a marching melody: velocity and stamina."), { D::Outline, D::LongShort, D::ShortLong, D::Impulses, D::AccentThree } },
			{ TEXT("025-1b-LE-012.krn"), TEXT("Op. 25 No. 12"), TEXT("C minor"), TEXT("Ocean"), LOCTEXT("e25_12", "Surging arpeggios in both hands carrying a melody on their peaks."), { D::Outline, D::BlockChords, D::Impulses } },
		};
		return List;
	}

	// A readable title from the file itself (MusicXML / MEI / Humdrum), else the file name.
	static FString TitleFromFile(const FString& Path)
	{
		FString Text;
		if (FFileHelper::LoadFileToString(Text, *Path) && Text.Len() > 0)
		{
			for (const TCHAR* Tag : { TEXT("<work-title>"), TEXT("<movement-title>"), TEXT("<title>"), TEXT("!!!OTL:") })
			{
				const int32 Start = Text.Find(Tag);
				if (Start != INDEX_NONE)
				{
					const int32 From = Start + FCString::Strlen(Tag);
					int32 End = Text.Find(Tag[0] == TEXT('<') ? TEXT("<") : TEXT("\n"), ESearchCase::CaseSensitive, ESearchDir::FromStart, From);
					const FString Title = Text.Mid(From, End == INDEX_NONE ? 80 : End - From).TrimStartAndEnd();
					if (!Title.IsEmpty())
					{
						return Title;
					}
				}
			}
		}
		FString Name = FPaths::GetBaseFilename(Path).Replace(TEXT("_"), TEXT(" ")).Replace(TEXT("-"), TEXT(" "));
		if (Name.Len() > 0)
		{
			Name[0] = FChar::ToUpper(Name[0]);
		}
		return Name;
	}

	static bool IsScoreFile(const FString& Path)
	{
		const FString Ext = FPaths::GetExtension(Path).ToLower();
		return Ext == TEXT("musicxml") || Ext == TEXT("mxl") || Ext == TEXT("xml") || Ext == TEXT("krn") || Ext == TEXT("mei") || Ext == TEXT("mscz") || Ext == TEXT("abc");
	}
}

FString FPianoLibrary::GetScoresDir()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Scores"));
}

FString FPianoLibrary::ToScoresRelative(const FString& Path)
{
	FString Full = FPaths::ConvertRelativePathToFull(Path);
	FString Dir = GetScoresDir() / TEXT("");
	FPaths::NormalizeFilename(Full);
	FPaths::NormalizeFilename(Dir);
	return Full.StartsWith(Dir, ESearchCase::IgnoreCase) ? Full.RightChop(Dir.Len()) : FString();
}

FString FPianoLibrary::FromScoresRelative(const FString& File)
{
	return GetScoresDir() / File;
}

TArray<FPianoLibraryEntry> FPianoLibrary::Scan(const TArray<FString>& RecentPaths)
{
	using namespace PianoLibraryImpl;
	TArray<FPianoLibraryEntry> Out;
	const FString Dir = GetScoresDir();

	for (const FEtude& E : Etudes())
	{
		const FString File = FString(TEXT("Etudes/Chopin/")) + E.File;
		if (!FPaths::FileExists(Dir / File))
		{
			continue;
		}
		FPianoLibraryEntry& Entry = Out.AddDefaulted_GetRef();
		Entry.Path = Dir / File;
		Entry.File = File;
		Entry.Title = FString::Printf(TEXT("Étude in %s"), E.Key);
		Entry.Subtitle = FCString::Strlen(E.Nickname) > 0 ? FString::Printf(TEXT("%s · %s"), E.Number, E.Nickname) : FString(E.Number);
		Entry.Collection = TEXT("Chopin · Études");
		Entry.Focus = E.Focus;
		Entry.Drills = E.Drills;
		Entry.bBuiltIn = true;
	}

	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *Dir, TEXT("*.*"), true, false);
	Files.Sort();
	for (const FString& Path : Files)
	{
		const FString File = ToScoresRelative(Path);
		if (!IsScoreFile(Path) || File.StartsWith(TEXT("Etudes/Chopin/")))
		{
			continue;
		}
		FPianoLibraryEntry& Entry = Out.AddDefaulted_GetRef();
		Entry.Path = Path;
		Entry.File = File;
		Entry.Title = TitleFromFile(Path);
		Entry.Collection = TEXT("Samples");
		Entry.Drills = { EPianoDrill::LongShort, EPianoDrill::BlockChords, EPianoDrill::AccentFour };
		Entry.bBuiltIn = true;
	}

	for (const FString& Path : RecentPaths)
	{
		if (!FPaths::FileExists(Path) || !ToScoresRelative(Path).IsEmpty())
		{
			continue;
		}
		FPianoLibraryEntry& Entry = Out.AddDefaulted_GetRef();
		Entry.Path = Path;
		Entry.Title = TitleFromFile(Path);
		Entry.Subtitle = FPaths::GetCleanFilename(Path);
		Entry.Collection = TEXT("Recent");
		Entry.Drills = { EPianoDrill::LongShort, EPianoDrill::BlockChords, EPianoDrill::AccentFour };
	}
	return Out;
}

#undef LOCTEXT_NAMESPACE
