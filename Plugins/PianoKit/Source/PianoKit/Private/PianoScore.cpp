#include "PianoScore.h"

#include "PianoExpression.h"
#include "PianoKitLog.h"

#include "Algo/BinarySearch.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include "VerovioApi.h"
#include "lunasvg.h"

#include <string>

namespace PianoScoreImpl
{
	// One long system with no title block: the app scrolls it under a fixed cursor.
	static const char* VerovioOptions = R"({
		"breaks": "none",
		"header": "none",
		"footer": "none",
		"adjustPageHeight": true,
		"adjustPageWidth": true,
		"scale": 100,
		"pageMarginTop": 15,
		"pageMarginBottom": 15,
		"pageMarginLeft": 60,
		"pageMarginRight": 60,
		"spacingStaff": 10,
		"spacingNonLinear": 0.9,
		"spacingLinear": 0.07
	})"; // spacing nearly proportional to duration (Verovio: 0.6 / 0.25), so the line scrolls at an even speed

	static TSharedPtr<FJsonValue> ParseJson(const char* Utf8)
	{
		TSharedPtr<FJsonValue> Value;
		const FString Text = UTF8_TO_TCHAR(Utf8 ? Utf8 : "");
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		FJsonSerializer::Deserialize(Reader, Value);
		return Value;
	}

	static bool HasClass(const lunasvg::Element& Element, const char* Class)
	{
		const std::string& Classes = Element.getAttribute("class");
		// class lists are space separated; match whole words
		size_t Pos = 0;
		const size_t Len = strlen(Class);
		while ((Pos = Classes.find(Class, Pos)) != std::string::npos)
		{
			const bool bStart = Pos == 0 || Classes[Pos - 1] == ' ';
			const bool bEnd = Pos + Len == Classes.size() || Classes[Pos + Len] == ' ';
			if (bStart && bEnd)
			{
				return true;
			}
			Pos += Len;
		}
		return false;
	}

	static lunasvg::Element FindChildWithClass(const lunasvg::Element& Parent, const char* Class)
	{
		for (const lunasvg::Node& Child : Parent.children())
		{
			if (Child.isElement())
			{
				lunasvg::Element E = Child.toElement();
				if (HasClass(E, Class))
				{
					return E;
				}
			}
		}
		return lunasvg::Element();
	}

	static FBox2f ToBox(const lunasvg::Box& B)
	{
		return FBox2f(FVector2f(B.x, B.y), FVector2f(B.x + B.w, B.y + B.h));
	}

	static bool IsPrivateUse(TCHAR C)
	{
		return C >= 0xE000 && C <= 0xF8FF;
	}

	// Text content of an XML fragment (markup dropped).
	static FString InnerText(const FString& Xml)
	{
		FString Out;
		bool bInTag = false;
		for (const TCHAR C : Xml)
		{
			if (C == '<')
			{
				bInTag = true;
			}
			else if (C == '>')
			{
				bInTag = false;
			}
			else if (!bInTag)
			{
				Out.AppendChar(C);
			}
		}
		return Out;
	}

	// Finds the next <Name ...>...</Name> or <Name .../> element (elements of this name must not nest).
	static bool FindElement(const FString& Xml, const FString& Name, int32 From, int32& OutStart, int32& OutOpenEnd, int32& OutEnd)
	{
		const FString Open = TEXT("<") + Name;
		int32 Pos = From;
		for (;;)
		{
			Pos = Xml.Find(Open, ESearchCase::CaseSensitive, ESearchDir::FromStart, Pos);
			if (Pos == INDEX_NONE)
			{
				return false;
			}
			const int32 After = Pos + Open.Len();
			const TCHAR Next = After < Xml.Len() ? Xml[After] : TCHAR(0);
			if (Next == ' ' || Next == '>' || Next == '/' || Next == '\n' || Next == '\r' || Next == '\t')
			{
				break;
			}
			Pos = After; // e.g. <labelAbbr when looking for <label
		}
		const int32 TagEnd = Xml.Find(TEXT(">"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Pos);
		if (TagEnd == INDEX_NONE)
		{
			return false;
		}
		OutStart = Pos;
		OutOpenEnd = TagEnd + 1;
		if (Xml[TagEnd - 1] == '/')
		{
			OutEnd = OutOpenEnd;
			return true;
		}
		const FString Close = TEXT("</") + Name + TEXT(">");
		const int32 CloseAt = Xml.Find(Close, ESearchCase::CaseSensitive, ESearchDir::FromStart, TagEnd);
		if (CloseAt == INDEX_NONE)
		{
			return false;
		}
		OutEnd = CloseAt + Close.Len();
		return true;
	}

	// Rewrites every <Name> element for which Edit returns true (with its replacement). Returns the number edited.
	static int32 RewriteElements(FString& Xml, const TCHAR* Name, TFunctionRef<bool(const FString& Element, const FString& OpenTag, FString& Replacement)> Edit)
	{
		FString Out;
		Out.Reserve(Xml.Len());
		int32 Cursor = 0, Start = 0, OpenEnd = 0, End = 0, Edits = 0;
		while (FindElement(Xml, Name, Cursor, Start, OpenEnd, End))
		{
			Out.AppendChars(*Xml + Cursor, Start - Cursor);
			const FString Element = Xml.Mid(Start, End - Start);
			FString Replacement;
			if (Edit(Element, Xml.Mid(Start, OpenEnd - Start), Replacement))
			{
				Out += Replacement;
				++Edits;
			}
			else
			{
				Out += Element;
			}
			Cursor = End;
		}
		if (Edits > 0)
		{
			Out.AppendChars(*Xml + Cursor, Xml.Len() - Cursor);
			Xml = MoveTemp(Out);
		}
		return Edits;
	}

	// Cleans what scores exported from notation editors often carry that a practice view should not show
	// (seen on musescore.com files): instrument names, rubato written as a metronome change on nearly every beat,
	// and text made only of the editor's private music-font characters. Returns a summary, empty if nothing changed.
	static FString CleanMei(FString& Mei)
	{
		TArray<FString> Changes;

		// Instrument names: one instrument here, and they may need fonts the renderer doesn't have.
		const int32 Labels = RewriteElements(Mei, TEXT("label"), [](const FString&, const FString&, FString& Out) { Out.Reset(); return true; })
			+ RewriteElements(Mei, TEXT("labelAbbr"), [](const FString&, const FString&, FString& Out) { Out.Reset(); return true; });
		if (Labels > 0)
		{
			Changes.Add(FString::Printf(TEXT("%d instrument labels"), Labels));
		}

		// Directions that are only private-use glyphs (e.g. MuseScore's own tuplet digits, which Verovio draws anyway).
		const int32 GlyphDirs = RewriteElements(Mei, TEXT("dir"), [](const FString& Element, const FString&, FString& Out)
		{
			const FString Text = InnerText(Element).TrimStartAndEnd();
			for (const TCHAR C : Text)
			{
				if (!IsPrivateUse(C) && !FChar::IsWhitespace(C))
				{
					return false;
				}
			}
			Out.Reset();
			return true;
		});
		if (GlyphDirs > 0)
		{
			Changes.Add(FString::Printf(TEXT("%d glyph-only texts"), GlyphDirs));
		}

		// Humdrum import warnings (type="problem": a crimson warning sign the music font cannot draw, so it shows as
		// an empty box). They flag the encoding, not the music.
		const int32 Problems = RewriteElements(Mei, TEXT("dir"), [](const FString&, const FString& OpenTag, FString& Out)
		{
			if (!OpenTag.Contains(TEXT("type=\"problem\"")))
			{
				return false;
			}
			Out.Reset();
			return true;
		});
		if (Problems > 0)
		{
			Changes.Add(FString::Printf(TEXT("%d import warnings"), Problems));
		}

		// Metronome marks without words (♩ = 52): when they are dense they are rubato, not reading aids.
		// Keep the first one visible and empty the others; their midi.bpm stays, so playback timing is unchanged.
		auto IsBareMetronome = [](const FString& Element, const FString& OpenTag)
		{
			if (!OpenTag.Contains(TEXT(" mm=")))
			{
				return false;
			}
			for (const TCHAR C : InnerText(Element))
			{
				if (FChar::IsAlpha(C) && !IsPrivateUse(C))
				{
					return false; // "Lento", "Poco più lento": real tempo words
				}
			}
			return true;
		};
		int32 Metronomes = 0;
		RewriteElements(Mei, TEXT("tempo"), [&](const FString& Element, const FString& OpenTag, FString&)
		{
			Metronomes += IsBareMetronome(Element, OpenTag) ? 1 : 0;
			return false;
		});
		int32 MeasureCount = 0;
		for (int32 Pos = Mei.Find(TEXT("<measure ")); Pos != INDEX_NONE; Pos = Mei.Find(TEXT("<measure "), ESearchCase::CaseSensitive, ESearchDir::FromStart, Pos + 9))
		{
			++MeasureCount;
		}
		if (Metronomes > FMath::Max(4, MeasureCount / 4))
		{
			// The playback tempo map is smoothed too: a running median over neighbouring marks removes ritardandos
			// written as 18-bpm beats (they read as the music stopping) but keeps real section changes.
			TArray<double> Bpm;
			RewriteElements(Mei, TEXT("tempo"), [&](const FString& Element, const FString& OpenTag, FString&)
			{
				if (IsBareMetronome(Element, OpenTag))
				{
					const int32 At = OpenTag.Find(TEXT(" midi.bpm=\""));
					Bpm.Add(At != INDEX_NONE ? FCString::Atod(*OpenTag + At + 11) : 0.0);
				}
				return false;
			});
			TArray<double> Smoothed;
			for (int32 Index = 0; Index < Bpm.Num(); ++Index)
			{
				TArray<double> Window;
				for (int32 Other = FMath::Max(0, Index - 6); Other <= FMath::Min(Bpm.Num() - 1, Index + 6); ++Other)
				{
					if (Bpm[Other] > 0.0)
					{
						Window.Add(Bpm[Other]);
					}
				}
				Window.Sort();
				Smoothed.Add(Window.Num() > 0 ? Window[Window.Num() / 2] : Bpm[Index]);
			}
			auto WithBpm = [](const FString& OpenTag, double NewBpm)
			{
				const int32 At = OpenTag.Find(TEXT(" midi.bpm=\""));
				if (At == INDEX_NONE || NewBpm <= 0.0)
				{
					return OpenTag;
				}
				const int32 ValueEnd = OpenTag.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, At + 11);
				return OpenTag.Left(At) + FString::Printf(TEXT(" midi.bpm=\"%.2f\""), NewBpm) + OpenTag.Mid(ValueEnd + 1);
			};
			const double MinBefore = Bpm.Num() > 0 ? FMath::Min(Bpm) : 0.0, MaxBefore = Bpm.Num() > 0 ? FMath::Max(Bpm) : 0.0;
			const double MinAfter = Smoothed.Num() > 0 ? FMath::Min(Smoothed) : 0.0, MaxAfter = Smoothed.Num() > 0 ? FMath::Max(Smoothed) : 0.0;

			int32 MarkIndex = 0;
			bool bFirst = true;
			const int32 Hidden = RewriteElements(Mei, TEXT("tempo"), [&](const FString& Element, const FString& OpenTag, FString& Out)
			{
				if (!IsBareMetronome(Element, OpenTag))
				{
					return false;
				}
				const FString NewOpen = WithBpm(OpenTag, Smoothed.IsValidIndex(MarkIndex) ? Smoothed[MarkIndex] : 0.0);
				++MarkIndex;
				if (bFirst)
				{
					bFirst = false; // the opening tempo stays readable
					Out = NewOpen + Element.Mid(OpenTag.Len());
					return true;
				}
				Out = NewOpen.EndsWith(TEXT("/>")) ? NewOpen : NewOpen.LeftChop(1).TrimEnd() + TEXT("/>");
				return true;
			});
			Changes.Add(FString::Printf(TEXT("%d of %d metronome marks hidden (rubato, %d measures), tempo map smoothed from %.0f-%.0f to %.0f-%.0f bpm"),
				Hidden - 1, Metronomes, MeasureCount, MinBefore, MaxBefore, MinAfter, MaxAfter));
		}
		return FString::Join(Changes, TEXT(", "));
	}

	static FString ExtractTitle(const FString& Text, const FString& Fallback)
	{
		for (const TCHAR* Tag : { TEXT("work-title"), TEXT("movement-title"), TEXT("credit-words") })
		{
			const FString Open = FString::Printf(TEXT("<%s"), Tag);
			const int32 Start = Text.Find(Open);
			if (Start == INDEX_NONE)
			{
				continue;
			}
			const int32 ContentStart = Text.Find(TEXT(">"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start);
			const int32 End = Text.Find(TEXT("</"), ESearchCase::CaseSensitive, ESearchDir::FromStart, ContentStart);
			if (ContentStart != INDEX_NONE && End != INDEX_NONE)
			{
				FString Found = Text.Mid(ContentStart + 1, End - ContentStart - 1).TrimStartAndEnd();
				if (!Found.IsEmpty())
				{
					return Found;
				}
			}
		}
		return Fallback;
	}
}

