#include "PianoWidgets.h"

#include "PianoStyle.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace PianoWidgetsImpl
{
	// Vertical gradient (Top -> Bottom) over a box.
	static void Gradient(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const FVector2f& Pos, const FVector2f& Size,
		const FLinearColor& Top, const FLinearColor& Bottom, float Radius = 0.f)
	{
		if (Size.X <= 0.f || Size.Y <= 0.f)
		{
			return;
		}
		TArray<FSlateGradientStop> Stops;
		Stops.Emplace(FVector2f(0.f, 0.f), Top);
		Stops.Emplace(Size, Bottom);
		FSlateDrawElement::MakeGradient(Out, Layer, Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(Pos)), MoveTemp(Stops),
			Orient_Horizontal, ESlateDrawEffect::None, FVector4f(Radius));
	}

	static void Disc(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const FVector2f& Center, float Radius, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeBox(Out, Layer, Geometry.ToPaintGeometry(FVector2f(Radius * 2.f), FSlateLayoutTransform(Center - FVector2f(Radius))),
			PianoStyle::Rounded(FLinearColor::White, Radius), ESlateDrawEffect::None, Color);
	}

	static const FButtonStyle& ButtonStyle(PianoUI::EButton Kind)
	{
		// White brushes so ButtonColorAndOpacity sets the exact colour; hover/press shade it.
		static TMap<uint8, FButtonStyle> Styles;
		FButtonStyle* Found = Styles.Find(uint8(Kind));
		if (Found)
		{
			return *Found;
		}
		const float Radius = Kind == PianoUI::EButton::Chip ? 11.f : Kind == PianoUI::EButton::Icon ? 5.f : 7.f;
		FButtonStyle S;
		if (Kind == PianoUI::EButton::Ghost)
		{
			// outline only: the button colour tints the outline
			S.SetNormal(*PianoStyle::Rounded(FLinearColor::Transparent, Radius, FLinearColor::White, 1.f));
			S.SetHovered(*PianoStyle::Rounded(FLinearColor(1.f, 1.f, 1.f, 0.08f), Radius, FLinearColor::White, 1.f));
			S.SetPressed(*PianoStyle::Rounded(FLinearColor(1.f, 1.f, 1.f, 0.16f), Radius, FLinearColor::White, 1.f));
		}
		else
		{
			S.SetNormal(*PianoStyle::Rounded(FLinearColor::White, Radius));
			S.SetHovered(*PianoStyle::Rounded(FLinearColor(0.86f, 0.86f, 0.86f), Radius));
			S.SetPressed(*PianoStyle::Rounded(FLinearColor(0.7f, 0.7f, 0.7f), Radius));
		}
		S.SetDisabled(*PianoStyle::Rounded(FLinearColor(0.5f, 0.5f, 0.5f, 0.5f), Radius));
		S.SetNormalPadding(FMargin(0.f));
		S.SetPressedPadding(FMargin(0.f));
		return Styles.Add(uint8(Kind), S);
	}
}

TSharedRef<SWidget> PianoUI::Button(TAttribute<FText> Label, TFunction<void()> OnClick, EButton Kind, TAttribute<bool> IsActive, TAttribute<bool> IsEnabled, int32 FontSize)
{
	using namespace PianoWidgetsImpl;
	const FMargin Padding = Kind == EButton::Chip ? FMargin(10.f, 3.f) : Kind == EButton::Icon ? FMargin(10.f, 6.f) : FMargin(14.f, 7.f);
	return SNew(SButton)
		.ButtonStyle(&ButtonStyle(Kind))
		.IsFocusable(false)
		.IsEnabled(IsEnabled)
		.ContentPadding(Padding)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.ButtonColorAndOpacity_Lambda([Kind, IsActive]()
		{
			const bool bActive = IsActive.Get();
			switch (Kind)
			{
			case EButton::Primary: return PianoStyle::Gold;
			case EButton::Ghost: return bActive ? PianoStyle::Gold : PianoStyle::PanelLine;
			case EButton::Chip: return bActive ? PianoStyle::Gold : PianoStyle::PanelRaised;
			case EButton::Icon: return bActive ? PianoStyle::Gold : PianoStyle::Hex(TEXT("D8D2C8"));
			default: return bActive ? PianoStyle::Gold : PianoStyle::PanelRaised;
			}
		})
		.OnClicked_Lambda([OnClick]() { OnClick(); return FReply::Handled(); })
		[
			SNew(STextBlock)
			.Font(PianoStyle::Font(FontSize, TEXT("Bold")))
			.ColorAndOpacity_Lambda([Kind, IsActive]()
			{
				const bool bOnGold = Kind == EButton::Primary || Kind == EButton::Icon || (IsActive.Get() && Kind != EButton::Ghost);
				return FSlateColor(bOnGold ? PianoStyle::Hex(TEXT("1A120A")) : (Kind == EButton::Ghost && IsActive.Get() ? PianoStyle::Gold : PianoStyle::Text));
			})
			.Text(Label)
		];
}

