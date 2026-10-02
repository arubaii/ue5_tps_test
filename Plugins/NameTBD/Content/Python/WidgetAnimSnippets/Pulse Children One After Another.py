# Every child of one panel pulses - grows and settles back - each a little after the one before.
# Set the names and numbers at the top, then Run. Running it again replaces what it made last time.

PANEL = "Row"          # the panel whose children pulse
STEP = 0.08            # seconds between one child's pulse and the next
PULSE = 0.4            # how long each pulse takes
SCALE = 1.35           # how big each pulse grows
NAME = "Pulse"         # the collapsed track it makes

panel = anim.widget(PANEL)
if panel is None:
    raise ScriptError(f"No widget called {PANEL!r}. There are: {', '.join(w.name for w in anim.widgets)}")

anim.remove(anim.collapsed(NAME))

made = []
for i, child in enumerate(panel.children):
    made.append(anim.add_track(
        TrackType.SCALE, target=child,
        start=i * STEP, duration=PULSE,
        values=(1.0, 1.0), keys=[(0.5, SCALE)],
        easing=Ease.EASE_IN_OUT))

anim.collapse(made, NAME)
print(f"{len(made)} pulses, {anim.length:.2f} s in all.")
