#include <algorithm>
#include <cinttypes>
#include <cstdint>
#include <fstream>

#include <catch2/catch_test_macros.hpp>

#include <simdjson.h>

#include <fastgltf/core.hpp>
#include "gltf_path.hpp"

TEST_CASE("Test simple glTF composition", "[write-tests]") {
	fastgltf::BufferView bufferView = {};
	bufferView.bufferIndex = 0;
	bufferView.byteStride = 4;
	bufferView.byteLength = 16;

	fastgltf::Asset asset;
	asset.bufferViews.emplace_back(std::move(bufferView));

	fastgltf::Exporter exporter;
	auto result = exporter.writeGltfJson(asset);
	REQUIRE(result.error() == fastgltf::Error::None);
	REQUIRE(!result.get().output.empty());
}

TEST_CASE("Read glTF, write it, and then read it again and validate", "[write-tests]") {
	auto cubePath = sampleAssets / "Models" / "Cube" / "glTF";
	fastgltf::GltfFileStream cubeJsonData(cubePath / "Cube.gltf");
	REQUIRE(cubeJsonData.isOpen());

	fastgltf::Parser parser;
	auto cube = parser.loadGltfJson(cubeJsonData, cubePath);
	REQUIRE(cube.error() == fastgltf::Error::None);
	REQUIRE(fastgltf::validate(cube.get()) == fastgltf::Error::None);

	fastgltf::Exporter exporter;
	auto expected = exporter.writeGltfJson(cube.get());
    REQUIRE(expected.error() == fastgltf::Error::None);

	auto exportedJsonData = fastgltf::GltfDataBuffer::FromBytes(
			reinterpret_cast<const std::byte*>(expected.get().output.data()), expected.get().output.size());
	REQUIRE(exportedJsonData.error() == fastgltf::Error::None);
	auto cube2 = parser.loadGltfJson(exportedJsonData.get(), cubePath);
	REQUIRE(cube2.error() == fastgltf::Error::None);
	REQUIRE(fastgltf::validate(cube2.get()) == fastgltf::Error::None);
}

TEST_CASE("Rewrite read glTF with multiple material extensions", "[write-tests]") {
	auto dishPath = sampleAssets / "Models" / "IridescentDishWithOlives" / "glTF";
	fastgltf::GltfFileStream dishJsonData(dishPath / "IridescentDishWithOlives.gltf");
	REQUIRE(dishJsonData.isOpen());

	static constexpr auto requiredExtensions = fastgltf::Extensions::KHR_materials_ior |
		fastgltf::Extensions::KHR_materials_iridescence |
		fastgltf::Extensions::KHR_materials_transmission |
		fastgltf::Extensions::KHR_materials_volume;

	fastgltf::Parser parser(requiredExtensions);
	auto dish = parser.loadGltfJson(dishJsonData, dishPath);
	REQUIRE(dish.error() == fastgltf::Error::None);
	REQUIRE(fastgltf::validate(dish.get()) == fastgltf::Error::None);

	fastgltf::Exporter exporter;
	auto expected = exporter.writeGltfJson(dish.get());
	REQUIRE(expected.error() == fastgltf::Error::None);

	auto exportedDishJsonData = fastgltf::GltfDataBuffer::FromBytes(
			reinterpret_cast<const std::byte*>(expected.get().output.data()), expected.get().output.size());
	REQUIRE(exportedDishJsonData.error() == fastgltf::Error::None);
	auto exportedDish = parser.loadGltfJson(exportedDishJsonData.get(), dishPath);
	REQUIRE(exportedDish.error() == fastgltf::Error::None);
	REQUIRE(fastgltf::validate(exportedDish.get()) == fastgltf::Error::None);
}

