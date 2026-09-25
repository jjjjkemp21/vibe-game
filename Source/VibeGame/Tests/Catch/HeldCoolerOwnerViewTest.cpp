// Lure T-076 tests (unreal-engineer): the carrier's OWN view of the cooler in his hands, measured with the engine's own
// projection on the real SK_FPArms in its animated CarryCooler pose. T-064's check (a clear sight line from the eye to
// the top fish) passed while PIE showed no fish: the view is only 58.7 deg tall (ULocalPlayer AspectRatio_MaintainYFOV
// with the camera's 16:9 AspectRatio keeps a 29.4 deg half-height at every window shape) and the placeholder open turn
// left the fish 30-45 deg below the view centre. These tests put the posed vertices through FMinimalViewInfo's
// first-person transform and CalculateProjectionMatrixGivenViewRectangle at the playtest's 1600x900.
// Also: the closed carry's share of the screen height, and the lighting flags that keep first-person primitives lit.
// Rules: docs/specs/catch-handling-rules.md "The cooler"; art/export/Characters/A_FPArms_CarryCooler_OpenShow.anim.md.
// Project.Catch.HeldCooler.OwnerView.*

#include "Tests/Catch/CatchTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Camera/CameraComponent.h"
#include "Camera/CameraTypes.h"
#include "Catch/LureCatchSettings.h"
#include "Catch/LureCatchSubsystem.h"
#include "Catch/LureCoolerActor.h"
#include "Character/LurePlayerCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/LureInteractionComponent.h"
#include "Math/InverseRotationMatrix.h"
#include "Progression/LureCoolerComponent.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "SceneView.h"
#include "StaticMeshResources.h"

namespace LureHeldCoolerOwnerViewTest
{
	/** The playtest window (16:9; the vertical view is the same at every aspect under MaintainYFOV) */
	const FIntPoint Screen(1600, 900);
	/** The Sprint 1 playtest's portrait window (its shots): the fix must hold at any aspect */
	const FIntPoint PortraitScreen(1008, 1164);

	/** The starter cooler's rim and liner opening in Contents space, cm (as Project.Catch.HeldCooler.Open.*) */
	constexpr double RimZ = 30.0;
	constexpr double OpeningHalfX = 18.5;
	constexpr double OpeningHalfY = 26.5;

	/** Share of the top shown fish's posed vertices its carrier must see: in the view, through the opening, clear of the lid */
	constexpr double MinSeenShare = 0.5;
	/** The open lid must not reach into the upper half of the view (screen row, 0 = top) */
	constexpr double LidHighestRow = 0.5;
	/** The open turn pivots on the rope-handle axis, so the handles stay on the carry's fists (cm; the old -35 turn: 6.6) */
	constexpr double MaxHandleDriftCm = 1.0;
	/** The closed carry's share of the screen height: a guard on the art composition (CarryCooler_Idle measures 28.5 %) */
	constexpr double MaxClosedCarryShare = 0.30;
	/** The designer's Sprint 1 target for the closed carry (needs a lower arm pose, see ClosedCarryScreenShare's info) */
	constexpr double DesignerClosedCarryShare = 0.15;

	/** The carrier's view: the engine's projection with the project's ULocalPlayer aspect-ratio axis constraint */
	struct FOwnerView
	{
		FMinimalViewInfo View;
		FMatrix ViewProjection = FMatrix::Identity;

		bool Init(const ALurePlayerCharacter* Player, const FIntPoint& Size)
		{
			UCameraComponent* Camera = Player ? Player->GetFirstPersonCamera() : nullptr;
			if (!Camera)
			{
				return false;
			}
			Camera->GetCameraView(0.0f, View);
			FSceneViewProjectionData Data;
			Data.ViewOrigin = View.Location;
			// As ULocalPlayer::GetProjectionData: world to view, then Unreal's X-forward to the renderer's Z-forward.
			Data.ViewRotationMatrix = FInverseRotationMatrix(View.Rotation) * FMatrix(
				FPlane(0, 0, 1, 0),
				FPlane(1, 0, 0, 0),
				FPlane(0, 1, 0, 0),
				FPlane(0, 0, 0, 1));
			Data.SetViewRectangle(FIntRect(0, 0, Size.X, Size.Y));
			FMinimalViewInfo::CalculateProjectionMatrixGivenViewRectangle(View, GetDefault<ULocalPlayer>()->AspectRatioAxisConstraint, Data.GetConstrainedViewRect(), Data);
			ViewProjection = Data.ComputeViewProjectionMatrix();
			return true;
		}

