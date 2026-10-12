"""Widget Animations scripting: tracks made from code.

Patterns over many widgets - eight borders each pulsing a little after the one before, every other one also
turning - are tedious by hand and don't survive a change of mind. A script makes them in one go, and changing
the count is one edit.

Run from the Script area of the Widget Animations panel, a script gets:

    anim        the animation open in the panel (an Animation)
    selection   what is selected there: selection.tracks, selection.group

and every name below (TrackType, Ease, Starts...). One run is one undoable edit - one Ctrl+Z takes back all of
it - and a run that stops with an error changes nothing.

From the editor's Python console, or a script of your own:

    import widget_anim_scripting as wa
    anim = wa.open_animation()                 # the animation in the panel used last
    anim = wa.edit_asset(unreal.load_asset("/Game/UI/WA_Spinner"))
    with anim.edit("Make spinner"):            # everything inside is one undoable edit
        ...

Seconds are the animation's own - Duration and Delay as they are typed - which the panel shows divided by the
animation's Speed Multiplier. Changing one track never moves another: every other track keeps its time, as in
the panel.

print_api_reference() prints the whole API.
"""

import glob
import linecache
import os
import re
import sys
import traceback

import unreal

SCRIPT_FILENAME = "<Script>"


class ScriptError(Exception):
    """An edit the animation refused, saying why - a widget that isn't there, a field a track doesn't have."""


# =====================================================================================================
# Names
#
# Plain strings, so a value typed by hand ("EaseOut", "Ease Out", "ease_out") works just as well: the plugin
# matches names loosely, ignoring case, spaces and underscores.
# =====================================================================================================


class TrackType:
    """The built-in track types. Registered and Blueprint types go by their name in the Add Track menu
    ("Count", "Text Reveal", a Blueprint type's name) - anim.track_types lists them all."""
    FADE = "Fade"
    SCALE = "Scale"
    SLIDE = "Slide"
    ROTATE = "Rotate"
    PULSE = "Pulse"
    SHAKE = "Shake"
    MATERIAL_SCALAR = "MaterialScalar"
    MATERIAL_VECTOR = "MaterialVector"
    TRANSFORM = "Transform"


class Ease:
    """A track's Easing."""
    LINEAR = "Linear"
    EASE_IN = "EaseIn"
    EASE_OUT = "EaseOut"
    EASE_IN_OUT = "EaseInOut"
    CUSTOM = "Custom"
    HOLD = "Hold"  # stays on its start, then jumps to its end the moment it arrives
    IN = EASE_IN
    OUT = EASE_OUT
    IN_OUT = EASE_IN_OUT


class Curve:
    """How a stretch between two keys moves (a key's Curve To Next)."""
    TRACK = "Track"
    LINEAR = "Linear"
    EASE_IN = "EaseIn"
    EASE_OUT = "EaseOut"
    EASE_IN_OUT = "EaseInOut"
    HOLD = "Hold"
    CUSTOM = "Custom"
    BEZIER = "Bezier"  # shaped by its LeaveHandle / ArriveHandle, as dragged in the Graph Editor


class Starts:
    """Whether a track starts with the track above it or after it."""
    WITH_PREVIOUS = "WithPrevious"
    AFTER_PREVIOUS = "AfterPrevious"


class Timing:
    TWEEN = "Tween"
    SPRING = "Spring"
    VALUE = "Value"


class Direction:
    """Where a Slide comes from."""
    UP = "Up"
    DOWN = "Down"
    LEFT = "Left"
    RIGHT = "Right"


class Stagger:
    """What a Stagger plays the track once for."""
    EACH_CHILD = "EachChild"
    EACH_TEXT_PART = "EachTextPart"


class Spacing:
    """How a Stagger spaces its items."""
    SECONDS_APART = "Each"
    SPREAD_OVER = "Across"


class Order:
    """The order a Stagger's items take their turns in."""
    FIRST_TO_LAST = "FirstToLast"
    LAST_TO_FIRST = "LastToFirst"
    FROM_THE_MIDDLE = "FromCenter"
    FROM_BOTH_ENDS = "FromEdges"
    RANDOM = "Random"


class Falloff:
    """How a Shake's strength changes along it."""
    STEADY = "Steady"
    FADE_OUT = "FadeOut"
    FADE_IN = "FadeIn"
    FADE_IN_AND_OUT = "FadeInOut"


class Smoothing:
    """How a value-driven track travels to each new value. IMMEDIATE is kept for old scripts: it is TWEEN with Hold
    (Constant) easing, which jumps to each new value the same way - the editor no longer offers it."""
    IMMEDIATE = "Immediate"
    TWEEN = "Tween"
    SPRING = "Spring"
    STEADY = "Steady"


class ParamType:
    """What an Animation Parameter holds - anim.add_parameter("Distance", ParamType.FLOAT, 40)."""
    FLOAT = "Float"
    INTEGER = "Integer"
    BOOL = "Bool"
    COLOR = "Color"
    VECTOR2D = "Vector2D"
    CHOICE = "Choice"


# =====================================================================================================
# Values to and from the plugin
#
# Everything crosses as text in Unreal's own property format, so any field of any track - a registered type's
# settings, a Blueprint struct's fields - can be set without the plugin knowing it.
# =====================================================================================================

_NUMBER_TYPES = (int, float)


def _is_number(value):
    return isinstance(value, _NUMBER_TYPES) and not isinstance(value, bool)


def _text(value):
    """A Python value as Unreal property text."""
    if value is None:
        return "None"
    if isinstance(value, bool):
        return "True" if value else "False"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        return repr(value)
    if isinstance(value, str):
        return value
    if isinstance(value, Widget):
        return value.path
    if isinstance(value, (Track, Group)):
        raise ScriptError("A track or group can't be stored in a field.")
    if isinstance(value, (tuple, list)):
        if value and all(_is_number(v) for v in value):
            # Two numbers are a 2D vector, three or four a color: the only small structs tracks hold.
            if len(value) == 2:
                return "(X=%r,Y=%r)" % (float(value[0]), float(value[1]))
            if len(value) in (3, 4):
                alpha = float(value[3]) if len(value) == 4 else 1.0
                return "(R=%r,G=%r,B=%r,A=%r)" % (float(value[0]), float(value[1]), float(value[2]), alpha)
        raise ScriptError("Can't turn %r into a value: give two numbers for a 2D vector, three or four for a color." % (value,))
    export_text = getattr(value, "export_text", None)
    if callable(export_text):
        return export_text()
    get_path_name = getattr(value, "get_path_name", None)
    if callable(get_path_name):
        return get_path_name()
    name = getattr(value, "name", None)
    if isinstance(name, str):
        return name
    return str(value)


_FIELD_PATTERN = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)=([-+0-9.eE]+)")


def _from_text(text, kind):
    """Unreal property text as a Python value, by the kind of field it came from."""
    if kind == "bool":
        return text.strip().lower() == "true"
    if kind == "int":
        try:
            return int(text)
        except ValueError:
            return text
    if kind == "float":
        try:
            return float(text)
        except ValueError:
            return text
    if kind in ("struct:LinearColor", "struct:Vector2D", "struct:Vector", "struct:Color"):
        fields = dict((k, float(v)) for k, v in _FIELD_PATTERN.findall(text))
        order = {"struct:LinearColor": "RGBA", "struct:Color": "RGBA", "struct:Vector2D": "XY", "struct:Vector": "XYZ"}[kind]
        return tuple(fields.get(axis, 0.0) for axis in order)
    if kind == "object" and text in ("None", ""):
        return None
    return text


def _segment_curve_text(curve):
    """A key's Curve To Next, from a Curve name or a Curve asset."""
    if curve is None:
        return "(Easing=Track)"
    if isinstance(curve, str):
        return "(Easing=%s)" % _curve_name(curve)
    # A Curve asset: a Custom segment following it.
    return "(Easing=Custom,CustomCurve=%s)" % _text(curve)


_CURVE_NAMES = {
    "track": "Track", "trackeasing": "Track",
    "linear": "Linear",
    "easein": "EaseIn", "in": "EaseIn",
    "easeout": "EaseOut", "out": "EaseOut",
    "easeinout": "EaseInOut", "inout": "EaseInOut",
    "hold": "Hold",
    "custom": "Custom",
}