TEST_CASE("Try writing a glTF with all buffers and images", "[write-tests]") {
    auto cubePath = sampleAssets / "Models" / "Cube" / "glTF";

	fastgltf::GltfFileStream cubeJson(cubePath / "Cube.gltf");
	REQUIRE(cubeJson.isOpen());

    fastgltf::Parser parser;
    auto options = fastgltf::Options::LoadExternalBuffers | fastgltf::Options::LoadExternalImages;
    auto cube = parser.loadGltfJson(cubeJson, cubePath, options);
    REQUIRE(cube.error() == fastgltf::Error::None);

	// The following code assumes that only a single image is written to a file
	REQUIRE(cube->images.size() == 1);

	// Destroy the directory to make sure that the FileExporter correctly creates directories.
	auto exportedFolder = path / "export";
	if (std::filesystem::is_directory(exportedFolder)) {
		std::error_code ec;
		std::filesystem::remove_all(exportedFolder, ec);
		REQUIRE(!ec);
	}

    fastgltf::FileExporter exporter;
    auto error = exporter.writeGltfJson(cube.get(), exportedFolder / "cube.gltf",
                                        fastgltf::ExportOptions::PrettyPrintJson);
    REQUIRE(error == fastgltf::Error::None);
	REQUIRE(std::filesystem::exists(exportedFolder / "buffer0.bin"));
	REQUIRE(std::filesystem::exists(exportedFolder / "image0.bin"));
}

TEST_CASE("Try writing a GLB with all buffers and images", "[write-tests]") {
    auto cubePath = sampleAssets / "Models" / "Cube" / "glTF";

	fastgltf::GltfFileStream cubeJson(cubePath / "Cube.gltf");
	REQUIRE(cubeJson.isOpen());

    fastgltf::Parser parser;
    auto options = fastgltf::Options::LoadExternalBuffers | fastgltf::Options::LoadExternalImages;
    auto cube = parser.loadGltfJson(cubeJson, cubePath, options);
    REQUIRE(cube.error() == fastgltf::Error::None);

	// The following code assumes that only a single image is written to a file
	REQUIRE(cube->images.size() == 1);

	// Destroy the directory to make sure that the FileExporter correctly creates directories.
	auto exportedFolder = path / "export_glb";
	if (std::filesystem::exists(exportedFolder)) {
		std::error_code ec;
		std::filesystem::remove_all(exportedFolder, ec);
		REQUIRE(!ec);
	}

    fastgltf::FileExporter exporter;
	auto exportedPath = exportedFolder / "cube.glb";
    auto error = exporter.writeGltfBinary(cube.get(), exportedPath);
    REQUIRE(error == fastgltf::Error::None);
	REQUIRE(std::filesystem::exists(exportedFolder / "image0.bin"));

	// Make sure the GLB buffer is written
	std::ifstream glb(exportedPath, std::ios::binary);
	REQUIRE(glb.is_open());

	// Skip over the header
	glb.seekg(sizeof(std::uint32_t) * 3, std::ifstream::beg);

	// Read the chunk length
	std::uint32_t chunkLength = 0;
	glb.read(reinterpret_cast<char*>(&chunkLength), sizeof chunkLength);

	// Skip over the chunk type + chunk
	glb.seekg(sizeof(std::uint32_t) + fastgltf::alignUp(chunkLength, 4), std::ifstream::cur);

	// Read binary chunk length
	glb.read(reinterpret_cast<char*>(&chunkLength), sizeof chunkLength);
	REQUIRE(chunkLength == cube->buffers.front().byteLength);

	// Read & verify BIN chunk type id
	std::array<std::uint8_t, 4> binType {};
	glb.read(reinterpret_cast<char*>(binType.data()), sizeof binType);
	REQUIRE(binType[0] == 'B');
	REQUIRE(binType[1] == 'I');
	REQUIRE(binType[2] == 'N');
	REQUIRE(binType[3] == 0);
}

TEST_CASE("Test string escape", "[write-tests]") {
    std::string x = "\"stuff\\";
    std::string escaped = fastgltf::escapeString(x);
    REQUIRE(escaped == "\\\"stuff\\\\");
}

