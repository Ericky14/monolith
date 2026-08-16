#pragma once

#include "MonolithIndexer.h"

/**
 * Indexes UInputMappingContext assets into the input_bindings table: one row per key -> InputAction
 * mapping.
 *
 * This closes the gap that made "what does the E key run?" unanswerable from the index. The
 * handler side is supplied by FBlueprintIndexer, which tags Enhanced Input event nodes with the
 * referenced action name; FMonolithIndexDatabase::FindInputHandlers joins the two.
 */
class FInputContextIndexer : public IMonolithIndexer
{
public:
	virtual TArray<FString> GetSupportedClasses() const override
	{
		return { TEXT("InputMappingContext") };
	}

	virtual bool IndexAsset(const FAssetData& AssetData, UObject* LoadedAsset, FMonolithIndexDatabase& DB, int64 AssetId) override;
	virtual FString GetName() const override { return TEXT("InputContextIndexer"); }
};