def _curve_name(name):
    # Inside struct text an enum must be its exact name, so loose names are matched here rather than by the plugin.
    key = re.sub(r"[\s_]", "", name).lower()
    if key not in _CURVE_NAMES:
        raise ScriptError("No curve is called %r. Use one of: %s." % (name, ", ".join(sorted(set(_CURVE_NAMES.values())))))
    return _CURVE_NAMES[key]


def _key_text(key):
    """One key between a track's start and end, from (time, value[, curve]) or a dict."""
    if isinstance(key, dict):
        time = key.get("time")
        value = key.get("value")
        color = key.get("color")
        curve = key.get("curve")
    else:
        if len(key) < 2:
            raise ScriptError("A key is (time, value) or (time, value, curve), not %r." % (key,))
        time, value = key[0], key[1]
        curve = key[2] if len(key) > 2 else None
        color = None
        if isinstance(value, (tuple, list)) and len(value) != 2:
            color, value = value, None
    if not _is_number(time):
        raise ScriptError("A key's time is a fraction of the track, 0 to 1 - not %r." % (time,))
    parts = ["Time=%r" % float(time)]
    if isinstance(value, (tuple, list)):
        # Two numbers: a Scale key's X and Y.
        parts.append("Value=%r" % float(value[0]))
        parts.append("ValueY=%r" % float(value[1]))
    elif value is not None:
        # One number: on a Scale, both X and Y.
        parts.append("Value=%r" % float(value))
        parts.append("ValueY=%r" % float(value))
    if color is not None:
        parts.append("Color=%s" % _text(color))
    parts.append("CurveToNext=%s" % _segment_curve_text(curve))
    return "(" + ",".join(parts) + ")"


def _target_path(target):
    if target is None:
        return ""
    if isinstance(target, Widget):
        return target.path
    if isinstance(target, str):
        return target
    raise ScriptError("A target is a widget's name, its path or a Widget - not %r." % (target,))


# =====================================================================================================
# Widgets
# =====================================================================================================


class Widget:
    """A widget of the Widget Blueprint the panel previews on. Usable anywhere a target is asked for, and str() of it
    is its path, so f-strings work too.

    It can be changed as the designer changes it - its properties (set), its slot in its parent (set_slot), copied,
    moved or removed - and widgets added inside it (add). Widgets inside a nested User Widget belong to that Widget
    Blueprint: they can be read and animated here, not changed."""

    def __init__(self, anim, path):
        self._anim = anim
        self.path = path

    @property
    def name(self):
        """Its own name - the last part of its path."""
        return self.path.rsplit("/", 1)[-1]

    @property
    def class_name(self):
        return self._anim._raw.get_widget_class(self.path)

    @property
    def parent(self):
        path = self._anim._raw.get_widget_parent(self.path)
        return Widget(self._anim, path) if path else None

    @property
    def children(self):
        """Its children, in order - a panel's slots, a border's content."""
        return [Widget(self._anim, p) for p in self._anim._raw.get_widget_children(self.path)]

    def descendants(self):
        """Every widget inside it, depth first."""
        result = []
        for child in self.children:
            result.append(child)
            result.extend(child.descendants())
        return result

    @property
    def index(self):
        """Its place among its parent's children, from 0."""
        return self._anim._raw.get_widget_index(self.path)

    @property
    def tracks(self):
        """The tracks that move this widget."""
        return self._anim.find(target=self)

    # ---- its properties ------------------------------------------------------------------------

    def get(self, path):
        """A property's value, by name or dotted path: "Visibility", "BrushColor", "Background.TintColor"."""
        return self._anim._widget_get(self.path, path, slot=False)

    def set(self, fields=None, **named):
        """Sets properties by name - border.set(BrushColor=(1, 0, 0), Padding="(Left=4,Top=4,Right=4,Bottom=4)") - or by
        dotted path: set({"Background.TintColor": ...}). Names are matched loosely. Returns the widget."""
        self._anim._widget_set(self.path, fields, named, slot=False)
        return self

    def fields(self, path=""):
        """The names of its properties at path - its own with none."""
        return list(self._anim._raw.get_widget_property_names(self.path, path, False))

    @property
    def slot(self):
        """Its slot in its parent - Row and Column in a Uniform Grid, Padding and alignment in a box. None at the top."""
        return Slot(self) if self._anim._raw.get_slot_class(self.path) else None

    def set_slot(self, fields=None, **named):
        """Sets properties of its slot: cell.set_slot(Row=1, Column=2), item.set_slot(Padding=8). Returns the widget."""
        self._anim._widget_set(self.path, fields, named, slot=True)
        return self

    # ---- the widget itself ---------------------------------------------------------------------

    def add(self, type, name=None, index=None, **properties):
        """A new widget inside this one - a panel's child, a border's content: add("Border"), add("TextBlock",
        Text="Hi"), a Widget Blueprint by name ("WBP_Card") or path. Returns it."""
        return self._anim.add_widget(type, into=self, name=name, index=index, **properties)

    def copy(self, count=1, into=None, index=None, name=None):
        """Copies of this widget - everything inside it, and its slot's settings, included - added to its parent, or
        into another panel. One copy is returned as a Widget, several as a list."""
        copies = self._anim.copy_widget(self, count=count, into=into, index=index, name=name)
        return copies[0] if count == 1 else copies

    def move(self, into=None, index=None):
        """Moves it into another panel, or to index among its parent's children. Returns it."""
        self._anim.move_widget(self, into=into, index=index)
        return self

    def remove(self, remove_tracks=True):
        """Removes it and everything inside it - and, with remove_tracks, the tracks that moved them."""
        self._anim.remove_widget(self, remove_tracks=remove_tracks)

    def __eq__(self, other):
        return isinstance(other, Widget) and other.path == self.path

    def __hash__(self):
        return hash(self.path)

    def __str__(self):
        return self.path

    def __repr__(self):
        return "<Widget %s (%s)>" % (self.path, self.class_name)


class Slot:
    """A widget's slot in its parent panel: slot["Row"], slot["Row"] = 2, slot.set(Row=2, Column=1)."""

    def __init__(self, widget):
        self._widget = widget

    @property
    def class_name(self):
        """What kind of slot - UniformGridSlot, HorizontalBoxSlot, CanvasPanelSlot..."""
        return self._widget._anim._raw.get_slot_class(self._widget.path)

    def get(self, path):
        return self._widget._anim._widget_get(self._widget.path, path, slot=True)

    def set(self, fields=None, **named):
        self._widget.set_slot(fields, **named)
        return self

    def fields(self, path=""):
        return list(self._widget._anim._raw.get_widget_property_names(self._widget.path, path, True))

    def __getitem__(self, path):
        return self.get(path)

    def __setitem__(self, path, value):
        self.set({path: value})

    def __repr__(self):
        return "<Slot %s of %s>" % (self.class_name, self._widget.path)


# =====================================================================================================
# Groups
# =====================================================================================================


class Group:
    """A Track Group: one widget the animation moves, and the tracks that move it."""

    def __init__(self, anim, index):
        self._anim = anim
        self._index = index

    @property
    def index(self):
        if self._index < 0:
            raise ScriptError("This group was removed.")
        return self._index

    @property
    def name(self):
        return self._anim._raw.get_group_name(self.index)

    @name.setter
    def name(self, value):
        self._anim._check(self._anim._raw.set_group_name(self.index, _text(value)))

    @property
    def target(self):
        """Its Preview Target's path, or None for the widget the animation plays on."""
        return self._anim._raw.get_group_target(self.index) or None

    @target.setter
    def target(self, value):
        self._anim._check(self._anim._raw.set_group_target(self.index, _target_path(value)))

    @property
    def folder(self):
        """The List Folder the group is shown under in the panel's Animation Tracks list, or None - not a folder on disk
        or in the Content Browser. Only the panel reads it."""
        return self._anim._raw.get_group_folder(self.index) or None

    @folder.setter
    def folder(self, value):
        self._anim._check(self._anim._raw.set_group_folder(self.index, _text(value) if value else ""))

    @property
    def tracks(self):
        return [t for t in self._anim.tracks if t._raw_group() == self.index]

    def add_track(self, type, **settings):
        """A new track in this group. See Animation.add_track."""
        return self._anim.add_track(type, group=self, **settings)

    def remove(self):
        """Removes the group and its tracks. Every other track keeps its time."""
        self._anim.remove_group(self)

    def __eq__(self, other):
        return isinstance(other, Group) and other._anim is self._anim and other._index == self._index

    def __hash__(self):
        return hash((id(self._anim), self._index))

    def __repr__(self):
        if self._index < 0:
            return "<Group (removed)>"
        return "<Group %d %r on %s>" % (self._index, self.name, self.target or "the widget itself")


