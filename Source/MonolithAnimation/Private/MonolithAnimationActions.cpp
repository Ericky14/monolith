#include "MonolithAnimationActions.h"
#include "MonolithAssetUtils.h"
#include "MonolithParamSchema.h"

#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "AnimGraphNode_Base.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_LinkedAnimLayer.h"
#include "AnimationStateMachineGraph.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "AnimStateConduitNode.h"
#include "AnimStateEntryNode.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Editor.h"

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void FMonolithAnimationActions::RegisterActions(FMonolithToolRegistry& Registry)
{
	// Montage Sections
	Registry.RegisterAction(TEXT("animation"), TEXT("add_montage_section"),
		TEXT("Add a section to an animation montage"),
		FMonolithActionHandler::CreateStatic(&HandleAddMontageSection),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Montage asset path"))
			.Required(TEXT("section_name"), TEXT("string"), TEXT("Name for the new section"))
			.Required(TEXT("start_time"), TEXT("number"), TEXT("Start time in seconds"))
			.Build());
	Registry.RegisterAction(TEXT("animation"), TEXT("delete_montage_section"),
		TEXT("Delete a section from an animation montage by index"),
		FMonolithActionHandler::CreateStatic(&HandleDeleteMontageSection),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Montage asset path"))
			.Required(TEXT("section_index"), TEXT("integer"), TEXT("Index of the section to delete"))
			.Build());
	Registry.RegisterAction(TEXT("animation"), TEXT("set_section_next"),
		TEXT("Set the next section for a montage section"),
		FMonolithActionHandler::CreateStatic(&HandleSetSectionNext),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Montage asset path"))
			.Required(TEXT("section_name"), TEXT("string"), TEXT("Name of the section"))
			.Required(TEXT("next_section_name"), TEXT("string"), TEXT("Name of the next section to play"))
			.Build());
	Registry.RegisterAction(TEXT("animation"), TEXT("set_section_time"),
		TEXT("Set the start time of a montage section"),
		FMonolithActionHandler::CreateStatic(&HandleSetSectionTime),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Montage asset path"))
			.Required(TEXT("section_name"), TEXT("string"), TEXT("Name of the section"))
			.Required(TEXT("new_time"), TEXT("number"), TEXT("New start time in seconds"))
			.Build());

	// BlendSpace Samples
	Registry.RegisterAction(TEXT("animation"), TEXT("add_blendspace_sample"),
		TEXT("Add a sample to a blend space"),
		FMonolithActionHandler::CreateStatic(&HandleAddBlendSpaceSample),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("BlendSpace asset path"))
			.Required(TEXT("anim_path"), TEXT("string"), TEXT("Animation sequence asset path"))
			.Required(TEXT("x"), TEXT("number"), TEXT("X axis value"))
			.Required(TEXT("y"), TEXT("number"), TEXT("Y axis value"))
			.Build());
	Registry.RegisterAction(TEXT("animation"), TEXT("edit_blendspace_sample"),
		TEXT("Edit a blend space sample position and optionally its animation"),
		FMonolithActionHandler::CreateStatic(&HandleEditBlendSpaceSample),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("BlendSpace asset path"))
			.Required(TEXT("sample_index"), TEXT("integer"), TEXT("Index of the sample to edit"))
			.Required(TEXT("x"), TEXT("number"), TEXT("New X axis value"))
			.Required(TEXT("y"), TEXT("number"), TEXT("New Y axis value"))
			.Optional(TEXT("anim_path"), TEXT("string"), TEXT("New animation sequence asset path"))
			.Build());
	Registry.RegisterAction(TEXT("animation"), TEXT("delete_blendspace_sample"),
		TEXT("Delete a sample from a blend space by index"),
		FMonolithActionHandler::CreateStatic(&HandleDeleteBlendSpaceSample),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("BlendSpace asset path"))
			.Required(TEXT("sample_index"), TEXT("integer"), TEXT("Index of the sample to delete"))
			.Build());

	// ABP Graph Reading
	Registry.RegisterAction(TEXT("animation"), TEXT("get_state_machines"),
		TEXT("Get all state machines in an animation blueprint"),
		FMonolithActionHandler::CreateStatic(&HandleGetStateMachines),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Animation Blueprint asset path"))
			.Build());
	Registry.RegisterAction(TEXT("animation"), TEXT("get_state_info"),
		TEXT("Get detailed info about a state in a state machine"),
		FMonolithActionHandler::CreateStatic(&HandleGetStateInfo),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Animation Blueprint asset path"))
			.Required(TEXT("machine_name"), TEXT("string"), TEXT("State machine name"))
			.Required(TEXT("state_name"), TEXT("string"), TEXT("State name"))
			.Build());
	Registry.RegisterAction(TEXT("animation"), TEXT("get_transitions"),
		TEXT("Get all transitions in a state machine"),
		FMonolithActionHandler::CreateStatic(&HandleGetTransitions),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Animation Blueprint asset path"))
			.Optional(TEXT("machine_name"), TEXT("string"), TEXT("Filter to a specific state machine"))
			.Build());
	Registry.RegisterAction(TEXT("animation"), TEXT("get_blend_nodes"),
		TEXT("Get blend nodes in an animation blueprint graph"),
		FMonolithActionHandler::CreateStatic(&HandleGetBlendNodes),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Animation Blueprint asset path"))
			.Optional(TEXT("graph_name"), TEXT("string"), TEXT("Filter to a specific graph"))
			.Build());
	Registry.RegisterAction(TEXT("animation"), TEXT("get_linked_layers"),
		TEXT("Get linked animation layers in an animation blueprint"),
		FMonolithActionHandler::CreateStatic(&HandleGetLinkedLayers),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Animation Blueprint asset path"))
			.Build());
	Registry.RegisterAction(TEXT("animation"), TEXT("get_graphs"),
		TEXT("Get all graphs in an animation blueprint"),
		FMonolithActionHandler::CreateStatic(&HandleGetGraphs),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Animation Blueprint asset path"))
			.Build());
	Registry.RegisterAction(TEXT("animation"), TEXT("get_nodes"),
		TEXT("Get animation nodes with optional class filter"),
		FMonolithActionHandler::CreateStatic(&HandleGetNodes),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Animation Blueprint asset path"))
			.Optional(TEXT("node_class_filter"), TEXT("string"), TEXT("Only include nodes whose class contains this substring"))
			.Optional(TEXT("graph_name"), TEXT("string"), TEXT("Filter to a specific graph"))
			.Build());

	// Notify Editing
	Registry.RegisterAction(TEXT("animation"), TEXT("set_notify_time"),
		TEXT("Set the trigger time of an animation notify"),
		FMonolithActionHandler::CreateStatic(&HandleSetNotifyTime),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Animation asset path"))
			.Required(TEXT("notify_index"), TEXT("integer"), TEXT("Index of the notify"))
			.Required(TEXT("new_time"), TEXT("number"), TEXT("New trigger time in seconds"))
			.Build());
	Registry.RegisterAction(TEXT("animation"), TEXT("set_notify_duration"),
		TEXT("Set the duration of a state animation notify"),
		FMonolithActionHandler::CreateStatic(&HandleSetNotifyDuration),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Animation asset path"))
			.Required(TEXT("notify_index"), TEXT("integer"), TEXT("Index of the notify"))
			.Required(TEXT("new_duration"), TEXT("number"), TEXT("New duration in seconds"))
			.Build());

	// Bone Tracks
	Registry.RegisterAction(TEXT("animation"), TEXT("set_bone_track_keys"),
		TEXT("Set position, rotation, and scale keys on a bone track"),
		FMonolithActionHandler::CreateStatic(&HandleSetBoneTrackKeys),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Animation sequence asset path"))
			.Required(TEXT("bone_name"), TEXT("string"), TEXT("Bone name"))
			.Required(TEXT("positions_json"), TEXT("string"), TEXT("JSON array of position keys"))
			.Required(TEXT("rotations_json"), TEXT("string"), TEXT("JSON array of rotation keys"))
			.Required(TEXT("scales_json"), TEXT("string"), TEXT("JSON array of scale keys"))
			.Build());
	Registry.RegisterAction(TEXT("animation"), TEXT("add_bone_track"),
		TEXT("Add a bone track to an animation sequence"),
		FMonolithActionHandler::CreateStatic(&HandleAddBoneTrack),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Animation sequence asset path"))
			.Required(TEXT("bone_name"), TEXT("string"), TEXT("Bone name to add"))
			.Build());
	Registry.RegisterAction(TEXT("animation"), TEXT("remove_bone_track"),
		TEXT("Remove a bone track from an animation sequence"),
		FMonolithActionHandler::CreateStatic(&HandleRemoveBoneTrack),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Animation sequence asset path"))
			.Required(TEXT("bone_name"), TEXT("string"), TEXT("Bone name to remove"))
			.Optional(TEXT("include_children"), TEXT("bool"), TEXT("Also remove child bone tracks"), TEXT("false"))
			.Build());

	// Virtual Bones
	Registry.RegisterAction(TEXT("animation"), TEXT("add_virtual_bone"),
		TEXT("Add a virtual bone to a skeleton"),
		FMonolithActionHandler::CreateStatic(&HandleAddVirtualBone),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Skeleton asset path"))
			.Required(TEXT("source_bone"), TEXT("string"), TEXT("Source bone name"))
			.Required(TEXT("target_bone"), TEXT("string"), TEXT("Target bone name"))
			.Build());
	Registry.RegisterAction(TEXT("animation"), TEXT("remove_virtual_bones"),
		TEXT("Remove virtual bones from a skeleton"),
		FMonolithActionHandler::CreateStatic(&HandleRemoveVirtualBones),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Skeleton asset path"))
			.Required(TEXT("bone_names"), TEXT("array"), TEXT("Array of virtual bone names to remove"))
			.Build());

	// Skeleton Info
	Registry.RegisterAction(TEXT("animation"), TEXT("get_skeleton_info"),
		TEXT("Get skeleton bone hierarchy and virtual bones"),
		FMonolithActionHandler::CreateStatic(&HandleGetSkeletonInfo),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Skeleton asset path"))
			.Build());
	Registry.RegisterAction(TEXT("animation"), TEXT("get_skeletal_mesh_info"),
		TEXT("Get skeletal mesh info including morph targets, sockets, LODs, and materials"),
		FMonolithActionHandler::CreateStatic(&HandleGetSkeletalMeshInfo),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Skeletal mesh asset path"))
			.Build());
}

