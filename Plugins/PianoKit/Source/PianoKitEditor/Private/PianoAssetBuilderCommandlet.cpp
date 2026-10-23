#include "PianoAssetBuilderCommandlet.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Factories/MaterialFactoryNew.h"
#include "FileHelpers.h"
#include "IAssetTools.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionMin.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionPerInstanceCustomData.h"
#include "Materials/MaterialExpressionSaturate.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionVectorParameter.h"

DEFINE_LOG_CATEGORY_STATIC(LogPianoAssetBuilder, Log, All);

UPianoAssetBuilderCommandlet::UPianoAssetBuilderCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

namespace PianoAssetBuilderImpl
{
	static UMaterialExpressionVectorParameter* Vector(UMaterial* M, const TCHAR* Name, const FLinearColor& Default, int32 Y)
	{
		auto* Param = Cast<UMaterialExpressionVectorParameter>(UMaterialEditingLibrary::CreateMaterialExpression(M, UMaterialExpressionVectorParameter::StaticClass(), -500, Y));
		Param->ParameterName = Name;
		Param->DefaultValue = Default;
		return Param;
	}

	static UMaterialExpressionScalarParameter* Scalar(UMaterial* M, const TCHAR* Name, float Default, int32 Y)
	{
		auto* Param = Cast<UMaterialExpressionScalarParameter>(UMaterialEditingLibrary::CreateMaterialExpression(M, UMaterialExpressionScalarParameter::StaticClass(), -500, Y));
		Param->ParameterName = Name;
		Param->DefaultValue = Default;
		return Param;
	}
}

namespace PianoAssetBuilderImpl
{
	template <typename T>
	static T* Node(UMaterial* M, int32 X, int32 Y)
	{
		return Cast<T>(UMaterialEditingLibrary::CreateMaterialExpression(M, T::StaticClass(), X, Y));
	}

	static void Link(UMaterialExpression* From, UMaterialExpression* To, const TCHAR* Input)
	{
		UMaterialEditingLibrary::ConnectMaterialExpressions(From, TEXT(""), To, Input);
	}

	static UMaterialExpression* Binary(UMaterial* M, UClass* Class, UMaterialExpression* A, UMaterialExpression* B, int32 X, int32 Y)
	{
		UMaterialExpression* E = UMaterialEditingLibrary::CreateMaterialExpression(M, Class, X, Y);
		Link(A, E, TEXT("A"));
		Link(B, E, TEXT("B"));
		return E;
	}

	static UMaterialExpression* Constant(UMaterial* M, float Value, int32 X, int32 Y)
	{
		auto* C = Node<UMaterialExpressionConstant>(M, X, Y);
		C->R = Value;
		return C;
	}