# =====================================================================================================
# Tracks
# =====================================================================================================


class Track:
    """One track of the animation. Handles keep pointing at their track as tracks are added and removed through
    the script; an edit made in the panel meanwhile can leave one pointing elsewhere, so read anim.tracks again
    after one."""

    def __init__(self, anim, index):
        self._anim = anim
        self._index = index

    # ---- identity ------------------------------------------------------------------------------

    @property
    def index(self):
        """Its place in the animation's one list of tracks - the order they play in."""
        if self._index < 0:
            raise ScriptError("This track was removed.")
        return self._index

    @property
    def anim(self):
        return self._anim

    @property
    def type(self):
        """Its type's name: "Fade", "Count", a Blueprint type's name - or, for another animation inserted as a track,
        that animation's name."""
        return self._anim._raw.get_track_type(self.index)

    @property
    def name(self):
        """The name the track was given, or None while it is called by its type. Setting one numbers it ("Pop 2") if
        another renamed track has it; None names it by its type again."""
        return self._anim._raw.get_track_name(self.index) or None

    @name.setter
    def name(self, value):
        given = self._anim._raw.set_track_name(self.index, _text(value) if value else "")
        self._anim._check(bool(given) or not value)

    def _raw_group(self):
        return self._anim._raw.get_track_group(self.index)

    @property
    def group(self):
        """Its Track Group - None in a plain list of tracks, which has none."""
        if not self._anim.has_groups:
            return None
        return self._anim._group(self._raw_group())

    @group.setter
    def group(self, value):
        index = value.index if isinstance(value, Group) else int(value)
        self._anim._check(self._anim._raw.set_track_group(self.index, index))

    @property
    def target(self):
        """The path of the widget it moves - its group's - or None for the widget the animation plays on."""
        return self._anim._raw.get_track_target(self.index) or None

    @target.setter
    def target(self, value):
        """Moves it to the group for that widget, made if there is none (in a plain list, sets its Target Widget)."""
        self._anim._check(self._anim._raw.set_track_target(self.index, _target_path(value)))

    # ---- time ----------------------------------------------------------------------------------

    @property
    def start(self):
        """Where it starts, in seconds from the start of the animation."""
        return self._anim._raw.get_track_start(self.index)

    @start.setter
    def start(self, seconds):
        self._anim._check(self._anim._raw.set_track_start(self.index, float(seconds)))

    @property
    def length(self):
        """How long it runs, a Stagger's items included."""
        return self._anim._raw.get_track_length(self.index)

    @property
    def end(self):
        return self.start + self.length

    @property
    def duration(self):
        return self.get("Duration")

    @duration.setter
    def duration(self, seconds):
        self.set(Duration=float(seconds))

    @property
    def delay(self):
        return self.get("Delay")

    @delay.setter
    def delay(self, seconds):
        self.set(Delay=float(seconds))

    @property
    def starts(self):
        return self.get("Start")

    @starts.setter
    def starts(self, value):
        self.set(Start=value)

    # ---- the common fields ------------------------------------------------------------------------

    easing = property(lambda self: self.get("Easing"), lambda self, v: self.set(Easing=v))
    timing = property(lambda self: self.get("Timing"), lambda self, v: self.set(Timing=v))
    invert = property(lambda self: self.get("bInvertTrack"), lambda self, v: self.set(bInvertTrack=bool(v)))
    hidden = property(lambda self: self.get("bHiddenInEditor"), lambda self, v: self.set(bHiddenInEditor=bool(v)))
    pinned = property(lambda self: self.get("bPinnedInEditor"), lambda self, v: self.set(bPinnedInEditor=bool(v)))
    locked = property(lambda self: self.get("bLockedInEditor"), lambda self, v: self.set(bLockedInEditor=bool(v)))
    value_name = property(lambda self: self.get("ValueName"), lambda self, v: self.set(ValueName=v))

    @property
    def collapsed(self):
        """The name of the collapsed track it is folded into, or None. Setting a name folds it into that one."""
        name = self.get("CollapsedTrack")
        return None if name in ("", "None") else name

    @collapsed.setter
    def collapsed(self, name):
        self.set(CollapsedTrack=name or "None")

    @property
    def stagger(self):
        """Its Stagger's kind ("EachChild"...) or None. Set a kind - or None - to change it; see set_stagger."""
        return self._struct_kind("Expander")

    @stagger.setter
    def stagger(self, kind):
        self.set_stagger(kind)

    def _struct_kind(self, path):
        kind = self._anim._raw.get_track_property_kind(self.index, path)
        name = kind.split(":", 1)[1] if kind.startswith("instanced:") else ""
        return name or None

    # ---- any field ----------------------------------------------------------------------------

    def get(self, path):
        """A field's value, by its name or a dotted path: "Duration", "Spring.Bounce", "ValueKeys.Keys[0].Value".
        A registered type's own settings are found by name too: count.get("EndValue")."""
        raw = self._anim._raw
        text = raw.get_track_property(self.index, path)
        kind = raw.get_track_property_kind(self.index, path)
        if not kind:
            raise ScriptError(raw.get_last_error())
        return _from_text(text, kind)

    def set(self, fields=None, **named):
        """Sets fields by name - track.set(Duration=0.4, SlideFromDirection="Left"), snake_case works too
        (slide_from_direction) - or by dotted path: track.set({"Spring.Bounce": 0.3}). Returns the track."""
        values = dict(fields or {})
        values.update(named)
        for path, value in values.items():
            self._anim._check(self._anim._raw.set_track_property(self.index, path, _text(value)))
        return self

    def fields(self, path=""):
        """The names of the fields at path - the track's own with none - for finding what can be set."""
        return list(self._anim._raw.get_track_property_names(self.index, path))

    def configure(self, **settings):
        """Several settings at once - every keyword add_track takes. Returns the track."""
        self._anim._configure(self, settings)
        return self

    # ---- values and keys ------------------------------------------------------------------------

    _VALUE_FIELDS = {
        "fade": ("StartOpacity", "EndOpacity"),
        "scale": ("StartScale", "EndScale"),
        "rotate": ("StartAngle", "EndAngle"),
        "materialscalar": ("StartScalarValue", "EndScalarValue"),
        "materialvector": ("StartVectorValue", "EndVectorValue"),
    }

    def set_values(self, from_=None, to=None):
        """Where it starts and ends: an opacity on a Fade, a scale on a Scale - one number, or (x, y) - an angle on a Rotate, a color on a
        Material Vector, a registered type's Start and End Value (or Color). On a Slide, from_ is how far away it
        starts; it always ends where the widget is. Leave one out to keep it. Returns the track."""
        type_key = re.sub(r"[\s_]", "", self.get("AnimType")).lower()
        if type_key == "slide":
            if to not in (None, 0, 0.0):
                raise ScriptError("A Slide always ends where the widget is; give how far away it starts as from_, and its direction with set(SlideFromDirection=...).")
            if from_ is not None:
                self.set(SlideDistance=float(from_))
            return self
        if type_key in ("pulse", "shake"):
            raise ScriptError("A %s has no start and end values; set its strength with set(...) - track.fields() lists them." % self.get("AnimType"))
        if type_key == "transform":
            raise ScriptError("A Transform track is set through its keys: track.set({\"Transform....\": ...}) - track.fields(\"Transform\") lists them.")
        if type_key == "scale":
            # X and Y apart: one number scales both alike, (x, y) each its own.
            from_ = (from_, from_) if _is_number(from_) else from_
            to = (to, to) if _is_number(to) else to
        if type_key in self._VALUE_FIELDS:
            start_field, end_field = self._VALUE_FIELDS[type_key]
        else:
            # A registered type: its own Start and End Value, or Start and End Color when given colors.
            is_color = isinstance(from_ if from_ is not None else to, (tuple, list)) or hasattr(from_ if from_ is not None else to, "export_text")
            start_field, end_field = ("StartColor", "EndColor") if is_color else ("StartValue", "EndValue")
        if from_ is not None:
            self.set({start_field: from_})
        if to is not None:
            self.set({end_field: to})
        return self

    def set_keys(self, keys):
        """Replaces the keys between its start and end: [(time, value), (time, value, curve), ...] - time a fraction
        of the track, 0 to 1; value a color (r, g, b[, a]) on a Material Vector; curve how it moves on to the next
        key (a Curve name or a Curve asset). Dicts with time, value, color, curve work too. Returns the track."""
        keys = sorted(keys, key=lambda k: k["time"] if isinstance(k, dict) else k[0])
        text = "(" + ",".join(_key_text(k) for k in keys) + ")"
        return self.set({"ValueKeys.Keys": text})

    def clear_keys(self):
        return self.set({"ValueKeys.Keys": "()"})

    # ---- Animation Parameters -------------------------------------------------------------------

    def bind(self, path, parameter, scale=1.0, offset=0.0, mirrored=False):
        """Binds a field to one of the animation's parameters: track.bind("SlideDistance", "Distance") is
        "Slide.Distance = $Distance" - the field plays the parameter's value, times scale, plus offset (mirrored: scale
        -1, for the other side). track.set(SlideDistance="$Distance") does the same ("-$Distance" mirrored). Returns
        the track."""
        name = _text(parameter)
        if name.startswith("$"):
            name = name[1:]
        if mirrored:
            scale = -scale
        self._anim._check(self._anim._raw.bind_track_field(self.index, path, name, float(scale), float(offset)))
        return self

    def unbind(self, path):
        """Lets a field go of its parameter: it plays its own value again. Returns the track."""
        self._anim._check(self._anim._raw.bind_track_field(self.index, path, "", 1.0, 0.0))
        return self

    def binding(self, path):
        """The parameter a field is bound to, or None while it plays its own value."""
        return self._anim._raw.get_track_field_binding(self.index, path) or None

    def plays_when(self, parameter, options=None):
        """Plays the track only for some options of a Choice parameter - plays_when("Side", ["Left"]) - or, for a
        Bool, while it is true (options=["False"]: while it is false). None: it always plays. Returns the track."""
        name = "" if parameter is None else _text(parameter)
        self._anim._check(self._anim._raw.set_track_plays_when(self.index, name, [_text(o) for o in (options or [])]))
        return self

    # ---- Stagger and Value Input ---------------------------------------------------------------

    def set_stagger(self, kind, **fields):
        """Plays the track once per child (Stagger.EACH_CHILD) or per letter, word or line (Stagger.EACH_TEXT_PART),
        with its fields set: set_stagger(Stagger.EACH_CHILD, Interval=0.05, Order=Order.FROM_THE_MIDDLE).
        None removes it. Returns the track."""
        return self._set_struct("Expander", kind, fields)

    def set_input(self, kind, **fields):
        """A value-driven track's Value Input - "Pointer Position", "Hover, Press or Focus", "Drag", "Scroll" - with
        its fields set. None: set from gameplay. Returns the track."""
        return self._set_struct("ValueInput", kind, fields)

    def _set_struct(self, path, kind, fields):
        raw = self._anim._raw
        self._anim._check(raw.set_track_struct(self.index, path, "" if kind is None else _text(kind)))
        for name, value in fields.items():
            self._anim._check(raw.set_track_property(self.index, "%s.%s" % (path, name), _text(value)))
        return self

    # ---- the track itself ----------------------------------------------------------------------

    def remove(self):
        """Removes the track. Every other track keeps its time."""
        self._anim.remove(self)

    def duplicate(self):
        """A copy, right after it, starting with it. Returns the copy."""
        return self._anim.duplicate(self)

    def __eq__(self, other):
        return isinstance(other, Track) and other._anim is self._anim and other._index == self._index

    def __hash__(self):
        return hash((id(self._anim), self._index))

    def __repr__(self):
        if self._index < 0:
            return "<Track (removed)>"
        return "<Track %d %s on %s, %.2f-%.2fs>" % (self._index, self.type, self.target or "the widget itself", self.start, self.end)


