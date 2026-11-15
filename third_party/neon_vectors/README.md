# Neon vector support

Independently authored C++17 vector objects and bounded beam mesh generation.
Gridrunner uses BeamMesh and Stroke to tessellate semantic beam commands; its
host supplies GLES shaders, accumulation and resolve. Grid text uses the original
bitmap assets, not this module's vector-font API.

The module also provides transformed objects, paths/arcs, vector-font layout and
object serialization. Logical mesh coordinates span 640x480; stroke width is
screen-relative. BeamMesh bounds stroke counts and optional history. Gridrunner
uses immediate meshes with history disabled. See vector_scene.h for contracts.

Implementation contributions use the project contribution terms. Character-form
provenance remains separate in FONT-PROVENANCE.md. No emulator implementation or
original executable is part of this module.
