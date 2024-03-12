#include "buffer.hpp"

#include "../../engine/engine.hpp"
#include "../../renderer/renderer.hpp"

#include <stdexcept>
#include <cassert>

#include <glad/glad.h>

GLBuffer::GLBuffer(GLBufferDescriptor desc) : descriptor{desc} { glCreateBuffers(1, &descriptor.handle); }

GLBuffer::GLBuffer(void *data, uint32_t size_bytes, GLBufferDescriptor desc) : GLBuffer(desc) {
    push_data(data, size_bytes);
}

GLBuffer::GLBuffer(GLBuffer &&other) noexcept { *this = std::move(other); }

GLBuffer &GLBuffer::operator=(GLBuffer &&other) noexcept {
    descriptor              = std::move(other.descriptor);
    other.descriptor.handle = 0;
    return *this;
}

void GLBuffer::push_data(void *data, size_t data_size) {
    if (descriptor.capacity < descriptor.size + data_size) { resize(descriptor.size + data_size); }

    if ((descriptor.flags & GL_DYNAMIC_STORAGE_BIT) != GL_DYNAMIC_STORAGE_BIT) {
        GLuint temp_buffer;
        glCreateBuffers(1, &temp_buffer);
        glNamedBufferStorage(temp_buffer, data_size, data, 0);

        glCopyNamedBufferSubData(temp_buffer, descriptor.handle, 0, descriptor.size, data_size);
        glDeleteBuffers(1, &temp_buffer);
    } else {
        glNamedBufferSubData(descriptor.handle, descriptor.size, data_size, data);
    }

    descriptor.size += data_size;
}

void GLBuffer::resize(size_t required_size) {
    size_t new_capacity = fmaxl(required_size * GROWTH_FACTOR, 1.);

    uint32_t old_handle = descriptor.handle;
    uint32_t new_handle;

    glCreateBuffers(1, &new_handle);
    glNamedBufferStorage(new_handle, new_capacity, 0, descriptor.flags);
    glCopyNamedBufferSubData(old_handle, new_handle, 0, 0, descriptor.size);
    glDeleteBuffers(1, &old_handle);

    descriptor.handle   = new_handle;
    descriptor.capacity = new_capacity;
    descriptor.on_handle_change.emit(descriptor.handle);
}

GLBuffer::~GLBuffer() { glDeleteBuffers(1, &descriptor.handle); }

GLVao::GLVao(GLVaoDescriptor desc) {
    glCreateVertexArrays(1, &handle);

    auto &re = *eng::Engine::instance().renderer_.get();

    for (auto &vbob : desc.vbo_bindings) {
        auto &buff = re.get_buffer(vbob.buffer).descriptor;
        configure_binding(vbob.binding, buff.handle, vbob.stride, vbob.offset);
        buff.on_handle_change.connect([handle = this->handle, vbob, &buff, desc](auto nh) {
            glVertexArrayVertexBuffer(handle, vbob.binding, nh, vbob.offset, vbob.stride);
            // configure_binding(vbob.binding, buff.handle, vbob.stride, vbob.offset);
        });
    }

    if (auto var = std::get_if<GLVaoAttrCustom>(&desc.formats)) {
        configure_attr(var->formats);
    } else if (auto var = std::get_if<GLVaoAttrSameFormat>(&desc.formats)) {
        configure_attr(var->size, var->type, var->type_size_bytes, var->formats);
    } else if (auto var = std::get_if<GLVaoAttrSameType>(&desc.formats)) {
        configure_attr(var->type, var->type_size_bytes, var->formats);
    }

    if (desc.ebo_set) {
        auto &ebo = re.get_buffer(desc.ebo_buffer);
        configure_ebo(ebo.descriptor.handle);
        ebo.descriptor.on_handle_change.connect(
            [handle = this->handle, &desc = ebo.descriptor](auto nh) { glVertexArrayElementBuffer(handle, nh); });
    }
}

GLVao::GLVao(GLVao &&other) noexcept { *this = std::move(other); }

GLVao &GLVao::operator=(GLVao &&other) noexcept {
    handle       = other.handle;
    other.handle = 0u;

    return *this;
}

GLVao::~GLVao() { glDeleteVertexArrays(1, &handle); }

void GLVao::bind() const { glBindVertexArray(handle); }

void GLVao::configure_binding(uint32_t id, uint32_t handle, size_t stride, size_t offset) {
    glVertexArrayVertexBuffer(this->handle, id, handle, offset, stride);
}

void GLVao::configure_ebo(uint32_t handle) { glVertexArrayElementBuffer(this->handle, handle); }

void GLVao::configure_attr(std::vector<ATTRCUSTORMFORMAT> formats) {
    for (auto &f : formats) { _configure_impl(f.format_func, f.id, f.buffer, f.size, f.type, f.normalized, f.offset); }
}

void GLVao::configure_attr(uint32_t size, uint32_t type, uint32_t type_size, std::vector<ATTRSAMEFORMAT> formats) {
    std::map<uint32_t, uint32_t> buff_offsets;

    for (auto &f : formats) {
        _configure_impl(f.format_func, f.id, f.buffer, size, type, f.normalized, buff_offsets[f.buffer]);
        buff_offsets[f.buffer] += type_size * size;
    }
}

void GLVao::configure_attr(uint32_t type, uint32_t type_size, std::vector<ATTRSAMETYPE> formats) {
    std::map<uint32_t, uint32_t> buff_offsets;

    for (auto &f : formats) {
        _configure_impl(f.format_func, f.id, f.buffer, f.size, type, f.normalized, buff_offsets[f.buffer]);
        buff_offsets[f.buffer] += type_size * f.size;
    }
}

void GLVao::_configure_impl(ATTRFORMATCOMMON::GL_FUNC func,
                            uint32_t idx,
                            uint32_t buffer,
                            uint32_t size,
                            uint32_t type,
                            uint32_t normalized,
                            uint32_t offset) {

    glEnableVertexArrayAttrib(handle, idx);
    glVertexArrayAttribBinding(handle, idx, buffer);

    switch (func) {
    case ATTRFORMATCOMMON::GL_FUNC::FLOAT:
        glVertexArrayAttribFormat(handle, idx, size, type, normalized, offset);
        break;
    case ATTRFORMATCOMMON::GL_FUNC::INT: glVertexArrayAttribIFormat(handle, idx, size, type, offset); break;
    case ATTRFORMATCOMMON::GL_FUNC::LONG: glVertexArrayAttribLFormat(handle, idx, size, type, offset); break;
    default: break;
    }
}