// ---------------------------------------------------------------------------
// Montage Sections
// ---------------------------------------------------------------------------

FMonolithActionResult FMonolithAnimationActions::HandleAddMontageSection(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	FString SectionName = Params->GetStringField(TEXT("section_name"));
	double StartTime = Params->GetNumberField(TEXT("start_time"));

	UAnimMontage* Montage = FMonolithAssetUtils::LoadAssetByPath<UAnimMontage>(AssetPath);
	if (!Montage) return FMonolithActionResult::Error(FString::Printf(TEXT("Montage not found: %s"), *AssetPath));

	GEditor->BeginTransaction(FText::FromString(TEXT("Add Montage Section")));
	Montage->Modify();

	int32 Index = Montage->AddAnimCompositeSection(FName(*SectionName), static_cast<float>(StartTime));

	GEditor->EndTransaction();

	if (Index == INDEX_NONE)
		return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to add section '%s' (name may already exist)"), *SectionName));

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("section_name"), SectionName);
	Root->SetNumberField(TEXT("index"), Index);
	Root->SetNumberField(TEXT("start_time"), StartTime);
	return FMonolithActionResult::Success(Root);
}

FMonolithActionResult FMonolithAnimationActions::HandleDeleteMontageSection(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	int32 SectionIndex = static_cast<int32>(Params->GetNumberField(TEXT("section_index")));

	UAnimMontage* Montage = FMonolithAssetUtils::LoadAssetByPath<UAnimMontage>(AssetPath);
	if (!Montage) return FMonolithActionResult::Error(FString::Printf(TEXT("Montage not found: %s"), *AssetPath));

	if (!Montage->IsValidSectionIndex(SectionIndex))
		return FMonolithActionResult::Error(FString::Printf(TEXT("Invalid section index: %d"), SectionIndex));

	FName SectionName = Montage->GetSectionName(SectionIndex);

	GEditor->BeginTransaction(FText::FromString(TEXT("Delete Montage Section")));
	Montage->Modify();
	bool bSuccess = Montage->DeleteAnimCompositeSection(SectionIndex);
	GEditor->EndTransaction();

	if (!bSuccess)
		return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to delete section at index %d"), SectionIndex));

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("deleted_section"), SectionName.ToString());
	Root->SetNumberField(TEXT("index"), SectionIndex);
	return FMonolithActionResult::Success(Root);
}

FMonolithActionResult FMonolithAnimationActions::HandleSetSectionNext(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	FString SectionName = Params->GetStringField(TEXT("section_name"));
	FString NextSectionName = Params->GetStringField(TEXT("next_section_name"));

	UAnimMontage* Montage = FMonolithAssetUtils::LoadAssetByPath<UAnimMontage>(AssetPath);
	if (!Montage) return FMonolithActionResult::Error(FString::Printf(TEXT("Montage not found: %s"), *AssetPath));

	int32 SectionIndex = Montage->GetSectionIndex(FName(*SectionName));
	if (SectionIndex == INDEX_NONE)
		return FMonolithActionResult::Error(FString::Printf(TEXT("Section not found: %s"), *SectionName));

	GEditor->BeginTransaction(FText::FromString(TEXT("Set Section Next")));
	Montage->Modify();
	FCompositeSection& Section = Montage->GetAnimCompositeSection(SectionIndex);
	Section.NextSectionName = FName(*NextSectionName);
	GEditor->EndTransaction();

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("section"), SectionName);
	Root->SetStringField(TEXT("next_section"), NextSectionName);
	return FMonolithActionResult::Success(Root);
}

