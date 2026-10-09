// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "Styling/SlateBrush.h"
#include "StructUtils/PropertyBag.h"

#include "NiagaraUIParameters.h"

#include "NiagaraUIWidget.generated.h"

class SNiagaraUIWidget;
class UMaterialInterface;
class UNiagaraComponent;
class UNiagaraSystem;
class UNiagaraUIWidget;
class UWorld;
struct FNiagaraUISource;

/**
 * Which plane of the Niagara System is laid on the widget - the view of the system the UI shows. Every position, velocity
 * and size the system writes is read in this plane; the third axis is depth (see Perspective Distance).
 */
UENUM(BlueprintType)
enum class ENiagaraUIPlane : uint8
{
	XZ UMETA(DisplayName = "X Right, Z Up (Front)", ToolTip = "X is right on screen and Z is up - the system as seen from the front. What most UI particle systems are authored for: gravity pulls down Z, as in the Niagara editor's preview."),

	XY UMETA(DisplayName = "X Right, Y Down (Top)", ToolTip = "X is right and Y is down, as Slate's own axes are - the system seen from above. Handy for systems written with screen coordinates in mind."),

	YZ UMETA(DisplayName = "Y Right, Z Up (Side)", ToolTip = "Y is right and Z is up - the system seen from the side.")
};

/** One simulation the widget runs: its main system, or a burst played at a point. Kept by the widget, never saved. */
USTRUCT()
struct FNiagaraUISimulation
{
	GENERATED_BODY()

	/** Ownerless and transient, registered with the widget's world, its rendering off - the widget draws it. */
	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> Component = nullptr;

	/** A burst's point in the widget, in its local units. The main simulation sits at the widget's Origin instead. */
	FVector2f Position = FVector2f::ZeroVector;

	/** A burst played at the widget's Origin (Burst At Origin) rather than at Position - worked out as it is drawn, wherever it is. */
	bool bAtOrigin = false;

	/** When a burst last started, for recycling the oldest when every burst is busy. */
	double StartedAt = 0.0;

	/** Asked to play before it was ever placed: starts on the next paint, once it is where the widget is. */
	bool bPendingPlay = false;
	bool bPendingRestart = true;

	/** Where the component was last put - only moved again when the widget moves. */
	bool bPlaced = false;
	FVector2f PlacedAt = FVector2f::ZeroVector;
	float PlacedAngle = 0.f;
	float PlacedScale = 1.f;

	/** A Particles track's own User parameter values, put on over the widget's as it next starts - none for anything else. */
	UPROPERTY(Transient)
	FNiagaraUIParameterOverrides Overrides;

	/** The track values on its component now - taken back off (to the system's values) before it plays without them. */
	UPROPERTY(Transient)
	FNiagaraUIParameterOverrides AppliedOverrides;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNiagaraUIFinished, UNiagaraUIWidget*, ParticlesWidget);

