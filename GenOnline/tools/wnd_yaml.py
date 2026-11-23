"""Compile declarative YAML layout sources into .wnd window layouts.

A source lives at GenOnline/tools/wnd_yaml/<archive path>.yaml (for example Window/Menus/OptionsMenu.yaml) and compiles
to the .wnd at the same archive path. GenOnline/tools/go_wnd_layouts.py writes the compiled text unchanged, so a source
carries the final kit colours, GO header templates and hover images. All rects are in 800x600 units.

Source keys:
    layout_block  {init, update, shutdown}: the LAYOUTINIT, LAYOUTUPDATE and LAYOUTSHUTDOWN callbacks
    tokens        {name: int}: spacing values that sizes, gaps, pads and component placements refer to by name
    components    {name: component}: components local to this source, next to the shared ones
    windows       [window]: the top-level windows, placed in the 800x600 screen

Components (GenOnline/tools/wnd_yaml/components/<name>.yaml, one per file, or local ones) hold the WND property block of
one kind of window as a template with {placeholders}:
    extends   another component whose template, params, status, callbacks and place this one starts from
    template  the property block; name, x0, y0, x1, y1, status, system, input, tooltip_callback, draw, text and
              tooltip are filled by the compiler, every other placeholder is a param; a line holding {text} or
              {tooltip} is left out when the window sets none
    params    {name: value}: the default of every other placeholder
    status    the default STATUS flags; callbacks {system, input, tooltip, draw} the default callbacks ("[None]")
    place     the default placement (below), usually by token name

Windows:
    component, name (the part after "<file>.wnd:", empty for an unnamed window), id (a handle for an unnamed
    window), text, tooltip, hidden, status, callbacks, params (override the component's params)
    rect          [x0, y0, x1, y1]: absolute, outside the flow of its container
    placement     left, right, top, bottom (insets in the box the container gives), w, h (fixed size, or fill),
                  valign (top or middle)
    column / row / grid / overlay   lays out the window's children (see containers); a window without h takes
                  the height of that content
    children      [item]: children each placed in the window's own rect, like overlay (for pinned children)
    hidden_windows  [window]: children kept for the game code but never shown; each needs a rect and gets HIDDEN
    child_of      emit the window under this enclosing window (by id) while it is laid out here
    order         the ids of the window's emitted children in file order; the file order is the draw order

Containers are an item {column|row|grid|overlay: settings}; settings hold items, pad (one value or
[top, right, bottom, left]) and the placement keys, plus:
    column   gap: items stack downwards; each needs a height
    row      gap: items run rightwards; items without w share the width left over
    grid     columns (widths, fill shares what is left), col_gap, row_gap: items fill the cells row by row, span
             takes several cells and ~ leaves a cell empty
    overlay  items share one box (pages shown one at a time)
An item {repeat: {count: n | each: [{key: value}], item: window}} expands to n windows; {i} (from 1) and the
keys of each entry are filled into the item's strings.

Loading is strict: duplicate keys, unknown keys and values of the wrong type fail with the file and key path, and
YAML 1.1 booleans such as no or on never pass as text.
"""

import re
from pathlib import Path

import yaml

HERE = Path(__file__).resolve().parent
SOURCE_ROOT = HERE / "wnd_yaml"
COMPONENTS = SOURCE_ROOT / "components"
SCREEN = (0, 0, 800, 600)

# WindowStatusNames in GameWindowManagerScript.cpp; STATUS flags are written in this order
STATUS_FLAGS = (
    "ACTIVE", "TOGGLE", "DRAGABLE", "ENABLED", "HIDDEN", "ABOVE", "BELOW", "IMAGE", "TABSTOP", "NOINPUT", "NOFOCUS",
    "DESTROYED", "BORDER", "SMOOTH_TEXT", "ONE_LINE", "NO_FLUSH", "SEE_THRU", "RIGHT_CLICK", "WRAP_CENTERED",
    "CHECK_LIKE", "HOTKEY_TEXT", "USE_OVERLAY_STATES", "NOT_READY", "FLASHING", "ALWAYS_COLOR", "ON_MOUSE_DOWN",
)
BUILTINS = {"name", "x0", "y0", "x1", "y1", "status", "system", "input", "tooltip_callback", "draw", "text", "tooltip"}
CALLBACKS = {"system": "system", "input": "input", "tooltip": "tooltip_callback", "draw": "draw"}
PLACE_KEYS = {"left", "right", "top", "bottom", "w", "h", "valign"}
CONTAINERS = ("column", "row", "grid", "overlay")
CONTAINER_KEYS = {
    "column": {"items", "pad", "gap"},
    "row": {"items", "pad", "gap"},
    "grid": {"items", "pad", "columns", "col_gap", "row_gap"},
    "overlay": {"items", "pad"},
}
WINDOW_KEYS = {"component", "name", "id", "text", "tooltip", "hidden", "status", "callbacks", "params", "rect",
               "hidden_windows", "child_of", "order", "span", "children", *PLACE_KEYS, *CONTAINERS}
