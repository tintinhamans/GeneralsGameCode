"""Windows transport to a running W3DView.

W3DView marks its main window with a property. A request is a small text file
whose path is delivered with WM_COPYDATA. W3DView writes the JSON answer to
that path plus ".response" before the message returns.
"""

from __future__ import annotations

import json
import tempfile
import uuid
from pathlib import Path
from typing import Any, Sequence


WINDOW_PROPERTY = "GeneralsW3DViewMcp"
COPYDATA_MAGIC = 0x5733444D
WM_COPYDATA = 0x004A
SMTO_ABORTIFHUNG = 0x0002
DEFAULT_TIMEOUT_MS = 30_000


class BridgeError(RuntimeError):
    """A bridge error with a stable machine-readable code."""

    def __init__(self, code: str, message: str) -> None:
        super().__init__(message)
        self.code = code
        self.message = message


def work_directory() -> Path:
    directory = Path(tempfile.gettempdir()) / "w3dview-mcp"
    directory.mkdir(parents=True, exist_ok=True)
    return directory


class W3DViewBridge:
    def __init__(self, timeout_ms: int = DEFAULT_TIMEOUT_MS) -> None:
        import ctypes
        from ctypes import wintypes

        self._ctypes = ctypes
        self._timeout_ms = timeout_ms
        self._user32 = ctypes.WinDLL("user32", use_last_error=True)
        self._enum_callback_type = ctypes.WINFUNCTYPE(
            wintypes.BOOL, wintypes.HWND, wintypes.LPARAM
        )
        self._user32.EnumWindows.argtypes = [self._enum_callback_type, wintypes.LPARAM]
        self._user32.EnumWindows.restype = wintypes.BOOL
        self._user32.GetPropW.argtypes = [wintypes.HWND, wintypes.LPCWSTR]
        self._user32.GetPropW.restype = wintypes.HANDLE
        self._user32.GetWindowTextW.argtypes = [
            wintypes.HWND,
            wintypes.LPWSTR,
            ctypes.c_int,
        ]
        self._user32.GetWindowThreadProcessId.argtypes = [
            wintypes.HWND,
            ctypes.POINTER(wintypes.DWORD),
        ]

        class CopyDataStruct(ctypes.Structure):
            _fields_ = [
                ("dwData", ctypes.c_size_t),
                ("cbData", wintypes.DWORD),
                ("lpData", ctypes.c_void_p),
            ]

        self._copy_data_type = CopyDataStruct
        self._user32.SendMessageTimeoutW.argtypes = [
            wintypes.HWND,
            wintypes.UINT,
            ctypes.c_void_p,
            ctypes.POINTER(CopyDataStruct),
            wintypes.UINT,
            wintypes.UINT,
            ctypes.POINTER(ctypes.c_size_t),
        ]
        self._user32.SendMessageTimeoutW.restype = ctypes.c_size_t

    def list_viewers(self) -> list[dict[str, Any]]:
        ctypes = self._ctypes
        viewers: list[dict[str, Any]] = []

        def visit(handle: int, _parameter: int) -> bool:
            version = self._user32.GetPropW(handle, WINDOW_PROPERTY)
            if version:
                title = ctypes.create_unicode_buffer(512)
                self._user32.GetWindowTextW(handle, title, 512)
                process_id = ctypes.c_ulong()
                self._user32.GetWindowThreadProcessId(handle, ctypes.byref(process_id))
                viewers.append(
                    {
                        "pid": process_id.value,
                        "title": title.value,
                        "bridgeVersion": int(version),
                        "handle": handle,
                    }
                )
            return True

        self._user32.EnumWindows(self._enum_callback_type(visit), 0)
        return viewers

    def _select(self, pid: int | None) -> dict[str, Any]:
        viewers = self.list_viewers()
        if pid is not None:
            viewers = [viewer for viewer in viewers if viewer["pid"] == pid]
        if not viewers:
            raise BridgeError(
                "no_viewer",
                "No running W3DView with the MCP bridge was found. Start W3DViewZH.exe "
                "from a build that includes W3DViewMcpBridge.",
            )
        return viewers[0]

    def call(self, command: str, lines: Sequence[str] = (), pid: int | None = None) -> Any:
        """Send one command and return its result, or raise BridgeError."""
        for line in lines:
            if "\n" in line or "\r" in line:
                raise BridgeError("bad_argument", "Arguments must be single lines.")

        ctypes = self._ctypes
        viewer = self._select(pid)
        request_path = work_directory() / f"{uuid.uuid4().hex}.request"
        response_path = Path(str(request_path) + ".response")

        try:
            # W3DView is an ANSI program, so its paths travel in the ANSI code page.
            request_path.write_text(
                "\n".join([command, *lines]), encoding="mbcs", newline="\n"
            )

            path_buffer = ctypes.create_unicode_buffer(str(request_path))
            copy_data = self._copy_data_type(
                dwData=COPYDATA_MAGIC,
                cbData=ctypes.sizeof(path_buffer),
                lpData=ctypes.cast(path_buffer, ctypes.c_void_p),
            )
            handled = ctypes.c_size_t()
            sent = self._user32.SendMessageTimeoutW(
                viewer["handle"],
                WM_COPYDATA,
                None,
                ctypes.byref(copy_data),
                SMTO_ABORTIFHUNG,
                self._timeout_ms,
                ctypes.byref(handled),
            )
            if not sent:
                raise BridgeError(
                    "timeout",
                    f"W3DView (pid {viewer['pid']}) did not answer within "
                    f"{self._timeout_ms} ms. A modal dialog may be open.",
                )
            if not handled.value or not response_path.exists():
                raise BridgeError("no_response", "W3DView did not write a response.")

            envelope = json.loads(response_path.read_text(encoding="utf-8"))
        finally:
            request_path.unlink(missing_ok=True)
            response_path.unlink(missing_ok=True)

        if not envelope.get("ok"):
            error = envelope.get("error", {})
            raise BridgeError(
                str(error.get("code", "remote_error")),
                str(error.get("message", "W3DView reported an error.")),
            )
        return envelope.get("result")
