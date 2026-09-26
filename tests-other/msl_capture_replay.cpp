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

static std::vector<uint32_t> read_spirv(const char *filename)
{
	std::ifstream file(filename, std::ios::binary | std::ios::ate);
	check(bool(file), "Cannot open fixture.");
	auto size = file.tellg();
	check(size > 0 && size % 4 == 0, "Invalid fixture size.");
	std::vector<uint32_t> words(size_t(size) / 4);
	file.seekg(0);
	file.read(reinterpret_cast<char *>(words.data()), size);
	check(bool(file), "Cannot read fixture.");
	return words;
}

static void configure(CompilerMSL &compiler, bool capture = false, bool native_arrays = false, bool compute = false)
{
	auto options = compiler.get_msl_options();
	options.set_msl_version(2, 4);
	options.capture_output_to_buffer = capture;
	options.force_native_arrays = native_arrays;
	options.vertex_for_tessellation = compute;
	compiler.set_msl_options(options);
}

static size_t count(const std::string &source, const std::string &needle)
{
	size_t result = 0;
	for (size_t pos = 0; (pos = source.find(needle, pos)) != std::string::npos; pos += needle.size())
		result++;
	return result;
}

static std::string output_struct(const std::string &source)
{
	auto begin = source.find("struct main0_out\n");
	check(begin != std::string::npos, "Missing raster output struct.");
	auto end = source.find("};", begin);
	check(end != std::string::npos, "Unterminated output struct.");
	return source.substr(begin, end + 2 - begin);
}

static void check_interface(const std::string &normal, const std::string &replay)
{
	auto actual = output_struct(replay);
	auto attr = actual.find("[[user(locn15), flat]]");
	check(attr != std::string::npos, "Missing private flat key.");
	auto begin = actual.rfind('\n', attr);
	auto end = actual.find('\n', attr);
	actual.erase(begin, end - begin);
	check(actual == output_struct(normal), "Replay does not preserve the normal raster interface.\n" + actual + "\n" + output_struct(normal));
}

static void write(const std::string &directory, const std::string &name, const std::string &source)
{
	if (directory.empty())
		return;
	std::ofstream file(directory + "/" + name + ".metal");
	file << source;
	check(bool(file), "Cannot write MSL fixture.");
}

static void rejects(const std::function<void()> &operation, const char *diagnostic)
{
	try { operation(); }
	catch (const CompilerError &error)
	{
		check(std::string(error.what()).find(diagnostic) != std::string::npos, std::string("Unexpected diagnostic: ") + error.what());
		return;
	}
	throw std::runtime_error(std::string("Expected diagnostic: ") + diagnostic);
}

