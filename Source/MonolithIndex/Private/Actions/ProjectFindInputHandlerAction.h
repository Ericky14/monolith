#pragma once

#include "CoreMinimal.h"
#include "MonolithToolRegistry.h"

class FProjectFindInputHandlerAction
{
public:
	static FMonolithActionResult Execute(const TSharedPtr<FJsonObject>& Params);
	static FString GetName() { return TEXT("find_input_handler"); }
	static FString GetDescription()
	{
		return TEXT("What a key actually runs: key -> InputAction (which InputMappingContext bound it) -> the ")
			TEXT("Blueprint node handling it, with graph + node_id. Ask this BEFORE gating or changing input ")
			TEXT("behaviour - the handler is frequently not the function whose name matches the feature. Covers ")
			TEXT("Enhanced Input; legacy K2Node_InputKey handlers are matched by key name.");
	}
	static TSharedPtr<FJsonObject> GetSchema();
};
