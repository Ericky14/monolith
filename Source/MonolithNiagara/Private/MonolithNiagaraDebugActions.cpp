#include "MonolithNiagaraDebugActions.h"
#include "MonolithParamSchema.h"
#include "MonolithToolRegistry.h"

#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemInstanceController.h" // brings NiagaraSystemInstance.h + NiagaraSystemSimulation.h
#include "NiagaraEmitterInstance.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraDataSet.h"
#include "NiagaraDataSetAccessor.h"
#include "NiagaraParameterStore.h"
#include "NiagaraUserRedirectionParameterStore.h"
#include "NiagaraTypes.h"

#include "Editor.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Actor.h"
#include "UObject/UObjectIterator.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

DEFINE_LOG_CATEGORY_STATIC(LogMonolithNiagaraDebug, Log, All);

// ============================================================================
//  Local helpers (named namespace per module convention — see issue #68 note in
//  MonolithNiagaraActions.cpp about cross-TU collisions)
// ============================================================================

namespace MonolithNiagaraDebugLocal
{
	// Stable distinct palette, cycled by emitter index (spec: red/green/blue/magenta/yellow/cyan).
	static const FColor& PaletteColor(int32 EmitterIndex)
	{
		static const FColor Palette[6] =
		{
			FColor::Red, FColor::Green, FColor::Blue,
			FColor::Magenta, FColor::Yellow, FColor::Cyan
		};
		return Palette[FMath::Abs(EmitterIndex) % 6];
	}

	// What we know how to read out of a CPU particle buffer.
	enum class EAttrKind : uint8
	{
		Float,
		Int32,
		Bool,
		Vec2,
		Vec3,
		Vec4,
		Color,
		Quat,
		Position,   // FNiagaraPosition (LWC sim-space) — gets sim->world conversion
		NiagaraID,  // FNiagaraID {Index, AcquireTag}
	};

	// One requested attribute, resolved against an emitter's particle data set.
	// Readers are cheap POD-ish structs (raw component pointers + count) — safe to
	// hold for the duration of one game-thread sampling pass.
	struct FAttrReader
	{
		FString RequestedName;   // exactly as the caller asked
		FString OutputKey;       // JSON key ("Position" -> local_position/world_position pair)
		EAttrKind Kind = EAttrKind::Float;
		bool bIsCanonicalPosition = false; // the "Position" attribute (drives debug draw + local/world fields)

		FNiagaraDataSetReaderFloat<float>            FloatReader;
		FNiagaraDataSetReaderInt32<int32>            IntReader;
		FNiagaraDataSetReaderInt32<FNiagaraBool>     BoolReader;
		FNiagaraDataSetReaderFloat<FVector2f>        Vec2Reader;
		FNiagaraDataSetReaderFloat<FVector3f>        Vec3Reader;
		FNiagaraDataSetReaderFloat<FVector4f>        Vec4Reader;
		FNiagaraDataSetReaderFloat<FLinearColor>     ColorReader;
		FNiagaraDataSetReaderFloat<FQuat4f>          QuatReader;
		FNiagaraDataSetReaderFloat<FNiagaraPosition> PositionReader;
		FNiagaraDataSetReaderStruct<FNiagaraID>      IDReader;

		bool IsValid() const
		{
			switch (Kind)
			{
				case EAttrKind::Float:     return FloatReader.IsValid();
				case EAttrKind::Int32:     return IntReader.IsValid();
				case EAttrKind::Bool:      return BoolReader.IsValid();
				case EAttrKind::Vec2:      return Vec2Reader.IsValid();
				case EAttrKind::Vec3:      return Vec3Reader.IsValid();
				case EAttrKind::Vec4:      return Vec4Reader.IsValid();
				case EAttrKind::Color:     return ColorReader.IsValid();
				case EAttrKind::Quat:      return QuatReader.IsValid();
				case EAttrKind::Position:  return PositionReader.IsValid();
				case EAttrKind::NiagaraID: return IDReader.IsValid();
			}
			return false;
		}
	};

	static FString OutputKeyForAttribute(const FString& Requested)
	{
		if (Requested.Equals(TEXT("Velocity"), ESearchCase::IgnoreCase))      return TEXT("velocity");
		if (Requested.Equals(TEXT("Color"), ESearchCase::IgnoreCase))         return TEXT("color");
		if (Requested.Equals(TEXT("NormalizedAge"), ESearchCase::IgnoreCase)) return TEXT("normalized_age");
		if (Requested.Equals(TEXT("UniqueID"), ESearchCase::IgnoreCase))      return TEXT("UniqueID");
		return Requested; // everything else keeps the requested name verbatim
	}

	static TSharedPtr<FJsonValue> Vec2Json(const FVector2f& V)
	{
		TArray<TSharedPtr<FJsonValue>> A;
		A.Add(MakeShared<FJsonValueNumber>(V.X));
		A.Add(MakeShared<FJsonValueNumber>(V.Y));
		return MakeShared<FJsonValueArray>(A);
	}

