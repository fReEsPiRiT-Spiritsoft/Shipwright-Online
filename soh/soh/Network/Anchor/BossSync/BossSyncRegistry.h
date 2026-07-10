#pragma once

#include <memory>
#include <vector>
#include "soh/Network/Anchor/BossSync/BossSyncAdapter.h"

namespace AnchorBossSync {

void RegisterDefaultBossSyncAdapters();

const std::vector<std::shared_ptr<BossSyncAdapter>>& GetBossSyncAdapters();

std::shared_ptr<BossSyncAdapter> FindBossSyncAdapter(s16 sceneNum, s16 actorId);

// Calls Reset() on all registered adapters. Called from OnSceneInit so
// per-adapter tracking maps are cleared on every scene transition.
void ResetAllBossSyncAdapters();

} // namespace AnchorBossSync