FMonolithActionResult FMonolithAnimationActions::HandleSetSectionTime(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	FString SectionName = Params->GetStringField(TEXT("section_name"));
	float NewTime = static_cast<float>(Params->GetNumberField(TEXT("new_time")));

	UAnimMontage* Montage = FMonolithAssetUtils::LoadAssetByPath<UAnimMontage>(AssetPath);
	if (!Montage) return FMonolithActionResult::Error(FString::Printf(TEXT("Montage not found: %s"), *AssetPath));

	int32 SectionIndex = Montage->GetSectionIndex(FName(*SectionName));
	if (SectionIndex == INDEX_NONE)
		return FMonolithActionResult::Error(FString::Printf(TEXT("Section not found: %s"), *SectionName));

	GEditor->BeginTransaction(FText::FromString(TEXT("Set Section Time")));
	Montage->Modify();
	FCompositeSection& Section = Montage->GetAnimCompositeSection(SectionIndex);
	Section.SetTime(NewTime);
	Section.Link(Montage, NewTime, 0);
	GEditor->EndTransaction();

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("section"), SectionName);
	Root->SetNumberField(TEXT("new_time"), NewTime);
	return FMonolithActionResult::Success(Root);
}

// ---------------------------------------------------------------------------
// BlendSpace Samples
// ---------------------------------------------------------------------------

FMonolithActionResult FMonolithAnimationActions::HandleAddBlendSpaceSample(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	FString AnimPath = Params->GetStringField(TEXT("anim_path"));
	float X = static_cast<float>(Params->GetNumberField(TEXT("x")));
	float Y = static_cast<float>(Params->GetNumberField(TEXT("y")));

	UBlendSpace* BS = FMonolithAssetUtils::LoadAssetByPath<UBlendSpace>(AssetPath);
	if (!BS) return FMonolithActionResult::Error(FString::Printf(TEXT("BlendSpace not found: %s"), *AssetPath));

	UAnimSequence* Anim = FMonolithAssetUtils::LoadAssetByPath<UAnimSequence>(AnimPath);
	if (!Anim) return FMonolithActionResult::Error(FString::Printf(TEXT("AnimSequence not found: %s"), *AnimPath));

	GEditor->BeginTransaction(FText::FromString(TEXT("Add BlendSpace Sample")));
	BS->Modify();
	FVector SampleValue(X, Y, 0.0f);
	int32 Index = BS->AddSample(Anim, SampleValue);
	GEditor->EndTransaction();

	if (Index == INDEX_NONE)
		return FMonolithActionResult::Error(TEXT("Failed to add sample"));

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("index"), Index);
	Root->SetStringField(TEXT("animation"), AnimPath);
	Root->SetNumberField(TEXT("x"), X);
	Root->SetNumberField(TEXT("y"), Y);
	return FMonolithActionResult::Success(Root);
}

FMonolithActionResult FMonolithAnimationActions::HandleEditBlendSpaceSample(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	int32 SampleIndex = static_cast<int32>(Params->GetNumberField(TEXT("sample_index")));
	float X = static_cast<float>(Params->GetNumberField(TEXT("x")));
	float Y = static_cast<float>(Params->GetNumberField(TEXT("y")));
	FString AnimPath;
	Params->TryGetStringField(TEXT("anim_path"), AnimPath);

	UBlendSpace* BS = FMonolithAssetUtils::LoadAssetByPath<UBlendSpace>(AssetPath);
	if (!BS) return FMonolithActionResult::Error(FString::Printf(TEXT("BlendSpace not found: %s"), *AssetPath));

	if (!BS->IsValidBlendSampleIndex(SampleIndex))
		return FMonolithActionResult::Error(FString::Printf(TEXT("Invalid sample index: %d"), SampleIndex));

	GEditor->BeginTransaction(FText::FromString(TEXT("Edit BlendSpace Sample")));
	BS->Modify();

	FVector NewValue(X, Y, 0.0f);
	BS->EditSampleValue(SampleIndex, NewValue);

	if (!AnimPath.IsEmpty())
	{
		UAnimSequence* Anim = FMonolithAssetUtils::LoadAssetByPath<UAnimSequence>(AnimPath);
		if (Anim)
		{
			BS->DeleteSample(SampleIndex);
			BS->AddSample(Anim, NewValue);
		}
	}

	GEditor->EndTransaction();

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("index"), SampleIndex);
	Root->SetNumberField(TEXT("x"), X);
	Root->SetNumberField(TEXT("y"), Y);
	if (!AnimPath.IsEmpty()) Root->SetStringField(TEXT("animation"), AnimPath);
	return FMonolithActionResult::Success(Root);
}

FMonolithActionResult FMonolithAnimationActions::HandleDeleteBlendSpaceSample(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	int32 SampleIndex = static_cast<int32>(Params->GetNumberField(TEXT("sample_index")));

	UBlendSpace* BS = FMonolithAssetUtils::LoadAssetByPath<UBlendSpace>(AssetPath);
	if (!BS) return FMonolithActionResult::Error(FString::Printf(TEXT("BlendSpace not found: %s"), *AssetPath));

	if (!BS->IsValidBlendSampleIndex(SampleIndex))
		return FMonolithActionResult::Error(FString::Printf(TEXT("Invalid sample index: %d"), SampleIndex));

	GEditor->BeginTransaction(FText::FromString(TEXT("Delete BlendSpace Sample")));
	BS->Modify();
	bool bSuccess = BS->DeleteSample(SampleIndex);
	GEditor->EndTransaction();

	if (!bSuccess)
		return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to delete sample at index %d"), SampleIndex));

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("deleted_index"), SampleIndex);
	return FMonolithActionResult::Success(Root);
}

// ---------------------------------------------------------------------------
// Notify Editing
// ---------------------------------------------------------------------------

FMonolithActionResult FMonolithAnimationActions::HandleSetNotifyTime(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	int32 NotifyIndex = static_cast<int32>(Params->GetNumberField(TEXT("notify_index")));
	float NewTime = static_cast<float>(Params->GetNumberField(TEXT("new_time")));

	UAnimSequence* Seq = FMonolithAssetUtils::LoadAssetByPath<UAnimSequence>(AssetPath);
	if (!Seq) return FMonolithActionResult::Error(FString::Printf(TEXT("AnimSequence not found: %s"), *AssetPath));

	if (!Seq->Notifies.IsValidIndex(NotifyIndex))
		return FMonolithActionResult::Error(FString::Printf(TEXT("Invalid notify index: %d (total: %d)"), NotifyIndex, Seq->Notifies.Num()));

	GEditor->BeginTransaction(FText::FromString(TEXT("Set Notify Time")));
	Seq->Modify();
	Seq->Notifies[NotifyIndex].SetTime(NewTime);
	GEditor->EndTransaction();

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("index"), NotifyIndex);
	Root->SetNumberField(TEXT("new_time"), NewTime);
	Root->SetStringField(TEXT("notify_name"), Seq->Notifies[NotifyIndex].NotifyName.ToString());
	return FMonolithActionResult::Success(Root);
}

