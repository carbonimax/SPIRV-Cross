# Portable fragment barycentrics

Explicit C++ opt-in for `BuiltInBaryCoordKHR` and `BuiltInBaryCoordNoPerspKHR`, including their NV aliases. Both can be active together. This generates MSL 2.4 user varyings and never requires Metal `barycentric_coord`. Without the setter, the native backend, restrictions and emitted attributes are unchanged. No C API, CLI option, extension advertisement, MoltenVK change or GPU-support claim is included.

```cpp
CompilerMSL compiler(spirv);
auto options = compiler.get_msl_options();
options.set_msl_version(2, 4);
compiler.set_msl_options(options);
MSLFragmentBarycentricInputBinding barycentrics;
barycentrics.perspective_location = 12;
barycentrics.no_perspective_location = 14;
compiler.set_msl_fragment_barycentric_input(barycentrics);
std::string msl = compiler.compile();
```

Configure before compilation. Only active builtins require a location; the other may remain `~0u`. When both are active, reserve distinct locations. Private locations must not collide with active application inputs (including matrix columns, Component inputs and captured PerVertex inputs), any `add_msl_shader_input` mapping at that location, or the active `MSLPerVertexInputBinding::primitive_index_location`. Locations are not auto-allocated or exposed as application inputs by `is_msl_shader_input_used()`. Original SPIR-V BuiltIn reflection is retained. Include the opt-in and both locations in shader/pipeline cache identity.

## Producer contract

The rasterizing producer supplies a **float3** at each configured active location. Each triangle corner writes the following values to **both** varyings:

| Corner in the original PerVertexKHR order | Perspective location | No-perspective location |
| --- | --- | --- |
| 0 | `(1, 0, 0)` | `(1, 0, 0)` |
| 1 | `(0, 1, 0)` | `(0, 1, 0)` |
| 2 | `(0, 0, 1)` | `(0, 0, 1)` |

For the example configuration, the producer fields are `float3 ... [[user(locn12)]]` and `float3 ... [[user(locn14)]]`. The fragment selects perspective versus no-perspective interpolation. Supply the same unscaled one-hot basis at both locations: do not premultiply by clip `w`, divide by `w`, or normalize it yourself. The producer must preserve the application's clip-space Position, especially its `w`.

At the selected evaluation position, let `lambda[j]` be screen-space triangle weights and `w[j]` the corners' clip-space `w`. Interpolating the basis yields:

```text
BaryCoordKHR[j]        = (lambda[j] / w[j]) / sum_k(lambda[k] / w[k])
BaryCoordNoPerspKHR[j] = lambda[j]
```

`Centroid` and `Sample` select the evaluation position independently for each builtin. Without either, interpolation is at the center. NoPersp uses no-perspective interpolation even without a `NoPerspective` decoration. A `NoPerspective` decoration on the perspective builtin is rejected, matching the existing native diagnostic.

For captured PerVertex consumption, corner `j` must correspond exactly to `primitive_indices[3 * uint64(privatePrimitiveKey) + j]`. All corners share the primitive key, but each corner has its own basis. A shared indexed vertex may require a separate replay occurrence for each primitive/corner. The caller owns triangle assembly, restart, clipping/rasterization compatibility, degenerates, corner ordering, instance/view selection and pipeline linkage. This contract is for triangles; point/line barycentrics are not inferred.

The existing `compile_captured_output_replay()` still emits its private flat uint key and captured application outputs. Its existing `(record, primitive_key)` occurrence pairs do **not** describe the corner basis. This patch does not extend that producer API or infer a corner from `vertex_id % 3`; a later producer must provide the basis above and reserve both varying locations alongside the key. The fragment opt-in is independent of `set_msl_per_vertex_input_buffer()` and does not enable either the portable or native PerVertex path automatically.

## Explicit interpolation and failures

Direct builtin variables and BuiltIn-decorated block members support ordinary loads, including helper functions. `GLSL.std.450 InterpolateAtCentroid`, `InterpolateAtSample` and `InterpolateAtOffset` use the existing MSL `interpolant<float3, interpolation::perspective/no_perspective>` path. The ordinary loads still use the builtin's default center/centroid/sample setting. Whole vectors, scalar access chains, runtime vector components and helper interpolation are covered. The existing offset conversion (`offset + 0.4375`) is retained.

Whole-variable `InterpolateAt*` uses activate the portable builtins even when there is no `OpLoad`, including in helpers and without any ordinary user input. Binding validation therefore also applies to explicit-only shaders.

In a builtin block containing either barycentric declaration, active `FragCoord`, `FrontFacing`, `PointCoord`, `SampleId` and `SamplePosition` members use ordinary builtin lowering, independently of the block's pull-model interpolation. They are excluded from the interpolant stage-in structure. This retains the normal position/front-facing/point-coordinate/sample-ID arguments, FragCoord sample-position correction, and SamplePosition calculation. Ordinary loads and helper calls are covered, including a block whose barycentric members are inactive. Inactive ordinary members are omitted.

