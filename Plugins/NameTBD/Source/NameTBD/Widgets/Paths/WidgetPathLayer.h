// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "UObject/Interface.h"
#include "UObject/ObjectKey.h"
#include "Widgets/SLeafWidget.h"
#include "NameTBD/Widgets/WidgetMotion.h"
#include "NameTBD/Widgets/WidgetSVGBrush.h"
#include "NameTBD/Widgets/Paths/WidgetPathTypes.h"
#include "NameTBD/Widgets/Shapes/WidgetShapeGeometry.h"

#include "WidgetPathLayer.generated.h"

UINTERFACE(BlueprintType, meta = (DisplayName = "Widget Path Node"))
class NAMETBD_API UWidgetPathNode : public UInterface
{
	GENERATED_BODY()
};

/**
 * Optional, for a Widget Path Layer's nodes: implement it on a node's Widget Blueprint (Class Settings > Interfaces) and the
 * layer reads each node's state from the node itself (with Read States From Nodes on), so a skill node that knows whether it
 * is unlocked keeps its lines up to date with nothing else to call.
 */
class NAMETBD_API IWidgetPathNode
{
	GENERATED_BODY()

public:
	/**
	 * The node's state: Locked, Unlocked, Available or Unavailable - or a Custom State of the game's own (it wins over State).
	 * Return false to let the layer decide (Set Node State, Nodes, Default Node State).
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Widget Path")
	bool GetWidgetPathNodeState(EWidgetPathState& State, FName& CustomState) const;
};

/** How the Slate layer reads a node's outline. */
struct FWidgetPathNodeShapeInfo
{
	FVector4f Radii = FVector4f(0.f, 0.f, 0.f, 0.f);
	bool bPill = false;
	bool bEllipse = false;
	bool bPoint = false;
};

/** One line as the Slate layer draws it, resolved by the UMG layer. */
struct FWidgetPathSlateEdge
{
	/** The same for the same two nodes from one sync to the next - what a change of look is matched by. */
	uint64 Key = 0;

	/**
	 * The nodes' widgets: their Slate widgets are found from them whenever they change - a node built after the layer (it comes
	 * later in its panel), a node rebuilt (the designer's compile) - so a line never waits on its next sync to be drawn.
	 */
	TWeakObjectPtr<UWidget> FromWidget;
	TWeakObjectPtr<UWidget> ToWidget;
	TWeakObjectPtr<UWidget> OrbitCenterWidget;

	TWeakPtr<SWidget> From;
	TWeakPtr<SWidget> To;
	FWidgetPathNodeShapeInfo FromShape;
	FWidgetPathNodeShapeInfo ToShape;
	FWidgetPathRouting Routing;
	TWeakPtr<SWidget> OrbitCenter;

	/** Indices into the layer's looks; INDEX_NONE draws nothing. */
	int32 Look = INDEX_NONE;
	int32 ProgressLook = INDEX_NONE;
	float Progress = 0.f;

	/** A change of look draws on from the From end (else from the To end): from the node that was reached first. */
	bool bChangeFromStart = true;

	/** How many steps from the tree's roots it is - its place in the intro. */
	int32 Depth = 0;