	static TSharedPtr<FJsonValue> Vec3Json(const FVector3f& V)
	{
		TArray<TSharedPtr<FJsonValue>> A;
		A.Add(MakeShared<FJsonValueNumber>(V.X));
		A.Add(MakeShared<FJsonValueNumber>(V.Y));
		A.Add(MakeShared<FJsonValueNumber>(V.Z));
		return MakeShared<FJsonValueArray>(A);
	}

	static TSharedPtr<FJsonValue> DVec3Json(const FVector& V)
	{
		TArray<TSharedPtr<FJsonValue>> A;
		A.Add(MakeShared<FJsonValueNumber>(V.X));
		A.Add(MakeShared<FJsonValueNumber>(V.Y));
		A.Add(MakeShared<FJsonValueNumber>(V.Z));
		return MakeShared<FJsonValueArray>(A);
	}

	static TSharedPtr<FJsonValue> Vec4Json(const FVector4f& V)
	{
		TArray<TSharedPtr<FJsonValue>> A;
		A.Add(MakeShared<FJsonValueNumber>(V.X));
		A.Add(MakeShared<FJsonValueNumber>(V.Y));
		A.Add(MakeShared<FJsonValueNumber>(V.Z));
		A.Add(MakeShared<FJsonValueNumber>(V.W));
		return MakeShared<FJsonValueArray>(A);
	}

	static TSharedPtr<FJsonValue> ColorJson(const FLinearColor& C)
	{
		TArray<TSharedPtr<FJsonValue>> A;
		A.Add(MakeShared<FJsonValueNumber>(C.R));
		A.Add(MakeShared<FJsonValueNumber>(C.G));
		A.Add(MakeShared<FJsonValueNumber>(C.B));
		A.Add(MakeShared<FJsonValueNumber>(C.A));
		return MakeShared<FJsonValueArray>(A);
	}

	// Dump a component's user-facing override parameters (UNiagaraComponent::GetOverrideParameters,
	// an FNiagaraUserRedirectionParameterStore — canonical entries carry the "User." prefix; the
	// unprefixed aliases live in a separate redirect map and are NOT duplicated here). Value types
	// only — float/int/bool/Vec2/Vec3/Position/Vec4/Color/Quat; data-interface, UObject, and other
	// struct/enum parameters are skipped. JSON keys are the names with the "User." prefix stripped.
	// Captures a float "VisibleEdgeZ" user parameter into OutVisibleEdgeZ when present (drives the
	// visible-edge invariant in the handler).
	static TSharedPtr<FJsonObject> DumpUserParameters(const FNiagaraParameterStore& Store, TOptional<float>& OutVisibleEdgeZ)
	{
		TSharedPtr<FJsonObject> ParamsJson = MakeShared<FJsonObject>();
		const TArray<uint8>& Bytes = Store.GetParameterDataArray();

		for (const FNiagaraVariableWithOffset& Param : Store.ReadParameterVariables())
		{
			// DI/UObject offsets index separate tables, not the byte buffer — skip.
			if (Param.IsDataInterface() || Param.IsUObject() || Param.Offset == INDEX_NONE)
			{
				continue;
			}

			const FNiagaraTypeDefinition& Type = Param.GetType();
			if (Param.Offset < 0 || Param.Offset + Type.GetSize() > Bytes.Num())
			{
				continue; // defensive: never read out of bounds
			}
			const uint8* Data = Bytes.GetData() + Param.Offset;

			FString Key = Param.GetName().ToString();
			Key.RemoveFromStart(TEXT("User."));

			// Memcpy out of the raw buffer (alignment-safe), dispatched on the stored type.
			if (Type == FNiagaraTypeDefinition::GetFloatDef())
			{
				float V; FMemory::Memcpy(&V, Data, sizeof(V));
				ParamsJson->SetNumberField(Key, V);
				if (Key.Equals(TEXT("VisibleEdgeZ"), ESearchCase::IgnoreCase))
				{
					OutVisibleEdgeZ = V;
				}
			}
			else if (Type == FNiagaraTypeDefinition::GetIntDef())
			{
				int32 V; FMemory::Memcpy(&V, Data, sizeof(V));
				ParamsJson->SetNumberField(Key, V);
			}
			else if (Type == FNiagaraTypeDefinition::GetBoolDef())
			{
				FNiagaraBool V; FMemory::Memcpy(&V, Data, sizeof(V));
				ParamsJson->SetBoolField(Key, V.GetValue());
			}
			else if (Type == FNiagaraTypeDefinition::GetVec2Def())
			{
				FVector2f V; FMemory::Memcpy(&V, Data, sizeof(V));
				ParamsJson->SetField(Key, Vec2Json(V));
			}
			else if (Type == FNiagaraTypeDefinition::GetVec3Def())
			{
				FVector3f V; FMemory::Memcpy(&V, Data, sizeof(V));
				ParamsJson->SetField(Key, Vec3Json(V));
			}
			else if (Type == FNiagaraTypeDefinition::GetPositionDef())
			{
				// LWC: Position params keep a double-precision original keyed by name —
				// prefer it, fall back to the sim-facing float bytes.
				if (const FVector* Original = Store.GetPositionParameterValue(Param.GetName()))
				{
					ParamsJson->SetField(Key, DVec3Json(*Original));
				}
				else
				{
					FVector3f V; FMemory::Memcpy(&V, Data, sizeof(V));
					ParamsJson->SetField(Key, Vec3Json(V));
				}
			}
			else if (Type == FNiagaraTypeDefinition::GetVec4Def())
			{
				FVector4f V; FMemory::Memcpy(&V, Data, sizeof(V));
				ParamsJson->SetField(Key, Vec4Json(V));
			}
			else if (Type == FNiagaraTypeDefinition::GetColorDef())
			{
				FLinearColor V; FMemory::Memcpy(&V, Data, sizeof(V));
				ParamsJson->SetField(Key, ColorJson(V));
			}
			else if (Type == FNiagaraTypeDefinition::GetQuatDef())
			{
				FQuat4f V; FMemory::Memcpy(&V, Data, sizeof(V));
				ParamsJson->SetField(Key, Vec4Json(FVector4f(V.X, V.Y, V.Z, V.W)));
			}
			// else: unsupported value type (enum / custom struct) — skipped.
		}
		return ParamsJson;
	}

