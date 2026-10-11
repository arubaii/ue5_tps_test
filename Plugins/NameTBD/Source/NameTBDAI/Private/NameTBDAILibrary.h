// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "NameTBDAIMaterialGraph.h"

class UMaterial;
class UMaterialFunction;
class UMaterialInterface;
class UNiagaraSystem;
struct FNameTBDAIToolContext;

/**
 * The library the AI builds with: master UI materials (aurora, soft glow, shimmer, gradient, noise fog, vignette, ring
 * pulse, dissolve, soft dot), Material Functions (soft circle, rounded-rect SDF, panning noise, color ramp) and Niagara UI
 * templates (rising bubbles, sparkles, confetti, embers, glow ring, trail, rising sparkle loop).
 *
 * Each is defined as data in the plugin's Resources/AI (Materials/*.json - node graphs; Niagara/Templates.json - recipes),
 * and built into a real asset the first time it is needed: looked for first in the plugin's own content (/NameTBD/AI/...,
 * where a release can ship them built), then in the project's Library Folder, else built there. Materials and functions are
 * checked and built node by node (the graph format build_ui_material takes, through NameTBDAIMaterialGraph), so the
 * shipped definitions and the AI's own materials go through one path.
 */
namespace NameTBDAILibrary
{
	/** Names and one-line descriptions, with each master's parameters. */
	TSharedPtr<FJsonObject> DescribeMasters();
	TSharedPtr<FJsonObject> DescribeFunctions();
	TSharedPtr<FJsonObject> DescribeNiagaraTemplates();

	/** A library definition's JSON (Resources/AI/Materials/<Name>.json) by name; Kind "master" or "function". Null if none. */
	TSharedPtr<FJsonObject> FindDefinitionJson(const FString& Name, const FString& Kind);

	/** A library master's node graph (to read, or to extend with build_ui_material's from_master). Null if none. */
	TSharedPtr<FJsonObject> GetMasterGraph(const FString& Name);

	/** One master in full: what it is, its parameters and its graph. */
	TSharedPtr<FJsonObject> DescribeMaster(const FString& Name);

	/** A master material by name ("Aurora", "M_UI_Aurora"), built if needed. Null with OutError. */
	UMaterialInterface* GetMaster(const FString& Name, FString& OutError);

	/** A library Material Function by name, built if needed. */
	UMaterialFunction* GetFunction(const FString& Name, FString& OutError);

	/** A Niagara UI template system by name, built if needed. */
	UNiagaraSystem* GetNiagaraTemplate(const FString& Name, FString& OutError, TArray<FString>& OutNotes);

	/** The template's recipe from Templates.json, or null. */
	TSharedPtr<FJsonObject> FindNiagaraRecipe(const FString& Name);

	/**
	 * Builds every library asset that isn't there yet, or (a project's copy) was built from an older definition - Material
	 * Functions first, then masters, then Niagara templates. bForce rebuilds every project copy. One line per asset built,
	 * rebuilt or failed; each also goes to the Output Log. Run by the console commands NameTBDAI.BuildLibrary /
	 * NameTBDAI.RebuildLibrary and Editor Preferences' Build Library Now.
	 */
	void BuildAll(TArray<FString>& OutReport, bool bForce = false);

	/**
	 * Builds a UI material from a graph (build_ui_material's format) into Material, which is emptied first: the graph is
	 * checked and repaired first (NameTBDAIMaterialGraph::Prepare) and not built when it has errors. Graph problems and
	 * compile errors (tied to the graph's node ids, with hints) go to OutProblems; true when it compiled.
	 */
	bool BuildMaterialGraph(UMaterial* Material, const TSharedPtr<FJsonObject>& Graph, TArray<FString>& OutProblems);

	/** Builds an already prepared graph (no errors) into Material. OutCompileErrors: how many the compile gave. */
	bool BuildPreparedMaterial(UMaterial* Material, const NameTBDAIMaterialGraph::FGraph& Graph, TArray<FString>& OutProblems, int32& OutCompileErrors);

	/** The same for a Material Function: "inputs" / "outputs" become Function Input / Output nodes. */
	bool BuildFunctionGraph(UMaterialFunction* Function, const TSharedPtr<FJsonObject>& Graph, TArray<FString>& OutProblems);

	/** Writes module constants ("SpawnRate.SpawnRate": 12) into a system's scripts; notes which took. True if any did. */
	bool ApplyNiagaraConstants(UNiagaraSystem* System, const TSharedPtr<FJsonObject>& Constants, TArray<FString>& OutNotes);

	/** CPU simulation, local space, Material on sprite renderers that don't draw in UI. */
	void MakeUIReady(UNiagaraSystem* System, UMaterialInterface* Material, TArray<FString>& OutNotes);

	/** The UI soft-dot material particles draw with when theirs isn't a UI material. */
	UMaterialInterface* GetSoftDotMaterial(FString& OutError);
}
