"""Turns the raw .w3d material numbers W3DView reports into named settings.

Names follow the W3D_*, W3DVERTMAT_*, W3DSHADER_* and W3DTEXTURE_* constants in
WW3D2/w3d_file.h, which are also what the 3ds Max W3D material rollouts edit.
"""

from __future__ import annotations

import math
import struct
import zlib
from typing import Any, Mapping, Sequence


MAPPING_TYPES = (
    "UV",
    "ENVIRONMENT",
    "CHEAP_ENVIRONMENT",
    "SCREEN",
    "LINEAR_OFFSET",
    "SILHOUETTE",
    "SCALE",
    "GRID",
    "ROTATE",
    "SINE_LINEAR_OFFSET",
    "STEP_LINEAR_OFFSET",
    "ZIGZAG_LINEAR_OFFSET",
    "WS_CLASSIC_ENV",
    "WS_ENVIRONMENT",
    "GRID_CLASSIC_ENV",
    "GRID_ENVIRONMENT",
    "RANDOM",
    "EDGE",
    "BUMPENV",
    "GRID_WS_CLASSIC_ENV",
    "GRID_WS_ENVIRONMENT",
)

DEPTH_COMPARE = (
    "PASS_NEVER",
    "PASS_LESS",
    "PASS_EQUAL",
    "PASS_LEQUAL",
    "PASS_GREATER",
    "PASS_NOTEQUAL",
    "PASS_GEQUAL",
    "PASS_ALWAYS",
)
DEST_BLEND = (
    "ZERO",
    "ONE",
    "SRC_COLOR",
    "ONE_MINUS_SRC_COLOR",
    "SRC_ALPHA",
    "ONE_MINUS_SRC_ALPHA",
    "SRC_COLOR_PREFOG",
)
SRC_BLEND = ("ZERO", "ONE", "SRC_ALPHA", "ONE_MINUS_SRC_ALPHA")
PRI_GRADIENT = (
    "DISABLE",
    "MODULATE",
    "ADD",
    "BUMPENVMAP",
    "BUMPENVMAPLUMINANCE",
    "MODULATE2X",
)
DETAIL_COLOR = (
    "DISABLE",
    "DETAIL",
    "SCALE",
    "INVSCALE",
    "ADD",
    "SUB",
    "SUBR",
    "BLEND",
    "DETAILBLEND",
    "ADDSIGNED",
    "ADDSIGNED2X",
    "SCALE2X",
    "MODALPHAADDCOLOR",
)
DETAIL_ALPHA = ("DISABLE", "DETAIL", "SCALE", "INVSCALE")

GEOMETRY_TYPES = {
    0x00000000: "NORMAL",
    0x00010000: "CAMERA_ALIGNED",
    0x00020000: "SKIN",
    0x00030000: "SHADOW (obsolete)",
    0x00040000: "AABOX (obsolete)",
    0x00050000: "OBBOX (obsolete)",
    0x00060000: "CAMERA_ORIENTED",
}
COLLISION_TYPES = (
    (0x00000010, "PHYSICAL"),
    (0x00000020, "PROJECTILE"),
    (0x00000040, "VIS"),
    (0x00000080, "CAMERA"),
    (0x00000100, "VEHICLE"),
)
PRELIT_TYPES = (
    (0x01000000, "UNLIT"),
    (0x02000000, "VERTEX"),
    (0x04000000, "LIGHTMAP_MULTI_PASS"),
    (0x08000000, "LIGHTMAP_MULTI_TEXTURE"),
)
TEXTURE_HINTS = ("BASE", "EMISSIVE", "ENVIRONMENT", "SHINY_MASK")
TEXTURE_ANIM_TYPES = ("LOOP", "PINGPONG", "ONCE", "MANUAL")
MIP_LEVELS = {0x00: "ALL", 0x40: "2", 0x80: "3", 0xC0: "4"}

UV_CHANNEL_NOTE = (
    "A .w3d file stores texture coordinates per pass and stage, never the 3ds Max "
    "map channel number. uvChannel numbers the mesh's distinct coordinate sets in "
    "file order from 1, so stages that share a number were exported from one channel."
)


def _name(table: Sequence[str], value: int) -> str:
    if 0 <= value < len(table):
        return table[value]
    return f"UNKNOWN({value})"


def _flags(table: Sequence[tuple[int, str]], value: int) -> list[str]:
    return [name for bit, name in table if value & bit]