/**
 * Niagara Particles: a Niagara System played in UI, drawn by Slate with the rest of the widgets - sorted, clipped, tinted,
 * transformed, faded and laid out as any widget is. Not a render target and not a scene capture: each frame the
 * simulation's particles are read and drawn as Slate meshes, one draw per renderer.
 *
 * What it draws: CPU-simulated emitters (Sim Target CPUSim) with Sprite and Ribbon renderers. Sprites follow their
 * Position, Size, Color, Rotation, Alignment (unaligned, velocity or custom), Pivot, Sub UV frames, Sort Mode and Dynamic
 * Parameter; ribbons their Ribbon ID, Link Order, Width, Color and UV0/UV1 distribution. GPU emitters, meshes and
 * lights are not drawn - the Blueprint compiler says so for the system set here.
 *
 * Materials: a renderer's material is used when it is a User Interface material (Material Domain: User Interface); the
 * sprite's UVs are TexCoord0 and its Dynamic Parameter (x, y) TexCoord1, a ribbon's UV0 and UV1 TexCoord0 and TexCoord1,
 * the particle color is Vertex Color. Any other material is looked up in Material Remap, else drawn with Particle Image
 * (a soft round dot when empty) - so any system shows something at once.
 *
 * Space: the system's origin sits at Origin in the widget; Particle Scale Slate units per Niagara unit. Local-space
 * emitters move, turn and scale with the widget, Render Transform and Widget Animations included. World-space emitters
 * leave their particles behind as the widget moves - a trail behind a dragged item.
 *
 * Cost: nothing until first painted. While shown, one simulation per widget (plus bursts in flight) and one mesh per
 * renderer. Hidden - collapsed, scrolled off, removed from the screen - it pauses (Pause When Hidden) and resumes where it
 * was. Inside an invalidation panel it repaints only while particles are live. stat NiagaraUI shows what it costs.
 *
 * Bursts: Burst At Origin, Play At (or the Particles cue of a Widget Animation) plays a one-shot of the system - or another - at a point
 * of the widget, up to Max Bursts at once, reusing finished ones: one Particles widget over a screen serves every click
 * sparkle and reward pop on it.
 */
UCLASS(meta = (DisplayName = "Niagara Particles", PrioritizeCategories = "Particles Parameters Playback Layout Appearance"))
class NAMETBDNIAGARA_API UNiagaraUIWidget : public UWidget
{
	GENERATED_BODY()

public:
	UNiagaraUIWidget();

	// ---------------------------------------------------------------------------------------------
	// Particles
	// ---------------------------------------------------------------------------------------------