	// Loads the material to rebuild it in place (keeps references), or creates it.
	static UMaterial* CreateOrReset(const FString& Path, const FString& Name)
	{
		UMaterial* Material = LoadObject<UMaterial>(nullptr, *(Path / Name + TEXT(".") + Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (Material)
		{
			UMaterialEditingLibrary::DeleteAllMaterialExpressions(Material);
			return Material;
		}
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		return Cast<UMaterial>(AssetTools.CreateAsset(Name, Path, UMaterial::StaticClass(), NewObject<UMaterialFactoryNew>()));
	}

	// Lacquered piano surfaces: clear coat over a base colour; keys glow (emissive) when they are hints.
	static void BuildSurface(UMaterial* Material)
	{
		Material->SetShadingModel(MSM_ClearCoat);
		Material->TwoSided = true;
		UMaterialEditingLibrary::ConnectMaterialProperty(Vector(Material, TEXT("Color"), FLinearColor(0.9f, 0.9f, 0.88f), 0), TEXT(""), MP_BaseColor);
		UMaterialEditingLibrary::ConnectMaterialProperty(Scalar(Material, TEXT("Roughness"), 0.35f, 150), TEXT(""), MP_Roughness);
		UMaterialEditingLibrary::ConnectMaterialProperty(Scalar(Material, TEXT("Specular"), 0.5f, 250), TEXT(""), MP_Specular);
		UMaterialEditingLibrary::ConnectMaterialProperty(Scalar(Material, TEXT("ClearCoat"), 1.f, 350), TEXT(""), MP_CustomData0);
		UMaterialEditingLibrary::ConnectMaterialProperty(Scalar(Material, TEXT("ClearCoatRoughness"), 0.08f, 450), TEXT(""), MP_CustomData1);
		UMaterialEditingLibrary::ConnectMaterialProperty(Vector(Material, TEXT("Emissive"), FLinearColor::Black, 550), TEXT(""), MP_EmissiveColor);
	}

	// Falling notes, sparks and highway lines (instanced cubes): unlit glow in the instance's colour
	// (per-instance custom data 0-2 = colour, 3 = intensity), brighter towards the edges of each face like a lit rim.
	static void BuildGlow(UMaterial* Material)
	{
		Material->SetShadingModel(MSM_Unlit);
		Material->bUsedWithInstancedStaticMeshes = true;
		auto* Color = Node<UMaterialExpressionPerInstanceCustomData3Vector>(Material, -900, 0);
		Color->DataIndex = 0;
		auto* Intensity = Node<UMaterialExpressionPerInstanceCustomData>(Material, -900, 150);
		Intensity->DataIndex = 3;

		auto* UV = Node<UMaterialExpressionTextureCoordinate>(Material, -1300, 300);
		auto* U = Node<UMaterialExpressionComponentMask>(Material, -1150, 260);
		U->R = true; U->G = false; U->B = false; U->A = false;
		Link(UV, U, TEXT(""));
		auto* V = Node<UMaterialExpressionComponentMask>(Material, -1150, 360);
		V->R = false; V->G = true; V->B = false; V->A = false;
		Link(UV, V, TEXT(""));
		auto* OneMinusU = Node<UMaterialExpressionOneMinus>(Material, -1000, 260);
		Link(U, OneMinusU, TEXT(""));
		auto* OneMinusV = Node<UMaterialExpressionOneMinus>(Material, -1000, 360);
		Link(V, OneMinusV, TEXT(""));
		UMaterialExpression* EdgeU = Binary(Material, UMaterialExpressionMin::StaticClass(), U, OneMinusU, -850, 260);
		UMaterialExpression* EdgeV = Binary(Material, UMaterialExpressionMin::StaticClass(), V, OneMinusV, -850, 360);
		UMaterialExpression* Edge = Binary(Material, UMaterialExpressionMin::StaticClass(), EdgeU, EdgeV, -700, 300);
		UMaterialExpression* Scaled = Binary(Material, UMaterialExpressionMultiply::StaticClass(), Edge, Constant(Material, 9.f, -850, 450), -550, 300);
		auto* Clamped = Node<UMaterialExpressionSaturate>(Material, -420, 300);
		Link(Scaled, Clamped, TEXT(""));
		auto* Rim = Node<UMaterialExpressionOneMinus>(Material, -300, 300);
		Link(Clamped, Rim, TEXT(""));
		UMaterialExpression* RimGain = Binary(Material, UMaterialExpressionMultiply::StaticClass(), Rim, Constant(Material, 1.2f, -420, 420), -180, 300);
		UMaterialExpression* Shade = Binary(Material, UMaterialExpressionAdd::StaticClass(), RimGain, Constant(Material, 0.65f, -300, 450), -60, 300);

		UMaterialExpression* Lit = Binary(Material, UMaterialExpressionMultiply::StaticClass(), Color, Intensity, -600, 60);
		UMaterialExpression* Out = Binary(Material, UMaterialExpressionMultiply::StaticClass(), Lit, Shade, 100, 150);
		UMaterialEditingLibrary::ConnectMaterialProperty(Out, TEXT(""), MP_EmissiveColor);
	}

	static bool Save(UMaterial* Material)
	{
		UMaterialEditingLibrary::RecompileMaterial(Material);
		Material->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(Material);
		const bool bSaved = UEditorLoadingAndSavingUtils::SavePackages({ Material->GetPackage() }, false);
		UE_LOG(LogPianoAssetBuilder, Display, TEXT("%s: %s"), *Material->GetPathName(), bSaved ? TEXT("saved") : TEXT("SAVE FAILED"));
		return bSaved;
	}
}

int32 UPianoAssetBuilderCommandlet::Main(const FString& Params)
{
	using namespace PianoAssetBuilderImpl;
	const FString Path = TEXT("/PianoKit/Materials");
	bool bOk = true;
	if (UMaterial* Surface = CreateOrReset(Path, TEXT("M_PianoSurface")))
	{
		BuildSurface(Surface);
		bOk &= Save(Surface);
	}
	else
	{
		bOk = false;
	}
	if (UMaterial* Glow = CreateOrReset(Path, TEXT("M_NoteGlow")))
	{
		BuildGlow(Glow);
		bOk &= Save(Glow);
	}
	else
	{
		bOk = false;
	}
	return bOk ? 0 : 1;
}