COMPONENT_KEYS = {"extends", "template", "params", "status", "callbacks", "place"}
PLACEHOLDER_RE = re.compile(r"\{(\w+)\}")


class LayoutError(ValueError):
    """A source that cannot compile; the message names the file and the key path."""


class _Loader(getattr(yaml, "CSafeLoader", yaml.SafeLoader)):
    pass


def _join(path, key):
    return f"{path}.{key}" if path else str(key)


def load_yaml(text, origin):
    """Parse YAML text, rejecting duplicate keys with their path."""
    loader = _Loader(text)
    try:
        node = loader.get_single_node()
        if node is None:
            return None
        _no_duplicates(node, origin, "")
        return loader.construct_document(node)
    except yaml.YAMLError as e:
        raise LayoutError(f"{origin}: {e}") from None
    finally:
        loader.dispose()


def _no_duplicates(node, origin, path):
    if isinstance(node, yaml.MappingNode):
        seen = set()
        for key, value in node.value:
            sub = _join(path, key.value)
            if key.value in seen:
                raise LayoutError(f"{origin}: {sub}: duplicate key (line {key.start_mark.line + 1})")
            seen.add(key.value)
            _no_duplicates(value, origin, sub)
    elif isinstance(node, yaml.SequenceNode):
        for i, item in enumerate(node.value):
            _no_duplicates(item, origin, f"{path}[{i}]")


def _type(value):
    return f"{type(value).__name__} {value!r}"


class _Checker:
    """Typed access to loaded YAML values; every failure names the origin file and the key path."""

    def __init__(self, origin, tokens=None):
        self.origin = origin
        self.tokens = tokens or {}

    def fail(self, path, problem):
        raise LayoutError(f"{self.origin}: {path or '<top>'}: {problem}")

    def mapping(self, value, path, keys=None, required=()):
        """A mapping with string keys; keys limits them when given."""
        if not isinstance(value, dict):
            self.fail(path, f"expected a mapping, got {_type(value)}")
        for key in value:
            if not isinstance(key, str):
                self.fail(path, f"key {key!r} is not a string")
            if keys is not None and key not in keys:
                self.fail(_join(path, key), "unknown key")
        for key in required:
            if key not in value:
                self.fail(path, f"missing key '{key}'")
        return value

    def sequence(self, value, path):
        if not isinstance(value, list):
            self.fail(path, f"expected a list, got {_type(value)}")
        return value

    def string(self, value, path):
        if not isinstance(value, str):
            self.fail(path, f"expected a string, got {_type(value)}")
        if '"' in value or "\n" in value:
            self.fail(path, "a string may not hold quotes or line breaks")
        return value

    def boolean(self, value, path):
        if not isinstance(value, bool):
            self.fail(path, f"expected true or false, got {_type(value)}")
        return value

    def integer(self, value, path):
        if isinstance(value, bool) or not isinstance(value, int):
            self.fail(path, f"expected an integer, got {_type(value)}")
        return value

    def scalar_text(self, value, path):
        """A param value: a string or an integer, as text."""
        if isinstance(value, bool) or not isinstance(value, (str, int)):
            self.fail(path, f"expected a string or an integer, got {_type(value)}")
        return self.string(str(value), path)

    def strings(self, value, path):
        return [self.string(v, f"{path}[{i}]") for i, v in enumerate(self.sequence(value, path))]

    def size(self, value, path, fill=False):
        """An integer or a token name; fill gives None where allowed."""
        if fill and value == "fill":
            return None
        if isinstance(value, str):
            if value not in self.tokens:
                self.fail(path, f"unknown token '{value}'")
            return self.tokens[value]
        return self.integer(value, path)

    def status(self, value, path):
        flags = self.strings(value, path)
        for i, flag in enumerate(flags):
            if flag not in STATUS_FLAGS:
                self.fail(f"{path}[{i}]", f"unknown status flag '{flag}'")
        return flags

    def callbacks(self, value, path):
        self.mapping(value, path, CALLBACKS)
        return {CALLBACKS[k]: self.string(v, _join(path, k)) for k, v in value.items()}


