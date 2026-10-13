# Video in UI (Media Box)

## The widget
MediaBox ("Media Box", module NameTBDMedia) plays a Media Box Clip in UMG. Clip = the video (Media Source), a Poster (still of its first frame, shown at once and whenever stopped), Start Time, Loop / Loop Start / Loop End, Audio (Auto, Embedded, Separate Sound, Silent) and Sound, Subtitles (pick an .srt / .vtt as Subtitle File, or paste its text into Subtitle Text). Play When: Manual, Visible, Hovered, Focused, Hovered Or Focused; Hover Target (None = the button it is in); After Trigger (Stop And Rewind, Pause, Keep Playing); Hover Delay. Fit: Contain, Cover (Crop Focus), Stretch. Events: On Opened, On First Frame, On Looped, On Ended, On Error, On Subtitle Changed, On Status Changed. Functions: Play, Pause, Stop, Seek, Set Clip, Set Volume, Set Muted, Get Progress.

## Video player with controls
MediaBoxWithControls ("Media Box With Controls") is a Media Box with a controls bar over it. Controls Class: a Widget Blueprint whose parent class is MediaBoxTransportBar (None: Project Settings > Media Box > Default Controls Class). Controls Behavior (tick to override the project's Default Controls Behavior): Preset YouTube (shown while the pointer moves, hidden 3 s after it stops or 0.1 s after it leaves while playing, always shown while paused, fades 0.1 s in / 0.25 s out, pointer hidden with the bar, click plays / pauses, subtitles lifted above the bar) or Custom (set Hide After Idle, Hide After Unhovered, the Show While... switches, fades). Controls Height (0.13 of the video height, like YouTube; Min / Max Height limits) scales the bar evenly to fit any video size and stretches it across; design the bar with a Horizontal Box or Overlay root, not a Canvas Panel. Controls Alignment / Padding place the bar.

## Audio in the world
Audio Space World (On Diegetic Screen): a Media Box on a Diegetic Screen is heard from the screen - falloff (Inner Radius, Falloff Distance), muffled through walls (Occlusion, channel Visibility), level reverb; or give World Audio a Sound Attenuation asset. Each player hears it from their own camera (multiplayer and split screen work). Default 2D (UI).

## Instant start
Give every preview of one screen the same Warm Group name: the pool keeps the hovered or focused clip and its neighbours (or what the pointer heads for) open and paused on frame 0, so the next hover starts that frame. For one shared preview driven by buttons: Add To Media Warm Group on each button (with its clip), Set Clip (Play Now) from On Hovered, Stop from On Unhovered. Keep Media Clip Warm pins a clip. Project Settings > Plugins > Media Box: Max Warm Players (4), Warm Per Group (3), fades, Sound Class.

## Recipes
Skill tree preview: a Media Box inside each skill button, Play When Hovered Or Focused, Warm Group "SkillTree", Fit Cover. Card preview: a Media Box in the Info Card's content, Play When Visible. Trailer panel: Play When Manual, a Media cue (Play) in the panel's entrance animation, Stop in its exit; a Media Box Transport Bar Widget Blueprint (parts PlayPauseButton, ScrubBar, TimeText...) under it. Progress bar: a value track with the Media Progress input.