		/** Where a first-person point is drawn (0..1 from the top left, not clamped); false behind the near plane */
		bool Project(const FVector& World, FVector2D& OutUV) const
		{
			const FVector Drawn = View.TransformWorldToFirstPerson(World, /*bIgnoreFirstPersonScale*/ false);
			const FVector4 Clip = ViewProjection.TransformFVector4(FVector4(Drawn, 1.0));
			if (Clip.W <= View.GetFinalPerspectiveNearClipPlane())
			{
				return false;
			}
			OutUV = FVector2D(0.5 + 0.5 * Clip.X / Clip.W, 0.5 - 0.5 * Clip.Y / Clip.W);
			return true;
		}

		static bool Inside(const FVector2D& UV)
		{
			return UV.X >= 0.0 && UV.X <= 1.0 && UV.Y >= 0.0 && UV.Y <= 1.0;
		}

		/** Degrees below the view centre of a world point (the view's own frame) */
		double DegreesBelow(const FVector& World) const
		{
			const FVector Local = View.Rotation.UnrotateVector(World - View.Location);
			return FMath::RadiansToDegrees(FMath::Atan2(-Local.Z, Local.X));
		}
	};

	/** World positions of a primitive's drawn vertices: skinned in its current pose, a static mesh's LOD 0, else its bounds' corners */
	TArray<FVector> DrawnVertices(const UPrimitiveComponent* Primitive)
	{
		TArray<FVector> Out;
		const FTransform ToWorld = Primitive->GetComponentTransform();
		if (const USkeletalMeshComponent* Skinned = Cast<USkeletalMeshComponent>(Primitive))
		{
			const FSkeletalMeshRenderData* RenderData = Skinned->GetSkeletalMeshRenderData();
			const FSkinWeightVertexBuffer* Weights = (RenderData && RenderData->LODRenderData.Num() > 0) ? Skinned->GetSkinWeightBuffer(0) : nullptr;
			if (Weights && RenderData->LODRenderData[0].StaticVertexBuffers.PositionVertexBuffer.GetVertexData() && Weights->GetDataVertexBuffer()
				&& Weights->GetDataVertexBuffer()->GetWeightData() && Skinned->GetComponentSpaceTransforms().Num() > 0)
			{
				TArray<FMatrix44f> RefToLocals;
				Skinned->CacheRefToLocalMatrices(RefToLocals);
				TArray<FVector3f> Positions;
				USkinnedMeshComponent::ComputeSkinnedPositions(const_cast<USkeletalMeshComponent*>(Skinned), Positions, RefToLocals, RenderData->LODRenderData[0], *Weights);
				for (const FVector3f& Position : Positions)
				{
					Out.Add(ToWorld.TransformPosition(FVector(Position)));
				}
			}
		}
		else if (const UStaticMeshComponent* Static = Cast<UStaticMeshComponent>(Primitive))
		{
			const UStaticMesh* Mesh = Static->GetStaticMesh();
			const FStaticMeshRenderData* RenderData = Mesh ? Mesh->GetRenderData() : nullptr;
			if (RenderData && RenderData->LODResources.Num() > 0)
			{
				const FPositionVertexBuffer& Buffer = RenderData->LODResources[0].VertexBuffers.PositionVertexBuffer;
				if (Buffer.GetVertexData())
				{
					for (uint32 Index = 0; Index < Buffer.GetNumVertices(); ++Index)
					{
						Out.Add(ToWorld.TransformPosition(FVector(Buffer.VertexPosition(Index))));
					}
				}
			}
		}
		if (Out.IsEmpty())
		{
			const FBox Local = Primitive->CalcBounds(FTransform::Identity).GetBox();
			for (int32 Corner = 0; Corner < 8; ++Corner)
			{
				Out.Add(ToWorld.TransformPosition(FVector((Corner & 1) ? Local.Max.X : Local.Min.X, (Corner & 2) ? Local.Max.Y : Local.Min.Y,
					(Corner & 4) ? Local.Max.Z : Local.Min.Z)));
			}
		}
		return Out;
	}

	const UStaticMeshComponent* PartOf(const ALureCoolerActor* Cooler, const TCHAR* Name)
	{
		TInlineComponentArray<UStaticMeshComponent*> Parts(Cooler);
		for (const UStaticMeshComponent* Part : Parts)
		{
			if (Part->GetFName() == Name)
			{
				return Part;
			}
		}
		return nullptr;
	}