def decode_mesh_properties(mesh: Mapping[str, Any]) -> dict[str, Any]:
    attributes = int(mesh["attributes"])
    geometry = attributes & 0x00FF0000
    return {
        "geometryType": GEOMETRY_TYPES.get(geometry, f"UNKNOWN(0x{geometry:08X})"),
        "hidden": bool(attributes & 0x00001000),
        "twoSided": bool(attributes & 0x00002000),
        "castShadow": bool(attributes & 0x00008000),
        "collision": _flags(COLLISION_TYPES, attributes),
        "shatterable": bool(attributes & 0x10000000),
        "npatchable": bool(attributes & 0x20000000),
        "prelit": _flags(PRELIT_TYPES, attributes),
        "sortLevel": mesh["sortLevel"],
        "surfaceType": mesh["surfaceType"],
        "userText": mesh["userText"],
        "attributesHex": f"0x{attributes:08X}",
    }


def decode_shader(shader: Mapping[str, Any]) -> dict[str, Any]:
    return {
        "srcBlend": _name(SRC_BLEND, shader["srcBlend"]),
        "destBlend": _name(DEST_BLEND, shader["destBlend"]),
        "depthWrite": bool(shader["depthMask"]),
        "depthCompare": _name(DEPTH_COMPARE, shader["depthCompare"]),
        "alphaTest": bool(shader["alphaTest"]),
        "texturing": bool(shader["texturing"]),
        "priGradient": _name(PRI_GRADIENT, shader["priGradient"]),
        "secGradient": bool(shader["secGradient"]),
        "detailColorFunc": _name(DETAIL_COLOR, shader["detailColorFunc"]),
        "detailAlphaFunc": _name(DETAIL_ALPHA, shader["detailAlphaFunc"]),
        "postDetailColorFunc": _name(DETAIL_COLOR, shader["postDetailColorFunc"]),
        "postDetailAlphaFunc": _name(DETAIL_ALPHA, shader["postDetailAlphaFunc"]),
    }


def decode_texture(texture: Mapping[str, Any]) -> dict[str, Any]:
    attributes = int(texture["attributes"])
    decoded: dict[str, Any] = {
        "name": texture["name"],
        "resolvedPath": texture["resolvedPath"] or None,
    }
    if not texture["hasInfo"]:
        # Without the optional info chunk every setting is at its default.
        decoded["settings"] = "defaults"
        return decoded
    decoded.update(
        {
            "publish": bool(attributes & 0x0001),
            "noLod": bool(attributes & 0x0004),
            "clampU": bool(attributes & 0x0008),
            "clampV": bool(attributes & 0x0010),
            "alphaBitmap": bool(attributes & 0x0020),
            "mipLevels": MIP_LEVELS[attributes & 0x00C0],
            "hint": _name(TEXTURE_HINTS, (attributes & 0xFF00 & ~0x1000) >> 8),
            "bumpmap": bool(attributes & 0x1000),
            "animType": _name(TEXTURE_ANIM_TYPES, texture["animType"]),
            "frameCount": texture["frameCount"],
            "frameRate": texture["frameRate"],
        }
    )
    return decoded


def decode_vertex_material(
    material: Mapping[str, Any], uv_sets: Sequence[int]
) -> dict[str, Any]:
    attributes = int(material["attributes"])

    def stage(index: int, mapping: int, args: str) -> dict[str, Any]:
        uv_set = uv_sets[index]
        return {
            "type": _name(MAPPING_TYPES, mapping),
            "args": args,
            "uvChannel": uv_set + 1 if uv_set >= 0 else None,
        }

    return {
        "name": material["name"],
        "ambient": material["ambient"],
        "diffuse": material["diffuse"],
        "specular": material["specular"],
        "emissive": material["emissive"],
        "shininess": material["shininess"],
        "opacity": material["opacity"],
        "translucency": material["translucency"],
        "specularToDiffuse": bool(attributes & 0x00000004),
        "useDepthCue": bool(attributes & 0x00000001),
        "argbEmissiveOnly": bool(attributes & 0x00000002),
        "depthCueToAlpha": bool(attributes & 0x00000008),
        "stage0Mapping": stage(0, (attributes >> 16) & 0xFF, material["mapperArgs0"]),
        "stage1Mapping": stage(1, (attributes >> 8) & 0xFF, material["mapperArgs1"]),
    }


def _pick(table: Sequence[Any], index: int) -> Any:
    return table[index] if 0 <= index < len(table) else None