FMonolithActionResult FMonolithAnimationActions::HandleSetNotifyDuration(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	int32 NotifyIndex = static_cast<int32>(Params->GetNumberField(TEXT("notify_index")));
	float NewDuration = static_cast<float>(Params->GetNumberField(TEXT("new_duration")));

	UAnimSequence* Seq = FMonolithAssetUtils::LoadAssetByPath<UAnimSequence>(AssetPath);
	if (!Seq) return FMonolithActionResult::Error(FString::Printf(TEXT("AnimSequence not found: %s"), *AssetPath));

	if (!Seq->Notifies.IsValidIndex(NotifyIndex))
		return FMonolithActionResult::Error(FString::Printf(TEXT("Invalid notify index: %d"), NotifyIndex));

	if (!Seq->Notifies[NotifyIndex].NotifyStateClass)
		return FMonolithActionResult::Error(TEXT("Notify is not a state notify (no duration)"));

	GEditor->BeginTransaction(FText::FromString(TEXT("Set Notify Duration")));
	Seq->Modify();
	Seq->Notifies[NotifyIndex].SetDuration(NewDuration);
	GEditor->EndTransaction();

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("index"), NotifyIndex);
	Root->SetNumberField(TEXT("new_duration"), NewDuration);
	return FMonolithActionResult::Success(Root);
}

// ---------------------------------------------------------------------------
// Bone Tracks
// ---------------------------------------------------------------------------

FMonolithActionResult FMonolithAnimationActions::HandleAddBoneTrack(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	FString BoneName = Params->GetStringField(TEXT("bone_name"));

	UAnimSequence* Seq = FMonolithAssetUtils::LoadAssetByPath<UAnimSequence>(AssetPath);
	if (!Seq) return FMonolithActionResult::Error(FString::Printf(TEXT("AnimSequence not found: %s"), *AssetPath));

	IAnimationDataController& Controller = Seq->GetController();

	GEditor->BeginTransaction(FText::FromString(TEXT("Add Bone Track")));
	Seq->Modify();
	Controller.AddBoneCurve(FName(*BoneName));
	GEditor->EndTransaction();

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("bone_name"), BoneName);
	return FMonolithActionResult::Success(Root);
}

FMonolithActionResult FMonolithAnimationActions::HandleRemoveBoneTrack(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	FString BoneName = Params->GetStringField(TEXT("bone_name"));
	bool bIncludeChildren = false;
	Params->TryGetBoolField(TEXT("include_children"), bIncludeChildren);

	UAnimSequence* Seq = FMonolithAssetUtils::LoadAssetByPath<UAnimSequence>(AssetPath);
	if (!Seq) return FMonolithActionResult::Error(FString::Printf(TEXT("AnimSequence not found: %s"), *AssetPath));

	IAnimationDataController& Controller = Seq->GetController();

	// Null check skeleton for include_children
	if (bIncludeChildren && !Seq->GetSkeleton())
		return FMonolithActionResult::Error(TEXT("Skeleton is null — cannot resolve children"));

	TArray<FName> BonesToRemove;
	FName TargetBone(*BoneName);
	BonesToRemove.Add(TargetBone);

	if (bIncludeChildren && Seq->GetSkeleton())
	{
		const FReferenceSkeleton& RefSkel = Seq->GetSkeleton()->GetReferenceSkeleton();
		int32 BoneIndex = RefSkel.FindBoneIndex(TargetBone);
		if (BoneIndex != INDEX_NONE)
		{
			for (int32 i = 0; i < RefSkel.GetNum(); ++i)
			{
				int32 ParentIdx = i;
				while (ParentIdx != INDEX_NONE)
				{
					if (ParentIdx == BoneIndex && i != BoneIndex)
					{
						BonesToRemove.Add(RefSkel.GetBoneName(i));
						break;
					}
					ParentIdx = RefSkel.GetParentIndex(ParentIdx);
				}
			}
		}
	}

	GEditor->BeginTransaction(FText::FromString(TEXT("Remove Bone Track")));
	Seq->Modify();
	int32 RemovedCount = 0;
	for (const FName& Bone : BonesToRemove)
	{
		if (Controller.RemoveBoneTrack(Bone))
		{
			++RemovedCount;
		}
	}
	GEditor->EndTransaction();

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("bone_name"), BoneName);
	Root->SetBoolField(TEXT("include_children"), bIncludeChildren);
	Root->SetNumberField(TEXT("removed_count"), RemovedCount);
	return FMonolithActionResult::Success(Root);
}

FMonolithActionResult FMonolithAnimationActions::HandleSetBoneTrackKeys(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	FString BoneName = Params->GetStringField(TEXT("bone_name"));
	FString PositionsJson = Params->GetStringField(TEXT("positions_json"));
	FString RotationsJson = Params->GetStringField(TEXT("rotations_json"));
	FString ScalesJson = Params->GetStringField(TEXT("scales_json"));

	UAnimSequence* Seq = FMonolithAssetUtils::LoadAssetByPath<UAnimSequence>(AssetPath);
	if (!Seq) return FMonolithActionResult::Error(FString::Printf(TEXT("AnimSequence not found: %s"), *AssetPath));

	// Parse positions: [[x,y,z], ...]
	TArray<FVector> Positions;
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PositionsJson);
		TArray<TSharedPtr<FJsonValue>> Arr;
		if (FJsonSerializer::Deserialize(Reader, Arr))
		{
			for (const auto& Val : Arr)
			{
				const TArray<TSharedPtr<FJsonValue>>* Inner;
				if (Val->TryGetArray(Inner) && Inner->Num() >= 3)
				{
					Positions.Add(FVector((*Inner)[0]->AsNumber(), (*Inner)[1]->AsNumber(), (*Inner)[2]->AsNumber()));
				}
			}
		}
	}

	// Parse rotations: [[x,y,z,w], ...]
	TArray<FQuat> Rotations;
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RotationsJson);
		TArray<TSharedPtr<FJsonValue>> Arr;
		if (FJsonSerializer::Deserialize(Reader, Arr))
		{
			for (const auto& Val : Arr)
			{
				const TArray<TSharedPtr<FJsonValue>>* Inner;
				if (Val->TryGetArray(Inner) && Inner->Num() >= 4)
				{
					Rotations.Add(FQuat((*Inner)[0]->AsNumber(), (*Inner)[1]->AsNumber(), (*Inner)[2]->AsNumber(), (*Inner)[3]->AsNumber()));
				}
			}
		}
	}

	// Parse scales: [[x,y,z], ...]
	TArray<FVector> Scales;
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ScalesJson);
		TArray<TSharedPtr<FJsonValue>> Arr;
		if (FJsonSerializer::Deserialize(Reader, Arr))
		{
			for (const auto& Val : Arr)
			{
				const TArray<TSharedPtr<FJsonValue>>* Inner;
				if (Val->TryGetArray(Inner) && Inner->Num() >= 3)
				{
					Scales.Add(FVector((*Inner)[0]->AsNumber(), (*Inner)[1]->AsNumber(), (*Inner)[2]->AsNumber()));
				}
			}
		}
	}

	IAnimationDataController& Controller = Seq->GetController();

	GEditor->BeginTransaction(FText::FromString(TEXT("Set Bone Track Keys")));
	Seq->Modify();

	int32 NumKeys = FMath::Max3(Positions.Num(), Rotations.Num(), Scales.Num());
	Controller.SetBoneTrackKeys(FName(*BoneName), Positions, Rotations, Scales);

	GEditor->EndTransaction();

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("bone_name"), BoneName);
	Root->SetNumberField(TEXT("num_keys"), NumKeys);
	return FMonolithActionResult::Success(Root);
}

// ---------------------------------------------------------------------------
// Virtual Bones
// ---------------------------------------------------------------------------