TEST_CASE("Test pretty-print", "[write-tests]") {
    std::string json = R"({"value":5,"thing":{}})";
    fastgltf::prettyPrintJson(json);
	REQUIRE(json == "{\n\t\"value\":5,\n\t\"thing\":{\n\t\t\n\t}\n}");
}

TEST_CASE("Test all local models and re-export them", "[write-tests]") {
	// Enable all extensions
	static constexpr auto requiredExtensions = static_cast<fastgltf::Extensions>(~0U);
	fastgltf::Parser parser(requiredExtensions);

	static std::filesystem::path folderPath = "";
	if (folderPath.empty()) {
		SKIP();
	}

	std::uint64_t testedAssets = 0;
	for (const auto& entry : std::filesystem::recursive_directory_iterator(folderPath)) {
		if (!entry.is_regular_file())
			continue;
		const auto& epath = entry.path();
		if (!epath.has_extension())
			continue;
		if (epath.extension() != ".gltf" && epath.extension() != ".glb")
			continue;

		// Parse the glTF
		fastgltf::GltfFileStream gltfData(epath);
		auto model = parser.loadGltf(gltfData, epath.parent_path());
		if (model.error() == fastgltf::Error::UnsupportedVersion || model.error() == fastgltf::Error::UnknownRequiredExtension || model.error() == fastgltf::Error::InvalidOrMissingAssetField)
			continue; // Skip any glTF 1.0 or 0.x files or glTFs with unsupported extensions.

		REQUIRE(model.error() == fastgltf::Error::None);
		REQUIRE(fastgltf::validate(model.get()) == fastgltf::Error::None);

		// Re-export the glTF as an in-memory JSON
		fastgltf::Exporter exporter;
		auto exported = exporter.writeGltfJson(model.get());
		REQUIRE(exported.error() == fastgltf::Error::None);

		// UTF-8 validation on the exported JSON string
		auto& exportedJson = exported.get().output;
		REQUIRE(simdjson::validate_utf8(exportedJson));

		// Parse the re-generated glTF and validate
		auto regeneratedJson = fastgltf::GltfDataBuffer::FromBytes(
				reinterpret_cast<const std::byte*>(exportedJson.data()), exportedJson.size());
		REQUIRE(regeneratedJson.error() == fastgltf::Error::None);
		auto regeneratedModel = parser.loadGltf(regeneratedJson.get(), epath.parent_path());
		REQUIRE(regeneratedModel.error() == fastgltf::Error::None);

		REQUIRE(fastgltf::validate(regeneratedModel.get()) == fastgltf::Error::None);

		++testedAssets;
	}
	printf("Successfully tested fastgltf exporter on %" PRIu64 " assets.", testedAssets);
}

TEST_CASE("Test Unicode exporting", "[write-tests]") {
#if FASTGLTF_CPP_20
	auto unicodePath = sampleAssets / "Models" / std::filesystem::path(u8"Unicode❤♻Test") / "glTF";
	fastgltf::GltfFileStream jsonData(unicodePath / std::filesystem::path(u8"Unicode❤♻Test.gltf"));
#else
	auto unicodePath = sampleAssets / "Models" / std::filesystem::u8path(u8"Unicode❤♻Test") / "glTF";
	fastgltf::GltfFileStream jsonData(unicodePath / std::filesystem::u8path(u8"Unicode❤♻Test.gltf"));
#endif
	REQUIRE(jsonData.isOpen());

	fastgltf::Parser parser;
	auto asset = parser.loadGltfJson(jsonData, unicodePath);
	REQUIRE(asset.error() == fastgltf::Error::None);

	fastgltf::Exporter exporter;
	auto exported = exporter.writeGltfJson(asset.get());
	REQUIRE(exported.error() == fastgltf::Error::None);

	// UTF-8 validation on the exported JSON string
	auto& exportedJson = exported.get().output;
	REQUIRE(simdjson::validate_utf8(exportedJson));

	auto regeneratedJson = fastgltf::GltfDataBuffer::FromBytes(
			reinterpret_cast<const std::byte*>(exportedJson.data()), exportedJson.size());
	REQUIRE(regeneratedJson.error() == fastgltf::Error::None);
	auto reparsed = parser.loadGltfJson(regeneratedJson.get(), unicodePath);
	REQUIRE(reparsed.error() == fastgltf::Error::None);

	REQUIRE(!asset->materials.empty());
	REQUIRE(asset->materials[0].name == "Unicode❤♻Material");

	REQUIRE(!asset->buffers.empty());
	REQUIRE(std::holds_alternative<fastgltf::sources::URI>(asset->buffers.front().data));
	auto bufferUri = std::get<fastgltf::sources::URI>(asset->buffers.front().data);
	REQUIRE(bufferUri.uri.path() == "Unicode❤♻Binary.bin");
}

