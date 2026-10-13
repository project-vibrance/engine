#include "mesh.h"
#include "../common/mesh_decode.h"

MetalModel load_metal_model(const std::filesystem::path &path, const MetalTextureUpload &upload)
{
    const auto absolute = std::filesystem::absolute(path);
    LoadedMesh mesh;
    std::vector<LoadedImage> images;
    std::vector<LoadedTexture> textures;
    if (absolute.extension() == ".glb" || absolute.extension() == ".GLB")
    {
        std::vector<LoadedMesh> meshes;
        if (!load_glb_meshes_json(absolute, meshes, images, textures))
            return {};
        mesh = merge_loaded_meshes(meshes);
    }
    else
    {
        auto data = fastgltf::GltfDataBuffer::FromPath(absolute);
        if (data.error() != fastgltf::Error::None)
            return {};
        fastgltf::Parser parser(fastgltf::Extensions::KHR_mesh_quantization);
        auto asset =
            parser.loadGltf(data.get(), absolute.parent_path(),
                            fastgltf::Options::LoadExternalBuffers | fastgltf::Options::LoadExternalImages);
        if (asset.error() != fastgltf::Error::None)
            return {};
        mesh = merge_scene_mesh(asset.get());
        images = decode_fastgltf_images(asset.get(), absolute.parent_path());
        textures = collect_fastgltf_textures(asset.get());
    }
    MetalModel result;
    result.vertices = flatten_mesh(mesh);
    normalise_for_demo_view(result.vertices);
    std::array<std::shared_ptr<StorageImage>, 5> fallback;
    const std::array<std::array<unsigned char, 4>, 5> colours{{{255, 255, 255, 255},
                                                               {128, 128, 255, 255},
                                                               {255, 255, 255, 255},
                                                               {255, 255, 255, 255},
                                                               {255, 255, 255, 255}}};
    for (size_t i = 0; i < 5; ++i)
        fallback[i] = upload(1, 1, colours[i], i == 0 || i == 4, false);
    std::vector<std::shared_ptr<StorageImage>> linear(textures.size()), srgb(textures.size());
    for (size_t i = 0; i < textures.size(); ++i)
    {
        const auto imageIndex = textures[i].imageIndex;
        if (imageIndex >= images.size() || !images[imageIndex].valid())
            continue;
        const auto &image = images[imageIndex];
        linear[i] = upload(image.width, image.height, image.rgba, false, true);
        srgb[i] = upload(image.width, image.height, image.rgba, true, true);
    }
    for (const auto &range : mesh.ranges)
    {
        MetalModelRange draw{range.firstIndex, range.indexCount, fallback};
        const std::array indices{range.material.baseColorTexture, range.material.normalTexture,
                                 range.material.metallicRoughnessTexture, range.material.occlusionTexture,
                                 range.material.emissiveTexture};
        for (size_t i = 0; i < 5; ++i)
        {
            const auto &table = i == 0 || i == 4 ? srgb : linear;
            if (indices[i] < table.size() && table[indices[i]])
            {
                draw.textures[i] = table[indices[i]];
                const auto &sampler = textures[indices[i]].sampler;
                draw.samplers[i] = {sampler.minFilter, sampler.magFilter, sampler.wrapS, sampler.wrapT};
            }
        }
        result.ranges.push_back(std::move(draw));
    }
    if (result.ranges.empty() && !result.vertices.empty())
        result.ranges.push_back({0, static_cast<uint32_t>(result.vertices.size()), fallback});
    return result;
}

MetalModel make_metal_triangle(const MetalTextureUpload &upload)
{
    MetalModel model;
    model.vertices = {
        {{-0.55f, 0.45f, 0.0f},
         {1.0f, 0.2f, 0.2f},
         {0.0f, 0.0f, 1.0f},
         {0.0f, 0.0f},
         {1.0f, 0.0f, 0.0f, 1.0f},
         {1.0f, 1.0f, 1.0f, 1.0f},
         {1.0f, 0.0f, 0.0f, 0.0f},
         {0.0f, 0.5f, 0.0f, 0.0f}},
        {{0.55f, 0.45f, 0.0f},
         {0.2f, 1.0f, 0.2f},
         {0.0f, 0.0f, 1.0f},
         {1.0f, 0.0f},
         {1.0f, 0.0f, 0.0f, 1.0f},
         {1.0f, 1.0f, 1.0f, 1.0f},
         {1.0f, 0.0f, 0.0f, 0.0f},
         {0.0f, 0.5f, 0.0f, 0.0f}},
        {{0.00f, -0.45f, 0.0f},
         {0.2f, 0.4f, 1.0f},
         {0.0f, 0.0f, 1.0f},
         {0.5f, 1.0f},
         {1.0f, 0.0f, 0.0f, 1.0f},
         {1.0f, 1.0f, 1.0f, 1.0f},
         {1.0f, 0.0f, 0.0f, 0.0f},
         {0.0f, 0.5f, 0.0f, 0.0f}},
    };

    MetalModelRange range{0, 3, {}};
    const std::array<std::array<unsigned char, 4>, 5> colours{{{255, 255, 255, 255},
                                                               {128, 128, 255, 255},
                                                               {255, 255, 255, 255},
                                                               {255, 255, 255, 255},
                                                               {255, 255, 255, 255}}};
    for (size_t i = 0; i < 5; ++i)
        range.textures[i] = upload(1, 1, colours[i], i == 0 || i == 4, false);
    model.ranges.push_back(std::move(range));
    return model;
}