FMonolithActionResult FMonolithAnimationActions::HandleAddVirtualBone(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	FString SourceBone = Params->GetStringField(TEXT("source_bone"));
	FString TargetBone = Params->GetStringField(TEXT("target_bone"));

	USkeleton* Skeleton = FMonolithAssetUtils::LoadAssetByPath<USkeleton>(AssetPath);
	if (!Skeleton) return FMonolithActionResult::Error(FString::Printf(TEXT("Skeleton not found: %s"), *AssetPath));

	FName VBoneName;

	GEditor->BeginTransaction(FText::FromString(TEXT("Add Virtual Bone")));
	Skeleton->Modify();
	bool bSuccess = Skeleton->AddNewVirtualBone(FName(*SourceBone), FName(*TargetBone), VBoneName);
	GEditor->EndTransaction();

	if (!bSuccess)
		return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to add virtual bone from '%s' to '%s'"), *SourceBone, *TargetBone));

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("virtual_bone"), VBoneName.ToString());
	Root->SetStringField(TEXT("source"), SourceBone);
	Root->SetStringField(TEXT("target"), TargetBone);
	return FMonolithActionResult::Success(Root);
}

FMonolithActionResult FMonolithAnimationActions::HandleRemoveVirtualBones(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));

	USkeleton* Skeleton = FMonolithAssetUtils::LoadAssetByPath<USkeleton>(AssetPath);
	if (!Skeleton) return FMonolithActionResult::Error(FString::Printf(TEXT("Skeleton not found: %s"), *AssetPath));

	// Extract bone names from JSON array
	TArray<FString> BoneNames;
	const TArray<TSharedPtr<FJsonValue>>* BoneNamesArray;
	if (Params->TryGetArrayField(TEXT("bone_names"), BoneNamesArray))
	{
		for (const auto& Val : *BoneNamesArray)
		{
			BoneNames.Add(Val->AsString());
		}
	}

	GEditor->BeginTransaction(FText::FromString(TEXT("Remove Virtual Bones")));
	Skeleton->Modify();

	TArray<FString> Removed;
	if (BoneNames.Num() == 0)
	{
		TArray<FName> AllVBNames;
		for (const FVirtualBone& VB : Skeleton->GetVirtualBones())
		{
			AllVBNames.Add(VB.VirtualBoneName);
		}
		if (AllVBNames.Num() > 0)
		{
			Skeleton->RemoveVirtualBones(AllVBNames);
		}
		Removed.Add(TEXT("all"));
	}
	else
	{
		TArray<FName> NamesToRemove;
		for (const FString& Name : BoneNames)
		{
			NamesToRemove.Add(FName(*Name));
			Removed.Add(Name);
		}
		Skeleton->RemoveVirtualBones(NamesToRemove);
	}

	GEditor->EndTransaction();

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> RemovedArr;
	for (const FString& R : Removed)
		RemovedArr.Add(MakeShared<FJsonValueString>(R));
	Root->SetArrayField(TEXT("removed"), RemovedArr);
	return FMonolithActionResult::Success(Root);
}

// ---------------------------------------------------------------------------
// Skeleton Info
// ---------------------------------------------------------------------------

FMonolithActionResult FMonolithAnimationActions::HandleGetSkeletonInfo(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));

	USkeleton* Skeleton = FMonolithAssetUtils::LoadAssetByPath<USkeleton>(AssetPath);
	if (!Skeleton) return FMonolithActionResult::Error(FString::Printf(TEXT("Skeleton not found: %s"), *AssetPath));

	const FReferenceSkeleton& RefSkel = Skeleton->GetReferenceSkeleton();
	int32 BoneCount = RefSkel.GetNum();

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("asset_path"), AssetPath);
	Root->SetNumberField(TEXT("bone_count"), BoneCount);

	TArray<TSharedPtr<FJsonValue>> BonesArr;
	for (int32 i = 0; i < BoneCount; ++i)
	{
		TSharedPtr<FJsonObject> BoneObj = MakeShared<FJsonObject>();
		BoneObj->SetNumberField(TEXT("index"), i);
		BoneObj->SetStringField(TEXT("name"), RefSkel.GetBoneName(i).ToString());
		BoneObj->SetNumberField(TEXT("parent_index"), RefSkel.GetParentIndex(i));
		if (RefSkel.GetParentIndex(i) != INDEX_NONE)
			BoneObj->SetStringField(TEXT("parent_name"), RefSkel.GetBoneName(RefSkel.GetParentIndex(i)).ToString());
		BonesArr.Add(MakeShared<FJsonValueObject>(BoneObj));
	}
	Root->SetArrayField(TEXT("bones"), BonesArr);

	const TArray<FVirtualBone>& VBones = Skeleton->GetVirtualBones();
	TArray<TSharedPtr<FJsonValue>> VBonesArr;
	for (const FVirtualBone& VB : VBones)
	{
		TSharedPtr<FJsonObject> VBObj = MakeShared<FJsonObject>();
		VBObj->SetStringField(TEXT("name"), VB.VirtualBoneName.ToString());
		VBObj->SetStringField(TEXT("source"), VB.SourceBoneName.ToString());
		VBObj->SetStringField(TEXT("target"), VB.TargetBoneName.ToString());
		VBonesArr.Add(MakeShared<FJsonValueObject>(VBObj));
	}
	Root->SetArrayField(TEXT("virtual_bones"), VBonesArr);

	return FMonolithActionResult::Success(Root);
}

FMonolithActionResult FMonolithAnimationActions::HandleGetSkeletalMeshInfo(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));

	USkeletalMesh* Mesh = FMonolithAssetUtils::LoadAssetByPath<USkeletalMesh>(AssetPath);
	if (!Mesh) return FMonolithActionResult::Error(FString::Printf(TEXT("SkeletalMesh not found: %s"), *AssetPath));

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("asset_path"), AssetPath);

	USkeleton* Skel = Mesh->GetSkeleton();
	Root->SetStringField(TEXT("skeleton"), Skel ? Skel->GetPathName() : TEXT("None"));

	// Morph targets
	TArray<TSharedPtr<FJsonValue>> MorphArr;
	TArray<FString> MorphNames = Mesh->K2_GetAllMorphTargetNames();
	for (const FString& MorphName : MorphNames)
	{
		MorphArr.Add(MakeShared<FJsonValueString>(MorphName));
	}
	Root->SetArrayField(TEXT("morph_targets"), MorphArr);
	Root->SetNumberField(TEXT("morph_target_count"), MorphArr.Num());

	// Sockets
	TArray<TSharedPtr<FJsonValue>> SocketArr;
	for (int32 i = 0; i < Mesh->NumSockets(); ++i)
	{
		USkeletalMeshSocket* Sock = Mesh->GetSocketByIndex(i);
		if (!Sock) continue;
		TSharedPtr<FJsonObject> SockObj = MakeShared<FJsonObject>();
		SockObj->SetStringField(TEXT("name"), Sock->SocketName.ToString());
		SockObj->SetStringField(TEXT("bone"), Sock->BoneName.ToString());
		SocketArr.Add(MakeShared<FJsonValueObject>(SockObj));
	}
	Root->SetArrayField(TEXT("sockets"), SocketArr);
	Root->SetNumberField(TEXT("socket_count"), SocketArr.Num());

	// LOD count
	int32 LODCount = 0;
	if (FSkeletalMeshRenderData* RenderData = Mesh->GetResourceForRendering())
	{
		LODCount = RenderData->LODRenderData.Num();
	}
	Root->SetNumberField(TEXT("lod_count"), LODCount);

	// Materials
	TArray<TSharedPtr<FJsonValue>> MatArr;
	for (int32 i = 0; i < Mesh->GetMaterials().Num(); ++i)
	{
		const FSkeletalMaterial& MatSlot = Mesh->GetMaterials()[i];
		TSharedPtr<FJsonObject> MatObj = MakeShared<FJsonObject>();
		MatObj->SetNumberField(TEXT("index"), i);
		MatObj->SetStringField(TEXT("name"), MatSlot.MaterialSlotName.ToString());
		MatObj->SetStringField(TEXT("material"), MatSlot.MaterialInterface ? MatSlot.MaterialInterface->GetPathName() : TEXT("None"));
		MatArr.Add(MakeShared<FJsonValueObject>(MatObj));
	}
	Root->SetArrayField(TEXT("materials"), MatArr);
	Root->SetNumberField(TEXT("material_count"), MatArr.Num());

	return FMonolithActionResult::Success(Root);
}

