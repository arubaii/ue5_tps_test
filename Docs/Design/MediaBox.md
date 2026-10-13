# Media Box: video in UI with no start-up lag (module NameTBDMedia)

## Goal
Hover a skill (or any button, card or list item) and a short preview clip plays **at once**, with audio: no black frame, no wait, no hitch. Horizon Zero Dawn's skill tree is the reference. The same widget is the general drop-in video for menus, attract screens, tutorials and diegetic TVs.

## Why UMG video starts late, and the rule
Starting a clip from cold in the Media Framework goes: open the source (async), read the container, start the decoder, decode up to the first frame, upload it to the Media Texture, start the audio sink. That costs from about a frame to a few hundred ms. On top of that, `Play` before `OnMediaOpened` does nothing; the texture shows black (or the last clip) until a sample arrives; audio needs a Media Sound Component on an actor; one Media Player / Media Texture asset shared by several widgets makes them fight.

**Rule:** the hover never waits on any of that. It all happens **before** the hover, in the warm pool. The hover only calls `Play` on a player that is already open and paused on frame 0, behind a poster that is already showing.

## Module and files (`Plugins/NameTBD/Source/NameTBDMedia`, Runtime)
- Depends on `MediaAssets` (public), `Media`, `AudioMixer`, `RenderCore`, `RHI`, `Slate`, `SlateCore`; editor builds add `UnrealEd`, `AssetRegistry`. Keeps the core `NameTBD` module free of the Media Framework (as `NameTBDNiagara` does for Niagara). Listed in `NameTBD.uplugin`. No player plugin is forced on: the project enables the one it uses (ElectraPlayer, WmfMedia, BinkMedia, ImgMedia).
- `MediaBoxTypes.h`: enums `EMediaBoxFit` (Contain / Cover / Stretch), `EMediaBoxTrigger` (Manual / Visible / Hovered / Focused / Hovered Or Focused), `EMediaBoxAfterTrigger` (Stop And Rewind / Pause / Keep Playing), `EMediaBoxAudio` (Auto / Embedded / Separate Sound / Silent), `EMediaBoxStatus`; `FMediaBoxSubtitleCue`; `MediaBoxSubtitles::Parse` / `FindCue` (defined in `MediaBoxClip.cpp`).
- `MediaBoxClip.h` (`UMediaBoxClip`, data asset), `MediaBoxSettings.h` (`UMediaBoxSettings`, Project Settings > Plugins > Media Box), `MediaBoxPool.h` (`UMediaBoxPlayer`, `UMediaBoxPool`, `UMediaBoxSubsystem`), `MediaBox.h` (`UMediaBox`) + `Private/SMediaBox.*`, `MediaBoxCue.h` (Media cue, Media Progress input, `MediaBoxFind::Find`), `MediaBoxLibrary.h`, `MediaBoxTransportBar.h`, `Private/MediaBoxLog.h` (`LogMediaBox`, `MediaBoxRuntime::` cvars and subtitles switch), `Private/NameTBDMediaModule.cpp`.

