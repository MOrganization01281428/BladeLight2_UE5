﻿// VRM4U Copyright (c) 2021 Haruyoshi Yamamoto. This software is released under the MIT License.

#include "VrmConvertRig.h"
#include "VrmConvert.h"
#include "VrmUtil.h"

#include "VrmAssetListObject.h"
#include "VrmMetaObject.h"
#include "VrmBPFunctionLibrary.h"

#include "Engine/SkeletalMesh.h"

#include "Animation/MorphTarget.h"
#include "Animation/NodeMappingContainer.h"
#include "Animation/Rig.h"
#include "Animation/PoseAsset.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"

#if WITH_EDITOR
#include "IPersonaToolkit.h"
#include "PersonaModule.h"
#include "Modules/ModuleManager.h"
#include "Animation/DebugSkelMeshComponent.h"
#endif

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/mesh.h>
#include <assimp/postprocess.h>
#include <assimp/pbrmaterial.h>
#include <assimp/vrm/vrmmeta.h>

//#include "Engine/.h"

namespace {
// utility function 
#if WITH_EDITOR
	FSmartName GetUniquePoseName(USkeleton* Skeleton, const FString &Name)
	{
		check(Skeleton);
		int32 NameIndex = 0;

		SmartName::UID_Type NewUID;
		FName NewName;

		do
		{
			NewName = FName(*FString::Printf(TEXT("%s_%d"), *Name,NameIndex++));

			if (NameIndex == 1) {
				NewName = *Name;
			}

			NewUID = Skeleton->GetUIDByName(USkeleton::AnimCurveMappingName, NewName);
		} while (NewUID != SmartName::MaxUID);

		// if found, 
		FSmartName NewPoseName;
		Skeleton->AddSmartNameAndModify(USkeleton::AnimCurveMappingName, NewName, NewPoseName);

		return NewPoseName;
	}
#endif 

#if WITH_EDITOR
	static void localFaceMorphConv(UVrmAssetListObject* vrmAssetList, const aiScene* mScenePtr) {
		const TArray<FString> arkitMorphName = {
			// Left eye blend shapes
			TEXT("EyeBlinkLeft"),
			TEXT("EyeLookDownLeft"),
			TEXT("EyeLookInLeft"),
			TEXT("EyeLookOutLeft"),
			TEXT("EyeLookUpLeft"),
			TEXT("EyeSquintLeft"),
			TEXT("EyeWideLeft"),
			// Right eye blend shapes
			TEXT("EyeBlinkRight"),
			TEXT("EyeLookDownRight"),
			TEXT("EyeLookInRight"),
			TEXT("EyeLookOutRight"),
			TEXT("EyeLookUpRight"),
			TEXT("EyeSquintRight"),
			TEXT("EyeWideRight"),
			// Jaw blend shapes
			TEXT("JawForward"),
			TEXT("JawLeft"),
			TEXT("JawRight"),
			TEXT("JawOpen"),
			// Mouth blend shapes
			TEXT("MouthClose"),
			TEXT("MouthFunnel"),
			TEXT("MouthPucker"),
			TEXT("MouthLeft"),
			TEXT("MouthRight"),
			TEXT("MouthSmileLeft"),
			TEXT("MouthSmileRight"),
			TEXT("MouthFrownLeft"),
			TEXT("MouthFrownRight"),
			TEXT("MouthDimpleLeft"),
			TEXT("MouthDimpleRight"),
			TEXT("MouthStretchLeft"),
			TEXT("MouthStretchRight"),
			TEXT("MouthRollLower"),
			TEXT("MouthRollUpper"),
			TEXT("MouthShrugLower"),
			TEXT("MouthShrugUpper"),
			TEXT("MouthPressLeft"),
			TEXT("MouthPressRight"),
			TEXT("MouthLowerDownLeft"),
			TEXT("MouthLowerDownRight"),
			TEXT("MouthUpperUpLeft"),
			TEXT("MouthUpperUpRight"),
			// Brow blend shapes
			TEXT("BrowDownLeft"),
			TEXT("BrowDownRight"),
			TEXT("BrowInnerUp"),
			TEXT("BrowOuterUpLeft"),
			TEXT("BrowOuterUpRight"),
			// Cheek blend shapes
			TEXT("CheekPuff"),
			TEXT("CheekSquintLeft"),
			TEXT("CheekSquintRight"),
			// Nose blend shapes
			TEXT("NoseSneerLeft"),
			TEXT("NoseSneerRight"),
			TEXT("TongueOut"),
			// Treat the head rotation as curves for LiveLink support
			TEXT("HeadYaw"),
			TEXT("HeadPitch"),
			TEXT("HeadRoll"),
			// Treat eye rotation as curves for LiveLink support
			TEXT("LeftEyeYaw"),
			TEXT("LeftEyePitch"),
			TEXT("LeftEyeRoll"),
			TEXT("RightEyeYaw"),
			TEXT("RightEyePitch"),
			TEXT("RightEyeRoll"),
		};

		FString name = FString(TEXT("POSE_face_")) + vrmAssetList->BaseFileName;


		auto &sk = vrmAssetList->SkeletalMesh;
		auto *k = VRMGetSkeleton(sk);

		UPoseAsset* pose = nullptr;
		{
			pose = VRM4U_NewObject<UPoseAsset>(vrmAssetList->Package, *name, RF_Public | RF_Standalone);
			pose->SetSkeleton(k);
			pose->SetPreviewMesh(sk);
			pose->Modify();
		}

		TArray<FString> MorphNameList;
		{
			for (uint32_t m = 0; m < mScenePtr->mNumMeshes; ++m) {
				const aiMesh& aiM = *(mScenePtr->mMeshes[m]);
				for (uint32_t a = 0; a < aiM.mNumAnimMeshes; ++a) {
					const aiAnimMesh& aiA = *(aiM.mAnimMeshes[a]);
					FString morphName = UTF8_TO_TCHAR(aiA.mName.C_Str());
					MorphNameList.AddUnique(morphName);
				}
			}
		}

		{
			FPersonaModule& PersonaModule = FModuleManager::LoadModuleChecked<FPersonaModule>("Persona");
			auto PersonaToolkit = PersonaModule.CreatePersonaToolkit(sk);
			UDebugSkelMeshComponent* PreviewComponent = PersonaToolkit->GetPreviewMeshComponent();

			auto* kk = Cast<USkeletalMeshComponent>(PreviewComponent);
			kk->SetComponentSpaceTransformsDoubleBuffering(false);
		}

		TArray < FSmartName > SmartNamePoseList;
		{
			FSmartName n;
			n = GetUniquePoseName(k, TEXT("DefaultRefPose"));
			SmartNamePoseList.Add(n);
		}

		UAnimSequence* ase = nullptr;
		{
			FString AnimName = FString(TEXT("A_face_")) + vrmAssetList->BaseFileName;
			ase = VRM4U_NewObject<UAnimSequence>(vrmAssetList->Package, *AnimName, RF_Public | RF_Standalone);
#if UE_VERSION_OLDER_THAN(5,0,0)
			ase->CleanAnimSequenceForImport();
#else
			IAnimationDataController& DataController = ase->GetController();
			IAnimationDataController::FScopedBracket ScopedBracket(&DataController, FText());
			DataController.ResetModel();


			//auto *DataModel = DuplicateObject(ase->GetDataModel(), ase);
			//DataController.SetModel(DataModel);

			//DataModel->GetNumberOfTransformCurves
#endif
			ase->SetSkeleton(k);

		// arkit blendshape
		for (auto& arkitMorph : arkitMorphName) {
			int no = MorphNameList.Find(arkitMorph);

			FString modelMorph;
			if (no >= 0) {
				modelMorph = arkitMorph;
			} else {
				auto ar = arkitMorph.ToLower();
				FString* find_s = MorphNameList.FindByPredicate(
					[&ar, &modelMorph](const FString& str) {
					//
					auto s = str.ToLower();
					if (s == ar) {
						modelMorph = str;
						return true;
					}
					FString tmp;
					tmp = ar.Replace(TEXT("Left"), TEXT("_L")).ToLower();
					if (s == tmp) {
						modelMorph = str;
						return true;
					}
					tmp = ar.Replace(TEXT("Right"), TEXT("_R")).ToLower();
					if (s == tmp) {
						modelMorph = str;
						return true;
					}
					return false;
				}
				);
				if (find_s) {
					no = MorphNameList.Find(*find_s);
				}
			}

			// no morph. add no weight curve
			//if (no < 0) {
			//	continue;
			//}

			FSmartName SmartPoseName;
			SmartPoseName = GetUniquePoseName(k, *arkitMorph);

			FSmartName curveName;
			int targetNo = 0;
			bool bSameName = false;
			if (arkitMorph == modelMorph) {
				// same name. no curve weight
				curveName = SmartPoseName;
				bSameName = true;
			} else if (modelMorph == ""){
				// no morph. norcurve weight
				curveName = SmartPoseName;
				bSameName = true;
			}else{
				bool bFind = false;
				for (auto& c : ase->RawCurveData.FloatCurves) {
					if (c.Name.DisplayName.ToString().ToLower() == modelMorph.ToLower()) {
						// found curve in list
						bFind = true;
						curveName = c.Name;
						break;
					}
					++targetNo;
				}
				if (bFind == false) {
					// create new curve
					curveName = GetUniquePoseName(k, *modelMorph);
					targetNo = ase->RawCurveData.FloatCurves.Num();
				}
			}
			ase->RawCurveData.AddCurveData(curveName);

			// Anim to Pose
			if (bSameName == false){
				auto& c = ase->RawCurveData.FloatCurves;
				auto& a = c[targetNo];

				a.SetCurveTypeFlag(AACF_Editable, true);

				// for default +1
				int targetFrame = ase->RawCurveData.FloatCurves.Num() + 1;

				// 0 for prev and forward frame
				if (a.Evaluate(targetFrame - 2) == 0) {
					a.UpdateOrAddKey(0, targetFrame - 2);
				}
				if (a.Evaluate(targetFrame + 0) == 0) {
					a.UpdateOrAddKey(0, targetFrame + 0);
				}
				a.UpdateOrAddKey(1, targetFrame - 1);

#if UE_VERSION_OLDER_THAN(5,0,0)
#else
				FAnimationCurveIdentifier CurveId(curveName, ERawCurveTrackTypes::RCT_Float);
				DataController.AddCurve(CurveId);
				DataController.SetCurveKeys(CurveId, a.FloatCurve.GetConstRefOfKeys());
#endif
			}

			auto newName = SmartPoseName;
			SmartNamePoseList.Add(newName);
		}

		// vrm blendshape
		{
			// for add and offset
			int targetFrame = ase->RawCurveData.FloatCurves.Num() + 1 + 1;

			for (auto& group : vrmAssetList->VrmMetaObject->BlendShapeGroup) {

				if (group.name == "") continue;
				if (group.BlendShape.Num() == 0) continue;

				FSmartName SmartPoseName;
				SmartPoseName = GetUniquePoseName(k, *group.name);

				// !! vrm blend shape !!
				bool addCurve = false;
				for (auto& shape : group.BlendShape) {
					if (shape.morphTargetName == "") continue;

					FSmartName curveName;
					int targetNo = 0;

					bool bSameName = false;
					if (group.name.ToLower() == shape.morphTargetName.ToLower()) {
						// same name
						curveName = SmartPoseName;
						bSameName = true;
					} else {
						bool bFind = false;
						for (auto& c : ase->RawCurveData.FloatCurves) {
							if (c.Name.DisplayName.ToString().ToLower() == shape.morphTargetName.ToLower()) {
								// found curve name in list
								bFind = true;
								curveName = c.Name;
								break;
							}
							++targetNo;
						}
						if (bFind == false) {
							// create new curve
							curveName = GetUniquePoseName(k, *shape.morphTargetName);
							targetNo = ase->RawCurveData.FloatCurves.Num();
						}
					}
					ase->RawCurveData.AddCurveData(curveName);

					if (bSameName == false) {
						auto& c = ase->RawCurveData.FloatCurves;
						auto& a = c[targetNo];

						a.SetCurveTypeFlag(AACF_Editable, true);

						// 0 for prev and forward frame
						if (a.Evaluate(targetFrame - 2) == 0) {
							a.UpdateOrAddKey(0, targetFrame - 2);
						}
						if (a.Evaluate(targetFrame  + 0) == 0) {
							a.UpdateOrAddKey(0, targetFrame + 0);
						}
						a.UpdateOrAddKey((float)(shape.weight) / 100.f, targetFrame - 1);

#if UE_VERSION_OLDER_THAN(5,0,0)
#else
						FAnimationCurveIdentifier CurveId(curveName, ERawCurveTrackTypes::RCT_Float);
						DataController.AddCurve(CurveId);
						DataController.SetCurveKeys(CurveId, a.FloatCurve.GetConstRefOfKeys());
#endif
					}
					addCurve = true;
				}

				if (addCurve) {
					// new pose
					SmartNamePoseList.Add(SmartPoseName);
					targetFrame += 1;
				}
			}
		}

		// support arkit from vrm
		{
			TArray<FString> BlendFrom = {
				TEXT("EyeBlinkLeft"),
				TEXT("EyeBlinkRight"),
				TEXT("JawOpen"),
			};
			TArray<FString> BlendTo = {
				TEXT("Blink_L"),
				TEXT("Blink_R"),
				TEXT("A"),
			};
			for (auto& arkitMorph : arkitMorphName) {
				int tableNo = BlendFrom.Find(arkitMorph);
				if (tableNo < 0) {
					continue;
				}
				const FString vrmMorph = BlendTo[tableNo];

				// arkit MorphName check
				{
					FString* s = MorphNameList.FindByPredicate(
						[&arkitMorph](const FString& str) {
						return (arkitMorph.ToLower() == str.ToLower());
					});
					if (s) {
						// has same arkit morph name
						continue;
					}
				}

				int arMorphFrameNo = -1;
				{
					FSmartName* m = SmartNamePoseList.FindByPredicate(
						[&arkitMorph](FSmartName& name) {
						return (arkitMorph.ToLower() == name.DisplayName.ToString().ToLower());
					});
					if (m == nullptr) continue;
					arMorphFrameNo = SmartNamePoseList.Find(*m);
				}
				int vrmMorphFrameNo = -1;
				{
					FSmartName* m = SmartNamePoseList.FindByPredicate(
						[&vrmMorph](FSmartName& name) {
						return (vrmMorph.ToLower() == name.DisplayName.ToString().ToLower());
					});
					if (m == nullptr) continue;
					vrmMorphFrameNo = SmartNamePoseList.Find(*m);
				}

				auto& curves = ase->RawCurveData.FloatCurves;
				if (curves.Num() <= arMorphFrameNo || curves.Num() <= vrmMorphFrameNo) {
					// range check
					continue;
				}

				bool bHasCurve = false;
				for (auto& c : curves) {
					if (c.Evaluate(arMorphFrameNo) >= 0.5f) {
						bHasCurve = true;
						break;
					}
				}
				if (bHasCurve == true) {
					// has armorph value
					continue;
				}

				for (auto& c : curves) {
					if (c.Evaluate(arMorphFrameNo) == c.Evaluate(vrmMorphFrameNo)) {
						continue;
					}

					// 0 for prev and forward frame
					if (c.Evaluate(arMorphFrameNo - 1) == 0) {
						c.UpdateOrAddKey(0, arMorphFrameNo - 1);
					}
					if (c.Evaluate(arMorphFrameNo + 1) == 0) {
						c.UpdateOrAddKey(0, arMorphFrameNo + 1);
					}
					c.UpdateOrAddKey(c.Evaluate(vrmMorphFrameNo), arMorphFrameNo);
				}
			}
#if UE_VERSION_OLDER_THAN(5,0,0)
#else
				//FAnimationCurveIdentifier CurveId(curveName, ERawCurveTrackTypes::RCT_Float);
				//DataController.AddCurve(CurveId);
				//DataController.SetCurveKeys(CurveId, a.FloatCurve.GetConstRefOfKeys());
#endif

		}


#if	UE_VERSION_OLDER_THAN(4,22,0)
		ase->NumFrames = SmartNamePoseList.Num();
#elif UE_VERSION_OLDER_THAN(5,0,0)
		ase->SetRawNumberOfFrame(SmartNamePoseList.Num());
#endif

		ase->RateScale = 24.f;

#if UE_VERSION_OLDER_THAN(5,0,0)
			ase->SequenceLength = float(SmartNamePoseList.Num() - 1);
#else
			{
				DataController.SetPlayLength(float(SmartNamePoseList.Num() - 1));

				FFrameRate f(1, 1);
				DataController.SetFrameRate(f);

				//FAnimationCurveIdentifier CurveId(SmartNamePoseList[0], ERawCurveTrackTypes::RCT_Float);
				//DataController.AddCurve(CurveId);
				//const FFloatCurve* NewCurve = Sequence->GetDataModel()->FindFloatCurve(CurveId);


				DataController.UpdateCurveNamesFromSkeleton(k, ERawCurveTrackTypes::RCT_Float);
				DataController.NotifyPopulated();
			}
#endif
		}

		if (SmartNamePoseList.Num() > 0) {
			pose->CreatePoseFromAnimation(ase, &SmartNamePoseList);
			// for additive
			pose->ConvertSpace(true, 0);

		}
#if	UE_VERSION_OLDER_THAN(5,1,0)
		ase->PreSave(nullptr);
#else
#endif
		ase->PostEditChange();
	}
#endif
}

