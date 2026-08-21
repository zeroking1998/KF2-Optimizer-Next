#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <set>
#include <string>

#include "kf2/diagnostics/feature_inventory.hpp"

#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__      \
 << ": check failed: " #x << '\n'; return EXIT_FAILURE; } } while(false)

int main() {
    using namespace kf2::diagnostics;
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
      }
    }
    const auto json = serialize_feature_inventory_json("test+abc");
    CHECK(json.find("KF2_ISSUE72_INVENTORY_V3") != std::string::npos);
    CHECK(json.find("\"build_identity\":\"test+abc\"") != std::string::npos);
    CHECK(json.find("\"function_count\":149") != std::string::npos);
    CHECK(json.find("\"id\":\"I72-A18-F08\"") != std::string::npos);
    CHECK(json.find("\"linkage\":") != std::string::npos);
    CHECK(json.find("\"remaining_scope\":\"external_validation\"") !=
          std::string::npos);
    return EXIT_SUCCESS;
}
