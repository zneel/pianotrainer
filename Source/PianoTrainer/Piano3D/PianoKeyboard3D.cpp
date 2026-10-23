#include "PianoKeyboard3D.h"

#include "PianoPlayer.h"
#include "../UI/PianoStyle.h"

#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureCube.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"

// Keyboard space (cm, UE axes): X runs from the front edge of the keys (0) to the back (15), Y from the lowest key
// (left, -Y) to the highest, Z up from the key bottoms. The camera sits in front (-X), above, looking at the keys.
namespace PianoKeyboard3DImpl
{
	constexpr int32 LowestPitch = 21;  // A0
	constexpr int32 HighestPitch = 108; // C8
	constexpr int32 NumWhite = 52;
	constexpr float WhitePitch = 2.35f;     // octave = 16.45 cm, a standard keyboard
	constexpr float HalfGap = 0.055f;
	constexpr float WhiteLength = 15.f;
	constexpr float WhiteHeadLength = 5.f;  // full-width front part
	constexpr float WhiteHeight = 2.2f;
	constexpr float BlackWidth = 1.32f;
	constexpr float BlackLength = 9.5f;
	constexpr float BlackHeight = 3.35f;    // top of a black key above the key bottoms
	constexpr float WhiteBevel = 0.12f;
	constexpr float BlackBevel = 0.3f;
	constexpr float PressDegrees = 3.6f;    // front of a white key drops ~0.95 cm
	constexpr float KeyboardWidth = NumWhite * WhitePitch;
	constexpr float CheekWidth = 4.5f;

	bool IsBlack(int32 Pitch)
	{
		const int32 N = Pitch % 12;
		return N == 1 || N == 3 || N == 6 || N == 8 || N == 10;
	}

	int32 WhiteIndex(int32 Pitch)
	{
		static const int32 WhitesBefore[12] = { 0, 1, 1, 2, 2, 3, 4, 4, 5, 5, 6, 6 };
		return (Pitch / 12) * 7 + WhitesBefore[Pitch % 12] - 12; // A0 = 0
	}

	float WhiteLeftEdge(int32 Pitch) { return -KeyboardWidth * 0.5f + WhiteIndex(Pitch) * WhitePitch; }

	// Black key centre: on the boundary after the white key below it.
	float BlackCenter(int32 Pitch) { return -KeyboardWidth * 0.5f + (WhiteIndex(Pitch - 1) + 1) * WhitePitch; }

	struct FMeshData
	{
		TArray<FVector> Vertices;
		TArray<int32> Triangles;
		TArray<FVector> Normals;
		TArray<FVector2D> UVs;

		void Quad(const FVector& A, const FVector& B, const FVector& C, const FVector& D)
		{
			const FVector Normal = FVector::CrossProduct(B - A, C - A).GetSafeNormal() * -1.f; // UE: clockwise front faces
			const int32 Base = Vertices.Num();
			Vertices.Append({ A, B, C, D });
			Normals.Append({ Normal, Normal, Normal, Normal });
			UVs.Append({ FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1), FVector2D(0, 1) });
			Triangles.Append({ Base, Base + 1, Base + 2, Base, Base + 2, Base + 3 });
		}

