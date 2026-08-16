#pragma once

#include "CoreMinimal.h"
#include "IO/IoHash.h"
#include "SQLiteDatabase.h"
#include "MonolithIndexLog.h"

struct FIndexedAsset
{
	int64 Id = 0;
	FString PackagePath;
	FString AssetName;
	FString AssetClass;
	FString ModuleName;
	FString Description;
	int64 FileSizeBytes = 0;
	FString LastModified;
	FString SavedHash;
	FString IndexedAt;
};

struct FIndexedNode
{
	int64 Id = 0;
	int64 AssetId = 0;
	FString NodeType;
	FString NodeName;   // display TITLE - not a join key; see CallTarget
	FString NodeClass;
	FString Properties; // JSON blob
	int32 PosX = 0;
	int32 PosY = 0;
	FString GraphName;      // owning graph, so a hit is addressable
	FString NodeObjectName; // UEdGraphNode::GetName() - the id blueprint actions accept
	FString CallTarget;     // called function name for call nodes; drives the caller lookup
};

/** One key -> InputAction mapping harvested from a UInputMappingContext. */
struct FIndexedInputBinding
{
	int64 Id = 0;
	int64 ContextAssetId = 0;
	FString KeyName;
	FString ActionName;
	FString ActionPath;
	FString Triggers;
};

/** A non-empty literal wired into an input pin (string, text or object - all flattened). */
struct FIndexedPinDefault
{
	int64 NodeId = 0;
	FString PinName;
	FString ValueKind; // string | text | object
	FString ValueText;
};

struct FIndexedConnection
{
	int64 Id = 0;
	int64 SourceNodeId = 0;
	FString SourcePin;
	int64 TargetNodeId = 0;
	FString TargetPin;
	FString PinType;
};

struct FIndexedVariable
{
	int64 Id = 0;
	int64 AssetId = 0;
	FString VarName;
	FString VarType;
	FString Category;
	FString DefaultValue;
	bool bIsExposed = false;
	bool bIsReplicated = false;
};

struct FIndexedParameter
{
	int64 Id = 0;
	int64 AssetId = 0;
	FString ParamName;
	FString ParamType;
	FString ParamGroup;
	FString DefaultValue;
	FString Source; // "Material", "Niagara", etc.
};

struct FIndexedDependency
{
	int64 Id = 0;
	int64 SourceAssetId = 0;
	int64 TargetAssetId = 0;
	FString DependencyType; // "Hard", "Soft", "Searchable"
};

struct FIndexedActor
{
	int64 Id = 0;
	int64 AssetId = 0; // Level asset
	FString ActorName;
	FString ActorClass;
	FString ActorLabel;
	FString Transform; // JSON
	FString Components; // JSON array
};

struct FIndexedTag
{
	int64 Id = 0;
	FString TagName;
	FString ParentTag;
	int32 ReferenceCount = 0;
};

struct FIndexedTagReference
{
	int64 Id = 0;
	int64 TagId = 0;
	int64 AssetId = 0;
	FString Context; // "Variable", "Node", "Component", etc.
};

struct FIndexedConfig
{
	int64 Id = 0;
	FString FilePath;
	FString Section;
	FString Key;
	FString Value;
};

struct FIndexedCppSymbol
{
	int64 Id = 0;
	FString FilePath;
	FString SymbolName;
	FString SymbolType; // "Class", "Function", "Enum", "Struct", "Delegate"
	FString Signature;
	int32 LineNumber = 0;
	FString ParentSymbol;
};

struct FIndexedDataTableRow
{
	int64 Id = 0;
	int64 AssetId = 0;
	FString RowName;
	FString RowData; // JSON blob
};

struct FSearchResult
{
	FString AssetPath;
	FString AssetName;
	FString AssetClass;
	FString ModuleName;
	FString MatchContext; // snippet around the match
	float Rank = 0.0f;
	// Node-level hits carry their address so a result is directly actionable: without these,
	// "asset X mentions Y" always needed a second round-trip to locate the actual node.
	FString GraphName;
	FString NodeObjectName;
	FString NodeClass;
};

/** One row of "who calls this function" - a call site with its address. */
struct FCallSite
{
	FString AssetPath;
	FString AssetName;
	FString GraphName;
	FString NodeObjectName;
	FString NodeTitle;
	FString CallTarget;
};

/** key -> action -> the Blueprint graph node that handles it. */
struct FInputHandlerResult
{
	FString KeyName;
	FString ActionName;
	FString ActionPath;
	FString ContextAssetPath; // the InputMappingContext the binding came from
	FString HandlerAssetPath; // Blueprint containing the input event node (empty if none found)
	FString HandlerGraphName;
	FString HandlerNodeObjectName;
	FString HandlerNodeTitle;
};

/**
 * RAII wrapper around FSQLiteDatabase for the Monolith project index.
 * Creates all tables on first open, provides typed insert/query helpers.
 * Thread-safe for reads; writes should be serialized by the caller.
 */