# =====================================================================================================
# Selection
# =====================================================================================================


class Selection:
    """What was selected in the panel when the script started."""

    def __init__(self, tracks, group):
        self.tracks = tracks
        self.group = group

    def __iter__(self):
        return iter(self.tracks)

    def __len__(self):
        return len(self.tracks)

    def __bool__(self):
        return bool(self.tracks) or self.group is not None

    def __repr__(self):
        return "<Selection %d tracks, group %r>" % (len(self.tracks), self.group)


# =====================================================================================================
# The animation
# =====================================================================================================


class _EditScope:
    def __init__(self, anim, description):
        self._anim = anim
        self._description = description

    def __enter__(self):
        self._anim._check(self._anim._raw.begin_edit(self._description))
        return self._anim

    def __exit__(self, exc_type, exc, tb):
        self._anim._raw.end_edit()
        return False


class Animation:
    """The animation a script edits: its groups, its tracks, and the widgets they can move."""

    def __init__(self, raw):
        if raw is None:
            raise ScriptError("There is no animation to edit: open one in the Widget Animations panel.")
        self._raw = raw
        # Handles by index, kept in step with every add and remove made here, so a Track held by the script keeps
        # meaning the same track.
        self._tracks = {}
        self._groups = {}

    # ---- plumbing --------------------------------------------------------------------------------

    @property
    def raw(self):
        """The plugin's own object (unreal.WidgetAnimScriptAnimation), for anything not wrapped here."""
        return self._raw

    def _check(self, ok):
        if not ok:
            raise ScriptError(self._raw.get_last_error() or "The animation refused the edit.")

    def _track(self, index):
        handle = self._tracks.get(index)
        if handle is None:
            handle = self._tracks[index] = Track(self, index)
        return handle

    def _group(self, index):
        if index < 0:
            return None
        handle = self._groups.get(index)
        if handle is None:
            handle = self._groups[index] = Group(self, index)
        return handle

    @staticmethod
    def _shift(handles, removed=(), inserted_at=None):
        """Renumbers handles after some were removed, or one went in at inserted_at."""
        removed = sorted(set(removed))
        moved = {}
        for index, handle in handles.items():
            if index in removed:
                handle._index = -1
                continue
            new_index = index - sum(1 for r in removed if r < index)
            if inserted_at is not None and new_index >= inserted_at:
                new_index += 1
            handle._index = new_index
            moved[new_index] = handle
        handles.clear()
        handles.update(moved)

    def edit(self, description="Edit Widget Animation"):
        """Everything done inside `with anim.edit("..."):` is one undoable edit. A script run from the panel is one
        already."""
        return _EditScope(self, description)

    # ---- about it ----------------------------------------------------------------------------------

    @property
    def name(self):
        return self._raw.get_animation_name()

    @property
    def editable(self):
        """False while the panel is locked, or what it shows can't be edited."""
        return self._raw.is_editable()

    @property
    def has_groups(self):
        """False for a plain list of tracks - a component's Additive Anim Layers - where each track names its widget."""
        return self._raw.supports_groups()

    @property
    def speed_multiplier(self):
        return self._raw.get_speed_multiplier()

    @property
    def length(self):
        """Seconds one pass takes: where the last track ends, or the Minimum Length if that is longer."""
        return self._raw.get_length()

    @property
    def track_types(self):
        """Every type add_track takes: the built-in ones, then registered and Blueprint types by name."""
        return list(self._raw.get_track_types())

    def kinds(self, field):
        """What a field that holds a struct of a type to pick can hold: kinds("Expander") lists the Staggers,
        kinds("ValueInput") the Value Inputs."""
        return list(self._raw.get_track_struct_options(field))

    # ---- groups ------------------------------------------------------------------------------------

    @property
    def groups(self):
        return [self._group(i) for i in range(self._raw.get_num_groups())]

    def group(self, name_or_index):
        """A group by its name (as shown) or position, or None."""
        if isinstance(name_or_index, int):
            return self._group(name_or_index) if 0 <= name_or_index < self._raw.get_num_groups() else None
        wanted = str(name_or_index).strip().lower()
        for group in self.groups:
            if group.name.strip().lower() == wanted:
                return group
        return None

    def add_group(self, name="", target=None):
        """A new Track Group, moving target (a widget's name, path or Widget; None for the widget itself)."""
        index = self._raw.add_group(_text(name), _target_path(target))
        self._check(index >= 0)
        return self._group(index)

    def group_for(self, target, create=True, name=None):
        """The group moving target - the first one whose Preview Target it is - made (named after the widget, or
        name) when there is none and create is True."""
        path = self._resolve(target)
        for group in self.groups:
            if (group.target or "").lower() == path.lower():
                return group
        if not create:
            return None
        return self.add_group(name if name is not None else (path.rsplit("/", 1)[-1] if path else ""), path)

    def remove_group(self, group):
        """Removes a group and every track in it. Every other track keeps its time."""
        index = group.index if isinstance(group, Group) else int(group)
        track_indices = [t.index for t in self.tracks if t._raw_group() == index]
        self._check(self._raw.remove_group(index))
        self._shift(self._tracks, removed=track_indices)
        self._shift(self._groups, removed=[index])

    # ---- tracks ------------------------------------------------------------------------------------

    @property
    def tracks(self):
        """Every track, in the order they play."""
        return [self._track(i) for i in range(self._raw.get_num_tracks())]

    def track(self, index):
        if not 0 <= index < self._raw.get_num_tracks():
            raise ScriptError("There is no track %d; the animation has %d." % (index, self._raw.get_num_tracks()))
        return self._track(index)

    def find(self, type=None, target=None, group=None, collapsed=None, name=None):
        """The tracks matching everything given: a type name, a target widget, a group, a collapsed track's name, the
        name a track was given."""
        result = []
        type_key = re.sub(r"[\s_]", "", type).lower() if type else None
        target_path = self._resolve(target).lower() if target is not None else None
        group_index = (group.index if isinstance(group, Group) else group) if group is not None else None
        for track in self.tracks:
            if type_key and re.sub(r"[\s_]", "", track.type).lower() != type_key:
                continue
            if target_path is not None and (track.target or "").lower() != target_path:
                continue
            if group_index is not None and track._raw_group() != group_index:
                continue
            if collapsed is not None and track.collapsed != collapsed:
                continue
            if name is not None and (track.name or "").lower() != str(name).strip().lower():
                continue
            result.append(track)
        return result

    def add_track(self, type, group=None, target=None, **settings):
        """A new track at the end of the animation, after the track before it.

        type      a TrackType, or a registered or Blueprint type's name ("Count")
        target    the widget it moves - a name, path or Widget: put in the group moving that widget, made if there is
                  none (in a plain list, its Target Widget). Or give group= a Group.
        settings  start (seconds from the start of the animation), delay, starts, duration, easing, timing,
                  from_, to (or values=(from, to)), keys, stagger (a kind, or (kind, {fields})), invert, hidden,
                  pinned, locked, collapsed, name, value_name - and any other field by name: SlideFromDirection="Left".
        """
        group_index = -1
        if self.has_groups:
            if target is not None:
                preferred = group if isinstance(group, Group) else None
                path = self._resolve(target)
                if preferred is not None and (preferred.target or "").lower() == path.lower():
                    group_index = preferred.index
                else:
                    group_index = self.group_for(path).index
            elif group is not None:
                group_index = group.index if isinstance(group, Group) else int(group)

        index = self._raw.add_track(_text(type), group_index)
        self._check(index >= 0)
        track = self._track(index)

        if target is not None and not self.has_groups:
            track.target = target
        self._configure(track, settings)
        return track

    def _configure(self, track, settings):
        settings = dict(settings)
        # Placement first, then the start - which overrides a delay - once the track's own length is known.
        start = settings.pop("start", None)
        values = settings.pop("values", None)
        from_ = settings.pop("from_", None)
        to = settings.pop("to", None)
        if values is not None:
            from_, to = values
        keys = settings.pop("keys", None)
        stagger = settings.pop("stagger", None)
        simple = {
            "starts": "Start", "delay": "Delay", "duration": "Duration", "easing": "Easing", "timing": "Timing",
            "invert": "bInvertTrack", "hidden": "bHiddenInEditor", "pinned": "bPinnedInEditor",
            "locked": "bLockedInEditor", "value_name": "ValueName",
        }
        # Timing before the fields that depend on it, so a spring's or a value's own fields exist to be set.
        if "timing" in settings:
            track.set(Timing=settings.pop("timing"))
        collapsed = settings.pop("collapsed", None)
        locked = settings.pop("locked", None)
        name = settings.pop("name", None)
        if name is not None:
            track.name = name
        for key in list(settings.keys()):
            if key in simple:
                track.set({simple[key]: settings.pop(key)})
        if settings:
            track.set(settings)
        if from_ is not None or to is not None:
            track.set_values(from_, to)
        if keys is not None:
            track.set_keys(keys)
        if stagger is not None:
            if isinstance(stagger, (tuple, list)):
                track.set_stagger(stagger[0], **(stagger[1] if len(stagger) > 1 else {}))
            else:
                track.set_stagger(stagger)
        if start is not None:
            track.start = start
        if collapsed is not None:
            track.collapsed = collapsed
        # Locked last: a locked track takes no more edits.
        if locked is not None:
            track.locked = locked

    def duplicate(self, track):
        """A copy of track right after it, starting with it. Returns the copy."""
        index = self._raw.duplicate_track(track.index)
        self._check(index >= 0)
        self._shift(self._tracks, inserted_at=index)
        return self._track(index)

    def remove(self, *tracks):
        """Removes tracks - remove(a, b) or remove(list). Every other track keeps its time."""
        flat = []
        for item in tracks:
            if isinstance(item, Track):
                flat.append(item)
            else:
                flat.extend(item)
        indices = sorted(set(t.index for t in flat))
        if not indices:
            return
        self._check(self._raw.remove_tracks(indices))
        self._shift(self._tracks, removed=indices)

    def clear(self, group=None):
        """Removes every track - or every track of one group."""
        self.remove(self.tracks if group is None else self.find(group=group))

    # ---- collapsed tracks ----------------------------------------------------------------------------

    def collapse(self, tracks, name="Collapsed Tracks"):
        """Folds tracks into one collapsed track, as the panel's Collapse does. Returns its name - numbered if another
        collapsed track already has the one asked for."""
        indices = [t.index for t in tracks]
        result = self._raw.collapse_tracks(indices, _text(name))
        self._check(bool(result))
        return result

    def expand(self, name):
        """Lets a collapsed track's tracks out again."""
        self._check(self._raw.expand_collapsed(_text(name)))

    @property
    def collapsed_names(self):
        return list(self._raw.get_collapsed_names())

    def collapsed(self, name):
        """The tracks folded into the collapsed track name."""
        return [self._track(i) for i in self._raw.get_collapsed_tracks(_text(name))]

    # ---- Animation Parameters -----------------------------------------------------------------------

    _PARAMETER_KINDS = {
        "Float": "float", "Integer": "int", "Bool": "bool", "Color": "struct:LinearColor", "Vector2D": "struct:Vector2D", "Choice": "name",
    }

    @property
    def parameters(self):
        """The animation's parameters: a dict of each name to its default."""
        return dict((name, self.parameter_default(name)) for name in self._raw.get_parameter_names())

    def add_parameter(self, name, type=ParamType.FLOAT, default=None, options=None):
        """Declares a parameter - add_parameter("Distance", ParamType.FLOAT, 40) is "param Distance: Float = 40" - or
        changes the one of that name. A Choice's options: options=["Left", "Right"] (its default the first unless given).
        Bind fields to it with track.set(SlideDistance="$Distance") or track.bind(...). Returns its name."""
        default_text = "" if default is None else _text(default)
        self._check(self._raw.set_parameter(_text(name), _text(type), default_text, [_text(o) for o in (options or [])]))
        return name

    param = add_parameter

    def parameter_type(self, name):
        """A parameter's type, as ParamType names it."""
        found = self._raw.get_parameter_type(_text(name))
        if not found:
            raise ScriptError(self._raw.get_last_error())
        return found

    def parameter_default(self, name):
        """A parameter's default: a number, a bool, (r, g, b, a), (x, y), or a Choice's option."""
        kind = self._PARAMETER_KINDS.get(self.parameter_type(name), "string")
        return _from_text(self._raw.get_parameter_default(_text(name)), kind)

    def parameter_options(self, name):
        """A Choice parameter's options."""
        return list(self._raw.get_parameter_options(_text(name)))

    def remove_parameter(self, name):
        """Removes a parameter, and every binding to it: those fields play their own values again."""
        self._check(self._raw.remove_parameter(_text(name)))

    # ---- widgets -------------------------------------------------------------------------------------

    def _resolve(self, target):
        """target as a widget path: as given when there is no widget tree to check it against."""
        path = _target_path(target)
        if not path or path.lower() == "none":
            return ""
        found = self._raw.find_widget(path)
        if found:
            return found
        if self._raw.get_widget_paths():
            raise ScriptError("No widget called %r in this Widget Blueprint." % path)
        return path

    @property
    def widgets(self):
        """Every widget tracks can move, depth first. Empty when there is no preview to read them from."""
        return [Widget(self, p) for p in self._raw.get_widget_paths()]

    @property
    def root(self):
        """The top widget of the Widget Blueprint's tree, or None."""
        path = self._raw.get_root_widget()
        return Widget(self, path) if path else None

    def widget(self, name):
        """A widget by its name or path, or None."""
        path = self._raw.find_widget(_target_path(name))
        return Widget(self, path) if path else None

    # ---- changing the widgets ------------------------------------------------------------------
    #
    # The Widget Blueprint's own designer tree, changed as the designer changes it: each change is part of the run's one
    # undoable edit, and the designer catches up once the run is done.

    @property
    def can_edit_widgets(self):
        """False where there is no Widget Blueprint to change - a WA_ asset edited on its own."""
        return self._raw.can_edit_widgets()

    def _widget_path(self, widget, what="widget"):
        path = self._resolve(widget)
        if not path:
            raise ScriptError("Say which %s: a name, a path or a Widget." % what)
        return path

    def _widget_get(self, path, field, slot):
        raw = self._raw
        text = raw.get_widget_property(path, field, slot)
        kind = raw.get_widget_property_kind(path, field, slot)
        if not kind:
            raise ScriptError(raw.get_last_error())
        return _from_text(text, kind)

    def _widget_set(self, path, fields, named, slot):
        values = dict(fields or {})
        values.update(named)
        for field, value in values.items():
            if slot and isinstance(value, _NUMBER_TYPES) and not isinstance(value, bool) and \
                    re.sub(r"[\s_]", "", field).lower() == "padding":
                value = "(Left=%r,Top=%r,Right=%r,Bottom=%r)" % ((float(value),) * 4)
            self._check(self._raw.set_widget_property(path, field, _text(value), slot))

    def add_widget(self, type, into=None, name=None, index=None, **properties):
        """A new widget: type is a widget class ("Border", "TextBlock", "UniformGridPanel", "Image", "SizeBox"...) or a
        Widget Blueprint by name or path. into is the panel it goes in - or the widget whose content it becomes; left
        out, it becomes the top widget of an empty Widget Blueprint. name is its name, numbered if taken. properties are
        set on it. Returns it."""
        parent = self._widget_path(into, "panel") if into is not None else ""
        path = self._raw.add_widget(_text(type), parent, name or "", -1 if index is None else int(index))
        self._check(bool(path))
        widget = Widget(self, path)
        if properties:
            widget.set(properties)
        return widget

    def copy_widget(self, widget, count=1, into=None, index=None, name=None):
        """count copies of widget - everything inside it, and its slot's settings, included - added to its parent, or
        into the panel into, at index or after its last child. Named after it, numbered (Border_1...), or after name.
        Returns the copies, as a list."""
        source = self._widget_path(widget)
        parent = self._widget_path(into, "panel") if into is not None else ""
        if count < 1:
            return []
        paths = self._raw.copy_widget(source, int(count), parent, -1 if index is None else int(index), name or "")
        self._check(len(paths) > 0)
        return [Widget(self, p) for p in paths]

    def move_widget(self, widget, into=None, index=None):
        """Moves widget into the panel into, or to index among its parent's children."""
        path = self._widget_path(widget)
        parent = self._widget_path(into, "panel") if into is not None else ""
        self._check(self._raw.move_widget(path, parent, -1 if index is None else int(index)))

    def remove_widget(self, *widgets, remove_tracks=True):
        """Removes widgets and everything inside them - remove_widget(a, b) or remove_widget(list). With remove_tracks,
        the tracks that moved them go too, and the groups left with nothing to move."""
        flat = []
        for item in widgets:
            if isinstance(item, (list, tuple, set)):
                flat.extend(item)
            else:
                flat.append(item)
        paths = [self._widget_path(w) for w in flat]
        if not paths:
            return
        if remove_tracks:
            gone = set()
            for path in paths:
                gone.add(path.lower())
                gone.update(w.path.lower() for w in Widget(self, path).descendants())
            self.remove([t for t in self.tracks if (t.target or "").lower() in gone])
            if self.has_groups:
                for group in reversed(self.groups):
                    if (group.target or "").lower() in gone and not group.tracks and len(self.groups) > 1:
                        self.remove_group(group)
        self._check(self._raw.remove_widgets(paths))

    def layout_grid(self, grid, columns):
        """Sets Row and Column on every child of a Uniform Grid or Grid Panel, in order, row by row. Returns the cells."""
        cells = self._as_widget(grid).children
        for i, cell in enumerate(cells):
            row, column = divmod(i, int(columns))
            cell.set_slot(Row=row, Column=column)
        return cells

    def fill_grid(self, grid, rows, columns, template=None, remove_extra=True, name=None):
        """Makes a Uniform Grid (or Grid Panel) rows x columns: copies of template - a widget, by default its first child
        - added until there are enough, extras removed (with their tracks) unless remove_extra is False, and every cell's
        Row and Column set, row by row. Returns the cells, row by row."""
        grid_widget = self._as_widget(grid)
        cells = grid_widget.children
        if template is None:
            if not cells:
                raise ScriptError("%s is empty: give a template - a widget to copy, or a type such as \"Border\"." % grid_widget.name)
            template_widget = cells[0]
        elif isinstance(template, Widget) or self._raw.find_widget(_target_path(template)):
            template_widget = self._as_widget(template)
        else:
            # A type rather than a widget: the first cell made new, the rest copied from it.
            if cells:
                raise ScriptError("%s already has children: give one of them as the template, or empty it first." % grid_widget.name)
            template_widget = self.add_widget(template, into=grid_widget, name=name)
            cells = [template_widget]

        wanted = int(rows) * int(columns)
        if len(cells) < wanted:
            self.copy_widget(template_widget, count=wanted - len(cells), into=grid_widget, name=name)
        elif len(cells) > wanted and remove_extra:
            extra = [c for c in cells[wanted:] if c != template_widget]
            self.remove_widget(extra)
        return self.layout_grid(grid_widget, columns)[:wanted]

    def _as_widget(self, widget):
        if isinstance(widget, Widget):
            return widget
        found = self.widget(widget)
        if found is None:
            raise ScriptError("No widget called %r in this Widget Blueprint." % (widget,))
        return found

    # ---- reading it --------------------------------------------------------------------------------

    def print_tracks(self):
        """Prints every track: its place, type, widget and time."""
        groups = {g.index: g.name for g in self.groups} if self.has_groups else {}
        for track in self.tracks:
            where = groups.get(track._raw_group(), "") if self.has_groups else (track.target or "")
            folded = "  [%s]" % track.collapsed if track.collapsed else ""
            print("%3d  %-16s %-20s %6.2f - %6.2f s%s" % (track.index, track.type, where, track.start, track.end, folded))

    def __repr__(self):
        return "<Animation %r: %d groups, %d tracks, %.2f s>" % (self.name, self._raw.get_num_groups(), self._raw.get_num_tracks(), self.length)


