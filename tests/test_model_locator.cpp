#include <fstream>

#include "ModelLocator.h"
#include "test_framework.h"
#include "test_paths.h"

using whisperflow::ModelQuery;
using whisperflow::ModelSearch;
using whisperflow::ModelSize;

WF_TEST(ModelLocator_parsesKnownSizes) {
    ModelSize size{};
    WF_CHECK(whisperflow::parseModelSize("small", size));
    WF_CHECK(size == ModelSize::Small);
    WF_CHECK(whisperflow::parseModelSize("TINY", size));
    WF_CHECK(size == ModelSize::Tiny);
    WF_CHECK(whisperflow::parseModelSize(" medium ", size));
    WF_CHECK(size == ModelSize::Medium);
    WF_CHECK(!whisperflow::parseModelSize("large", size));
    WF_CHECK(!whisperflow::parseModelSize("", size));
}

WF_TEST(ModelLocator_buildsGgmlFileNames) {
    WF_CHECK_EQ(whisperflow::modelFileName(ModelSize::Tiny), std::string("ggml-tiny.bin"));
    WF_CHECK_EQ(whisperflow::modelFileName(ModelSize::Base), std::string("ggml-base.bin"));
    WF_CHECK_EQ(whisperflow::modelFileName(ModelSize::Small), std::string("ggml-small.bin"));
    WF_CHECK_EQ(whisperflow::modelFileName(ModelSize::Medium), std::string("ggml-medium.bin"));
    WF_CHECK_EQ(whisperflow::allModelSizeNames().size(), static_cast<std::size_t>(4));
}

WF_TEST(ModelLocator_honoursExplicitPath) {
    const std::filesystem::path dir = wftest::scratchDirectory("explicit");
    const std::filesystem::path model = dir / "custom-model.bin";
    WF_CHECK(wftest::writeFile(model, "not really a model"));

    ModelQuery query;
    query.explicitPath = model;
    const ModelSearch found = whisperflow::locateModel(query);
    WF_CHECK(found.found);
    WF_CHECK_EQ(found.path, model);

    ModelQuery missing;
    missing.explicitPath = dir / "absent.bin";
    const ModelSearch notFound = whisperflow::locateModel(missing);
    WF_CHECK(!notFound.found);
    WF_CHECK_EQ(notFound.searchedPaths.size(), static_cast<std::size_t>(1));
}

WF_TEST(ModelLocator_searchesDirectoriesInOrder) {
    const std::filesystem::path dir = wftest::scratchDirectory("search");
    const std::filesystem::path lowPriority = dir / "low";
    const std::filesystem::path highPriority = dir / "high";
    std::error_code ec;
    std::filesystem::create_directories(lowPriority, ec);
    std::filesystem::create_directories(highPriority, ec);
    WF_CHECK(wftest::writeFile(lowPriority / "ggml-tiny.bin", "x"));
    WF_CHECK(wftest::writeFile(highPriority / "ggml-tiny.bin", "y"));

    ModelQuery query;
    query.size = ModelSize::Tiny;
    query.extraSearchDirectories = {highPriority, lowPriority};

    const ModelSearch search = whisperflow::locateModel(query);
    WF_CHECK(search.found);
    WF_CHECK_EQ(search.path, highPriority / "ggml-tiny.bin");
}

WF_TEST(ModelLocator_reportsMissingModelWithGuidance) {
    ModelQuery query;
    query.size = ModelSize::Base;
    query.extraSearchDirectories = {wftest::scratchDirectory("empty")};

    const ModelSearch search = whisperflow::locateModel(query);
    WF_CHECK(!search.found);

    const std::string message = whisperflow::describeMissingModel(search);
    WF_CHECK(message.find("ggml-base.bin") != std::string::npos);
    WF_CHECK(message.find("download_model.ps1") != std::string::npos);
}

WF_TEST(ModelLocator_defaultDirectoriesIncludeLocalAppDataLayout) {
    const std::filesystem::path models = whisperflow::userModelsDirectory();
    WF_CHECK(models.filename() == "models");
    WF_CHECK(!whisperflow::defaultSearchDirectories(std::filesystem::path()).empty());
}