	/** The fish shown inside the cooler (visible components under its ContentsRoot), bottom of the pile first */
	TArray<UPrimitiveComponent*> ShownFish(const ALureCoolerActor* Cooler)
	{
		TArray<UPrimitiveComponent*> Out;
		TInlineComponentArray<UPrimitiveComponent*> Parts(Cooler);
		for (UPrimitiveComponent* Part : Parts)
		{
			if (Part->GetAttachParent() == Cooler->GetContentsRoot() && Part->IsVisible())
			{
				Out.Add(Part);
			}
		}
		return Out;
	}

	/** Poses every shown skinned fish now (headless never refreshes the bones on its own) */
	void RefreshShownPoses(ALureCoolerActor* Cooler)
	{
		for (UPrimitiveComponent* Part : ShownFish(Cooler))
		{
			if (USkeletalMeshComponent* Skinned = Cast<USkeletalMeshComponent>(Part))
			{
				Skinned->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
				for (int32 Step = 0; Step < 10; ++Step)
				{
					Skinned->TickAnimation(1.0f / 60.0f, false);
					Skinned->RefreshBoneTransforms();
				}
			}
		}
	}

	/** How much of one shown fish its carrier sees */
	struct FFishSight
	{
		int32 Vertices = 0;
		int32 InView = 0;
		int32 Seen = 0;
		double TopRow = 2.0;
		double BottomRow = -1.0;
		double Share() const { return Vertices > 0 ? static_cast<double>(Seen) / Vertices : 0.0; }
	};

	/** Each vertex: drawn inside the view, below the rim only if its line to the eye leaves through the opening, and clear of the lid */
	FFishSight SeeFish(const FOwnerView& View, const ALureCoolerActor* Cooler, const UPrimitiveComponent* Fish)
	{
		FFishSight Out;
		const FVector Eye = View.View.Location;
		const FTransform Contents = Cooler->GetContentsRoot()->GetComponentTransform();
		const FVector EyeLocal = Contents.InverseTransformPosition(Eye);
		const UStaticMeshComponent* Lid = PartOf(Cooler, TEXT("LidMesh"));
		const FBox LidBox = (Lid && Lid->GetStaticMesh()) ? Lid->GetStaticMesh()->GetBoundingBox() : FBox(ForceInit);
		const FTransform LidTransform = Lid ? Lid->GetComponentTransform() : FTransform::Identity;
		for (const FVector& Vertex : DrawnVertices(Fish))
		{
			++Out.Vertices;
			FVector2D UV;
			if (!View.Project(Vertex, UV) || !FOwnerView::Inside(UV))
			{
				continue;
			}
			++Out.InView;
			const FVector Local = Contents.InverseTransformPosition(Vertex);
			if (Local.Z < RimZ)
			{
				if (EyeLocal.Z <= RimZ)
				{
					continue;
				}
				const FVector Cross = FMath::Lerp(Local, EyeLocal, (RimZ - Local.Z) / (EyeLocal.Z - Local.Z));
				if (FMath::Abs(Cross.X) > OpeningHalfX || FMath::Abs(Cross.Y) > OpeningHalfY)
				{
					continue;
				}
			}
			if (LidBox.IsValid)
			{
				const FVector Start = LidTransform.InverseTransformPosition(Vertex);
				const FVector End = LidTransform.InverseTransformPosition(Eye);
				if (FMath::LineBoxIntersection(LidBox, Start, End, End - Start))
				{
					continue;
				}
			}
			++Out.Seen;
			Out.TopRow = FMath::Min(Out.TopRow, UV.Y);
			Out.BottomRow = FMath::Max(Out.BottomRow, UV.Y);
		}
		return Out;
	}

	/** Screen rows (0 = top) a part covers, over its vertices in front of the near plane (not clamped to the screen) */
	FVector2D RowsOf(const FOwnerView& View, const UPrimitiveComponent* Part, double DropCm = 0.0)
	{
		FVector2D Rows(TNumericLimits<double>::Max(), TNumericLimits<double>::Lowest());
		const FVector Down = -View.View.Rotation.RotateVector(FVector::UpVector) * DropCm;
		for (const FVector& Vertex : DrawnVertices(Part))
		{
			FVector2D UV;
			if (View.Project(Vertex + Down, UV))
			{
				Rows.X = FMath::Min(Rows.X, UV.Y);
				Rows.Y = FMath::Max(Rows.Y, UV.Y);
			}
		}
		return Rows;
	}