# =====================================================================================================
# Getting an animation
# =====================================================================================================


def open_animation():
    """The animation in the Widget Animations panel used last."""
    return Animation(unreal.WidgetAnimScriptLibrary.get_open_animation())


def edit_asset(asset):
    """A Widget Animation asset (WA_), edited directly - no panel needed, and no widgets to read."""
    if isinstance(asset, str):
        asset = unreal.load_asset(asset)
    return Animation(unreal.WidgetAnimScriptLibrary.edit_animation_asset(asset))


# =====================================================================================================
# The panel's Run
# =====================================================================================================

SCRIPT_NAMES = {
    "ScriptError": ScriptError,
    "TrackType": TrackType,
    "Ease": Ease,
    "Curve": Curve,
    "Starts": Starts,
    "Timing": Timing,
    "Direction": Direction,
    "Stagger": Stagger,
    "Spacing": Spacing,
    "Order": Order,
    "Falloff": Falloff,
    "Smoothing": Smoothing,
    "ParamType": ParamType,
    "Animation": Animation,
    "Track": Track,
    "Group": Group,
    "Widget": Widget,
    "Slot": Slot,
}


# =====================================================================================================
# Your own functions
#
# Every .py file in the project's Content/Python/WidgetAnimFunctions is run before each script - my_functions.py first
# (the one the Script area's Functions tab edits), then the rest by name - and what each defines is there for the script
# to use by name. They are read afresh every run, so an edit counts at once. They see what a script sees: anim,
# selection, TrackType and the rest - so a function can use anim without being handed it.
# =====================================================================================================