	// Resolve one requested attribute against a CPU particle data set. Returns false
	// (with OutNote) when the attribute doesn't exist or its type isn't readable.
	static bool ResolveAttribute(const FNiagaraDataSet& DataSet, const FString& Requested,
		FAttrReader& Out, FString& OutNote)
	{
		Out.RequestedName = Requested;
		Out.OutputKey = OutputKeyForAttribute(Requested);
		const FName AttrName(*Requested); // FName compare below is case-insensitive

		// "Position" always goes through the FNiagaraPosition accessor: its Init falls back
		// Position -> Vec3 -> HalfVec3, covering every storage form the compiler may pick.
		if (Requested.Equals(TEXT("Position"), ESearchCase::IgnoreCase))
		{
			Out.Kind = EAttrKind::Position;
			Out.bIsCanonicalPosition = true;
			Out.PositionReader = FNiagaraDataSetAccessor<FNiagaraPosition>::CreateReader(DataSet, AttrName);
			if (!Out.PositionReader.IsValid())
			{
				OutNote = FString::Printf(TEXT("attribute '%s' not found in particle data"), *Requested);
				return false;
			}
			return true;
		}

		const FNiagaraDataSetCompiledData& Compiled = DataSet.GetCompiledData();
		const FNiagaraVariableBase* Var = Compiled.Variables.FindByPredicate(
			[&AttrName](const FNiagaraVariableBase& V) { return V.GetName() == AttrName; });
		if (Var == nullptr)
		{
			OutNote = FString::Printf(TEXT("attribute '%s' not found in particle data"), *Requested);
			return false;
		}

		const FNiagaraTypeDefinition& Type = Var->GetType();
		if (Type == FNiagaraTypeDefinition::GetPositionDef())
		{
			Out.Kind = EAttrKind::Position;
			Out.PositionReader = FNiagaraDataSetAccessor<FNiagaraPosition>::CreateReader(DataSet, AttrName);
		}
		else if (Type == FNiagaraTypeDefinition::GetVec3Def() || Type == FNiagaraTypeDefinition::GetHalfVec3Def())
		{
			Out.Kind = EAttrKind::Vec3;
			Out.Vec3Reader = FNiagaraDataSetAccessor<FVector3f>::CreateReader(DataSet, AttrName);
		}
		else if (Type == FNiagaraTypeDefinition::GetVec2Def() || Type == FNiagaraTypeDefinition::GetHalfVec2Def())
		{
			Out.Kind = EAttrKind::Vec2;
			Out.Vec2Reader = FNiagaraDataSetAccessor<FVector2f>::CreateReader(DataSet, AttrName);
		}
		else if (Type == FNiagaraTypeDefinition::GetColorDef())
		{
			Out.Kind = EAttrKind::Color;
			Out.ColorReader = FNiagaraDataSetAccessor<FLinearColor>::CreateReader(DataSet, AttrName);
		}
		else if (Type == FNiagaraTypeDefinition::GetVec4Def() || Type == FNiagaraTypeDefinition::GetHalfVec4Def())
		{
			Out.Kind = EAttrKind::Vec4;
			Out.Vec4Reader = FNiagaraDataSetAccessor<FVector4f>::CreateReader(DataSet, AttrName);
		}
		else if (Type == FNiagaraTypeDefinition::GetQuatDef())
		{
			Out.Kind = EAttrKind::Quat;
			Out.QuatReader = FNiagaraDataSetAccessor<FQuat4f>::CreateReader(DataSet, AttrName);
		}
		else if (Type == FNiagaraTypeDefinition::GetFloatDef() || Type == FNiagaraTypeDefinition::GetHalfDef())
		{
			Out.Kind = EAttrKind::Float;
			Out.FloatReader = FNiagaraDataSetAccessor<float>::CreateReader(DataSet, AttrName);
		}
		else if (Type == FNiagaraTypeDefinition::GetIntDef())
		{
			Out.Kind = EAttrKind::Int32;
			Out.IntReader = FNiagaraDataSetAccessor<int32>::CreateReader(DataSet, AttrName);
		}
		else if (Type == FNiagaraTypeDefinition::GetBoolDef())
		{
			Out.Kind = EAttrKind::Bool;
			Out.BoolReader = FNiagaraDataSetAccessor<FNiagaraBool>::CreateReader(DataSet, AttrName);
		}
		else if (Type == FNiagaraTypeDefinition(FNiagaraID::StaticStruct()))
		{
			Out.Kind = EAttrKind::NiagaraID;
			Out.IDReader = FNiagaraDataSetAccessor<FNiagaraID>::CreateReader(DataSet, AttrName);
		}
		else
		{
			OutNote = FString::Printf(TEXT("attribute '%s' has unsupported type '%s'"),
				*Requested, *Type.GetName());
			return false;
		}

		if (!Out.IsValid())
		{
			OutNote = FString::Printf(TEXT("attribute '%s' found but reader failed to bind (buffer empty?)"), *Requested);
			return false;
		}
		return true;
	}
}