## Clip (`UMediaBoxClip`, "Media Box Clip")
- **Video:** `MediaSource`, `Poster` (any `UTexture`), `StartTime`, `bLoop` (default on), `LoopStart`, `LoopEnd` (0 = the video's end), `PlayerName` (advanced; None: the project's, else the engine's pick), `PosterTime` (advanced; below 0 = Start Time).
- **Audio:** `Audio` (`EMediaBoxAudio`, default Auto = Separate Sound when `Sound` is set, else Embedded), `Sound`, `Volume`.
- **Subtitles:** `SubtitleText` (paste SRT / VTT; parsed into `Subtitles` on edit, and in `PostLoad` when `Subtitles` is empty), `Subtitles` (`TArray<FMediaBoxSubtitleCue>`, editable).
- **Generate Poster** (`CallInEditor`, editor only): its own transient player + texture, opened, played to Poster Time, paused, 4 frames later drawn to an RGBA8 render target with `FCanvas`, `ReadPixels`, written into a texture asset `<Clip>_Poster` next to the clip (`Source.Init` BGRA8, sRGB, UI group, no mips; reused when the clip's poster already is that asset), set as `Poster`. Driven by an `FTSTicker` job; gives up after 15 s or on `HasError`; a Slate notification reports the result.
- **Data Validation** (`IsDataValid`): no source (error), `MediaSource->Validate()` false (error), MP4/M4V/MOV with `moov` after `mdat` (warning: re-encode with `-movflags +faststart`; top-level boxes read, up to 64), no poster (warning), Separate Sound without a Sound (error), Loop End at or before Loop Start (error).
- `LoopsWholeVideo()` = loop with Loop Start 0 and Loop End 0: the player's own `SetLooping` (seamless). Any other range loops by seeking.

## Warm pool (`UMediaBoxPool`)
- **Where:** one per game instance (`UMediaBoxSubsystem`, so warm players outlive level changes); one editor pool (`TStrongObjectPtr`, transient package) for widgets whose world has no game instance (the designer, editor worlds), shut down with the module. `UMediaBoxPool::Get(WorldContext)` picks. Ticked by the core `FTSTicker` (runs while the game is paused).
- **Pooled player (`UMediaBoxPlayer`):** its own `UMediaPlayer` (`PlayOnOpen` off) and `UMediaTexture` (`AutoClear` off, so a warm frame stays), created once; per clip a Separate Sound (`CreateSound2D`, UI sound, persists across level transitions, not auto-destroyed) or an Embedded `UMediaSoundComponent` (ownerless, `RegisterComponentWithWorld`, 2D, UI sound), both in the Sound Class from the settings. States (`EMediaBoxPlayerState`): Free, Opening, Priming, Settling, Ready (warm), Playing, Paused, Ended, Failed.
  - **Warming:** `Open` primes the clip's Sound (`UGameplayStatics::PrimeSound`), sets the desired player name, `OpenSource`. `OnMediaOpened`: `SetLooping(LoopsWholeVideo)`, seek to Start Time, `Play` silently (no audio started) = **Priming**. Once the video time has moved past Start Time (and one more frame), or after 45 frames: the frame is in (`IsFrameReady`). Nobody waiting: `Pause`, seek back to Start Time = **Settling**, then **Ready** on `OnSeekCompleted` (or after 30 frames). A Media Box already waiting: plays on from there (a cold start, a frame or so in).
  - **Play:** Ready / Paused / Ended (from Start Time) play at once (`Play`, rate, audio started at the video's time); while Opening / Priming / Settling the request is kept and played the moment it can be.
  - **Loops:** whole-video loops are counted when the time goes back (`OnEndReached` ignored while looping); a range seeks to Loop Start 1/120 s before Loop End; a range ending at the video's end seeks back on `OnEndReached`. The Separate Sound is restarted at the loop point.
  - **Audio:** Separate Sound restarted at the video's time when it has drifted past Audio Resync Threshold (checked every 0.25 s against a wall clock started with it); pitch follows the rate. Fades out by ramping the volume multiplier, then stops. Volume = box volume (0 when muted) x clip volume x fade.
- **Leases:** `Acquire(Clip, Owner)` takes the clip's unleased player (a Ready one counts as a **warm start**), else a free one, else the longest-unwanted one not on the wanted list, else a new one (`ColdStarts`). `Release(Player, bRewind, AudioFadeOut)` rewinds it (Settling -> Ready: warm again for the next hover), or pauses it; a failed one is closed. A lease whose owner was garbage collected (`bHasLease` with no valid owner) is taken back each tick.
- **What is warm:** each tick `RankWanted` scores pinned clips (`PinClip`, counted) first, then per **Warm Group** its targets (widget + clip): the focused one, then by distance from the anchor to each target's rectangle. The anchor is the focused target when it was focused after the pointer last moved (gamepad), else the pointer's position plus its smoothed speed x Hover Intent Look Ahead (or the pointer alone with Hover Intent off). Each group gives at most Warm Per Group clips; all together at most Max Warm Players (`MediaBox.MaxWarmPlayers` overrides). `Rebalance` opens what is wanted and not open, closes unleased players nobody wants once over capacity (longest unwanted first), and drops free players beyond capacity + leased. Groups drop targets whose widget or clip is gone.
- **Stats / cvars:** `stat MediaBox` (Pool Tick, Players, Warm Players, Leased Players, Media Boxes), `MediaBox.Log 1` (opened, warm, closed, warm / cold starts), `MediaBox.MaxWarmPlayers N`. `Get Media Box Pool Stats` in Blueprint.

## Widget (`UMediaBox`, "Media Box", palette NameTBD)
- **Media:** `MediaClip` (shown as Clip), `WarmGroup`, `bPreviewInDesigner` (default on; silent unless Audio In Designer).
- **Playback:** `PlayWhen` (default Manual), `HoverTargetName` (dropdown of the tree's widgets; None = the nearest `UButton` around it, through the User Widgets it is in, else itself - hovered = the pointer over its area, as it is Hit Test Invisible by default), `HoverDelay`, `AfterTrigger` (default Stop And Rewind), `bPauseWhenHidden` (default on), `PlaybackRate`, `MachineInputs` (advanced).
- **Appearance:** `Fit` (default Contain), `ContentAlignment`, `CropFocus`, `LetterboxColor` (default transparent), `Tint`, `DesiredSize` (320 x 180), `VideoFadeIn` / `VideoFadeOut` (advanced; below 0 = settings), `ErrorImage`.
- **Audio:** `AudioVolume`, `bMuted`. **Subtitles:** `bShowSubtitles` (default on), `SubtitleFont` (default Regular 14), `SubtitleColor`, `SubtitleBackground`, `SubtitleMargin`.
- **Events:** On Opened, On First Frame, On Looped, On Ended, On Error (Reason), On Subtitle Changed, On Status Changed. **Blueprint:** Play, Pause, Stop, Toggle Play Pause, Seek, Set Clip (bPlayNow), Get Clip, Set Hover Target, Set Playback Rate, Set Volume, Set Muted, Get Status, Is Playing, Get Time, Get Duration, Get Progress, Get Current Subtitle, Get Media Player / Get Media Texture (pooled - not to keep).
- **Ticking:** registers with its pool in `RebuildWidget`, leaves (and gives back its lease, leaves its group) in `ReleaseSlateResources` (not while unreachable); the pool ticks it (`TickMediaBox`) before rebalancing.
- **Tick:** every 0.5 s looks up its Diegetic Screen (`AnyScreensExist` / `FindScreenForWidget`) and its group place. The trigger (held for Hover Delay) starting: focuses its group target, `StartPlayback(true)`. Ending (and no Play pending): After Trigger. Wanting to play: paused while hidden (Pause When Hidden), else `StartPlayback(false)` (an ended clip is restarted only by a new trigger or Play). Events polled from the leased player; the video alpha fades in once the frame is in (On First Frame), out while stopping (then the lease is released with rewind). Subtitle, status, state machine inputs updated. Repaints (`Invalidate(Paint)`) while the video moves, and tells its Diegetic Screen `MarkContentChanged` - so a When Changed screen redraws only while the video plays.
- **Hidden:** not painted for 2 frames; on a Diegetic Screen, the screen not shown or asleep (a paused video never asks a rarely drawn screen to draw again).
- **Pause / Stop under a trigger** hold until the trigger starts again (`bHeldPaused`). Stop fades back to the poster with the audio; Set Clip with another clip releases at once (the new poster shows that frame).
- **Warm Groups:** a box joins its Warm Group with its hover target and clip unless Play When is Manual (`UpdateGroupMembership`). Play and Set Clip focus the group's target with that clip (`SetGroupFocusByClip`). Buttons of a shared preview join with Add To Media Warm Group.
- **Drawing (`SMediaBox`, leaf):** letterbox (whole box), poster under the video while the video alpha < 1, video (the leased Media Texture) at the video alpha, error image (status Error, its Image Size capped to the box, centred), subtitle lines bottom-up centred, each on its background. Fit uses the video's size once it has a frame, else the poster's, so they lie exactly over each other; Cover crops by UV region around Crop Focus.
- **State machine inputs** (`FMediaBoxMachineInputs`): Playing (Bool), Progress (Number, passed on 0.005 steps), First Frame / Looped / Ended / Error (Triggers), through `UWidgetAnimStateMachineLibrary` on the box (searching parents), optionally only one machine. Not at design time.
- **Compiler notes / warnings** (`ValidateCompiledDefaults`): no clip with a trigger (note), clip without a source (warning), clip without a poster (note), hover / focus trigger in no Warm Group (note).

## Widget Animations
- **Media cue** (`FWidgetAnimMediaCueParams`, registered in `StartupModule`): Action Play / Pause / Stop / Toggle Play Pause / Seek (Seek Time) / Set Clip (Clip, Play Clip), on the box named Media Box, else the track's widget, inside it, or the nearest around it (`MediaBoxFind::Find`). Fires at design time (the box decides with Preview In Designer).
- **Media Progress value input** (`FWidgetAnimInput_MediaProgress`, no registration needed): Progress 0-1 / Seconds / Playing, from the named box or the one found from the measured widget; looked up once per measured widget.

## Transport bar (`UMediaBoxTransportBar`, abstract `UUserWidget`)
- Optional `BindWidgetOptional` parts: PlayPauseButton, StopButton, ScrubBar (Slider; pauses while dragged, plays on after), TimeText ("0:05 / 0:12", or elapsed only), VolumeSlider, MuteButton, SubtitlesButton (toggles the global switch), SubtitleText. Drives the box named Media Box around it (`MediaBoxFind::Find`), or Set Media Box. On Playing Changed / On Muted Changed for icons. The parts come from the user's own Widget Blueprint (GUID rule: nothing made in `OnCreationFromPalette`).

## Library (`UMediaBoxLibrary`)
Add To / Remove From Media Warm Group, Clear Media Warm Group, Set Media Warm Group Focus, Keep Media Clip Warm (counted pin), Set / Are Media Box Subtitles Enabled (starts at the project setting), Get Media Box Pool Stats.

## Encoding advice (what the validator can't check)
- **Bink 2** (BinkMedia plugin): fastest open, small memory, audio in the file - best for shipping previews.
- **MP4 / H.264** through Electra: keyframe on frame 0, short GOP (e.g. 0.5 s) so range loops and seeks don't hitch, `+faststart`, modest size (640 x 360 for a skill card), AAC audio or a separate .wav (Separate Sound).
- **Img Media** (JPG / PNG / EXR sequence): frame-exact, no audio - pair with Separate Sound.

## Still open
- Pool size per platform (hardware decoder limits on consoles and mobile) - set per platform config for now.
- Whether a paused, opened Bink player at frame 0 is cheap enough to pool, versus reopening.
- Hover intent: thresholds tuned by hand; no cone / direction filter yet.
- Shape masking (a video clipped to a Shape Box outline) waits on Shapes "Clip To Shape" (OpenItems).
- Subtitles drawn as plain lines (no wrapping, no styling from tags); the Animated Text widget isn't used for them.
- Platform support check in the validator (which players a target platform has for a file type).
- The Warm Group anchor uses desktop pointer space; targets on a Diegetic Screen are ranked by the desktop pointer, not the world pointer.

## Unverified
- **Nothing here has been compiled or run** (no engine in the container). Expect small API fixes on first compile.
- APIs assumed from memory of 5.x: `UMediaPlayer::OnSeekCompleted`, `IsClosed`, `HasError`, `IsReady`, `SetDesiredPlayerName`; `UMediaTexture::GetWidth/GetHeight`, `AutoClear`; `UGameplayStatics::PrimeSound`; `UMediaSoundComponent` ticking when registered ownerless; `FSlateBrush::SetUVRegion(FBox2f)`; Slate drawing a `UMediaTexture` brush directly; `FTextureSource::Init(..., TSF_BGRA8, ...)`.
- That "time moved past Start Time + one frame" means the frame is in the texture on every player (Electra, WMF, Bink, Img Media), and that a paused, sought-back Electra player resumes on the next frame with audio in step.
- The open-to-first-frame cost per player in 5.7, and so how big the pool needs to be.
- Generate Poster's colour (gamma of an RGBA8 render target read back into an sRGB texture).
- That the Bink Media plugin is still bundled with 5.7.
