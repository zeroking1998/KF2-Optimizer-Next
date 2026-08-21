#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "kf2/diagnostics/feature_inventory.hpp"

int wmain(int argc, wchar_t** argv) {
    if (argc != 3 || argv[1] == nullptr || argv[2] == nullptr) {
        std::wcerr << L"Usage: KF2InventoryExport <output.json> <build-identity>\n";
        return EXIT_FAILURE;
    }
    const auto records = kf2::diagnostics::issue72_feature_inventory();
    if (records.size() != 149) {
        std::wcerr << L"Issue 72 inventory count is not 149\n";
        return EXIT_FAILURE;
    }
    const std::wstring wide_identity{argv[2]};
    if (wide_identity.size() > 256) {
        std::wcerr << L"Build identity is too long\n";
        return EXIT_FAILURE;
    }
    std::string identity;
    identity.reserve(wide_identity.size());
    for (const wchar_t character : wide_identity) {
        if (character < 0x20 || character > 0x7e) {
            std::wcerr << L"Build identity must be printable ASCII\n";
            return EXIT_FAILURE;
        }
        identity.push_back(static_cast<char>(character));
    }
    const std::filesystem::path output_path{argv[1]};
    if (!output_path.is_absolute() || output_path.filename().empty()) {
        std::wcerr << L"Output path must be an absolute file path\n";
        return EXIT_FAILURE;
    }
    std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
    if (!output) {
        std::wcerr << L"Inventory output cannot be opened\n";
        return EXIT_FAILURE;
    }
    const auto document =
        kf2::diagnostics::serialize_feature_inventory_json(identity, records);
    output.write(document.data(), static_cast<std::streamsize>(document.size()));
    output.flush();
    if (!output) {
        std::wcerr << L"Inventory output cannot be completed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
