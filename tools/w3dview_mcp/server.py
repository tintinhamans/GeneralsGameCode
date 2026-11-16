"""MCP stdio server for a running W3DView."""

from __future__ import annotations

import base64
import json
import re
import sys
from pathlib import Path
from typing import Any, Mapping

from ..worldbuilder_mcp.server import (
    DEFAULT_PROTOCOL_VERSION,
    SUPPORTED_PROTOCOL_VERSIONS,
    serve,
)
from .bridge import BridgeError, W3DViewBridge, work_directory
from .decode import bmp_to_png, decode_model, decode_textures, view_region


SERVER_NAME = "generals-w3dview"
SERVER_VERSION = "0.1.0"
DEFAULT_VIEWS = ["front-left-top", "front", "left", "top"]
DEFAULT_MAX_SIZE = 1024

INSTRUCTIONS = (
    "Inspect W3D models through a running W3DView (W3DViewZH.exe). read_model "
    "reports each mesh's W3D export properties, pass count, vertex materials, "
    "shaders, textures and stage mappings straight from the .w3d file. "
    "read_textures lists the textures each mesh uses with the path of each file. "
    "open_model displays a file, list_animations lists its animations and frame counts, "
    "set_animation_frame jumps to a frame, find_meshes searches the displayed model's meshes, "
    "set_mesh_visibility hides or shows them, and capture_views returns screenshots "
    "of the displayed model from view cube sides. Front is +X, left is -Y, top is +Z."
)

_PID = {
    "type": "integer",
    "description": "Process id of the W3DView to use when several run. Defaults to the first found.",
}

TOOL_DEFINITIONS = [
    {
        "name": "get_viewer_state",
        "description": (
            "List running W3DView instances and the model the selected one displays, "
            "with its polygon and vertex totals."
        ),
        "inputSchema": {"type": "object", "properties": {"pid": _PID}},
    },
    {
        "name": "read_model",
        "description": (
            "Read a .w3d model's meshes and materials. Polygon and vertex counts for the "
            "file and each mesh. Per mesh: W3D export properties "
            "(geometry type, hidden, two-sided, shadow, collision, user text), pass count, "
            "and per pass the vertex material (colours, opacity, stage 0 and stage 1 "
            "mapping type, args and UV channel), shader and stage textures with their "
            "options. Reads the file itself, so the model need not be displayed."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {
                "path": {
                    "type": "string",
                    "description": (
                        "Absolute .w3d path, or a bare file name resolved through "
                        "W3DView's search paths. Omit to read the displayed model."
                    ),
                },
                "mesh": {
                    "type": "string",
                    "description": "Only detail meshes whose name contains this text. The totals, mesh counts and texture list stay complete.",
                },
                "pid": _PID,
            },
        },
    },
    {
        "name": "read_textures",
        "description": (
            "Read which textures each mesh of a .w3d model uses, on which pass and stage, "
            "and the full path and folder of each texture file. Also reports W3DView's "
            "texture search paths and the textures that are missing."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {
                "path": {
                    "type": "string",
                    "description": "The model, as for read_model. Omit to read the displayed model.",
                },
                "mesh": {
                    "type": "string",
                    "description": "Only list meshes whose name contains this text. The texture list stays complete.",
                },
                "pid": _PID,
            },
        },
    },
    {
        "name": "open_model",
        "description": "Load a .w3d file into W3DView and display its top-level object.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "path": {"type": "string", "description": "Absolute .w3d path."},
                "pid": _PID,
            },
            "required": ["path"],
        },
    },
    {
        "name": "list_animations",
        "description": (
            "List the animations loaded for the displayed model's skeleton, with each one's "
            "frame count, frame rate and length, and the animation and frame now showing. "
            "An animation kept in its own .w3d file must be loaded first, with open_model."
        ),
        "inputSchema": {"type": "object", "properties": {"pid": _PID}},
    },
    {
        "name": "set_animation_frame",
        "description": (
            "Jump the displayed model to a frame of an animation through the frame scrubber "
            "and pause there. Follow with capture_views to screenshot that frame."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {
                "frame": {
                    "type": "integer",
                    "description": "Frame to stop on, from 0 to the animation's lastFrame.",
                },
                "animation": {
                    "type": "string",
                    "description": (
                        "Animation name or part of it, ignoring case. Omit to stay on the "
                        "animation already showing."
                    ),
                },
                "pid": _PID,
            },
            "required": ["frame"],
        },
    },
    {
        "name": "find_meshes",
        "description": (
            "Search the meshes of the displayed model by name. Returns each match's name, "
            "polygon and vertex counts, and whether it is hidden."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {
                "query": {
                    "type": "string",
                    "description": "Text the mesh name must contain, ignoring case. Omit to list every mesh.",
                },
                "pid": _PID,
            },
        },
    },
    {
        "name": "set_mesh_visibility",
        "description": (
            "Hide or show meshes of the displayed model in the viewport. The .w3d file is "
            "not changed. Screenshots taken afterwards leave hidden meshes out."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {
                "action": {
                    "type": "string",
                    "enum": ["hide", "show", "show_only", "show_all"],
                    "description": (
                        "hide or show the matching meshes; show_only shows them and hides "
                        "every other mesh; show_all shows every mesh and needs no meshes."
                    ),
                },
                "meshes": {
                    "type": "array",
                    "items": {"type": "string"},
                    "description": "Names or parts of names, ignoring case. A mesh matches when its name contains any entry.",
                },
                "pid": _PID,
            },
            "required": ["action"],
        },
    },
    {
        "name": "capture_views",
        "description": (
            "Screenshot the displayed model from view cube sides and return the images. "
            "The camera is put back afterwards."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {
                "views": {
                    "type": "array",
                    "items": {"type": "string"},
                    "description": (
                        "View cube regions: front, back, left, right, top, bottom, or an "
                        "edge or corner such as front-left or back-right-top. "
                        f"Defaults to {DEFAULT_VIEWS}."
                    ),
                },
                "fit": {
                    "type": "boolean",
                    "description": "Frame the whole model (default true). False keeps the current zoom and orbit centre.",
                },
                "max_size": {
                    "type": "integer",
                    "description": f"Longest image side in pixels (default {DEFAULT_MAX_SIZE}).",
                },
                "pid": _PID,
            },
        },
    },
]


