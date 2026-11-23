// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vibranceUI/media/session.h>

class UnixMediaSession
{
public:
    UnixMediaSession();
    ~UnixMediaSession();
    bool refresh(MediaSessionSnapshot& snapshot);
    bool send(const MediaSessionSnapshot& snapshot, MediaSessionCommand command);
    bool seek(const MediaSessionSnapshot& snapshot, std::int64_t position);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
