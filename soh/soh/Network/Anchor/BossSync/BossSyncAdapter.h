#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>

extern "C" {
#include "z64.h"
}

namespace AnchorBossSync {

// Adapter interface for a single boss/miniboss state machine.
// Implementations convert local actor transitions to canonical BOSS_* events
// and apply remote events/snapshots back into local actor state.
class BossSyncAdapter {
  public:
    virtual ~BossSyncAdapter() = default;

    virtual bool CanHandle(s16 sceneNum, s16 actorId) const = 0;

    virtual const char* Name() const = 0;

    virtual nlohmann::json CaptureTransition(PlayState* play, Actor* actor) = 0;

    virtual void ApplyEvent(PlayState* play, const nlohmann::json& payload) = 0;

    virtual nlohmann::json BuildSnapshot(PlayState* play, Actor* actor) = 0;

    virtual void ApplySnapshot(PlayState* play, const nlohmann::json& snapshot) = 0;
};

} // namespace AnchorBossSync