	/** The system played. Its CPU emitters' sprites and ribbons are drawn. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Particles", meta = (DisplayName = "Niagara System"))
	TObjectPtr<UNiagaraSystem> NiagaraSystem = nullptr;

	/** Plays as soon as the widget is first shown. Off, it waits for Play. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Particles", meta = (DisplayName = "Auto Play"))
	bool bAutoPlay = true;

	/** Plays in the Widget Blueprint designer too, so the effect is laid out where it is seen. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Particles", meta = (DisplayName = "Preview In Designer"))
	bool bPreviewInDesigner = true;

	// ---------------------------------------------------------------------------------------------
	// Playback
	// ---------------------------------------------------------------------------------------------

	/**
	 * Keeps playing while the game is paused - a pause menu's particles. Such a simulation ticks on its own rather than
	 * batched with the world's other Niagara systems, which costs a little more each; off for widgets never shown paused.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Playback", meta = (DisplayName = "Play While Paused"))
	bool bPlayWhilePaused = true;

	/**
	 * Pauses the simulation while the widget isn't drawn - collapsed, hidden, scrolled out of view, on a screen not shown,
	 * removed from the viewport - and resumes it where it was once it is drawn again. Off, it plays on unseen.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Playback", meta = (DisplayName = "Pause When Hidden"))
	bool bPauseWhenHidden = true;

	/**
	 * Seconds simulated at once as it starts playing, so it opens already full - drifting embers on a menu that has just
	 * opened. Simulated in 1/30 s steps on the frame it starts, so keep it short. The system's own Warmup adds to it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Playback", meta = (DisplayName = "Prewarm Time", ClampMin = "0.0", UIMax = "5.0", Units = "s"))
	float PrewarmTime = 0.f;

	/**
	 * How many bursts - Play At, Play At Absolute Position, a Particles cue - may play at once. Finished ones are reused;
	 * past this many the oldest starts again at the new point. 0 turns bursts off. A system that loops keeps playing at
	 * each point until Clear, so bursts are for one-shot systems (Loop Behavior: Once).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Playback", meta = (DisplayName = "Max Bursts", ClampMin = "0", UIMax = "32"))
	int32 MaxBursts = 8;

	// ---------------------------------------------------------------------------------------------
	// Layout
	// ---------------------------------------------------------------------------------------------

	/** How big the widget asks to be. Particles are not held inside it: Clipping does that. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (DisplayName = "Desired Size"))
	FVector2D DesiredSize = FVector2D(128.0, 128.0);

	/** Where the system's origin sits in the widget: 0, 0 its top left, 0.5, 0.5 its middle, 1, 1 its bottom right. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
	FVector2D Origin = FVector2D(0.5, 0.5);

	/**
	 * Slate units per Niagara unit - positions, sizes, widths and speeds alike. 1 draws a 10-unit sprite 10 units across,
	 * at the widget's scale (DPI scale and Render Transform included).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (DisplayName = "Particle Scale", ClampMin = "0.001", UIMin = "0.1", UIMax = "10.0"))
	float ParticleScale = 1.f;

	/** Which plane of the system is laid on the widget. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
	ENiagaraUIPlane Plane = ENiagaraUIPlane::XZ;

	/**
	 * Depth as perspective: a particle half this many Niagara units toward the viewer is drawn twice as big and twice as
	 * far from the origin, one as far away two thirds as big - a swirl around a portrait reads in 3D. 0 ignores depth.
	 * Sprites are drawn far to near whenever their renderer's Sort Mode is View Depth or View Distance.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (DisplayName = "Perspective Distance", ClampMin = "0.0", UIMax = "2000.0"))
	float PerspectiveDistance = 0.f;

	// ---------------------------------------------------------------------------------------------
	// Appearance
	// ---------------------------------------------------------------------------------------------

	/** Multiplies every particle's color. Render Opacity and the parents' colors apply too, as on any widget. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance", meta = (DisplayName = "Color And Opacity"))
	FLinearColor ColorAndOpacity = FLinearColor::White;

	/**
	 * What particles are drawn with when their renderer's material can't draw in UI - not a User Interface material, and
	 * not remapped below - tinted by each particle's color. Empty: a soft round dot. A texture, or a User Interface
	 * material. Drawn with plain alpha blending; for additive glows give the renderer a UI material set to Additive.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance", meta = (DisplayName = "Particle Image"))
	FSlateBrush ParticleImage;

	/**
	 * The User Interface material to draw with in place of a renderer's material - the system's own surface material on
	 * the left, its UI twin on the right - so one system serves a level and a menu. Read before Particle Image.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance", meta = (DisplayName = "Material Remap"))
	TMap<TObjectPtr<UMaterialInterface>, TObjectPtr<UMaterialInterface>> MaterialRemap;

	/** Bends ribbons smoothly through their particles rather than in straight steps, where they are far apart on screen. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance", meta = (DisplayName = "Smooth Ribbons"))
	bool bSmoothRibbons = true;

	// ---------------------------------------------------------------------------------------------
	// Parameters
	// ---------------------------------------------------------------------------------------------

	/**
	 * The Niagara System's User parameters, listed from the system itself - every float, int, bool, enum, color, vector,
	 * quat, position, texture, material and object it exposes - each starting at the system's own value. Change one and the
	 * widget plays the system with it (bursts too). Kept in step with the system: a parameter added or removed in the Niagara
	 * editor appears or goes the next time this widget is loaded or its system is set; one left at the system's value follows
	 * the system when that value changes there. Curves (Curve for Floats, Color, Vector and Vector 2D Curve) and arrays (Float,
	 * Int, Bool, Vector 2D, Vector, Position, Vector 4, Color, Quat Array) are listed too, as curves and lists to edit; other
	 * data interfaces aren't. Changing a curve restarts what is playing. Set from Blueprint with the Set ... Parameter
	 * functions (values), or on Get Niagara Component (curves and arrays).
	 */
	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (DisplayName = "User Parameters", FixedLayout))
	FInstancedPropertyBag UserParameters;

	// ---------------------------------------------------------------------------------------------
	// Events
	// ---------------------------------------------------------------------------------------------

	/** The main system has finished: Stop Emitting and its last particle gone, or a one-shot system done. Not for bursts. */
	UPROPERTY(BlueprintAssignable, Category = "Niagara Particles|Events")
	FOnNiagaraUIFinished OnFinished;

	// ---------------------------------------------------------------------------------------------
	// Blueprint
	// ---------------------------------------------------------------------------------------------

	/** Plays the system. Restart starts it over; off, a system already playing carries on. Shown or not, it starts once drawn. */
	UFUNCTION(BlueprintCallable, Category = "Niagara Particles")
	void Play(bool bRestart = true);

	/** Stops spawning: the particles already there live out their lives, then On Finished. Bursts finish too. */
	UFUNCTION(BlueprintCallable, Category = "Niagara Particles")
	void StopEmitting();

	/** Removes every particle at once, bursts' too, and stops. */
	UFUNCTION(BlueprintCallable, Category = "Niagara Particles")
	void Clear();

	/**
	 * Plays a one-shot of the system at a point of this widget, in its local units (0, 0 its top left) - a sparkle where it
	 * was clicked. Burst System plays another system there instead. See Max Bursts.
	 */
	UFUNCTION(BlueprintCallable, Category = "Niagara Particles", meta = (AdvancedDisplay = "BurstSystem"))
	void PlayAt(FVector2D LocalPosition, UNiagaraSystem* BurstSystem = nullptr);

	/**
	 * Plays a one-shot of the system where this widget is - at its Origin, as placed in the designer and moved since. Burst
	 * System plays another system there instead. See Max Bursts.
	 */
	UFUNCTION(BlueprintCallable, Category = "Niagara Particles", meta = (AdvancedDisplay = "BurstSystem"))
	void BurstAtOrigin(UNiagaraSystem* BurstSystem = nullptr);

	/**
	 * Play At, at a point in Slate's absolute (desktop) space - a pointer event's Screen Space Position, or Local To
	 * Absolute of another widget's geometry - wherever it is over this widget.
	 */
	UFUNCTION(BlueprintCallable, Category = "Niagara Particles", meta = (AdvancedDisplay = "BurstSystem"))
	void PlayAtAbsolutePosition(FVector2D AbsolutePosition, UNiagaraSystem* BurstSystem = nullptr);

	/** Plays another system. Play starts it at once (once the widget is drawn); off, it waits for Play. */
	UFUNCTION(BlueprintCallable, Category = "Niagara Particles")
	void SetNiagaraSystem(UNiagaraSystem* NewSystem, bool bPlay = true);

	/** Freezes every particle where it is, bursts included, until unpaused. */
	UFUNCTION(BlueprintCallable, Category = "Niagara Particles")
	void SetPaused(bool bPaused);

	/** Whether Set Paused has paused it. Pause When Hidden doesn't count. */
	UFUNCTION(BlueprintPure, Category = "Niagara Particles")
	bool IsPaused() const { return bUserPaused; }

	/** Whether the main system is playing - spawning, or its last particles still alive. */
	UFUNCTION(BlueprintPure, Category = "Niagara Particles")
	bool IsPlaying() const;

	/** Particles alive now in the widget's CPU emitters, bursts included. For debugging and for waiting on an effect. */
	UFUNCTION(BlueprintPure, Category = "Niagara Particles")
	int32 GetLiveParticleCount() const;

	/**
	 * The Niagara Component running the main system, for anything this widget doesn't offer - Niagara's own Set Variable
	 * functions, say. None before the widget is first drawn, and after it is released. Its transform and rendering are the
	 * widget's: don't move it or turn its rendering on.
	 */
	UFUNCTION(BlueprintPure, Category = "Niagara Particles")
	UNiagaraComponent* GetNiagaraComponent() const;

	/**
	 * Sets a User parameter on the system - by its name, Speed or User.Speed - now and whenever it starts again, bursts too. One
	 * listed in User Parameters is changed there; one that isn't (a burst system's own) only reaches what is playing now.
	 */
	UFUNCTION(BlueprintCallable, Category = "Niagara Particles|Parameters")
	void SetFloatParameter(FName ParameterName, float Value);

	UFUNCTION(BlueprintCallable, Category = "Niagara Particles|Parameters")
	void SetIntParameter(FName ParameterName, int32 Value);

	UFUNCTION(BlueprintCallable, Category = "Niagara Particles|Parameters")
	void SetBoolParameter(FName ParameterName, bool Value);

	UFUNCTION(BlueprintCallable, Category = "Niagara Particles|Parameters")
	void SetColorParameter(FName ParameterName, FLinearColor Value);

	/** A Vector 2 parameter. In the system's own axes, not the screen's. */
	UFUNCTION(BlueprintCallable, Category = "Niagara Particles|Parameters")
	void SetVector2DParameter(FName ParameterName, FVector2D Value);

	/** A Vector (3) or Position parameter. In the system's own axes, not the screen's: with the X Right, Z Up plane, up is +Z. */
	UFUNCTION(BlueprintCallable, Category = "Niagara Particles|Parameters")
	void SetVectorParameter(FName ParameterName, FVector Value);

	UFUNCTION(BlueprintCallable, Category = "Niagara Particles|Parameters")
	void SetVector4Parameter(FName ParameterName, FVector4 Value);

	/** A texture, material, static mesh or other object parameter. */
	UFUNCTION(BlueprintCallable, Category = "Niagara Particles|Parameters")
	void SetObjectParameter(FName ParameterName, UObject* Value);

	/** Puts every User parameter back to the system's own value. */
	UFUNCTION(BlueprintCallable, Category = "Niagara Particles|Parameters")
	void ResetParameters();

	UFUNCTION(BlueprintCallable, Category = "Niagara Particles|Appearance")
	void SetColorAndOpacity(FLinearColor InColorAndOpacity);

	UFUNCTION(BlueprintCallable, Category = "Niagara Particles|Layout")
	void SetParticleScale(float InParticleScale);

	UFUNCTION(BlueprintCallable, Category = "Niagara Particles|Layout")
	void SetDesiredSize(FVector2D InDesiredSize);

	// ---------------------------------------------------------------------------------------------
	// UWidget
	// ---------------------------------------------------------------------------------------------

	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PreSave(FObjectPreSaveContext SaveContext) override;