def decode_pass(
    mesh: Mapping[str, Any],
    raw_pass: Mapping[str, Any],
    textures: Sequence[Any],
    shaders: Sequence[Any],
) -> dict[str, Any]:
    uv_sets = (raw_pass["stage0UvSet"], raw_pass["stage1UvSet"])
    materials = [
        decode_vertex_material(mesh["vertexMaterials"][index], uv_sets)
        for index in raw_pass["vertexMaterialIds"]
        if 0 <= index < len(mesh["vertexMaterials"])
    ]
    pass_shaders = [_pick(shaders, index) for index in raw_pass["shaderIds"]]
    stage_textures = [
        [_pick(textures, index) for index in raw_pass[key]]
        for key in ("stage0TextureIds", "stage1TextureIds")
    ]

    decoded: dict[str, Any] = {
        "vertexMaterial": materials[0] if materials else None,
        "shader": pass_shaders[0] if pass_shaders else None,
        "stage0Texture": stage_textures[0][0] if stage_textures[0] else None,
        "stage1Texture": stage_textures[1][0] if stage_textures[1] else None,
        "vertexColors": {
            "diffuseAndAlpha": raw_pass["hasDCG"],
            "diffuseIllumination": raw_pass["hasDIG"],
            "specular": raw_pass["hasSCG"],
        },
    }
    # A per-vertex or per-triangle id array assigns more than one entry to the pass.
    if len(materials) > 1:
        decoded["otherVertexMaterials"] = materials[1:]
    if len(pass_shaders) > 1:
        decoded["otherShaders"] = pass_shaders[1:]
    for stage, entries in enumerate(stage_textures):
        if len(entries) > 1:
            decoded[f"otherStage{stage}Textures"] = entries[1:]
    return decoded


def decode_mesh(mesh: Mapping[str, Any]) -> dict[str, Any]:
    textures = [decode_texture(texture) for texture in mesh["textures"]]
    shaders = [decode_shader(shader) for shader in mesh["shaders"]]
    return {
        "name": mesh["name"],
        "polygons": mesh["triangles"],
        "vertices": mesh["vertices"],
        "w3dProperties": decode_mesh_properties(mesh),
        "passCount": mesh["passCount"],
        "passes": [
            decode_pass(mesh, raw_pass, textures, shaders) for raw_pass in mesh["passes"]
        ],
        "textures": [texture["name"] for texture in textures],
    }


def _directory(path: str | None) -> str | None:
    if not path:
        return None
    return path.replace("/", "\\").rpartition("\\")[0] or None


def texture_directories(paths: Sequence[str | None]) -> list[str]:
    """The distinct folders the found textures sit in, in first-use order."""
    directories: list[str] = []
    for path in paths:
        directory = _directory(path)
        if directory and directory.lower() not in (known.lower() for known in directories):
            directories.append(directory)
    return directories


def decode_textures(raw: Mapping[str, Any], mesh_filter: str | None = None) -> dict[str, Any]:
    """Which textures each mesh uses, on which pass and stage, and where each file is."""
    wanted = (mesh_filter or "").lower()
    textures: dict[str, dict[str, Any]] = {}
    meshes = []

    for mesh in raw["meshes"]:
        table = mesh["textures"]
        used: list[dict[str, Any]] = []
        placed = set()
        for pass_index, raw_pass in enumerate(mesh["passes"]):
            for stage, key in enumerate(("stage0TextureIds", "stage1TextureIds")):
                for index in raw_pass[key]:
                    if 0 <= index < len(table):
                        placed.add(index)
                        used.append(
                            {
                                "name": table[index]["name"],
                                "path": table[index]["resolvedPath"] or None,
                                "pass": pass_index + 1,
                                "stage": stage,
                            }
                        )
        # A texture can sit in the mesh's table without any pass using it.
        for index, texture in enumerate(table):
            if index not in placed:
                used.append(
                    {
                        "name": texture["name"],
                        "path": texture["resolvedPath"] or None,
                        "pass": None,
                        "stage": None,
                    }
                )

        for texture in table:
            entry = textures.setdefault(
                texture["name"],
                {
                    "name": texture["name"],
                    "path": texture["resolvedPath"] or None,
                    "directory": _directory(texture["resolvedPath"]),
                    "usedBy": [],
                },
            )
            if mesh["name"] not in entry["usedBy"]:
                entry["usedBy"].append(mesh["name"])

        if wanted in mesh["name"].lower():
            meshes.append({"name": mesh["name"], "textures": used})

    return {
        "file": raw["file"],
        "resolvedPath": raw["resolvedPath"],
        "texturePaths": raw.get("texturePaths", []),
        "textureDirectories": texture_directories(
            [texture["path"] for texture in textures.values()]
        ),
        "missingTextures": [name for name, texture in textures.items() if not texture["path"]],
        "textures": list(textures.values()),
        "meshes": meshes,
    }


