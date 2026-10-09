#include <unity.h>

#include <string>

#include "services/service_status/status_page.h"

namespace {
using namespace cardputer_hub;
using connectivity::ServiceStatusLevel;

void assertLevel(ServiceStatusLevel expected,
                 const std::optional<connectivity::CompanionServiceStatus>& parsed) {
    TEST_ASSERT_TRUE(parsed.has_value());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(expected), static_cast<unsigned>(parsed->level));
}

void test_reads_the_indicator_and_description() {
    const auto parsed = services::parseStatusPage(
        R"({"page":{"id":"kctbh9vrtdwd","name":"GitHub","url":"https://www.githubstatus.com",)"
        R"("time_zone":"Etc/UTC","updated_at":"2026-10-06T14:26:42.465Z"},)"
        R"("status":{"indicator":"none","description":"All Systems Operational"}})");
    assertLevel(ServiceStatusLevel::Operational, parsed);
    TEST_ASSERT_EQUAL_STRING("All Systems Operational", parsed->description.data());
}

void test_maps_every_statuspage_indicator() {
    const struct {
        const char* indicator;
        ServiceStatusLevel level;
    } cases[] = {{"none", ServiceStatusLevel::Operational},
                 {"minor", ServiceStatusLevel::Minor},
                 {"major", ServiceStatusLevel::Major},
                 {"critical", ServiceStatusLevel::Critical},
                 {"maintenance", ServiceStatusLevel::Maintenance}};
    for (const auto& item : cases) {
        const std::string body =
            std::string(R"({"status":{"indicator":")") + item.indicator + R"("}})";
        assertLevel(item.level, services::parseStatusPage(body));
    }
}

void test_accepts_reordered_keys_and_whitespace() {
    // status.openai.com lists the description first.
    const auto parsed = services::parseStatusPage(
        "{\"page\":{\"name\":\"OpenAI\"},\n \"status\" : {\"description\" : "
        "\"Partially Degraded Service\", \"indicator\" : \"minor\"}}");
    assertLevel(ServiceStatusLevel::Minor, parsed);
    TEST_ASSERT_EQUAL_STRING("Partially Degraded Service", parsed->description.data());
}

void test_unescapes_and_truncates_the_description() {
    const auto escaped = services::parseStatusPage(
        R"({"status":{"indicator":"major","description":"API \"v2\" échec \/ ok"}})");
    assertLevel(ServiceStatusLevel::Major, escaped);
    TEST_ASSERT_EQUAL_STRING("API \"v2\" \xC3\xA9"
                             "chec / ok",
                             escaped->description.data());

    const std::string longText(60, 'x');
    const auto truncated = services::parseStatusPage(
        R"({"status":{"indicator":"minor","description":")" + longText + R"("}})");
    assertLevel(ServiceStatusLevel::Minor, truncated);
    TEST_ASSERT_EQUAL_UINT(connectivity::companionMaxStatusDescriptionSize,
                           std::string(truncated->description.data()).size());
}

void test_rejects_bodies_without_a_known_indicator() {
    TEST_ASSERT_FALSE(services::parseStatusPage("").has_value());
    TEST_ASSERT_FALSE(services::parseStatusPage("<html>Bad gateway</html>").has_value());
    TEST_ASSERT_FALSE(
        services::parseStatusPage(R"({"status":{"indicator":"purple"}})").has_value());
    TEST_ASSERT_FALSE(services::parseStatusPage(R"({"status":{"indicator":"min)").has_value());
    // An indicator outside the status object does not count.
    TEST_ASSERT_FALSE(
        services::parseStatusPage(R"({"page":{"indicator":"none"},"status":{}})").has_value());
}
} // namespace

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_reads_the_indicator_and_description);
    RUN_TEST(test_maps_every_statuspage_indicator);
    RUN_TEST(test_accepts_reordered_keys_and_whitespace);
    RUN_TEST(test_unescapes_and_truncates_the_description);
    RUN_TEST(test_rejects_bodies_without_a_known_indicator);
    return UNITY_END();
}