		// Box with its top edges bevelled; faces wound so they face outwards (the material is two-sided anyway).
		void BevelBox(const FVector& Min, const FVector& Max, float Bevel)
		{
			const float X0 = Min.X, X1 = Max.X, Y0 = Min.Y, Y1 = Max.Y, Z0 = Min.Z, Z1 = Max.Z;
			const float ZB = Z1 - Bevel, B = Bevel;
			// sides (front -X, back +X, left -Y, right +Y)
			Quad({ X0, Y1, Z0 }, { X0, Y0, Z0 }, { X0, Y0, ZB }, { X0, Y1, ZB });
			Quad({ X1, Y0, Z0 }, { X1, Y1, Z0 }, { X1, Y1, ZB }, { X1, Y0, ZB });
			Quad({ X0, Y0, Z0 }, { X1, Y0, Z0 }, { X1, Y0, ZB }, { X0, Y0, ZB });
			Quad({ X1, Y1, Z0 }, { X0, Y1, Z0 }, { X0, Y1, ZB }, { X1, Y1, ZB });
			// bevels
			Quad({ X0, Y1, ZB }, { X0, Y0, ZB }, { X0 + B, Y0 + B, Z1 }, { X0 + B, Y1 - B, Z1 });
			Quad({ X1, Y0, ZB }, { X1, Y1, ZB }, { X1 - B, Y1 - B, Z1 }, { X1 - B, Y0 + B, Z1 });
			Quad({ X0, Y0, ZB }, { X1, Y0, ZB }, { X1 - B, Y0 + B, Z1 }, { X0 + B, Y0 + B, Z1 });
			Quad({ X1, Y1, ZB }, { X0, Y1, ZB }, { X0 + B, Y1 - B, Z1 }, { X1 - B, Y1 - B, Z1 });
			// top
			Quad({ X0 + B, Y0 + B, Z1 }, { X1 - B, Y0 + B, Z1 }, { X1 - B, Y1 - B, Z1 }, { X0 + B, Y1 - B, Z1 });
		}
	};

	struct FSurface
	{
		FLinearColor Color;
		float Roughness;
		float ClearCoat;
		float ClearCoatRoughness;
	};
	const FSurface Ivory{ FLinearColor(0.82f, 0.80f, 0.74f), 0.32f, 1.f, 0.05f };
	const FSurface Ebony{ FLinearColor(0.012f, 0.012f, 0.014f), 0.3f, 1.f, 0.06f };
	const FSurface Lacquer{ FLinearColor(0.004f, 0.004f, 0.005f), 0.6f, 0.3f, 0.4f }; // satin: glossy black washes out grey at grazing angles
	const FSurface Floor{ FLinearColor(0.035f, 0.022f, 0.014f), 0.55f, 0.2f, 0.4f }; // dark wood
	const FSurface Felt{ FLinearColor(0.32f, 0.02f, 0.03f), 0.95f, 0.f, 1.f };
}

APianoKeyboard3D::APianoKeyboard3D()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork; // after the player has updated its clock this frame
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Root);
	Camera->bConstrainAspectRatio = false;
	Camera->SetFieldOfView(30.f);

	// Fixed exposure and a little filmic softness; no motion blur on a keyboard.
	FPostProcessSettings& PP = Camera->PostProcessSettings;
	PP.bOverride_AutoExposureMethod = true;
	PP.AutoExposureMethod = AEM_Manual;
	PP.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	PP.AutoExposureApplyPhysicalCameraExposure = false;
	PP.bOverride_AutoExposureBias = true;
	PP.AutoExposureBias = 0.4f;
	PP.bOverride_BloomIntensity = true;
	PP.BloomIntensity = 0.35f;
	PP.bOverride_VignetteIntensity = true;
	PP.VignetteIntensity = 0.45f;
	PP.bOverride_MotionBlurAmount = true;
	PP.MotionBlurAmount = 0.f;
}

FVector2f APianoKeyboard3D::KeySpan(int32 Pitch)
{
	using namespace PianoKeyboard3DImpl;
	if (IsBlack(Pitch))
	{
		const float Center = BlackCenter(Pitch);
		return FVector2f(Center - BlackWidth * 0.5f, Center + BlackWidth * 0.5f);
	}
	const float Left = WhiteLeftEdge(Pitch);
	return FVector2f(Left + HalfGap, Left + WhitePitch - HalfGap);
}

bool APianoKeyboard3D::IsBlackKey(int32 Pitch)
{
	return PianoKeyboard3DImpl::IsBlack(Pitch);
}

