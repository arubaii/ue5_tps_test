# Your own functions for Widget Animations scripts.
#
# This file - the project's Content/Python/WidgetAnimFunctions/my_functions.py - runs before every script, so whatever
# it defines can be used in a script by name. It is read afresh on every Run: save an edit and run again. Other .py
# files in the same folder run too, after this one.
#
# A function here sees what a script sees: anim, selection, TrackType, Ease and the rest.


def ripple(cells, columns, step=0.06, duration=0.5, shrink_to=0.0, name="Ripple"):
    """Each cell scales down and back in turn, in a diagonal wave from the top left - cells row by row, as fill_grid
    returns them. Replaces the collapsed track name if it is there. Returns the tracks."""
    anim.remove(anim.collapsed(name))
    made = []
    for i, cell in enumerate(cells):
        row, column = divmod(i, columns)
        made.append(anim.add_track(
            TrackType.SCALE, target=cell,
            start=(row + column) * step, duration=duration,
            values=(1.0, 1.0), keys=[(0.5, shrink_to)],
            easing=Ease.EASE_IN_OUT))
    anim.collapse(made, name)
    return made


def fade_in_one_by_one(widgets, step=0.08, duration=0.3, name="Fade In"):
    """Each widget fades in a little after the one before. Returns the tracks."""
    anim.remove(anim.collapsed(name))
    made = [anim.add_track(TrackType.FADE, target=w, start=i * step, duration=duration, values=(0.0, 1.0))
            for i, w in enumerate(widgets)]
    anim.collapse(made, name)
    return made
