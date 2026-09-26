/*
 * Copyright 2026 Jean-Philippe Meunier
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the License);
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "spirv_msl.hpp"
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
using namespace spirv_cross;

static void check(bool condition, const std::string &message)
{
	if (!condition)
		throw std::runtime_error(message);
}

static std::vector<uint32_t> read_spirv(const char *path)
{
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	check(bool(file), "Cannot open SPIR-V fixture.");
	auto size = file.tellg();
	check(size > 0 && size % 4 == 0, "Invalid SPIR-V fixture size.");
	std::vector<uint32_t> words(size_t(size) / 4);
	file.seekg(0);
	file.read(reinterpret_cast<char *>(words.data()), size);
	check(bool(file), "Cannot read SPIR-V fixture.");
	return words;
}

static void rejects(const std::function<void()> &fn, const char *message)
{
	try { fn(); }
	catch (const CompilerError &error)
	{
		check(error.what() == std::string(message), std::string("Unexpected diagnostic: ") + error.what());
		return;
	}
	throw std::runtime_error(std::string("Missing diagnostic: ") + message);
}

static void configure(CompilerMSL &compiler, const char *entry = "main")
{
	auto options = compiler.get_msl_options();
	options.set_msl_version(2, 4);
	compiler.set_msl_options(options);
	compiler.set_entry_point(entry, spv::ExecutionModelFragment);
}

static MSLFragmentBarycentricInputBinding binding()
{
	MSLFragmentBarycentricInputBinding result;
	result.perspective_location = 12;
	result.no_perspective_location = 14;
	return result;
}

static void decorate(CompilerMSL &compiler, spv::BuiltIn builtin, spv::Decoration decoration)
{
	for (const auto &input : compiler.get_shader_resources().builtin_inputs)
		if (input.builtin == builtin)
		{
			const auto &type = compiler.get_type(input.resource.base_type_id);
			if (type.basetype == SPIRType::Struct)
			{
				for (uint32_t i = 0; i < type.member_types.size(); i++)
					if (compiler.get_member_decoration(type.self, i, spv::DecorationBuiltIn) == uint32_t(builtin))
						compiler.set_member_decoration(type.self, i, decoration);
			}
			else
				compiler.set_decoration(input.resource.id, decoration);
			return;
		}
	throw std::runtime_error("Missing builtin fixture.");
}

static void write_shader(const std::string &directory, const std::string &name, const std::string &source)
{
	if (directory.empty())
		return;
	std::ofstream file(directory + "/" + name + ".metal");
	file << source;
	check(bool(file), "Cannot write Metal shader.");
}

static void check_basis_contract()
{
	// Nonuniform clip w distinguishes perspective from linear interpolation.
	const double lambda[] = {0.2, 0.3, 0.5};
	const double w[] = {1.0, 2.0, 4.0};
	const double values[] = {2.0, 7.0, 13.0};
	double denominator = 0.0, interpolated = 0.0, reconstructed = 0.0, linear = 0.0;
	for (unsigned j = 0; j < 3; j++)
	{
		denominator += lambda[j] / w[j];
		interpolated += lambda[j] * values[j] / w[j];
		linear += lambda[j] * values[j];
	}
	for (unsigned corner = 0; corner < 3; corner++)
	{
		double basis = 0.0;
		for (unsigned j = 0; j < 3; j++)
			basis += lambda[j] * (j == corner ? 1.0 : 0.0) / w[j];
		reconstructed += basis / denominator * values[corner];
	}
	check(std::abs(reconstructed - interpolated / denominator) < 1e-12, "One-hot perspective reconstruction failed.");
	check(std::abs(reconstructed - linear) > 1.0, "Basis check must distinguish perspective interpolation.");
}

int main(int argc, char **argv)
{
	try
	{
		check(argc == 10 || argc == 11, "Usage: test portable.spv interpolate.spv block.spv copied_pointer.spv explicit_only.spv mixed_block.spv mixed_frontfacing.spv mixed_builtins.spv per_vertex.spv [output-directory]");
		auto plain = read_spirv(argv[1]);
		auto pull = read_spirv(argv[2]);
		auto block = read_spirv(argv[3]);
		auto copied = read_spirv(argv[4]);
		auto explicit_only = read_spirv(argv[5]);
		auto mixed_block = read_spirv(argv[6]);
		auto mixed_frontfacing = read_spirv(argv[7]);
		auto mixed_builtins = read_spirv(argv[8]);
		auto per_vertex = read_spirv(argv[9]);
		std::string directory = argc == 11 ? argv[10] : "";
		CompilerMSL facing(mixed_frontfacing);
		configure(facing);
		facing.set_msl_fragment_barycentric_input(binding());
		auto facing_source = facing.compile();
		write_shader(directory, "mixed-frontfacing", facing_source);
		check(facing_source.find("interpolant<bool") == std::string::npos, "FrontFacing was incorrectly lowered as an interpolant.");
		check(facing_source.find("bool gl_FrontFacing [[front_facing]]") != std::string::npos, "Mixed block FrontFacing lost its ordinary builtin argument.");
		check(facing_source.find("[[front_facing]]") == facing_source.rfind("[[front_facing]]"), "Mixed block FrontFacing was emitted twice.");
		for (const char *entry : {"main", "loads", "inactive"})
		{
			CompilerMSL compiler(mixed_builtins);
			configure(compiler, entry);
			compiler.set_msl_fragment_barycentric_input(std::string(entry) == "loads" ? MSLFragmentBarycentricInputBinding{} : binding());
			auto source = compiler.compile();
			write_shader(directory, std::string("mixed-builtins-") + entry, source);
			check(source.find("interpolant<bool") == std::string::npos && source.find("interpolant<uint") == std::string::npos && source.find("interpolant<float2") == std::string::npos, "Ordinary block builtin acquired pull-model interpolation.");
			check(source.find("simd_is_helper_thread()") == std::string::npos && source.find("[[sample_mask]]") == std::string::npos, "Inactive unsupported block members were activated.");
			if (std::string(entry) == "inactive")
				check(source.find("[[front_facing]]") == std::string::npos && source.find("[[point_coord]]") == std::string::npos, "Inactive ordinary block members were activated.");
			else
			{
				for (const char *argument : {"bool gl_FrontFacing [[front_facing]]", "uint gl_SampleID [[sample_id]]", "float2 gl_PointCoord [[point_coord]]"})
					check(source.find(argument) != std::string::npos, std::string("Missing ordinary builtin argument: ") + argument);
				check(source.find("[[sample_id]]") == source.rfind("[[sample_id]]"), "Mixed block SampleId was emitted twice.");
				check(source.find("gl_SamplePosition = get_sample_position(gl_SampleID)") != std::string::npos, "Mixed block SamplePosition lost its builtin fixup.");
			}
			if (std::string(entry) == "loads")
				check(source.find("interpolant<") == std::string::npos && source.find("user(locn12)") == std::string::npos, "Inactive barycentric members were activated.");
		}
		for (const auto &unsupported : {std::make_pair("unsupported", spv::BuiltInClipDistance), std::make_pair("unsupported_helper", spv::BuiltInHelperInvocation), std::make_pair("unsupported_mask", spv::BuiltInSampleMask)})
			for (bool manual_helper : {false, true})
			{
				CompilerMSL compiler(mixed_builtins);
				configure(compiler, unsupported.first);
				auto options = compiler.get_msl_options();
				options.manual_helper_invocation_updates = manual_helper;
				compiler.set_msl_options(options);
				compiler.set_msl_fragment_barycentric_input(binding());
				auto diagnostic = "Portable barycentric input blocks do not support active builtin " + std::to_string(unsupported.second) + " alongside barycentric members.";
				rejects([&]() { compiler.compile(); }, diagnostic.c_str());
			}
		// Compile both review repros before asserting, so their pre-fix MSL is preserved.
		CompilerMSL explicit_use(explicit_only), mixed(mixed_block);
		configure(explicit_use);
		configure(mixed);
		explicit_use.set_msl_fragment_barycentric_input(binding());
		mixed.set_msl_fragment_barycentric_input(binding());
		auto explicit_source = explicit_use.compile();
		auto mixed_source = mixed.compile();
		write_shader(directory, "explicit-only", explicit_source);
		write_shader(directory, "mixed-block", mixed_source);
		check(explicit_use.has_active_builtin(spv::BuiltInBaryCoordKHR, spv::StorageClassInput) && explicit_use.has_active_builtin(spv::BuiltInBaryCoordNoPerspKHR, spv::StorageClassInput), "Explicit-only barycentric builtins were not activated.");
		check(explicit_source.find("user(locn12)") != std::string::npos && explicit_source.find("user(locn14)") != std::string::npos && explicit_source.find("_m4294967295") == std::string::npos, "Explicit-only interpolation has no input member.");
		check(mixed_source.find("interpolant<float4") == std::string::npos, "FragCoord was incorrectly lowered as an interpolant.");
		check(mixed_source.find("float4 gl_FragCoord [[position]]") != std::string::npos, "Mixed block FragCoord lost its ordinary builtin argument.");
		check(mixed_source.find("gl_FragCoord.xy += get_sample_position(") != std::string::npos, "Mixed block FragCoord lost sample-rate correction.");
		check(mixed_source.find("[[position]]") == mixed_source.rfind("[[position]]"), "Mixed block FragCoord was emitted twice.");
		for (const char *entry : {"bare", "at_sample", "at_offset"})
		{
			CompilerMSL compiler(explicit_only);
			configure(compiler, entry);
			compiler.set_msl_fragment_barycentric_input(binding());
			auto source = compiler.compile();
			check(compiler.has_active_builtin(spv::BuiltInBaryCoordKHR, spv::StorageClassInput) && compiler.has_active_builtin(spv::BuiltInBaryCoordNoPerspKHR, spv::StorageClassInput), "Explicit-only helper/operation lost builtin activity.");
			check(source.find("user(locn12)") != std::string::npos && source.find("user(locn14)") != std::string::npos && source.find("_m4294967295") == std::string::npos, "Explicit-only operation has no input member.");
			check(source.find("interpolation::perspective>") != std::string::npos && source.find("interpolation::no_perspective>") != std::string::npos, "Explicit-only interpolation modes lost.");
			check(source.find("user(locn0)") == std::string::npos, "Unused ordinary input was activated.");
			const char *method = std::string(entry) == "bare" ? "centroid" : std::string(entry) == "at_sample" ? "sample" : "offset";
			for (const char *builtin : {"gl_BaryCoordEXT", "gl_BaryCoordNoPerspEXT"})
				check(source.find(std::string(builtin) + ".interpolate_at_" + method + "(") != std::string::npos, "Explicit-only operation changed evaluation position.");
			write_shader(directory, std::string("explicit-only-") + entry, source);
		}
		for (unsigned invalid = 0; invalid < 3; invalid++)
		{
			CompilerMSL compiler(explicit_only);
			configure(compiler);
			auto bindings = binding();
			if (invalid == 0) bindings.perspective_location = ~0u;
			if (invalid == 1) bindings.no_perspective_location = ~0u;
			if (invalid == 2) bindings.no_perspective_location = bindings.perspective_location;
			compiler.set_msl_fragment_barycentric_input(bindings);
			rejects([&]() { compiler.compile(); }, invalid == 2 ? "Portable barycentric inputs require distinct private Locations for the two builtins." : "Portable barycentric inputs require a private Location for each active builtin.");
		}
		for (const char *entry : {"loads", "frag_loads"})
		{
			CompilerMSL compiler(mixed_block);
			configure(compiler, entry);
			compiler.set_msl_fragment_barycentric_input(binding());
			auto source = compiler.compile();
			check(source.find("interpolant<") == std::string::npos, "Ordinary block loads acquired explicit interpolation.");
			if (std::string(entry) == "loads")
				check(source.find("gl_FragCoord") == std::string::npos, "Inactive FragCoord block member was activated.");
			else
			{
				check(source.find("float4 gl_FragCoord [[position]]") != std::string::npos, "Ordinary mixed block lost FragCoord.");
				check(source.find("gl_FragCoord.xy += get_sample_position(") != std::string::npos, "Ordinary mixed block lost FragCoord sample correction.");
			}
			write_shader(directory, std::string("mixed-block-") + entry, source);
		}
		check_basis_contract();
		const spv::Decoration decorations[] = {spv::DecorationMax, spv::DecorationCentroid, spv::DecorationSample};
		const char *positions[] = {"center", "centroid", "sample"};
		for (unsigned p = 0; p < 3; p++)
			for (unsigned n = 0; n < 3; n++)
				for (bool explicit_interpolation : {false, true})
				{
					CompilerMSL compiler(explicit_interpolation ? pull : plain);
					configure(compiler);
					compiler.set_msl_fragment_barycentric_input(binding());
					if (p) decorate(compiler, spv::BuiltInBaryCoordKHR, decorations[p]);
					if (n) decorate(compiler, spv::BuiltInBaryCoordNoPerspKHR, decorations[n]);
					auto source = compiler.compile();
					check(source.find("barycentric_coord") == std::string::npos, "Portable shader depends on native barycentrics.");
					check(!compiler.is_msl_shader_input_used(12) && !compiler.is_msl_shader_input_used(14), "Private locations leaked into application reflection.");
					check(compiler.has_active_builtin(spv::BuiltInBaryCoordKHR, spv::StorageClassInput) && compiler.has_active_builtin(spv::BuiltInBaryCoordNoPerspKHR, spv::StorageClassInput), "Builtin reflection was lost.");
					if (explicit_interpolation)
					{
						check(source.find("interpolant<float3, interpolation::perspective>") != std::string::npos, "Missing perspective interpolant.");
						check(source.find("interpolant<float3, interpolation::no_perspective>") != std::string::npos, "NoPersp default lost in pull-model interpolation.");
						check(source.find("[[user(locn12)]]") != std::string::npos && source.find("[[user(locn14)]]") != std::string::npos, "Wrong interpolant linkage.");
						for (const char *method : {".interpolate_at_centroid()", ".interpolate_at_sample(", ".interpolate_at_offset("})
							check(source.find(method) != std::string::npos, "Explicit interpolation operation lost.");
						check(source.find(" + 0.4375)") != std::string::npos, "Explicit offset translation lost.");
						check(source.find(".interpolate_at_centroid().y") != std::string::npos, "Constant component interpolation lost.");
						check(source.find(" + 0.4375)[") != std::string::npos, "Dynamic component interpolation lost.");
						auto entry = source.substr(source.find("\nfragment "));
						check(entry.find(std::string("in.gl_BaryCoordEXT.interpolate_at_") + positions[p] + "(") != std::string::npos, "Perspective default load lost.");
						check(entry.find(std::string("in.gl_BaryCoordNoPerspEXT.interpolate_at_") + positions[n] + "(") != std::string::npos, "Linear default load lost.");
					}
					else
					{
						check(source.find(std::string("[[user(locn12), ") + positions[p] + "_perspective]]") != std::string::npos, "Wrong perspective qualifier.");
						check(source.find(std::string("[[user(locn14), ") + positions[n] + "_no_perspective]]") != std::string::npos, "Wrong NoPersp qualifier.");
					}
					write_shader(directory, std::string(explicit_interpolation ? "pull-" : "plain-") + positions[p] + "-" + positions[n], source);
				}
		for (unsigned position = 0; position < 3; position++)
		{
			CompilerMSL compiler(block);
			configure(compiler, "loads");
			compiler.set_msl_fragment_barycentric_input(binding());
			if (position)
				for (auto builtin : {spv::BuiltInBaryCoordKHR, spv::BuiltInBaryCoordNoPerspKHR})
					decorate(compiler, builtin, decorations[position]);
			auto source = compiler.compile();
			check(source.find(std::string("user(locn14), ") + positions[position] + "_no_perspective") != std::string::npos, "Block NoPersp default lost.");
			check(source.find("user(locn12)") != std::string::npos && source.find("user(locn14)") != std::string::npos && source.find("barycentric_coord") == std::string::npos, "Block private linkage lost.");
			write_shader(directory, std::string("block-") + positions[position], source);
		}
		CompilerMSL explicit_block(block);
		configure(explicit_block);
		explicit_block.set_msl_fragment_barycentric_input(binding());
		write_shader(directory, "block-explicit", explicit_block.compile());
		CompilerMSL copied_pointer(copied);
		configure(copied_pointer);
		copied_pointer.set_msl_fragment_barycentric_input(binding());
		rejects([&]() { copied_pointer.compile(); }, "Portable barycentric inputs do not support explicit interpolation through an unresolved input pointer (including OpCopyObject).");
		for (const char *entry : {"perspective", "no_perspective"})
		{
			bool linear = std::string(entry) == "no_perspective";
			CompilerMSL native(plain);
			configure(native, entry);
			auto source = native.compile();
			check(source.find(std::string("[[barycentric_coord, center_") + (linear ? "no_perspective" : "perspective") + "]]") != std::string::npos, "Native default changed.");
			write_shader(directory, std::string("native-") + entry, source);
			CompilerMSL portable(plain);
			configure(portable, entry);
			MSLFragmentBarycentricInputBinding one;
			if (linear) one.no_perspective_location = 7; else one.perspective_location = 9;
			portable.set_msl_fragment_barycentric_input(one);
			auto portable_source = portable.compile();
			check(portable_source.find(linear ? "user(locn7), center_no_perspective" : "user(locn9), center_perspective") != std::string::npos, "Single-builtin private mapping lost.");
			write_shader(directory, std::string("single-") + entry, portable_source);
			for (auto decoration : {spv::DecorationCentroid, spv::DecorationSample})
			{
				CompilerMSL rejected(plain);
				configure(rejected, entry);
				decorate(rejected, linear ? spv::BuiltInBaryCoordNoPerspKHR : spv::BuiltInBaryCoordKHR, decoration);
				rejects([&]() { rejected.compile(); }, decoration == spv::DecorationCentroid ? "Centroid interpolation not supported for barycentrics in MSL." : "Sample interpolation not supported for barycentrics in MSL.");
			}
		}
		CompilerMSL native_both(plain);
		configure(native_both);
		rejects([&]() { native_both.compile(); }, "Cannot declare both BaryCoordNV and BaryCoordNoPerspNV in same shader in MSL.");
		for (bool explicit_interpolation : {false, true})
		{
			CompilerMSL compiler(explicit_interpolation ? pull : plain);
			configure(compiler);
			compiler.set_msl_fragment_barycentric_input(binding());
			decorate(compiler, spv::BuiltInBaryCoordNoPerspKHR, spv::DecorationNoPerspective);
			write_shader(directory, explicit_interpolation ? "pull-explicit-noperspective" : "plain-explicit-noperspective", compiler.compile());
		}
		for (unsigned location : {2u, 4u, 5u, 14u, ~0u})
		{
			CompilerMSL compiler(plain);
			configure(compiler);
			auto bindings = binding();
			bindings.perspective_location = location;
			compiler.set_msl_fragment_barycentric_input(bindings);
			rejects([&]() { compiler.compile(); }, location == ~0u ? "Portable barycentric inputs require a private Location for each active builtin." : location == 14 ? "Portable barycentric inputs require distinct private Locations for the two builtins." : "Portable barycentric private Location collides with a fragment input.");
		}
		CompilerMSL missing_linear(plain);
		configure(missing_linear);
		auto missing = binding();
		missing.no_perspective_location = ~0u;
		missing_linear.set_msl_fragment_barycentric_input(missing);
		rejects([&]() { missing_linear.compile(); }, "Portable barycentric inputs require a private Location for each active builtin.");
		CompilerMSL mapped(plain);
		configure(mapped);
		mapped.set_msl_fragment_barycentric_input(binding());
		MSLShaderInterfaceVariable mapping;
		mapping.location = 12;
		mapping.component = 2;
		mapped.add_msl_shader_input(mapping);
		rejects([&]() { mapped.compile(); }, "Portable barycentric private Location collides with an MSL shader input mapping.");
		for (auto decoration : {spv::DecorationFlat, spv::DecorationPatch, spv::DecorationPerVertexKHR, spv::DecorationNoPerspective})
		{
			CompilerMSL compiler(pull);
			configure(compiler);
			compiler.set_msl_fragment_barycentric_input(binding());
			decorate(compiler, spv::BuiltInBaryCoordKHR, decoration);
			rejects([&]() { compiler.compile(); }, decoration == spv::DecorationNoPerspective ? "NoPerspective decorations are not supported for BaryCoord inputs." : "Portable barycentric inputs do not support Flat, Patch, PerVertexKHR or combined Centroid/Sample decorations.");
		}
		CompilerMSL old(plain);
		configure(old);
		auto options = old.get_msl_options();
		options.set_msl_version(2, 3);
		old.set_msl_options(options);
		old.set_msl_fragment_barycentric_input(binding());
		rejects([&]() { old.compile(); }, "Portable barycentric inputs require MSL 2.4 or later.");
		CompilerMSL unused(plain), unused_native(plain);
		configure(unused, "unused");
		configure(unused_native, "unused");
		unused.set_msl_fragment_barycentric_input({});
		check(unused.compile() == unused_native.compile(), "Unused opt-in changed shader.");
		rejects([&]() { unused.set_msl_fragment_barycentric_input(binding()); }, "Portable barycentric inputs must be configured before compilation.");
		for (unsigned location : {12u, 0u, 15u})
		{
			CompilerMSL compiler(per_vertex);
			configure(compiler);
			auto barycentric = binding();
			barycentric.perspective_location = location;
			compiler.set_msl_fragment_barycentric_input(barycentric);
			MSLCapturedVertexLayout layout;
			layout.stride = 16;
			for (uint32_t c = 0; c < 3; c++)
			{
				MSLCapturedVertexComponent field;
				field.location = 0;
				field.component = c;
				field.byte_offset = 4 * c;
				field.scalar_type = SPIRType::Float;
				layout.components.push_back(field);
			}
			MSLPerVertexInputBinding buffers;
			buffers.vertex_buffer_index = 28;
			buffers.primitive_index_buffer_index = 29;
			buffers.primitive_index_location = 15;
			compiler.set_msl_per_vertex_input_buffer(layout, buffers);
			if (location == 12)
			{
				auto source = compiler.compile();
				check(compiler.needs_per_vertex_input_buffer(), "PerVertex buffer path was lost.");
				check(source.find("user(locn15)") != std::string::npos && source.find("barycentric_coord") == std::string::npos, "Barycentrics and primitive key do not coexist.");
				write_shader(directory, "per-vertex", source);
			}
			else
				rejects([&]() { compiler.compile(); }, location == 0 ? "Portable barycentric private Location collides with a fragment input." : "Portable barycentric private Location collides with the PerVertexKHR primitive key.");
		}
		std::cout << "Portable barycentric API checks passed.\n";
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
	return 0;
}