FVector APianoKeyboard3D::NoteLandingPoint()
{
	using namespace PianoKeyboard3DImpl;
	return FVector(WhiteLength + 0.4f, 0.f, 6.f);
}

float APianoKeyboard3D::GetKeyboardWidth()
{
	return PianoKeyboard3DImpl::KeyboardWidth;
}

void APianoKeyboard3D::Init(UPianoPlayer* InPlayer)
{
	Player = InPlayer;
	UMaterialInterface* Surface = LoadObject<UMaterialInterface>(nullptr, SurfaceMaterialPath);
	if (!Surface)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s missing (run -run=PianoAssetBuilder); using the engine default material"), SurfaceMaterialPath);
		Surface = UMaterial::GetDefaultMaterial(MD_Surface);
	}
	BuildKeys(Surface);
	BuildBody(Surface);
	BuildLights();
}

void APianoKeyboard3D::BuildKeys(UMaterialInterface* Surface)
{
	using namespace PianoKeyboard3DImpl;
	Keys.SetNum(HighestPitch - LowestPitch + 1);
	for (int32 Pitch = LowestPitch; Pitch <= HighestPitch; ++Pitch)
	{
		FKey& Key = Keys[Pitch - LowestPitch];
		Key.Pitch = Pitch;
		Key.bBlack = IsBlack(Pitch);

		// Mesh in key-local space: the pivot is at the back end of the key (X = 0), the key extends towards -X.
		FMeshData Mesh;
		float CenterY = 0.f;
		if (Key.bBlack)
		{
			CenterY = BlackCenter(Pitch);
			Mesh.BevelBox(FVector(-BlackLength, -BlackWidth * 0.5f, WhiteHeight - 0.6f), FVector(0.f, BlackWidth * 0.5f, BlackHeight), BlackBevel);
		}
		else
		{
			const float Left = WhiteLeftEdge(Pitch);
			CenterY = Left + WhitePitch * 0.5f;
			// tail: narrowed where neighbouring black keys sit
			const bool bBlackBelow = Pitch > LowestPitch && IsBlack(Pitch - 1);
			const bool bBlackAbove = Pitch < HighestPitch && IsBlack(Pitch + 1);
			const float TailLeft = bBlackBelow ? BlackCenter(Pitch - 1) + BlackWidth * 0.5f + HalfGap : Left + HalfGap;
			const float TailRight = bBlackAbove ? BlackCenter(Pitch + 1) - BlackWidth * 0.5f - HalfGap : Left + WhitePitch - HalfGap;
			Mesh.BevelBox(FVector(-WhiteLength, Left + HalfGap - CenterY, 0.f), FVector(-WhiteLength + WhiteHeadLength, Left + WhitePitch - HalfGap - CenterY, WhiteHeight), WhiteBevel);
			Mesh.BevelBox(FVector(-WhiteLength + WhiteHeadLength - 0.05f, TailLeft - CenterY, 0.f), FVector(0.f, TailRight - CenterY, WhiteHeight), WhiteBevel);
		}

		UProceduralMeshComponent* Component = NewObject<UProceduralMeshComponent>(this, *FString::Printf(TEXT("Key_%d"), Pitch));
		Component->SetupAttachment(Root);
		Component->bUseAsyncCooking = false;
		Component->CreateMeshSection(0, Mesh.Vertices, Mesh.Triangles, Mesh.Normals, Mesh.UVs, {}, {}, false);
		Component->SetRelativeLocation(FVector(WhiteLength, CenterY, 0.f));
		Component->SetCastShadow(true);
		Component->RegisterComponent();

		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Surface, this);
		Component->SetMaterial(0, Material);
		Key.Mesh = Component;
		Key.Material = Material;
		Owned.Add(Component);
		Owned.Add(Material);
	}
}

