// Small controls in the app's look: buttons, segmented pickers, switch, slider, a metal knob (tempo), a measure
// counter with digit wheels, and a brushed-metal plate panel.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"

namespace PianoUI
{
	enum class EButton : uint8
	{
		Primary,   // gold
		Secondary, // raised panel
		Ghost,     // outline only
		Chip,      // small pill, gold when active
		Icon,      // square transport button on metal
	};

	TSharedRef<SWidget> Button(TAttribute<FText> Label, TFunction<void()> OnClick, EButton Kind = EButton::Secondary,
		TAttribute<bool> IsActive = false, TAttribute<bool> IsEnabled = true, int32 FontSize = 10);
	// One of several (Wait / Play along / Listen ...).
	TSharedRef<SWidget> Segmented(const TArray<FText>& Options, TFunction<int32()> Get, TFunction<void(int32)> Set);
	// "GUIDED PRACTICE"
	TSharedRef<SWidget> SectionLabel(const FText& Text);
	// Label on the left, control on the right.
	TSharedRef<SWidget> Row(const FText& Label, const TSharedRef<SWidget>& Control);
	TSharedRef<SWidget> Separator();
}

// iOS-style switch, gold when on.
class SPianoSwitch : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SPianoSwitch) {}
		SLATE_ATTRIBUTE(bool, IsOn)
		SLATE_EVENT(FSimpleDelegate, OnToggled)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(34.0, 18.0); }
	virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FCursorReply OnCursorQuery(const FGeometry&, const FPointerEvent&) const override { return FCursorReply::Cursor(EMouseCursor::Hand); }

private:
	TAttribute<bool> IsOn;
	FSimpleDelegate OnToggled;
};

// Thin slider: track, gold fill, round thumb. Drag or click.
class SPianoSlider : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SPianoSlider) : _Min(0.f), _Max(1.f), _Step(0.f) {}
		SLATE_ATTRIBUTE(float, Value)
		SLATE_ARGUMENT(float, Min)
		SLATE_ATTRIBUTE(float, Max)
		SLATE_ARGUMENT(float, Step)
		SLATE_EVENT(TDelegate<void(float)>, OnChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(120.0, 18.0); }
	virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;

private:
	void SetFromX(const FGeometry& Geometry, const FPointerEvent& Event);
	TAttribute<float> Value;
	TAttribute<float> Max;
	float Min = 0.f, Step = 0.f;
	TDelegate<void(float)> OnChanged;
};

// A machined metal knob with a gold pointer and a tick scale. Drag up/down or scroll; double-click resets.
class SPianoKnob : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SPianoKnob) : _Min(0.25f), _Max(2.f), _Default(1.f) {}
		SLATE_ATTRIBUTE(float, Value)
		SLATE_ARGUMENT(float, Min)
		SLATE_ARGUMENT(float, Max)
		SLATE_ARGUMENT(float, Default)
		SLATE_EVENT(TDelegate<void(float)>, OnChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(46.0, 46.0); }
	virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FCursorReply OnCursorQuery(const FGeometry&, const FPointerEvent&) const override { return FCursorReply::Cursor(EMouseCursor::ResizeUpDown); }

private:
	void Set(float NewValue);
	TAttribute<float> Value;
	float Min = 0.25f, Max = 2.f, Default = 1.f;
	float DragStartValue = 1.f;
	float DragStartY = 0.f;
	TDelegate<void(float)> OnChanged;
};

// Odometer-like digit wheels ("0014").
class SPianoCounter : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SPianoCounter) : _Digits(4) {}
		SLATE_ATTRIBUTE(int32, Value)
		SLATE_ARGUMENT(int32, Digits)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs) { Value = InArgs._Value; Digits = InArgs._Digits; }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(Digits * 17.0 + 6.0, 30.0); }

private:
	TAttribute<int32> Value;
	int32 Digits = 4;
};

// Brushed dark metal plate with a bevel (transport sections).
class SPianoPlate : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPianoPlate) : _Padding(FMargin(12.f, 8.f)), _ContentHAlign(HAlign_Center) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_ARGUMENT(FMargin, Padding)
		SLATE_ARGUMENT(FText, Label)
		// HAlign_Fill lets wide content (the progress bar) use the whole plate
		SLATE_ARGUMENT(EHorizontalAlignment, ContentHAlign)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
};
