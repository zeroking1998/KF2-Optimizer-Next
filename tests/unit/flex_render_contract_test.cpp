#include "render_contract.hpp"

#include <array>

#define CHECK(condition) do { if(!(condition)) return __LINE__; } while(false)

int main() {
    using kf2::flex::optimized::compose_particle_indices;

    const std::array mesh{2, 0, 3, 1};
    const std::array particles{41, 7, 100, 19};
    std::array<int, mesh.size()> composed{};
    int maximum{};
    CHECK(compose_particle_indices(mesh.data(), static_cast<int>(mesh.size()),
                                   particles.data(),
                                   static_cast<int>(particles.size()), 128,
                                   composed.data(), maximum));
    CHECK((composed == std::array{100, 41, 19, 7}));
    CHECK(maximum == 100);

    auto invalid_mesh = mesh;
    invalid_mesh[1] = static_cast<int>(particles.size());
    CHECK(!compose_particle_indices(
        invalid_mesh.data(), static_cast<int>(invalid_mesh.size()),
        particles.data(), static_cast<int>(particles.size()), 128,
        composed.data(), maximum));

    auto invalid_particles = particles;
    invalid_particles[2] = 128;
    CHECK(!compose_particle_indices(
        mesh.data(), static_cast<int>(mesh.size()), invalid_particles.data(),
        static_cast<int>(invalid_particles.size()), 128, composed.data(),
        maximum));

    CHECK(!compose_particle_indices(nullptr, 4, particles.data(), 4, 128,
                                    composed.data(), maximum));
    CHECK(!compose_particle_indices(mesh.data(), 0, particles.data(), 4, 128,
                                    composed.data(), maximum));
    return 0;
}