void APianoKeyboard3D::BuildBody(UMaterialInterface* Surface)
{
	using namespace PianoKeyboard3DImpl;
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!Cube)
	{
		return;
	}
	auto Block = [&](const TCHAR* Name, const FVector& Min, const FVector& Max, const FSurface& Look)
	{
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(this, Name);
		Component->SetupAttachment(Root);
		Component->SetStaticMesh(Cube);
		Component->SetRelativeLocation((Min + Max) * 0.5f);
		Component->SetRelativeScale3D((Max - Min) / 100.f); // the engine cube is 100 cm
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Surface, this);
		Material->SetVectorParameterValue(TEXT("Color"), Look.Color);
		Material->SetScalarParameterValue(TEXT("Roughness"), Look.Roughness);
		Material->SetScalarParameterValue(TEXT("ClearCoat"), Look.ClearCoat);
		Material->SetScalarParameterValue(TEXT("ClearCoatRoughness"), Look.ClearCoatRoughness);
		Component->SetMaterial(0, Material);
		Component->RegisterComponent();
		Owned.Add(Component);
		Owned.Add(Material);
	};
	const float HalfW = KeyboardWidth * 0.5f;
	Block(TEXT("KeyBed"), FVector(-3.f, -HalfW - CheekWidth, -4.f), FVector(WhiteLength + 6.f, HalfW + CheekWidth, -0.05f), Lacquer);
	Block(TEXT("KeySlip"), FVector(-3.2f, -HalfW - CheekWidth, -4.f), FVector(-0.25f, HalfW + CheekWidth, 0.9f), Lacquer);
	Block(TEXT("CheekLeft"), FVector(-3.2f, -HalfW - CheekWidth, -4.f), FVector(WhiteLength + 6.f, -HalfW - 0.15f, 5.2f), Lacquer);
	Block(TEXT("CheekRight"), FVector(-3.2f, HalfW + 0.15f, -4.f), FVector(WhiteLength + 6.f, HalfW + CheekWidth, 5.2f), Lacquer);
	Block(TEXT("Felt"), FVector(WhiteLength - 0.9f, -HalfW, WhiteHeight - 0.3f), FVector(WhiteLength + 0.4f, HalfW, WhiteHeight + 0.55f), Felt);
	Block(TEXT("Fallboard"), FVector(WhiteLength + 0.4f, -HalfW - CheekWidth, -0.05f), FVector(WhiteLength + 6.f, HalfW + CheekWidth, 6.f), Lacquer);
	Block(TEXT("Floor"), FVector(-400.f, -600.f, -78.f), FVector(400.f, 600.f, -76.f), Floor);
}

void APianoKeyboard3D::BuildLights()
{
	// Sky cubemap for soft fill and reflections in the lacquer, a key light from behind the keys so their tops catch
	// a highlight towards the player, and a weak fill from the player's side.
	USkyLightComponent* Sky = NewObject<USkyLightComponent>(this, TEXT("Sky"));
	Sky->SetupAttachment(Root);
	Sky->SetMobility(EComponentMobility::Movable);
	Sky->SourceType = SLS_SpecifiedCubemap;
	Sky->Cubemap = LoadObject<UTextureCube>(nullptr, TEXT("/Engine/MapTemplates/Sky/SunsetAmbientCubemap.SunsetAmbientCubemap"));
	Sky->SetIntensity(0.22f); // a dim room: the lacquer should reflect shapes, not glare
	Sky->RegisterComponent();
	Sky->RecaptureSky();
	Owned.Add(Sky);

	UDirectionalLightComponent* KeyLight = NewObject<UDirectionalLightComponent>(this, TEXT("KeyLight"));
	KeyLight->SetupAttachment(Root);
	KeyLight->SetMobility(EComponentMobility::Movable);
	KeyLight->SetRelativeRotation(FRotator(-52.f, 160.f, 0.f)); // from above, behind the keys, towards the player
	KeyLight->SetIntensity(2.8f);
	KeyLight->SetLightColor(FLinearColor(1.f, 0.95f, 0.88f));
	KeyLight->RegisterComponent();
	Owned.Add(KeyLight);

	UDirectionalLightComponent* Fill = NewObject<UDirectionalLightComponent>(this, TEXT("FillLight"));
	Fill->SetupAttachment(Root);
	Fill->SetMobility(EComponentMobility::Movable);
	Fill->SetRelativeRotation(FRotator(-55.f, 20.f, 0.f));
	Fill->SetIntensity(0.6f);
	Fill->SetLightColor(FLinearColor(0.85f, 0.9f, 1.f));
	Fill->SetCastShadows(false);
	Fill->RegisterComponent();
	Owned.Add(Fill);
}

