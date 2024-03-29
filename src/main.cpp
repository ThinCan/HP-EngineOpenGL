﻿#include <engine/gpu/shaderprogram/shader_dec.hpp>
#include <filesystem>
#include <ranges>
#include <array>
#include <memory>
#include <numeric>
#include <initializer_list>
#include <compare>

#include <engine/engine.hpp>
#include <engine/camera/camera.hpp>
#include <engine/controller/keyboard/keyboard.hpp>
#include <engine/types/types.hpp>
#include <engine/renderer/renderer.hpp>
#include <engine/gpu/texture/texture.hpp>
#include <engine/gpu/buffers/buffer.hpp>
#include <engine/gpu/resource_manager/gpu_res_mgr.hpp>
#include <engine/gpu/framebuffer/framebuffer.hpp>

#include <GLFW/glfw3.h>
#include <assimp/scene.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <stb_image.h>
#include <imgui/imgui.h>
#include <imgui/imgui_stdlib.h>
#include <glm/gtx/euler_angles.hpp>

const auto make_model = [](glm::vec3 t, glm::vec3 r, glm::vec3 s) -> glm::mat4 {
    static constexpr auto I = glm::mat4{1.f};
    const auto T            = glm::translate(I, t);
    const auto R            = glm::eulerAngleXYZ(r.x, r.y, r.z);
    const auto S            = glm::scale(I, s);
    return T * R * S;
};

int main() {
    eng::Engine::initialise("window", 1920, 1080);
    auto &engine      = eng::Engine::instance();
    const auto window = engine.get_window();
    using namespace eng;

    engine.get_camera()->set_position(glm::vec3{1.f, 3.f, 5.f});
    Engine::instance().get_window()->set_clear_flags(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT
                                                     | GL_STENCIL_BUFFER_BIT);

    auto &prog = *engine.get_gpu_res_mgr()->create_resource(ShaderProgram{"a"});

    {
        Assimp::Importer i;
        auto scene
            = i.ReadFile("3dmodels/bust/scene.gltf",
                         aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_CalcTangentSpace);

        std::vector<Mesh> meshes;

        std::function<void(const aiScene *scene, const aiNode *node)> parse_scene;
        parse_scene = [&](const aiScene *scene, const aiNode *node) {
            for (auto i = 0u; i < node->mNumMeshes; ++i) {
                auto mesh = scene->mMeshes[node->mMeshes[i]];

                std::vector<float> mesh_vertices;
                std::vector<unsigned> mesh_indices;

                for (auto j = 0u; j < mesh->mNumVertices; ++j) {
                    auto &v = mesh->mVertices[j];

                    mesh_vertices.push_back(v.x);
                    mesh_vertices.push_back(v.y);
                    mesh_vertices.push_back(v.z);
                    mesh_vertices.push_back(mesh->mTextureCoords[0][j].x);
                    mesh_vertices.push_back(mesh->mTextureCoords[0][j].y);
                    mesh_vertices.push_back(mesh->mTextureCoords[0][j].z);
                    mesh_vertices.push_back(mesh->mTangents[j].x);
                    mesh_vertices.push_back(mesh->mTangents[j].y);
                    mesh_vertices.push_back(mesh->mTangents[j].z);
                    mesh_vertices.push_back(mesh->mBitangents[j].x);
                    mesh_vertices.push_back(mesh->mBitangents[j].y);
                    mesh_vertices.push_back(mesh->mBitangents[j].z);
                }

                for (auto j = 0u; j < mesh->mNumFaces; ++j) {
                    auto &face = mesh->mFaces[j];

                    assert((face.mNumIndices == 3 && "Accepting only triangular faces"));

                    mesh_indices.push_back(face.mIndices[0]);
                    mesh_indices.push_back(face.mIndices[1]);
                    mesh_indices.push_back(face.mIndices[2]);
                }

                auto material = scene->mMaterials[mesh->mMaterialIndex];
                aiString path;

                std::array<std::pair<aiTextureType, TextureType>, 5> textures_types{{
                    {aiTextureType_DIFFUSE, TextureType::Diffuse},
                    {aiTextureType_NORMALS, TextureType::Normal},
                    {aiTextureType_METALNESS, TextureType::Metallic},
                    {aiTextureType_DIFFUSE_ROUGHNESS, TextureType::Roughness},
                    {aiTextureType_EMISSIVE, TextureType::Emissive},
                }};

                Material *def_mat = engine.get_gpu_res_mgr()->create_resource(Material{});
                def_mat->passes[RenderPass::Forward] = &prog;

                for (const auto &[ait, t] : textures_types) {
                    const auto ait_count = material->GetTextureCount(ait);

                    if (ait_count == 0) { continue; }

                    aiString path;
                    std::string texture_path{"3dmodels/bust/"};

                    material->GetTexture(ait, 0, &path);
                    assert((path.length > 0 && "invalid path to texture"));
                    texture_path += path.C_Str();

                    auto texture         = engine.get_gpu_res_mgr()->create_resource(Texture{
                        TextureSettings{GL_RGB8, GL_CLAMP_TO_EDGE, GL_LINEAR_MIPMAP_LINEAR, 7},
                        TextureImageDataDescriptor{texture_path}});
                    def_mat->textures[t] = texture;
                }

                Mesh &m    = *engine.get_gpu_res_mgr()->create_resource(Mesh{});
                m.material = def_mat->res_handle();
                m.vertices = mesh_vertices;
                m.indices  = mesh_indices;
                meshes.push_back(m);
            }

            for (auto i = 0u; i < node->mNumChildren; ++i) {
                parse_scene(scene, node->mChildren[i]);
            }
        };

        parse_scene(scene, scene->mRootNode);

        Object o{meshes};
        engine.get_renderer()->register_object(&o);
    }

    engine.start();

    return 0;
}
