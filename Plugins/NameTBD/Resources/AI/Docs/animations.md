# Widget Animations

## Assets, groups and tracks
A Widget Animation (WA_ asset) is a list of tracks, each in a Layer Group; a group is one widget the animation moves (Target Widget; empty = the widget it plays on). Tracks start With Previous or After Previous plus Delay. Timing: Tween (Duration, Easing Linear / EaseIn / EaseOut / EaseInOut / Hold) or Spring (Spring: Duration and Bounce, or Stiffness / Damping / Mass - presets "@spring.Snappy", "@spring.Bouncy"...). A stagger (Expander) plays a track once per child, letter, word or line.

## Track types
Fade (StartOpacity, EndOpacity), Scale (StartScale, EndScale, Overshoot), Slide (SlideFromDirection Up/Down/Left/Right, SlideDistance), Rotate (StartAngle, EndAngle), Pulse (PulseScaleAmount), Shake, Transform (keyframes of translation / scale / angle / shear, curved motion paths), MaterialScalar / MaterialVector (ScalarParameterName, StartScalarValue, EndScalarValue...), Shape (a shape's values over time), Design (brush, colors, fonts), cues (Sound, Event, Particles, Play Animation), and registered types (TypeParams).

## Playing them
animate_widget puts an animation on a widget through its Widget Animation Component: Anim In entries play on construct or when shown; loop repeats (ping-pong); Anim ID plays from Blueprint (Play Widget Animation); Anim Out plays backwards before hiding (Set Visibility Animated). Motion recipes: fade_in, pop_in, bounce_in, slide_up_in, slide_in_left, slide_in_right, drop_in, pulse_loop, breathe_loop, float_loop, spin_loop, wiggle, shake.

## Parameters
An animation's Parameters (Float, Integer, Bool, Color, Vector 2D, Choice) let each use set values: bind a track field with "$Name" ("-$Name" mirrored). Set per entry, per component, or at play time (Play Widget Animation With Parameters, Set Widget Animation Parameter). A Choice can switch tracks on or off (Plays When).

## Editing by hand
The Widget Animations panel (Widget Blueprint editor, status bar drawer or docked) edits the same assets: Track Editor, curve editor, Selection panel, motion paths on the designer canvas, Script area (Python).
