# Captured output replay compiler

The C++ `CompilerMSL::compile_captured_output_replay()` API emits a normal rasterizing vertex shader which reads captured output without executing the application shader. The supported subset and checks below are implemented and tested on CPU and with Metal syntax compilation; this is not a complete runtime capture/replay system. The producer getter and portable fragment API are documented in [msl_per_vertex_input.md](msl_per_vertex_input.md). No C API or CLI entry point is added.

## API and compilation state

C++ entry point on a **fresh** `CompilerMSL`, constructed from the original vertex SPIR-V:

```cpp
struct MSLCapturedOutputReplayBinding
{
    uint32_t vertex_buffer_index = ~0u;
    uint32_t occurrence_buffer_index = ~0u;
    uint32_t draw_parameters_buffer_index = ~0u;
    uint32_t primitive_index_location = ~0u;
};

std::string compile_captured_output_replay(const MSLCapturedVertexLayout &layout, const MSLCapturedOutputReplayBinding &binding);
```

Configure the original entry point, specialization constants, output masks/remaps and rasterization options before calling. MSL must be at least 2.4. Capture, vertex-as-compute and disabled rasterization must be off for replay. The layout comes from the successfully compiled capture producer. Preserve a separate unmodified IR/configuration for replay: ordinary `compile()` mutates IR, builds and sorts interface structs, localizes globals and installs application fixup hooks. The method rejects a compiler on which compilation has already started, and consumes its own compiler even on failure. A subsequent ordinary `compile()` is also rejected. The capture-layout getter remains specific to successful ordinary capture compilation, not replay.

The generated function builds the ordinary rasterizing vertex stage-out interface through existing MSL backend interface logic, loads each required user output and builtin from the captured record, and adds one private `uint [[user(locnN), flat]]` key. It preserves the ordinary VS's linkage locations/components, physical stage types, builtin attributes and output masks. Application interpolation follows the ordinary MSL backend convention: qualifiers are selected on the fragment interface, not added to vertex outputs. The tests compare the emitted normal/replay interfaces with flat and noperspective source outputs. The private key is explicitly flat. Application SPIR-V function bodies, resources, input attributes and side effects are not emitted. Backend clip-space/Y rasterization fixups are applied after the loads.

The captured struct cannot simply be returned unchanged. Its members can be packed differently (for example Component=2 vec2 captured in a float4), and capture disables normal rasterization paths. The scalar layout does not contain interpolation qualifiers or the complete raster interface; original SPIR-V and matching interface configuration are required inputs. Any mismatch or missing required field must fail compilation, not silently zero the output.

## Buffer ABI

All buffer indices are explicit, distinct Metal slots in [0, 30]. The only emitted resources are these three classic buffers, even if application argument buffers are enabled; application binding slots can be reused because their resources are absent. Options requiring additional implicit replay resources are rejected. Bind buffer bases with alignment at least 4. `primitive_index_location` must be unused in both the captured layout and raster interface, and must equal the portable fragment binding's private location.

| Resource | Representation |
| --- | --- |
| Captured records | Read-only bytes; `layout.stride` and scalar offsets/types from the producer getter. |
| Replay occurrences | Read-only tightly packed uint32 pairs: `(absolute_record_index, absolute_primitive_key)`, **8 bytes** per occurrence. |
| Draw parameters | Four uint32 scalars, **16 bytes**: `(occurrence_base, vertex_id_origin, instance_id_origin, occurrences_per_instance)`. |

With Metal's actual `vertex_id` and `instance_id`, use unsigned 64-bit address arithmetic:

```text
v = uint64(vertex_id)   - vertex_id_origin
i = uint64(instance_id) - instance_id_origin
occurrence = occurrence_base + i * uint64(occurrences_per_instance) + v
record = occurrences[2 * occurrence + 0]
key    = occurrences[2 * occurrence + 1]
scalar_address = captured + uint64(record) * layout.stride + scalar.byte_offset
```

The caller guarantees IDs are at least their origins, address arithmetic does not overflow, all addresses are in bounds, and table rows are initialized. Origins normalize the **replay draw**, not the original application's vertex indices. The runtime may use an expanded nonindexed replay starting at zero; then origins are zero. Single-instance draws may use `occurrences_per_instance = 0`. Instanced draws use an explicit occurrence block per instance, or deliberately share a block if records and keys are identical. Draw parameters are runtime data, not shader specialization/cache constants.