bool VRMConverter::ConvertPose(UVrmAssetListObject *vrmAssetList, const aiScene *mScenePtr) {

	if (VRMConverter::Options::Get().IsDebugOneBone()) {
		return true;
	}

	if (vrmAssetList->SkeletalMesh == nullptr) {
		return false;
	}

	bool bPlay = false;
	{
		bool b1, b2, b3;
		b1 = b2 = b3 = false;
		UVrmBPFunctionLibrary::VRMGetPlayMode(b1, b2, b3);
		bPlay = b1;
	}

#if	UE_VERSION_OLDER_THAN(4,20,0)
#else
#if WITH_EDITOR

	// pose asset
	if (VRMConverter::Options::Get().IsDebugOneBone() == false && bPlay==false){
		USkeletalMesh *sk = vrmAssetList->SkeletalMesh;
		USkeleton* k = VRMGetSkeleton(sk);

		FString name = FString(TEXT("POSE_")) + vrmAssetList->BaseFileName;
		
		UPoseAsset *pose = nullptr;

		//if (VRMConverter::Options::Get().IsSingleUAssetFile()) {
			pose = VRM4U_NewObject<UPoseAsset>(vrmAssetList->Package, *name, RF_Public | RF_Standalone);
		//} else {
		//	FString originalPath = vrmAssetList->Package->GetPathName();
		//	const FString PackagePath = FPaths::GetPath(originalPath);

		//	FString NewPackageName = FPaths::Combine(*PackagePath, *name);
		//	UPackage* Pkg = CreatePackage(nullptr, *NewPackageName);

		//	pose = VRM4U_NewObject<UPoseAsset>(Pkg, *name, RF_Public | RF_Standalone);
		//}



		pose->SetSkeleton(k);
		pose->SetPreviewMesh(sk);
		pose->Modify();

		{
			/*
			type 0:
				poseasset +1: T-pose,
			type 1:
				poseasset +1: A-pose,
				retarget +1 : T-pose or A-pose
			type 2:
				poseasset +1: T-pose(footA)
			type 3:
				poseasset +1: A-pose(footT)
			*/
			enum class PoseType {
				TYPE_T,
				TYPE_A,
			};
			PoseType poseType_hand;
			PoseType poseType_foot;
			for (int poseCount = 0; poseCount < 4; ++poseCount) {

				switch (poseCount) {
				case 0:
					poseType_hand = PoseType::TYPE_T;
					poseType_foot = PoseType::TYPE_T;
					break;
				case 1:
					poseType_hand = PoseType::TYPE_A;
					poseType_foot = PoseType::TYPE_A;
					break;
				case 2:
					poseType_hand = PoseType::TYPE_T;
					poseType_foot = PoseType::TYPE_A;
					break;
				case 3:
				default:
					poseType_hand = PoseType::TYPE_A;
					poseType_foot = PoseType::TYPE_T;
					break;
				}

				FPersonaModule& PersonaModule = FModuleManager::LoadModuleChecked<FPersonaModule>("Persona");
				auto PersonaToolkit = PersonaModule.CreatePersonaToolkit(sk);

				UDebugSkelMeshComponent* PreviewComponent = PersonaToolkit->GetPreviewMeshComponent();

				auto *kk = Cast<USkeletalMeshComponent>(PreviewComponent);
				kk->SetComponentSpaceTransformsDoubleBuffering(false);

				{
					struct RetargetParts {
						FString BoneUE4;
						FString BoneVRM;
						FString BoneModel;

						FRotator rot;
					};

					TArray<RetargetParts> retargetTable;
					if (VRMConverter::Options::Get().IsVRMModel() || VRMConverter::Options::Get().IsBVHModel()) {
						if (poseType_hand== PoseType::TYPE_A) {
							{
								RetargetParts t;
								t.BoneUE4 = TEXT("UpperArm_R");
								t.rot = FRotator(40, 0, 0);
								retargetTable.Push(t);
							}
							{
								RetargetParts t;
								t.BoneUE4 = TEXT("lowerarm_r");
								t.rot = FRotator(0, -30, 0);
								retargetTable.Push(t);
							}
							{
								RetargetParts t;
								t.BoneUE4 = TEXT("Hand_R");
								t.rot = FRotator(10, 0, 0);
								retargetTable.Push(t);
							}
							{
								RetargetParts t;
								t.BoneUE4 = TEXT("UpperArm_L");
								t.rot = FRotator(-40, 0, 0);
								retargetTable.Push(t);
							}
							{
								RetargetParts t;
								t.BoneUE4 = TEXT("lowerarm_l");
								t.rot = FRotator(-0, 30, 0);
								retargetTable.Push(t);
							}
							{
								RetargetParts t;
								t.BoneUE4 = TEXT("Hand_L");
								t.rot = FRotator(-10, 0, 0);
								retargetTable.Push(t);
							}
						}
					}
					if (VRMConverter::Options::Get().IsPMXModel()) {
						if (poseType_hand == PoseType::TYPE_T) {
							auto& poseList = k->GetReferenceSkeleton().GetRefBonePose();
							FString* boneName = vrmAssetList->VrmMetaObject->humanoidBoneTable.Find(TEXT("rightLowerArm"));
							float degRot = 0.f;
							if (boneName) {
								int ind = k->GetReferenceSkeleton().FindBoneIndex(**boneName);
								if (ind >= 0) {
									FVector v = poseList[ind].GetLocation();
									v.Z = FMath::Abs(v.Z);
									v.X = FMath::Abs(v.X);
									degRot = FMath::Abs(FMath::Atan2(v.Z, v.X)) * 180.f / PI;
								}
							}
							if (degRot) {
								{
									RetargetParts t;
									t.BoneUE4 = TEXT("UpperArm_R");
									t.rot = FRotator(-degRot, 0, 0);
									retargetTable.Push(t);
								}
								{
									RetargetParts t;
									t.BoneUE4 = TEXT("UpperArm_L");
									t.rot = FRotator(degRot, 0, 0);
									retargetTable.Push(t);
								}
							}
						}
						if (poseType_hand == PoseType::TYPE_A) {
							{
								RetargetParts t;
								t.BoneUE4 = TEXT("lowerarm_r");
								t.rot = FRotator(0, -30, 0);
								retargetTable.Push(t);
							}
							{
								RetargetParts t;
								t.BoneUE4 = TEXT("Hand_R");
								t.rot = FRotator(10, 0, 0);
								retargetTable.Push(t);
							}
							{
								RetargetParts t;
								t.BoneUE4 = TEXT("lowerarm_l");
								t.rot = FRotator(-0, 30, 0);
								retargetTable.Push(t);
							}
							{
								RetargetParts t;
								t.BoneUE4 = TEXT("Hand_L");
								t.rot = FRotator(-10, 0, 0);
								retargetTable.Push(t);
							}
						}
					}
					if (poseType_foot == PoseType::TYPE_A) {
						{
							RetargetParts t;
							t.BoneUE4 = TEXT("Thigh_R");
							t.rot = FRotator(-7, 0, 0);
							retargetTable.Push(t);
						}
						{
							RetargetParts t;
							t.BoneUE4 = TEXT("Thigh_L");
							t.rot = FRotator(7, 0, 0);
							retargetTable.Push(t);
						}
					}

					TMap<FString, RetargetParts> mapTable;
					for (auto &a : retargetTable) {
						bool bFound = false;
						//vrm
						for (auto &t : VRMUtil::table_ue4_vrm) {
							if (t.BoneUE4.Compare(a.BoneUE4) != 0) {
								continue;
							}
							auto *m = vrmAssetList->VrmMetaObject->humanoidBoneTable.Find(t.BoneVRM);
							if (m) {
								bFound = true;
								a.BoneVRM = t.BoneVRM;
								a.BoneModel = *m;
								mapTable.Add(a.BoneModel, a);
							}
							break;
						}
						if (bFound) {
							continue;
						}
						//pmx
						for (auto &t : VRMUtil::table_ue4_pmx) {
							if (t.BoneUE4.Compare(a.BoneUE4) != 0) {
								continue;
							}
							FString pmxBone;
							VRMUtil::GetReplacedPMXBone(pmxBone, t.BoneVRM);

							FString target[2] = {
								pmxBone,
								t.BoneVRM,
							};
							bool finish = false;
							for (int i = 0; i < 2; ++i) {
								auto* m = vrmAssetList->VrmMetaObject->humanoidBoneTable.Find(target[i]);
								if (m) {
									bFound = true;
									a.BoneVRM = target[i];
									a.BoneModel = *m;
									mapTable.Add(a.BoneModel, a);
								}
								finish = true;
								break;
							}

							if (finish) break;
						}
						//bvh
						{
							auto *mc = vrmAssetList->HumanoidRig;
							const auto &m = mc->GetNodeMappingTable();
							auto *value = m.Find(*a.BoneUE4);
							if (value) {
								bFound = true;
								mapTable.Add(value->ToString(), a);
							}
						}
						if (bFound) {
							continue;
						}
					}

					auto &rk = k->GetReferenceSkeleton();
					auto &dstTrans = kk->GetEditableComponentSpaceTransforms();

					// init retarget pose
					for (int i = 0; i < dstTrans.Num(); ++i) {
						auto &t = dstTrans[i];
						t = rk.GetRefBonePose()[i];
					}
					if (poseCount == 1) {
						VRMSetRetargetBasePose(sk, dstTrans);
					}

					// override
					for (int i = 0; i < dstTrans.Num(); ++i) {
						auto &t = dstTrans[i];

						auto *m = mapTable.Find(rk.GetBoneName(i).ToString());
						if (m) {
							t.SetRotation(FQuat(m->rot));
						}
					}

					// current pose retarget. local
					if (VRMConverter::Options::Get().IsAPoseRetarget() == true) {
						if (poseCount == 1) {
							VRMSetRetargetBasePose(sk, dstTrans);
						}
					}

					// for rig asset. world
					for (int i = 0; i < dstTrans.Num(); ++i) {
						int parent = rk.GetParentIndex(i);
						if (parent == INDEX_NONE) continue;

						dstTrans[i] = dstTrans[i] * dstTrans[parent];
					}
					// ik bone hand
					{
						int32 ik_g = VRMGetRefSkeleton(sk).FindBoneIndex(TEXT("ik_hand_gun"));
						int32 ik_r = VRMGetRefSkeleton(sk).FindBoneIndex(TEXT("ik_hand_r"));
						int32 ik_l = VRMGetRefSkeleton(sk).FindBoneIndex(TEXT("ik_hand_l"));

						if (ik_g >= 0 && ik_r >= 0 && ik_l >= 0) {
							const VRM::VRMMetadata *meta = reinterpret_cast<VRM::VRMMetadata*>(mScenePtr->mVRMMeta);

							auto ar = vrmAssetList->VrmMetaObject->humanoidBoneTable.Find(TEXT("rightHand"));
							auto al = vrmAssetList->VrmMetaObject->humanoidBoneTable.Find(TEXT("leftHand"));
							if (ar && al) {
								int32 kr = VRMGetRefSkeleton(sk).FindBoneIndex(**ar);
								int32 kl = VRMGetRefSkeleton(sk).FindBoneIndex(**al);

								dstTrans[ik_g] = dstTrans[kr];
								dstTrans[ik_r] = dstTrans[kr];
								dstTrans[ik_l] = dstTrans[kl];

								// local
								VRMGetRetargetBasePose(sk)[ik_g] = dstTrans[kr];
								VRMGetRetargetBasePose(sk)[ik_r].SetIdentity();
								VRMGetRetargetBasePose(sk)[ik_l] = dstTrans[kl] * dstTrans[kr].Inverse();
							}
						}
					}
					// ik bone foot
					{
						int32 ik_r = VRMGetRefSkeleton(sk).FindBoneIndex(TEXT("ik_foot_r"));
						int32 ik_l = VRMGetRefSkeleton(sk).FindBoneIndex(TEXT("ik_foot_l"));

						if (ik_r >= 0 && ik_l >= 0) {
							const VRM::VRMMetadata *meta = reinterpret_cast<VRM::VRMMetadata*>(mScenePtr->mVRMMeta);

							auto ar = vrmAssetList->VrmMetaObject->humanoidBoneTable.Find(TEXT("rightFoot"));
							auto al = vrmAssetList->VrmMetaObject->humanoidBoneTable.Find(TEXT("leftFoot"));
							if (ar && al) {
								int32 kr = VRMGetRefSkeleton(sk).FindBoneIndex(**ar);
								int32 kl = VRMGetRefSkeleton(sk).FindBoneIndex(**al);

								dstTrans[ik_r] = dstTrans[kr];
								dstTrans[ik_l] = dstTrans[kl];

								// local
								VRMGetRetargetBasePose(sk)[ik_r] = dstTrans[kr];
								VRMGetRetargetBasePose(sk)[ik_l] = dstTrans[kl];
							}
						}
					}

				}
				{
					FSmartName PoseName;
					switch(poseCount) {
					case 0:
						PoseName = GetUniquePoseName(VRMGetSkeleton(kk->SkeletalMesh), TEXT("POSE_T"));
						break;
					case 1:
						PoseName = GetUniquePoseName(VRMGetSkeleton(kk->SkeletalMesh), TEXT("POSE_A"));
						break;
					case 2:
						PoseName = GetUniquePoseName(VRMGetSkeleton(kk->SkeletalMesh), TEXT("POSE_T(foot_A)"));
						break;
					case 3:
					default:
						PoseName = GetUniquePoseName(VRMGetSkeleton(kk->SkeletalMesh), TEXT("POSE_A(foot_T)"));
						break;
					}
					//pose->AddOrUpdatePose(PoseName, Cast<USkeletalMeshComponent>(PreviewComponent));

					FSmartName newName;
					pose->AddOrUpdatePoseWithUniqueName(Cast<USkeletalMeshComponent>(PreviewComponent), &newName);
					pose->ModifyPoseName(newName.DisplayName, PoseName.DisplayName, nullptr);
				}
			}
		}
	}

//#if	UE_VERSION_OLDER_THAN(5,0,0)
	//todo crash ue5...
	localFaceMorphConv(vrmAssetList, mScenePtr);
//#endif

#endif // editor
#endif //420

	return true;

}