FUNCTIONS_FILE = "my_functions.py"


def functions_folder():
    """Where your function files are: the project's Content/Python/WidgetAnimFunctions."""
    content = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir())
    return os.path.join(content, "Python", "WidgetAnimFunctions")


def _function_files():
    folder = functions_folder()
    if not os.path.isdir(folder):
        return []
    files = sorted(glob.glob(os.path.join(folder, "*.py")), key=lambda f: (os.path.basename(f).lower() != FUNCTIONS_FILE, f.lower()))
    return [f for f in files if not os.path.basename(f).startswith("_")]


def load_functions(scope):
    """Runs every function file into a copy of scope, and returns what they defined - names not starting with _."""
    shared = dict(scope)
    for path in _function_files():
        with open(path, "r", encoding="utf-8") as file:
            code = file.read()
        linecache.checkcache(path)
        exec(compile(code, path, "exec"), shared)
    return dict((k, v) for k, v in shared.items() if not k.startswith("_") and k not in scope)


def script_scope(anim, selection=None):
    """What a script sees: anim, selection, every name, and your functions. For running scripts of your own outside the
    panel: exec(code, wa.script_scope(anim))."""
    scope = {"__name__": "__widget_anim_script__", "unreal": unreal, "anim": anim,
             "selection": selection if selection is not None else Selection([], None)}
    scope.update(SCRIPT_NAMES)
    scope.update(load_functions(scope))
    return scope