`key` is exactly the portable fragment ABI's absolute triplet index: fragment vertex `j` reads `primitive_indices[3 * uint64(key) + j]`. There is no inferred primitive division, index topology, base vertex, instance or view offset. All replay vertices of one primitive must carry the same key. Shared indexed vertices generally require separate occurrences per primitive to satisfy this; the backend does not create that expansion. The occurrence table also makes the record mapping independent of whether capture used a vertex function or a compute kernel.

## Fixups and initial limits

Capture skips `CompilerMSL::emit_fixup()`, including clip-space conversion, Y inversion, default PointSize and depth/viewport emulation. Replay applies `Options::vertex.fixup_clipspace` and `Options::vertex.flip_vert_y` **exactly once**, after loading captured Position, by calling the existing raster fixup emitter. It never invokes application output hooks. Runtime-dependent depth/viewport emulation and `enable_point_size_default` are explicitly rejected. A captured application PointSize is supported; missing outputs are not silently synthesized.

Implemented subset:

- Original vertex entry point; no tessellation, geometry, mesh, patch records, automatic multiview or layered-rendering emulation. Exporting a vertex-as-compute record does not imply tessellation replay support.
- User scalar/vector physical members of Half/Short/UShort/Float/Int/UInt, including members of a plain interface block. Original user arrays, matrices and nested structs are rejected before flattening, including when masked; 64-bit and other scalar types are unsupported.
- Position (required), PointSize, ClipDistance, Layer and ViewportIndex, with explicit type/shape checks and pipeline/device feature checks by the caller. Other active output builtins or builtin descriptors are rejected even if masked. ClipDistance's raster array and optional `user(clipN)` aliases are all loaded from the same captured builtin descriptors.
- Layout validation includes required user/builtin coverage, duplicate keys, offset bounds/alignment, physical type compatibility, overlapping user/builtin scalars, buffer/varying collisions and supported rasterization options. Extra valid captured fields can be ignored. Diagnostics identify missing user Location/Component or builtin/index/component; no partial shader is returned on failure.
- Separate namespaces for user fields and builtins. Load each builtin via `(builtin, array_index, component)`; Layer/ViewportIndex use their exported physical UInt types.

The runtime owns topology/restart/degenerate handling, provoking vertex order, original capture index formulas, instance/view mapping, bounds, synchronization, lifetime and final pipeline compatibility. It must ensure application side effects happen only during capture. Values not written by the original shader remain undefined. This compiler subset does not establish runtime portability on every MSL 2.4 device. In particular, syntax validation is not pipeline validation for PointSize, Layer or ViewportIndex.

## CPU and Metal checks

`spirv-cross-msl-capture-replay-test` is a native C++ project target registered with CTest. It checks simple/complex capture, normal raster and replay shaders with both native/default arrays and vertex/compute capture. Interfaces are compared after removing the private key. The side-effect fixture has a positive control atomic SSBO write in capture, absent from replay along with the application resource and helper. It also covers masks, builtin remapping, ClipDistance aliases, all four clip-space/Y combinations, and invalid layouts/bindings/options/compiler reuse/stages.

```sh
glslangValidator -V --target-env vulkan1.1 tests-other/msl_capture_replay_effects.vert -o tests-other/msl_capture_replay_effects.spv
glslangValidator -V --target-env vulkan1.1 tests-other/msl_capture_replay_unsupported.tese -o tests-other/msl_capture_replay_unsupported.spv
cmake --build BUILD --target spirv-cross-msl-capture-replay-test spirv-cross-msl-capture-layout-test spirv-cross-msl-per-vertex-input-test
ctest --test-dir BUILD -R '^spirv-cross-msl-(capture-replay|capture-layout|per-vertex-input)-test$' --output-on-failure
mkdir -p OUT
BUILD/spirv-cross-msl-capture-replay-test tests-other/msl_capture_layout_simple.spv tests-other/msl_capture_layout_complex.spv tests-other/msl_capture_replay_effects.spv tests-other/msl_capture_replay_unsupported.spv tests-other/msl_capture_layout_unsupported.spv OUT
for shader in OUT/*.metal; do
    xcrun metal -x metal -std=macos-metal2.4 -fsyntax-only -fno-modules "$shader" || exit
done
```

The test writes 49 MSL shaders: capture, normal raster and replay variants, plus four fixup variants. These syntax checks and the existing layout/fragment tests pass; GPU replay execution and MoltenVK integration remain separate validation work. TES is a rejection fixture, not a supported replay stage.
