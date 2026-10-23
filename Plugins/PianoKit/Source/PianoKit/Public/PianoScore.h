// A score engraved by Verovio as one long horizontal system (flowkey-style scrolling line),
// with every note's pitch, timing, hand and notehead position on that line.
#pragma once

#include "CoreMinimal.h"
#include "PianoSession.h"

namespace lunasvg { class Document; }

struct FPianoScoreNote
{
	FString Id;          // Verovio xml:id, also the SVG element id
	int32 Pitch = 60;    // MIDI note number
	double OnMs = 0.0;   // at the score's written tempo
	double DurMs = 0.0;
	int32 Staff = 1;     // 1-based staff index within the system
	EPianoHand Hand = EPianoHand::Right;
	FBox2f Head = FBox2f(ForceInit); // notehead box in SVG pixels
	// For the later notes of a tie: index of the tie's first note (which carries the whole duration). Else INDEX_NONE.
	int32 TieRoot = INDEX_NONE;
	bool bGrace = false; // ornament: playable and scored when played, but wait mode doesn't stop for it
	bool IsStruck() const { return TieRoot == INDEX_NONE; }
};

struct FPianoScoreMeasure
{
	FString Id;
	double OnMs = 0.0;
	double QStamp = 0.0; // start in quarter notes from the beginning of the piece
	float X = 0.f;      // left edge in SVG pixels
};

class PIANOKIT_API FPianoScore
{
public:
	FPianoScore();
	~FPianoScore();

	// MusicXML (.musicxml/.xml), compressed MusicXML (.mxl), MEI, Humdrum, ABC; .mscz/.mscx via MuseScore if installed.
	bool LoadFile(const FString& Path, FString& OutError);

	const FString& GetTitle() const { return Title; }
	const FString& GetSourcePath() const { return SourcePath; }
	const TArray<FPianoScoreNote>& GetNotes() const { return Notes; }
	const TArray<FPianoScoreMeasure>& GetMeasures() const { return Measures; }
	FVector2f GetSize() const { return Size; }
	double GetLengthMs() const { return LengthMs; }
	int32 GetNumStaves() const { return NumStaves; }

	// Song time of every quarter-note beat (from the measures' quarter stamps; tempo is constant within a measure).
	TArray<double> GetBeatTimes() const;
	// Start / end of measure Index (end = the next measure's start, or the end of the piece).
	double GetMeasureStartMs(int32 Index) const;
	double GetMeasureEndMs(int32 Index) const;
	// Measure playing at Ms (0 before the first).
	int32 GetMeasureAt(double Ms) const;

	// Scroll mapping between song time and x on the engraved line (SVG pixels).
	float TimeToX(double Ms) const;
	double XToTime(float X) const;

	lunasvg::Document* GetDocument() const { return Document.Get(); }
	// What the sequencer plays: one entry per score note (SourceIndex = its index here), sorted by time.
	TArray<FPianoSessionNote> MakeSessionNotes() const;

	static FString GetVerovioDataDir();
	static FString FindMuseScore();

private:
	bool ConvertWithMuseScore(const FString& InPath, FString& OutPath, FString& OutError) const;
	void BuildScrollMap();

	FString Title;
	FString SourcePath;
	TArray<FPianoScoreNote> Notes;
	TArray<FPianoScoreMeasure> Measures;
	FVector2f Size = FVector2f::ZeroVector;
	double LengthMs = 0.0;
	int32 NumStaves = 0;

	TArray<double> MapMs;
	TArray<float> MapX;

	TUniquePtr<lunasvg::Document> Document;
};
