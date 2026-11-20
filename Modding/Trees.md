# Map trees for modders

The trees a map places (objects drawn by `W3DTreeDraw`, such as the pines of Alpine Assault) are not drawn one by one.
The engine copies every tree in view, already placed, scaled and lit, into shared buffers and draws each buffer at
once. Zero Hour had ONE such buffer: 30,000 vertices and 60,000 indices for all the trees in view together. That was
plenty for its low-poly trees; a detailed tree filled it after a few copies, and every tree past that point was simply
not drawn.

This build keeps up to **64 buffers**, each 30,000 vertices and 65,000 indices (three indices a triangle; a buffer's
counts are 16-bit, so 65,535 is the ceiling). Only the first is made at map load; the others are made the first time
the trees in view need them.

---

## 1. What a tree model can be

`W3DTreeDraw`'s `ModelName` names a W3D model: a plain mesh, or an HLOD whose first sub-object is the tree's mesh.
Anything else is not drawn. The engine takes from the mesh:

- the vertices (no bones: a tree does not animate; it sways and topples as a whole),
- the normals (the engine lights each vertex on the CPU from them),
- the vertex colours, if the mesh has them (multiplied into that light: paint shade into the crown here),
- UV set 0, clamped to 0..1, and the texture named by `TextureName`.

Trees are drawn two-sided with an alpha test: a card of needles needs no back face.

## 2. The limits

| | Zero Hour | this build |
|---|---|---|
| one tree, the most it can be | ~29,990 vertices, ~19,990 triangles | ~29,990 vertices, ~21,660 triangles |
| all the trees in view together | 30,000 vertices / 20,000 triangles | 64 buffers of the above |

A tree must fit in one buffer: a model past the per-tree limit is not drawn, and neither is any tree the engine would
have copied after it. Keep well under it.

How many trees fit in one buffer is the smaller of 30,000 / its vertices and 65,000 / (3 x its triangles). This build's
remade Alpine pine (2,869 vertices, 3,080 triangles) fits 7 to a buffer, so 64 buffers draw ~450 of them in view.

## 3. What a detailed tree costs

The trees in view are copied again whenever one comes into view or leaves it - while the camera scrolls. That copy runs
on the CPU, vertex by vertex, so its cost grows with the vertices of the trees on screen. Measured on Alpine Assault with
the remade pine (the forest in view, ~200 pines on the map): the copying cost 0.17 ms a frame while scrolling, against a
test build that copied every tree once and never again. That build drew the trees out of view too and lost 0.24 ms a
frame standing still, so the copy stayed. The drawing itself grows with the triangles of the trees in view.