FPianoScore::FPianoScore() = default;
FPianoScore::~FPianoScore() = default;

FString FPianoScore::GetVerovioDataDir()
{
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("PianoKit"));
	const FString Base = Plugin ? Plugin->GetBaseDir() : FPaths::ProjectPluginsDir() / TEXT("PianoKit");
	return FPaths::ConvertRelativePathToFull(Base / TEXT("ThirdParty/Verovio/data"));
}

FString FPianoScore::FindMuseScore()
{
	TArray<FString> Candidates;
#if PLATFORM_WINDOWS
	for (const TCHAR* Root : { TEXT("C:/Program Files"), TEXT("C:/Program Files (x86)") })
	{
		Candidates.Add(FString(Root) / TEXT("MuseScore Studio 4/bin/MuseScore4.exe"));
		Candidates.Add(FString(Root) / TEXT("MuseScore 4/bin/MuseScore4.exe"));
		Candidates.Add(FString(Root) / TEXT("MuseScore 3/bin/MuseScore3.exe"));
	}
#elif PLATFORM_MAC
	Candidates.Add(TEXT("/Applications/MuseScore Studio 4.app/Contents/MacOS/mscore"));
	Candidates.Add(TEXT("/Applications/MuseScore 4.app/Contents/MacOS/mscore"));
	Candidates.Add(TEXT("/Applications/MuseScore 3.app/Contents/MacOS/mscore"));
#endif
	for (const FString& Candidate : Candidates)
	{
		if (FPaths::FileExists(Candidate))
		{
			return Candidate;
		}
	}
	return FString();
}

