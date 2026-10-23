#include "PianoNoteHighway.h"

#include "PianoKeyboard3D.h"
#include "PianoPlayer.h"
#include "PianoScore.h"

#include "Camera/CameraComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace PianoNoteHighwayImpl
{
	// Linear colours (the glow material multiplies them by an intensity; bloom does the rest).
	const FLinearColor Gold(1.f, 0.42f, 0.05f);
	const FLinearColor Copper(1.f, 0.2f, 0.04f);
	const FLinearColor Played(0.2f, 1.f, 0.3f);
	const FLinearColor Missed(1.f, 0.08f, 0.06f);
	const FLinearColor Line(1.f, 0.55f, 0.2f);

	constexpr float BarDepth = 0.5f;
	constexpr float CubeSize = 100.f; // the engine cube

	FTransform Box(const FVector& Center, const FMatrix& Basis, const FVector& Size)
	{
		return FTransform(Basis.ToQuat(), Center, Size / CubeSize);
	}
}

APianoNoteHighway::APianoNoteHighway()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork; // after the keyboard placed its camera this frame
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

UInstancedStaticMeshComponent* APianoNoteHighway::MakeLayer(const TCHAR* Name, UStaticMesh* Mesh, UMaterialInterface* Material)
{
	UInstancedStaticMeshComponent* Layer = NewObject<UInstancedStaticMeshComponent>(this, Name);
	Layer->SetupAttachment(Root);
	Layer->SetStaticMesh(Mesh);
	Layer->SetMaterial(0, Material);
	Layer->SetNumCustomDataFloats(4);
	Layer->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Layer->SetCastShadow(false);
	Layer->bAffectDistanceFieldLighting = false;
	Layer->SetMobility(EComponentMobility::Movable);
	Layer->RegisterComponent();
	return Layer;
}

void APianoNoteHighway::Init(UPianoPlayer* InPlayer, APianoKeyboard3D* InKeyboard)
{
	Player = InPlayer;
	Keyboard = InKeyboard;
	SetActorTransform(InKeyboard->GetActorTransform());

	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UMaterialInterface* Glow = LoadObject<UMaterialInterface>(nullptr, GlowMaterialPath);
	if (!Glow)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s missing (run -run=PianoAssetBuilder); falling notes use the default material"), GlowMaterialPath);
		Glow = UMaterial::GetDefaultMaterial(MD_Surface);
	}
	Notes = MakeLayer(TEXT("Notes"), Cube, Glow);
	Effects = MakeLayer(TEXT("Effects"), Cube, Glow);

	// A dark, matte stage behind the notes (the piano's lacquer material, nearly black).
	if (UMaterialInterface* Surface = LoadObject<UMaterialInterface>(nullptr, APianoKeyboard3D::SurfaceMaterialPath))
	{
		Backdrop = NewObject<UStaticMeshComponent>(this, TEXT("Backdrop"));
		Backdrop->SetupAttachment(Root);
		Backdrop->SetStaticMesh(Cube);
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Surface, this);
		Material->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.010f, 0.0075f, 0.006f));
		Material->SetScalarParameterValue(TEXT("Roughness"), 0.9f);
		Material->SetScalarParameterValue(TEXT("ClearCoat"), 0.f);
		Backdrop->SetMaterial(0, Material);
		Backdrop->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Backdrop->SetCastShadow(false);
		Backdrop->RegisterComponent();
	}
}

void APianoNoteHighway::SetShown(bool bShow)
{
	bShown = bShow;
	SetActorHiddenInGame(!bShow);
}