def _format_error(exc_type, exc, tb):
    """The error as Python prints it, keeping only the script's lines and your functions' - the plugin's are no help.
    Returns the text, and the line and file (empty for the script) it stopped in."""
    lines = ["Traceback (most recent call last):"]
    error_line = 0
    error_file = ""
    function_files = set(os.path.normcase(f) for f in _function_files())
    for frame in traceback.extract_tb(tb):
        in_script = frame.filename == SCRIPT_FILENAME
        if not in_script and os.path.normcase(frame.filename) not in function_files:
            continue
        error_line = frame.lineno
        error_file = "" if in_script else frame.filename
        where = "the script" if frame.name == "<module>" else frame.name
        if in_script:
            lines.append("  Line %d, in %s" % (frame.lineno, where))
        else:
            lines.append("  %s, line %d, in %s" % (os.path.basename(frame.filename), frame.lineno, frame.name))
        if frame.line:
            lines.append("    " + frame.line.strip())
    if isinstance(exc, SyntaxError) and exc.filename and exc.filename != SCRIPT_FILENAME:
        error_line = exc.lineno or 0
        error_file = exc.filename
        lines.append("  %s, line %d" % (os.path.basename(exc.filename), error_line))
    lines.append("%s: %s" % (exc_type.__name__, exc))
    return "\n".join(lines), error_line, error_file


def _report(library, message, line, file=""):
    """Tells the panel where a run stopped - in the script, or in one of your function files."""
    report_in = getattr(library, "report_panel_script_error_in", None)
    if report_in is not None:
        report_in(message, line, file)
    else:
        library.report_panel_script_error(message, line if not file else 0)


def _run_panel_script():
    """Runs the panel's script - called by the panel's Run, which hands over the code and the animation."""
    library = unreal.WidgetAnimScriptLibrary
    code = library.get_panel_script_code()
    raw = library.get_panel_script_animation()
    if raw is None:
        message = "There is no animation to run the script on."
        unreal.log_error(message)
        _report(library, message, 0)
        return

    anim = Animation(raw)
    selected = [anim._track(i) for i in raw.get_selected_tracks()]
    selected_group = raw.get_selected_group()
    selection = Selection(selected, anim._group(selected_group) if selected_group >= 0 else None)

    # So tracebacks can show the script's own lines.
    linecache.cache[SCRIPT_FILENAME] = (len(code), None, code.splitlines(True), SCRIPT_FILENAME)
    try:
        compiled = compile(code, SCRIPT_FILENAME, "exec")
    except SyntaxError as error:
        line = error.lineno or 0
        text = "Line %d: %s" % (line, error.msg)
        if error.text:
            text += "\n    " + error.text.rstrip("\n")
        unreal.log_error(text)
        _report(library, "Line %d: %s" % (line, error.msg), line)
        return

    try:
        scope = script_scope(anim, selection)
        exec(compiled, scope)
    except BaseException:
        exc_type, exc, tb = sys.exc_info()
        text, line, file = _format_error(exc_type, exc, tb)
        unreal.log_error(text)
        where = ("%s, line %d: " % (os.path.basename(file), line)) if file else ("Line %d: " % line if line else "")
        _report(library, "%s%s: %s" % (where, exc_type.__name__, exc), line, file)


# =====================================================================================================
# The API reference
# =====================================================================================================

