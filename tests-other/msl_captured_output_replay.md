# Captured output replay compiler: proposed contract

**Design proposal only; no replay compiler API is implemented.** The implemented producer getter and portable fragment API are documented in [msl_per_vertex_input.md](msl_per_vertex_input.md).

## API and compilation state

Proposed C++ entry point on a **fresh** `CompilerMSL`, constructed from the original vertex SPIR-V:

```cpp
struct MSLCapturedOutputReplayBinding
{
    uint32_t vertex_buffer_index;
    uint32_t occurrence_buffer_index;
    uint32_t draw_parameters_buffer_index;
    uint32_t primitive_index_location;
};

std::string compile_captured_output_replay(const MSLCapturedVertexLayout &layout, const MSLCapturedOutputReplayBinding &binding);
```

Configure the original entry point, specialization constants, output masks/remaps and rasterization options before calling. MSL must be at least 2.4. Capture, vertex-as-compute and disabled rasterization must be off for replay. The layout comes from the successfully compiled capture producer. Preserve a separate unmodified IR/configuration for replay: ordinary `compile()` mutates IR, builds and sorts interface structs, localizes globals and installs application fixup hooks. Reusing that mutated capture compiler is not a safe implementation shortcut.

The generated function would build the ordinary rasterizing vertex stage-out interface through existing MSL backend interface logic, load each required user output and builtin from the captured record, and add one private `uint [[user(locnN), flat]]` key. It must preserve the original linkage locations/components, physical stage types, interpolation attributes, builtin attributes and output masks. It must neither emit nor execute application SPIR-V function bodies, application resource access or side effects. Backend rasterization fixups are a separate, necessary operation.

The captured struct cannot simply be returned unchanged. Its members can be packed differently (for example Component=2 vec2 captured in a float4), and capture disables normal rasterization paths. The scalar layout does not contain interpolation qualifiers or the complete raster interface; original SPIR-V and matching interface configuration are required inputs. Any mismatch or missing required field must fail compilation, not silently zero the output.

## Proposed buffer ABI

All buffer indices are explicit, distinct Metal slots in [0, 30]. Reject collisions with any required backend resources. Bind buffer bases with alignment at least 4. `primitive_index_location` must be unused and must equal the portable fragment binding's private location.

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

The caller guarantees IDs are at least their origins, all addresses are in bounds, and table rows are initialized. Origins normalize the **replay draw**, not the original application's vertex indices. The runtime may use an expanded nonindexed replay starting at zero; then origins are zero. Single-instance draws may use `occurrences_per_instance = 0`. Instanced draws use an explicit occurrence block per instance, or deliberately share a block if records and keys are identical. Draw parameters are runtime data, not shader specialization/cache constants.

`key` is exactly the portable fragment ABI's absolute triplet index: fragment vertex `j` reads `primitive_indices[3 * uint64(key) + j]`. There is no inferred primitive division, index topology, base vertex, instance or view offset. All replay vertices of one primitive must carry the same key. Shared indexed vertices generally require separate occurrences per primitive to satisfy this; the backend does not create that expansion. The occurrence table also makes the record mapping independent of whether capture used a vertex function or a compute kernel.

## Fixups and initial limits

Capture currently skips `CompilerMSL::emit_fixup()`, including clip-space conversion, Y inversion, default PointSize and depth/viewport emulation. Replay must apply the selected rasterization transforms **exactly once**, after loading the captured values. It must not invoke the capture compiler's application output hooks a second time. Simple clip-space/Y fixups can be included in a first implementation. Runtime-dependent depth/viewport emulation must either receive its documented backend resources or be explicitly rejected; no implicit state is available from the scalar layout alone. A missing captured default PointSize may only be synthesized from an explicitly supported fixed configuration.

Recommended first implementation scope:

- Original vertex entry point; no tessellation, geometry, mesh, patch records, automatic multiview or layered-rendering emulation. Exporting a vertex-as-compute record does not imply tessellation replay support.
- User scalar/vector physical members of Half/Short/UShort/Float/Int/UInt, including already flattened interface members. No new support for retained user arrays, matrices, nested structs or 64-bit data.
- Position (required), PointSize, ClipDistance, Layer and ViewportIndex, with explicit type/shape checks and pipeline/device feature checks by the caller. Other builtins may be reflected by the layout getter, but would be rejected by an initial replay generator until individually supported and verified.
- Exact layout validation, including required user/builtin coverage, duplicate keys, offset bounds/alignment, physical type compatibility, buffer/varying collisions and supported rasterization options. Extra valid captured fields can be ignored.
- Separate namespaces for user fields and builtins. Load each builtin via `(builtin, array_index, component)`; Layer/ViewportIndex use their exported physical UInt types.

The runtime owns topology/restart/degenerate handling, provoking vertex order, original capture index formulas, instance/view mapping, bounds, synchronization, lifetime and final pipeline compatibility. It must ensure application side effects happen only during capture. Values not written by the original shader remain undefined. This proposal does not establish runtime portability on every MSL 2.4 device.

## Proof needed before calling replay implemented

Add native C++ tests that generate capture and replay from the same fixtures/configuration, then compile both with Metal 2.4. Check the normal raster interface plus the private key, field reconstruction including builtins, component packing, flat/noperspective qualifiers, and clip-space/Y transforms exactly once. Include a source shader with an application storage side effect and prove replay emits no application resource/body. Negative tests should cover missing Position, missing scalar fields, key collisions and unsupported builtin/options. Producer-layout tests and existing MSL references must continue to pass. GPU capture/replay and MoltenVK integration remain separate validation work.
