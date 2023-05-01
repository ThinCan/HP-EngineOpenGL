#include "renderer.hpp"
#include <engine/engine.hpp>

// #define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

eng::Renderer::Renderer() {
    glCreateVertexArrays(1, &vao);
    glVertexArrayAttribBinding(vao, 0, 0);
    glVertexArrayAttribBinding(vao, 1, 0);
    glVertexArrayAttribBinding(vao, 2, 0);
    glVertexArrayAttribBinding(vao, 3, 0);
    glEnableVertexArrayAttrib(vao, 0);
    glEnableVertexArrayAttrib(vao, 1);
    glEnableVertexArrayAttrib(vao, 2);
    glEnableVertexArrayAttrib(vao, 3);
    glVertexArrayAttribFormat(vao, 0, 3, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribFormat(vao, 1, 3, GL_FLOAT, GL_FALSE, 12);
    glVertexArrayAttribFormat(vao, 2, 3, GL_FLOAT, GL_FALSE, 24);
    glVertexArrayAttribFormat(vao, 3, 3, GL_FLOAT, GL_FALSE, 36);
}

namespace eng {
    void MeshPass::refresh(Renderer *r) {
        while (unbatched.empty() == false) {
            auto h_ro = unbatched.front();
            unbatched.erase(unbatched.begin());
            auto &ro = r->get_render_object(h_ro);

            PassObject po{.mat
                          = {.prog = r->get_material(ro.material).passes.at(RenderPass::Forward)},
                          .mesh          = ro.mesh,
                          .render_object = h_ro};

            pass_objects.push_back(po);
        }

        flat_batches.clear();
        indirect_batches.clear();
        multi_batches.clear();

        for (const auto &po : pass_objects) {
            auto &ro = r->get_render_object(po.render_object);
            flat_batches.emplace_back(get_batch_id(po.mesh, ro.material),
                                      Handle<PassObject>(po.id));
        }

        std::sort(flat_batches.begin(), flat_batches.end(), [](auto &&a, auto &&b) {
            return a.batch_id < b.batch_id;
        });

        FlatBatch *prev_fb = &flat_batches[0];
        {
            IndirectBatch ib{.mesh     = get_pass_object(prev_fb->object).mesh,
                             .material = get_pass_object(prev_fb->object).mat,
                             .first    = 0,
                             .count    = 1};
            indirect_batches.push_back(ib);
        }
        IndirectBatch *curr_ib = &indirect_batches[0];
        for (auto i = 1u; i < flat_batches.size(); ++i) {
            auto &fb = flat_batches[i];
            if (fb.batch_id != prev_fb->batch_id) {
                IndirectBatch ib{.mesh     = get_pass_object(fb.object).mesh,
                                 .material = get_pass_object(fb.object).mat,
                                 .first    = i,
                                 .count    = 1};
                indirect_batches.push_back(ib);
                curr_ib = &indirect_batches.back();
                prev_fb = &fb;
                continue;
            }
            prev_fb = &fb;
            curr_ib->count++;
        }

        multi_batches.push_back(MultiBatch{.first = 0, .count = (uint32_t)indirect_batches.size()});

        return;
    }

    uint32_t MeshPass::get_batch_id(Handle<Mesh> mesh, Handle<Material> mat) {
        auto it = std::find(_batch_ids.begin(), _batch_ids.end(), std::make_pair(mesh, mat));

        if (it == _batch_ids.end()) {
            _batch_ids.push_back(std::make_pair(mesh, mat));
            it = _batch_ids.end() - 1;
        }

        return std::distance(_batch_ids.begin(), it);
    }

    void Renderer::register_object(const Object *o) {

        for (auto &m : o->meshes) {
            RenderObject ro{.object_id = o->id,
                            .mesh      = get_resource_handle(m),
                            .material  = get_resource_handle(*m.material),
                            .transform = m.transform};

            _renderables.insert(ro);
            _dirty_objects.emplace_back(ro.id);
            _mesh_instance_count[m.id]++;

            if (m.material->passes.contains(RenderPass::Forward)) {
                forward_pass.unbatched.push_back(Handle<RenderObject>{ro.id});
            }
        }
    }

    void Renderer::render() {
        if (_dirty_objects.empty() == false) {
            _dirty_objects.clear();

            std::unordered_map<uint32_t, uint32_t> offsets;

            struct alignas(16) Payload {
                uint64_t diffuse;
                uint64_t normal;
                uint64_t metallic;
                uint64_t roughness;
                uint64_t emissive;
                uint64_t _1;
                glm::vec4 texture_channels;
                glm::mat4 transform;
            };

            std::vector<Payload> mesh_data;
            std::vector<uint64_t> mesh_bindless_handle;
            mesh_data.resize(_renderables.size());
            mesh_bindless_handle.resize(_renderables.size());

            offsets[_meshes[0].id] = 0;
            for (auto i = 1u, prev_offset = 0u; i < _meshes.size(); ++i) {
                const auto &m0    = _meshes[i - 1];
                const auto &m1    = _meshes[i];
                const auto offset = _mesh_instance_count.at(m0.id) + prev_offset;
                offsets[m1.id]    = offset;
                prev_offset       = offset;
            }

            for (const auto &r : _renderables) {
                const auto offset = offsets.at(r.mesh.id)++;
                const auto &mesh  = *_meshes.try_find(r.mesh);

                mesh.material->textures.at(TextureType::Diffuse)->make_resident();
                mesh.material->textures.at(TextureType::Normal)->make_resident();
                mesh.material->textures.at(TextureType::Metallic)->make_resident();
                mesh.material->textures.at(TextureType::Roughness)->make_resident();
                mesh.material->textures.at(TextureType::Emissive)->make_resident();

                mesh_data[offset] = {
                    mesh.material->textures.at(TextureType::Diffuse)->bindless_handle(),
                    mesh.material->textures.at(TextureType::Normal)->bindless_handle(),
                    mesh.material->textures.at(TextureType::Metallic)->bindless_handle(),
                    mesh.material->textures.at(TextureType::Roughness)->bindless_handle(),
                    mesh.material->textures.at(TextureType::Emissive)->bindless_handle(),
                    0,
                    glm::vec4{0.f, 0.f, 1.f, 0.f},
                    r.transform,
                };

                ENG_DEBUG("Inserting %i at %i\n", mesh.id, offset);
            }

            mesh_data_buffer.clear_invalidate();
            mesh_data_buffer.push_data(mesh_data.data(), mesh_data.size() * sizeof(Payload));

            std::vector<float> mesh_vertices;
            std::vector<unsigned> mesh_indices;
            for (const auto &m : _meshes) {
                mesh_vertices.insert(mesh_vertices.end(), m.vertices.begin(), m.vertices.end());
                mesh_indices.insert(mesh_indices.end(), m.indices.begin(), m.indices.end());
            }

            geometry_buffer.clear_invalidate();
            geometry_buffer.push_data(mesh_vertices.data(), mesh_vertices.size() * sizeof(float));

            index_buffer.clear_invalidate();
            index_buffer.push_data(mesh_indices.data(), mesh_indices.size() * sizeof(unsigned));

            glVertexArrayVertexBuffer(vao, 0, geometry_buffer.descriptor.handle, 0, 48);
            glVertexArrayElementBuffer(vao, index_buffer.descriptor.handle);
        }

        if (forward_pass.unbatched.empty() == false) { forward_pass.refresh(this); }

        struct DrawElementsIndirectCommandExtended : public DrawElementsIndirectCommand {
            DrawElementsIndirectCommandExtended() = default;
            DrawElementsIndirectCommandExtended(const DrawElementsIndirectCommand *cmd,
                                                uint32_t vertex_count) {
                *static_cast<DrawElementsIndirectCommand *>(this) = *cmd;
                this->vertex_count                                = vertex_count;
            }

            uint32_t vertex_count{0u};
        } prev_cmd;

        std::vector<DrawElementsIndirectCommand> draw_commands;

        for (auto i = 0u; i < forward_pass.indirect_batches.size(); ++i) {
            const auto &ib = forward_pass.indirect_batches[i];
            const auto &m  = *_meshes.try_find(ib.mesh);
            DrawElementsIndirectCommand cmd{
                .count          = (uint32_t)m.indices.size(),
                .instance_count = ib.count,
                .first_index    = prev_cmd.first_index + prev_cmd.count,
                .base_vertex    = prev_cmd.base_vertex + prev_cmd.vertex_count,
                .base_instance  = prev_cmd.base_instance + prev_cmd.instance_count};
            prev_cmd = DrawElementsIndirectCommandExtended{&cmd, (uint32_t)m.vertices.size() / 12u};
            draw_commands.push_back(cmd);
        }

        commands_buffer.clear_invalidate();
        commands_buffer.push_data(draw_commands.data(),
                                  draw_commands.size() * sizeof(DrawElementsIndirectCommand));

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);

        glBindVertexArray(vao);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, mesh_data_buffer.descriptor.handle);
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, commands_buffer.descriptor.handle);
        for (const auto &mb : forward_pass.multi_batches) {
            forward_pass.indirect_batches[mb.first].material.prog->use();
            forward_pass.indirect_batches[mb.first].material.prog->set(
                "v", Engine::instance().cam->view_matrix());
            forward_pass.indirect_batches[mb.first].material.prog->set( "p", Engine::instance().cam->perspective_matrix());
            forward_pass.indirect_batches[mb.first].material.prog->set( "view_vec", Engine::instance().cam->forward_vec());
            forward_pass.indirect_batches[mb.first].material.prog->set( "view_pos", Engine::instance().cam->position());
            forward_pass.indirect_batches[mb.first].material.prog->use();
            glMultiDrawElementsIndirect(GL_TRIANGLES,
                                        GL_UNSIGNED_INT,
                                        (void *)(mb.first * sizeof(DrawElementsIndirectCommand)),
                                        mb.count,
                                        0);
        }
    }

    RenderObject &Renderer::get_render_object(Handle<RenderObject> h) {
        auto ptr_ro = _renderables.try_find(h);
        assert((ptr_ro != nullptr && "Invalid handle"));
        return *ptr_ro;
    }
    Material &Renderer::get_material(Handle<Material> h) {
        auto ptr_ro = _materials.try_find(h);
        assert((ptr_ro != nullptr && "Invalid handle"));
        return *ptr_ro;
    }

    template <typename Resource> Handle<Resource> Renderer::get_resource_handle(const Resource &m) {

        SortedVector<Resource, IdResourceSortComp> *vec{nullptr};
        if constexpr (std::is_same_v<Resource, Mesh>) {
            vec = &_meshes;
        } else if (std::is_same_v<Resource, Material>) {
            vec = &_materials;
        }

        assert((vec != nullptr && "Resource not recognized"));

        auto ptr_data = vec->try_find(m);

        if (ptr_data == nullptr) { vec->insert(m); }

        return Handle<Resource>{m.id};
    }

    template <typename Iter>
    typename Iter::value_type *Renderer::try_find_idresource(uint32_t id, Iter &it) {
        auto data = std::lower_bound(
            it.begin(), it.end(), id, [](auto &&e, auto &&v) { return e.id < v; });

        if (data == it.end() || data->id != id) { return nullptr; }

        return &*data;
    }
} // namespace eng