class MONOLITHINDEX_API FMonolithIndexDatabase
{
public:
	FMonolithIndexDatabase();
	~FMonolithIndexDatabase();

	/** Open (or create) the database at the given path */
	bool Open(const FString& InDbPath);

	/** Close the database */
	void Close();

	/** Is the database currently open? */
	bool IsOpen() const;

	/** Wipe all data and recreate tables (for full re-index) */
	bool ResetDatabase();

	/** Direct access to the underlying SQLite database */
	FSQLiteDatabase* GetRawDatabase() const { return Database; }

	// --- Transaction helpers ---
	bool BeginTransaction();
	bool CommitTransaction();
	bool RollbackTransaction();

	// --- Asset CRUD ---
	int64 InsertAsset(const FIndexedAsset& Asset);
	TOptional<FIndexedAsset> GetAssetByPath(const FString& PackagePath);
	int64 GetAssetId(const FString& PackagePath);
	bool DeleteAssetAndRelated(int64 AssetId);

	// --- Node CRUD ---
	int64 InsertNode(const FIndexedNode& Node);
	TArray<FIndexedNode> GetNodesForAsset(int64 AssetId);

	// --- Connection CRUD ---
	int64 InsertConnection(const FIndexedConnection& Conn);

	/** Insert one key->action mapping harvested from an InputMappingContext. */
	int64 InsertInputBinding(const FIndexedInputBinding& Binding);

	/** Insert one non-empty pin literal. Node must already be inserted. */
	int64 InsertPinDefault(const FIndexedPinDefault& PinDefault);
	TArray<FIndexedConnection> GetConnectionsForAsset(int64 AssetId);

	// --- Variable CRUD ---
	int64 InsertVariable(const FIndexedVariable& Var);
	TArray<FIndexedVariable> GetVariablesForAsset(int64 AssetId);

	// --- Parameter CRUD ---
	int64 InsertParameter(const FIndexedParameter& Param);

	// --- Dependency CRUD ---
	int64 InsertDependency(const FIndexedDependency& Dep);
	TArray<FIndexedDependency> GetDependenciesForAsset(int64 AssetId);
	TArray<FIndexedDependency> GetReferencersOfAsset(int64 AssetId);

	// --- Actor CRUD ---
	int64 InsertActor(const FIndexedActor& Actor);

	// --- Tag CRUD ---
	int64 InsertTag(const FIndexedTag& Tag);
	int64 GetOrCreateTag(const FString& TagName, const FString& ParentTag);
	int64 InsertTagReference(const FIndexedTagReference& Ref);

	// --- Meta ---
	bool WriteMeta(const FString& Key, const FString& Value);
	FString ReadMeta(const FString& Key) const;

	// --- Config CRUD ---
	int64 InsertConfig(const FIndexedConfig& Config);

	// --- C++ Symbol CRUD ---
	int64 InsertCppSymbol(const FIndexedCppSymbol& Symbol);

	// --- DataTable Row CRUD ---
	int64 InsertDataTableRow(const FIndexedDataTableRow& Row);

	// --- FTS5 Search ---
	TArray<FSearchResult> FullTextSearch(const FString& Query, int32 Limit = 50);

	/**
	 * Every call site of FunctionName. Indexed on nodes.call_target, so this is a direct lookup
	 * rather than a project-wide text crawl - and it answers the question that actually matters
	 * when tracing behaviour ("what runs this?") instead of "what mentions this word".
	 */
	TArray<FCallSite> FindCallers(const FString& FunctionName, int32 Limit = 50);

	/**
	 * What a key runs: the binding(s) for KeyName and, where one exists, the Blueprint input-event
	 * node handling that action. KeyName is matched case-insensitively ("e" == "E").
	 */
	TArray<FInputHandlerResult> FindInputHandlers(const FString& KeyName, int32 Limit = 50);

	// --- Stats ---
	TSharedPtr<FJsonObject> GetStats();

	// --- Asset details (all related data) ---
	TSharedPtr<FJsonObject> GetAssetDetails(const FString& PackagePath);

	// --- Find by type ---
	TArray<FIndexedAsset> FindByType(const FString& AssetClass, int32 Limit = 100, int32 Offset = 0);

	// --- Find references (bidirectional) ---
	TSharedPtr<FJsonObject> FindReferences(const FString& PackagePath);

	// --- Incremental indexing helpers ---
	TArray<FString> GetAllIndexedPaths();
	FString GetSavedHash(const FString& PackagePath);
	TMap<FString, FString> GetAllPathsAndHashes();
	bool DeleteAssetByPath(const FString& PackagePath);
	bool UpdateAssetPath(const FString& OldPath, const FString& NewPath, const FString& NewAssetName = FString());
	bool UpdateAssetMetadata(const FIndexedAsset& Asset);
	bool DeleteChildDataForAsset(int64 AssetId);
	bool UpdateSavedHash(const FString& PackagePath, const FString& HashHex);

private:
	bool CreateTables();
	bool ExecuteSQL(const FString& SQL);
	FSQLiteDatabase* Database = nullptr;
	FString DbPath;
};