class RpcError(Exception):
    def __init__(self, code: int, message: str) -> None:
        super().__init__(message)
        self.code = code
        self.message = message


def _text(value: Any) -> dict[str, Any]:
    return {"type": "text", "text": json.dumps(value, ensure_ascii=False)}


class McpServer:
    def __init__(self, bridge: W3DViewBridge | None = None) -> None:
        self.bridge = bridge or W3DViewBridge()

    def _get_viewer_state(self, arguments: Mapping[str, Any]) -> list[dict[str, Any]]:
        viewers = [
            {key: value for key, value in viewer.items() if key != "handle"}
            for viewer in self.bridge.list_viewers()
        ]
        state = self.bridge.call("get_state", pid=arguments.get("pid"))
        return [_text({"viewers": viewers, "selected": state})]

    def _read_model(self, arguments: Mapping[str, Any]) -> list[dict[str, Any]]:
        raw = self.bridge.call(
            "read_model", [str(arguments.get("path") or "")], pid=arguments.get("pid")
        )
        return [_text(decode_model(raw, arguments.get("mesh")))]

    def _read_textures(self, arguments: Mapping[str, Any]) -> list[dict[str, Any]]:
        raw = self.bridge.call(
            "read_model", [str(arguments.get("path") or "")], pid=arguments.get("pid")
        )
        return [_text(decode_textures(raw, arguments.get("mesh")))]

    def _open_model(self, arguments: Mapping[str, Any]) -> list[dict[str, Any]]:
        path = arguments.get("path")
        if not isinstance(path, str) or not path:
            raise BridgeError("bad_argument", "open_model needs a path.")
        return [_text(self.bridge.call("open_model", [path], pid=arguments.get("pid")))]

    @staticmethod
    def _animation_report(result: Mapping[str, Any]) -> dict[str, Any]:
        animations = [
            {**animation, "lastFrame": animation["frames"] - 1}
            for animation in result["animations"]
        ]
        current = None
        if result["currentAnimation"]:
            current = {
                "name": result["currentAnimation"],
                "frame": result["currentFrame"],
                "frames": result["currentFrames"],
                "state": result["state"],
            }
        return {
            "displayedObject": result["displayedObject"],
            "hierarchy": result["hierarchy"],
            "hasAnimations": bool(animations),
            "animationCount": len(animations),
            "animations": animations,
            "current": current,
        }

    def _list_animations(self, arguments: Mapping[str, Any]) -> list[dict[str, Any]]:
        result = self.bridge.call("list_animations", pid=arguments.get("pid"))
        return [_text(self._animation_report(result))]

    def _set_animation_frame(self, arguments: Mapping[str, Any]) -> list[dict[str, Any]]:
        frame = arguments.get("frame")
        if not isinstance(frame, int) or isinstance(frame, bool):
            raise BridgeError("bad_argument", "frame must be an integer.")
        pid = arguments.get("pid")

        name = ""
        wanted = str(arguments.get("animation") or "").lower()
        if wanted:
            names = [
                animation["name"]
                for animation in self.bridge.call("list_animations", pid=pid)["animations"]
            ]
            exact = [
                candidate
                for candidate in names
                if wanted in (candidate.lower(), candidate.lower().rpartition(".")[2])
            ]
            matches = exact or [candidate for candidate in names if wanted in candidate.lower()]
            if len(matches) != 1:
                problem = "No animation matches" if not matches else "Several animations match"
                raise BridgeError(
                    "no_match" if not matches else "ambiguous",
                    f"{problem} '{wanted}'. Animations: {names}",
                )
            name = matches[0]

        result = self.bridge.call("set_animation_frame", [name, str(frame)], pid=pid)
        return [_text(self._animation_report(result))]

    def _find_meshes(self, arguments: Mapping[str, Any]) -> list[dict[str, Any]]:
        result = self.bridge.call("list_meshes", pid=arguments.get("pid"))
        query = str(arguments.get("query") or "").lower()
        matches = [mesh for mesh in result["meshes"] if query in mesh["name"].lower()]
        return [
            _text(
                {
                    "displayedObject": result["displayedObject"],
                    "meshCount": len(result["meshes"]),
                    "matches": matches,
                }
            )
        ]

    def _set_mesh_visibility(self, arguments: Mapping[str, Any]) -> list[dict[str, Any]]:
        action = arguments.get("action")
        if action not in ("hide", "show", "show_only", "show_all"):
            raise BridgeError("bad_argument", "action must be hide, show, show_only or show_all.")
        queries = arguments.get("meshes") or []
        if not isinstance(queries, list) or not all(isinstance(query, str) for query in queries):
            raise BridgeError("bad_argument", "meshes must be a list of strings.")
        queries = [query.lower() for query in queries if query]

        pid = arguments.get("pid")
        names = [mesh["name"] for mesh in self.bridge.call("list_meshes", pid=pid)["meshes"]]
        matched = [name for name in names if any(query in name.lower() for query in queries)]
        if action != "show_all" and not matched:
            raise BridgeError(
                "no_match", f"No mesh name contains any of {queries}. Meshes: {names}"
            )

        if action == "show_all":
            result = self.bridge.call("set_meshes_hidden", ["0", *names], pid=pid)
        elif action == "show_only":
            others = [name for name in names if name not in matched]
            self.bridge.call("set_meshes_hidden", ["1", *others], pid=pid)
            result = self.bridge.call("set_meshes_hidden", ["0", *matched], pid=pid)
        else:
            result = self.bridge.call(
                "set_meshes_hidden", ["1" if action == "hide" else "0", *matched], pid=pid
            )

        return [
            _text(
                {
                    "displayedObject": result["displayedObject"],
                    "changed": names if action == "show_all" else matched,
                    "hidden": [mesh["name"] for mesh in result["meshes"] if mesh["hidden"]],
                    "visible": [mesh["name"] for mesh in result["meshes"] if not mesh["hidden"]],
                }
            )
        ]

    def _capture_views(self, arguments: Mapping[str, Any]) -> list[dict[str, Any]]:
        views = arguments.get("views") or DEFAULT_VIEWS
        if not isinstance(views, list) or not all(isinstance(view, str) for view in views):
            raise BridgeError("bad_argument", "views must be a list of strings.")
        max_size = arguments.get("max_size") or DEFAULT_MAX_SIZE
        if not isinstance(max_size, int) or max_size < 16:
            raise BridgeError("bad_argument", "max_size must be an integer of at least 16.")

        directory = work_directory()
        lines = ["1" if arguments.get("fit", True) else "0"]
        captures = []
        try:
            for index, view in enumerate(views):
                x, y, z = view_region(view)
                path = directory / f"capture-{index}.bmp"
                captures.append((view, path))
                lines.append(f"{x} {y} {z} {path}")
        except ValueError as error:
            raise BridgeError("bad_argument", str(error)) from error

        result = self.bridge.call("capture_views", lines, pid=arguments.get("pid"))
        model = re.sub(r"[^A-Za-z0-9_.-]", "_", result.get("displayedObject") or "model")

        content: list[dict[str, Any]] = []
        saved = []
        for view, bmp_path in captures:
            png, width, height = bmp_to_png(bmp_path.read_bytes(), max_size)
            bmp_path.unlink(missing_ok=True)
            png_path = directory / f"{model}-{view.lower()}.png"
            png_path.write_bytes(png)
            saved.append({"view": view, "path": str(png_path), "width": width, "height": height})
            content.append(
                {
                    "type": "image",
                    "data": base64.b64encode(png).decode("ascii"),
                    "mimeType": "image/png",
                }
            )
        content.insert(0, _text({"displayedObject": result.get("displayedObject"), "images": saved}))
        return content

    def call_tool(self, name: str, arguments: Any) -> dict[str, Any]:
        handlers = {
            "get_viewer_state": self._get_viewer_state,
            "read_model": self._read_model,
            "read_textures": self._read_textures,
            "open_model": self._open_model,
            "list_animations": self._list_animations,
            "set_animation_frame": self._set_animation_frame,
            "find_meshes": self._find_meshes,
            "set_mesh_visibility": self._set_mesh_visibility,
            "capture_views": self._capture_views,
        }
        handler = handlers.get(name)
        if handler is None:
            raise RpcError(-32602, f"Unknown tool: {name}")
        if arguments is None:
            arguments = {}
        if not isinstance(arguments, dict):
            raise RpcError(-32602, "arguments must be an object")

        try:
            return {"content": handler(arguments)}
        except BridgeError as error:
            return {
                "content": [_text({"error": {"code": error.code, "message": error.message}})],
                "isError": True,
            }
        except (OSError, ValueError, KeyError) as error:
            return {
                "content": [_text({"error": {"code": "internal", "message": repr(error)}})],
                "isError": True,
            }

    def dispatch(self, method: str, params: Any) -> Any:
        if params is None:
            params = {}
        if not isinstance(params, dict):
            raise RpcError(-32602, "params must be an object")

        if method == "initialize":
            requested = params.get("protocolVersion")
            return {
                "protocolVersion": requested
                if requested in SUPPORTED_PROTOCOL_VERSIONS
                else DEFAULT_PROTOCOL_VERSION,
                "capabilities": {"tools": {"listChanged": False}},
                "serverInfo": {"name": SERVER_NAME, "version": SERVER_VERSION},
                "instructions": INSTRUCTIONS,
            }
        if method == "ping":
            return {}
        if method == "tools/list":
            return {"tools": TOOL_DEFINITIONS}
        if method == "tools/call":
            name = params.get("name")
            if not isinstance(name, str) or not name:
                raise RpcError(-32602, "tools/call requires a non-empty name")
            return self.call_tool(name, params.get("arguments", {}))
        raise RpcError(-32601, f"Method not found: {method}")

    def handle_message(self, message: Any) -> dict[str, Any] | None:
        if not isinstance(message, dict) or not isinstance(message.get("method"), str):
            if isinstance(message, dict) and "id" not in message:
                return None
            request_id = message.get("id") if isinstance(message, dict) else None
            return _error(request_id, -32600, "Invalid Request")

        has_id = "id" in message
        try:
            result = self.dispatch(message["method"], message.get("params", {}))
        except RpcError as error:
            return _error(message.get("id"), error.code, error.message) if has_id else None
        except Exception as error:  # noqa: BLE001 - a server must answer every request
            return _error(message.get("id"), -32603, f"Internal error: {error!r}") if has_id else None
        return {"jsonrpc": "2.0", "id": message["id"], "result": result} if has_id else None


def _error(request_id: Any, code: int, message: str) -> dict[str, Any]:
    return {"jsonrpc": "2.0", "id": request_id, "error": {"code": code, "message": message}}


def main() -> int:
    try:
        serve(
            getattr(sys.stdin, "buffer", sys.stdin),
            getattr(sys.stdout, "buffer", sys.stdout),
            McpServer(),  # type: ignore[arg-type]
        )
    except (BrokenPipeError, KeyboardInterrupt):
        return 0
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