TEST_CASE("Test URI normalization and removing backslashes", "[write-tests]") {
#if FASTGLTF_CPP_20
	auto unicodePath = sampleAssets / "Models" / std::filesystem::path(u8"Unicode❤♻Test") / "glTF";
	fastgltf::GltfFileStream jsonData(unicodePath / std::filesystem::path(u8"Unicode❤♻Test.gltf"));
#else
	auto unicodePath = sampleAssets / "Models" / std::filesystem::u8path(u8"Unicode❤♻Test") / "glTF";
	fastgltf::GltfFileStream jsonData(unicodePath / std::filesystem::u8path(u8"Unicode❤♻Test.gltf"));
#endif
	REQUIRE(jsonData.isOpen());

	fastgltf::Parser parser;
	auto asset = parser.loadGltfJson(jsonData, unicodePath, fastgltf::Options::LoadExternalImages);
	REQUIRE(asset.error() == fastgltf::Error::None);

	REQUIRE(asset->images.size() == 1);
	asset->images.front().name = "Unicode❤♻Texture";

	// When exporting, we specify a redundant image base path to test if the normalization properly works.
	// Additionally, on Windows we test if the slashes are changed properly.
	fastgltf::Exporter exporter;
#ifdef _WIN32
	// When on Windows the default path separator is a backslash, but glTF requires a forward slash.
	exporter.setImagePath(".\\textures1\\textures2\\..\\");
#else
	exporter.setImagePath("./textures1/textures2/../");
#endif
	auto exported = exporter.writeGltfJson(asset.get());

	// Parse the JSON and inspect the image URI.
	simdjson::ondemand::parser jsonParser;
	simdjson::ondemand::document doc;
	REQUIRE(jsonParser.iterate(exported.get().output.c_str(), exported.get().output.size(), exported.get().output.capacity()).get(doc) == simdjson::SUCCESS);

	simdjson::ondemand::object object;
	REQUIRE(doc.get_object().get(object) == simdjson::SUCCESS);

	simdjson::ondemand::array images;
	REQUIRE(doc["images"].get_array().get(images) == simdjson::SUCCESS);
	REQUIRE(images.count_elements().value() == 1);

	simdjson::ondemand::object imageObject;
	REQUIRE(images.at(0).get_object().get(imageObject) == simdjson::SUCCESS);

	std::string_view imageUri;
	REQUIRE(imageObject["uri"].get_string().get(imageUri) == simdjson::SUCCESS);
	REQUIRE(imageUri == "textures1/Unicode❤♻Texture.bin");
}