bool FPianoScore::ConvertWithMuseScore(const FString& InPath, FString& OutPath, FString& OutError) const
{
	const FString MuseScore = FindMuseScore();
	if (MuseScore.IsEmpty())
	{
		OutError = TEXT("This is a MuseScore file. Install MuseScore Studio, or export MusicXML from MuseScore / musescore.com.");
		return false;
	}
	const FString OutDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Converted"));
	IFileManager::Get().MakeDirectory(*OutDir, true);
	OutPath = OutDir / (FPaths::GetBaseFilename(InPath) + TEXT(".musicxml"));
	const FString Args = FString::Printf(TEXT("-o \"%s\" \"%s\""), *OutPath, *InPath);
	int32 ReturnCode = -1;
	FString StdOut, StdErr;
	FPlatformProcess::ExecProcess(*MuseScore, *Args, &ReturnCode, &StdOut, &StdErr);
	if (ReturnCode != 0 || !FPaths::FileExists(OutPath))
	{
		OutError = FString::Printf(TEXT("MuseScore conversion failed (%d): %s"), ReturnCode, *StdErr.Left(400));
		return false;
	}
	UE_LOG(LogPianoKit, Log, TEXT("Converted %s with MuseScore -> %s"), *InPath, *OutPath);
	return true;
}