TSharedRef<SWidget> PianoUI::Segmented(const TArray<FText>& Options, TFunction<int32()> Get, TFunction<void(int32)> Set)
{
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	for (int32 Index = 0; Index < Options.Num(); ++Index)
	{
		Row->AddSlot().AutoWidth().Padding(FMargin(Index > 0 ? 3.f : 0.f, 0.f, 0.f, 0.f))
		[
			Button(Options[Index], [Set, Index]() { Set(Index); }, EButton::Chip, TAttribute<bool>::CreateLambda([Get, Index]() { return Get() == Index; }), true, 9)
		];
	}
	return SNew(SBorder)
		.BorderImage(PianoStyle::Rounded(PianoStyle::Background, 13.f))
		.Padding(3.f)
		[
			Row
		];
}

TSharedRef<SWidget> PianoUI::SectionLabel(const FText& Text)
{
	return SNew(SBox).Padding(FMargin(0.f, 14.f, 0.f, 6.f))
		[
			SNew(STextBlock).Font(PianoStyle::Label()).ColorAndOpacity(PianoStyle::TextFaint).Text(Text)
		];
}

TSharedRef<SWidget> PianoUI::Row(const FText& Label, const TSharedRef<SWidget>& Control)
{
	return SNew(SBox).Padding(FMargin(0.f, 4.f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Font(PianoStyle::Font(9, TEXT("Bold"), 60)).ColorAndOpacity(PianoStyle::TextDim).Text(Label)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				Control
			]
		];
}

TSharedRef<SWidget> PianoUI::Separator()
{
	return SNew(SBox).HeightOverride(1.f).Padding(FMargin(0.f, 8.f, 0.f, 0.f))
		[
			SNew(SBorder).BorderImage(PianoStyle::White()).BorderBackgroundColor(PianoStyle::PanelLine)
		];
}

// ---------------------------------------------------------------- switch

void SPianoSwitch::Construct(const FArguments& InArgs)
{
	IsOn = InArgs._IsOn;
	OnToggled = InArgs._OnToggled;
}

int32 SPianoSwitch::OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool) const
{
	using namespace PianoWidgetsImpl;
	const FVector2f Size = Geometry.GetLocalSize();
	const bool bOn = IsOn.Get();
	FSlateDrawElement::MakeBox(Out, Layer, Geometry.ToPaintGeometry(), PianoStyle::Rounded(FLinearColor::White, Size.Y * 0.5f), ESlateDrawEffect::None,
		bOn ? PianoStyle::Gold : PianoStyle::PanelLine);
	const float R = Size.Y * 0.5f - 2.f;
	Disc(Out, Layer + 1, Geometry, FVector2f(bOn ? Size.X - Size.Y * 0.5f : Size.Y * 0.5f, Size.Y * 0.5f), R, bOn ? PianoStyle::Hex(TEXT("FFF6E5")) : PianoStyle::TextDim);
	return Layer + 1;
}

FReply SPianoSwitch::OnMouseButtonDown(const FGeometry&, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		OnToggled.ExecuteIfBound();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

// ---------------------------------------------------------------- slider

void SPianoSlider::Construct(const FArguments& InArgs)
{
	Value = InArgs._Value;
	Min = InArgs._Min;
	Max = InArgs._Max;
	Step = InArgs._Step;
	OnChanged = InArgs._OnChanged;
}

int32 SPianoSlider::OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool) const
{
	using namespace PianoWidgetsImpl;
	const FVector2f Size = Geometry.GetLocalSize();
	const float T = FMath::Clamp((Value.Get() - Min) / FMath::Max(1e-6f, Max.Get() - Min), 0.f, 1.f);
	const float Y = Size.Y * 0.5f;
	const float Left = 7.f, Right = Size.X - 7.f;
	FSlateDrawElement::MakeBox(Out, Layer, Geometry.ToPaintGeometry(FVector2f(Right - Left, 4.f), FSlateLayoutTransform(FVector2f(Left, Y - 2.f))),
		PianoStyle::Rounded(FLinearColor::White, 2.f), ESlateDrawEffect::None, PianoStyle::PanelLine);
	FSlateDrawElement::MakeBox(Out, Layer + 1, Geometry.ToPaintGeometry(FVector2f((Right - Left) * T, 4.f), FSlateLayoutTransform(FVector2f(Left, Y - 2.f))),
		PianoStyle::Rounded(FLinearColor::White, 2.f), ESlateDrawEffect::None, PianoStyle::Gold);
	Disc(Out, Layer + 2, Geometry, FVector2f(FMath::Lerp(Left, Right, T), Y), 6.5f, PianoStyle::Hex(TEXT("F5EEE2")));
	return Layer + 2;
}

