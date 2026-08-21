#include <cstdlib>
#include <iostream>
#include <string>

#include "kf2/config/ini_document.hpp"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                \
            return EXIT_FAILURE;                                                \
        }                                                                       \
    } while (false)

int main() {
    using kf2::config::IniDocument;
    const std::string bytes =
        "; keep\r\n[Engine]\r\nUnknown = value\r\nTarget=60\r\n";
    auto parsed = IniDocument::parse(bytes);
    CHECK(parsed.has_value());
    CHECK(parsed.value().serialize() == bytes);
    CHECK(parsed.value().find(L"Engine", L"Target").value() == L"60");
    const auto replaced = parsed.value().replace(L"Engine", L"Target", L"90");
    CHECK(replaced.changed);
    CHECK(replaced.shadowed_occurrences == 0);
    CHECK(parsed.value().serialize() ==
          "; keep\r\n[Engine]\r\nUnknown = value\r\nTarget=90\r\n");

    auto duplicate = IniDocument::parse(
        "\xEF\xBB\xBF[Engine]\nTarget = 30 ; old\nTarget=60\n[Other]\nName=Résumé\n");
    CHECK(duplicate.has_value());
    CHECK(duplicate.value().encoding() == kf2::config::TextEncoding::utf8_bom);
    CHECK(duplicate.value().line_ending() == kf2::config::LineEnding::lf);
    const auto changed = duplicate.value().replace(L"engine", L"target", L"75");
    CHECK(changed.changed);
    CHECK(changed.shadowed_occurrences == 1);
    CHECK(duplicate.value().serialize().find("Target=75\n") != std::string::npos);
    CHECK(duplicate.value().serialize().find("Target = 30 ; old\n") != std::string::npos);

    auto added = IniDocument::parse("[Engine]\r\nExisting=1\r\n");
    CHECK(added.has_value());
    CHECK(added.value().replace(L"Engine", L"NewKey", L"yes").changed);
    CHECK(added.value().serialize() ==
          "[Engine]\r\nExisting=1\r\nNewKey=yes\r\n");

    auto section_added = IniDocument::parse("\xEF\xBB\xBF[Engine]\r\nExisting=1");
    CHECK(section_added.has_value());
    CHECK(section_added.value().upsert(
        L"KFGame.KFAISpawnManager", L"bLogWaveSpawnTiming", L"True").changed);
    CHECK(section_added.value().serialize() ==
          "\xEF\xBB\xBF[Engine]\r\nExisting=1\r\n"
          "[KFGame.KFAISpawnManager]\r\n"
          "bLogWaveSpawnTiming=True\r\n");
    CHECK(!section_added.value().upsert(
        L"KFGame.KFAISpawnManager", L"bLogWaveSpawnTiming", L"True").changed);

    auto array_values = IniDocument::parse(
        "[Engine.GameEngine]\r\nServerActors=IpDrv.WebServer\r\n"
        "bUsedForTakeover=True\r\n[Next]\r\nValue=1\r\n");
    CHECK(array_values.has_value());
    const auto appended = array_values.value().append_unique(
        L"Engine.GameEngine", L"ServerActors",
        L"KF2OptimizerTelemetry.KF2OptimizerTelemetryProbe");
    CHECK(appended.changed);
    CHECK(appended.shadowed_occurrences == 0);
    CHECK(array_values.value().serialize() ==
        "[Engine.GameEngine]\r\nServerActors=IpDrv.WebServer\r\n"
        "bUsedForTakeover=True\r\n"
        "ServerActors=KF2OptimizerTelemetry.KF2OptimizerTelemetryProbe\r\n"
        "[Next]\r\nValue=1\r\n");
    CHECK(!array_values.value().append_unique(
        L"Engine.GameEngine", L"ServerActors",
        L"KF2OptimizerTelemetry.KF2OptimizerTelemetryProbe").changed);

    auto ambiguous_sections = IniDocument::parse(
        "[Engine.GameEngine]\nServerActors=Probe.Class\n"
        "[Engine.GameEngine]\nB=2\n");
    CHECK(ambiguous_sections.has_value());
    const auto rejected = ambiguous_sections.value().append_unique(
        L"Engine.GameEngine", L"ServerActors", L"Probe.Class");
    CHECK(!rejected.changed);
    CHECK(rejected.shadowed_occurrences == 1);

    CHECK(!IniDocument::parse(std::string{"[X]\nA=1\0bad", 13}).has_value());
    CHECK(!IniDocument::parse("[broken\nA=1\n").has_value());
    return EXIT_SUCCESS;
}
