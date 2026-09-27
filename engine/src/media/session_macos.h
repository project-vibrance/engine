#pragma once

#include <vibranceUI/media/session.h>

class MacMediaSession
{
public:
    MacMediaSession();
    ~MacMediaSession();
    bool refresh(MediaSessionSnapshot& snapshot);
    bool send(MediaSessionCommand command);
    bool seek(std::int64_t milliseconds);

private:
    struct State;
    std::unique_ptr<State> state;
};