bool APianoNoteHighway::ComputeFrame(FVector& OutOrigin, FVector& OutUp, FVector& OutNormal, float& OutLength) const
{
	const APianoKeyboard3D* Board = Keyboard.Get();
	UCameraComponent* Camera = Board ? Board->GetCamera() : nullptr;
	FVector2D ViewSize(1600.0, 900.0);
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->GetViewportSize(ViewSize);
	}
	if (!Camera || ViewSize.X < 1.0 || ViewSize.Y < 1.0)
	{
		return false;
	}
	// Everything in this actor's space (the keyboard's).
	const FTransform ToLocal = GetActorTransform().Inverse();
	const FVector CameraPos = ToLocal.TransformPosition(Camera->GetComponentLocation());
	const FQuat CameraRot = ToLocal.TransformRotation(Camera->GetComponentQuat());
	OutUp = CameraRot.GetUpVector();
	OutNormal = -CameraRot.GetForwardVector();
	OutOrigin = APianoKeyboard3D::NoteLandingPoint();

	// Length: up the view plane until the far end reaches the top of the stage on screen.
	const double TanV = FMath::Tan(FMath::DegreesToRadians(Camera->FieldOfView * 0.5)) / (ViewSize.X / ViewSize.Y);
	auto ScreenY = [&](const FVector& Point)
	{
		const FVector Local = CameraRot.UnrotateVector(Point - CameraPos);
		return Local.X > 1.0 ? 0.5 - 0.5 * Local.Z / (Local.X * TanV) : -1.0;
	};
	const double Target = StageTop + 0.012;
	double Lo = 0.0, Hi = 800.0;
	if (ScreenY(OutOrigin) <= Target)
	{
		return false; // no room above the keys
	}
	for (int32 Step = 0; Step < 40; ++Step)
	{
		const double Mid = (Lo + Hi) * 0.5;
		(ScreenY(OutOrigin + OutUp * Mid) > Target ? Lo : Hi) = Mid;
	}
	OutLength = float(Lo);
	return OutLength > 2.f;
}

void APianoNoteHighway::SpawnSparks(const FVector& At, const FLinearColor& Color, int32 Count)
{
	if (!bSparks || Sparks.Num() > 900)
	{
		return;
	}
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FSpark& S = Sparks.AddDefaulted_GetRef();
		S.Position = At + FVector(Random.FRandRange(-0.3f, 0.3f), Random.FRandRange(-0.5f, 0.5f), 0.f);
		S.Velocity = FVector(Random.FRandRange(-3.f, 3.f), Random.FRandRange(-9.f, 9.f), Random.FRandRange(6.f, 26.f)); // in (normal, right, up) units
		S.Life = Random.FRandRange(0.3f, 0.75f);
		S.Color = FMath::Lerp(Color, FLinearColor(1.f, 0.9f, 0.7f), Random.FRandRange(0.f, 0.6f));
	}
}

void APianoNoteHighway::Upload(UInstancedStaticMeshComponent* Layer, const TArray<FInstance>& Instances, int32& Capacity)
{
	if (!Layer)
	{
		return;
	}
	if (Instances.Num() > Capacity)
	{
		TArray<FTransform> Extra;
		Extra.Init(FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector), Instances.Num() - Capacity + 64);
		Layer->AddInstances(Extra, false, false, false);
		Capacity += Extra.Num();
	}
	TArray<FTransform> Transforms;
	Transforms.Reserve(Capacity);
	for (const FInstance& I : Instances)
	{
		Transforms.Add(I.Transform);
	}
	Transforms.SetNum(Capacity, EAllowShrinking::No);
	for (int32 Index = Instances.Num(); Index < Capacity; ++Index)
	{
		Transforms[Index] = FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector); // unused: zero size
	}
	Layer->BatchUpdateInstancesTransforms(0, Transforms, false, false, true);
	for (int32 Index = 0; Index < Instances.Num(); ++Index)
	{
		const FInstance& I = Instances[Index];
		const float Data[4] = { I.Color.R, I.Color.G, I.Color.B, I.Intensity };
		Layer->SetCustomData(Index, MakeArrayView(Data, 4), false);
	}
	Layer->MarkRenderStateDirty();
}