TEST_CASE("Test floating point round-trip precision", "[write-tests]") {
	auto cubePath = sampleAssets / "Models" / "Duck" / "glTF-Binary";

	fastgltf::Parser parser;

	auto original = [&parser, &cubePath]() {
		fastgltf::GltfFileStream cubeJson(cubePath / "Duck.glb");
		REQUIRE(cubeJson.isOpen());

		auto asset = parser.loadGltfBinary(cubeJson, cubePath,
			fastgltf::Options::LoadExternalBuffers | fastgltf::Options::LoadExternalImages);
		REQUIRE(asset.error() == fastgltf::Error::None);
		REQUIRE(fastgltf::validate(asset.get()) == fastgltf::Error::None);
		return std::move(asset.get());
	}();

	fastgltf::FileExporter exporter;
	auto exportedPath = path / "export_glb" / "Duck_rewritten.glb";
	REQUIRE(exporter.writeGltfBinary(original, exportedPath) == fastgltf::Error::None);

	// Now read the exported file again, and check if the floating-point values are the same as the original asset.
	auto reimported = [&parser, &exportedPath] {
		fastgltf::GltfFileStream rewrittenJson(exportedPath);
		REQUIRE(rewrittenJson.isOpen());

		auto asset = parser.loadGltfBinary(rewrittenJson, exportedPath.parent_path());
		REQUIRE(asset.error() == fastgltf::Error::None);
		REQUIRE(fastgltf::validate(asset.get()) == fastgltf::Error::None);
		return std::move(asset.get());
	}();

	REQUIRE(original.accessors.size() == reimported.accessors.size());
	for (std::size_t i = 0; i < reimported.accessors.size(); ++i) {
		const auto& accessor_a = original.accessors[i];
		const auto& accessor_b = reimported.accessors[i];

		REQUIRE(accessor_a.min.has_value());
		REQUIRE(accessor_b.min.has_value());
		REQUIRE(accessor_a.min->type() == accessor_b.min->type());
		if (!accessor_a.min->isType<double>())
			continue;

		const auto& min_a = accessor_a.min.value();
		const auto& min_b = accessor_b.min.value();
		REQUIRE(min_a.size() == min_b.size());
		for (std::size_t j = 0; j < min_a.size(); ++j) {
			REQUIRE(min_a.get<double>(j) == min_b.get<double>(j));
		}

		REQUIRE(accessor_a.max->type() == accessor_b.max->type());
		if (!accessor_a.max->isType<double>())
			continue;

		const auto& max_a = accessor_a.max.value();
		const auto& max_b = accessor_b.max.value();
		REQUIRE(max_a.size() == max_b.size());
		for (std::size_t j = 0; j < max_a.size(); ++j) {
			REQUIRE(max_a.get<double>(j) == max_b.get<double>(j));
		}
	}

	REQUIRE(original.materials.size() == reimported.materials.size());
	for (std::size_t i = 0; i < reimported.materials.size(); ++i) {
		const auto& material_a = original.materials[i];
		const auto& material_b = reimported.materials[i];

		for (std::size_t j = 0; j < 4; ++j) {
			REQUIRE(material_a.pbrData.baseColorFactor[j] == material_b.pbrData.baseColorFactor[j]);
		}
		REQUIRE(material_a.pbrData.metallicFactor == material_b.pbrData.metallicFactor);
		REQUIRE(material_a.pbrData.roughnessFactor == material_b.pbrData.roughnessFactor);

		for (std::size_t j = 0; j < 3; ++j) {
			REQUIRE(material_a.emissiveFactor[j] == material_b.emissiveFactor[j]);
		}
		REQUIRE(material_a.alphaCutoff == material_b.alphaCutoff);
	}
}

