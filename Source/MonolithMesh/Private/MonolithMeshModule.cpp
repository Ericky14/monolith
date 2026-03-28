#include "MonolithMeshModule.h"
#include "MonolithMeshInspectionActions.h"
#include "MonolithMeshSceneActions.h"
#include "MonolithMeshSpatialActions.h"
#include "MonolithMeshBlockoutActions.h"
#include "MonolithMeshHorrorActions.h"
#include "MonolithMeshAccessibilityActions.h"
#include "MonolithMeshPerformanceActions.h"
#include "MonolithMeshLightingActions.h"
#include "MonolithMeshDecalActions.h"
#include "MonolithMeshAudioActions.h"
#include "MonolithMeshTemplateActions.h"
#include "MonolithMeshLevelDesignActions.h"
#include "MonolithMeshVolumeActions.h"
#include "MonolithMeshTechArtActions.h"
#include "MonolithMeshHorrorDesignActions.h"
#include "MonolithToolRegistry.h"
#include "MonolithJsonUtils.h"
#include "MonolithSettings.h"
#include "Misc/CoreDelegates.h"

#if WITH_GEOMETRYSCRIPT
#include "MonolithMeshOperationActions.h"
#include "MonolithMeshProceduralActions.h"
#include "MonolithMeshHandlePool.h"
#endif

#define LOCTEXT_NAMESPACE "FMonolithMeshModule"

void FMonolithMeshModule::StartupModule()
{
	if (!GetDefault<UMonolithSettings>()->bEnableMesh)
	{
		UE_LOG(LogMonolith, Log, TEXT("Monolith — Mesh module disabled via settings"));
		return;
	}

	FMonolithMeshInspectionActions::RegisterActions(FMonolithToolRegistry::Get());
	FMonolithMeshSceneActions::RegisterActions(FMonolithToolRegistry::Get());
	FMonolithMeshSpatialActions::RegisterActions(FMonolithToolRegistry::Get());
	FMonolithMeshBlockoutActions::RegisterActions(FMonolithToolRegistry::Get());
	FMonolithMeshHorrorActions::RegisterActions(FMonolithToolRegistry::Get());
	FMonolithMeshAccessibilityActions::RegisterActions(FMonolithToolRegistry::Get());
	FMonolithMeshPerformanceActions::RegisterActions(FMonolithToolRegistry::Get());
	FMonolithMeshLightingActions::RegisterActions(FMonolithToolRegistry::Get());
	FMonolithMeshDecalActions::RegisterActions(FMonolithToolRegistry::Get());
	FMonolithMeshAudioActions::RegisterActions(FMonolithToolRegistry::Get());
	FMonolithMeshTemplateActions::RegisterActions(FMonolithToolRegistry::Get());
	FMonolithMeshLevelDesignActions::RegisterActions(FMonolithToolRegistry::Get());
	FMonolithMeshVolumeActions::RegisterActions(FMonolithToolRegistry::Get());
	FMonolithMeshTechArtActions::RegisterActions(FMonolithToolRegistry::Get());
	FMonolithMeshHorrorDesignActions::RegisterActions(FMonolithToolRegistry::Get());

#if WITH_GEOMETRYSCRIPT
	HandlePool = NewObject<UMonolithMeshHandlePool>();
	HandlePool->AddToRoot();
	HandlePool->Initialize();
	FMonolithMeshOperationActions::SetHandlePool(HandlePool);
	FMonolithMeshOperationActions::RegisterActions(FMonolithToolRegistry::Get());
	FMonolithMeshProceduralActions::SetHandlePool(HandlePool);
	FMonolithMeshProceduralActions::RegisterActions(FMonolithToolRegistry::Get());
	FMonolithMeshTechArtActions::SetHandlePool(HandlePool);

	// Clean up handle pool on PreExit — before GC destroys UObjects.
	// ShutdownModule runs too late; by then the UObject array may be torn down.
	FCoreDelegates::OnPreExit.AddLambda([this]()
	{
		if (HandlePool && HandlePool->IsValidLowLevelFast())
		{
			HandlePool->Teardown();
			HandlePool->RemoveFromRoot();
			FMonolithMeshOperationActions::SetHandlePool(nullptr);
			FMonolithMeshProceduralActions::SetHandlePool(nullptr);
			FMonolithMeshTechArtActions::SetHandlePool(nullptr);
			HandlePool = nullptr;
		}
	});

	UE_LOG(LogMonolith, Log, TEXT("Monolith — Mesh operations enabled (GeometryScript available)"));
#endif

	UE_LOG(LogMonolith, Log, TEXT("Monolith — Mesh module loaded (%d actions)"),
		FMonolithToolRegistry::Get().GetActions(TEXT("mesh")).Num());
}

void FMonolithMeshModule::ShutdownModule()
{
	// Handle pool cleanup happens in OnPreExit (before GC destroys UObjects).
	// By the time ShutdownModule runs, the UObject array may already be torn down.
	// Just null our pointer defensively.
#if WITH_GEOMETRYSCRIPT
	HandlePool = nullptr;
#endif

	FMonolithToolRegistry::Get().UnregisterNamespace(TEXT("mesh"));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMonolithMeshModule, MonolithMesh)