def _origin(path):
    path = Path(path)
    return path.relative_to(HERE.parents[1]).as_posix() if HERE.parents[1] in path.parents else str(path)


# ---------------------------------------------------------------------------------------------
# components


class _Component:
    def __init__(self, name, template, params, status, callbacks, place):
        self.name = name
        self.template = template
        self.params = params
        self.status = status
        self.callbacks = callbacks
        self.place = place


class _Components:
    """Shared components from a directory plus the local ones of a source, resolved through extends."""

    def __init__(self, local, check, directory):
        self.local = local
        self.check = check
        self.directory = directory
        self.resolved = {}

    def _spec(self, name, path):
        if name in self.local:
            return self.local[name], self.check, f"components.{name}"
        file = self.directory / f"{name}.yaml"
        if not file.is_file():
            self.check.fail(path, f"unknown component '{name}'")
        origin = _origin(file)
        return load_yaml(file.read_text("utf-8"), origin), _Checker(origin), ""

    def get(self, name, path, chain=()):
        if name in self.resolved:
            return self.resolved[name]
        if name in chain:
            self.check.fail(path, f"component '{name}' extends itself")
        spec, check, base = self._spec(name, path)
        check.mapping(spec, base, COMPONENT_KEYS)
        if "extends" in spec:
            where = _join(base, "extends")
            parent = self.get(check.string(spec["extends"], where), where, (*chain, name))
            template, params = parent.template, dict(parent.params)
            status, callbacks, place = parent.status, dict(parent.callbacks), dict(parent.place)
        elif "template" in spec:
            template, params, status, callbacks, place = None, {}, ["ENABLED"], {}, {}
        else:
            check.fail(base, "a component needs a template or extends")
        if "template" in spec:
            if not isinstance(spec["template"], str):
                check.fail(_join(base, "template"), f"expected text, got {_type(spec['template'])}")
            template = spec["template"].rstrip("\n")
        for key, value in check.mapping(spec.get("params", {}), _join(base, "params")).items():
            if "extends" in spec and "template" not in spec and key not in params:
                check.fail(_join(base, f"params.{key}"), f"'{key}' is not a param of '{spec['extends']}'")
            params[key] = check.scalar_text(value, _join(base, f"params.{key}"))
        if "status" in spec:
            status = check.status(spec["status"], _join(base, "status"))
        if "callbacks" in spec:
            callbacks.update(check.callbacks(spec["callbacks"], _join(base, "callbacks")))
        if "place" in spec:
            place.update(check.mapping(spec["place"], _join(base, "place"), PLACE_KEYS))
        holes = set(PLACEHOLDER_RE.findall(template))
        for hole in sorted(holes - BUILTINS - set(params)):
            check.fail(_join(base, "template"), f"placeholder {{{hole}}} has no param default")
        for key in sorted(set(params) - holes):
            check.fail(_join(base, f"params.{key}"), "param not used by the template")
        component = _Component(name, template, params, status, callbacks, place)
        self.resolved[name] = component
        return component


# ---------------------------------------------------------------------------------------------
# the source tree


class _Window:
    def __init__(self, path):
        self.path = path
        self.box = None        # the container laying out its children
        self.hidden_windows = []
        self.children = []     # emitted children, in file order
        self.rect = None


class _Container:
    def __init__(self, path, kind, settings, items):
        self.path = path
        self.kind = kind
        self.settings = settings
        self.items = items
        self.place = {}
        self.span = 1
        self.rect = None


