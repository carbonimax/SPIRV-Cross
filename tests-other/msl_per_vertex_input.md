# Portable captured PerVertexKHR input ABI

This is a compiler-side subset, not a capture/replay implementation or a claim of GPU support on all Macs. Configure `CompilerMSL` before compilation. The C++ API is intended for callers such as MoltenVK; no C API or CLI layout syntax is added.

## Selecting the path

- Default: no PerVertexKHR support is enabled.
- Native: `Options::supports_per_vertex_fragment_input = true`, MSL >= 4.0, and the caller has verified Apple10+ `vertex_value` support and the native triangle contract. MSL version alone is insufficient.
- Portable: call `set_msl_per_vertex_input_buffer(layout, binding)`, use MSL >= 2.4, and leave the native option false. This path never introduces `vertex_value` or `[[primitive_id]]`.
- Enabling both paths for active PerVertexKHR inputs throws. No automatic hardware selection occurs.
- `needs_per_vertex_input_buffer()` after successful compilation tells the caller whether these resources are required. An enabled portable configuration with no active PerVertexKHR input adds no resources and does not change generated MSL. Existing builtin feature requirements remain applicable.

## Producer and resource contract

`MSLCapturedVertexLayout::stride` is the actual byte stride of a captured producer record. Each `MSLCapturedVertexComponent` specifies `(location, component, byte_offset, scalar_type)` for one physical scalar. Offsets are relative to the record start. Components may be noncontiguous; vector padding, unused outputs, and holes are not inferred from the fragment shader. A normal Metal `float3` may occupy 16 bytes even though only 12 bytes are read. A packed or reordered producer requires its own correct offsets and stride. This API does not export or infer the producer layout.

`MSLPerVertexInputBinding` specifies:

- `vertex_buffer_index`: `const device uchar*` containing captured records.
- `primitive_index_buffer_index`: `const device uint*` containing exactly three consecutive uint32 record indices per primitive (12 bytes per entry). **Do not use an array of Metal uint3**, which has a different stride.
- `primitive_index_location`: reserved `user(locnN)` interface location. The replay/rasterizing shader must output a `uint` varying with the same value at every vertex of each primitive. Its integral interpolation is flat in Metal. SPIRV-Cross adds the matching fragment member.

For fragment key `K` and local vertex `v`:

```text
record = indices[uint64(K) * 3 + v]
scalar = load<scalar_type>(vertices + uint64(record) * stride + byte_offset)
```

`K` is an **absolute triplet index in the bound table**, not a byte offset, uint element offset, hardware PrimitiveId, or instance ID. The caller includes draw/instance/view identity in K and in the record indices. There is no separate instance offset argument. Binding offsets may select a draw's table/capture slice; K and indices are relative to those bound slices. Both bound bases must be aligned to at least four bytes. The table and every accessed scalar must be within the bound buffers. The compiler uses 64-bit address arithmetic to avoid uint32 multiplication wraparound; this does not enable 64-bit shader input types. Bounds, initialization, lifetime and memory visibility are the caller's responsibility.

Example for a producer whose `float3 color` occupies bytes 0-11 of a 32-byte record:

```cpp
spirv_cross::MSLCapturedVertexLayout layout;
layout.stride = 32;
for (uint32_t component = 0; component < 3; component++)
{
    spirv_cross::MSLCapturedVertexComponent field;
    field.location = 0;
    field.component = component;
    field.byte_offset = 4 * component;
    field.scalar_type = spirv_cross::SPIRType::Float;
    layout.components.push_back(field);
}
spirv_cross::MSLPerVertexInputBinding binding;
binding.vertex_buffer_index = 28;
binding.primitive_index_buffer_index = 29;
binding.primitive_index_location = 15;
auto options = compiler.get_msl_options();
options.set_msl_version(2, 4);
options.supports_per_vertex_fragment_input = false;
compiler.set_msl_options(options);
compiler.set_msl_per_vertex_input_buffer(layout, binding);
std::string msl = compiler.compile();
```

MoltenVK must reserve both fragment buffer slots and the private interface location, supply the physical layout from the actual final producer, and include this configuration in shader/pipeline cache identity. `is_msl_shader_input_used()` still reports captured source locations; the private location is not exposed as an application input. The replay shader need not interpolate those captured inputs to the fragment. This compiler does not modify the producer or replay shader.

The runtime owns primitive assembly and ordering, base vertex/index handling, restart, degenerates, provoking vertex rules, instance/view selection, and point/line duplication into triplets. Shared indexed vertices may need separate replay occurrences to carry distinct primitive keys. Capture must occur at the last relevant vertex-producing stage. Replay must not repeat application shader side effects. Writes must be visible before fragment reads, including the requirements of the active render pass and transient attachments. None of these runtime properties is proven by syntax tests.

## Supported subset and validation

Supported: variable-decorated PerVertexKHR arrays of 1-3 elements, each a scalar or vector of `Half`, `Short`, `UShort`, `Float`, `Int` or `UInt`; scalar-exact producer types; Location/Component; dynamic indexing; forwarding local arrays to functions; ordinary inputs; classic bindings and argument buffers; SPIRV-Cross arrays and native MSL arrays. Existing arrays are reconstructed before the shader body; the ordinary access-chain path is preserved.

Rejected: inner arrays, matrices, structs/blocks, member-decorated PerVertexKHR, nonliteral outer sizes, 64-bit or other scalar types, missing locations/mappings, mismatched scalar types, captured input remapping through `add_msl_shader_input`, duplicate logical fields, overlapping physical scalars, misaligned or out-of-stride scalars, invalid or colliding resource indices, private location collisions, and fragment output capture. Extra producer scalar fields are allowed but must satisfy the same layout rules. Buffer indices must be distinct and in [0, 30]; collision checks include buffer arrays, actual argument buffer bindings and implicit fragment buffers. The runtime must also reserve Metal's vertex descriptor buffer slots where applicable.

No GPU execution, image comparison, pipeline linking on hardware, topology validation or performance measurement is part of this test. In particular, compiling MSL 2.4 is not proof of runtime portability, synchronization or ordering across all devices.

## Runnable CPU checks

`spirv-cross-msl-per-vertex-input-test` is registered with CTest when the existing SPIRV-Cross test prerequisites are enabled. It consumes the checked-in `msl_per_vertex_input.spv`; regenerate it with:

```sh
glslangValidator -V --target-env vulkan1.1 tests-other/msl_per_vertex_input.frag -o tests-other/msl_per_vertex_input.spv
spirv-val --target-env vulkan1.1 tests-other/msl_per_vertex_input.spv
ctest --test-dir BUILD -R '^spirv-cross-msl-per-vertex-input-test$' --output-on-failure
```

The executable can also write four MSL variants (classic/argument buffers, default/native arrays):

```sh
mkdir -p OUT
BUILD/spirv-cross-msl-per-vertex-input-test tests-other/msl_per_vertex_input.spv OUT
for shader in OUT/*.metal; do
    xcrun metal -x metal -std=macos-metal2.4 -fsyntax-only -fno-modules "$shader" || exit
done
```

An optional third argument loads the standalone single-color PerVertex reproducer and uses its explicit 32-byte producer layout. Unsupported fixtures can be checked with `TEST fixture.spv --reject 'expected diagnostic substring'`. Use the existing native composites and 64-bit fixtures to exercise unsupported types. Optimizing the typed fixture with `spirv-opt -O` and rerunning the test exercises the same ABI after optimizer transformations.
