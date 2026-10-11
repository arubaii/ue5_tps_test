// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UMaterialExpression;

/**
 * Material graphs a model writes, made reliable before anything is built: what build_ui_material and validate_material run.
 *
 * Prepare takes a graph in whatever form a model wrote it (nodes as a list or a map, wires as inputs or as a connections
 * list, constants and parameters written straight into inputs), expands macro nodes (Resources/AI/MaterialMacros.json:
 * soft_circle, glow, linear_gradient... - tested subgraphs), then checks it against the node catalog
 * (Resources/AI/MaterialNodes.json) and the engine's own reflection - node types, pin and output names, required inputs,
 * loops, value widths (float1-4) on every wire, what a UI material may output - and returns every problem at once, each
 * tied to the graph's node id with what to do about it.
 *
 * Mechanical mistakes are repaired instead of refused, and reported as "fixed": names a model often uses (Lerp, Mul,
 * Edge0, Alpha...), a float4 meeting a float3 (masked to RGB), a float2 where a float3 is needed (appended with 0), a
 * color going into Opacity, Opacity not kept in 0..1, unused nodes, a Noise with no position in a UI material, and the
 * widget tint / particle color (VertexColor) multiplied in when the graph doesn't use it.
 *
 * The prepared graph is what NameTBDAILibrary builds - node by node, wired by the engine's own pin indices.
 */
namespace NameTBDAIMaterialGraph
{
	struct FProblem
	{
		/** For the stats: unknown_node, unknown_pin, unknown_output, missing_node, missing_input, cycle, width, output, param, prop, ui, function, custom, json, compile. */
		FString Code;

		/** The graph's node id ("" for the graph itself). Nodes made from a macro read "g/d": node d of macro g. */
		FString Node;

		FString Message;
		bool bError = true;
	};

	struct FWire
	{
		/** The pin's engine name (or the catalog name, which is matched to the engine's when built). */
		FString Pin;

		/** Other names the pin may go by in this engine (catalog aliases), tried when building. */
		TArray<FString> PinNames;

		int32 From = INDEX_NONE;

		/** The source's output by name: "" = its first, "A", "RGB", "Result"... */
		FString FromOutput;
	};

	struct FNode
	{
		FString Id;

		/** The catalog's name (LinearInterpolate), or the expression class's short name for nodes outside the catalog. */
		FString Type;

		UClass* Class = nullptr;
		TSharedPtr<FJsonObject> Props;

		/** MaterialFunctionCall: the library function's name or an asset path. */
		FString Function;

		/** Custom: its HLSL, output width (1-4) and input names in order. */
		FString Code;
		int32 CustomWidth = 3;
		TArray<FString> CustomInputs;

		/** Custom: shader files its code includes (HelpersInclude when it uses the NTB_ helpers). */
		TArray<FString> Includes;

		/** FunctionInput (function graphs): the input's name and width. */
		FString InputName;
		int32 InputWidth = 0;

		TArray<FWire> Wires;

		/** The macro node this came from, if any. */
		FString Macro;

		/** Width of its first output: 1-4, 0 when it can't be known. */
		int32 Width = 0;

		/** False for nodes nothing uses (not built). */
		bool bLive = true;
	};

	struct FOutput
	{
		/** A material: FinalColor, Opacity, OpacityMask, BaseColor... A function: the output's own name. */
		FString Name;
		int32 From = INDEX_NONE;
		FString FromOutput;
		int32 Width = 0;
	};

	struct FGraph
	{
		bool bFunction = false;
		FString Domain = TEXT("UI");
		FString Blend = TEXT("Translucent");
		TArray<FNode> Nodes;
		TArray<FOutput> Outputs;
		TArray<FProblem> Problems;

		/** The model's slips that were repaired ("Lerp is LinearInterpolate", "a float4 met a float3..."). */
		TArray<FString> Fixed;

		/** What every UI material gets, not a slip: the VertexColor tint, Saturate on Opacity. */
		TArray<FString> Added;

		bool HasErrors() const;
		int32 CountErrors() const;
		int32 Find(const FString& Id) const;