class _Builder:
    def __init__(self, check, components):
        self.check = check
        self.components = components
        self.ids = {}

    def window(self, spec, path, hidden=False):
        check = self.check
        check.mapping(spec, path, WINDOW_KEYS, ("component",))
        win = _Window(path)
        where = _join(path, "component")
        win.component = self.components.get(check.string(spec["component"], where), where)
        win.name = check.string(spec.get("name", ""), _join(path, "name"))
        if "id" in spec and win.name:
            check.fail(_join(path, "id"), "a named window is known by its name")
        win.id = win.name or (check.string(spec["id"], _join(path, "id")) if "id" in spec else None)
        if win.id is not None:
            if win.id in self.ids:
                check.fail(path, f"'{win.id}' is used twice (also at {self.ids[win.id].path})")
            self.ids[win.id] = win
        win.text = check.string(spec["text"], _join(path, "text")) if "text" in spec else None
        win.tooltip = check.string(spec["tooltip"], _join(path, "tooltip")) if "tooltip" in spec else None
        for key in ("text", "tooltip"):
            if key in spec and "{" + key + "}" not in win.component.template:
                check.fail(_join(path, key), f"component '{win.component.name}' has no {key}")
        status = check.status(spec["status"], _join(path, "status")) if "status" in spec else list(win.component.status)
        if hidden or check.boolean(spec.get("hidden", False), _join(path, "hidden")):
            status.append("HIDDEN")
        win.status = "+".join(f for f in STATUS_FLAGS if f in status)
        win.callbacks = {key: "[None]" for key in CALLBACKS.values()}
        win.callbacks.update(win.component.callbacks)
        win.callbacks.update(check.callbacks(spec.get("callbacks", {}), _join(path, "callbacks")))
        win.params = dict(win.component.params)
        params = check.mapping(spec.get("params", {}), _join(path, "params"), set(win.component.params))
        for key, value in params.items():
            win.params[key] = check.scalar_text(value, _join(path, f"params.{key}"))
        win.place = self.place(spec, path, win.component.place)
        win.span = self.span(spec, path)
        win.fixed = None
        if "rect" in spec:
            rect = check.sequence(spec["rect"], _join(path, "rect"))
            if len(rect) != 4:
                check.fail(_join(path, "rect"), f"expected [x0, y0, x1, y1], got {rect!r}")
            win.fixed = tuple(check.integer(v, f"{path}.rect[{i}]") for i, v in enumerate(rect))
        elif hidden:
            check.fail(path, "a hidden window needs a rect")
        win.child_of = check.string(spec["child_of"], _join(path, "child_of")) if "child_of" in spec else None
        win.order = check.strings(spec["order"], _join(path, "order")) if "order" in spec else None
        kinds = [k for k in (*CONTAINERS, "children") if k in spec]
        if len(kinds) > 1:
            check.fail(path, f"a window lays out its children one way, not {' and '.join(kinds)}")
        if kinds == ["children"]:
            items = self.items(spec["children"], _join(path, "children"))
            win.box = _Container(_join(path, "children"), "overlay", {"pad": (0, 0, 0, 0)}, items)
        elif kinds:
            win.box = self.container(kinds[0], spec[kinds[0]], _join(path, kinds[0]), own=False)
        hidden_path = _join(path, "hidden_windows")
        win.hidden_windows = [self.window(s, f"{hidden_path}[{i}]", hidden=True)
                              for i, s in enumerate(check.sequence(spec.get("hidden_windows", []), hidden_path))]
        return win

    def place(self, spec, path, defaults):
        check = self.check
        place = {}
        for key in sorted(PLACE_KEYS):
            if key in spec:
                value, where = spec[key], _join(path, key)
            elif key in defaults:
                value, where = defaults[key], f"{path} (component place.{key})"
            else:
                continue
            if key == "valign":
                if value not in ("top", "middle"):
                    check.fail(where, f"expected top or middle, got {_type(value)}")
                place[key] = value
            else:
                place[key] = check.size(value, where, fill=key in ("w", "h"))
        return place

    def span(self, spec, path):
        if "span" not in spec:
            return 1
        span = self.check.integer(spec["span"], _join(path, "span"))
        if span < 1:
            self.check.fail(_join(path, "span"), "expected 1 or more")
        return span

    def container(self, kind, settings, path, own=True):
        check = self.check
        keys = CONTAINER_KEYS[kind] | (PLACE_KEYS | {"span"} if own else set())
        check.mapping(settings, path, keys, ("items",))
        values = {"pad": self.pad(settings.get("pad", 0), _join(path, "pad"))}
        for key in ("gap", "col_gap", "row_gap"):
            values[key] = check.size(settings.get(key, 0), _join(path, key))
        if kind == "grid":
            columns = check.sequence(settings.get("columns", []), _join(path, "columns"))
            if not columns:
                check.fail(_join(path, "columns"), "a grid needs columns")
            values["columns"] = [check.size(c, f"{path}.columns[{i}]", fill=True) for i, c in enumerate(columns)]
        box = _Container(path, kind, values, self.items(settings["items"], _join(path, "items"), kind == "grid"))
        if own:
            box.place = self.place(settings, path, {})
            box.span = self.span(settings, path)
        return box

    def pad(self, value, path):
        if isinstance(value, list):
            if len(value) != 4:
                self.check.fail(path, f"expected one value or [top, right, bottom, left], got {value!r}")
            return tuple(self.check.size(v, f"{path}[{i}]") for i, v in enumerate(value))
        return (self.check.size(value, path),) * 4

    def items(self, value, path, grid=False):
        out = []
        for i, spec in enumerate(self.check.sequence(value, path)):
            where = f"{path}[{i}]"
            if spec is None and grid:
                out.append(None)
            elif isinstance(spec, dict) and "repeat" in spec:
                self.check.mapping(spec, where, {"repeat"})
                out.extend(self.window(s, p) for s, p in self.repeat(spec["repeat"], _join(where, "repeat")))
            elif isinstance(spec, dict) and "component" not in spec and any(k in spec for k in CONTAINERS):
                kind = next(k for k in CONTAINERS if k in spec)
                self.check.mapping(spec, where, {kind})
                out.append(self.container(kind, spec[kind], _join(where, kind)))
            else:
                out.append(self.window(spec, where))
        for item in out:
            if item is not None and item.span != 1 and not grid:
                self.check.fail(item.path, "span only works in a grid")
        return out

    def repeat(self, spec, path):
        check = self.check
        check.mapping(spec, path, {"count", "each", "item"}, ("item",))
        if ("count" in spec) == ("each" in spec):
            check.fail(path, "a repeat needs count or each")
        if "count" in spec:
            entries = [{} for _ in range(check.integer(spec["count"], _join(path, "count")))]
        else:
            entries = check.sequence(spec["each"], _join(path, "each"))
            for i, entry in enumerate(entries):
                for key, value in check.mapping(entry, f"{path}.each[{i}]").items():
                    check.scalar_text(value, f"{path}.each[{i}].{key}")
        for i, entry in enumerate(entries):
            values = {"i": str(i + 1), **{k: str(v) for k, v in entry.items()}}
            yield self.fill(spec["item"], values, f"{path}.item"), f"{path}.item#{i + 1}"

    def fill(self, value, values, path):
        if isinstance(value, dict):
            return {k: self.fill(v, values, _join(path, k)) for k, v in value.items()}
        if isinstance(value, list):
            return [self.fill(v, values, f"{path}[{i}]") for i, v in enumerate(value)]
        if isinstance(value, str):
            for hole in PLACEHOLDER_RE.findall(value):
                if hole not in values:
                    self.check.fail(path, f"repeat has no value for {{{hole}}}")
            return PLACEHOLDER_RE.sub(lambda m: values[m.group(1)], value)
        return value