// ============================================================================
//  Registration
// ============================================================================

void FMonolithNiagaraDebugActions::RegisterActions(FMonolithToolRegistry& Registry)
{
	Registry.RegisterAction(TEXT("niagara"), TEXT("dump_particles"),
		TEXT("Per-particle numeric readback (+ optional debug draw) from LIVE NiagaraComponents in the PIE world (preferred when a PIE session is active) or the editor world. "
			"CPU-sim emitters only: GPU-sim emitters are listed with sim_target='GPU', skipped=true (no readback). "
			"Samples evenly across the particle buffer (not just the first N). "
			"Per emitter reports local_space (emitter bLocalSpace) and per particle BOTH local_position (raw sim value for local-space; component-space back-computation for world-space) "
			"and world_position (LWC sim-to-world applied). Unknown requested attributes produce per-attribute 'not found' notes (attributes_not_found + available_attributes) — not a failure. "
			"With draw=true each sampled particle gets a DrawDebugPoint at its world position, color-cycled red/green/blue/magenta/yellow/cyan by emitter index. "
			"Each component also reports user_parameters — the values the driving code pushed into its exposed (User.) override parameter store, keyed by name with the 'User.' prefix STRIPPED "
			"(value types only: float/int/bool/Vec2/Vec3/Position/Vec4/Color/Quat; data-interface and object parameters skipped). "
			"Visible-edge invariant (generic, opt-in by data): when user_parameters contains a float 'VisibleEdgeZ' (a reveal-edge height in emitter-local Z), every LOCAL-SPACE emitter additionally gets "
			"per-particle z_beyond_visible_edge (= local_position.z - VisibleEdgeZ) and a visible_edge summary {visible_edge_z, max_z_beyond, count_beyond_3cm, arrivals_beyond_3cm} computed over the FULL buffer "
			"(arrivals = particles beyond 3cm with NormalizedAge >= 0.75; omitted with a note when the emitter lacks NormalizedAge). "
			"Returns {components:[{component, component_path, owner_actor, system, world, user_parameters:{...}, emitters:[{name, sim_target, local_space, num_particles, sampled, visible_edge?, particles:[{index, UniqueID, local_position, world_position, z_beyond_visible_edge?, velocity, color, normalized_age, ...}]}]}], drawn}."),
		FMonolithActionHandler::CreateStatic(&HandleDumpParticles),
		FParamSchemaBuilder()
			.OptionalAssetPath(TEXT("system_path"), TEXT("Filter: only components whose Niagara system asset matches this /Game path (package or object path; substring accepted)"))
			.Optional(TEXT("actor_name"), TEXT("string"), TEXT("Filter: substring match (case-insensitive) on the owning actor's label or name"))
			.Optional(TEXT("emitter"), TEXT("string"), TEXT("Filter: single emitter name (exact, case-insensitive)"))
			.Optional(TEXT("max_particles"), TEXT("integer"), TEXT("Per-emitter sample cap, sampled evenly across the buffer (clamped 1..10000)"), TEXT("200"))
			.Optional(TEXT("attributes"), TEXT("array"), TEXT("Particle attribute names to read (default: [\"Position\",\"Velocity\",\"Color\",\"NormalizedAge\",\"UniqueID\"]). Supported types: float/int/bool/Vec2/Vec3/Vec4/Color/Quat/Position/NiagaraID (half-compressed variants included)"))
			.Optional(TEXT("draw"), TEXT("boolean"), TEXT("DrawDebugPoint each sampled particle at its world position (default: false)"), TEXT("false"))
			.Optional(TEXT("draw_duration"), TEXT("number"), TEXT("Debug point lifetime in seconds (default: 5.0)"), TEXT("5.0"))
			.Build());
}

// ============================================================================
//  dump_particles
// ============================================================================