TEST_CASE("Test Accessor::updateBoundsToInclude", "[write-tests]") {
	SECTION("Scalar") {
		fastgltf::Accessor accessor;

		accessor.updateBoundsToInclude(static_cast<std::int64_t>(2));
		accessor.updateBoundsToInclude(static_cast<std::int64_t>(4));
		accessor.updateBoundsToInclude(static_cast<std::int64_t>(-2));

		REQUIRE(accessor.max.has_value());
		REQUIRE(accessor.max->isType<std::int64_t>());
		REQUIRE(accessor.max->size() == 1);
		REQUIRE(accessor.max->get<std::int64_t>(0) == 4);

		REQUIRE(accessor.min.has_value());
		REQUIRE(accessor.min->isType<std::int64_t>());
		REQUIRE(accessor.min->size() == 1);
		REQUIRE(accessor.min->get<std::int64_t>(0) == -2);
	}

	SECTION("Doubles") {
		fastgltf::Accessor accessor;

		accessor.updateBoundsToInclude(fastgltf::math::f64vec3(1.0, 2.0, 3.0));
		accessor.updateBoundsToInclude(fastgltf::math::f64vec3(2.0, 3.0, -4.0));
		accessor.updateBoundsToInclude(fastgltf::math::f64vec3(0.0, 0.0, 0.0));

		REQUIRE(accessor.max.has_value());
		REQUIRE(accessor.max->size() == 3);
		REQUIRE(accessor.max->isType<double>());
		REQUIRE(!accessor.max->isType<std::int64_t>());
		REQUIRE(accessor.max->get<double>(0) == 2.0);
		REQUIRE(accessor.max->get<double>(1) == 3.0);
		REQUIRE(accessor.max->get<double>(2) == 3.0);

		REQUIRE(accessor.min.has_value());
		REQUIRE(accessor.min->size() == 3);
		REQUIRE(accessor.min->isType<double>());
		REQUIRE(!accessor.min->isType<std::int64_t>());
		REQUIRE(accessor.min->get<double>(0) == 0.0);
		REQUIRE(accessor.min->get<double>(1) == 0.0);
		REQUIRE(accessor.min->get<double>(2) == -4.0);
	}

	SECTION("Integers") {
		fastgltf::Accessor accessor;

		accessor.updateBoundsToInclude(fastgltf::math::s64vec3(1, 2, 3));
		accessor.updateBoundsToInclude(fastgltf::math::s64vec3(2, 3, -4));
		accessor.updateBoundsToInclude(fastgltf::math::s64vec3(0, 0, 0));

		REQUIRE(accessor.max.has_value());
		REQUIRE(accessor.max->size() == 3);
		REQUIRE(!accessor.max->isType<double>());
		REQUIRE(accessor.max->isType<std::int64_t>());
		REQUIRE(accessor.max->get<std::int64_t>(0) == 2);
		REQUIRE(accessor.max->get<std::int64_t>(1) == 3);
		REQUIRE(accessor.max->get<std::int64_t>(2) == 3);

		REQUIRE(accessor.min.has_value());
		REQUIRE(accessor.min->size() == 3);
		REQUIRE(!accessor.min->isType<double>());
		REQUIRE(accessor.min->isType<std::int64_t>());
		REQUIRE(accessor.min->get<std::int64_t>(0) == 0);
		REQUIRE(accessor.min->get<std::int64_t>(1) == 0);
		REQUIRE(accessor.min->get<std::int64_t>(2) == -4);
	}
}

TEST_CASE("Test empty scene omits nodes array", "[write-tests]") {
	// scene.nodes is optional (minItems:1), so omit when empty.
	fastgltf::Asset asset;
	asset.nodes.emplace_back();

	fastgltf::Scene populated;
	populated.nodeIndices.emplace_back(0);
	asset.scenes.emplace_back(std::move(populated));

	asset.scenes.emplace_back();

	fastgltf::Scene named;
	named.name = "Empty";
	asset.scenes.emplace_back(std::move(named));

	fastgltf::Exporter exporter;
	auto result = exporter.writeGltfJson(asset);
	REQUIRE(result.error() == fastgltf::Error::None);
	const auto& json = result.get().output;

	REQUIRE(json.find(R"("nodes":[0])") != std::string::npos);
	REQUIRE(json.find(R"("nodes":[])") == std::string::npos);
	REQUIRE(json.find(R"({"name":"Empty"})") != std::string::npos);
}