def decode_model(raw: Mapping[str, Any], mesh_filter: str | None = None) -> dict[str, Any]:
    meshes = raw["meshes"]
    if mesh_filter:
        wanted = mesh_filter.lower()
        meshes = [mesh for mesh in meshes if wanted in mesh["name"].lower()]

    textures: dict[str, str | None] = {}
    for mesh in raw["meshes"]:
        for texture in mesh["textures"]:
            textures.setdefault(texture["name"], texture["resolvedPath"] or None)

    return {
        "file": raw["file"],
        "resolvedPath": raw["resolvedPath"],
        "topLevelName": raw["topLevelName"],
        "meshCount": len(raw["meshes"]),
        # Every mesh in the file counts, hidden and collision meshes included.
        "totalPolygons": sum(mesh["triangles"] for mesh in raw["meshes"]),
        "totalVertices": sum(mesh["vertices"] for mesh in raw["meshes"]),
        "meshCounts": [
            {"name": mesh["name"], "polygons": mesh["triangles"], "vertices": mesh["vertices"]}
            for mesh in raw["meshes"]
        ],
        "texturesUsed": textures,
        "textureDirectories": texture_directories(list(textures.values())),
        "texturePaths": raw.get("texturePaths", []),
        "meshes": [decode_mesh(mesh) for mesh in meshes],
        "uvChannelNote": UV_CHANNEL_NOTE,
    }


FACE_NORMALS = {
    "front": (1, 0, 0),
    "back": (-1, 0, 0),
    "left": (0, -1, 0),
    "right": (0, 1, 0),
    "top": (0, 0, 1),
    "bottom": (0, 0, -1),
}


def view_region(view: str) -> tuple[int, int, int]:
    """Sum the face normals a view cube region touches, e.g. "front-left-top"."""
    region = [0, 0, 0]
    faces = [face for face in view.lower().replace("_", "-").replace(" ", "-").split("-") if face]
    if not faces:
        raise ValueError("A view needs at least one face name.")
    for face in faces:
        normal = FACE_NORMALS.get(face)
        if normal is None:
            raise ValueError(
                f"Unknown face '{face}' in view '{view}'. Use front, back, left, "
                "right, top, bottom, or two or three of them joined by '-'."
            )
        for axis in range(3):
            if normal[axis] and region[axis]:
                raise ValueError(f"View '{view}' names one axis twice.")
            region[axis] += normal[axis]
    return region[0], region[1], region[2]


def bmp_to_png(data: bytes, max_size: int) -> tuple[bytes, int, int]:
    """Convert W3DView's 32-bit BMP capture to an RGB PNG, shrunk to fit max_size."""
    if data[:2] != b"BM":
        raise ValueError("Not a BMP file.")
    (offset,) = struct.unpack_from("<I", data, 10)
    width, height = struct.unpack_from("<ii", data, 18)
    (bits,) = struct.unpack_from("<H", data, 28)
    if bits != 32 or width <= 0 or height == 0:
        raise ValueError("Unsupported BMP layout.")

    top_down = height < 0
    height = abs(height)
    step = max(1, math.ceil(max(width, height) / max_size))
    out_width = (width + step - 1) // step
    stride = width * 4

    rows = bytearray()
    out_height = 0
    for y in range(0, height, step):
        source_y = y if top_down else height - 1 - y
        row = data[offset + source_y * stride : offset + (source_y + 1) * stride]
        line = bytearray(out_width * 3)
        line[0::3] = row[2 :: 4 * step]
        line[1::3] = row[1 :: 4 * step]
        line[2::3] = row[0 :: 4 * step]
        rows.append(0)
        rows += line
        out_height += 1

    def chunk(kind: bytes, payload: bytes) -> bytes:
        return (
            struct.pack(">I", len(payload))
            + kind
            + payload
            + struct.pack(">I", zlib.crc32(kind + payload))
        )

    png = (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", out_width, out_height, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(bytes(rows), 6))
        + chunk(b"IEND", b"")
    )
    return png, out_width, out_height