// Motivation: a week-long VFX debugging saga was solved only when per-particle positions
// were finally read back via a convoluted SimCache python recipe — this action makes that
// one MCP call. Readback recipe mirrors the engine's own FNiagaraDebugHud in-world
// particle display (NiagaraDebugHud.cpp): wait for the concurrent tick, read the current
// FNiagaraDataBuffer through FNiagaraDataSetAccessor readers, convert sim->world via the
// system instance's LWC transform.
FMonolithActionResult FMonolithNiagaraDebugActions::HandleDumpParticles(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MonolithNiagaraDebugLocal;

	// ---- Params ----
	FString SystemPathFilter;
	Params->TryGetStringField(TEXT("system_path"), SystemPathFilter);
	FString ActorNameFilter;
	Params->TryGetStringField(TEXT("actor_name"), ActorNameFilter);
	FString EmitterFilter;
	Params->TryGetStringField(TEXT("emitter"), EmitterFilter);

	int32 MaxParticles = 200;
	{
		double N = 0.0;
		if (Params->TryGetNumberField(TEXT("max_particles"), N))
		{
			MaxParticles = FMath::Clamp(static_cast<int32>(N), 1, 10000);
		}
	}

	TArray<FString> RequestedAttributes;
	{
		const TArray<TSharedPtr<FJsonValue>>* AttrArr = nullptr;
		if (Params->TryGetArrayField(TEXT("attributes"), AttrArr))
		{
			for (const TSharedPtr<FJsonValue>& V : *AttrArr)
			{
				FString S;
				if (V.IsValid() && V->TryGetString(S) && !S.IsEmpty())
				{
					RequestedAttributes.AddUnique(S);
				}
			}
		}
		if (RequestedAttributes.Num() == 0)
		{
			RequestedAttributes = { TEXT("Position"), TEXT("Velocity"), TEXT("Color"), TEXT("NormalizedAge"), TEXT("UniqueID") };
		}
	}

	bool bDraw = false;
	Params->TryGetBoolField(TEXT("draw"), bDraw);
	double DrawDuration = 5.0;
	Params->TryGetNumberField(TEXT("draw_duration"), DrawDuration);

	// ---- World resolution: prefer the live PIE world, else the editor world ----
	if (GEditor == nullptr)
	{
		return FMonolithActionResult::Error(TEXT("GEditor unavailable — editor not running"));
	}
	UWorld* TargetWorld = nullptr;
	FString WorldKind;
	if (FWorldContext* PIEContext = GEditor->GetPIEWorldContext())
	{
		TargetWorld = PIEContext->World();
		WorldKind = TEXT("PIE");
	}
	if (TargetWorld == nullptr)
	{
		TargetWorld = GEditor->GetEditorWorldContext().World();
		WorldKind = TEXT("Editor");
	}
	if (TargetWorld == nullptr)
	{
		return FMonolithActionResult::Error(TEXT("No PIE or editor world available"));
	}

	// ---- Gather matching live components (game-thread only, like all Monolith actions) ----
	bool bDrewAnything = false;
	TArray<TSharedPtr<FJsonValue>> ComponentsJson;

	for (TObjectIterator<UNiagaraComponent> It; It; ++It)
	{
		UNiagaraComponent* Comp = *It;
		if (Comp == nullptr || Comp->IsTemplate() || !IsValid(Comp))
		{
			continue;
		}
		if (Comp->GetWorld() != TargetWorld)
		{
			continue;
		}

		UNiagaraSystem* Asset = Comp->GetAsset();
		if (!SystemPathFilter.IsEmpty())
		{
			if (Asset == nullptr)
			{
				continue;
			}
			const FString ObjectPath = Asset->GetPathName();          // /Game/X/NS_Foo.NS_Foo
			const FString PackagePath = Asset->GetPackage()->GetName(); // /Game/X/NS_Foo
			const bool bMatch =
				ObjectPath.Equals(SystemPathFilter, ESearchCase::IgnoreCase) ||
				PackagePath.Equals(SystemPathFilter, ESearchCase::IgnoreCase) ||
				ObjectPath.Contains(SystemPathFilter, ESearchCase::IgnoreCase);
			if (!bMatch)
			{
				continue;
			}
		}

		AActor* Owner = Comp->GetOwner();
		if (!ActorNameFilter.IsEmpty())
		{
			if (Owner == nullptr)
			{
				continue;
			}
			const bool bMatch =
				Owner->GetName().Contains(ActorNameFilter, ESearchCase::IgnoreCase) ||
				Owner->GetActorLabel().Contains(ActorNameFilter, ESearchCase::IgnoreCase);
			if (!bMatch)
			{
				continue;
			}
		}

		FNiagaraSystemInstanceControllerPtr Controller = Comp->GetSystemInstanceController();
		const bool bHasInstance = Controller.IsValid() && Controller->IsValid();
		if (!Comp->IsActive() && !bHasInstance)
		{
			continue;
		}

		TSharedPtr<FJsonObject> CompJson = MakeShared<FJsonObject>();
		CompJson->SetStringField(TEXT("component"), Comp->GetName());
		CompJson->SetStringField(TEXT("component_path"), Comp->GetPathName());
		CompJson->SetStringField(TEXT("owner_actor"), Owner ? Owner->GetActorLabel() : FString());
		CompJson->SetStringField(TEXT("system"), Asset ? Asset->GetPathName() : FString());
		CompJson->SetStringField(TEXT("world"), WorldKind);

		// What the driving code pushed into the component's exposed (User.) parameters.
		// Also captures the optional VisibleEdgeZ float for the visible-edge invariant below.
		TOptional<float> VisibleEdgeZ;
		CompJson->SetObjectField(TEXT("user_parameters"),
			DumpUserParameters(Comp->GetOverrideParameters(), VisibleEdgeZ));

		TArray<TSharedPtr<FJsonValue>> EmittersJson;

		if (!bHasInstance)
		{
			// Fail soft: component is active but its simulation isn't materialized (pooled,
			// scalability-culled, or mid-(re)activation).
			CompJson->SetStringField(TEXT("note"), TEXT("no live system instance (pooled/culled/not yet activated) — no particle data to read"));
			CompJson->SetArrayField(TEXT("emitters"), EmittersJson);
			ComponentsJson.Add(MakeShared<FJsonValueObject>(CompJson));
			continue;
		}

		FNiagaraSystemInstance* Inst = Controller->GetSystemInstance_Unsafe();
		if (Inst == nullptr)
		{
			CompJson->SetStringField(TEXT("note"), TEXT("system instance unavailable — skipped"));
			CompJson->SetArrayField(TEXT("emitters"), EmittersJson);
			ComponentsJson.Add(MakeShared<FJsonValueObject>(CompJson));
			continue;
		}

		// Same guard the engine's debug HUD takes before reading particle buffers on the
		// game thread: block until any in-flight concurrent tick has finalized.
		Inst->WaitForConcurrentTickAndFinalize();

		const FTransform ComponentToWorld = Comp->GetComponentToWorld();

		int32 EmitterIndex = -1;
		for (const FNiagaraEmitterInstanceRef& EmitterRef : Inst->GetEmitters())
		{
			++EmitterIndex;
			FNiagaraEmitterInstance& EmitterInst = EmitterRef.Get();

			const FString EmitterName = EmitterInst.GetEmitterHandle().GetName().ToString();
			if (!EmitterFilter.IsEmpty() && !EmitterName.Equals(EmitterFilter, ESearchCase::IgnoreCase))
			{
				continue;
			}

			const bool bLocalSpace = EmitterInst.IsLocalSpace();

			TSharedPtr<FJsonObject> EmitterJson = MakeShared<FJsonObject>();
			EmitterJson->SetStringField(TEXT("name"), EmitterName);
			EmitterJson->SetBoolField(TEXT("local_space"), bLocalSpace);
			if (EmitterInst.AsStateless() != nullptr)
			{
				EmitterJson->SetBoolField(TEXT("stateless"), true);
			}

			// GPU-sim: buffers live on the GPU — report and skip (spec).
			if (EmitterInst.GetSimTarget() == ENiagaraSimTarget::GPUComputeSim)
			{
				EmitterJson->SetStringField(TEXT("sim_target"), TEXT("GPU"));
				EmitterJson->SetBoolField(TEXT("skipped"), true);
				EmitterJson->SetNumberField(TEXT("num_particles"), EmitterInst.GetNumParticles());
				EmitterJson->SetStringField(TEXT("note"), TEXT("GPU-sim emitter: no CPU readback. Switch the emitter to CPUSim to dump particles."));
				EmittersJson.Add(MakeShared<FJsonValueObject>(EmitterJson));
				continue;
			}
			EmitterJson->SetStringField(TEXT("sim_target"), TEXT("CPU"));

			const FNiagaraDataSet& DataSet = EmitterInst.GetParticleData();
			const FNiagaraDataBuffer* Buffer = DataSet.GetCurrentData();
			const int32 NumInstances = Buffer ? static_cast<int32>(Buffer->GetNumInstances()) : 0;
			EmitterJson->SetNumberField(TEXT("num_particles"), NumInstances);

			// Never-simulated (e.g. disabled) emitters have no current buffer and may have an
			// uninitialized data set — GetCompiledData() would assert. Nothing to read anyway.
			if (NumInstances == 0)
			{
				EmitterJson->SetNumberField(TEXT("sampled"), 0);
				EmitterJson->SetArrayField(TEXT("particles"), TArray<TSharedPtr<FJsonValue>>());
				EmittersJson.Add(MakeShared<FJsonValueObject>(EmitterJson));
				continue;
			}

			// Resolve requested attributes against this emitter's layout.
			TArray<FAttrReader> Readers;
			TArray<TSharedPtr<FJsonValue>> NotFoundJson;
			for (const FString& Requested : RequestedAttributes)
			{
				FAttrReader Reader;
				FString Note;
				if (ResolveAttribute(DataSet, Requested, Reader, Note))
				{
					Readers.Add(MoveTemp(Reader));
				}
				else
				{
					NotFoundJson.Add(MakeShared<FJsonValueString>(Note));
				}
			}
			if (NotFoundJson.Num() > 0)
			{
				EmitterJson->SetArrayField(TEXT("attributes_not_found"), NotFoundJson);

				// Discovery aid: list what IS in the buffer so the next call can succeed.
				TArray<TSharedPtr<FJsonValue>> AvailableJson;
				for (const FNiagaraVariableBase& Var : DataSet.GetCompiledData().Variables)
				{
					TSharedPtr<FJsonObject> VarJson = MakeShared<FJsonObject>();
					VarJson->SetStringField(TEXT("name"), Var.GetName().ToString());
					VarJson->SetStringField(TEXT("type"), Var.GetType().GetName());
					AvailableJson.Add(MakeShared<FJsonValueObject>(VarJson));
				}
				EmitterJson->SetArrayField(TEXT("available_attributes"), AvailableJson);
			}

			// Sim->world for this emitter's space (LWC tile handled by the engine).
			const FTransform SimToWorld = Inst->GetLWCSimToWorld(bLocalSpace);
			const FColor DrawColor = PaletteColor(EmitterIndex);

			// Visible-edge invariant (fully generic — activates ONLY when the component carries a
			// float user parameter named VisibleEdgeZ = the material-accurate reveal-edge height in
			// emitter-local Z). Local-space emitters then get per-particle z_beyond_visible_edge
			// plus this summary, computed over the FULL buffer (all num_particles), not just the
			// sampled set. "Arrivals" = particles with NormalizedAge >= 0.75.
			const bool bEdgeInvariant = VisibleEdgeZ.IsSet() && bLocalSpace;
			const float EdgeZ = VisibleEdgeZ.Get(0.0f);
			if (bEdgeInvariant)
			{
				TSharedPtr<FJsonObject> EdgeJson = MakeShared<FJsonObject>();
				EdgeJson->SetNumberField(TEXT("visible_edge_z"), EdgeZ);

				const FNiagaraDataSetReaderFloat<FNiagaraPosition> EdgePosReader =
					FNiagaraDataSetAccessor<FNiagaraPosition>::CreateReader(DataSet, FName(TEXT("Position")));
				if (!EdgePosReader.IsValid())
				{
					EdgeJson->SetStringField(TEXT("note"), TEXT("Position attribute not readable — invariant not computed"));
				}
				else
				{
					const FNiagaraDataSetReaderFloat<float> AgeReader =
						FNiagaraDataSetAccessor<float>::CreateReader(DataSet, FName(TEXT("NormalizedAge")));

					float MaxZBeyond = -FLT_MAX;
					int32 CountBeyond3cm = 0;
					int32 ArrivalsBeyond3cm = 0;
					for (int32 Instance = 0; Instance < NumInstances; ++Instance)
					{
						const float ZBeyond = EdgePosReader.GetSafe(Instance, FNiagaraPosition(ForceInit)).Z - EdgeZ;
						MaxZBeyond = FMath::Max(MaxZBeyond, ZBeyond);
						if (ZBeyond > 3.0f)
						{
							++CountBeyond3cm;
							if (AgeReader.IsValid() && AgeReader.GetSafe(Instance, 0.0f) >= 0.75f)
							{
								++ArrivalsBeyond3cm;
							}
						}
					}
					EdgeJson->SetNumberField(TEXT("max_z_beyond"), MaxZBeyond);
					EdgeJson->SetNumberField(TEXT("count_beyond_3cm"), CountBeyond3cm);
					if (AgeReader.IsValid())
					{
						EdgeJson->SetNumberField(TEXT("arrivals_beyond_3cm"), ArrivalsBeyond3cm);
					}
					else
					{
						EdgeJson->SetStringField(TEXT("note"), TEXT("NormalizedAge not present on this emitter — arrivals_beyond_3cm omitted"));
					}
				}
				EmitterJson->SetObjectField(TEXT("visible_edge"), EdgeJson);
			}

			// Even sampling across the buffer, not just the first N.
			const int32 SampleCount = FMath::Min(NumInstances, MaxParticles);
			const double Step = SampleCount > 0 ? static_cast<double>(NumInstances) / SampleCount : 0.0;

			TArray<TSharedPtr<FJsonValue>> ParticlesJson;
			ParticlesJson.Reserve(SampleCount);
			for (int32 Sample = 0; Sample < SampleCount; ++Sample)
			{
				const int32 Index = FMath::Clamp(static_cast<int32>(Sample * Step), 0, NumInstances - 1);

				TSharedPtr<FJsonObject> ParticleJson = MakeShared<FJsonObject>();
				ParticleJson->SetNumberField(TEXT("index"), Index);

				for (const FAttrReader& Reader : Readers)
				{
					switch (Reader.Kind)
					{
						case EAttrKind::Float:
							ParticleJson->SetNumberField(Reader.OutputKey, Reader.FloatReader.GetSafe(Index, 0.0f));
							break;
						case EAttrKind::Int32:
							ParticleJson->SetNumberField(Reader.OutputKey, Reader.IntReader.GetSafe(Index, 0));
							break;
						case EAttrKind::Bool:
							ParticleJson->SetBoolField(Reader.OutputKey, Reader.BoolReader.GetSafe(Index, FNiagaraBool(false)).GetValue());
							break;
						case EAttrKind::Vec2:
							ParticleJson->SetField(Reader.OutputKey, Vec2Json(Reader.Vec2Reader.GetSafe(Index, FVector2f::ZeroVector)));
							break;
						case EAttrKind::Vec3:
							ParticleJson->SetField(Reader.OutputKey, Vec3Json(Reader.Vec3Reader.GetSafe(Index, FVector3f::ZeroVector)));
							break;
						case EAttrKind::Vec4:
							ParticleJson->SetField(Reader.OutputKey, Vec4Json(Reader.Vec4Reader.GetSafe(Index, FVector4f(0, 0, 0, 0))));
							break;
						case EAttrKind::Color:
							ParticleJson->SetField(Reader.OutputKey, ColorJson(Reader.ColorReader.GetSafe(Index, FLinearColor::White)));
							break;
						case EAttrKind::Quat:
						{
							const FQuat4f Q = Reader.QuatReader.GetSafe(Index, FQuat4f::Identity);
							ParticleJson->SetField(Reader.OutputKey, Vec4Json(FVector4f(Q.X, Q.Y, Q.Z, Q.W)));
							break;
						}
						case EAttrKind::NiagaraID:
						{
							const FNiagaraID Id = Reader.IDReader.GetSafe(Index, FNiagaraID());
							TSharedPtr<FJsonObject> IdJson = MakeShared<FJsonObject>();
							IdJson->SetNumberField(TEXT("index"), Id.Index);
							IdJson->SetNumberField(TEXT("acquire_tag"), Id.AcquireTag);
							ParticleJson->SetObjectField(Reader.OutputKey, IdJson);
							break;
						}
						case EAttrKind::Position:
						{
							const FNiagaraPosition SimPos = Reader.PositionReader.GetSafe(Index, FNiagaraPosition(ForceInit));
							const FVector WorldPos = SimToWorld.TransformPosition(FVector(SimPos));
							// local: raw sim value for local-space emitters; component-space
							// back-computation for world-space emitters (spec symmetry).
							const FVector LocalPos = bLocalSpace
								? FVector(SimPos)
								: ComponentToWorld.InverseTransformPosition(WorldPos);

							if (Reader.bIsCanonicalPosition)
							{
								ParticleJson->SetField(TEXT("local_position"), DVec3Json(LocalPos));
								ParticleJson->SetField(TEXT("world_position"), DVec3Json(WorldPos));

								if (bEdgeInvariant)
								{
									ParticleJson->SetNumberField(TEXT("z_beyond_visible_edge"), LocalPos.Z - EdgeZ);
								}

								if (bDraw)
								{
									DrawDebugPoint(TargetWorld, WorldPos, 6.0f, DrawColor,
										/*bPersistentLines*/ false, static_cast<float>(DrawDuration));
									bDrewAnything = true;
								}
							}
							else
							{
								// Other Position-typed attributes get both spaces too, nested.
								TSharedPtr<FJsonObject> PosJson = MakeShared<FJsonObject>();
								PosJson->SetField(TEXT("local"), DVec3Json(LocalPos));
								PosJson->SetField(TEXT("world"), DVec3Json(WorldPos));
								ParticleJson->SetObjectField(Reader.OutputKey, PosJson);
							}
							break;
						}
					}
				}

				ParticlesJson.Add(MakeShared<FJsonValueObject>(ParticleJson));
			}

			EmitterJson->SetNumberField(TEXT("sampled"), ParticlesJson.Num());
			EmitterJson->SetArrayField(TEXT("particles"), ParticlesJson);
			EmittersJson.Add(MakeShared<FJsonValueObject>(EmitterJson));
		}

		CompJson->SetArrayField(TEXT("emitters"), EmittersJson);
		ComponentsJson.Add(MakeShared<FJsonValueObject>(CompJson));
	}

	UE_LOG(LogMonolithNiagaraDebug, Verbose, TEXT("dump_particles: %d component(s) matched in %s world '%s'%s"),
		ComponentsJson.Num(), *WorldKind, *TargetWorld->GetName(),
		bDrewAnything ? TEXT(" (debug points drawn)") : TEXT(""));

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("world"), WorldKind);
	Result->SetStringField(TEXT("world_name"), TargetWorld->GetName());
	Result->SetNumberField(TEXT("component_count"), ComponentsJson.Num());
	Result->SetArrayField(TEXT("components"), ComponentsJson);
	Result->SetBoolField(TEXT("drawn"), bDrewAnything);

	if (ComponentsJson.Num() == 0)
	{
		Result->SetStringField(TEXT("note"),
			TEXT("No matching live NiagaraComponents. Check filters, and note that components in preview scenes / other worlds are excluded — only the active PIE world (when playing) or the editor world is searched."));
	}

	return FMonolithActionResult::Success(Result);
}
