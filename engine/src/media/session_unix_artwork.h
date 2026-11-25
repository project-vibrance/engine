// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

class UnixMediaArtwork
{
public:
    ~UnixMediaArtwork();

    std::string resolve(std::string_view uri);

private:
    void run(std::stop_token stop);

    std::mutex mutex;
    std::condition_variable_any wake;
    std::atomic<std::uint64_t> revision {0};
    std::chrono::steady_clock::time_point nextAttempt;
    std::string requested;
    std::string directory;
    std::deque<std::pair<std::string, std::string>> cached;
    bool pending = false;
    std::jthread worker {[this](std::stop_token stop) { run(stop); }};
};