**Remaining valid-shader gap:** other active builtins in the same block are rejected before MSL emission. This includes `HelperInvocation` and `SampleMask`, whose existing builtin-block helper-argument handling is not safe to reuse, and `ClipDistance`/`CullDistance`, which need their own user varyings. This restriction applies only to blocks containing a barycentric declaration in opt-in mode; standalone ordinary builtin inputs retain their existing behavior. All unsupported members use this diagnostic, with the decimal SPIR-V BuiltIn value substituted for `<N>`:

```text
MSL fragment input blocks with builtin members do not support active builtin <N>.
```

Regression fixtures check the exact diagnostics for `ClipDistance` (`3`), `SampleMask` (`20`) and `HelperInvocation` (`23`), including both HelperInvocation update modes. Merely declaring an inactive unsupported member does not trigger rejection. General support for these mixed blocks remains deferred.

The existing preprocessing does not retain the interpolation source/member/component mapping through unresolved input pointers such as `OpCopyObject`. In an opt-in shader with active barycentrics, such explicit interpolation fails **before MSL emission**, including when the unresolved pointer might refer to an ordinary varying. The exact diagnostic is:

```text
Portable barycentric inputs do not support explicit interpolation through an unresolved input pointer (including OpCopyObject).
```

No default interpolation is substituted for an explicit operation. The valid copied-pointer fixture tests this rejection. Resolving that pointer to a direct input/access chain before compilation avoids this limit.

Other checked failures include non-fragment entry points, MSL below 2.4, missing/duplicate private locations, collisions, non-float3 barycentrics, Flat/Patch/PerVertexKHR or simultaneous Centroid/Sample decorations, and reconfiguration after compilation has started. The shader must otherwise be valid SPIR-V and satisfy the existing backend's requirements.

## Reproduction and evidence

The CMake test target assembles/compiles and validates the nine source fixtures as Vulkan 1.1 SPIR-V in the build directory. No checked-in binary regeneration is needed.

```sh
cmake -S . -B build/barycentric-portable/compiler -G Ninja -DCMAKE_BUILD_TYPE=Release -DSPIRV_CROSS_ENABLE_TESTS=ON -Dspirv-cross-glslang=/opt/homebrew/bin/glslangValidator -Dspirv-cross-spirv-as=/opt/homebrew/bin/spirv-as -Dspirv-cross-spirv-val=/opt/homebrew/bin/spirv-val -Dspirv-cross-spirv-opt=/opt/homebrew/bin/spirv-opt
cmake --build build/barycentric-portable/compiler -j 6
ctest --test-dir build/barycentric-portable/compiler -R 'spirv-cross-msl-(barycentric|capture|per-vertex|interface-locations)' --output-on-failure
bash tests-other/msl_barycentric_portable.sh build/barycentric-portable/compiler build/barycentric-portable/metal
```

The shell check performs CPU code generation and `xcrun metal -std=macos-metal2.4 -fsyntax-only -fno-modules` only. It never creates a GPU device, pipeline or command buffer. The C++ test covers both builtins, all nine independent interpolation-decoration combinations with and without explicit interpolation, single-builtin private mappings, builtin blocks, explicit NoPerspective, builtin/application reflection, invalid configurations, captured PerVertex coexistence, the one-hot perspective equation with nonuniform `w`, and native attribute/rejection regressions.

Run the existing native MSL reference corpora using:

```sh
python3 test_shaders.py shaders-msl --msl --force-no-external-validation --parallel --spirv-cross build/barycentric-portable/compiler/spirv-cross --glslang /opt/homebrew/bin/glslangValidator --spirv-as /opt/homebrew/bin/spirv-as --spirv-val /opt/homebrew/bin/spirv-val --spirv-opt /opt/homebrew/bin/spirv-opt
```

Repeat with `--opt` for optimized references, then replace `shaders-msl` with `shaders-msl-no-opt` without `--opt`. These check unchanged native output against the existing corpus; the new C++ fixtures exercise the opt-in, which has no CLI switch. Metal syntax success and the CPU basis calculation do not establish GPU interpolation accuracy, pipeline support, replay integration or rendering on the host in U state.

Verified after the two independent review fixes: **7/7 focused CTests** and **36 Metal 2.4 shaders**. The new `explicit_only` and `mixed_block` fixtures reproduce the reported failures. The original compiler failed the new activity assertion and both repros failed Metal syntax compilation; after the fixes they pass, along with additional explicit Sample/Offset/helper cases, missing/duplicate binding checks, and active/inactive FragCoord cases. Evidence is under `build/barycentric-review/`: `before.log`, `before/*.metal.log`, `after.log`, `after/*.metal`, and `ctest.log`. Native corpus logs retain the existing **510 default + 510 optimized + 332 no-opt** cases. No GPU was used.

The subsequent FrontFacing review fix passes **7/7 focused CTests** and **40 Metal 2.4 syntax checks**, including the existing native regressions. `mixed_frontfacing` reproduces the reported shader unchanged; before the correction its new API assertion and Metal syntax compilation both fail. `mixed_builtins` covers the supported ordinary members in helpers, inactive members, ordinary-only use, and the rejection boundary above. Evidence is under `build/barycentric-builtins-review/`: `before.log`, `before/mixed-frontfacing.metal.log`, `final.log`, `final/*.metal`, and `ctest.log`. The corpus counts above are from the earlier review run; this bounded follow-up reran the focused checks only. No GPU was used.