TEST_CASE("Round-trip a glTF with MYLAB_generative", "[write-tests]") {
	auto mylabGenerativeValid = path / "mylab_generative_valid.gltf";
	fastgltf::GltfFileStream jsonData(mylabGenerativeValid);
	REQUIRE(jsonData.isOpen());

	fastgltf::Parser parser(fastgltf::Extensions::MYLAB_generative);
	auto asset = parser.loadGltfJson(jsonData, mylabGenerativeValid);
	REQUIRE(asset.error() == fastgltf::Error::None);
	REQUIRE(fastgltf::validate(asset.get()) == fastgltf::Error::None);

	fastgltf::Exporter exporter;
	auto expected = exporter.writeGltfJson(asset.get());
	REQUIRE(expected.error() == fastgltf::Error::None);

	auto exportedJsonData = fastgltf::GltfDataBuffer::FromBytes(
			reinterpret_cast<const std::byte*>(expected.get().output.data()), expected.get().output.size());
	REQUIRE(exportedJsonData.error() == fastgltf::Error::None);
	auto exported = parser.loadGltfJson(exportedJsonData.get(), mylabGenerativeValid);
	REQUIRE(exported.error() == fastgltf::Error::None);
	REQUIRE(fastgltf::validate(exported.get()) == fastgltf::Error::None);

	// The whole point of writing the extension out is that a re-read recovers the same data.
	REQUIRE(exported->generators.size() == 1);
	const auto& generator = exported->generators.front();
	REQUIRE(generator.name == "cam_gen");
	REQUIRE(generator.task == fastgltf::GenerativeTask::TextToCameraTrajectory);
	REQUIRE(generator.model.uri == "models/camtraj.onnx");
	REQUIRE(generator.model.sha256 == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	REQUIRE(generator.output.fps == 30);
	REQUIRE(generator.output.cameraConvention == fastgltf::CameraConvention::OpenCV);

	REQUIRE(exported->generativeTimeline.has_value());
	REQUIRE(exported->generativeTimeline->duration == 8);

	REQUIRE(exported->nodes.size() == 2);
	REQUIRE(!exported->nodes[0].generative);
	REQUIRE(exported->nodes[1].generative);
	const auto& generative = *exported->nodes[1].generative;
	REQUIRE(generative.generator == 0);
	REQUIRE(generative.prompt == "slow orbit around the actor, ending on a close-up");
	REQUIRE(generative.seed.has_value());
	REQUIRE(generative.seed.value() == 7);
	REQUIRE(generative.start == 0.5);
	REQUIRE(generative.duration.has_value());
	REQUIRE(generative.duration.value() == 6);
	REQUIRE(generative.conditionOn.lookAtNode.has_value());
	REQUIRE(generative.conditionOn.lookAtNode.value() == 0);
}

TEST_CASE("Test MYLAB_generative output shape and string escaping", "[write-tests]") {
	fastgltf::Asset asset;
	// The exporter passes extensionsUsed through verbatim rather than deriving it from the data,
	// so a hand-built asset has to declare the extension itself.
	asset.extensionsUsed.emplace_back("MYLAB_generative");

	fastgltf::Generator generator = {};
	generator.task = fastgltf::GenerativeTask::TextToMotion;
	generator.model.uri = "models/t2m_v2.onnx";
	generator.model.skeleton = "smpl_22";
	generator.output.fps = 20;
	generator.output.rotation = fastgltf::RotationEncoding::AxisAngle;
	generator.output.upAxis = fastgltf::UpAxis::Z;
	generator.name = "motion_gen";
	asset.generators.emplace_back(std::move(generator));
	asset.generativeTimeline = fastgltf::GenerativeTimeline { 8 };

	asset.nodes.emplace_back();
	asset.nodes.front().generative = std::make_unique<fastgltf::GenerativeNode>();
	asset.nodes.front().generative->generator = 0;
	// Prompts are arbitrary user text, so they have to survive JSON escaping.
	asset.nodes.front().generative->prompt = "she said \"hello\"\nthen \\ waved";
	asset.nodes.front().generative->seed = 42;

	fastgltf::Exporter exporter;
	auto result = exporter.writeGltfJson(asset);
	REQUIRE(result.error() == fastgltf::Error::None);
	const auto& json = result.get().output;

	REQUIRE(json.find(R"("MYLAB_generative":{"generators":[)") != std::string::npos);
	REQUIRE(json.find(R"("task":"text_to_motion")") != std::string::npos);
	// Floats go through to_string_fp (grisu2), which always spells out a decimal point.
	REQUIRE(json.find(R"("output":{"fps":20.0)") != std::string::npos);
	REQUIRE(json.find(R"("skeleton":"smpl_22")") != std::string::npos);
	REQUIRE(json.find(R"("rotation":"axis_angle")") != std::string::npos);
	REQUIRE(json.find(R"("upAxis":"Z")") != std::string::npos);
	REQUIRE(json.find(R"("name":"motion_gen")") != std::string::npos);
	REQUIRE(json.find(R"("timeline":{"duration":8.0})") != std::string::npos);
	REQUIRE(json.find(R"("MYLAB_generative":{"generator":0)") != std::string::npos);
	REQUIRE(json.find(R"("seed":42)") != std::string::npos);
	// Hoisted out of REQUIRE: MSVC's legacy preprocessor mangles raw string literals that are
	// passed as macro arguments.
	const std::string escapedPrompt = "\"prompt\":\"she said \\\"hello\\\"\\nthen \\\\ waved\"";
	REQUIRE(json.find(escapedPrompt) != std::string::npos);

	// Defaults are omitted rather than written out.
	REQUIRE(json.find(R"("units")") == std::string::npos);
	REQUIRE(json.find(R"("cameraConvention")") == std::string::npos);
	REQUIRE(json.find(R"("poseType")") == std::string::npos);
	REQUIRE(json.find(R"("start")") == std::string::npos);

	// And the escaped prompt survives a re-read intact.
	auto exportedJsonData = fastgltf::GltfDataBuffer::FromBytes(
			reinterpret_cast<const std::byte*>(json.data()), json.size());
	REQUIRE(exportedJsonData.error() == fastgltf::Error::None);
	fastgltf::Parser parser(fastgltf::Extensions::MYLAB_generative);
	auto exported = parser.loadGltfJson(exportedJsonData.get(), path);
	REQUIRE(exported.error() == fastgltf::Error::None);
	REQUIRE(exported->nodes.front().generative);
	REQUIRE(exported->nodes.front().generative->prompt == "she said \"hello\"\nthen \\ waved");
}

TEST_CASE("Test JSON string escaping", "[write-tests]") {
	REQUIRE(fastgltf::escapeString("").empty());
	REQUIRE(fastgltf::escapeString("plain text") == "plain text");
	REQUIRE(fastgltf::escapeString("a\"b") == "a\\\"b");
	REQUIRE(fastgltf::escapeString("a\\b") == "a\\\\b");

	// JSON forbids raw control characters inside strings, so these have to come back escaped.
	// Prompts and names are arbitrary user text and do contain newlines.
	REQUIRE(fastgltf::escapeString("a\bb") == "a\\bb");
	REQUIRE(fastgltf::escapeString("a\fb") == "a\\fb");
	REQUIRE(fastgltf::escapeString("a\nb") == "a\\nb");
	REQUIRE(fastgltf::escapeString("a\rb") == "a\\rb");
	REQUIRE(fastgltf::escapeString("a\tb") == "a\\tb");

	// Control characters without a short escape fall back to \u00XX.
	REQUIRE(fastgltf::escapeString(std::string_view("a\x01" "b", 3)) == "a\\u0001b");
	REQUIRE(fastgltf::escapeString(std::string_view("a\x1f" "b", 3)) == "a\\u001fb");

	// Anything from U+0020 up is passed through untouched, including UTF-8 continuation bytes.
	REQUIRE(fastgltf::escapeString(" ") == " ");
	REQUIRE(fastgltf::escapeString("\xc3\xa9") == "\xc3\xa9");
}
