#include <cstdlib>
#include <iostream>
#include <string>

#include "kf2/update/release_notes.hpp"

#define CHECK(condition) do { if (!(condition)) {                              \
    std::cerr << __FILE__ << ':' << __LINE__ << ": check failed: "            \
              #condition << '\n'; return EXIT_FAILURE; } } while (false)

int main() {
    const std::string source = R"(
# Version 0.0.3-alpha

## What's new
- Safe portable updater.
- Clear update settings.

## Technical details
- Internal helper protocol.
- WinHTTP timeouts.

## Bug fixes
* User data is preserved during replacement.

## Security
- This should not appear in the short changelog.

## Important notes
- Updates always require approval.
)";
    const auto concise = kf2::update::concise_release_notes(source);
    CHECK(concise.find("## What's new") != std::string::npos);
    CHECK(concise.find("Safe portable updater") != std::string::npos);
    CHECK(concise.find("## Bug fixes") != std::string::npos);
    CHECK(concise.find("User data is preserved") != std::string::npos);
    CHECK(concise.find("## Important notes") != std::string::npos);
    CHECK(concise.find("always require approval") != std::string::npos);
    CHECK(concise.find("Technical details") == std::string::npos);
    CHECK(concise.find("WinHTTP") == std::string::npos);
    CHECK(concise.find("Security") == std::string::npos);

    const auto wrapped = kf2::update::concise_release_notes(
        "## Bug fixes\n- Preserve original\n  settings after exit.\n"
        "\n  Unrelated paragraph.\n"
        "## Technical details\n- Hidden implementation\n  detail.\n"
        "## Important notes\r\n* Updates require\r\n\tapproval.\r\n");
    CHECK(wrapped == "## Bug fixes\n- Preserve original settings after exit."
        "\n\n## Important notes\n- Updates require approval.");
    CHECK(kf2::update::concise_release_notes(
        "## Bug fixes\n  Orphan continuation.\n- Accepted.\n"
        "Unrelated paragraph.\n  Still unrelated.\n- Next item.\n") ==
        "## Bug fixes\n- Accepted.\n- Next item.");

    const std::string boundary_item = "- " + std::string(508, 'x');
    CHECK(kf2::update::concise_release_notes(
        "## Bug fixes\n" + boundary_item + "\n  y\n") ==
        "## Bug fixes\n" + boundary_item + " y");
    CHECK(kf2::update::concise_release_notes(
        "## Bug fixes\n- Kept.\n" + boundary_item + "\n  yz\n"
        "  Detached.\n- Also kept.\n") ==
        "## Bug fixes\n- Kept.\n- Also kept.");
    CHECK(kf2::update::concise_release_notes(
        "## Bug fixes\n- Kept.\n- " + std::string(511, 'x') +
        "\n  Detached.\n") == "## Bug fixes\n- Kept.");

    std::string many_items = "## Bug fixes\n";
    std::string expected_items = "## Bug fixes";
    for (int index = 0; index < 32; ++index) {
        many_items += "- Kept.\n";
        expected_items += "\n- Kept.";
    }
    many_items += "- Rejected.\n  Detached.\n";
    CHECK(kf2::update::concise_release_notes(many_items) == expected_items);

    const auto fallback = kf2::update::concise_release_notes(
        "A long unstructured release description.");
    CHECK(fallback ==
          "## Important notes\n- No concise release notes were provided.");
    return EXIT_SUCCESS;
}