// ---------------------------------------------------------------------------
// ABP Graph Reading
// ---------------------------------------------------------------------------

FMonolithActionResult FMonolithAnimationActions::HandleGetStateMachines(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));

	UAnimBlueprint* ABP = FMonolithAssetUtils::LoadAssetByPath<UAnimBlueprint>(AssetPath);
	if (!ABP) return FMonolithActionResult::Error(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AssetPath));

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("asset_path"), AssetPath);
	TArray<TSharedPtr<FJsonValue>> MachinesArr;

	for (UEdGraph* Graph : ABP->FunctionGraphs)
	{
		if (!Graph) continue;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UAnimGraphNode_StateMachine* SMNode = Cast<UAnimGraphNode_StateMachine>(Node);
			if (!SMNode) continue;

			UAnimationStateMachineGraph* SMGraph = Cast<UAnimationStateMachineGraph>(SMNode->EditorStateMachineGraph);
			if (!SMGraph) continue;

			TSharedPtr<FJsonObject> MachineObj = MakeShared<FJsonObject>();
			FString SMName = SMNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
			int32 NewlineIdx = INDEX_NONE;
			if (SMName.FindChar(TEXT('\n'), NewlineIdx))
			{
				SMName.LeftInline(NewlineIdx);
			}
			MachineObj->SetStringField(TEXT("name"), SMName);
			MachineObj->SetStringField(TEXT("graph"), Graph->GetName());

			if (SMGraph->EntryNode)
			{
				for (UEdGraphPin* Pin : SMGraph->EntryNode->Pins)
				{
					if (Pin && Pin->Direction == EGPD_Output && Pin->LinkedTo.Num() > 0)
					{
						UAnimStateNode* EntryState = Cast<UAnimStateNode>(Pin->LinkedTo[0]->GetOwningNode());
						if (EntryState)
						{
							MachineObj->SetStringField(TEXT("entry_state"), EntryState->GetStateName());
						}
					}
				}
			}

			TArray<TSharedPtr<FJsonValue>> StatesArr;
			TArray<TSharedPtr<FJsonValue>> TransitionsArr;

			for (UEdGraphNode* SMChildNode : SMGraph->Nodes)
			{
				if (UAnimStateNode* StateNode = Cast<UAnimStateNode>(SMChildNode))
				{
					TSharedPtr<FJsonObject> StateObj = MakeShared<FJsonObject>();
					StateObj->SetStringField(TEXT("name"), StateNode->GetStateName());
					TArray<TSharedPtr<FJsonValue>> PosArr;
					PosArr.Add(MakeShared<FJsonValueNumber>(StateNode->NodePosX));
					PosArr.Add(MakeShared<FJsonValueNumber>(StateNode->NodePosY));
					StateObj->SetArrayField(TEXT("position"), PosArr);
					StatesArr.Add(MakeShared<FJsonValueObject>(StateObj));
				}
				else if (UAnimStateTransitionNode* TransNode = Cast<UAnimStateTransitionNode>(SMChildNode))
				{
					TSharedPtr<FJsonObject> TransObj = MakeShared<FJsonObject>();
					UAnimStateNodeBase* PrevState = TransNode->GetPreviousState();
					UAnimStateNodeBase* NextState = TransNode->GetNextState();
					TransObj->SetStringField(TEXT("from"), PrevState ? PrevState->GetStateName() : TEXT("?"));
					TransObj->SetStringField(TEXT("to"), NextState ? NextState->GetStateName() : TEXT("?"));
					TransObj->SetStringField(TEXT("from_type"), PrevState ? (Cast<UAnimStateConduitNode>(PrevState) ? TEXT("conduit") : TEXT("state")) : TEXT("unknown"));
					TransObj->SetStringField(TEXT("to_type"), NextState ? (Cast<UAnimStateConduitNode>(NextState) ? TEXT("conduit") : TEXT("state")) : TEXT("unknown"));
					TransObj->SetNumberField(TEXT("cross_fade_duration"), TransNode->CrossfadeDuration);
					TransObj->SetStringField(TEXT("blend_mode"),
						TransNode->BlendMode == EAlphaBlendOption::Linear ? TEXT("Linear") :
						TransNode->BlendMode == EAlphaBlendOption::Cubic ? TEXT("Cubic") : TEXT("Other"));
					TransObj->SetBoolField(TEXT("bidirectional"), TransNode->Bidirectional);
					TransitionsArr.Add(MakeShared<FJsonValueObject>(TransObj));
				}
			}

			MachineObj->SetArrayField(TEXT("states"), StatesArr);
			MachineObj->SetArrayField(TEXT("transitions"), TransitionsArr);
			MachineObj->SetNumberField(TEXT("state_count"), StatesArr.Num());
			MachineObj->SetNumberField(TEXT("transition_count"), TransitionsArr.Num());
			MachinesArr.Add(MakeShared<FJsonValueObject>(MachineObj));
		}
	}

	Root->SetArrayField(TEXT("state_machines"), MachinesArr);
	Root->SetNumberField(TEXT("count"), MachinesArr.Num());
	return FMonolithActionResult::Success(Root);
}

