#pragma once

#include "MonolithIndexer.h"

/**
 * Indexes animation assets: AnimSequence, AnimMontage, BlendSpace, and PoseAsset.
 * Runs as a post-processing step on the game thread (requires asset loading).
 * Uses sentinel class "__Animations__" for dispatch.
 */
class FAnimationIndexer : public IMonolithIndexer
{
public:
	virtual TArray<FString> GetSupportedClasses() const override
	{
		return { TEXT("__Animations__") };
	}

	virtual bool IndexAsset(const FAssetData& AssetData, UObject* LoadedAsset, FMonolithIndexDatabase& DB, int64 AssetId) override;
	virtual FString GetName() const override { return TEXT("AnimationIndexer"); }

private:
	/** Index a single UAnimSequence */
	void IndexAnimSequence(class UAnimSequence* AnimSeq, FMonolithIndexDatabase& DB, int64 AssetId);

	/** Index a single UAnimMontage */
	void IndexAnimMontage(class UAnimMontage* Montage, FMonolithIndexDatabase& DB, int64 AssetId);

	/** Index a single UBlendSpace */
	void IndexBlendSpace(class UBlendSpace* BlendSpace, FMonolithIndexDatabase& DB, int64 AssetId);

	/** Index a single UPoseAsset */
	void IndexPoseAsset(class UPoseAsset* PoseAsset, FMonolithIndexDatabase& DB, int64 AssetId);

	/** Serialize anim notifies to a JSON array string */
	static FString NotifiesToJson(const TArray<struct FAnimNotifyEvent>& Notifies);
};
