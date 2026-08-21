#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace kf2::diagnostics {

enum class FeatureStatus {
    present,
    partial,
    planned,
    discarded,
    implementation_ready,
};

enum class RemainingScope {
    none,
    external_validation,
    engine_contract,
    safety_boundary,
    user_authority,
};

struct FeatureRecord {
    std::string id;
    std::string name;
    std::uint16_t area{};
    std::uint16_t item{};
    FeatureStatus status;
    RemainingScope remaining_scope{RemainingScope::none};
    std::string user_requirement;
    std::string code_path;
    std::string data_source;
    std::string trust_class;
    std::string technical_statement;
    std::string expected_benefit;
    std::string risks;
    std::string mode_support;
    std::string reversible_path;
    std::string dependencies;
    std::string required_tests;
    std::string evidence;
    std::string decision;
    std::string linkage;
};

struct RemainingScopeCounts {
    std::size_t none{0};
    std::size_t external_validation{0};
    std::size_t engine_contract{0};
    std::size_t safety_boundary{0};
    std::size_t user_authority{0};
};

[[nodiscard]] std::span<const FeatureRecord> issue72_feature_inventory() noexcept;
struct FeatureStatusCounts {
    std::size_t present{0};
    std::size_t partial{0};
    std::size_t planned{0};
    std::size_t discarded{0};
    std::size_t implementation_ready{0};
};
[[nodiscard]] FeatureStatusCounts feature_status_counts(
    std::span<const FeatureRecord> records = issue72_feature_inventory()) noexcept;
[[nodiscard]] RemainingScopeCounts remaining_scope_counts(
    std::span<const FeatureRecord> records = issue72_feature_inventory()) noexcept;
[[nodiscard]] std::string serialize_feature_inventory_json(
    std::string_view build_identity,
    std::span<const FeatureRecord> records = issue72_feature_inventory());

}  // namespace kf2::diagnostics
