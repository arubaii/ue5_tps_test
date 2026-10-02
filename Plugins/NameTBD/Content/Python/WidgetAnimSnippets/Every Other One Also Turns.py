# Every child of one panel pulses in turn, and every other one also turns as it pulses.
# Set the names and numbers at the top, then Run. Running it again replaces what it made last time.

PANEL = "Row"          # the panel whose children animate
STEP = 0.08            # seconds between one child and the next
PULSE = 0.4            # how long each one takes
SCALE = 1.3            # how big each pulse grows
TURN = 90              # how far the odd ones turn, in degrees
NAME = "Alternating"   # the collapsed track it makes

panel = anim.widget(PANEL)
if panel is None:
    raise ScriptError(f"No widget called {PANEL!r}. There are: {', '.join(w.name for w in anim.widgets)}")

anim.remove(anim.collapsed(NAME))

made = []
for i, child in enumerate(panel.children):
    start = i * STEP
    made.append(anim.add_track(TrackType.SCALE, target=child, start=start, duration=PULSE,
                               values=(1.0, 1.0), keys=[(0.5, SCALE)], easing=Ease.EASE_IN_OUT))
    if i % 2 == 1:
        made.append(anim.add_track(TrackType.ROTATE, target=child, start=start, duration=PULSE,
                                   values=(0, TURN), easing=Ease.EASE_IN_OUT))

anim.collapse(made, NAME)
print(f"{len(made)} tracks, {anim.length:.2f} s in all.")