	/** How far (cm) the rope handles sit from where the carry bone (and so the fists) holds them: the visual turn's drift */
	double HandleDrift(const ALureCoolerActor* Cooler)
	{
		const UStaticMeshComponent* Body = PartOf(Cooler, TEXT("BodyMesh"));
		if (!Body || !Body->DoesSocketExist(TEXT("Handle_L")))
		{
			return 0.0;
		}
		double Drift = 0.0;
		for (const TCHAR* Socket : { TEXT("Handle_L"), TEXT("Handle_R") })
		{
			const FVector Local = Body->GetSocketTransform(Socket, RTS_Component).GetLocation();
			const FVector OnBone = Cooler->GetRootComponent()->GetComponentTransform().TransformPosition(Local);
			Drift = FMath::Max(Drift, FVector::Dist(Body->GetSocketLocation(Socket), OnBone));
		}
		return Drift;
	}

	/** A player on the dock with a cooler in front of him holding NumFish; he carries it (the rig of Project.Catch.HeldCooler.Open.*) */
	struct FRig
	{
		LCT::FWorld W;
		ALurePlayerCharacter* Player = nullptr;
		ULureInteractionComponent* Use = nullptr;
		ALureCoolerActor* Cooler = nullptr;

		bool Create(FAutomationTestBase& Test, int32 NumFish)
		{
			if (!W.Create(Test))
			{
				return false;
			}
			Player = W.SpawnPlayer(Test, FVector(0.0f, 0.0f, LCT::DockTop));
			Use = LCT::InteractionOf(Player);
			Cooler = W.SpawnCooler(FVector(130.0f, 0.0f, LCT::DockTop), 180.0f);
			if (!Test.TestNotNull(TEXT("player"), Player) || !Test.TestNotNull(TEXT("use keys"), Use) || !Test.TestNotNull(TEXT("cooler"), Cooler))
			{
				return false;
			}
			for (int32 Index = 0; Index < NumFish; ++Index)
			{
				Cooler->GetStorage()->AddFish(FLureCaughtFish::Landed(LCT::MakeFish(Index % 2 ? TEXT("CoralSnapper") : TEXT("Bonefish"), 10 + Index, 1,
					1.2f + 0.8f * Index, 6400 + Index), W.Now()));
			}
			W.Tick(20);
			return true;
		}

		/** The lane's SK_FPArms with the CarryCooler attach bone: the owner's view needs the real carry */
		bool HasArmsBone() const
		{
			const USkeletalMeshComponent* Arms = Player ? Player->GetFirstPersonArms() : nullptr;
			return Arms && Arms->GetSkeletalMeshAsset() && Arms->DoesSocketExist(GetDefault<ULureCatchSettings>()->CarriedCoolerSocket);
		}

		bool Press(FAutomationTestBase& Test, ELureInteractKey Key, ELureInteractVerb Verb, const FString& What)
		{
			const FLureResolvedInteraction Now = Use->ResolveInteraction(Key);
			return Test.TestEqual(What + TEXT(": the key's verb"), LCT::VerbName(Now.Verb), LCT::VerbName(Verb)) && Test.TestTrue(What, Use->PressKey(Key));
		}

		bool PickUp(FAutomationTestBase& Test)
		{
			LCT::LookAt(Player, Cooler->GetInteractionLocation());
			W.Tick(2);
			if (!Press(Test, ELureInteractKey::Secondary, ELureInteractVerb::PickUpCooler, TEXT("F picks the cooler up")))
			{
				return false;
			}
			Look(0.0f);
			Advance(0.5f); // the carry clip plays a few frames
			return Test.TestTrue(TEXT("carried in both hands"), Cooler->IsHeldBy(Player, ELureHoldMode::Hand));
		}

		/** E opens it in the hands; waits for the lid and the open turn */
		bool Open(FAutomationTestBase& Test)
		{
			if (!Press(Test, ELureInteractKey::Primary, ELureInteractVerb::OpenCooler, TEXT("E opens the carried cooler")))
			{
				return false;
			}
			const FLureCatchRow& Tuning = ULureCatchSubsystem::GetTuningFor(W.World);
			const float LidSeconds = Tuning.LidOpenTime * FMath::Max(1.0f, Tuning.CarriedOpenLidPitch / FMath::Max(1.0f, Tuning.LidOpenPitch));
			Advance(LidSeconds + 0.3f);
			return Test.TestEqual(TEXT("the open turn is complete"), Cooler->GetOpenPoseAlpha(), 1.0f, 1.0e-4f);
		}