	/** What its materials are told (PathHighlighted, PathHovered, PathSelected, PathRandom). */
	bool bHighlighted = false;
	bool bHovered = false;
	bool bSelected = false;
	float Random = 0.f;
};

/** How the Slate layer animates. */
struct FWidgetPathSlateAnimation
{
	EWidgetPathTransition Transition = EWidgetPathTransition::DrawOn;
	float TransitionDuration = 0.6f;
	EWidgetMotion Motion = EWidgetMotion::Smooth;
	EWidgetPathIntro Intro = EWidgetPathIntro::None;
	float IntroDuration = 0.5f;
	float IntroStagger = 0.08f;
};

/**
 * The Slate side of a Widget Path Layer: draws the lines between the Slate widgets of its nodes, measured where they were last
 * painted, in the layer's own space - so a node moved by layout, a Widget Animation, a constraint or a Flow List takes its
 * lines with it. Lines are built with the shapes' stroke builder and cached until a node, the routing or the look moves.
 */
class NAMETBD_API SWidgetPathLayer : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SWidgetPathLayer) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetLooks(const TArray<FWidgetPathLook>& InLooks);

	/** The lines to draw. bAnimate: a line whose look or progress changed moves to it as Animation says; else at once. */
	void SetEdges(const TArray<FWidgetPathSlateEdge>& InEdges, bool bAnimate);

	void SetAnimation(const FWidgetPathSlateAnimation& InAnimation);

	/** Plays the intro (from nothing) on every line. */
	void PlayIntro();

	/** Called every frame the layer ticks (runtime only): the UMG layer's hover and state polling. */
	void SetTickCallback(TFunction<void(float)> InCallback);

	/** A line's route as last drawn, in this layer's local space, from its From end. False when it isn't drawn. */
	bool GetRoute(uint64 Key, TArray<FVector2f>& OutPoints) const;

	/** At design time nothing is polled, and the intro isn't played on its own. */
	void SetDesignTime(bool bInDesignTime) { bDesignTime = bInDesignTime; }

	/** Finds the nodes' Slate widgets again from their widgets (any built or rebuilt since). True when any changed. */
	bool ResolveNodes() const;

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

protected:
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override { return FVector2D::ZeroVector; }

private:
	struct FMeshCache
	{
		int32 Slot = INDEX_NONE;
		uint32 Hash = 0;
		FWidgetShapeMesh Mesh;
	};

	struct FEdgeState
	{
		FWidgetPathSlateEdge Edge;
		int32 ShownLook = INDEX_NONE;
		int32 PreviousLook = INDEX_NONE;
		FWidgetMotionValue Change;
		bool bChangeFromStart = true;
		FWidgetMotionValue ShownProgress;
		FWidgetMotionValue Intro;
		float IntroDelay = 0.f;
		bool bIntroWaiting = false;

		/** The route, resampled, and what it was built from. */
		uint32 RouteHash = 0;
		TArray<FVector2f> Points;
		float Length = 0.f;

		TArray<FMeshCache> Caches;
	};

	/** One stretch of one look drawn along a line this frame. */
	struct FDrawPiece
	{
		int32 Edge = INDEX_NONE;
		int32 Look = INDEX_NONE;
		float Start = 0.f;
		float End = 1.f;
		float Opacity = 1.f;
		int32 Slot = 0;
	};

	EActiveTimerReturnType OnActiveTick(double InCurrentTime, float InDeltaTime);
	void Wake();
	bool IsAnimating() const;
	uint32 HashNodePlacement() const;

	bool MeasureNode(const TWeakPtr<SWidget>& Node, const FWidgetPathNodeShapeInfo& Shape, const FGeometry& AllottedGeometry, FWidgetPathNodeBox& Out) const;
	void UpdateRoutes(const FGeometry& AllottedGeometry, float Tolerance) const;
	void GatherPieces(TArray<FDrawPiece>& Out) const;

	static FWidgetShapeMesh& GetCache(FEdgeState& State, int32 Slot, uint32 Hash, bool& bOutStale);
	void PaintPiece(const FDrawPiece& Piece, int32 Pass, const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements,
		int32 PassLayer, const FWidgetStyle& InWidgetStyle, float PixelScale, float Tolerance, double Now) const;

	/** The route a look draws along: the line's, or - with a Distortion - moved as it says now. */
	const TArray<FVector2f>& GetLookRoute(const FEdgeState& State, int32 Look, double Now, TArray<FVector2f>& Scratch) const;

	/** A look's extra strokes, under the line (bAbove false) or over it. */
	void PaintStrokes(const FDrawPiece& Piece, bool bAbove, const TArray<FVector2f>& Route, const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements,
		int32 PassLayer, const FWidgetStyle& InWidgetStyle, float PixelScale, double Now) const;

	/** A picture drawn as a quad of Size at Center, its up (local -Y) along Up. */
	void PaintImageQuad(uint64 Key, const FWidgetEffectImage& Image, const FWidgetEffectMaterialInputs& Inputs, const FVector2f& Center, const FVector2f& Up,
		float Size, const FLinearColor& Tint, const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 PassLayer, const FWidgetStyle& InWidgetStyle) const;

	/** The live values a piece's materials are given. */
	void MakeMaterialInputs(const FDrawPiece& Piece, const FWidgetPathLook& Look, float TileLength, FWidgetEffectMaterialInputs& Out) const;

	/** A key for one material instance: the line, the look and what in it. */
	static uint64 MaterialKey(uint64 EdgeKey, int32 Look, int32 Slot);

	TArray<FWidgetPathLook> Looks;
	mutable TArray<FEdgeState> Edges;
	TMap<uint64, int32> EdgeByKey;
	FWidgetPathSlateAnimation Animation;
	TFunction<void(float)> TickCallback;
	bool bDesignTime = false;
	TSharedPtr<FActiveTimerHandle> TickTimer;
	uint32 LastPlacementHash = 0;

	/** The SVGs looks lay along their lines, one drawing per look. */
	mutable TMap<int32, TSharedPtr<FWidgetSVGBrush>> SVGBrushes;

	/** The looks' materials (an instance per line and look and part) and their other pictures. */
	mutable FWidgetEffectMaterials Materials;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnWidgetPathNodeStateChanged, UWidget*, Node, EWidgetPathState, State, FName, CustomState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWidgetPathNodeEvent, UWidget*, Node);

