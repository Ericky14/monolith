#include "Indexers/InputContextIndexer.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "InputTriggers.h"
#include "EnhancedActionKeyMapping.h"

bool FInputContextIndexer::IndexAsset(const FAssetData& AssetData, UObject* LoadedAsset, FMonolithIndexDatabase& DB, int64 AssetId)
{
	UInputMappingContext* Context = Cast<UInputMappingContext>(LoadedAsset);
	if (!Context)
	{
		return false;
	}

	// GetMappings() reads DefaultKeyMappings.Mappings. The bare `Mappings` array is DEPRECATED as
	// of 5.7 and reads EMPTY on assets saved by this engine - iterating it silently indexes
	// nothing, which is exactly how the key->action data went missing before.
	for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
	{
		if (!Mapping.Action)
		{
			continue; // an unassigned row in the editor - not a binding
		}

		FIndexedInputBinding Binding;
		Binding.ContextAssetId = AssetId;
		// FKey's display string is empty for some keys; the FName is the stable identifier and is
		// what a caller asking about "E" or "Gamepad_FaceButton_Left" will type.
		Binding.KeyName = Mapping.Key.GetFName().ToString();
		Binding.ActionName = Mapping.Action->GetName();
		Binding.ActionPath = Mapping.Action->GetPathName();

		// Triggers decide WHEN the action fires (Pressed/Hold/Released). Recorded as a flat list
		// because "the key is bound but only on hold" is a real answer to "why did nothing happen".
		TArray<FString> TriggerNames;
		for (const UInputTrigger* Trigger : Mapping.Triggers)
		{
			if (Trigger)
			{
				TriggerNames.Add(Trigger->GetClass()->GetName());
			}
		}
		Binding.Triggers = FString::Join(TriggerNames, TEXT(","));

		DB.InsertInputBinding(Binding);
	}

	return true;
}