void APianoKeyboard3D::UpdateCamera()
{
	using namespace PianoKeyboard3DImpl;
	FVector2D ViewSize(1600.0, 900.0);
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->GetViewportSize(ViewSize);
	}
	if (ViewSize.X < 1.0 || ViewSize.Y < 1.0)
	{
		return;
	}
	// Frame the keyboard (with its cheeks) across 96 % of the rectangle's width, centred in the rectangle given by
	// the UI (the window minus the sidebar, bottom part of the stage).
	const double Aspect = ViewSize.X / ViewSize.Y;
	const double HalfH = FMath::DegreesToRadians(Camera->FieldOfView * 0.5);
	const double HalfV = FMath::Atan(FMath::Tan(HalfH) / Aspect);
	const double Pitch = FMath::DegreesToRadians(44.0); // looking down at the keys like a seated player
	const double Width = KeyboardWidth + 2.0 * CheekWidth;
	const double WidthFraction = FMath::Clamp(double(BandRight - BandLeft), 0.1, 1.0);
	const double Depth = Width / (0.96 * WidthFraction * 2.0 * FMath::Tan(HalfH));
	const double BandCenter = (BandTop + BandBottom) * 0.5;
	const double Ndc = (BandCenter - 0.5) * 2.0; // + = below the screen centre
	const double Below = FMath::Atan(Ndc * FMath::Tan(HalfV));
	const FVector Target(WhiteLength * 0.45, 0.0, WhiteHeight);
	const double RayAngle = Pitch + Below;
	const double Distance = Depth / FMath::Cos(Below);
	FVector Position = Target - FVector(FMath::Cos(RayAngle), 0.0, -FMath::Sin(RayAngle)) * Distance;
	// sideways: the keyboard centre appears at the rectangle's centre
	const double NdcX = (BandLeft + BandRight) - 1.0;
	Position.Y -= NdcX * Depth * FMath::Tan(HalfH);
	Camera->SetRelativeLocationAndRotation(Position, FRotator(-FMath::RadiansToDegrees(Pitch), 0.0, 0.0));
}

void APianoKeyboard3D::ApplyLook(FKey& Key, const FPianoKeyState& State)
{
	using namespace PianoKeyboard3DImpl;
	const FSurface& Base = Key.bBlack ? Ebony : Ivory;
	FLinearColor Tint = FLinearColor::Black;
	float Amount = 0.f, Glow = 0.f;
	if (State.bHeld)
	{
		Tint = State.Verdict == 1 ? PianoStyle::Good : State.Verdict == 0 ? PianoStyle::Bad : PianoStyle::Hex(TEXT("94A3B8"));
		Amount = 0.85f;
		Glow = State.Verdict >= 0 ? 0.8f : 0.2f;
	}
	else if (State.WrongFlash > 0.f)
	{
		Tint = PianoStyle::Bad;
		Amount = 0.85f * State.WrongFlash;
		Glow = 0.8f * State.WrongFlash;
	}
	else if (State.UpcomingHand != 0)
	{
		Tint = PianoStyle::HandColor(State.UpcomingHand);
		Amount = 0.7f;
		Glow = 0.55f;
	}
	else if (State.bAccompaniment)
	{
		Tint = PianoStyle::Hex(TEXT("94A3B8"));
		Amount = 0.35f;
	}

	const uint32 Hash = HashCombine(GetTypeHash(Tint.ToFColor(false)), HashCombine(GetTypeHash(int32(Amount * 100.f)), GetTypeHash(int32(Glow * 100.f))));
	if (Hash == Key.LookHash)
	{
		return;
	}
	Key.LookHash = Hash;
	Key.Material->SetVectorParameterValue(TEXT("Color"), FMath::Lerp(Base.Color, Tint, Amount));
	Key.Material->SetScalarParameterValue(TEXT("Roughness"), Base.Roughness);
	Key.Material->SetScalarParameterValue(TEXT("ClearCoat"), Base.ClearCoat);
	Key.Material->SetScalarParameterValue(TEXT("ClearCoatRoughness"), Base.ClearCoatRoughness);
	Key.Material->SetVectorParameterValue(TEXT("Emissive"), Tint * Glow);
}