# ---------------------------------------------------------------------------------------------
# layout


def _pinned(item):
    return isinstance(item, _Window) and item.fixed is not None


def _outer_height(item, check):
    """Height an item takes in its container, or None when it fills the box."""
    if _pinned(item):
        return 0
    h = item.place.get("h")
    if h is None:
        box = item.box if isinstance(item, _Window) else item
        if box is None:
            return None
        h = _content_height(box, check)
    return item.place.get("top", 0) + h + item.place.get("bottom", 0)


def _needed_height(item, check):
    h = _outer_height(item, check)
    if h is None:
        check.fail(item.path, "needs a height (h) here")
    return h


def _outer_width(item):
    w = item.place.get("w")
    return None if w is None else item.place.get("left", 0) + w + item.place.get("right", 0)


def _content_height(box, check):
    top, _, bottom, _ = box.settings["pad"]
    items = [i for i in box.items if i is not None and not _pinned(i)]
    if box.kind == "column":
        heights = [_needed_height(i, check) for i in items]
        return top + sum(heights) + box.settings["gap"] * max(len(items) - 1, 0) + bottom
    if box.kind == "grid":
        rows = [max(_needed_height(i, check) for _, _, i in row) for row in _grid_rows(box, check)]
        return top + sum(rows) + box.settings["row_gap"] * max(len(rows) - 1, 0) + bottom
    heights = [h for h in (_outer_height(i, check) for i in items) if h is not None]
    if not heights:
        check.fail(box.path, "cannot tell the height: give the container or one of its items an h")
    return top + max(heights) + bottom


