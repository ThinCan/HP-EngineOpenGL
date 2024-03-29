#pragma once

#include <concepts>
#include <type_traits>
#include <typeindex>
#include <unordered_map>

#include <engine/types/sorted_vec.hpp>
#include <engine/types/idresource.hpp>
#include <engine/gpu/buffers/buffer.hpp>
#include <engine/gpu/texture/texture.hpp>

namespace eng {
    template <typename Resource>
    concept GpuResource = std::derived_from<Resource, IdResource<Resource>>
                          and std::default_initializable<Resource>;

    class GpuResMgr {
      public:
        ~GpuResMgr();

        template <GpuResource Resource> Resource *create_resource(Resource &&rsc) {
            return static_cast<Resource *>(
                _get_storage<Resource>().insert(new Resource{std::move(rsc)}));
        }
        template <typename Resource> Resource *get_resource(Handle<Resource>);
        template <typename Resource> Resource *get_resource(size_t idx) {
            return _get_storage<Resource>()[idx];
        }
        template <typename Resource> auto count() { return _get_storage<Resource>().size(); };

        // Don't like this idea, but didn't have time to make something safer.
        // Casting from vec<B*> to vec<D*>: no object slicing, containers are always homogenous.
        // B - base, D - Derived
        template <typename Resource> auto &get_storage() {
            return *reinterpret_cast<SortedVector<Resource *, _sort_func_t> *>(
                &_get_storage<Resource>());
        }

      private:
        using _sort_func_t = decltype([](auto &&a, auto &&b) { return a->id < b->id; });
        using _storage_t   = SortedVector<IdWrapper *, _sort_func_t>;

        template <typename Resource> _storage_t &_get_storage() {
            auto ti = std::type_index{typeid(Resource)};

            if (_containers.contains(ti) == false) { _containers[ti]; }

            return _containers.at(ti);
        }

        std::unordered_map<std::type_index, _storage_t> _containers;
    };

    template <typename Resource> Resource *GpuResMgr::get_resource(Handle<Resource> handle) {
        auto &storage = _get_storage<Resource>();
        auto p_data   = storage.try_find(handle, [](auto &&a, auto &&b) {
            uint32_t _a, _b;

            // clang-format off
            if constexpr (std::is_pointer_v<std::remove_reference_t<decltype(a)>>) { _a = a->id; } 
            else { _a = a.id; }
            if constexpr (std::is_pointer_v<std::remove_reference_t<decltype(b)>>) { _b = b->id; } 
            else { _b = b.id; }
            // clang-format on

            return _a < _b;
        });

        assert(p_data != nullptr && "Bad handle");

        return static_cast<Resource *>(*p_data);
    }

} // namespace eng