		/** Problems as lines: "node: message". */
		TArray<FString> ErrorLines() const;
		TArray<FString> WarningLines() const;

		/** What the model reads: ok, errors, fixed, added, warnings, node count, parameters, outputs and their widths. */
		TSharedPtr<FJsonObject> Summary() const;
	};

	/** The helpers every Custom node may use (NTB_SmoothStep, NTB_Noise...), as a Custom node's include path. */
	extern const TCHAR* const HelpersInclude;

	/** Whether the engine can find HelpersInclude (the NameTBDShaders module mapped its folder at startup). */
	bool HelpersAvailable();

	/**
	 * A material written as HLSL, as the node graph that runs it: {code, params, blend, tint} - each param a Scalar /
	 * Vector Parameter (a color reaches the code as float4 RGBA, [x, y] as float2), "uv" (float2) and "time" (float) when
	 * the code uses them, one Custom node returning float4(color, alpha) into FinalColor / Opacity. A function
	 * (bFunction): {inputs: [{name, type, preview}], outputs: {Name: {type: float1..4, code}}} - one Custom node per output,
	 * reading the function's inputs by name. code is a string or a list of lines. Null with OutError.
	 */
	TSharedPtr<FJsonObject> HlslToGraph(const TSharedPtr<FJsonObject>& Hlsl, bool bFunction, FString& OutError);

	/** A library definition's graph: its "graph", or its "hlsl" made into one. */
	TSharedPtr<FJsonObject> DefinitionGraph(const TSharedPtr<FJsonObject>& Definition, bool bFunction);

	/** Reads, expands, checks and repairs a material graph (bFunction: a Material Function graph with inputs / outputs). */
	FGraph Prepare(const TSharedPtr<FJsonObject>& Graph, bool bFunction = false);

	/**
	 * A master's graph with a model's additions: its nodes plus Extra's, Extra's outputs replacing the master's, and
	 * "master.FinalColor" / "master.Opacity" in Extra naming what the master's outputs were.
	 */
	TSharedPtr<FJsonObject> MergeWithMaster(const TSharedPtr<FJsonObject>& MasterGraph, const TSharedPtr<FJsonObject>& Extra, FString& OutError);

	// ---- Pins and outputs, by the engine's own names (shared with the builder) -------------------------------------

	/** The input of Expression called any of Names (loosely); a node with one input takes any name. INDEX_NONE if none. */
	int32 FindInputIndex(UMaterialExpression* Expression, const TArray<FString>& Names);

	/** The output of Expression called Name: its own name, or the channels it masks ("R", "RGB", "A"...); "" = the first. */
	int32 FindOutputIndex(UMaterialExpression* Expression, const FString& Name);

	/** "Did you mean": a hint for a compile error's text, from the catalog's hints. Empty if none fits. */
	FString HintFor(const FString& CompileError);

	/** "glow1/d" -> "glow1 (glow, part d)"; other ids as they are. */
	FString DisplayId(const FGraph& Graph, int32 NodeIndex);

	// ---- What the model can look up --------------------------------------------------------------------------------

	/**
	 * list_options material_nodes: the catalog in one line per node and the macros (empty Filter); with a Filter, each
	 * matching node in detail - exact pins (engine names, optional or not, widths), outputs and widths, props; "all":
	 * every expression class the engine has.
	 */
	TSharedPtr<FJsonObject> DescribeNodes(const FString& Filter);
	TSharedPtr<FJsonObject> DescribeMacros();

	// ---- First-try stats -------------------------------------------------------------------------------------------

	/**
	 * Records one validate_material / build_ui_material call by Model (the chat's provider:model, or the MCP client) in
	 * Saved/NameTBDAI/stats.json: materials built, how many worked on the first try (no failed validate / build before
	 * the build that compiled), calls, repairs, and which mistakes each model makes. Graph null: a material instance made
	 * from a master (can't fail). bBuildCall: a build (it ends the attempt when it compiled).
	 */
	void RecordAttempt(const FString& Model, const FGraph* Graph, bool bBuildCall, bool bCompiled, int32 CompileErrors);
}