/**
 * Widget Path Layer: draws lines between widgets - a skill tree's links, a talent web, a tech or quest tree, a level map, a
 * flowchart, a circuit, a constellation. Place it in the same Widget Blueprint as the nodes (usually behind them, filling the
 * area they are in), list its Connections (pick From and To from the dropdowns - or add them from Blueprint for trees built
 * from data), and every line follows its nodes wherever they are laid out, animated or moved.
 *
 * Each line takes a Style by its nodes' states through the Rules: Locked lines dim and dashed, a line from an unlocked node to
 * one that can be unlocked glowing, lines between unlocked nodes bright with light flowing along, the route to a hovered or
 * selected node highlighted - every look fully styleable (width, color, gradient, a texture / material / SVG along the line,
 * dashes, outline, glow, flowing pulses / dots / arrows, end markers), and routed straight, curved, in right angles with round
 * or chamfered corners, on 45-degree diagonals, as arcs, round an orbit or through waypoints. A change of state draws the new
 * look along the line from the node that was reached. Presets fill everything in for the common looks.
 *
 * Node states are picked, not typed - Locked, Unlocked, Available, Unavailable - with a Custom State for a game's own (it
 * wins wherever it is given). They come from Set Node State (or the Nodes list, or the nodes themselves through the Widget
 * Path Node interface); Available is worked out on its own (a locked node next to an unlocked one). The layer also answers
 * graph questions - the route to a node, a node's neighbours - for the game's own unlock logic.
 *
 * Looks are kept as presets: the built-in ones (Preset), or a Widget Path Preset asset of your own (Preset Asset; Save As
 * Preset makes one from this layer).
 */