		/** Looks straight ahead at Pitch (degrees, negative = down) */
		void Look(float Pitch)
		{
			if (APlayerController* Controller = LCT::ControllerOf(Player))
			{
				Controller->SetControlRotation(FRotator(Pitch, Player->GetActorRotation().Yaw, 0.0f));
			}
			SyncCamera();
			W.Tick(3);
			SyncCamera();
		}

		/** Headless has no camera manager pass: the first-person camera takes the control rotation as a view would */
		void SyncCamera() const
		{
			if (UCameraComponent* Camera = Player ? Player->GetFirstPersonCamera() : nullptr)
			{
				FMinimalViewInfo View;
				Camera->GetCameraView(0.0f, View);
			}
		}

		void Advance(float Seconds)
		{
			for (float Done = 0.0f; Done < Seconds; Done += LCT::Dt)
			{
				SyncCamera();
				W.Tick(1);
			}
			SyncCamera();
		}
	};

	// ---- 1. Open in your hands: you see your fish (the Sprint 1 bug) ----

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOwnerViewOpenShowsFish, "Project.Catch.HeldCooler.OwnerView.OpenShowsFish", LCT::Flags)
	bool FOwnerViewOpenShowsFish::RunTest(const FString& Parameters)
	{
		for (const int32 NumFish : { 1, 4 })
		{
			FRig Rig;
			if (!Rig.Create(*this, NumFish) || !Rig.PickUp(*this))
			{
				return false;
			}
			if (!Rig.HasArmsBone())
			{
				AddWarning(TEXT("SK_FPArms (or its cooler bone) is not loaded here: the owner's view can't be measured"));
				return true;
			}
			if (!Rig.Open(*this))
			{
				return false;
			}
			ALureCoolerActor* Cooler = Rig.Cooler;
			const UStaticMeshComponent* Lid = PartOf(Cooler, TEXT("LidMesh"));
			const double Drift = HandleDrift(Cooler);
			TestTrue(FString::Printf(TEXT("%d fish: the rope handles stay within %.0f cm of the carry's fists (%.1f cm)"), NumFish, MaxHandleDriftCm, Drift),
				Drift <= MaxHandleDriftCm);
			// Straight ahead, a normal downward look and the playtest's 45 deg look down (the cooler rides the arms: all alike),
			// in the 16:9 playtest window and the Sprint 1 portrait one (MaintainYFOV: the same rows at every aspect).
			for (const float Pitch : { 0.0f, -25.0f, -45.0f })
			for (const FIntPoint& Size : { Screen, PortraitScreen })
			{
				Rig.Look(Pitch);
				RefreshShownPoses(Cooler);
				FOwnerView View;
				if (!TestTrue(TEXT("the owner's view"), View.Init(Rig.Player, Size)))
				{
					return false;
				}
				const FString Case = FString::Printf(TEXT("%d fish, pitch %.0f, %dx%d"), NumFish, Pitch, Size.X, Size.Y);
				const TArray<UPrimitiveComponent*> Fish = ShownFish(Cooler);
				ULureCatchSubsystem* Subsystem = ULureCatchSubsystem::Get(Rig.W.World);
				const FLureCoolerDisplayRow Row = Subsystem ? Subsystem->GetCoolerDisplayRow(Cooler->GetCoolerId()) : FLureCoolerDisplayRow::GetFallbackRow();
				if (!TestEqual(Case + TEXT(": fish shown = min(fish, slots)"), Fish.Num(), FMath::Min(NumFish, Row.Slots.Num())) || Fish.IsEmpty())
				{
					return false;
				}
				const FFishSight Top = SeeFish(View, Cooler, Fish.Last());
				TestTrue(FString::Printf(TEXT("%s: the top fish is seen by its carrier (%d of %d vertices in the view, %d also clear of the walls and lid = %.0f %%, rows %.0f-%.0f %%, centre %.1f deg below)"),
					*Case, Top.InView, Top.Vertices, Top.Seen, 100.0 * Top.Share(), 100.0 * Top.TopRow, 100.0 * Top.BottomRow,
					View.DegreesBelow(Fish.Last()->Bounds.Origin)), Top.Share() >= MinSeenShare);
				if (Lid)
				{
					const FVector2D LidRows = RowsOf(View, Lid);
					TestTrue(FString::Printf(TEXT("%s: the open lid stays out of the upper half of the view (rows %.0f-%.0f %%)"), *Case, 100.0 * LidRows.X, 100.0 * LidRows.Y),
						LidRows.X >= LidHighestRow);
				}
			}
		}
		return true;
	}

	// ---- 2. Closed carry: its share of the screen height (designer's must-fix numbers) ----

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOwnerViewClosedShare, "Project.Catch.HeldCooler.OwnerView.ClosedCarryScreenShare", LCT::Flags)
	bool FOwnerViewClosedShare::RunTest(const FString& Parameters)
	{
		FRig Rig;
		if (!Rig.Create(*this, 1) || !Rig.PickUp(*this))
		{
			return false;
		}
		if (!Rig.HasArmsBone())
		{
			AddWarning(TEXT("SK_FPArms (or its cooler bone) is not loaded here: the owner's view can't be measured"));
			return true;
		}
		FOwnerView View;
		if (!TestTrue(TEXT("the owner's view"), View.Init(Rig.Player, Screen)))
		{
			return false;
		}
		const UStaticMeshComponent* Body = PartOf(Rig.Cooler, TEXT("BodyMesh"));
		const UStaticMeshComponent* Lid = PartOf(Rig.Cooler, TEXT("LidMesh"));
		if (!TestNotNull(TEXT("body"), Body) || !TestNotNull(TEXT("lid"), Lid))
		{
			return false;
		}
		auto TopRow = [&View, Body, Lid](double DropCm)
		{
			return FMath::Min(RowsOf(View, Body, DropCm).X, RowsOf(View, Lid, DropCm).X);
		};
		const double Share = 1.0 - FMath::Max(0.0, TopRow(0.0));
		TestTrue(FString::Printf(TEXT("the closed carry takes %.1f %% of the screen height (art composition: the lower quarter, at most %.0f %%)"), 100.0 * Share, 100.0 * MaxClosedCarryShare),
			Share <= MaxClosedCarryShare && Share > 0.05);
		// The designer's target: how much lower the whole carry (fists and cooler together) would have to sit.
		double DropNeeded = -1.0;
		for (double Drop = 0.0; Drop <= 25.0; Drop += 0.25)
		{
			if (1.0 - FMath::Max(0.0, TopRow(Drop)) <= DesignerClosedCarryShare)
			{
				DropNeeded = Drop;
				break;
			}
		}
		const FVector HandleL = Body->GetSocketLocation(TEXT("Handle_L"));
		FVector2D HandleUV;
		View.Project(HandleL, HandleUV);
		const FTransform Visual = Rig.Cooler->GetVisualTransform();
		double LidTop = TNumericLimits<double>::Lowest();
		for (const FVector& Vertex : DrawnVertices(Lid))
		{
			LidTop = FMath::Max(LidTop, Visual.InverseTransformPosition(Vertex).Z);
		}
		AddInfo(FString::Printf(TEXT("designer target %.0f %%: the carry (fists with it) would sit %.2f cm lower; the rope handles are %.1f deg below the view centre (row %.0f %%) and the lid top %.1f cm above them, so an offset on the cooler bone alone moves the handles off the fists by that much"),
			100.0 * DesignerClosedCarryShare, DropNeeded, View.DegreesBelow(HandleL), 100.0 * HandleUV.Y, LidTop - Visual.InverseTransformPosition(HandleL).Z));
		return true;
	}

	// ---- 3. First-person primitives stay lit: the carried cooler and the owner's own body leave the GI scenes ----

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOwnerViewLighting, "Project.Catch.HeldCooler.OwnerView.FirstPersonLighting", LCT::Flags)
	bool FOwnerViewLighting::RunTest(const FString& Parameters)
	{
		FRig Rig;
		if (!Rig.Create(*this, 1))
		{
			return false;
		}
		// The owner's own placeholder body: out of ray tracing, distance fields and Lumen on his machine.
		const UStaticMeshComponent* Body = Rig.Player->GetPlaceholderBody();
		if (!TestNotNull(TEXT("placeholder body"), Body))
		{
			return false;
		}
		TestTrue(TEXT("the local player is locally controlled"), Rig.Player->IsLocallyControlled());
		TestFalse(TEXT("own body: not in ray tracing"), static_cast<bool>(Body->bVisibleInRayTracing));
		TestFalse(TEXT("own body: no distance-field lighting"), static_cast<bool>(Body->bAffectDistanceFieldLighting));
		TestFalse(TEXT("own body: no dynamic indirect lighting (Lumen)"), static_cast<bool>(Body->bAffectDynamicIndirectLighting));
		TestTrue(TEXT("own body: still hidden from its owner only"), Body->bOwnerNoSee && !Body->bHiddenInGame);

		// Standing, the cooler is a world primitive with its defaults.
		auto Parts = [&Rig]()
		{
			TArray<const UPrimitiveComponent*> Out;
			TInlineComponentArray<UPrimitiveComponent*> All(Rig.Cooler);
			for (const UPrimitiveComponent* Part : All)
			{
				if (Part->IsA<UStaticMeshComponent>() || Part->IsA<USkeletalMeshComponent>())
				{
					Out.Add(Part);
				}
			}
			return Out;
		};
		for (const UPrimitiveComponent* Part : Parts())
		{
			TestTrue(FString::Printf(TEXT("standing: %s keeps distance-field and indirect lighting"), *Part->GetName()),
				Part->bAffectDistanceFieldLighting && Part->bAffectDynamicIndirectLighting && Part->FirstPersonPrimitiveType == EFirstPersonPrimitiveType::None);
		}
		if (!Rig.PickUp(*this))
		{
			return false;
		}
		if (Rig.Cooler->GetRootComponent()->GetAttachParent() == Rig.Player->GetRootComponent())
		{
			AddWarning(TEXT("no first-person attachment here: the carried flags are not checked"));
			return true;
		}
		// Open, and a second fish goes in while carried: the display fish are re-made while first person (RefreshDisplay).
		Rig.Open(*this);
		Rig.Cooler->GetStorage()->AddFish(FLureCaughtFish::Landed(LCT::MakeFish(TEXT("CoralSnapper"), 12, 1, 1.4f, 6500), Rig.W.Now()));
		Rig.Advance(0.2f);
		TestEqual(TEXT("carried open: both fish shown"), ShownFish(Rig.Cooler).Num(), 2);
		for (const UPrimitiveComponent* Part : Parts())
		{
			TestTrue(FString::Printf(TEXT("carried: %s drawn first person, out of the distance-field and Lumen scenes"), *Part->GetName()),
				Part->FirstPersonPrimitiveType == EFirstPersonPrimitiveType::FirstPerson && !Part->bAffectDistanceFieldLighting && !Part->bAffectDynamicIndirectLighting);
		}
		// Put down (F closes it first: F on an open cooler closes it, F again puts it down).
		Rig.Press(*this, ELureInteractKey::Secondary, ELureInteractVerb::CloseCooler, TEXT("F closes it"));
		Rig.Advance(0.5f);
		Rig.Press(*this, ELureInteractKey::Secondary, ELureInteractVerb::PutDownCooler, TEXT("F puts it down"));
		Rig.Advance(1.0f);
		TestTrue(TEXT("put down"), Rig.Cooler->IsFree());
		for (const UPrimitiveComponent* Part : Parts())
		{
			TestTrue(FString::Printf(TEXT("put down: %s back to its defaults"), *Part->GetName()),
				Part->bAffectDistanceFieldLighting && Part->bAffectDynamicIndirectLighting && Part->FirstPersonPrimitiveType == EFirstPersonPrimitiveType::None);
		}

		// Unpossessed (nobody's own view), the body is back in the GI scenes: what other machines have.
		if (APlayerController* Controller = LCT::ControllerOf(Rig.Player))
		{
			Controller->UnPossess();
			TestFalse(TEXT("unpossessed: not locally controlled"), Rig.Player->IsLocallyControlled());
			TestTrue(TEXT("unpossessed: the body is back in ray tracing, distance fields and Lumen"),
				Body->bVisibleInRayTracing && Body->bAffectDistanceFieldLighting && Body->bAffectDynamicIndirectLighting);
			Controller->Possess(Rig.Player);
			TestTrue(TEXT("possessed again: out again"), !Body->bVisibleInRayTracing && !Body->bAffectDistanceFieldLighting && !Body->bAffectDynamicIndirectLighting);
		}
		return true;
	}
}

#endif
