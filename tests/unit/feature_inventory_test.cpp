#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <new>
#include <sstream>
#include <set>
#include <stdexcept>
#include <string>

#include "kf2/diagnostics/feature_inventory.hpp"

int inventory_export_for_testing(int argc, wchar_t** argv);

namespace {
thread_local bool fail_inventory_growth{}, inventory_growth_failed{};
}
void* operator new(std::size_t size) {
    if (fail_inventory_growth && size == 1024) {
        fail_inventory_growth = false;
        inventory_growth_failed = true;
        throw std::bad_alloc{};
    }
    if (void* memory = std::malloc(size == 0 ? 1 : size)) return memory;
    throw std::bad_alloc{};
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__      \
 << ": check failed: " #x << '\n'; return EXIT_FAILURE; } } while(false)

int main() {
    using namespace kf2::diagnostics;
    static_assert(!noexcept(issue72_feature_inventory()));
    bool initialization_failed = false;
    try {
        static_cast<void>(issue72_feature_inventory([] {
            throw std::runtime_error{
                "injected feature-inventory construction failure"};
        }));
    } catch (const std::runtime_error&) {
        initialization_failed = true;
    }
    CHECK(initialization_failed);
    const auto records = issue72_feature_inventory();
    CHECK(records.size() == 149);
    const auto counts = feature_status_counts(records);
    CHECK(counts.present == 89);
    CHECK(counts.partial == 59);
    CHECK(counts.planned == 0);
    CHECK(counts.discarded == 1);
    CHECK(counts.implementation_ready == 0);
    const auto remaining = remaining_scope_counts(records);
    CHECK(remaining.none == 89);
    CHECK(remaining.external_validation > 0);
    CHECK(remaining.engine_contract > 0);
    CHECK(remaining.safety_boundary == 2);
    CHECK(remaining.user_authority > 0);
    CHECK(remaining.none + remaining.external_validation +
              remaining.engine_contract + remaining.safety_boundary +
              remaining.user_authority == records.size());
    constexpr std::size_t area_counts[]{
        9, 9, 8, 8, 8, 7, 8, 4, 10, 6, 7, 14, 8, 10, 8, 9, 8, 8};
    std::set<std::string> ids;
    std::size_t index = 0;
    for (std::size_t area = 0; area < std::size(area_counts); ++area) {
      for (std::size_t item = 0; item < area_counts[area]; ++item, ++index) {
        const auto& record = records[index];
        std::ostringstream expected;
        expected << "I72-A" << std::setw(2) << std::setfill('0') << area + 1
                 << "-F" << std::setw(2) << item + 1;
        CHECK(record.id == expected.str());
        CHECK(record.area == area + 1);
        CHECK(record.item == item + 1);
        CHECK(ids.insert(record.id).second);
        CHECK(!record.name.empty());
        CHECK(!record.user_requirement.empty());
        CHECK(!record.code_path.empty());
        CHECK(!record.data_source.empty());
        CHECK(!record.trust_class.empty());
        CHECK(!record.technical_statement.empty());
        CHECK(!record.expected_benefit.empty());
        CHECK(!record.risks.empty());
        CHECK(!record.mode_support.empty());
        CHECK(!record.reversible_path.empty());
        CHECK(!record.dependencies.empty());
        CHECK(!record.required_tests.empty());
        CHECK(!record.evidence.empty());
        CHECK(!record.decision.empty());
        CHECK(!record.linkage.empty());
        CHECK(record.decision == "PRESENT" || record.decision == "OBSERVE" ||
              record.decision == "LAB" || record.decision == "DISCARD" ||
              record.decision == "IMPLEMENTATION_READY");
        if (record.id == "I72-A01-F05") {
            CHECK(record.technical_statement.find(
                "checks all manifest-listed payload hashes") != std::string::npos);
        }
        if (record.id == "I72-A03-F01") {
            CHECK(record.data_source ==
                "Three verified KF2 INIs and a strict verified settings catalog");
        }
        if (record.id == "I72-A10-F01") {
            CHECK(record.evidence ==
                "Verified settings catalog tests; optimizer tests; copied-config "
                "roundtrip; target gameplay evidence");
        }
        if (record.id == "I72-A17-F06") {
            CHECK(record.name.find("CI, workflows, artifacts, and cleanup") !=
                  std::string::npos);
            CHECK(record.dependencies.find("GitHub-hosted Windows CI") !=
                  std::string::npos);
            CHECK(record.technical_statement.find(
                "Desktop checks and complete portable/SDK packages are separate "
                "local validations; CI does not establish gameplay acceptance.") !=
                  std::string::npos);
        }
      }
    }
    const auto json = serialize_feature_inventory_json("test+abc", records);
    CHECK(json.find("KF2_ISSUE72_INVENTORY_V3") != std::string::npos);
    CHECK(json.find("\"build_identity\":\"test+abc\"") != std::string::npos);
    CHECK(json.find("\"function_count\":149") != std::string::npos);
    CHECK(json.find("\"id\":\"I72-A18-F08\"") != std::string::npos);
    CHECK(json.find("\"linkage\":") != std::string::npos);
    CHECK(json.find("\"remaining_scope\":\"external_validation\"") !=
          std::string::npos);
    bool serialization_failed = false;
    std::size_t failed_bytes{};
    fail_inventory_growth = true;
    try {
        failed_bytes = serialize_feature_inventory_json("test+abc", records).size();
    } catch (...) {
        serialization_failed = true;
    }
    fail_inventory_growth = false;
    std::cout << "Inventory growth failure: injected=" << inventory_growth_failed
              << "; rejected=" << serialization_failed
              << "; returned bytes=" << failed_bytes << '\n';
    CHECK(inventory_growth_failed && serialization_failed);
    CHECK(serialize_feature_inventory_json("test+abc", records) == json);
    namespace fs = std::filesystem;
    const fs::path root{KF2_TEST_ROOT};
    fs::create_directories(root);
    const auto read_bytes = [](const fs::path& path) {
        std::ifstream input{path, std::ios::binary};
        return std::string{std::istreambuf_iterator<char>{input}, {}};
    };
    for (const bool existing : {true, false}) {
        const auto path = root / (existing ? L"inventory.json" : L"missing.json");
        if (!existing && fs::exists(path)) CHECK(fs::remove(path));
        auto output_argument = path.wstring();
        wchar_t program[] = L"inventory-test";
        wchar_t identity[] = L"test+abc";
        wchar_t* arguments[]{program, output_argument.data(), identity};
        if (existing) CHECK(inventory_export_for_testing(3, arguments) == EXIT_SUCCESS);
        inventory_growth_failed = false;
        fail_inventory_growth = true;
        const int result = inventory_export_for_testing(3, arguments);
        fail_inventory_growth = false;
        const bool injected = inventory_growth_failed;
        const bool created = fs::exists(path);
        const bool preserved = existing ? read_bytes(path) == json : !created;
        // Restore the owned fixture before reporting a failed preservation check.
        if (existing) {
            CHECK(inventory_export_for_testing(3, arguments) == EXIT_SUCCESS);
            CHECK(read_bytes(path) == json);
        } else if (created) {
            CHECK(fs::remove(path));
        }
        std::cout << "Inventory export failure: prior=" << existing
                  << "; injected=" << injected << "; result=" << result
                  << "; preserved=" << preserved << '\n';
        CHECK(injected && result == EXIT_FAILURE && preserved);
        CHECK(inventory_export_for_testing(3, arguments) == EXIT_SUCCESS);
        CHECK(read_bytes(path) == json);
        if (!existing) CHECK(fs::remove(path));
    }
    const auto escaped = serialize_feature_inventory_json(
        "quote\"\\\nUnicode caf\xc3\xa9", {});
    CHECK(escaped ==
        "{\"schema\":\"KF2_ISSUE72_INVENTORY_V3\",\"build_identity\":"
        "\"quote\\\"\\\\\\nUnicode caf\xc3\xa9\",\"issue\":72,\"function_count\":0,"
        "\"status_counts\":{\"present\":0,\"partial\":0,\"planned\":0,"
        "\"discarded\":0,\"implementation_ready\":0},"
        "\"remaining_scope_counts\":{\"none\":0,\"external_validation\":0,"
        "\"engine_contract\":0,\"safety_boundary\":0,\"user_authority\":0},"
        "\"records\":[]}");
    const auto owned = [&] {
        auto record = records.back();
        record.name.assign(4096, 'x');
        record.name.append(std::string{"\0\"\\\n\x01", 5});
        record.linkage = "Owned after source destruction";
        return serialize_feature_inventory_json("growth", {&record, 1});
    }();
    const auto subsequent = serialize_feature_inventory_json("unrelated", {});
    CHECK(subsequent.find("\"build_identity\":\"unrelated\"") !=
          std::string::npos);
    CHECK(owned.find("\"name\":\"" + std::string(4096, 'x') +
        "\\u0000\\\"\\\\\\n\\u0001\"") != std::string::npos);
    CHECK(owned.find("\"linkage\":\"Owned after source destruction\"") !=
          std::string::npos);
    return EXIT_SUCCESS;
}