int main(int argc, char **argv)
{
	try
	{
		check(argc >= 6, "Usage: test simple.spv complex.spv effects.spv tessellation.spv unsupported.spv [output-directory]");
		auto simple = read_spirv(argv[1]);
		auto complex = read_spirv(argv[2]);
		auto effects = read_spirv(argv[3]);
		auto tessellation = read_spirv(argv[4]);
		auto unsupported = read_spirv(argv[5]);
		std::string directory = argc > 6 ? argv[6] : "";
		MSLCapturedOutputReplayBinding binding;
		binding.vertex_buffer_index = 0;
		binding.occurrence_buffer_index = 1;
		binding.draw_parameters_buffer_index = 2;
		binding.primitive_index_location = 15;
		MSLCapturedVertexLayout simple_layout;
		for (uint32_t fixture = 0; fixture < 3; fixture++)
			for (bool native_arrays : {false, true})
				for (bool compute : {false, true})
				{
					const auto &words = fixture == 0 ? simple : fixture == 1 ? complex : effects;
					CompilerMSL capture(words), normal(words), replay(words);
					configure(capture, true, native_arrays, compute);
					configure(normal, false, native_arrays);
					configure(replay, false, native_arrays);
					auto capture_msl = capture.compile();
					auto layout = capture.get_msl_captured_vertex_layout();
					auto normal_msl = normal.compile();
					auto replay_msl = replay.compile_captured_output_replay(layout, binding);
					check_interface(normal_msl, replay_msl);
					check(replay_msl.find("vertex main0_out main0(") != std::string::npos, "Replay is not a normal vertex entry point.");
					check(count(replay_msl, "[[buffer(") == 3 && replay_msl.find("[[stage_in]]") == std::string::npos, "Replay emitted application inputs/resources.");
					check(replay_msl.find("2ul * spvReplayOccurrence + 1ul") != std::string::npos && replay_msl.find("ulong(spvReplayOccurrences[2ul * spvReplayOccurrence]) * " + std::to_string(layout.stride) + "ul") != std::string::npos, "Incorrect record/key addressing.");
					check(replay_msl.find("(ulong(spvReplayInstance) - ulong(spvReplayDraw[2])) * ulong(spvReplayDraw[3])") != std::string::npos && replay_msl.find("(ulong(spvReplayVertex) - ulong(spvReplayDraw[1]))") != std::string::npos, "Missing explicit instance/vertex normalization.");
					if (fixture == 0)
					{
						simple_layout = layout;
						check(count(replay_msl, "reinterpret_cast<const device float*>") == 7, "Simple replay must load color and Position once each.");
						check(replay_msl.find("out.gl_Position[3] = *reinterpret_cast<const device float*>(spvReplayRecord + 28ul)") != std::string::npos, "Incorrect Position offset.");
					}
					if (fixture == 1)
					{
						check(replay_msl.find("out.pair[0] = *reinterpret_cast<const device float*>(spvReplayRecord + 8ul)") != std::string::npos, "Component-packed capture was not unpacked.");
						check(replay_msl.find("out.gl_Layer = *reinterpret_cast<const device uint*>(spvReplayRecord + 92ul)") != std::string::npos, "Layer physical type/offset lost.");
						check(replay_msl.find("out.gl_ClipDistance[1] = *reinterpret_cast<const device float*>(spvReplayRecord + 88ul)") != std::string::npos, "ClipDistance array element lost.");
						check(count(replay_msl, "reinterpret_cast<const device ") == 24, "Complex replay must load all fields, including clip user aliases.");
					}
					if (fixture == 2)
					{
						check(capture_msl.find("atomic_fetch_add_explicit") != std::string::npos, "Side-effect control fixture has no atomic write.");
						check(replay_msl.find("atomic") == std::string::npos && replay_msl.find("AppSideEffects") == std::string::npos && replay_msl.find("app_write_side_effect") == std::string::npos, "Replay contains application side effects or resources.");
						// As in a normal MSL VS, application interpolation is selected by the fragment.
						check(replay_msl.find("[[user(locn0_3)]]") != std::string::npos, "Component linkage qualifier lost.");
					}
					std::string name = std::string(fixture == 0 ? "simple" : fixture == 1 ? "complex" : "effects") + (native_arrays ? "-arrays" : "") + (compute ? "-compute" : "");
					write(directory, name + "-capture", capture_msl);
					write(directory, name + "-normal", normal_msl);
					write(directory, name + "-replay", replay_msl);
				}
		for (uint32_t mode = 0; mode < 3; mode++)
		{
			CompilerMSL capture(complex), normal(complex), replay(complex);
			configure(capture, true);
			configure(normal);
			configure(replay);
			for (auto compiler : {&capture, &normal, &replay})
			{
				if (mode == 0)
				{
					compiler->mask_stage_output_by_location(7, 0);
					compiler->mask_stage_output_by_builtin(spv::BuiltInPointSize);
					compiler->mask_stage_output_by_builtin(spv::BuiltInClipDistance);
				}
				else if (mode == 1)
				{
					MSLShaderInterfaceVariable output;
					output.builtin = spv::BuiltInPosition;
					output.location = 1;
					compiler->add_msl_shader_output(output);
				}
				else
				{
					auto options = compiler->get_msl_options();
					options.enable_clip_distance_user_varying = false;
					compiler->set_msl_options(options);
				}
			}
			auto producer = capture.compile();
			auto raster = normal.compile();
			auto msl = replay.compile_captured_output_replay(capture.get_msl_captured_vertex_layout(), binding);
			check_interface(raster, msl);
			if (mode == 0)
				check(msl.find("out.later") == std::string::npos && msl.find("gl_ClipDistance") == std::string::npos && msl.find("gl_PointSize") == std::string::npos, "Masked outputs leaked into replay.");
			std::string name = mode == 0 ? "masked" : mode == 1 ? "builtin-remap" : "no-clip-varying";
			write(directory, name + "-capture", producer);
			write(directory, name + "-normal", raster);
			write(directory, name + "-replay", msl);
		}
		for (bool clip : {false, true})
			for (bool flip : {false, true})
			{
				CompilerMSL capture(simple), replay(simple), normal(simple);
				configure(capture, true);
				configure(replay);
				configure(normal);
				for (auto compiler : {&capture, &replay, &normal})
				{
					auto options = compiler->get_common_options();
					options.vertex.fixup_clipspace = clip;
					options.vertex.flip_vert_y = flip;
					compiler->set_common_options(options);
				}
				auto producer = capture.compile();
				auto msl = replay.compile_captured_output_replay(capture.get_msl_captured_vertex_layout(), binding);
				check(count(producer, "Adjust clip-space") == 0 && count(producer, "Invert Y-axis") == 0, "Capture unexpectedly applies raster fixups.");
				check(count(msl, "Adjust clip-space") == size_t(clip) && count(msl, "Invert Y-axis") == size_t(flip), "Replay fixups missing or repeated.");
				check(count(msl, "out.gl_Position.z = (out.gl_Position.z + out.gl_Position.w) * 0.5;") == size_t(clip) && count(msl, "out.gl_Position.y = -(out.gl_Position.y);") == size_t(flip), "Incorrect raster fixup expression.");
				check_interface(normal.compile(), msl);
				if (clip || flip)
					check(msl.find("out.gl_Position[3] =") < msl.find(clip ? "Adjust clip-space" : "Invert Y-axis"), "Fixup precedes captured Position load.");
				write(directory, std::string("fixups-") + (clip ? "clip" : "no-clip") + (flip ? "-flip" : "-no-flip"), msl);
			}
		auto reject_layout = [&](MSLCapturedVertexLayout layout, const char *diagnostic) {
			CompilerMSL compiler(simple);
			configure(compiler);
			rejects([&]() { compiler.compile_captured_output_replay(layout, binding); }, diagnostic);
		};
		auto bad = simple_layout;
		bad.components.erase(bad.components.begin());
		reject_layout(bad, "missing Location 0 Component 0");
		bad = simple_layout;
		bad.builtins.clear();
		reject_layout(bad, "missing builtin");
		bad = simple_layout;
		bad.components[0].scalar_type = SPIRType::UInt;
		reject_layout(bad, "physical scalar type mismatch");
		bad = simple_layout;
		bad.components[0].byte_offset = 16;
		reject_layout(bad, "overlapping scalar offsets");
		bad = simple_layout;
		bad.builtins.push_back(bad.builtins[0]);
		reject_layout(bad, "duplicate builtin");
		bad = simple_layout;
		bad.components.push_back(bad.components[0]);
		reject_layout(bad, "duplicate Location/Component");
		bad = simple_layout;
		bad.components[0].byte_offset = 2;
		reject_layout(bad, "naturally aligned");
		bad = simple_layout;
		bad.stride = 16;
		reject_layout(bad, "outside the captured stride");
		bad = simple_layout;
		bad.components[0].scalar_type = SPIRType::UInt64;
		reject_layout(bad, "16/32-bit");
		bad = simple_layout;
		bad.builtins[0].builtin = spv::BuiltInCullDistance;
		reject_layout(bad, "does not support builtin");
		bad = simple_layout;
		bad.builtins[0].array_index = 1;
		reject_layout(bad, "unsupported type or shape");
		for (uint32_t mode = 0; mode < 9; mode++)
		{
			CompilerMSL compiler(simple);
			configure(compiler);
			auto options = compiler.get_msl_options();
			auto binds = binding;
			const char *diagnostic;
			switch (mode)
			{
			case 0: binds.occurrence_buffer_index = binds.vertex_buffer_index; diagnostic = "three distinct"; break;
			case 1: binds.draw_parameters_buffer_index = 31; diagnostic = "three distinct"; break;
			case 2: binds.primitive_index_location = 0; diagnostic = "private Location collides"; break;
			case 3: options.capture_output_to_buffer = true; diagnostic = "normal rasterizing"; break;
			case 4: options.vertex_for_tessellation = true; diagnostic = "normal rasterizing"; break;
			case 5: options.multiview = true; diagnostic = "does not support multiview"; break;
			case 6: options.emulate_depth_clip_enable = true; diagnostic = "depth/viewport emulation"; break;
			case 7: options.enable_point_size_default = true; diagnostic = "default PointSize"; break;
			default: options.set_msl_version(2, 3); diagnostic = "MSL 2.4"; break;
			}
			compiler.set_msl_options(options);
			rejects([&]() { compiler.compile_captured_output_replay(simple_layout, binds); }, diagnostic);
			rejects([&]() { compiler.compile_captured_output_replay(simple_layout, binding); }, "fresh compiler");
		}
		CompilerMSL reused(simple);
		configure(reused);
		reused.compile();
		rejects([&]() { reused.compile_captured_output_replay(simple_layout, binding); }, "fresh compiler");
		CompilerMSL replayed(simple);
		configure(replayed);
		replayed.compile_captured_output_replay(simple_layout, binding);
		rejects([&]() { replayed.compile(); }, "fresh compiler");
		rejects([&]() { replayed.get_msl_captured_vertex_layout(); }, "successful MSL compilation");
		CompilerMSL no_position(simple);
		configure(no_position);
		no_position.mask_stage_output_by_builtin(spv::BuiltInPosition);
		rejects([&]() { no_position.compile_captured_output_replay(simple_layout, binding); }, "Position output");
		CompilerMSL tese(tessellation);
		configure(tese);
		rejects([&]() { tese.compile_captured_output_replay(simple_layout, binding); }, "only vertex entry points");
		CompilerMSL matrix(unsupported);
		configure(matrix);
		rejects([&]() { matrix.compile_captured_output_replay(simple_layout, binding); }, "user arrays, matrices");
		std::cout << "Captured output replay checks passed.\n";
		return 0;
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