void APianoNoteHighway::Tick(float DeltaSeconds)
{
	using namespace PianoNoteHighwayImpl;
	Super::Tick(DeltaSeconds);
	UPianoPlayer* P = Player.Get();
	FVector Origin, Up, Normal;
	float Length = 0.f;
	if (!bShown || !P || !P->GetScore().IsValid() || !ComputeFrame(Origin, Up, Normal, Length))
	{
		TArray<FInstance> None;
		Upload(Notes, None, NotesCapacity);
		Upload(Effects, None, EffectsCapacity);
		if (Backdrop)
		{
			Backdrop->SetVisibility(false);
		}
		return;
	}
	const FVector Right(0.f, 1.f, 0.f);
	const FMatrix Basis = FRotationMatrix::MakeFromXZ(Normal, Up);
	const double Now = P->GetSongMs();
	const double Window = double(LookaheadMs) * FMath::Max(0.1f, P->GetTempoScale());
	auto Along = [&](double Ms) { return float((Ms - Now) / Window * Length); };
	const float BoardWidth = APianoKeyboard3D::GetKeyboardWidth();
	const uint8 Hands = uint8(P->GetHands());
	const bool bListen = P->GetListenMode();
	const bool bAccentDrill = P->GetDrill() == EPianoDrill::AccentThree || P->GetDrill() == EPianoDrill::AccentFour;

	if (Backdrop)
	{
		Backdrop->SetVisibility(true);
		Backdrop->SetRelativeTransform(Box(Origin + Up * (Length * 0.5f + 6.f) - Normal * 1.2f, Basis, FVector(0.2f, BoardWidth + 40.f, Length + 30.f)));
	}

	// Sparks for notes that sound now: the app's notes when they reach the line, the player's when they are played.
	const TArray<FPianoSessionNote>& PlayNotes = P->GetPlayNotes();
	if (LandedSerial != P->GetScoreSerial() || Landed.Num() != PlayNotes.Num() || Now < LastNowMs - 50.0)
	{
		const bool bReset = LandedSerial != P->GetScoreSerial() || Landed.Num() != PlayNotes.Num();
		if (bReset)
		{
			Landed.Init(false, PlayNotes.Num());
			LandedSerial = P->GetScoreSerial();
		}
		for (int32 Index = 0; Index < PlayNotes.Num(); ++Index)
		{
			if (PlayNotes[Index].OnMs >= Now - 1.0)
			{
				Landed[Index] = false; // jumped back: these can spark again
			}
		}
	}

	TArray<FInstance> Bars;
	const int32 First = Algo::LowerBoundBy(PlayNotes, Now - 30000.0, &FPianoSessionNote::OnMs);
	for (int32 Index = First; Index < PlayNotes.Num(); ++Index)
	{
		const FPianoSessionNote& N = PlayNotes[Index];
		if (N.OnMs > Now + Window)
		{
			break;
		}
		if (N.OffMs <= Now || N.bTied || N.Pitch < 21 || N.Pitch > 108)
		{
			continue;
		}
		const bool bUser = !bListen && (uint8(N.Hand) & Hands) != 0;
		const EPianoNoteState State = P->GetNoteState(N.SourceIndex);
		FLinearColor Color = (bColorByHand && N.Hand == EPianoHand::Left) ? Copper : Gold;
		float Intensity = bUser ? 1.25f : 0.5f;
		if (bUser && State == EPianoNoteState::Hit) { Color = Played; }
		else if (bUser && State == EPianoNoteState::Missed) { Color = Missed; }
		const bool bSounding = N.OnMs <= Now;
		if (bSounding)
		{
			Intensity *= 2.f;
		}

		const FVector2f Span = APianoKeyboard3D::KeySpan(N.Pitch);
		const bool bBlack = APianoKeyboard3D::IsBlackKey(N.Pitch);
		float Width = (Span.Y - Span.X) * (bBlack ? 0.95f : 0.82f);
		if (bAccentDrill && N.bAccent)
		{
			Width *= 1.15f;
			Intensity *= 1.3f;
		}
		const float A = FMath::Max(0.f, Along(N.OnMs));
		const float B = FMath::Min(Length, FMath::Max(A + 0.35f, Along(N.OffMs)));
		if (B <= A)
		{
			continue;
		}
		const FVector Center = Origin + Up * ((A + B) * 0.5f) + Right * ((Span.X + Span.Y) * 0.5f) + Normal * (bBlack ? 0.9f : 0.5f);
		Bars.Add({ Box(Center, Basis, FVector(BarDepth, Width, B - A)), Color, Intensity });

		// sparks where it lands
		if (bSounding && Landed.IsValidIndex(Index) && !Landed[Index] && (!bUser || State == EPianoNoteState::Hit))
		{
			Landed[Index] = true;
			SpawnSparks(FVector(1.2f, (Span.X + Span.Y) * 0.5f, 0.f), bUser ? Color : Gold, bUser ? 12 : 6);
		}
	}
	// the player's hits that happen while the clock waits (wait mode) spark too
	for (int32 Index = First; Index < PlayNotes.Num() && PlayNotes[Index].OnMs <= Now + 60.0; ++Index)
	{
		const FPianoSessionNote& N = PlayNotes[Index];
		if (Landed.IsValidIndex(Index) && !Landed[Index] && !N.bTied && P->GetNoteState(N.SourceIndex) == EPianoNoteState::Hit)
		{
			Landed[Index] = true;
			const FVector2f Span = APianoKeyboard3D::KeySpan(N.Pitch);
			SpawnSparks(FVector(1.2f, (Span.X + Span.Y) * 0.5f, 0.f), Gold, 12);
		}
	}
	LastNowMs = Now;
	Upload(Notes, Bars, NotesCapacity);

	// Lines: landing line, measure lines, octave lanes; then the sparks.
	TArray<FInstance> Fx;
	Fx.Add({ Box(Origin + Normal * 0.3f, Basis, FVector(0.3f, BoardWidth + 1.f, 0.22f)), Gold, 3.2f });
	if (const TSharedPtr<FPianoScore> Score = P->GetScore())
	{
		for (const FPianoScoreMeasure& M : Score->GetMeasures())
		{
			const float D = Along(M.OnMs);
			if (D > 0.f && D < Length)
			{
				Fx.Add({ Box(Origin + Up * D, Basis, FVector(0.1f, BoardWidth, 0.07f)), Line, 0.32f });
			}
		}
	}
	for (int32 Pitch = 24; Pitch <= 108; Pitch += 12)
	{
		const FVector2f Span = APianoKeyboard3D::KeySpan(Pitch);
		Fx.Add({ Box(Origin + Up * (Length * 0.5f) + Right * (Span.X - 0.05f) - Normal * 0.2f, Basis, FVector(0.1f, 0.06f, Length)), Line, 0.12f });
	}
	for (int32 Index = Sparks.Num() - 1; Index >= 0; --Index)
	{
		FSpark& S = Sparks[Index];
		S.Age += DeltaSeconds;
		if (S.Age >= S.Life)
		{
			Sparks.RemoveAtSwap(Index);
			continue;
		}
		S.Velocity.Z -= 70.f * DeltaSeconds; // falls back on screen
		S.Velocity *= FMath::Pow(0.25f, DeltaSeconds);
		S.Position += S.Velocity * DeltaSeconds;
		const float T = S.Age / S.Life;
		const FVector World = Origin + Normal * S.Position.X + Right * S.Position.Y + Up * S.Position.Z;
		const float Size = 0.34f * (1.f - T * 0.7f);
		Fx.Add({ Box(World, Basis, FVector(Size)), S.Color, 5.f * (1.f - T) });
	}
	Upload(Effects, Fx, EffectsCapacity);
}
