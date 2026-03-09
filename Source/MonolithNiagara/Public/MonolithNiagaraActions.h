#pragma once

#include "CoreMinimal.h"
#include "MonolithToolRegistry.h"
#include "NiagaraCommon.h"

class UNiagaraSystem;
class UNiagaraNodeOutput;
class UNiagaraNodeFunctionCall;
class UNiagaraScript;
class UNiagaraGraph;
class UNiagaraRendererProperties;
struct FVersionedNiagaraEmitterData;
struct FNiagaraVariable;
struct FNiagaraParameterStore;

/**
 * Niagara domain action handlers for Monolith.
 * Ported from NiagaraMCPBridge — 39 proven actions across 7 domains.
 * Fixed for UE 5.7 API compatibility.
 */
class FMonolithNiagaraActions
{
public:
	/** Register all 41 niagara actions with the tool registry */
	static void RegisterActions(FMonolithToolRegistry& Registry);

	// --- System (8) ---
	static FMonolithActionResult HandleAddEmitter(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleRemoveEmitter(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleDuplicateEmitter(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleSetEmitterEnabled(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleReorderEmitters(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleSetEmitterProperty(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleRequestCompile(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleCreateSystem(const TSharedPtr<FJsonObject>& Params);

	// --- Module (12) ---
	static FMonolithActionResult HandleGetOrderedModules(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleGetModuleInputs(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleGetModuleGraph(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleAddModule(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleRemoveModule(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleMoveModule(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleSetModuleEnabled(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleSetModuleInputValue(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleSetModuleInputBinding(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleSetModuleInputDI(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleCreateModuleFromHLSL(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleCreateFunctionFromHLSL(const TSharedPtr<FJsonObject>& Params);

	// --- Parameter (9) ---
	static FMonolithActionResult HandleGetAllParameters(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleGetUserParameters(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleGetParameterValue(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleGetParameterType(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleTraceParameterBinding(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleAddUserParameter(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleRemoveUserParameter(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleSetParameterDefault(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleSetCurveValue(const TSharedPtr<FJsonObject>& Params);

	// --- Renderer (6) ---
	static FMonolithActionResult HandleAddRenderer(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleRemoveRenderer(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleSetRendererMaterial(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleSetRendererProperty(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleGetRendererBindings(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleSetRendererBinding(const TSharedPtr<FJsonObject>& Params);

	// --- Read (2) ---
	static FMonolithActionResult HandleListEmitters(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleListRenderers(const TSharedPtr<FJsonObject>& Params);

	// --- Batch (2) ---
	static FMonolithActionResult HandleBatchExecute(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleCreateSystemFromSpec(const TSharedPtr<FJsonObject>& Params);

	// --- Data Interface (1) ---
	static FMonolithActionResult HandleGetDIFunctions(const TSharedPtr<FJsonObject>& Params);

	// --- HLSL (1) ---
	static FMonolithActionResult HandleGetCompiledGPUHLSL(const TSharedPtr<FJsonObject>& Params);

	// --- Helpers (public for use by free functions) ---
	static FString SerializeParameterValue(const FNiagaraVariable& Variable, const FNiagaraParameterStore& Store);

private:
	// --- Internal helpers ---
	static UNiagaraSystem* LoadSystem(const FString& SystemPath);
	static int32 FindEmitterHandleIndex(UNiagaraSystem* System, const FString& HandleIdOrName);
	static bool ResolveScriptUsage(const FString& UsageString, ENiagaraScriptUsage& OutUsage);
	static FString UsageToString(ENiagaraScriptUsage Usage);
	static UNiagaraGraph* GetGraphForUsage(UNiagaraSystem* System, const FString& EmitterHandleId, ENiagaraScriptUsage Usage);
	static UNiagaraNodeOutput* FindOutputNode(UNiagaraSystem* System, const FString& EmitterHandleId, ENiagaraScriptUsage Usage);
	static UNiagaraNodeFunctionCall* FindModuleNode(UNiagaraSystem* System, const FString& EmitterHandleId, const FString& NodeGuidStr, ENiagaraScriptUsage* OutUsage = nullptr);
	static UClass* ResolveRendererClass(const FString& RendererClass);
	static UNiagaraRendererProperties* GetRenderer(UNiagaraSystem* System, const FString& EmitterHandleId, int32 RendererIndex, FVersionedNiagaraEmitterData** OutEmitterData = nullptr);
	static FNiagaraTypeDefinition ResolveNiagaraType(const FString& TypeName);
	static FNiagaraVariable MakeUserVariable(const FString& ParamName, const FNiagaraTypeDefinition& TypeDef);
	static FString JsonObjectToString(const TSharedRef<FJsonObject>& JsonObj);
	static FString JsonArrayToString(const TArray<TSharedPtr<FJsonValue>>& JsonArray);
	static FString JsonValueToString(const TSharedPtr<FJsonValue>& Value);

	// HLSL script creation helper
	static FMonolithActionResult CreateScriptFromHLSL(const TSharedPtr<FJsonObject>& Params, ENiagaraScriptUsage Usage);
};
