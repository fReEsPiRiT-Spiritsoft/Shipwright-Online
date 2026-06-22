#pragma once

#include <memory>
#include <vector>
#include "soh/Network/Anchor/BossSync/BossSyncAdapter.h"

namespace AnchorBossSync {

void RegisterDefaultBossSyncAdapters();

const std::vector<std::shared_ptr<BossSyncAdapter>>& GetBossSyncAdapters();

std::shared_ptr<BossSyncAdapter> FindBossSyncAdapter(s16 sceneNum, s16 actorId);

} // namespace AnchorBossSync
