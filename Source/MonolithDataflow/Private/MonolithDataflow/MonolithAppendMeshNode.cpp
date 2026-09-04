// AETHERKIN — Dataflow node: append two meshes into one.

#include "MonolithDataflow/MonolithAppendMeshNode.h"

#include "Dataflow/DataflowNodeFactory.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMeshEditor.h"
#include "Materials/MaterialInterface.h"

#define LOCTEXT_NAMESPACE "MonolithAppendMeshNode"

FMonolithAppendMeshNode::FMonolithAppendMeshNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FDataflowNode(InParam, InGuid)
{
	RegisterInputConnection(&MeshA);
	RegisterInputConnection(&MeshB);
	RegisterOutputConnection(&CombinedMesh, &MeshA);
}

void FMonolithAppendMeshNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	using namespace UE::Geometry;

	if (!Out || Out->GetName() != TEXT("CombinedMesh"))
	{
		return;
	}

	const TObjectPtr<UDataflowMesh> InA = GetValue(Context, &MeshA);
	const TObjectPtr<UDataflowMesh> InB = GetValue(Context, &MeshB);

	// Degrade to whichever side we actually have rather than emitting an empty mesh —
	// an empty output downstream reads as "the merge silently produced nothing".
	if (!InA || !InA->GetDynamicMesh())
	{
		SetValue(Context, InB, &CombinedMesh);
		return;
	}
	if (!InB || !InB->GetDynamicMesh())
	{
		SetValue(Context, InA, &CombinedMesh);
		return;
	}

	FDynamicMesh3 Combined;
	Combined.Copy(InA->GetDynamicMeshRef());

	// AppendMesh needs attributes enabled on the destination or per-triangle material
	// IDs and UV/normal overlays are dropped on the floor.
	if (!Combined.HasAttributes())
	{
		Combined.EnableAttributes();
	}

	const int32 MaterialOffset = bRemapMaterials ? InA->GetMaterials().Num() : 0;

	FDynamicMesh3 ToAppend;
	ToAppend.Copy(InB->GetDynamicMeshRef());
	if (!ToAppend.HasAttributes())
	{
		ToAppend.EnableAttributes();
	}

	// Shift B's material IDs up so they index into the concatenated list.
	if (MaterialOffset > 0 && ToAppend.Attributes() && ToAppend.Attributes()->HasMaterialID())
	{
		FDynamicMeshMaterialAttribute* const MaterialIDs = ToAppend.Attributes()->GetMaterialID();
		for (const int32 TriangleID : ToAppend.TriangleIndicesItr())
		{
			int32 MaterialID = 0;
			MaterialIDs->GetValue(TriangleID, &MaterialID);
			MaterialID += MaterialOffset;
			MaterialIDs->SetValue(TriangleID, &MaterialID);
		}
	}

	FDynamicMeshEditor Editor(&Combined);
	FMeshIndexMappings Mappings;
	Editor.AppendMesh(&ToAppend, Mappings);

	TArray<TObjectPtr<UMaterialInterface>> CombinedMaterials = InA->GetMaterials();
	if (bRemapMaterials)
	{
		CombinedMaterials.Append(InB->GetMaterials());
	}

	TObjectPtr<UDataflowMesh> OutMesh = NewObject<UDataflowMesh>();
	OutMesh->SetDynamicMesh(MoveTemp(Combined));
	OutMesh->SetMaterials(MoveTemp(CombinedMaterials));
	SetValue(Context, OutMesh, &CombinedMesh);
}

namespace UE::MonolithDataflow
{
	void RegisterMonolithDataflowNodes()
	{
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FMonolithAppendMeshNode);
	}
}

#undef LOCTEXT_NAMESPACE