bool FPianoScore::LoadFile(const FString& InPath, FString& OutError)
{
	using namespace PianoScoreImpl;

	// Scores load on background threads too; one Verovio toolkit at a time (it keeps some global state).
	static FCriticalSection VerovioLock;
	FScopeLock VerovioGuard(&VerovioLock);

	FString Path = FPaths::ConvertRelativePathToFull(InPath);
	FString Ext = FPaths::GetExtension(Path).ToLower();
	if (Ext == TEXT("mscz") || Ext == TEXT("mscx"))
	{
		FString Converted;
		if (!ConvertWithMuseScore(Path, Converted, OutError))
		{
			return false;
		}
		Path = Converted;
		Ext = TEXT("musicxml");
	}
	if (Ext == TEXT("mid") || Ext == TEXT("midi"))
	{
		OutError = TEXT("MIDI files have no notation to engrave. Load the MusicXML version of this piece.");
		return false;
	}

	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path))
	{
		OutError = FString::Printf(TEXT("Could not read %s"), *Path);
		return false;
	}

	const FVerovioApi& Vrv = FVerovioApi::Get();
	if (!Vrv.IsLoaded())
	{
		OutError = TEXT("Verovio library is not loaded (see log)");
		return false;
	}
	const double StartSeconds = FPlatformTime::Seconds();
	void* Tk = Vrv.constructorResourcePath(TCHAR_TO_UTF8(*GetVerovioDataDir()));
	if (!Tk)
	{
		OutError = TEXT("Verovio failed to start (resource data missing?)");
		return false;
	}
	ON_SCOPE_EXIT { Vrv.destructor(Tk); };
	Vrv.setOptions(Tk, VerovioOptions);
	if (FParse::Param(FCommandLine::Get(), TEXT("vrvdefaultspacing")))
	{
		Vrv.setOptions(Tk, R"({"spacingNonLinear": 0.6, "spacingLinear": 0.25})"); // tests: Verovio's own spacing
	}

	bool bLoaded = false;
	FString TextForTitle;
	if (Ext == TEXT("mxl"))
	{
		bLoaded = Vrv.loadZipDataBuffer(Tk, Bytes.GetData(), Bytes.Num());
	}
	else
	{
		Bytes.Add(0);
		bLoaded = Vrv.loadData(Tk, reinterpret_cast<const char*>(Bytes.GetData()));
		TextForTitle = UTF8_TO_TCHAR(reinterpret_cast<const char*>(Bytes.GetData()));
	}
	if (!bLoaded)
	{
		OutError = FString::Printf(TEXT("Verovio could not read %s: %s"), *FPaths::GetCleanFilename(Path), UTF8_TO_TCHAR(Vrv.getLog(Tk)));
		return false;
	}

	// Clean-up pass, independent of the input format: export Verovio's MEI, clean it, load it again.
	// Element ids and timing (midi.bpm) survive the round trip.
	FString MeiText = UTF8_TO_TCHAR(Vrv.getMEI(Tk, "{}"));
	const FString Cleaned = CleanMei(MeiText);
	if (!Cleaned.IsEmpty())
	{
		if (Vrv.loadData(Tk, TCHAR_TO_UTF8(*MeiText)))
		{
			UE_LOG(LogPianoKit, Log, TEXT("  cleaned score: %s"), *Cleaned);
		}
		else
		{
			UE_LOG(LogPianoKit, Warning, TEXT("  cleaned MEI did not load, keeping the original: %s"), UTF8_TO_TCHAR(Vrv.getLog(Tk)));
			bLoaded = Ext == TEXT("mxl") ? Vrv.loadZipDataBuffer(Tk, Bytes.GetData(), Bytes.Num())
				: Vrv.loadData(Tk, reinterpret_cast<const char*>(Bytes.GetData()));
			MeiText = UTF8_TO_TCHAR(Vrv.getMEI(Tk, "{}"));
		}
	}

	SourcePath = InPath;
	if (TextForTitle.IsEmpty())
	{
		int32 Start = 0, OpenEnd = 0, End = 0;
		if (FindElement(MeiText, TEXT("title"), 0, Start, OpenEnd, End))
		{
			TextForTitle = TEXT("<work-title>") + InnerText(MeiText.Mid(Start, End - Start)).TrimStartAndEnd() + TEXT("</work-title>");
		}
	}
	Title = ExtractTitle(TextForTitle, FPaths::GetBaseFilename(InPath));
	Notes.Reset();
	Measures.Reset();
	NumStaves = 0;
	LengthMs = 0.0;

	// Engraving: one page holding the whole piece as a single system.
	const std::string Svg = Vrv.renderToSVG(Tk, 1, false);
	Document = TUniquePtr<lunasvg::Document>(lunasvg::Document::loadFromData(Svg).release());
	if (!Document)
	{
		OutError = TEXT("Could not parse Verovio's SVG output");
		return false;
	}
	Size = FVector2f(Document->width(), Document->height());

	// Timing: the timemap lists which notes start at each moment, MIDI values give pitch and duration.
	TSharedPtr<FJsonValue> TimemapValue = ParseJson(Vrv.renderToTimemap(Tk, R"({"includeMeasures": true, "includeRests": true})"));
	TMap<FString, double> RestTimes; // rest id -> onset (a fermata may sit on a rest)
	if (!TimemapValue.IsValid() || TimemapValue->Type != EJson::Array)
	{
		OutError = TEXT("Verovio returned no timemap");
		return false;
	}

	for (const TSharedPtr<FJsonValue>& EntryValue : TimemapValue->AsArray())
	{
		const TSharedPtr<FJsonObject> Entry = EntryValue->AsObject();
		if (!Entry.IsValid())
		{
			continue;
		}
		const double TimeMs = Entry->GetNumberField(TEXT("tstamp"));
		LengthMs = FMath::Max(LengthMs, TimeMs);

		FString MeasureId;
		if (Entry->TryGetStringField(TEXT("measureOn"), MeasureId))
		{
			FPianoScoreMeasure& Measure = Measures.AddDefaulted_GetRef();
			Measure.Id = MeasureId;
			Measure.OnMs = TimeMs;
			Measure.QStamp = Entry->GetNumberField(TEXT("qstamp"));
			if (lunasvg::Element E = Document->getElementById(TCHAR_TO_UTF8(*MeasureId)))
			{
				Measure.X = E.getGlobalBoundingBox().x;
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* RestIds = nullptr;
		if (Entry->TryGetArrayField(TEXT("restsOn"), RestIds))
		{
			for (const TSharedPtr<FJsonValue>& IdValue : *RestIds)
			{
				RestTimes.Add(IdValue->AsString(), TimeMs);
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* OnIds = nullptr;
		if (!Entry->TryGetArrayField(TEXT("on"), OnIds))
		{
			continue;
		}
		for (const TSharedPtr<FJsonValue>& IdValue : *OnIds)
		{
			const FString Id = IdValue->AsString();
			const auto IdUtf8 = StringCast<UTF8CHAR>(*Id);
			const char* IdChars = reinterpret_cast<const char*>(IdUtf8.Get());

			TSharedPtr<FJsonValue> Midi = ParseJson(Vrv.getMIDIValuesForElement(Tk, IdChars));
			const TSharedPtr<FJsonObject> MidiObj = Midi.IsValid() ? Midi->AsObject() : nullptr;
			if (!MidiObj.IsValid() || !MidiObj->HasField(TEXT("pitch")))
			{
				continue; // rests or unpitched elements
			}

			FPianoScoreNote& Note = Notes.AddDefaulted_GetRef();
			Note.Id = Id;
			Note.Pitch = int32(MidiObj->GetNumberField(TEXT("pitch")));
			Note.OnMs = MidiObj->HasField(TEXT("time")) ? MidiObj->GetNumberField(TEXT("time")) : TimeMs;
			Note.DurMs = MidiObj->GetNumberField(TEXT("duration"));
			LengthMs = FMath::Max(LengthMs, Note.OnMs + Note.DurMs);

			if (lunasvg::Element E = Document->getElementById(IdChars))
			{
				lunasvg::Element Head = FindChildWithClass(E, "notehead");
				Note.Head = ToBox(Head ? Head.getGlobalBoundingBox() : E.getGlobalBoundingBox());

				// note -> (chord) -> layer -> staff -> measure; the staff's position among its siblings is the staff number
				lunasvg::Element Staff = E.parentElement();
				while (Staff && !HasClass(Staff, "staff"))
				{
					Staff = Staff.parentElement();
				}
				if (Staff)
				{
					int32 Index = 0;
					for (const lunasvg::Node& Sibling : Staff.parentElement().children())
					{
						if (Sibling.isElement() && HasClass(Sibling.toElement(), "staff"))
						{
							++Index;
							if (Sibling == Staff)
							{
								break;
							}
						}
					}
					Note.Staff = FMath::Max(1, Index);
				}
			}
			NumStaves = FMath::Max(NumStaves, Note.Staff);
		}
	}

	// Staff 1 is the right hand; everything below it is the left hand (single-staff pieces are all right hand).
	for (FPianoScoreNote& Note : Notes)
	{
		Note.Hand = Note.Staff <= 1 ? EPianoHand::Right : EPianoHand::Left;
	}
	Notes.StableSort([](const FPianoScoreNote& A, const FPianoScoreNote& B)
	{
		return A.OnMs != B.OnMs ? A.OnMs < B.OnMs : A.Pitch < B.Pitch;
	});

	// Ties: Verovio's timemap starts every tied note on its own, but only the first is struck. Its MEI keeps the
	// ties as <tie startid="#a" endid="#b"/>; fold each chain into its first note and mark the rest as continuations.
	{
		TMap<FString, FString> TieStartByEnd;
		const FString& Mei = MeiText;
		auto AttrValue = [](const FString& Tag, const TCHAR* Name) -> FString
		{
			const FString Key = FString::Printf(TEXT(" %s=\""), Name);
			const int32 Start = Tag.Find(Key);
			if (Start == INDEX_NONE)
			{
				return FString();
			}
			const int32 ValueStart = Start + Key.Len();
			const int32 End = Tag.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, ValueStart);
			FString Value = Tag.Mid(ValueStart, End - ValueStart);
			Value.RemoveFromStart(TEXT("#"));
			return Value;
		};
		for (int32 Pos = Mei.Find(TEXT("<tie ")); Pos != INDEX_NONE; Pos = Mei.Find(TEXT("<tie "), ESearchCase::CaseSensitive, ESearchDir::FromStart, Pos + 5))
		{
			const int32 End = Mei.Find(TEXT(">"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Pos);
			if (End == INDEX_NONE)
			{
				break;
			}
			const FString Tag = Mei.Mid(Pos, End - Pos);
			const FString StartId = AttrValue(Tag, TEXT("startid"));
			const FString EndId = AttrValue(Tag, TEXT("endid"));
			if (!StartId.IsEmpty() && !EndId.IsEmpty())
			{
				TieStartByEnd.Add(EndId, StartId);
			}
		}

		TMap<FString, int32> IndexById;
		for (int32 Index = 0; Index < Notes.Num(); ++Index)
		{
			IndexById.Add(Notes[Index].Id, Index);
		}
		int32 Folded = 0;
		for (int32 Index = 0; Index < Notes.Num(); ++Index)
		{
			FString RootId = Notes[Index].Id;
			for (int32 Guard = 0; Guard < 64 && TieStartByEnd.Contains(RootId); ++Guard)
			{
				RootId = TieStartByEnd[RootId];
			}
			const int32* Root = IndexById.Find(RootId);
			if (Root && *Root != Index && Notes[*Root].Pitch == Notes[Index].Pitch)
			{
				FPianoScoreNote& First = Notes[*Root];
				FPianoScoreNote& Continuation = Notes[Index];
				Continuation.TieRoot = *Root;
				First.DurMs = FMath::Max(First.DurMs, Continuation.OnMs + Continuation.DurMs - First.OnMs);
				++Folded;
			}
		}
		if (Folded > 0)
		{
			UE_LOG(LogPianoKit, Log, TEXT("  %d tied notes folded into their first note"), Folded);
		}

		// Grace notes: <note xml:id="..." ... grace="unacc|acc">
		int32 Graces = 0;
		for (int32 Pos = Mei.Find(TEXT("<note ")); Pos != INDEX_NONE; Pos = Mei.Find(TEXT("<note "), ESearchCase::CaseSensitive, ESearchDir::FromStart, Pos + 6))
		{
			const int32 TagEnd = Mei.Find(TEXT(">"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Pos);
			const FString Tag = Mei.Mid(Pos, TagEnd - Pos);
			if (!Tag.Contains(TEXT(" grace=\"")))
			{
				continue;
			}
			const int32 IdAt = Tag.Find(TEXT("xml:id=\""));
			if (IdAt == INDEX_NONE)
			{
				continue;
			}
			const int32 IdStart = IdAt + 8;
			const FString Id = Tag.Mid(IdStart, Tag.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, IdStart) - IdStart);
			if (const int32* Index = IndexById.Find(Id))
			{
				Notes[*Index].bGrace = true;
				++Graces;
			}
		}
		if (Graces > 0)
		{
			UE_LOG(LogPianoKit, Log, TEXT("  %d grace notes (wait mode won't hold for them)"), Graces);
		}

		// Phrasing: slurs and phrase marks, fermatas, bar lines and the key (practice sections are cut at phrases).
		auto ForEachTag = [&Mei](const TCHAR* Open, TFunctionRef<void(int32 Pos, const FString& Tag)> Fn)
		{
			const int32 OpenLen = FCString::Strlen(Open);
			for (int32 Pos = Mei.Find(Open, ESearchCase::CaseSensitive); Pos != INDEX_NONE; Pos = Mei.Find(Open, ESearchCase::CaseSensitive, ESearchDir::FromStart, Pos + OpenLen))
			{
				const int32 TagEnd = Mei.Find(TEXT(">"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Pos);
				if (TagEnd == INDEX_NONE)
				{
					break;
				}
				Fn(Pos, Mei.Mid(Pos, TagEnd - Pos));
			}
		};
		// slurs and fermatas often point at a chord: its first note stands for it
		TMap<FString, int32> FirstNoteOfChord;
		ForEachTag(TEXT("<chord "), [&](int32 Pos, const FString& Tag)
		{
			const int32 NotePos = Mei.Find(TEXT("<note "), ESearchCase::CaseSensitive, ESearchDir::FromStart, Pos + Tag.Len());
			const int32 NoteEnd = NotePos == INDEX_NONE ? INDEX_NONE : Mei.Find(TEXT(">"), ESearchCase::CaseSensitive, ESearchDir::FromStart, NotePos);
			if (NoteEnd != INDEX_NONE)
			{
				if (const int32* Index = IndexById.Find(AttrValue(Mei.Mid(NotePos, NoteEnd - NotePos), TEXT("xml:id"))))
				{
					FirstNoteOfChord.Add(AttrValue(Tag, TEXT("xml:id")), *Index);
				}
			}
		});
		auto NoteOf = [&](const FString& Id) -> const FPianoScoreNote*
		{
			const int32* Index = IndexById.Find(Id);
			Index = Index ? Index : FirstNoteOfChord.Find(Id);
			return Index ? &Notes[*Index] : nullptr;
		};
		Marks = FPianoScoreMarks();
		for (const TCHAR* Open : { TEXT("<slur "), TEXT("<phrase ") })
		{
			ForEachTag(Open, [&](int32, const FString& Tag)
			{
				const FPianoScoreNote* First = NoteOf(AttrValue(Tag, TEXT("startid")));
				const FPianoScoreNote* Last = NoteOf(AttrValue(Tag, TEXT("endid")));
				if (First && Last && Last->OnMs >= First->OnMs)
				{
					const FString StaffText = AttrValue(Tag, TEXT("staff"));
					Marks.Slurs.Add({ First->OnMs, Last->OnMs, Last->OnMs + Last->DurMs, StaffText.IsEmpty() ? First->Staff : FCString::Atoi(*StaffText) });
				}
			});
		}
		ForEachTag(TEXT("<fermata "), [&](int32, const FString& Tag)
		{
			const FString Id = AttrValue(Tag, TEXT("startid"));
			if (const FPianoScoreNote* N = NoteOf(Id))
			{
				Marks.Fermatas.Add(FVector2D(N->OnMs, N->OnMs + N->DurMs));
			}
			else if (const double* RestMs = RestTimes.Find(Id))
			{
				Marks.Fermatas.Add(FVector2D(*RestMs, *RestMs));
			}
		});
		for (const TCHAR* Open : { TEXT("<note "), TEXT("<chord "), TEXT("<rest ") })
		{
			ForEachTag(Open, [&](int32, const FString& Tag)
			{
				if (!Tag.Contains(TEXT(" fermata=\"")))
				{
					return;
				}
				const FString Id = AttrValue(Tag, TEXT("xml:id"));
				if (const FPianoScoreNote* N = NoteOf(Id))
				{
					Marks.Fermatas.Add(FVector2D(N->OnMs, N->OnMs + N->DurMs));
				}
				else if (const double* RestMs = RestTimes.Find(Id))
				{
					Marks.Fermatas.Add(FVector2D(*RestMs, *RestMs));
				}
			});
		}
		TMap<FString, int32> MeasureById;
		for (int32 Index = 0; Index < Measures.Num(); ++Index)
		{
			MeasureById.Add(Measures[Index].Id, Index);
		}
		ForEachTag(TEXT("<measure "), [&](int32, const FString& Tag)
		{
			const int32* Index = MeasureById.Find(AttrValue(Tag, TEXT("xml:id")));
			if (!Index)
			{
				return;
			}
			FPianoScoreMeasure& M = Measures[*Index];
			const FString Right = AttrValue(Tag, TEXT("right")), Left = AttrValue(Tag, TEXT("left"));
			M.bDoubleBarEnd = Right.StartsWith(TEXT("dbl"));
			M.bFinalEnd = Right == TEXT("end");
			M.bRepeatEnd = Right == TEXT("rptend") || Right == TEXT("rptboth");
			M.bRepeatStart = Left == TEXT("rptstart") || Left == TEXT("rptboth");
			if (Right == TEXT("rptboth") && Measures.IsValidIndex(*Index + 1))
			{
				Measures[*Index + 1].bRepeatStart = true;
			}
		});
		// key: <keySig sig="3f"/> or key.sig="2s" on a score or staff definition
		for (const TCHAR* Key : { TEXT(" sig=\""), TEXT(" key.sig=\"") })
		{
			const int32 At = Mei.Find(Key, ESearchCase::CaseSensitive);
			if (At != INDEX_NONE)
			{
				const int32 ValueStart = At + FCString::Strlen(Key);
				const FString Sig = Mei.Mid(ValueStart, Mei.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, ValueStart) - ValueStart);
				Marks.KeyFifths = FCString::Atoi(*Sig) * (Sig.EndsWith(TEXT("f")) ? -1 : 1);
				break;
			}
		}
		// Expression, for how the app plays the score (FPianoExpression): dynamics, hairpins, pedal marks, articulation.
		// A repeat's notes share their ids with the first time through: a mark applies every time it is played.
		TMap<FString, TArray<int32>> AllById, MeasuresById;
		for (int32 Index = 0; Index < Notes.Num(); ++Index)
		{
			AllById.FindOrAdd(Notes[Index].Id).Add(Index);
		}
		for (int32 Index = 0; Index < Measures.Num(); ++Index)
		{
			MeasuresById.FindOrAdd(Measures[Index].Id).Add(Index);
		}
		TArray<int32> MeasureTagPos;
		TArray<FString> MeasureTagId;
		ForEachTag(TEXT("<measure "), [&](int32 Pos, const FString& Tag) { MeasureTagPos.Add(Pos); MeasureTagId.Add(AttrValue(Tag, TEXT("xml:id"))); });
		int32 MeterUnit = 4; // what a tstamp counts (the time signature's lower number)
		for (const TCHAR* Key : { TEXT(" unit=\""), TEXT(" meter.unit=\"") })
		{
			const int32 At = Mei.Find(Key, ESearchCase::CaseSensitive);
			if (At != INDEX_NONE)
			{
				MeterUnit = FMath::Max(1, FCString::Atoi(*Mei.Mid(At + FCString::Strlen(Key), 3)));
				break;
			}
		}
		auto MsPerQuarter = [&](int32 M)
		{
			for (int32 K = M; K >= 0 && K >= M - 1; --K)
			{
				if (Measures.IsValidIndex(K + 1) && Measures[K + 1].QStamp > Measures[K].QStamp)
				{
					return (Measures[K + 1].OnMs - Measures[K].OnMs) / (Measures[K + 1].QStamp - Measures[K].QStamp);
				}
			}
			return 500.0;
		};
		// when a control event happens: at its note (every time it is played), else at its tstamp ("2.5", or "1m+2.5":
		// a measure later) in the measure it is written in
		auto EventTimes = [&](int32 Pos, const FString& Tag, const TCHAR* IdAttr, const TCHAR* StampAttr)
		{
			TArray<double> Times;
			const FString Id = AttrValue(Tag, IdAttr);
			if (!Id.IsEmpty())
			{
				const TArray<int32>* Indices = AllById.Find(Id);
				if (!Indices)
				{
					const int32* First = FirstNoteOfChord.Find(Id);
					Indices = First ? AllById.Find(Notes[*First].Id) : nullptr;
				}
				for (const int32 Index : Indices ? *Indices : TArray<int32>())
				{
					Times.Add(Notes[Index].OnMs);
				}
				if (Times.Num() == 0 && RestTimes.Contains(Id))
				{
					Times.Add(RestTimes[Id]);
				}
				if (Times.Num() > 0)
				{
					return Times;
				}
			}
			const FString Stamp = AttrValue(Tag, StampAttr);
			const int32 TagIndex = Algo::UpperBound(MeasureTagPos, Pos) - 1;
			const TArray<int32>* InMeasures = MeasureTagId.IsValidIndex(TagIndex) ? MeasuresById.Find(MeasureTagId[TagIndex]) : nullptr;
			if (Stamp.IsEmpty() || !InMeasures)
			{
				return Times;
			}
			FString Later, Beat = Stamp;
			const int32 Later_ = Stamp.Split(TEXT("m+"), &Later, &Beat) ? FCString::Atoi(*Later) : 0;
			for (const int32 M : *InMeasures)
			{
				const int32 At = FMath::Clamp(M + Later_, 0, FMath::Max(0, Measures.Num() - 1));
				Times.Add(GetMeasureStartMs(At) + (FCString::Atod(*Beat) - 1.0) * MsPerQuarter(At) * 4.0 / MeterUnit);
			}
			return Times;
		};
		// dynamics (and written cresc. / dim., to the next dynamic within two bars)
		auto AddTextHairpin = [&](const FString& Text, double Ms)
		{
			const bool bCres = Text.Contains(TEXT("cresc")) && !Text.Contains(TEXT("decresc"));
			if (bCres || Text.Contains(TEXT("dim")) || Text.Contains(TEXT("decresc")) || Text.Contains(TEXT("smorz")) || Text.Contains(TEXT("calando")))
			{
				Marks.Hairpins.Add({ Ms, Ms + 8.0 * MsPerQuarter(GetMeasureAt(Ms)), bCres });
			}
		};
		static const TMap<FString, int32> Levels = { { TEXT("pppp"), 22 }, { TEXT("ppp"), 28 }, { TEXT("pp"), 38 }, { TEXT("p"), 50 }, { TEXT("mp"), 62 },
			{ TEXT("mf"), 74 }, { TEXT("f"), 88 }, { TEXT("ff"), 102 }, { TEXT("fff"), 114 }, { TEXT("ffff"), 120 }, { TEXT("fp"), 50 }, { TEXT("sfp"), 50 } };
		static const TSet<FString> Sforzandi = { TEXT("sf"), TEXT("sfz"), TEXT("sff"), TEXT("sffz"), TEXT("fz"), TEXT("ffz"), TEXT("rf"), TEXT("rfz"), TEXT("fp"), TEXT("sfp") };
		for (const TCHAR* Name : { TEXT("dynam"), TEXT("dir") })
		{
			for (int32 Cursor = 0, Start = 0, OpenEnd = 0, End = 0; FindElement(Mei, Name, Cursor, Start, OpenEnd, End); Cursor = End)
			{
				const FString Tag = Mei.Mid(Start, OpenEnd - Start);
				const FString Text = InnerText(Mei.Mid(Start, End - Start)).ToLower();
				TArray<FString> Words;
				Text.ParseIntoArrayWS(Words, TEXT(".,;:()"));
				for (const double Ms : EventTimes(Start, Tag, TEXT("startid"), TEXT("tstamp")))
				{
					for (const FString& Word : Words)
					{
						const int32* Level = FCString::Strcmp(Name, TEXT("dynam")) == 0 ? Levels.Find(Word) : nullptr;
						const bool bAccent = FCString::Strcmp(Name, TEXT("dynam")) == 0 && Sforzandi.Contains(Word);
						if (Level || bAccent)
						{
							Marks.Dynamics.Add({ Ms, Level ? *Level : 0, bAccent });
						}
						AddTextHairpin(Word, Ms);
					}
				}
			}
		}
		Marks.Dynamics.StableSort([](const FPianoScoreDynamic& A, const FPianoScoreDynamic& B) { return A.Ms < B.Ms; });
		ForEachTag(TEXT("<hairpin "), [&](int32 Pos, const FString& Tag)
		{
			const TArray<double> Starts = EventTimes(Pos, Tag, TEXT("startid"), TEXT("tstamp"));
			const TArray<double> Ends = EventTimes(Pos, Tag, TEXT("endid"), TEXT("tstamp2"));
			for (int32 I = 0; I < Starts.Num(); ++I)
			{
				const double EndMs = Ends.IsValidIndex(I) && Ends[I] > Starts[I] ? Ends[I] : Starts[I] + 4.0 * MsPerQuarter(GetMeasureAt(Starts[I]));
				Marks.Hairpins.Add({ Starts[I], EndMs, AttrValue(Tag, TEXT("form")) != TEXT("dim") });
			}
		});
		Marks.Hairpins.StableSort([](const FPianoScoreHairpin& A, const FPianoScoreHairpin& B) { return A.StartMs < B.StartMs; });
		// sustain pedal marks (a "bounce" lifts and presses again)
		ForEachTag(TEXT("<pedal "), [&](int32 Pos, const FString& Tag)
		{
			const FString Dir = AttrValue(Tag, TEXT("dir"));
			for (const double Ms : EventTimes(Pos, Tag, TEXT("startid"), TEXT("tstamp")))
			{
				if (Dir == TEXT("up") || Dir == TEXT("bounce"))
				{
					Marks.Pedals.Add(FVector2D(Dir == TEXT("bounce") ? Ms - 1.0 : Ms, 0.0));
				}
				if (Dir == TEXT("down") || Dir == TEXT("half") || Dir == TEXT("bounce"))
				{
					Marks.Pedals.Add(FVector2D(Ms, 1.0));
				}
			}
		});
		Marks.Pedals.StableSort([](const FVector2D& A, const FVector2D& B) { return A.X < B.X; });
		// articulation, on a note or on a chord (then on all its notes): artic="acc stacc" or <artic artic="..."/>
		auto ArticFlags = [](const FString& Xml)
		{
			uint8 Flags = 0;
			for (int32 At = Xml.Find(TEXT("artic=\"")); At != INDEX_NONE; At = Xml.Find(TEXT("artic=\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, At + 7))
			{
				TArray<FString> Values;
				Xml.Mid(At + 7, Xml.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, At + 7) - At - 7).ParseIntoArrayWS(Values);
				for (const FString& V : Values)
				{
					Flags |= V == TEXT("acc") ? ArticAccent : V == TEXT("marc") ? ArticMarcato : V == TEXT("stacc") ? ArticStaccato
						: V == TEXT("stacciss") || V == TEXT("spicc") ? ArticStaccatissimo : V == TEXT("ten") ? ArticTenuto : 0;
				}
			}
			return Flags;
		};
		auto Articulate = [&](const FString& Id, uint8 Flags)
		{
			for (const int32 Index : AllById.Contains(Id) ? AllById[Id] : TArray<int32>())
			{
				Notes[Index].Artic |= Flags;
			}
		};
		for (int32 Cursor = 0, Start = 0, OpenEnd = 0, End = 0; FindElement(Mei, TEXT("note"), Cursor, Start, OpenEnd, End); Cursor = End)
		{
			const FString Element = Mei.Mid(Start, End - Start);
			if (const uint8 Flags = ArticFlags(Element))
			{
				Articulate(AttrValue(Mei.Mid(Start, OpenEnd - Start), TEXT("xml:id")), Flags);
			}
		}
		for (int32 Cursor = 0, Start = 0, OpenEnd = 0, End = 0; FindElement(Mei, TEXT("chord"), Cursor, Start, OpenEnd, End); Cursor = End)
		{
			// the chord's own marks: outside its notes
			FString Own = Mei.Mid(Start, End - Start);
			TArray<FString> NoteIds;
			for (int32 NoteStart = 0, NoteOpenEnd = 0, NoteEnd = 0; FindElement(Own, TEXT("note"), 0, NoteStart, NoteOpenEnd, NoteEnd);)
			{
				NoteIds.Add(AttrValue(Own.Mid(NoteStart, NoteOpenEnd - NoteStart), TEXT("xml:id")));
				Own.RemoveAt(NoteStart, NoteEnd - NoteStart);
			}
			if (const uint8 Flags = ArticFlags(Own))
			{
				for (const FString& Id : NoteIds)
				{
					Articulate(Id, Flags);
				}
			}
		}
		UE_LOG(LogPianoKit, Log, TEXT("  phrasing: %d slurs, %d fermatas, key %+d; expression: %d dynamics, %d hairpins, %d pedal marks"),
			Marks.Slurs.Num(), Marks.Fermatas.Num(), Marks.KeyFifths, Marks.Dynamics.Num(), Marks.Hairpins.Num(), Marks.Pedals.Num());
	}

	BuildScrollMap();

	// how the app plays it: dynamics, voicing, accents
	const TArray<uint8> Velocities = FPianoExpression::Velocities(*this);
	for (int32 Index = 0; Index < Notes.Num(); ++Index)
	{
		Notes[Index].Velocity = Velocities[Index];
	}

	UE_LOG(LogPianoKit, Log, TEXT("Loaded '%s': %d notes, %d measures, %d staves, %.1f s, strip %.0fx%.0f px, %.0f ms"),
		*Title, Notes.Num(), Measures.Num(), NumStaves, LengthMs / 1000.0, Size.X, Size.Y,
		(FPlatformTime::Seconds() - StartSeconds) * 1000.0);
	if (Notes.Num() > 0)
	{
		const FPianoScoreNote& N = Notes[0];
		UE_LOG(LogPianoKit, Log, TEXT("  first note %s pitch %d at %.0f ms, staff %d, head (%.1f,%.1f)-(%.1f,%.1f)"),
			*N.Id, N.Pitch, N.OnMs, N.Staff, N.Head.Min.X, N.Head.Min.Y, N.Head.Max.X, N.Head.Max.Y);
	}
	return Notes.Num() > 0;
}

void FPianoScore::BuildScrollMap()
{
	MapMs.Reset();
	MapX.Reset();
	MapSlope.Reset();
	MapRunStart.Reset();

	// Onsets: the leftmost notehead centre of the notes starting there.
	TArray<double> OnsetMs;
	TArray<float> OnsetX;
	for (int32 Index = 0; Index < Notes.Num();)
	{
		const double OnMs = Notes[Index].OnMs;
		float X = TNumericLimits<float>::Max();
		for (; Index < Notes.Num() && Notes[Index].OnMs - OnMs <= 1.0; ++Index)
		{
			if (Notes[Index].Head.bIsValid)
			{
				X = FMath::Min(X, Notes[Index].Head.GetCenter().X);
			}
		}
		if (X != TNumericLimits<float>::Max())
		{
			OnsetMs.Add(OnMs);
			OnsetX.Add(X);
		}
	}
	if (OnsetMs.Num() == 0)
	{
		return;
	}

	// Anchors: the first onset, then each bar's downbeat (a bar starting with a rest is spanned by its neighbours).
	// Following every note would make the line speed up and slow down with the engraving's spacing.
	auto AddAnchor = [this](double Ms, float X)
	{
		if (MapMs.Num() > 0 && Ms <= MapMs.Last() + 1.0)
		{
			return;
		}
		// a step back of more than a notehead: the second time through a repeat
		MapRunStart.Add(MapX.Num() == 0 || X < MapX.Last() - 4.f);
		MapMs.Add(Ms);
		MapX.Add(MapRunStart.Last() || X >= MapX.Last() ? X : MapX.Last());
	};
	AddAnchor(OnsetMs[0], OnsetX[0]);
	for (const FPianoScoreMeasure& Measure : Measures)
	{
		const int32 At = Algo::LowerBound(OnsetMs, Measure.OnMs - 2.0);
		if (OnsetMs.IsValidIndex(At) && OnsetMs[At] <= Measure.OnMs + 2.0)
		{
			AddAnchor(OnsetMs[At], OnsetX[At]);
		}
	}
	// End: the final barline (or the last onset).
	float EndX = OnsetX.Last();
	if (Measures.Num() > 0)
	{
		if (lunasvg::Element Last = Document->getElementById(TCHAR_TO_UTF8(*Measures.Last().Id)))
		{
			const lunasvg::Box Box = Last.getGlobalBoundingBox();
			EndX = FMath::Max(EndX, float(Box.x + Box.w));
		}
	}
	if (LengthMs > MapMs.Last() + 1.0 && EndX > MapX.Last())
	{
		AddAnchor(LengthMs, EndX);
	}
	if (MapMs.Num() == 1)
	{
		MapSlope.Add(0.1);
		return;
	}

	// Slopes for a monotone cubic (Fritsch-Carlson) within each run: smooth speed, never backwards.
	const int32 N = MapMs.Num();
	TArray<double> Secant;
	for (int32 I = 0; I + 1 < N; ++I)
	{
		Secant.Add(MapRunStart[I + 1] ? -1.0 : (MapX[I + 1] - MapX[I]) / FMath::Max(1.0, MapMs[I + 1] - MapMs[I]));
	}
	MapSlope.Init(0.0, N);
	for (int32 I = 0; I < N; ++I)
	{
		const bool bHasLeft = I > 0 && Secant[I - 1] >= 0.0;
		const bool bHasRight = I + 1 < N && Secant[I] >= 0.0;
		const double Left = bHasLeft ? Secant[I - 1] : 0.0, Right = bHasRight ? Secant[I] : 0.0;
		MapSlope[I] = bHasLeft && bHasRight ? (Left * Right <= 0.0 ? 0.0 : 0.5 * (Left + Right)) : bHasLeft ? Left : Right;
	}
	for (int32 I = 0; I + 1 < N; ++I)
	{
		if (Secant[I] <= 0.0)
		{
			continue;
		}
		const double A = MapSlope[I] / Secant[I], B = MapSlope[I + 1] / Secant[I];
		if (A * A + B * B > 9.0)
		{
			const double Tau = 3.0 / FMath::Sqrt(A * A + B * B);
			MapSlope[I] = Tau * A * Secant[I];
			MapSlope[I + 1] = Tau * B * Secant[I];
		}
	}
}

float FPianoScore::TimeToX(double Ms) const
{
	if (MapMs.Num() == 0)
	{
		return 0.f;
	}
	if (MapMs.Num() == 1 || Ms <= MapMs[0])
	{
		// before the first note, at the speed it starts with
		return float(MapX[0] + (Ms - MapMs[0]) * (MapSlope.Num() > 0 && MapSlope[0] > 0.0 ? MapSlope[0] : 0.1));
	}
	if (Ms >= MapMs.Last())
	{
		return MapX.Last();
	}
	const int32 Hi = Algo::UpperBound(MapMs, Ms);
	const int32 Lo = Hi - 1;
	const double H = FMath::Max(1e-6, MapMs[Hi] - MapMs[Lo]);
	if (MapRunStart[Hi])
	{
		// up to a repeat: on at the same speed, then the jump back
		return float(MapX[Lo] + MapSlope[Lo] * (Ms - MapMs[Lo]));
	}
	const double T = (Ms - MapMs[Lo]) / H, T2 = T * T, T3 = T2 * T;
	return float((2.0 * T3 - 3.0 * T2 + 1.0) * MapX[Lo] + (T3 - 2.0 * T2 + T) * H * MapSlope[Lo]
		+ (-2.0 * T3 + 3.0 * T2) * MapX[Hi] + (T3 - T2) * H * MapSlope[Hi]);
}

double FPianoScore::XToTime(float X, double NearMs) const
{
	if (MapMs.Num() < 2)
	{
		return 0.0;
	}
	// every run (pass) that covers X; within a run x only grows with time
	double Best = X <= MapX[0] ? MapMs[0] : MapMs.Last();
	double BestDistance = TNumericLimits<double>::Max();
	for (int32 First = 0; First < MapMs.Num();)
	{
		int32 End = First + 1;
		while (End < MapMs.Num() && !MapRunStart[End])
		{
			++End;
		}
		double Lo = MapMs[First], Hi = End < MapMs.Num() ? MapMs[End] - 1.0 : MapMs.Last();
		if (X >= TimeToX(Lo) && X <= TimeToX(Hi))
		{
			for (int32 Step = 0; Step < 40; ++Step)
			{
				const double Mid = 0.5 * (Lo + Hi);
				(TimeToX(Mid) < X ? Lo : Hi) = Mid;
			}
			if (FMath::Abs(Lo - NearMs) < BestDistance)
			{
				BestDistance = FMath::Abs(Lo - NearMs);
				Best = Lo;
			}
		}
		First = End;
	}
	return Best;
}

TArray<FPianoSessionNote> FPianoScore::MakeSessionNotes(bool bExpressive) const
{
	TArray<FPianoSessionNote> Out;
	Out.Reserve(Notes.Num());
	for (const FPianoScoreNote& Note : Notes)
	{
		FPianoSessionNote& S = Out.AddDefaulted_GetRef();
		S.Pitch = Note.Pitch;
		S.OnMs = Note.OnMs;
		// staccato sounds shorter (the written length stays the beat it takes)
		const double Length = !bExpressive ? 1.0 : (Note.Artic & ArticStaccatissimo) ? 0.35 : (Note.Artic & ArticStaccato) ? 0.5 : 1.0;
		S.OffMs = Note.OnMs + FMath::Max(30.0, Note.DurMs * Length);
		S.Hand = Note.Hand;
		S.Velocity = bExpressive ? Note.Velocity : 80;
		S.bTied = !Note.IsStruck();
		S.bGrace = Note.bGrace;
		S.SourceIndex = Out.Num() - 1;
	}
	return Out;
}

double FPianoScore::GetMeasureStartMs(int32 Index) const
{
	return Measures.IsValidIndex(Index) ? Measures[Index].OnMs : (Index <= 0 ? 0.0 : LengthMs);
}

double FPianoScore::GetMeasureEndMs(int32 Index) const
{
	return Measures.IsValidIndex(Index + 1) ? Measures[Index + 1].OnMs : LengthMs;
}

int32 FPianoScore::GetMeasureAt(double Ms) const
{
	const int32 Next = Algo::UpperBoundBy(Measures, Ms + 0.5, &FPianoScoreMeasure::OnMs);
	return FMath::Max(0, Next - 1);
}

TArray<double> FPianoScore::GetBeatTimes() const
{
	TArray<double> Beats;
	for (int32 Index = 0; Index < Measures.Num(); ++Index)
	{
		const FPianoScoreMeasure& M = Measures[Index];
		const double EndMs = GetMeasureEndMs(Index);
		// quarters in this measure: from the next measure's stamp, else from the duration at the previous rate
		double Quarters = Measures.IsValidIndex(Index + 1) ? Measures[Index + 1].QStamp - M.QStamp : 0.0;
		if (Quarters <= 0.0)
		{
			Quarters = Beats.Num() >= 2 ? (EndMs - M.OnMs) / FMath::Max(1.0, Beats.Last() - Beats[Beats.Num() - 2]) : 4.0;
		}
		const double QuarterMs = (EndMs - M.OnMs) / FMath::Max(0.25, Quarters);
		for (double Q = 0.0; Q < Quarters - 1e-6; Q += 1.0)
		{
			Beats.Add(M.OnMs + Q * QuarterMs);
		}
	}
	return Beats;
}
