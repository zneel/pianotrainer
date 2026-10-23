// Look of the app: a dark, warm stage (espresso panels, walnut wood, brushed metal) with gold accents, after the
// reference the user chose. Colours, fonts and cached brushes; the wood texture is generated at startup.
#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/CoreStyle.h"

struct FSlateBrush;

namespace PianoStyle
{
	inline FLinearColor Hex(const TCHAR* Code) { return FLinearColor(FColor::FromHex(Code)); }

	inline const FLinearColor Background = Hex(TEXT("0D0A08"));
	inline const FLinearColor Panel = Hex(TEXT("17120F"));
	inline const FLinearColor PanelRaised = Hex(TEXT("221A15"));
	inline const FLinearColor PanelHover = Hex(TEXT("2E241D"));
	inline const FLinearColor PanelLine = Hex(TEXT("3A2D24"));
	inline const FLinearColor Text = Hex(TEXT("F3E9DC"));
	inline const FLinearColor TextDim = Hex(TEXT("A8998A"));
	inline const FLinearColor TextFaint = Hex(TEXT("6B5E54"));
	inline const FLinearColor Paper = Hex(TEXT("FFFFFF"));

	inline const FLinearColor Gold = Hex(TEXT("F2B33D"));
	inline const FLinearColor GoldDeep = Hex(TEXT("C98A1E"));
	inline const FLinearColor GoldGlow = Hex(TEXT("FFD27A"));
	inline const FLinearColor GoldShade = Hex(TEXT("3B2A12"));
	inline const FLinearColor Copper = Hex(TEXT("E8743B"));

	// on the white sheet, slightly deeper so they read on paper
	inline const FLinearColor RightHand = Hex(TEXT("D99A1E"));
	inline const FLinearColor LeftHand = Hex(TEXT("D9622B"));
	inline const FLinearColor Good = Hex(TEXT("4ADE80"));
	inline const FLinearColor Bad = Hex(TEXT("F05252"));
	inline const FLinearColor Cursor = Hex(TEXT("D99A1E"));
	inline const FLinearColor LoopShade = Hex(TEXT("FDE68A"));

	inline FLinearColor HandColor(uint8 Hand) { return Hand == 2 ? LeftHand : RightHand; }

	inline FSlateFontInfo Font(int32 Size, const TCHAR* Weight = TEXT("Regular"), int32 LetterSpacing = 0)
	{
		FSlateFontInfo Info = FCoreStyle::GetDefaultFontStyle(Weight, Size);
		Info.LetterSpacing = LetterSpacing;
		return Info;
	}

	// Small caps-style section labels ("GUIDED PRACTICE").
	inline FSlateFontInfo Label() { return Font(8, TEXT("Bold"), 140); }

	// Rounded box brushes, cached by look (Slate keeps pointers to brushes).
	const FSlateBrush* Rounded(const FLinearColor& Fill, float Radius, const FLinearColor& Outline = FLinearColor::Transparent, float OutlineWidth = 0.f);
	// Walnut, generated once (tiled).
	const FSlateBrush* Wood();
	const FSlateBrush* White();
}
