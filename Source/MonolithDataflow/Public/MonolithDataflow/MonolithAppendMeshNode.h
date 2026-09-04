// AETHERKIN — Dataflow node: append two meshes into one.

#pragma once

#include "CoreMinimal.h"
#include "Dataflow/DataflowNode.h"
#include "Dataflow/DataflowMesh.h"
#include "MonolithAppendMeshNode.generated.h"

/**
 * Append MeshB onto MeshA, producing one combined Dataflow mesh.
 *
 * WHY THIS EXISTS
 * ---------------
 * The engine ships no merge/append node for Dataflow meshes (verified across
 * Dataflow/DataflowNodes, MeshResizing, ChaosClothAsset, ChaosOutfitAsset).
 *
 * The garment-refit pipeline needs one. MeshWrap conforms the ENTIRE source
 * topology onto the target shape, but an assembled MetaHuman body has no head —
 * the face ships as a separate skeletal mesh. Wrapping a full-bodied source
 * (e.g. SKM_Manny, which HAS a head) onto a headless target leaves the source's
 * head with nothing to project onto. That matters beyond cosmetics: MeshWarp's
 * RBF step samples interpolation points across the WHOLE source mesh
 * (MeshWarpNode.cpp:110-118), so garbage head positions corrupt the deformation
 * field and the error bleeds into the shoulders and upper chest — exactly where
 * armour fit matters most.
 *
 * Merging body + face restores a complete target and removes the problem, without
 * cropping the source (which would break helmets, hoods and cloaks).
 *
 * MATERIALS: MeshB's material indices are REMAPPED by MeshA's material count and
 * the two lists concatenated, so B's triangles keep their own materials instead of
 * silently drawing with A's.
 */
USTRUCT(meta = (DataflowMesh))
struct FMonolithAppendMeshNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FMonolithAppendMeshNode, "AppendMesh", "DataflowMesh", "Merge Combine Union Append")
	DATAFLOW_NODE_RENDER_TYPE("SurfaceRender", FName("UDataflowMesh"), "CombinedMesh")

public:

	FMonolithAppendMeshNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());

private:

	/** First mesh. Its material indices are preserved unchanged. */
	UPROPERTY(meta = (DataflowInput))
	TObjectPtr<UDataflowMesh> MeshA;

	/** Second mesh. Appended to MeshA; its material indices are offset by MeshA's material count. */
	UPROPERTY(meta = (DataflowInput))
	TObjectPtr<UDataflowMesh> MeshB;

	/** MeshA and MeshB combined into a single mesh. */
	UPROPERTY(meta = (DataflowOutput, DataflowPassthrough = "MeshA"))
	TObjectPtr<UDataflowMesh> CombinedMesh;

	/** Concatenate the material lists and offset MeshB's indices. Disable only if both meshes intentionally share one material list. */
	UPROPERTY(EditAnywhere, Category = "Append Mesh")
	bool bRemapMaterials = true;

	//~ Begin FDataflowNode interface
	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
	//~ End FDataflowNode interface
};

namespace UE::MonolithDataflow
{
	void RegisterMonolithDataflowNodes();
}