void SPianoSlider::SetFromX(const FGeometry& Geometry, const FPointerEvent& Event)
{
	const FVector2f Local = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
	const float Width = FMath::Max(1.f, Geometry.GetLocalSize().X - 14.f);
	float V = FMath::Lerp(Min, Max.Get(), FMath::Clamp((Local.X - 7.f) / Width, 0.f, 1.f));
	if (Step > 0.f)
	{
		V = Min + FMath::RoundToFloat((V - Min) / Step) * Step;
	}
	OnChanged.ExecuteIfBound(V);
}

FReply SPianoSlider::OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	SetFromX(Geometry, Event);
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SPianoSlider::OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (HasMouseCapture())
	{
		SetFromX(Geometry, Event);
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SPianoSlider::OnMouseButtonUp(const FGeometry&, const FPointerEvent&)
{
	return HasMouseCapture() ? FReply::Handled().ReleaseMouseCapture() : FReply::Unhandled();
}

// ---------------------------------------------------------------- knob

void SPianoKnob::Construct(const FArguments& InArgs)
{
	Value = InArgs._Value;
	Min = InArgs._Min;
	Max = InArgs._Max;
	Default = InArgs._Default;
	OnChanged = InArgs._OnChanged;
}

int32 SPianoKnob::OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool) const
{
	using namespace PianoWidgetsImpl;
	const FVector2f Size = Geometry.GetLocalSize();
	const FVector2f C = Size * 0.5f;
	const float R = FMath::Min(Size.X, Size.Y) * 0.5f;
	// scale: 270 degrees, ticks
	constexpr float Sweep = 270.f, Start = 135.f; // degrees, clockwise from +X (Slate's Y is down)
	TArray<FVector2f> Tick;
	for (int32 I = 0; I <= 10; ++I)
	{
		const float A = FMath::DegreesToRadians(Start + Sweep * I / 10.f);
		const FVector2f D(FMath::Cos(A), FMath::Sin(A));
		Tick = { C + D * (R - 1.f), C + D * (R - (I % 5 == 0 ? 6.f : 4.f)) };
		FSlateDrawElement::MakeLines(Out, Layer, Geometry.ToPaintGeometry(), Tick, ESlateDrawEffect::None, PianoStyle::TextFaint, true, 1.2f);
	}
	// machined body: dark rim, lighter face, highlight
	const float Body = R - 8.f;
	Disc(Out, Layer + 1, Geometry, C, Body, PianoStyle::Hex(TEXT("0B0907")));
	Disc(Out, Layer + 2, Geometry, C, Body - 1.5f, PianoStyle::Hex(TEXT("3A3632")));
	Disc(Out, Layer + 3, Geometry, C - FVector2f(0.f, 1.5f), Body - 4.f, PianoStyle::Hex(TEXT("5A554F")));
	Disc(Out, Layer + 4, Geometry, C, Body - 6.f, PianoStyle::Hex(TEXT("2B2825")));
	// pointer
	const float T = FMath::Clamp((Value.Get() - Min) / FMath::Max(1e-6f, Max - Min), 0.f, 1.f);
	const float A = FMath::DegreesToRadians(Start + Sweep * T);
	const FVector2f D(FMath::Cos(A), FMath::Sin(A));
	FSlateDrawElement::MakeLines(Out, Layer + 5, Geometry.ToPaintGeometry(), { C + D * 3.f, C + D * (Body - 2.f) }, ESlateDrawEffect::None, PianoStyle::Gold, true, 2.5f);
	return Layer + 5;
}

void SPianoKnob::Set(float NewValue)
{
	OnChanged.ExecuteIfBound(FMath::Clamp(FMath::RoundToFloat(NewValue * 100.f) / 100.f, Min, Max));
}