def _grid_rows(box, check):
    """[[(column, span, item)]] for the flow items of a grid, row by row."""
    count = len(box.settings["columns"])
    rows, row, col = [], [], 0
    for item in box.items:
        if _pinned(item):
            continue
        span = 1 if item is None else item.span
        if span > count:
            check.fail(item.path, f"span {span} is wider than the grid ({count} columns)")
        if col + span > count:
            rows.append(row)
            row, col = [], 0
        if item is not None:
            row.append((col, span, item))
        col += span
    if row:
        rows.append(row)
    return rows


def _shares(sizes, total):
    """Fixed sizes stay; None sizes share what is left, the first ones take the remainder."""
    fills = [i for i, s in enumerate(sizes) if s is None]
    left = total - sum(s for s in sizes if s is not None)
    out = list(sizes)
    for n, i in enumerate(fills):
        out[i] = left // len(fills) + (1 if n < left % len(fills) else 0)
    return out


def _place(item, box, check):
    """The rect of an item inside the box its container gives it."""
    place = item.place
    x0, y0, x1, y1 = box
    left, right, top, bottom = (place.get(k, 0) for k in ("left", "right", "top", "bottom"))
    w, h = place.get("w"), place.get("h")
    if place.get("valign") == "middle":
        if h is None:
            check.fail(item.path, "valign middle needs an h")
        top = (y1 - y0 - h) // 2
    rx0, ry0 = x0 + left, y0 + top
    return rx0, ry0, rx0 + w if w is not None else x1 - right, ry0 + h if h is not None else y1 - bottom


def _arrange(item, box, check):
    if isinstance(item, _Window):
        item.rect = item.fixed or _place(item, box, check)
        if item.box is not None:
            _arrange_box(item.box, item.rect, check)
        for hidden in item.hidden_windows:
            _arrange(hidden, item.rect, check)
    else:
        item.rect = _place(item, box, check)
        _arrange_box(item, item.rect, check)


def _arrange_box(box, rect, check):
    top, right, bottom, left = box.settings["pad"]
    x0, y0, x1, y1 = rect[0] + left, rect[1] + top, rect[2] - right, rect[3] - bottom
    for item in box.items:
        if _pinned(item):
            _arrange(item, None, check)
    items = [i for i in box.items if i is not None and not _pinned(i)]
    if box.kind == "column":
        y = y0
        for item in items:
            h = _needed_height(item, check)
            _arrange(item, (x0, y, x1, y + h), check)
            y += h + box.settings["gap"]
    elif box.kind == "row":
        gap = box.settings["gap"]
        x = x0
        for item, w in zip(items, _shares([_outer_width(i) for i in items], x1 - x0 - gap * max(len(items) - 1, 0))):
            _arrange(item, (x, y0, x + w, y1), check)
            x += w + gap
    elif box.kind == "grid":
        columns, gap = box.settings["columns"], box.settings["col_gap"]
        widths = _shares(columns, x1 - x0 - gap * (len(columns) - 1))
        starts = [x0 + sum(widths[:i]) + gap * i for i in range(len(widths))]
        y = y0
        for row in _grid_rows(box, check):
            h = max(_needed_height(i, check) for _, _, i in row)
            for col, span, item in row:
                last = col + span - 1
                _arrange(item, (starts[col], y, starts[last] + widths[last], y + h), check)
            y += h + box.settings["row_gap"]
    else:
        for item in items:
            _arrange(item, (x0, y0, x1, y1), check)


# ---------------------------------------------------------------------------------------------
# emission


def _windows_in(win):
    """The windows directly under a window in source order: its flow items (through containers), then hidden ones."""
    def walk(items):
        for item in items:
            if isinstance(item, _Window):
                yield item
            elif item is not None:
                yield from walk(item.items)
    yield from walk(win.box.items if win.box else [])
    yield from win.hidden_windows