#endif

#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
	virtual void ValidateCompiledDefaults(IWidgetCompilerLog& CompileLog) const override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/**
	 * Lays User Parameters out again from the Niagara System: its User parameters as they are now, each at the system's value
	 * unless this widget changed it. Called as it loads (in the editor), when its system is set, and by Set Niagara System.
	 */
	void RefreshUserParameters();

	// ---------------------------------------------------------------------------------------------
	// For the Particles cue
	// ---------------------------------------------------------------------------------------------

	/**
	 * Plays a one-shot of System (or the widget's own) at LocalPosition, or at the widget's Origin - Play At and Burst At Origin's
	 * work. Overrides: User parameter values of System's put on over this widget's, for this burst alone.
	 */
	void StartBurst(UNiagaraSystem* System, const FVector2f& LocalPosition, bool bAtOrigin, const FNiagaraUIParameterOverrides* Overrides = nullptr);

	/**
	 * Plays System as this widget's system (or its own, when none), from its start - with Overrides, User parameter values of
	 * System's, over this widget's, until another system is set or it plays without them.
	 */
	void PlayWithOverrides(UNiagaraSystem* System, const FNiagaraUIParameterOverrides* Overrides);

	// ---------------------------------------------------------------------------------------------
	// For the Slate widget and the module's ticker - not for gameplay code
	// ---------------------------------------------------------------------------------------------

	/**
	 * Called as the widget paints: makes the simulations it needs (the first paint starts them), moves each to where the
	 * widget is now, resumes what Pause When Hidden paused, and hands out where each one's particles are drawn.
	 */
	void PrepareForPaint(const FGeometry& Geometry, TArray<FNiagaraUISource>& OutSources);

	/** Once per frame, before the world ticks: Pause When Hidden, and whether the Slate widget repaints every frame. */
	void TickRuntime(uint64 Frame);

	/** Lets go of every simulation registered with World - it is being torn down. */
	void ReleaseSimulationsIn(const UWorld* World);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	UFUNCTION()
	void HandleSystemFinished(UNiagaraComponent* FinishedComponent);

	/** The world simulations are made in: the widget's, or in the designer the editor's. Null while none can be used. */
	UWorld* FindSimulationWorld() const;

	/** Whether anything may be simulated now: not in a designer with Preview In Designer off, not switched off by cvar. */
	bool CanSimulate() const;

	UNiagaraComponent* MakeComponent(UWorld* World, UNiagaraSystem* System);
	void ApplyRuntimeSettings(UNiagaraComponent* Component) const;
	void ApplyParameters(UNiagaraComponent* Component) const;

	/** RefreshUserParameters' work: false when the system's User parameters and values are as already laid out. */
	bool LayOutUserParameters();

	/** In the editor: has the Details panels laid out again, next tick - User Parameters' rows changed. */
	void RequestDetailsRefresh() const;

	/** Folds the v1 Float / Color / Vector Parameters maps into User Parameters, once, as an old widget loads. */
	void MigrateLegacyParameters();

	/** Makes the main simulation if it doesn't exist yet. False when it can't be made now. */
	bool EnsureMainSimulation();

	/** Starts a placed simulation playing - from its start if bRestart - with the Prewarm, and its track's parameter values. */
	void StartSimulation(FNiagaraUISimulation& Simulation, bool bRestart);

	/** Puts Simulation's track parameter values on its component - the ones it had on taken back off first. */
	void ApplyOverrides(FNiagaraUISimulation& Simulation);

	/** Takes Simulation's track parameter values off its component and forgets them - before another system is set on it. */
	void DropOverrides(FNiagaraUISimulation& Simulation);

	/** The burst to play the next Play At with: a finished one (of the same system first), a new one, or the oldest. */
	FNiagaraUISimulation* TakeBurst(UNiagaraSystem* System, UWorld* World);

	void ReleaseSimulations();
	void ApplyPause();
	void SetAnimating(bool bAnimating);

	template <typename FunctionType>
	void ForEachComponent(FunctionType&& Function) const;

	/**
	 * The system's own values of User Parameters when it was last laid out - what tells a parameter this widget changed from one
	 * left at the system's (and so following it). Never edited.
	 */
	UPROPERTY()
	FInstancedPropertyBag UserParameterDefaults;

	/** User Parameters that are Positions in the system (laid out as vectors) - set as positions. */
	UPROPERTY()
	TArray<FName> PositionParameters;

	// The v1 parameters, before User Parameters: read once as an old widget loads (MigrateLegacyParameters), then emptied.
	UPROPERTY()
	TMap<FName, float> FloatParameters;
	UPROPERTY()
	TMap<FName, FLinearColor> ColorParameters;
	UPROPERTY()
	TMap<FName, FVector> VectorParameters;

	UPROPERTY(Transient)
	FNiagaraUISimulation Main;

	UPROPERTY(Transient)
	TArray<FNiagaraUISimulation> Bursts;

	TSharedPtr<SNiagaraUIWidget> MyParticles;

	/** The frame this widget was last painted - what Pause When Hidden goes by. */
	uint64 LastPaintFrame = 0;

	bool bUserPaused = false;
	bool bHiddenPaused = false;

	/** Auto Play has started the main system since its simulation was made - it plays once per simulation, not per paint. */
	bool bAutoPlayDone = false;

	bool bSlateVolatile = false;
	bool bTracked = false;
};