FReply SPianoKnob::OnMouseButtonDown(const FGeometry&, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	DragStartValue = Value.Get();
	DragStartY = Event.GetScreenSpacePosition().Y;
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SPianoKnob::OnMouseMove(const FGeometry&, const FPointerEvent& Event)
{
	if (!HasMouseCapture())
	{
		return FReply::Unhandled();
	}
	Set(DragStartValue + (DragStartY - Event.GetScreenSpacePosition().Y) * (Max - Min) / 220.f);
	return FReply::Handled();
}

FReply SPianoKnob::OnMouseButtonUp(const FGeometry&, const FPointerEvent&)
{
	return HasMouseCapture() ? FReply::Handled().ReleaseMouseCapture() : FReply::Unhandled();
}

FReply SPianoKnob::OnMouseWheel(const FGeometry&, const FPointerEvent& Event)
{
	Set(Value.Get() + 0.05f * FMath::Sign(Event.GetWheelDelta()));
	return FReply::Handled();
}

FReply SPianoKnob::OnMouseButtonDoubleClick(const FGeometry&, const FPointerEvent&)
{
	Set(Default);
	return FReply::Handled();
}

// ---------------------------------------------------------------- counter

int32 SPianoCounter::OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool) const
{
	using namespace PianoWidgetsImpl;
	const FVector2f Size = Geometry.GetLocalSize();
	FSlateDrawElement::MakeBox(Out, Layer, Geometry.ToPaintGeometry(), PianoStyle::Rounded(FLinearColor::White, 4.f), ESlateDrawEffect::None, PianoStyle::Hex(TEXT("060504")));
	const FString Text = FString::Printf(TEXT("%0*d"), Digits, FMath::Clamp(Value.Get(), 0, int32(FMath::Pow(10.f, float(Digits))) - 1));
	const float CellW = (Size.X - 6.f) / Digits;
	const FSlateFontInfo Font = PianoStyle::Font(13, TEXT("Bold"));
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	for (int32 I = 0; I < Text.Len(); ++I)
	{
		const FVector2f Pos(3.f + I * CellW + 1.f, 3.f);
		const FVector2f Cell(CellW - 2.f, Size.Y - 6.f);
		// the wheel: lighter in the middle, dark at its top and bottom edges
		Gradient(Out, Layer + 1, Geometry, Pos, FVector2f(Cell.X, Cell.Y * 0.5f), PianoStyle::Hex(TEXT("1A1715")), PianoStyle::Hex(TEXT("3C3833")), 2.f);
		Gradient(Out, Layer + 1, Geometry, Pos + FVector2f(0.f, Cell.Y * 0.5f), FVector2f(Cell.X, Cell.Y * 0.5f), PianoStyle::Hex(TEXT("3C3833")), PianoStyle::Hex(TEXT("1A1715")), 2.f);
		const FString Digit = Text.Mid(I, 1);
		const FVector2f TextSize = FVector2f(Measure->Measure(Digit, Font));
		FSlateDrawElement::MakeText(Out, Layer + 2, Geometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(Pos + (Cell - TextSize) * 0.5f)),
			Digit, Font, ESlateDrawEffect::None, PianoStyle::Hex(TEXT("F1EADF")));
	}
	return Layer + 2;
}

// ---------------------------------------------------------------- plate

void SPianoPlate::Construct(const FArguments& InArgs)
{
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	if (!InArgs._Label.IsEmpty())
	{
		Box->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.f, 0.f, 0.f, 4.f))
		[
			SNew(STextBlock).Font(PianoStyle::Label()).ColorAndOpacity(PianoStyle::TextDim).Text(InArgs._Label)
		];
	}
	Box->AddSlot().FillHeight(1.f).HAlign(InArgs._ContentHAlign).VAlign(VAlign_Center)[InArgs._Content.Widget];
	ChildSlot.Padding(InArgs._Padding)[Box];
}

int32 SPianoPlate::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const
{
	using namespace PianoWidgetsImpl;
	const FVector2f Size = Geometry.GetLocalSize();
	// outer bevel, then the brushed face
	FSlateDrawElement::MakeBox(Out, Layer, Geometry.ToPaintGeometry(), PianoStyle::Rounded(FLinearColor::White, 6.f), ESlateDrawEffect::None, PianoStyle::Hex(TEXT("050404")));
	Gradient(Out, Layer + 1, Geometry, FVector2f(1.f, 1.f), Size - FVector2f(2.f), PianoStyle::Hex(TEXT("4A4540")), PianoStyle::Hex(TEXT("1C1A18")), 5.f);
	Gradient(Out, Layer + 2, Geometry, FVector2f(2.f, 2.f), Size - FVector2f(4.f), PianoStyle::Hex(TEXT("2E2B28")), PianoStyle::Hex(TEXT("191715")), 4.f);
	// screws
	for (const FVector2f& Corner : { FVector2f(6.f, 6.f), FVector2f(Size.X - 6.f, 6.f), FVector2f(6.f, Size.Y - 6.f), FVector2f(Size.X - 6.f, Size.Y - 6.f) })
	{
		Disc(Out, Layer + 3, Geometry, Corner, 1.8f, PianoStyle::Hex(TEXT("6E6760")));
	}
	return SCompoundWidget::OnPaint(Args, Geometry, Culling, Out, Layer + 4, Style, bParentEnabled);
}
