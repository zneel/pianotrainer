// The whole app window. The menu (SPianoHome) or the practice screen:
//   top bar | scrolling sheet | stage (falling notes over the keyboard) | measure ruler | transport bar,
//   with the settings sidebar on the right and walnut framing the stage. Falling notes are optional (Visual tab).
// The stage is drawn by Slate (2D); -keyboard3d renders it with the engine instead.
// Also handles the computer keyboard as a piano (A W S E D F T G Y H U J K O L P ; = C4..E5, Z/X octave),
// shortcuts, dropped files and mouse playing on the 3D keys.
#pragma once

#include "CoreMinimal.h"
#include "PianoLibrary.h"
#include "PianoViewSettings.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"

class FPianoCoach;
class SPianoHome;
class SPianoKeyboard;
class SSheetStrip;
class UPianoPlayer;

class SPianoApp : public SCompoundWidget
{
public:
	// Key under a viewport pixel (3D keyboard), or INDEX_NONE.
	using FPickPitch = TFunction<int32(const FVector2D& ViewportPixel)>;

	SLATE_BEGIN_ARGS(SPianoApp) : _StartInMenu(true) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UPianoPlayer>, Player)
		// When set (-keyboard3d), the stage is left transparent for the engine-rendered falling notes and keyboard.
		SLATE_ARGUMENT(FPickPitch, PickPitch)
		SLATE_ARGUMENT(bool, StartInMenu)
	SLATE_END_ARGS()

	SPianoApp();
	virtual ~SPianoApp() override;

	void Construct(const FArguments& InArgs);
	bool OpenScore(const FString& Path);

	// The stage (window fractions: 0 = left / top); false while the menu covers it or without a 3D keyboard.
	bool GetStageRect(float& OutLeft, float& OutTop, float& OutRight, float& OutBottom) const;
	// Top of the 3D keyboard inside the stage (window fraction), when falling notes take the rest.
	float GetKeyboardTop() const;
	const FPianoViewSettings& GetSettings() const { return Settings; }
	bool IsMenuShown() const { return bMenu; }
	FPianoCoach* GetCoach() const { return Coach.Get(); }
	void ShowMenu(bool bShow);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
	TSharedRef<SWidget> MakePractice();
	TSharedRef<SWidget> MakeTopBar();
	FText GetSubtitle() const;
	FText GetTitle() const;
	void ShowOpenDialog();
	// Loads the piece (in the background) and practises it; AfterLoad runs once it is on screen.
	void Practise(const FPianoLibraryEntry& Entry, TFunction<void()> AfterLoad = nullptr);
	// Opens a piece of the diary at one of its plan's sections and starts a session there.
	void PractiseSection(const FString& Piece, int32 Section);
	FPianoLibraryEntry EntryForPiece(const FString& Piece) const;
	TArray<FPianoLibraryEntry> GetRecentEntries() const;
	void RememberRecent(const FString& Path);
	void RefreshLibrary();
	int32 ComputerKeyToPitch(const FKey& Key) const;
	int32 PickAt(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) const;

	TWeakObjectPtr<UPianoPlayer> Player;
	FPickPitch PickPitch;
	FPianoViewSettings Settings;
	TUniquePtr<FPianoCoach> Coach;
	TArray<FPianoLibraryEntry> LibraryEntries;
	TArray<FString> Recent;
	TOptional<FPianoLibraryEntry> Current; // what is being practised (drills, focus)

	TSharedPtr<SPianoHome> Home;
	TSharedPtr<SSheetStrip> Sheet;
	TSharedPtr<SPianoKeyboard> Keyboard;
	TSharedPtr<SWidget> Stage;
	SVerticalBox::FSlot* StageSlot = nullptr;
	bool bStageFills = true;
	int32 SeenScoreSerial = -1;

	bool bMenu = true;
	bool bSidebar = true;
	FString Message;
	int32 MouseNote = INDEX_NONE;
	int32 ComputerOctave = 0;
	TMap<FKey, int32> ComputerKeysDown;
};
