#pragma once
#include <vibranceUI/factories/mesh_factory.h>
#include <vibranceUI/renderer/metal/resources.h>
#include <array>
struct MetalModelSampler
{
    uint32_t minFilter = 9729, magFilter = 9729, wrapS = 10497, wrapT = 10497;
};
struct MetalModelRange
{
    uint32_t firstVertex = 0, vertexCount = 0;
    std::array<std::shared_ptr<StorageImage>, 5> textures;
    std::array<MetalModelSampler, 5> samplers{};
};
struct MetalModel
{
    std::vector<Vertex> vertices;
    std::vector<MetalModelRange> ranges;
};
MetalModel load_metal_model(const std::filesystem::path &, const MetalTextureUpload &);

MetalModel make_metal_triangle(const MetalTextureUpload &);