FMonolithActionResult FMonolithAnimationActions::HandleGetStateInfo(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	FString MachineName = Params->GetStringField(TEXT("machine_name"));
	FString StateName = Params->GetStringField(TEXT("state_name"));

	if (MachineName.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("Missing required parameter: machine_name"));
	}
	if (StateName.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("Missing required parameter: state_name"));
	}

	UAnimBlueprint* ABP = FMonolithAssetUtils::LoadAssetByPath<UAnimBlueprint>(AssetPath);
	if (!ABP) return FMonolithActionResult::Error(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AssetPath));

	for (UEdGraph* Graph : ABP->FunctionGraphs)
	{
		if (!Graph) continue;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UAnimGraphNode_StateMachine* SMNode = Cast<UAnimGraphNode_StateMachine>(Node);
			if (!SMNode) continue;

			FString SMTitle = SMNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
			int32 NewlineIdx = INDEX_NONE;
			if (SMTitle.FindChar(TEXT('\n'), NewlineIdx))
			{
				SMTitle.LeftInline(NewlineIdx);
			}
			if (SMTitle != MachineName) continue;

			UAnimationStateMachineGraph* SMGraph = Cast<UAnimationStateMachineGraph>(SMNode->EditorStateMachineGraph);
			if (!SMGraph) continue;

			for (UEdGraphNode* SMChildNode : SMGraph->Nodes)
			{
				UAnimStateNode* StateNode = Cast<UAnimStateNode>(SMChildNode);
				if (!StateNode || StateNode->GetStateName() != StateName) continue;

				TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
				Root->SetStringField(TEXT("state_name"), StateName);
				Root->SetStringField(TEXT("machine_name"), MachineName);

				TArray<TSharedPtr<FJsonValue>> PosArr;
				PosArr.Add(MakeShared<FJsonValueNumber>(StateNode->NodePosX));
				PosArr.Add(MakeShared<FJsonValueNumber>(StateNode->NodePosY));
				Root->SetArrayField(TEXT("position"), PosArr);

				UEdGraph* StateGraph = StateNode->GetBoundGraph();
				TArray<TSharedPtr<FJsonValue>> NodesArr;
				if (StateGraph)
				{
					for (UEdGraphNode* InnerNode : StateGraph->Nodes)
					{
						UAnimGraphNode_Base* AnimNode = Cast<UAnimGraphNode_Base>(InnerNode);
						if (!AnimNode) continue;
						TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
						NodeObj->SetStringField(TEXT("class"), AnimNode->GetClass()->GetName());
						NodeObj->SetStringField(TEXT("title"), AnimNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
						NodesArr.Add(MakeShared<FJsonValueObject>(NodeObj));
					}
				}
				Root->SetArrayField(TEXT("nodes"), NodesArr);
				Root->SetNumberField(TEXT("node_count"), NodesArr.Num());

				return FMonolithActionResult::Success(Root);
			}
		}
	}

	return FMonolithActionResult::Error(FString::Printf(TEXT("State '%s' not found in machine '%s'"), *StateName, *MachineName));
}

FMonolithActionResult FMonolithAnimationActions::HandleGetTransitions(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	FString MachineName = Params->GetStringField(TEXT("machine_name"));

	UAnimBlueprint* ABP = FMonolithAssetUtils::LoadAssetByPath<UAnimBlueprint>(AssetPath);
	if (!ABP) return FMonolithActionResult::Error(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AssetPath));

	// Helper lambda to collect transitions from a state machine graph
	auto CollectTransitions = [](UAnimationStateMachineGraph* SMGraph, TArray<TSharedPtr<FJsonValue>>& OutArr)
	{
		for (UEdGraphNode* SMChildNode : SMGraph->Nodes)
		{
			UAnimStateTransitionNode* TransNode = Cast<UAnimStateTransitionNode>(SMChildNode);
			if (!TransNode) continue;

			TSharedPtr<FJsonObject> TransObj = MakeShared<FJsonObject>();
			UAnimStateNodeBase* PrevState = TransNode->GetPreviousState();
			UAnimStateNodeBase* NextState = TransNode->GetNextState();
			TransObj->SetStringField(TEXT("from"), PrevState ? PrevState->GetStateName() : TEXT("?"));
			TransObj->SetStringField(TEXT("to"), NextState ? NextState->GetStateName() : TEXT("?"));
			TransObj->SetStringField(TEXT("from_type"), PrevState ? (Cast<UAnimStateConduitNode>(PrevState) ? TEXT("conduit") : TEXT("state")) : TEXT("unknown"));
			TransObj->SetStringField(TEXT("to_type"), NextState ? (Cast<UAnimStateConduitNode>(NextState) ? TEXT("conduit") : TEXT("state")) : TEXT("unknown"));
			TransObj->SetNumberField(TEXT("cross_fade_duration"), TransNode->CrossfadeDuration);
			TransObj->SetStringField(TEXT("blend_mode"),
				TransNode->BlendMode == EAlphaBlendOption::Linear ? TEXT("Linear") :
				TransNode->BlendMode == EAlphaBlendOption::Cubic ? TEXT("Cubic") : TEXT("Other"));
			TransObj->SetBoolField(TEXT("bidirectional"), TransNode->Bidirectional);

			UEdGraph* RuleGraph = TransNode->GetBoundGraph();
			TArray<TSharedPtr<FJsonValue>> RuleNodesArr;
			if (RuleGraph)
			{
				for (UEdGraphNode* RuleNode : RuleGraph->Nodes)
				{
					TSharedPtr<FJsonObject> RuleObj = MakeShared<FJsonObject>();
					RuleObj->SetStringField(TEXT("class"), RuleNode->GetClass()->GetName());
					RuleObj->SetStringField(TEXT("title"), RuleNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
					RuleNodesArr.Add(MakeShared<FJsonValueObject>(RuleObj));
				}
			}
			TransObj->SetArrayField(TEXT("rule_nodes"), RuleNodesArr);
			OutArr.Add(MakeShared<FJsonValueObject>(TransObj));
		}
	};

	// If machine_name is empty, return transitions from ALL state machines
	bool bMatchAll = MachineName.IsEmpty();
	TArray<TSharedPtr<FJsonValue>> AllTransitionsArr;
	bool bFoundAny = false;

	for (UEdGraph* Graph : ABP->FunctionGraphs)
	{
		if (!Graph) continue;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UAnimGraphNode_StateMachine* SMNode = Cast<UAnimGraphNode_StateMachine>(Node);
			if (!SMNode) continue;

			FString SMTitle = SMNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
			int32 NewlineIdx = INDEX_NONE;
			if (SMTitle.FindChar(TEXT('\n'), NewlineIdx))
			{
				SMTitle.LeftInline(NewlineIdx);
			}
			if (!bMatchAll && SMTitle != MachineName) continue;

			UAnimationStateMachineGraph* SMGraph = Cast<UAnimationStateMachineGraph>(SMNode->EditorStateMachineGraph);
			if (!SMGraph) continue;

			bFoundAny = true;

			if (!bMatchAll)
			{
				// Specific machine match — return immediately
				TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
				Root->SetStringField(TEXT("machine_name"), MachineName);
				TArray<TSharedPtr<FJsonValue>> TransitionsArr;
				CollectTransitions(SMGraph, TransitionsArr);
				Root->SetArrayField(TEXT("transitions"), TransitionsArr);
				Root->SetNumberField(TEXT("count"), TransitionsArr.Num());
				return FMonolithActionResult::Success(Root);
			}

			// Collect from all machines
			CollectTransitions(SMGraph, AllTransitionsArr);
		}
	}

	// Return collected results (may be empty)
	auto Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("machine_name"), MachineName);
	Result->SetArrayField(TEXT("transitions"), AllTransitionsArr);
	Result->SetNumberField(TEXT("count"), AllTransitionsArr.Num());
	return FMonolithActionResult::Success(Result);
}