void APianoKeyboard3D::UpdateKeys(float DeltaSeconds)
{
	UPianoPlayer* P = Player.Get();
	if (!P)
	{
		return;
	}
	TArray<FPianoKeyState> States;
	P->GetKeyStates(States);
	for (FKey& Key : Keys)
	{
		const FPianoKeyState& State = States[Key.Pitch];
		ApplyLook(Key, State);

		// Keys go down fast and come back up a little slower, like real keys on their springs.
		const float Target = (State.bHeld || State.bAccompaniment) ? PianoKeyboard3DImpl::PressDegrees : 0.f;
		const float Rate = Target > Key.Angle ? 45.f : 22.f;
		const float NewAngle = FMath::Lerp(Key.Angle, Target, FMath::Min(1.f, DeltaSeconds * Rate));
		if (!FMath::IsNearlyEqual(NewAngle, Key.Angle, 0.001f))
		{
			Key.Angle = NewAngle;
			Key.Mesh->SetRelativeRotation(FRotator(Key.Angle, 0.f, 0.f)); // pitch up = front of the key down
		}
	}
}

void APianoKeyboard3D::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateCamera();
	UpdateKeys(DeltaSeconds);
}

int32 APianoKeyboard3D::PitchAtViewportPosition(const FVector2D& Pixel) const
{
	using namespace PianoKeyboard3DImpl;
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	FVector Origin, Direction;
	if (!PC || !PC->DeprojectScreenPositionToWorld(Pixel.X, Pixel.Y, Origin, Direction) || FMath::Abs(Direction.Z) < 1e-4)
	{
		return INDEX_NONE;
	}
	auto HitAt = [&](float Z) { return Origin + Direction * ((Z - Origin.Z) / Direction.Z); };

	// Black keys stand above the white ones: test their top plane first.
	const FVector BlackHit = HitAt(BlackHeight);
	if (BlackHit.X >= WhiteLength - BlackLength && BlackHit.X <= WhiteLength)
	{
		for (int32 Pitch = LowestPitch; Pitch <= HighestPitch; ++Pitch)
		{
			if (IsBlack(Pitch) && FMath::Abs(BlackHit.Y - BlackCenter(Pitch)) <= BlackWidth * 0.5f)
			{
				return Pitch;
			}
		}
	}
	const FVector WhiteHit = HitAt(WhiteHeight);
	if (WhiteHit.X < 0.f || WhiteHit.X > WhiteLength)
	{
		return INDEX_NONE;
	}
	for (int32 Pitch = LowestPitch; Pitch <= HighestPitch; ++Pitch)
	{
		if (!IsBlack(Pitch) && WhiteHit.Y >= WhiteLeftEdge(Pitch) && WhiteHit.Y < WhiteLeftEdge(Pitch) + WhitePitch)
		{
			return Pitch;
		}
	}
	return INDEX_NONE;
}
