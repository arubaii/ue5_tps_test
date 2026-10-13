# Video in UI (Media Box)

## The widget
MediaBox ("Media Box", module NameTBDMedia) plays a Media Box Clip in UMG. Clip = the video (Media Source), a Poster (still of its first frame, shown at once and whenever stopped), Start Time, Loop / Loop Start / Loop End, Audio (Auto, Embedded, Separate Sound, Silent) and Sound, Subtitles (paste SRT / VTT into Subtitle Text). Play When: Manual, Visible, Hovered, Focused, Hovered Or Focused; Hover Target (None = the button it is in); After Trigger (Stop And Rewind, Pause, Keep Playing); Hover Delay. Fit: Contain, Cover (Crop Focus), Stretch. Events: On Opened, On First Frame, On Looped, On Ended, On Error, On Subtitle Changed, On Status Changed. Functions: Play, Pause, Stop, Seek, Set Clip, Set Volume, Set Muted, Get Progress.

## Instant start
Give every preview of one screen the same Warm Group name: the pool keeps the hovered or focused clip and its neighbours (or what the pointer heads for) open and paused on frame 0, so the next hover starts that frame. For one shared preview driven by buttons: Add To Media Warm Group on each button (with its clip), Set Clip (Play Now) from On Hovered, Stop from On Unhovered. Keep Media Clip Warm pins a clip. Project Settings > Plugins > Media Box: Max Warm Players (4), Warm Per Group (3), fades, Sound Class.

## Recipes
Skill tree preview: a Media Box inside each skill button, Play When Hovered Or Focused, Warm Group "SkillTree", Fit Cover. Card preview: a Media Box in the Info Card's content, Play When Visible. Trailer panel: Play When Manual, a Media cue (Play) in the panel's entrance animation, Stop in its exit; a Media Box Transport Bar Widget Blueprint (parts PlayPauseButton, ScrubBar, TimeText...) under it. Progress bar: a value track with the Media Progress input.