UCLASS(meta = (DisplayName = "Widget Path Layer"))
class NAMETBD_API UWidgetPathLayer : public UWidget
{
	GENERATED_BODY()

public:
	UWidgetPathLayer(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Ready-made routing, styles and rules. Picking one replaces them; editing them makes it Custom. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Widget Path")
	EWidgetPathPreset Preset = EWidgetPathPreset::SkillTree;

	/**
	 * A look of your own, saved as an asset (Save As Preset makes one from this layer): picking one puts its routing, styles
	 * and rules here, as a built-in Preset does - every tree of a game sharing one look. Applied again with Apply Preset Asset
	 * after the asset is edited.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Widget Path")
	TObjectPtr<UWidgetPathPresetAsset> PresetAsset = nullptr;

	/** How every line runs (a connection can override it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Routing")
	FWidgetPathRouting Routing;

	/** The looks lines can take, by name. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Styles", meta = (TitleProperty = "Name"))
	TArray<FWidgetPathStyle> Styles;

	/** Which style a line takes, tried in order: the first rule that holds gives it. A line no rule matches takes the first style. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Styles", meta = (TitleProperty = "Style"))
	TArray<FWidgetPathStyleRule> Rules;

	/** The lines, between widgets of this Widget Blueprint. Lines added from Blueprint (Add Connection) come on top. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Connections")
	TArray<FWidgetPathConnection> Connections;

	/** Node states as authored - also what the designer shows. Set Node State changes them at runtime. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Nodes", meta = (TitleProperty = "Widget"))
	TArray<FWidgetPathNodeState> Nodes;

	/** The state of a node nothing has set. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Nodes", meta = (DisplayName = "Default Node State"))
	EWidgetPathState DefaultState = EWidgetPathState::Locked;

	/** A state of the game's own for nodes nothing has set - wins over Default Node State. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Nodes")
	FName DefaultCustomState;

	/** The state that counts as reached: what Derive Available and routes start from. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Nodes", meta = (DisplayName = "Unlocked State"))
	EWidgetPathState UnlockedNodeState = EWidgetPathState::Unlocked;

	/** A state of the game's own that counts as reached - wins over Unlocked State. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Nodes")
	FName UnlockedCustomState;

	/**
	 * A node in Default Node State connected to a node in Unlocked State reads as Available State - so the game only says what
	 * is unlocked. With Routes Follow Direction, only through connections from the unlocked node.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Nodes")
	bool bDeriveAvailable = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Nodes", meta = (DisplayName = "Available State", EditCondition = "bDeriveAvailable", EditConditionHides))
	EWidgetPathState AvailableNodeState = EWidgetPathState::Available;

	/** A state of the game's own that Derive Available gives - wins over Available State. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Nodes", meta = (EditCondition = "bDeriveAvailable", EditConditionHides))
	FName AvailableCustomState;

	/** Routes (Find Route, the highlighted route to a hovered or selected node) never pass through or end at an Unavailable node. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Nodes", meta = (DisplayName = "Routes Avoid Unavailable"))
	bool bUnavailableBlocksRoutes = true;

	/** Asks each node implementing Widget Path Node for its state, ten times a second, at runtime. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Nodes")
	bool bReadStatesFromNodes = true;

	/** What hovering a node highlights (runtime). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Highlight")
	EWidgetPathHoverHighlight HoverHighlight = EWidgetPathHoverHighlight::RouteTo;

	/** Highlights the route to the selected node (Set Selected Node) from the nearest node in Unlocked State. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Highlight")
	bool bHighlightRouteToSelected = true;

	/** Routes and Derive Available only go through connections the way they point (From to To) - a tree with one-way prerequisites. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Highlight")
	bool bRoutesFollowDirection = false;

	/** The node shown as selected in the designer - to judge the highlighted look. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Highlight", meta = (GetOptions = "GetWidgetOptions"))
	FName PreviewSelectedNode;

	/** How a line changes look when its style changes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	EWidgetPathTransition Transition = EWidgetPathTransition::DrawOn;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (EditCondition = "Transition != EWidgetPathTransition::Instant", EditConditionHides, ClampMin = "0", UIMax = "3", Units = "s"))
	float TransitionDuration = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (EditCondition = "Transition != EWidgetPathTransition::Instant", EditConditionHides))
	EWidgetMotion TransitionMotion = EWidgetMotion::Smooth;

	/** How the lines appear when the layer is first shown (Play Intro plays it again). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	EWidgetPathIntro Intro = EWidgetPathIntro::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (EditCondition = "Intro != EWidgetPathIntro::None", EditConditionHides, ClampMin = "0", UIMax = "3", Units = "s"))
	float IntroDuration = 0.5f;

	/** Seconds between one step from the roots and the next. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (EditCondition = "Intro != EWidgetPathIntro::None", EditConditionHides, ClampMin = "0", UIMax = "1", Units = "s"))
	float IntroStagger = 0.08f;

	/** A node's state changed (Set Node State, or read from the node). */
	UPROPERTY(BlueprintAssignable, Category = "Widget Path")
	FOnWidgetPathNodeStateChanged OnNodeStateChanged;

	/** The pointer moved onto another node, or off them all (None). */
	UPROPERTY(BlueprintAssignable, Category = "Widget Path")
	FOnWidgetPathNodeEvent OnHoveredNodeChanged;

	// ---------------------------------------------------------------------------------------------
	// Connections
	// ---------------------------------------------------------------------------------------------

	/** Adds a line between two widgets (any widgets - made at runtime too). Style None: by the rules. False if they are already connected. */
	UFUNCTION(BlueprintCallable, Category = "Widget Path|Connections")
	bool AddConnection(UWidget* FromNode, UWidget* ToNode, FName StyleName = NAME_None);

	/** Removes the line between two widgets (either way round), authored or added. */
	UFUNCTION(BlueprintCallable, Category = "Widget Path|Connections")
	bool RemoveConnection(UWidget* FromNode, UWidget* ToNode);

	/** Removes the lines added from Blueprint - and the authored ones too, with Include Authored. */
	UFUNCTION(BlueprintCallable, Category = "Widget Path|Connections")
	void ClearConnections(bool bIncludeAuthored = false);

	/** Whether two widgets are connected (either way round). */
	UFUNCTION(BlueprintPure, Category = "Widget Path|Connections")
	bool AreConnected(UWidget* NodeA, UWidget* NodeB) const;

	/** Gives the line between two widgets a style whatever the rules say (None: back to the rules). */
	UFUNCTION(BlueprintCallable, Category = "Widget Path|Connections")
	bool SetConnectionStyle(UWidget* FromNode, UWidget* ToNode, FName StyleName);

	/** Fills the line between two widgets from its first node with Progress Style, 0 to 1 - progress toward the next node. */
	UFUNCTION(BlueprintCallable, Category = "Widget Path|Connections")
	bool SetConnectionProgress(UWidget* FromNode, UWidget* ToNode, float InProgress, bool bAnimate = true);

	// ---------------------------------------------------------------------------------------------
	// Nodes
	// ---------------------------------------------------------------------------------------------

	/**
	 * Sets a node's state: Locked, Unlocked, Available or Unavailable - or a Custom State of the game's own, which wins over
	 * State when given (Custom with no Custom State clears what was set). Its lines move to their new look as Transition says.
	 */
	UFUNCTION(BlueprintCallable, Category = "Widget Path|Nodes", meta = (AdvancedDisplay = "CustomState"))
	void SetNodeState(UWidget* Node, EWidgetPathState State, FName CustomState = NAME_None, bool bAnimate = true);

	/** Sets the state of several nodes at once (Custom State wins over State when given). */
	UFUNCTION(BlueprintCallable, Category = "Widget Path|Nodes", meta = (AdvancedDisplay = "CustomState"))
	void SetNodeStates(const TArray<UWidget*>& InNodes, EWidgetPathState State, FName CustomState = NAME_None, bool bAnimate = true);

	/**
	 * A node's state as the rules see it: set, read, authored or derived (Available), else Default Node State. A state of the
	 * game's own comes back as Custom, its name in Custom State.
	 */
	UFUNCTION(BlueprintPure, Category = "Widget Path|Nodes")
	EWidgetPathState GetNodeState(UWidget* Node, FName& CustomState) const;

	/** A node's state as a name - "Locked", "Unlocked", "Available", "Unavailable" or a Custom State - for text and saving. */
	UFUNCTION(BlueprintPure, Category = "Widget Path|Nodes")
	FName GetNodeStateName(UWidget* Node) const;

	/** Whether a node is in State (or in Custom State, when given). */
	UFUNCTION(BlueprintPure, Category = "Widget Path|Nodes", meta = (AdvancedDisplay = "CustomState"))
	bool IsNodeInState(UWidget* Node, EWidgetPathState State, FName CustomState = NAME_None) const;

	/** Every node (widget on a line) in State - or in Custom State, when given. */
	UFUNCTION(BlueprintPure, Category = "Widget Path|Nodes", meta = (AdvancedDisplay = "CustomState"))
	TArray<UWidget*> GetNodesInState(EWidgetPathState State, FName CustomState = NAME_None) const;

	/** Forgets the states set at runtime: back to the authored Nodes list and Default Node State. */
	UFUNCTION(BlueprintCallable, Category = "Widget Path|Nodes")
	void ResetNodeStates(bool bAnimate = true);

	/** The widgets connected to Node: those it leads to, those leading to it, or both. */
	UFUNCTION(BlueprintPure, Category = "Widget Path|Nodes")
	TArray<UWidget*> GetConnectedNodes(UWidget* Node, EWidgetPathDirection Direction = EWidgetPathDirection::Both) const;

	/**
	 * The shortest route to Target from the nearest node in Unlocked State, through the connections (only their way with
	 * Routes Follow Direction): that node first, Target last. Empty when Target is already unlocked or can't be reached.
	 * What a "points needed" count or a buy-the-whole-route button needs.
	 */
	UFUNCTION(BlueprintPure, Category = "Widget Path|Nodes")
	TArray<UWidget*> FindRoute(UWidget* Target) const;

	// ---------------------------------------------------------------------------------------------
	// Highlight
	// ---------------------------------------------------------------------------------------------

	/** The selected node (its route is highlighted with Highlight Route To Selected). Null: none. */
	UFUNCTION(BlueprintCallable, Category = "Widget Path|Highlight")
	void SetSelectedNode(UWidget* Node);

	UFUNCTION(BlueprintPure, Category = "Widget Path|Highlight")
	UWidget* GetSelectedNode() const { return SelectedNode.Get(); }

	/** The node under the pointer (runtime), or null. */
	UFUNCTION(BlueprintPure, Category = "Widget Path|Highlight")
	UWidget* GetHoveredNode() const { return HoveredNode.Get(); }

	/** Highlights the lines between consecutive widgets of Route (as Find Route returns it), until Clear Highlight. */
	UFUNCTION(BlueprintCallable, Category = "Widget Path|Highlight")
	void HighlightRoute(const TArray<UWidget*>& Route);

	/** Highlights every line of Node, until Clear Highlight. */
	UFUNCTION(BlueprintCallable, Category = "Widget Path|Highlight")
	void HighlightNodeConnections(UWidget* Node);

	/** Clears what Highlight Route and Highlight Node Connections highlighted. */
	UFUNCTION(BlueprintCallable, Category = "Widget Path|Highlight")
	void ClearHighlight();

	// ---------------------------------------------------------------------------------------------
	// Look
	// ---------------------------------------------------------------------------------------------

	/** Puts a preset's routing, styles and rules on the layer. */
	UFUNCTION(BlueprintCallable, Category = "Widget Path|Look")
	void ApplyPreset(EWidgetPathPreset InPreset);

	/** Puts a Widget Path Preset asset's routing (when it applies routing), styles and rules on the layer; it becomes the Preset Asset. */
	UFUNCTION(BlueprintCallable, Category = "Widget Path|Look")
	void ApplyPresetAsset(UWidgetPathPresetAsset* InPresetAsset);

	/** Writes this layer's routing, styles and rules into a Widget Path Preset asset (at runtime too - an in-game theme editor). */
	UFUNCTION(BlueprintCallable, Category = "Widget Path|Look")
	void CopyToPresetAsset(UWidgetPathPresetAsset* Target) const;

	/** Editor: saves this layer's routing, styles and rules as a new Widget Path Preset asset (the Save Asset As window), and picks it. */
	UFUNCTION(CallInEditor, Category = "Widget Path")
	void SaveAsPreset();

	/** Changes (or adds) a style's look. */
	UFUNCTION(BlueprintCallable, Category = "Widget Path|Look")
	void SetStyleLook(FName StyleName, const FWidgetPathLook& InLook);

	/** Sets how every line runs. */
	UFUNCTION(BlueprintCallable, Category = "Widget Path|Look")
	void SetRouting(const FWidgetPathRouting& InRouting);

	/** Plays the intro on every line. */
	UFUNCTION(BlueprintCallable, Category = "Widget Path|Look")
	void PlayIntro();

	/**
	 * The route of the line between two widgets as last drawn, in desktop space (where the pointer is), from From Node to To
	 * Node - what a Follow Path constraint moves a widget along. False when they aren't connected or it isn't drawn yet.
	 */
	UFUNCTION(BlueprintPure, Category = "Widget Path|Connections")
	bool GetConnectionPath(UWidget* FromNode, UWidget* ToNode, TArray<FVector2D>& OutPoints) const;

	/** Finds the authored nodes again by name and re-reads their states - after widgets were added or renamed. */
	UFUNCTION(BlueprintCallable, Category = "Widget Path|Nodes")
	void RefreshNodes();

	/** The widget names offered in the dropdowns. */
	UFUNCTION()
	TArray<FString> GetWidgetOptions() const;

	/** The style names offered in the dropdowns. */
	UFUNCTION()
	TArray<FString> GetStyleOptions() const;

	// UWidget
	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	// UObject
	virtual void PostLoad() override;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	/** A line added from Blueprint. */
	struct FAddedConnection
	{
		TWeakObjectPtr<UWidget> From;
		TWeakObjectPtr<UWidget> To;
		FName Style;
	};

	/** What was set at runtime on any line, authored or added, by its key. */
	struct FConnectionOverride
	{
		TOptional<FName> Style;
		float Progress = -1.f;
	};

	/** A node's state as set at runtime or read from it. */
	struct FNodeRecord
	{
		TWeakObjectPtr<UWidget> Widget;
		FName SetState;
		FName ReadState;
		uint32 ChangeSerial = 0;
	};

	/** The state names in use, as the rules match them. */
	FName GetDefaultStateName() const;
	FName GetUnlockedStateName() const;
	FName GetAvailableStateName() const;

	/** Sets a node's state by name (None clears it); true when it changed. */
	bool SetNodeStateName(UWidget* Node, FName StateName);

	/** Whether a route may pass through or end at a node in this state. */
	bool CanRouteThrough(FName StateName) const;

	/** Moves custom names that are built-in states' into the enums (assets from before the built-in states). */
	void NormalizeStates();

	/** Editor: copies the routing, styles, rules and presets onto the Widget Blueprint's own widget when this is a designer preview copy. */
	void SyncLookToTemplate();

	/** One line, both ends found. */
	struct FEdgeInfo
	{
		UWidget* From = nullptr;
		UWidget* To = nullptr;
		uint64 Key = 0;
		FName Style;
		float Progress = 0.f;
		FName ProgressStyle;
		const FWidgetPathRouting* Routing = nullptr;
	};

	static uint64 MakeKey(const UWidget* A, const UWidget* B);
	void GatherEdges(TArray<FEdgeInfo>& Out) const;
	FName GetRawState(UWidget* Node) const;
	void ComputeStates(const TArray<FEdgeInfo>& InEdges, TMap<UWidget*, FName>& OutStates) const;
	TArray<UWidget*> FindRouteIn(const TArray<FEdgeInfo>& InEdges, const TMap<UWidget*, FName>& States, UWidget* Target) const;
	int32 FindStyleIndex(FName StyleName) const;
	int32 ResolveStyle(const FEdgeInfo& Edge, const TMap<UWidget*, FName>& States, bool bHighlighted) const;
	FWidgetPathNodeShapeInfo ReadNodeShape(UWidget* Node, const FWidgetPathRouting& InRouting) const;
	void Push(bool bAnimate);
	void PollNodes(float DeltaTime);
	void NoteStateChange(UWidget* Node);
	UWidget* FindNode(FName Name) const;
	void CacheNames();
	bool RulesReadHover() const;

	TSharedPtr<SWidgetPathLayer> MyLayer;

	TArray<FAddedConnection> AddedConnections;
	TSet<uint64> RemovedKeys;
	TMap<uint64, FConnectionOverride> Overrides;
	TMap<TObjectKey<UWidget>, FNodeRecord> NodeRecords;
	uint32 NextChangeSerial = 1;

	TWeakObjectPtr<UWidget> SelectedNode;
	TWeakObjectPtr<UWidget> HoveredNode;
	TSet<uint64> ManualHighlight;

	/** The widgets of this tree by name, and the authored node states - found once per Synchronize / Refresh Nodes. */
	TMap<FName, TWeakObjectPtr<UWidget>> NameCache;
	TMap<FName, FName> AuthoredStates;

	/** Every node on a line, as of the last push - what hover and state polling walk. */
	TArray<TWeakObjectPtr<UWidget>> KnownNodes;

	float ReadStatesTimer = 0.f;
	bool bIntroPlayed = false;
	bool bLooksDirty = true;
};
