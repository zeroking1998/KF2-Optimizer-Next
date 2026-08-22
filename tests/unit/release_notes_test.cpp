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

    const auto fallback = kf2::update::concise_release_notes(
        "A long unstructured release description.");
    CHECK(fallback ==
          "## Important notes\n- No concise release notes were provided.");
    return EXIT_SUCCESS;
}