API_REFERENCE = """\
Widget Animations scripting

In a script run from the panel:
  anim                     the animation open in the panel
  selection                .tracks, .group: what was selected when it ran
  Names: TrackType, Ease, Curve, Starts, Timing, Direction, Stagger, Spacing, Order, Falloff, Smoothing, ParamType
  One run is one undoable edit; a run that stops with an error changes nothing.
  Seconds are the animation's own (as Duration is typed). Changing a track never moves another.

anim
  .tracks .groups .widgets .root .length .name .track_types .collapsed_names .has_groups .editable
  .add_track(type, group=None, target=None, **settings) -> Track
      settings: start, delay, starts, duration, easing, timing, from_, to, values=(from, to), keys,
                stagger, invert, hidden, pinned, locked, collapsed, value_name, or any field by name
  .add_group(name="", target=None) -> Group     .group(name or index)     .group_for(target)
  .find(type=, target=, group=, collapsed=) -> [Track]      .track(index)
  .remove(tracks)  .remove_group(group)  .clear(group=None)  .duplicate(track)
  .collapse(tracks, name) -> name   .expand(name)   .collapsed(name) -> [Track]
  .widget(name or path) -> Widget   .print_tracks()   .kinds("Expander" or "ValueInput") -> names
  .edit(description)   with anim.edit("..."): one undoable edit (outside the panel)
  .parameters -> {name: default}   .add_parameter(name, type=ParamType.FLOAT, default=None, options=None)  (also .param)
  .parameter_type(name)  .parameter_default(name)  .parameter_options(name)  .remove_parameter(name)

Track
  .index .type .group .target .start .end .length .duration .delay .starts .easing .timing
  .invert .hidden .pinned .locked .collapsed .value_name .stagger
  .set(Field=value, ...) / .set({"Spring.Bounce": 0.3})     .get("Field" or "Dotted.Path")
  .set_values(from_, to)     .set_keys([(time, value[, curve]), ...])     .clear_keys()
  .set_stagger(Stagger.EACH_CHILD, Interval=0.05, Order=Order.FROM_THE_MIDDLE)    .set_input(kind, **fields)
  .fields(path="")   .configure(**settings)   .remove()   .duplicate()
  .set(SlideDistance="$Distance")  ("-$Distance" mirrored)   .bind(field, parameter, scale=1, offset=0, mirrored=False)
  .unbind(field)   .binding(field) -> name or None   .plays_when(parameter, options)  (None: always)

Group      .index .name .target .tracks .add_track(type, **settings) .remove()
Widget     .path .name .class_name .parent .children .descendants() .index .tracks     str(widget) is its path
           .set(Prop=value) .get("Prop") .fields()   .slot  .set_slot(Row=1, Column=2)
           .add(type, name=None, **props) -> Widget   .copy(count=1, into=None) -> Widget or [Widget]
           .move(into=None, index=None)   .remove(remove_tracks=True)
Slot       ["Row"]  ["Row"] = 2  .set(Row=2)  .get("Row")  .class_name  .fields()

Changing the Widget Blueprint (one undoable edit with the rest of the run; the designer catches up after it)
  anim.add_widget(type, into=None, name=None, index=None, **props) -> Widget   type: "Border", "TextBlock",
      "UniformGridPanel", "Image", "SizeBox"..., or a Widget Blueprint by name or path
  anim.copy_widget(widget, count=1, into=None, index=None, name=None) -> [Widget]
  anim.move_widget(widget, into=None, index=None)    anim.remove_widget(widgets, remove_tracks=True)
  anim.fill_grid(grid, rows, columns, template=None, remove_extra=True) -> [Widget], row by row
  anim.layout_grid(grid, columns) -> [Widget]

Your functions
  Every .py in the project's Content/Python/WidgetAnimFunctions runs before each script (my_functions.py first, the
  file the Functions tab edits); what they define is there by name. They see anim, selection and every name too.

Example
  N = 8
  row = anim.widget("Row")
  for i, cell in enumerate(row.children[:N]):
      anim.add_track(TrackType.SCALE, target=cell, start=i * 0.08, duration=0.4,
                     values=(1.0, 1.35), easing=Ease.EASE_IN_OUT)
  anim.collapse(anim.find(type=TrackType.SCALE), "Pulse Row")

Outside the panel:  import widget_anim_scripting as wa; anim = wa.open_animation()
                    exec(code, wa.script_scope(anim))  - with your functions loaded
"""


def api_reference():
    return API_REFERENCE


def print_api_reference():
    print(API_REFERENCE)


# =====================================================================================================
# What the code editor suggests
#
# The Script area's code editor (Monaco) suggests and explains the API as it is typed. It is told what there is by this:
# every class's members - their signatures and the first paragraph of their docs - and what each returns, so the editor
# can follow anim.widget("Row").children to a list of Widgets and suggest a Widget's members after the next ".".
# =====================================================================================================

# What a member gives back, where that is a class of this API: "Track", or "list[Track]" for a list of them.
_RETURNS = {
    "Animation": {
        "tracks": "list[Track]", "groups": "list[Group]", "widgets": "list[Widget]", "root": "Widget",
        "track": "Track", "add_track": "Track", "duplicate": "Track", "find": "list[Track]", "collapsed": "list[Track]",
        "group": "Group", "add_group": "Group", "group_for": "Group", "widget": "Widget", "edit": "Animation",
        "add_widget": "Widget", "copy_widget": "list[Widget]", "fill_grid": "list[Widget]", "layout_grid": "list[Widget]",
    },
    "Track": {
        "group": "Group", "anim": "Animation", "duplicate": "Track", "set": "Track", "set_values": "Track",
        "set_keys": "Track", "clear_keys": "Track", "set_stagger": "Track", "set_input": "Track", "configure": "Track",
        "bind": "Track", "unbind": "Track", "plays_when": "Track",
    },
    "Group": {"tracks": "list[Track]", "add_track": "Track"},
    "Widget": {"parent": "Widget", "children": "list[Widget]", "descendants": "list[Widget]", "tracks": "list[Track]",
               "slot": "Slot", "add": "Widget", "set": "Widget", "set_slot": "Widget", "move": "Widget"},
    "Slot": {"set": "Slot"},
    "Selection": {"tracks": "list[Track]", "group": "Group"},
}

# What a for loop over one of these gives, beyond lists.
_ITERATES = {"Selection": "Track"}

_GLOBAL_DOCS = {
    "anim": ("Animation", "The animation open in the panel."),
    "selection": ("Selection", "What was selected in the panel when the script started: .tracks, .group."),
    "unreal": (None, "Unreal's own Python module."),
}


def _first_paragraph(doc):
    if not doc:
        return ""
    import inspect
    text = inspect.cleandoc(doc)
    return text.split("\n\n", 1)[0].replace("\n", " ")


def _signature(member):
    import inspect
    try:
        signature = str(inspect.signature(member))
    except (TypeError, ValueError):
        return "(...)"
    return signature.replace("(self, ", "(").replace("(self)", "()")


def _describe_class(cls):
    import inspect
    members = []
    returns = _RETURNS.get(cls.__name__, {})
    for name, value in sorted(vars(cls).items()):
        if name.startswith("_"):
            continue
        if isinstance(value, property):
            members.append({"name": name, "kind": "property", "signature": "", "doc": _first_paragraph(value.__doc__),
                            "returns": returns.get(name)})
        elif inspect.isfunction(value):
            members.append({"name": name, "kind": "method", "signature": _signature(value), "doc": _first_paragraph(value.__doc__),
                            "returns": returns.get(name)})
    return {"doc": _first_paragraph(cls.__doc__), "members": members, "iterates": _ITERATES.get(cls.__name__)}


def _describe_names(cls):
    members = [{"name": name, "kind": "constant", "signature": " = %r" % value, "doc": ""}
               for name, value in vars(cls).items() if not name.startswith("_") and isinstance(value, str)]
    return {"doc": _first_paragraph(cls.__doc__), "members": members, "isNames": True}


def editor_completions():
    """What the code editor suggests: globals, and every class's members. A dict, ready for JSON."""
    types = {}
    for cls in (Animation, Track, Group, Widget, Slot, Selection):
        types[cls.__name__] = _describe_class(cls)

    globals_ = []
    for name, (type_name, doc) in _GLOBAL_DOCS.items():
        globals_.append({"name": name, "kind": "module" if name == "unreal" else "variable", "type": type_name, "doc": doc})
    for name, value in SCRIPT_NAMES.items():
        if isinstance(value, type) and issubclass(value, Exception):
            globals_.append({"name": name, "kind": "class", "detail": "exception", "doc": _first_paragraph(value.__doc__)})
        elif name in types:
            globals_.append({"name": name, "kind": "class", "detail": "class", "doc": types[name]["doc"]})
        else:
            types[name] = _describe_names(value)
            globals_.append({"name": name, "kind": "class", "type": name, "detail": "names", "doc": types[name]["doc"]})
    globals_.extend(_describe_functions())
    return {"globals": globals_, "types": types}


def _describe_functions():
    """Your functions, for the editor to suggest - read from the files, not run, so writing one can't break anything."""
    import ast
    described = []
    try:
        files = _function_files()
    except Exception:
        return described
    for path in files:
        try:
            with open(path, "r", encoding="utf-8") as file:
                tree = ast.parse(file.read(), path)
        except (OSError, SyntaxError, ValueError):
            continue
        for node in tree.body:
            if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)) and not node.name.startswith("_"):
                try:
                    arguments = ast.unparse(node.args)
                except Exception:
                    arguments = "..."
                described.append({"name": node.name, "kind": "function", "detail": "(%s)  - %s" % (arguments, os.path.basename(path)),
                                  "doc": _first_paragraph(ast.get_docstring(node))})
            elif isinstance(node, ast.Assign):
                for target in node.targets:
                    if isinstance(target, ast.Name) and not target.id.startswith("_"):
                        described.append({"name": target.id, "kind": "variable", "detail": os.path.basename(path), "doc": ""})
    return described


def write_editor_completions(path):
    """Writes editor_completions() to path as JSON - what the panel's code editor reads."""
    import json
    with open(path, "w", encoding="utf-8") as file:
        json.dump(editor_completions(), file)
