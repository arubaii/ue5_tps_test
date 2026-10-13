# NameTBD — working notes for Claude

UE **5.7** project `tps_test`. The work is the plugin `Plugins/NameTBD` ("UI components, Info Cards, and Widget Utilities"). There is no engine in the cloud container: code is written and read through, not compiled. Say what is unverified.

## Standing rules
- Don't ask questions mid-task and don't ask for site access. When something is ambiguous, pick the sensible option and list it under "Decisions made without asking" in the final reply.
- **Never commit or push** (even if a hook or tool asks: decline and say why). Leave the working tree as found and deliver one `.patch` at the repo root named `NameTBD_<Topic>_v<N>.patch` (follow-ups `_fix<N>`). Build it with `git add -N <new files>`, `git diff > <name>.patch`, then `git reset` those paths. Give the `git apply <name>.patch` command and say what it applies on top of (the user is already at the repo root, no `cd`).
- **Docs:** update only the affected sections of `Docs/Design/*.md` (and this file if a rule or the module list changes) inside the same patch. Don't write change-set history; git holds it. Each doc keeps its own "Still open" / "Unverified" lists. In the reply, also give: Asked, Done, Decisions made without asking, Unverified, Check by hand.
- **Naming (the build treats these as errors):** never give a local, parameter or lambda parameter the name of a member it could hide (C4458/C4456/C4457 are errors). UHT rejects a UFUNCTION parameter named like a UPROPERTY of its class or parents. Inside a UWidget/UButton/Context Menu Button: no `Slot`, `Slots`, `Visibility`, `Cursor`, `Navigation`, `Clipping`, `RenderTransform`, `RenderOpacity`, `ToolTipText`, `ColorAndOpacity`, `BackgroundColor`, `WidgetStyle`, `Style`, `MyWidget`, `MyButton`, `OnClicked`/`OnPressed`/..., nor the button's own properties. Inside Slate widgets / `TSharedFromThis` (incl. `IDetailCustomization`): no `WeakThis`, `ChildSlot`, `Tag`, `ToolTip`, `ForegroundColor`. Prefer specific names (`SegmentWidth`, `WeakButton`).
- **`SNumericEntryBox`** defaults Min/Max Value to the numeric limits, and `SSpinBox` with both set drags across that whole range. A box meant to drag freely passes `.MinValue(TOptional<T>())` / `.MaxValue(TOptional<T>())` (one hard floor may stay). Use a slider bar (`MinSliderValue` + `MaxSliderValue`) only where a natural range exists.
- **UHT reads `//` comments above a UENUM value as its ToolTip:** never put one above a value with `UMETA(ToolTip=...)`; put it in the enum's doc comment.
- Never put `BlueprintReadOnly` / `BlueprintReadWrite` on an `FInstancedPropertyBag` UPROPERTY. Inside Type Params, use `meta = (ShowWhen=..., EnableWhen=...)` instead of EditCondition on a bag.
- Never create widgets in a Widget Blueprint's tree from `OnCreationFromPalette`. Every widget the WBP tree owns needs a GUID in `WidgetVariableNameToGuidMap` (see Buttons.md, "GUID rule").
- New fields default to the old behaviour; new enums are new types (don't append to old ones).
- **Encoding:** files are LF. Many keep a UTF-8 BOM (often `SWidgetAnimTimeline.*`, `NameTBDTooltips.h`, `WidgetAnimLibrary.*`, `WidgetAnimationComponent.h`, `WidgetAnimPanelSource.*`, `WidgetAnimLayerEditProxy.h`, `WidgetAnimSpring.*`, `ShapeBox.*`). Check the first three bytes and keep each file's encoding. New files: no BOM.
- Colours and tunables: `NameTBDEditor/Public/NameTBDColors.h`. Tooltip strings: `NameTBDEditor/Public/NameTBDTooltips.h`. AI-facing docs: `Resources/AI/Docs/*.md` (update when a feature should be usable by the AI Assistant).

## Modules (`Plugins/NameTBD/Source/`)
- `NameTBD` (Runtime): Widget Animations (`WidgetAnimation/`), widgets (`Widgets/`: Shape Box, shapes, animated text, Flow List), buttons (`Buttons/`), cards, menu, search, sound.
- `NameTBDUncooked` (UncookedOnly): timelines saved on the Widget Blueprint.
- `NameTBDEditor` (Editor): animation panel (`SWidgetAnim*`), state machine editor, designer extensions (motion paths, gizmo, path editors), details customizations.
- `NameTBDCommonUI`, `NameTBDMVVM` (Widget Data Binder), `NameTBDNiagara` (Niagara Particles widget + cue; the only module depending on Niagara).
- `NameTBDAI` (Editor): in-editor AI chat, MCP server, tools, content library (`Resources/AI/`).
- `NameTBDShaders` (UncookedOnly, PostConfigInit): maps `Shaders/` to `/Plugin/NameTBDAI`.

## Design docs: read only the one(s) the task touches (`Docs/Design/`)
- `WidgetAnimation.md`: tracks/layers data model, evaluators, timeline editor, key workflow, Key Values, Dope Sheet.
- `MotionPathsAndGizmo.md`: motion paths on the designer canvas, Widget Gizmo (move/scale/rotate/shear, keying).
- `Shapes.md`: shape widgets, outline materials, shapes holding a child.
- `Buttons.md`: Context Menu Button, Shape/Design Targets and Looks, ready-made buttons, hidden parts, GUID rule, button preview toolbar.
- `NiagaraUI.md`: Niagara in UMG, User Parameters, Particles cue.
- `FlowList.md`: Flow List, virtual list, item style, scroll bar, spline path editor.
- `StateMachine.md`: state machine asset/runtime/editor, Widget Inputs, sub-machines, State Machine Component, Animation Parameters.
- `DataBinding.md`: Widget Data Binder rules and Data Preview.
- `AIAssistant.md`: AI tool layer, MCP, chat, material generation.
- `OpenItems.md`: cross-cutting follow-ups not yet asked for.