def _emit_tree(win, ancestors, check):
    for child in _windows_in(win):
        parent = win
        if child.child_of is not None:
            matches = [a for a in (*ancestors, win) if a.id == child.child_of]
            if not matches:
                check.fail(_join(child.path, "child_of"), f"'{child.child_of}' is not an enclosing window")
            parent = matches[-1]
        parent.children.append(child)
        _emit_tree(child, (*ancestors, win), check)


def _apply_order(win, check):
    if win.order is not None:
        path = _join(win.path, "order")
        for child in win.children:
            if child.id is None:
                check.fail(path, f"the unnamed child at {child.path} needs an id to be ordered")
        ids = [c.id for c in win.children]
        missing = [i for i in ids if i not in win.order]
        unknown = [i for i in win.order if i not in ids]
        if missing or unknown or len(win.order) != len(set(win.order)):
            check.fail(path, f"must list every emitted child once; missing {missing}, unknown {unknown}")
        by_id = {c.id: c for c in win.children}
        win.children = [by_id[i] for i in win.order]
    for child in win.children:
        _apply_order(child, check)


def _render(win, wnd_file, depth):
    pad = "  " * (depth + 1)
    values = dict(win.params)
    values.update(win.callbacks)
    values.update(zip(("x0", "y0", "x1", "y1"), map(str, win.rect)))
    values.update(name=f"{wnd_file}:{win.name}", status=win.status, text=win.text, tooltip=win.tooltip)
    lines = ["  " * depth + "WINDOW"]
    for line in win.component.template.split("\n"):
        if any(values[h] is None for h in PLACEHOLDER_RE.findall(line)):
            continue
        lines.append(pad + PLACEHOLDER_RE.sub(lambda m: values[m.group(1)], line))
    for child in win.children:
        lines.append(pad + "CHILD")
        lines.extend(_render(child, wnd_file, depth + 1))
    if win.children:
        lines.append(pad + "ENDALLCHILDREN")
    lines.append("  " * depth + "END")
    return lines


# ---------------------------------------------------------------------------------------------
# entry points


def compile_text(text, wnd_file, origin, components=COMPONENTS):
    """Compile YAML source text to .wnd text with CRLF line endings; window names start with wnd_file."""
    data = load_yaml(text, origin)
    check = _Checker(origin)
    check.mapping(data, "", {"layout_block", "tokens", "components", "windows"}, ("windows",))
    tokens = check.mapping(data.get("tokens", {}), "tokens")
    check.tokens = {k: check.integer(v, _join("tokens", k)) for k, v in tokens.items()}
    local = check.mapping(data.get("components", {}), "components")
    for name in local:
        if (components / f"{name}.yaml").is_file():
            check.fail(_join("components", name), f"'{name}' is a shared component already")
    builder = _Builder(check, _Components(local, check, components))
    roots = [builder.window(s, f"windows[{i}]") for i, s in enumerate(check.sequence(data["windows"], "windows"))]
    for root in roots:
        if root.child_of is not None:
            check.fail(_join(root.path, "child_of"), "a top-level window has no enclosing window")
        _arrange(root, SCREEN, check)
        _emit_tree(root, (), check)
        _apply_order(root, check)
    lines = ["FILE_VERSION = 2;", "STARTLAYOUTBLOCK"]
    if "layout_block" in data:
        block = check.mapping(data["layout_block"], "layout_block", {"init", "update", "shutdown"},
                              ("init", "update", "shutdown"))
        for key in ("init", "update", "shutdown"):
            lines.append(f"  LAYOUT{key.upper()} = {check.string(block[key], _join('layout_block', key))};")
    lines.append("ENDLAYOUTBLOCK")
    for root in roots:
        lines.extend(_render(root, wnd_file, 0))
    return "\r\n".join(lines) + "\r\n"


def compile_file(path):
    """Compile one source file; its window names start with "<file stem>.wnd"."""
    path = Path(path)
    return compile_text(path.read_text("utf-8"), path.stem + ".wnd", _origin(path))


def sources(root=SOURCE_ROOT):
    """{archive path of the .wnd: source path} for every YAML layout source under root."""
    return {
        path.relative_to(root).with_suffix(".wnd").as_posix(): path
        for path in sorted(root.rglob("*.yaml"))
        if COMPONENTS not in path.parents
    }
