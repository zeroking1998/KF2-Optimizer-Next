#include <cstdlib>

int main() {
    static_assert(sizeof(void*) == 8, "KF2 Optimizer Next supports x64 only");
    return EXIT_SUCCESS;
}
