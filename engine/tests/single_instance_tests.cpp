#include <vibranceUI/core/single_instance.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace
{
    bool expect(bool condition, std::string_view message)
    {
        if (!condition)
        {
            std::cerr << "single instance test failed: " << message << '\n';
        }
        return condition;
    }
}

int main(int argc, char* argv[])
{
    if (argc == 3 && std::string_view(argv[1]) == "--probe")
    {
        SingleInstanceOptions probeOptions = {};
        probeOptions.enabled = true;
        probeOptions.identifier = argv[2];
        probeOptions.showDuplicateError = false;
        SingleInstanceGuard probe(probeOptions);
        return probe.status() == SingleInstanceStatus::eDuplicate ? 0 : 2;
    }

    bool passed = true;
    SingleInstanceGuard disabled;
    passed &= expect(
        disabled.can_run() &&
            disabled.status() == SingleInstanceStatus::eDisabled,
        "the process guard should remain opt-in");

    SingleInstanceOptions options = {};
    options.enabled = true;
    options.identifier = "vibrance-engine-test-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    options.applicationName = "Vibrance engine test";
    options.showDuplicateError = false;

    {
        SingleInstanceGuard primary(options);
        passed &= expect(
            primary.can_run() && primary.owns_instance() &&
                primary.status() == SingleInstanceStatus::ePrimary,
            "the first guard should acquire the process instance");

        SingleInstanceGuard duplicate(options);
        passed &= expect(
            !duplicate.can_run() && !duplicate.owns_instance() &&
                duplicate.status() == SingleInstanceStatus::eDuplicate,
            "a second guard with the same identifier should be rejected");

        const std::string command = "\"" + std::string(argv[0]) +
            "\" --probe " + options.identifier;
        passed &= expect(
            std::system(command.c_str()) == 0,
            "a separate process with the same identifier should be rejected");

        primary.release();
        passed &= expect(
            !primary.owns_instance() &&
                primary.status() == SingleInstanceStatus::eDisabled,
            "an explicit release should relinquish the process instance");
        SingleInstanceGuard replacement(options);
        passed &= expect(
            replacement.can_run() && replacement.owns_instance(),
            "a replacement process guard should acquire after explicit release");
    }

    SingleInstanceGuard reacquired(options);
    passed &= expect(
        reacquired.can_run() && reacquired.owns_instance(),
        "destroying the primary guard should release the instance");
    return passed ? 0 : 1;
}
