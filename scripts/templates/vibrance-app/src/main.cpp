// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#include "starter.h"

#include <iostream>
#include <string_view>

int main(int argc, char** argv)
try
{
    const bool smokeTest = argc == 2 && std::string_view(argv[1]) == "--smoke-test";
    if (argc > 1 && !smokeTest)
    {
        std::cerr << "Usage: hello_vibrance [--smoke-test]\n";
        return 2;
    }
    return StarterApplication().run(smokeTest);
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