FMonolithActionResult FMonolithAnimationActions::HandleGetBlendNodes(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	FString GraphName;
	Params->TryGetStringField(TEXT("graph_name"), GraphName);

	UAnimBlueprint* ABP = FMonolithAssetUtils::LoadAssetByPath<UAnimBlueprint>(AssetPath);
	if (!ABP) return FMonolithActionResult::Error(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AssetPath));

	for (UEdGraph* Graph : ABP->FunctionGraphs)
	{
		if (!Graph) continue;
		if (!GraphName.IsEmpty() && Graph->GetName() != GraphName) continue;

		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("graph_name"), Graph->GetName());
		TArray<TSharedPtr<FJsonValue>> NodesArr;

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UAnimGraphNode_Base* AnimNode = Cast<UAnimGraphNode_Base>(Node);
			if (!AnimNode) continue;

			TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
			NodeObj->SetStringField(TEXT("class"), AnimNode->GetClass()->GetName());
			NodeObj->SetStringField(TEXT("title"), AnimNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());

			TArray<TSharedPtr<FJsonValue>> PinsArr;
			for (UEdGraphPin* Pin : AnimNode->Pins)
			{
				if (!Pin || Pin->LinkedTo.Num() == 0) continue;
				TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
				PinObj->SetStringField(TEXT("name"), Pin->GetName());
				PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("Input") : TEXT("Output"));
				PinObj->SetNumberField(TEXT("connections"), Pin->LinkedTo.Num());
				PinsArr.Add(MakeShared<FJsonValueObject>(PinObj));
			}
			NodeObj->SetArrayField(TEXT("connected_pins"), PinsArr);
			NodesArr.Add(MakeShared<FJsonValueObject>(NodeObj));
		}

		Root->SetArrayField(TEXT("nodes"), NodesArr);
		Root->SetNumberField(TEXT("count"), NodesArr.Num());
		return FMonolithActionResult::Success(Root);
	}

	return FMonolithActionResult::Error(FString::Printf(TEXT("Graph '%s' not found"), *GraphName));
}

FMonolithActionResult FMonolithAnimationActions::HandleGetGraphs(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));

	UAnimBlueprint* ABP = FMonolithAssetUtils::LoadAssetByPath<UAnimBlueprint>(AssetPath);
	if (!ABP) return FMonolithActionResult::Error(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AssetPath));

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("asset_path"), AssetPath);
	TArray<TSharedPtr<FJsonValue>> GraphsArr;

	for (UEdGraph* Graph : ABP->FunctionGraphs)
	{
		if (!Graph) continue;

		TSharedPtr<FJsonObject> GraphObj = MakeShared<FJsonObject>();
		GraphObj->SetStringField(TEXT("name"), Graph->GetName());
		GraphObj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());

		int32 AnimNodeCount = 0;
		int32 StateMachineCount = 0;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Cast<UAnimGraphNode_StateMachine>(Node)) StateMachineCount++;
			if (Cast<UAnimGraphNode_Base>(Node)) AnimNodeCount++;
		}
		GraphObj->SetNumberField(TEXT("anim_node_count"), AnimNodeCount);
		GraphObj->SetNumberField(TEXT("state_machine_count"), StateMachineCount);
		GraphsArr.Add(MakeShared<FJsonValueObject>(GraphObj));
	}

	Root->SetArrayField(TEXT("graphs"), GraphsArr);
	Root->SetNumberField(TEXT("count"), GraphsArr.Num());
	return FMonolithActionResult::Success(Root);
}

FMonolithActionResult FMonolithAnimationActions::HandleGetNodes(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	FString NodeClassFilter;
	Params->TryGetStringField(TEXT("node_class_filter"), NodeClassFilter);
	FString GraphFilter;
	Params->TryGetStringField(TEXT("graph_name"), GraphFilter);

	UAnimBlueprint* ABP = FMonolithAssetUtils::LoadAssetByPath<UAnimBlueprint>(AssetPath);
	if (!ABP) return FMonolithActionResult::Error(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AssetPath));

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("asset_path"), AssetPath);
	if (!NodeClassFilter.IsEmpty())
	{
		Root->SetStringField(TEXT("filter_class"), NodeClassFilter);
	}
	if (!GraphFilter.IsEmpty())
	{
		Root->SetStringField(TEXT("graph_name"), GraphFilter);
	}
	TArray<TSharedPtr<FJsonValue>> NodesArr;

	for (UEdGraph* Graph : ABP->FunctionGraphs)
	{
		if (!Graph) continue;
		if (!GraphFilter.IsEmpty() && Graph->GetName() != GraphFilter) continue;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UAnimGraphNode_Base* AnimNode = Cast<UAnimGraphNode_Base>(Node);
			if (!AnimNode) continue;

			FString ClassName = AnimNode->GetClass()->GetName();
			if (!NodeClassFilter.IsEmpty() && !ClassName.Contains(NodeClassFilter)) continue;

			TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
			NodeObj->SetStringField(TEXT("class"), ClassName);
			NodeObj->SetStringField(TEXT("name"), AnimNode->GetName());
			NodeObj->SetStringField(TEXT("title"), AnimNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
			NodeObj->SetStringField(TEXT("graph"), Graph->GetName());

			TArray<TSharedPtr<FJsonValue>> PinsArr;
			for (UEdGraphPin* Pin : AnimNode->Pins)
			{
				if (!Pin || Pin->LinkedTo.Num() == 0) continue;
				TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
				PinObj->SetStringField(TEXT("name"), Pin->GetName());
				PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("Input") : TEXT("Output"));
				PinObj->SetNumberField(TEXT("connections"), Pin->LinkedTo.Num());
				PinsArr.Add(MakeShared<FJsonValueObject>(PinObj));
			}
			NodeObj->SetArrayField(TEXT("connected_pins"), PinsArr);
			NodesArr.Add(MakeShared<FJsonValueObject>(NodeObj));
		}
	}

	Root->SetArrayField(TEXT("nodes"), NodesArr);
	Root->SetNumberField(TEXT("count"), NodesArr.Num());
	return FMonolithActionResult::Success(Root);
}

FMonolithActionResult FMonolithAnimationActions::HandleGetLinkedLayers(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));

	UAnimBlueprint* ABP = FMonolithAssetUtils::LoadAssetByPath<UAnimBlueprint>(AssetPath);
	if (!ABP) return FMonolithActionResult::Error(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AssetPath));

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("asset_path"), AssetPath);
	TArray<TSharedPtr<FJsonValue>> LayersArr;

	for (UEdGraph* Graph : ABP->FunctionGraphs)
	{
		if (!Graph) continue;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UAnimGraphNode_LinkedAnimLayer* LayerNode = Cast<UAnimGraphNode_LinkedAnimLayer>(Node);
			if (!LayerNode) continue;

			TSharedPtr<FJsonObject> LayerObj = MakeShared<FJsonObject>();
			LayerObj->SetStringField(TEXT("title"), LayerNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
			LayerObj->SetStringField(TEXT("graph"), Graph->GetName());
			LayerObj->SetStringField(TEXT("class"), LayerNode->GetClass()->GetName());
			LayersArr.Add(MakeShared<FJsonValueObject>(LayerObj));
		}
	}

	Root->SetArrayField(TEXT("linked_layers"), LayersArr);
	Root->SetNumberField(TEXT("count"), LayersArr.Num());
	return FMonolithActionResult::Success(Root);
}
