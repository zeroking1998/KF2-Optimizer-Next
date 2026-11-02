#pragma once

namespace kf2::flex::optimized {

inline bool compose_particle_indices(const int* mesh_indices, int vertex_count,
                                     const int* particle_indices,
                                     int particle_count, int capacity,
                                     int* output, int& maximum) {
    if(!mesh_indices || !particle_indices || !output || vertex_count <= 0 ||
       particle_count <= 0 || capacity <= 0)
        return false;

    maximum = -1;
    for(int vertex = 0; vertex < vertex_count; ++vertex) {
        const int local = mesh_indices[vertex];
        if(local < 0 || local >= particle_count) return false;
        const int global = particle_indices[local];
        if(global < 0 || global >= capacity) return false;
        output[vertex] = global;
        if(global > maximum) maximum = global;
    }
    return true;
}

} // namespace kf2::flex::optimized
