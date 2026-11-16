# W3DView

W3DView is the model viewer. The contraZH build carries the
[W3DView+](https://github.com/triatomic/w3dviewerPlus) changes listed on
[contraZH Changes](contraZH-Changes.md), and it can be driven by an AI assistant through a Model
Context Protocol (MCP) server.

## Asset filter

The **Filter assets** box on the toolbar narrows the asset tree to the names that hold the typed
text, ignoring case.

| The text matches | The tree shows |
|---|---|
| An object's name | The object with all of its folders. |
| A mesh or a bone of an object | The object, opened, with only the matching rows under Meshes and Skeleton. A row that a match is nested under stays too. |
| An emitter, aggregate, primitive, sound or loose texture | That asset under its category. |

The filter changes the tree only. The model in the viewport stays as it is, and Show Only This Mesh
still hides every other mesh, including the ones the filter left without a row.

## MCP server

The assistant reads a model's meshes and materials, opens a model in the viewer, lists its
animations and jumps to a frame, searches and hides its meshes, and takes screenshots of it from
chosen sides.

The server has two halves.

* A bridge compiled into W3DView answers each request on the viewer's UI thread.
* A Python program, `tools/w3dview_mcp` in the contraZH repository, speaks MCP over stdio to the
assistant and passes each tool call to the bridge.

The bridge never writes a `.w3d` file. Its only effects on the viewer are loading a model, hiding
or showing meshes in the viewport, pausing on an animation frame, and moving the camera for a
screenshot. It puts the camera back.

## Requirements

| Need | Detail |
|---|---|
| W3DView | A contraZH build of `W3DViewZH.exe`, running. |
| Python | Python 3 on Windows. The server uses the standard library only, so there is nothing to install. |
| Repository | A checkout of contraZH, because the server runs from `tools/w3dview_mcp`. |
| MCP client | Any client that starts stdio servers. |

## Setup

### 1. Turn the viewer's side on

W3DView's **MCP** menu has two entries.

| Entry | Effect |
|---|---|
| Enable MCP Server | Turns the bridge on or off. It starts on, and the setting is remembered between sessions. |
| Server Information... | Shows the state of the bridge in this viewer. |

With the bridge off, the viewer is hidden from discovery and ignores requests.

### 2. Register the server with the client

The client must start `python -m tools.w3dview_mcp` with the repository root as its working
directory. The repository's `.mcp.json` already holds this entry.

```json
{
  "mcpServers": {
    "generals-w3dview": {
      "command": "python",
      "args": ["-m", "tools.w3dview_mcp"],
      "cwd": "C:\\path\\to\\contraZH"
    }
  }
}
```

Some clients have no working directory field. Give those an absolute Python path and put the
repository root in `PYTHONPATH`.

## Commands

The user types a request to the assistant in plain words, and the assistant picks the tool and
fills in its input. Each table below lists requests for one subject, the tool the assistant calls,
and the result field that holds the answer.

A request that names a file needs its full path, unless the file sits in a folder W3DView already
searches. A request that names no file refers to the model in the viewport. A request can name one
mesh, such as `TURRET01`, or ask about every mesh of the model.

### Viewer and counts

| The user types | Tool | Answer is in |
|---|---|---|
| `Is W3DView running?` | `get_viewer_state` | `viewers` |
| `What model is W3DView showing?` | `get_viewer_state` | `selected.displayedObject`, `selected.sourceFile` |
| `How many polys does the model have?` | `get_viewer_state` | `selected.polygons` |
| `How many vertices does the model have?` | `get_viewer_state` | `selected.vertices` |
| `How many meshes does UVDScorp.w3d have?` | `read_model` | `meshCount` |
| `How many polys and vertices does each mesh have?` | `read_model` | `meshCounts` |
| `Which mesh has the most polys?` | `read_model` | `meshCounts` |
| `Open C:\Mod\Art\W3D\UVDScorp.w3d in W3DView.` | `open_model` | `displayedObject` |

### Passes and shaders

| The user types | Tool | Answer is in |
|---|---|---|
| `How many passes does TURRET01 use?` | `read_model` | `passCount` |
| `How many passes does each mesh use?` | `read_model` | `passCount` of every mesh |
| `Which meshes use more than one pass?` | `read_model` | `passCount` of every mesh |
| `What shader is used per pass on TURRET01?` | `read_model` | `passes[].shader` |
| `What blend mode does pass 2 of TURRET01 use?` | `read_model` | `passes[1].shader.srcBlend`, `destBlend` |
| `Which meshes are alpha blended or additive?` | `read_model` | `passes[].shader.srcBlend`, `destBlend` |
| `Which meshes use alpha test?` | `read_model` | `passes[].shader.alphaTest` |
| `Does TURRET01 write depth, and what is its depth compare?` | `read_model` | `passes[].shader.depthWrite`, `depthCompare` |
| `What are the primary and secondary gradients of each pass?` | `read_model` | `passes[].shader.priGradient`, `secGradient` |
| `What detail colour and detail alpha functions does pass 1 use?` | `read_model` | `passes[0].shader.detailColorFunc`, `detailAlphaFunc` |

### Vertex materials and mapping

| The user types | Tool | Answer is in |
|---|---|---|
| `What vertex material does each pass of TURRET01 use?` | `read_model` | `passes[].vertexMaterial.name` |
| `What are the ambient, diffuse, specular and emissive colours of TURRET01?` | `read_model` | `passes[].vertexMaterial.ambient`, `diffuse`, `specular`, `emissive` |
| `What are the opacity, translucency and shininess of TURRET01?` | `read_model` | `passes[].vertexMaterial.opacity`, `translucency`, `shininess` |
| `Which meshes have Specular To Diffuse ticked?` | `read_model` | `passes[].vertexMaterial.specularToDiffuse` |
| `What is the stage 0 mapping type of TREADSL02, and what are its args?` | `read_model` | `passes[].vertexMaterial.stage0Mapping.type`, `args` |
| `What is the stage 1 mapping of each pass, with its args?` | `read_model` | `passes[].vertexMaterial.stage1Mapping.type`, `args` |
| `Which UV channel does stage 0 and stage 1 use on TURRET01?` | `read_model` | `stage0Mapping.uvChannel`, `stage1Mapping.uvChannel` |
| `Which meshes scroll their texture?` | `read_model` | `stage0Mapping.type` of `LINEAR_OFFSET` and its `args` |
| `Does TURRET01 have vertex colours or vertex alpha?` | `read_model` | `passes[].vertexColors` |

### Textures

| The user types | Tool | Answer is in |
|---|---|---|
| `Which textures does UVDScorp.w3d use, and where are the files?` | `read_textures` | `textures[].name`, `path` |
| `Which folders do this model's textures come from?` | `read_textures` | `textureDirectories` |
| `What is the path of the texture on TURRET01?` | `read_textures` | `meshes[].textures[].path` |
| `Which textures does each mesh use, on which pass and stage?` | `read_textures` | `meshes[].textures` |
| `Which meshes use Scorpion.tga?` | `read_textures` | `textures[].usedBy` |
| `Is any texture of this model missing?` | `read_textures` | `missingTextures` |
| `Which texture paths is W3DView searching?` | `read_textures` | `texturePaths` |
| `What texture is on stage 0 and stage 1 of each pass of TURRET01?` | `read_model` | `passes[].stage0Texture.name`, `stage1Texture.name` |
| `Is the texture of TREADSL02 clamped in U or V?` | `read_model` | `stage0Texture.clampU`, `clampV` |
| `Is any texture animated, and with how many frames and what rate?` | `read_model` | `stage0Texture.animType`, `frameCount`, `frameRate` |
| `Which textures have No LOD, Publish or Alpha Bitmap set?` | `read_model` | `stage0Texture.noLod`, `publish`, `alphaBitmap` |

### W3D properties

| The user types | Tool | Answer is in |
|---|---|---|
| `What are the W3D properties of TURRET01?` | `read_model` | `w3dProperties` |
| `Which meshes are hidden in the file?` | `read_model` | `w3dProperties.hidden` |
| `Which meshes are two-sided?` | `read_model` | `w3dProperties.twoSided` |
| `Which meshes cast a shadow?` | `read_model` | `w3dProperties.castShadow` |
| `Which meshes have collision, and of which types?` | `read_model` | `w3dProperties.collision` |
| `What is the geometry type of each mesh?` | `read_model` | `w3dProperties.geometryType` |
| `What is the sort level of TURRET01?` | `read_model` | `w3dProperties.sortLevel` |
| `What is the user text of TURRET01?` | `read_model` | `w3dProperties.userText` |

### Animations

| The user types | Tool | Answer is in |
|---|---|---|
| `Does the model in W3DView have animations?` | `list_animations` | `hasAnimations` |
| `How many animations does the model have, and what are they called?` | `list_animations` | `animationCount`, `animations[].name` |
| `How many frames does the A2 animation have?` | `list_animations` | `animations[].frames` |
| `What is the frame rate and length of each animation?` | `list_animations` | `animations[].frameRate`, `seconds` |
| `Which animation and frame is W3DView showing now?` | `list_animations` | `current` |
| `Jump to frame 50.` | `set_animation_frame` | `current.frame` |
| `Go to frame 20 of the A2 animation.` | `set_animation_frame` with `animation` | `current` |
| `Jump to frame 50 and take a screenshot.` | `set_animation_frame`, then `capture_views` | The returned images |
| `Screenshot frames 0, 50 and 100 of the animation from the front.` | `set_animation_frame`, then `capture_views`, for each frame | The returned images |
| `Load C:\Mod\Art\W3D\AVScorp_A.w3d so its animation shows on the model.` | `open_model` | `displayedObject` |

### Meshes in the viewport

| The user types | Tool | Answer is in |
|---|---|---|
| `Find the turret meshes of the model.` | `find_meshes` | `matches` |
| `Which meshes are hidden in the viewport right now?` | `find_meshes` | `matches[].hidden` |
| `Hide the treads and the armor.` | `set_mesh_visibility` with `hide` | `hidden` |
| `Show the armor again.` | `set_mesh_visibility` with `show` | `visible` |
| `Show only the barrel.` | `set_mesh_visibility` with `show_only` | `visible` |
| `Show every mesh again.` | `set_mesh_visibility` with `show_all` | `visible` |

### Screenshots

| The user types | Tool | Answer is in |
|---|---|---|
| `Screenshot the model from the front, left and top.` | `capture_views` | The returned images |
| `Show me the back right top corner of the model.` | `capture_views` | The returned images |
| `Screenshot the front without changing the zoom.` | `capture_views` with `fit` false | The returned images |
| `Screenshot the model from all six sides at 512 pixels.` | `capture_views` with `max_size` 512 | The returned images |
| `Hide the armor and screenshot the model from the left.` | `set_mesh_visibility`, then `capture_views` | The returned images |
| `Open UVDScorp.w3d and show it from all six sides.` | `open_model`, then `capture_views` | The returned images |

### Tool inputs

The tool inputs those requests turn into look like this.

```json
{ "name": "get_viewer_state", "arguments": {} }
```

```json
{ "name": "read_model", "arguments": {} }
```

```json
{ "name": "read_model", "arguments": { "path": "C:\\Mod\\Art\\W3D\\UVDScorp.w3d", "mesh": "TURRET01" } }
```

```json
{ "name": "read_textures", "arguments": { "path": "C:\\Mod\\Art\\W3D\\UVDScorp.w3d" } }
```

```json
{ "name": "read_textures", "arguments": { "mesh": "TURRET01" } }
```

```json
{ "name": "open_model", "arguments": { "path": "C:\\Mod\\Art\\W3D\\UVDScorp.w3d" } }
```

```json
{ "name": "list_animations", "arguments": {} }
```

```json
{ "name": "set_animation_frame", "arguments": { "frame": 50 } }
```

```json
{ "name": "set_animation_frame", "arguments": { "frame": 20, "animation": "A2" } }
```

```json
{ "name": "find_meshes", "arguments": { "query": "turret" } }
```

```json
{ "name": "set_mesh_visibility", "arguments": { "action": "hide", "meshes": ["tread", "armor"] } }
```

```json
{ "name": "set_mesh_visibility", "arguments": { "action": "show_only", "meshes": ["barrel"] } }
```

```json
{ "name": "set_mesh_visibility", "arguments": { "action": "show_all" } }
```

```json
{ "name": "capture_views", "arguments": { "views": ["front", "left", "top"] } }
```

```json
{ "name": "capture_views", "arguments": { "views": ["back-right-top"], "fit": false, "max_size": 512 } }
```

Each input is described under [Tools](#tools).

## Tools

Every tool takes an optional `pid`, the process id of the W3DView to use. It can be left out while
one viewer is running. With several open, call `get_viewer_state` first and pass a `pid` it lists.

| Tool | Arguments | Does |
|---|---|---|
| `get_viewer_state` | none | Lists running viewers that have the bridge on, and the model the selected one displays. |
| `read_model` | `path`, `mesh` | Reads a `.w3d` file's meshes, counts, W3D properties, passes and materials. |
| `read_textures` | `path`, `mesh` | Reads the textures each mesh uses and the path of each texture file. |
| `open_model` | `path` | Loads a `.w3d` file and displays its top-level object. |
| `list_animations` | none | Lists the displayed model's animations with their frame counts. |
| `set_animation_frame` | `frame`, `animation` | Jumps the displayed model to a frame of an animation and pauses there. |
| `find_meshes` | `query` | Searches the meshes of the displayed model by name. |
| `set_mesh_visibility` | `action`, `meshes` | Hides or shows meshes of the displayed model. |
| `capture_views` | `views`, `fit`, `max_size` | Screenshots the displayed model from view cube sides. |

### get_viewer_state

| Field | Meaning |
|---|---|
| `viewers` | Each running viewer's `pid`, window `title` and `bridgeVersion`. |
| `selected.displayedObject` | Name of the object in the viewport, empty when there is none. |
| `selected.sourceFile` | File the object was loaded from. |
| `selected.polygons` | Polygon total of the displayed object. |
| `selected.vertices` | Vertex total of the displayed object. |
| `selected.texturePaths` | The folders set under File > Texture Path, which W3DView searches for textures. |

The two totals are the ones the `7` key draws in the viewport.

### read_model

`read_model` parses the chunks of the `.w3d` file, so the model does not have to be displayed.

| Argument | Meaning |
|---|---|
| `path` | An absolute `.w3d` path, or a bare file name that W3DView finds through its search paths. Leave it out to read the displayed model. |
| `mesh` | Details only the meshes whose name contains this text. The totals, mesh counts and texture list stay complete. |

The result describes the file first.

| Field | Meaning |
|---|---|
| `file`, `resolvedPath` | The path asked for and the file W3DView opened. |
| `topLevelName` | The HLod name, or the first mesh's name in a file without one. |
| `meshCount` | Number of meshes in the file. |
| `totalPolygons`, `totalVertices` | Sums over every mesh in the file, hidden and collision meshes included. |
| `meshCounts` | `name`, `polygons` and `vertices` of each mesh. |
| `texturesUsed` | Each texture name with the file it resolves to, or `null` when it is missing. |
| `textureDirectories` | The folders those files sit in. |
| `texturePaths` | The folders set under File > Texture Path. |
| `meshes` | One entry per mesh, described below. |

Each mesh has these fields.

| Field | Meaning |
|---|---|
| `name` | `Container.Mesh`. |
| `polygons`, `vertices` | Counts from the mesh header. |
| `w3dProperties` | The W3D export settings, in the next table. |
| `passCount` | Number of material passes. |
| `passes` | One entry per pass. |
| `textures` | Every texture name in the mesh's texture table. |

`w3dProperties` holds what the W3D export options in 3ds Max set.

| Field | Meaning |
|---|---|
| `geometryType` | `NORMAL`, `CAMERA_ALIGNED`, `CAMERA_ORIENTED`, `SKIN`, or an obsolete box or shadow type. |
| `hidden`, `twoSided`, `castShadow` | The Hide, 2-Side and Shadow flags. |
| `collision` | Any of `PHYSICAL`, `PROJECTILE`, `VIS`, `CAMERA`, `VEHICLE`. |
| `shatterable`, `npatchable` | The Shatter and NPatch flags. |
| `prelit` | Lighting wrappers in the mesh, if any. |
| `sortLevel` | Static sort level. |
| `surfaceType` | Surface type number of the first triangle. 13 is the default. |
| `userText` | The object's user properties text from Max. |
| `attributesHex` | The raw attribute bits. |

Each pass has these fields.

| Field | Meaning |
|---|---|
| `vertexMaterial` | The pass's vertex material. |
| `shader` | The pass's shader. |
| `stage0Texture`, `stage1Texture` | The texture of each stage, or `null`. |
| `vertexColors` | Whether the pass carries per-vertex `diffuseAndAlpha`, `diffuseIllumination` or `specular` colours. |
| `otherVertexMaterials`, `otherShaders`, `otherStage0Textures`, `otherStage1Textures` | Present when a per-vertex or per-triangle array assigns more than one entry to the pass. |

A vertex material has these fields.

| Field | Meaning |
|---|---|
| `name` | Material name. |
| `ambient`, `diffuse`, `specular`, `emissive` | Colours as `[r, g, b]`, 0 to 255. |
| `shininess`, `opacity`, `translucency` | The three scalar settings. |
| `specularToDiffuse` | The Specular To Diffuse checkbox. |
| `useDepthCue`, `argbEmissiveOnly`, `depthCueToAlpha` | The remaining attribute flags. |
| `stage0Mapping`, `stage1Mapping` | Each stage's mapping `type`, its `args` text and its `uvChannel`. |

A mapping `type` is one of `UV`, `ENVIRONMENT`, `CHEAP_ENVIRONMENT`, `SCREEN`, `LINEAR_OFFSET`,
`SILHOUETTE`, `SCALE`, `GRID`, `ROTATE`, `SINE_LINEAR_OFFSET`, `STEP_LINEAR_OFFSET`,
`ZIGZAG_LINEAR_OFFSET`, `WS_CLASSIC_ENV`, `WS_ENVIRONMENT`, `GRID_CLASSIC_ENV`, `GRID_ENVIRONMENT`,
`RANDOM`, `EDGE`, `BUMPENV`, `GRID_WS_CLASSIC_ENV` or `GRID_WS_ENVIRONMENT`. The `args` text is the
mapper argument string exactly as the file stores it, for example `UPerSec=0.5`.

A shader has these fields.

| Field | Values |
|---|---|
| `srcBlend` | `ZERO`, `ONE`, `SRC_ALPHA`, `ONE_MINUS_SRC_ALPHA` |
| `destBlend` | `ZERO`, `ONE`, `SRC_COLOR`, `ONE_MINUS_SRC_COLOR`, `SRC_ALPHA`, `ONE_MINUS_SRC_ALPHA`, `SRC_COLOR_PREFOG` |
| `depthWrite`, `alphaTest`, `texturing`, `secGradient` | `true` or `false` |
| `depthCompare` | `PASS_NEVER`, `PASS_LESS`, `PASS_EQUAL`, `PASS_LEQUAL`, `PASS_GREATER`, `PASS_NOTEQUAL`, `PASS_GEQUAL`, `PASS_ALWAYS` |
| `priGradient` | `DISABLE`, `MODULATE`, `ADD`, `BUMPENVMAP`, `BUMPENVMAPLUMINANCE`, `MODULATE2X` |
| `detailColorFunc`, `postDetailColorFunc` | `DISABLE`, `DETAIL`, `SCALE`, `INVSCALE`, `ADD`, `SUB`, `SUBR`, `BLEND`, `DETAILBLEND`, `ADDSIGNED`, `ADDSIGNED2X`, `SCALE2X`, `MODALPHAADDCOLOR` |
| `detailAlphaFunc`, `postDetailAlphaFunc` | `DISABLE`, `DETAIL`, `SCALE`, `INVSCALE` |

A texture has these fields.

| Field | Meaning |
|---|---|
| `name` | File name as the model stores it. |
| `resolvedPath` | The file W3DView found, or `null`. |
| `publish`, `noLod`, `clampU`, `clampV`, `alphaBitmap`, `bumpmap` | The texture flags. |
| `mipLevels` | `ALL`, `2`, `3` or `4`. |
| `hint` | `BASE`, `EMISSIVE`, `ENVIRONMENT` or `SHINY_MASK`. |
| `animType`, `frameCount`, `frameRate` | Animation settings. `animType` is `LOOP`, `PINGPONG`, `ONCE` or `MANUAL`. |

A texture without the optional info chunk reports `"settings": "defaults"` in place of the flag and
animation fields.

W3DView looks for a texture in its search paths first and beside the model second. It tries the
stored name and then the same name with a `.dds` extension, as the game does.

#### UV channels

A `.w3d` file stores texture coordinates per pass and stage. It does not store the 3ds Max map
channel number. `uvChannel` numbers the mesh's distinct coordinate sets from 1 in file order, so two
stages with the same number were exported from one channel. A stage without coordinates reports
`null`.

### read_textures

`read_textures` answers which textures a model's meshes use and where each file is. It takes the
same `path` and `mesh` arguments as `read_model`, and it reads the file the same way.

| Field | Meaning |
|---|---|
| `file`, `resolvedPath` | The path asked for and the file W3DView opened. |
| `texturePaths` | The folders set under File > Texture Path, which W3DView searches. |
| `textureDirectories` | The folders the model's textures were found in. |
| `missingTextures` | Names of the textures W3DView could not find. |
| `textures` | Each texture's `name`, full `path`, `directory`, and the meshes it is `usedBy`. |
| `meshes` | Each mesh's `name` and its `textures`. |

Each entry of a mesh's `textures` has these fields.

| Field | Meaning |
|---|---|
| `name` | File name as the model stores it. |
| `path` | The file W3DView found, or `null` when it is missing. |
| `pass` | The pass that uses the texture, counted from 1. |
| `stage` | The stage it sits on, 0 or 1. |

A texture that sits in a mesh's texture table without a pass using it reports `null` for `pass`
and `stage`. The stored name and the found file can differ in extension, because W3DView falls back
from `.tga` to `.dds` as the game does.

### open_model

`open_model` takes an absolute `path`. It loads the file as File > Open does, selects the top-level
object in the asset tree, and returns the same fields as `selected` in `get_viewer_state`.

A file that holds animations alone has no object to show. `open_model` loads its animations and
leaves the model in the viewport as it is, so `list_animations` then lists them.

### list_animations

`list_animations` lists the animations W3DView has loaded for the skeleton of the model in the
viewport. An animation kept in its own `.w3d` file is not listed until that file is loaded, which
`open_model` does.

| Field | Meaning |
|---|---|
| `displayedObject` | Name of the object in the viewport. |
| `hierarchy` | Name of its skeleton. An animation belongs to the model when it names the same skeleton. |
| `hasAnimations` | Whether any animation is loaded for the model. |
| `animationCount` | How many are loaded. |
| `animations` | Each animation's `name`, `frames`, `lastFrame`, `frameRate` and length in `seconds`. |
| `current` | The animation now on the model, or `null`. |

An animation name is `Skeleton.Animation`, such as `ABAFSW_A2.ABAFSW_A2`. `frames` is the frame
count, and frames are numbered from 0 to `lastFrame`.

`current` holds the `name` of the animation on the model, the `frame` it is on, its `frames`, and
the `state`, which is `playing`, `paused` or `stopped`.

### set_animation_frame

`set_animation_frame` moves the model to one frame and pauses playback there, through the frame
scrubber. The frame holds until playback is started again, so a `capture_views` call after it
screenshots that frame.

| Argument | Meaning |
|---|---|
| `frame` | The frame to stop on, from 0 to the animation's `lastFrame`. |
| `animation` | Animation name or part of it, ignoring case. Leave it out to stay on the animation already on the model. |

The result has the same fields as `list_animations`, with `current` on the new frame.

Naming an animation that is not on the model starts it first. Meshes hidden with
`set_mesh_visibility` stay hidden across that. A `frame` outside the animation, a name that matches
nothing, or a name that matches several animations changes nothing and returns an error that lists
the choices.

### find_meshes

`find_meshes` looks through the meshes of the model in the viewport. `query` is text the mesh name
must contain, ignoring case, and leaving it out lists every mesh.

| Field | Meaning |
|---|---|
| `displayedObject` | Name of the object in the viewport. |
| `meshCount` | Number of meshes the object has. |
| `matches` | Each matching mesh's `name`, `polygons`, `vertices` and whether it is `hidden`. |

A mesh name is `Object.Mesh`, such as `UVDSCORP.TURRET01`.

### set_mesh_visibility

`set_mesh_visibility` hides or shows meshes of the model in the viewport. The eye beside each mesh
in the asset tree follows. The `.w3d` file is not changed, and opening another model starts with
every mesh shown again.

| Argument | Meaning |
|---|---|
| `action` | One of the four actions below. |
| `meshes` | List of names or parts of names, ignoring case. A mesh matches when its name contains any entry. |

| Action | Effect |
|---|---|
| `hide` | Hides the matching meshes. |
| `show` | Shows the matching meshes. |
| `show_only` | Shows the matching meshes and hides every other mesh. |
| `show_all` | Shows every mesh. It needs no `meshes`. |

The result lists the meshes that were `changed`, and every mesh that is now `hidden` or `visible`.
A call whose `meshes` match nothing changes nothing and returns the mesh names to choose from.

A screenshot from `capture_views` leaves hidden meshes out.

### capture_views

| Argument | Default | Meaning |
|---|---|---|
| `views` | `front-left-top`, `front`, `left`, `top` | List of view cube regions to capture. |
| `fit` | `true` | Frames the whole model. `false` keeps the current zoom and orbit centre. |
| `max_size` | 1024 | Longest side of each returned image, in pixels. |

A view is a face of the view cube, or two or three faces joined by `-` for an edge or a corner.

| Face | Looks from |
|---|---|
| `front` | +X |
| `back` | -X |
| `left` | -Y |
| `right` | +Y |
| `top` | +Z |
| `bottom` | -Z |

`back-right` is an edge and `front-left-top` is a corner. A view cannot name both faces of one axis.

Each view puts the camera where a click on that part of the view cube ends, without the turn
animation. The tool returns one PNG per view and also saves it as `<object>-<view>.png` in
`%TEMP%\w3dview-mcp`. A later capture of the same object and view replaces the file.

The capture is the viewport as drawn, so it includes the view cube, the `7` key totals and any
overlay that is switched on. The camera returns to where it was after the last view.

## Limits

| Limit | Value |
|---|---|
| Request size | 64 KiB |
| Time for one command | 30 seconds |
| Image shrink | By a whole factor, nearest pixel |

## Safety

* The bridge opens no network port. It is desktop automation between two programs of the same
Windows user, so untrusted programs should not run in that desktop session.
* A request can make W3DView read any `.w3d` file the user can read, and write a response file
beside the request file and a `.bmp` capture at the path the request names. The Python server keeps
both in `%TEMP%\w3dview-mcp`.
* Turning **Enable MCP Server** off stops all of it.
