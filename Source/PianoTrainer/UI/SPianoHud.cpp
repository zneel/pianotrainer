#include "SPianoHud.h"

#include "PianoStyle.h"

#include "PianoPlayer.h"

#include "Widgets/Layout/SBorder.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "PianoHud"

void SPianoHud::Construct(const FArguments& InArgs)
{
	Player = InArgs._Player;

	ChildSlot
	[
		SNew(SOverlay)
		.Visibility(EVisibility::SelfHitTestInvisible)
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			// count-in: big beat numbers
			SNew(STextBlock)
			.Visibility_Lambda([this]() { const UPianoPlayer* P = Player.Get(); return P && P->GetCountInLeft() > 0 ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			.Font(PianoStyle::Font(72, TEXT("Bold")))
			.ColorAndOpacity(PianoStyle::GoldDeep)
			.ShadowOffset(FVector2D(0.0, 3.0))
			.ShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.35f))
			.Text_Lambda([this]() { const UPianoPlayer* P = Player.Get(); return FText::AsNumber(P ? P->GetCountInLeft() : 0); })
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBorder)
			.Visibility_Lambda([this]() { const UPianoPlayer* P = Player.Get(); return P && P->IsLoadingScore() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			.BorderImage(PianoStyle::Rounded(PianoStyle::Panel.CopyWithNewOpacity(0.92f), 10.f))
			.Padding(FMargin(22.f, 12.f))
			[
				SNew(STextBlock).Font(PianoStyle::Font(12, TEXT("Bold"))).ColorAndOpacity(PianoStyle::TextDim).Text(LOCTEXT("Loading", "Engraving the score…"))
			]
		]
	];
}

#undef LOCTEXT_NAMESPACE
