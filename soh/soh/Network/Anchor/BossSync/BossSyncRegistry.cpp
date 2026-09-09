#include "soh/Network/Anchor/BossSync/BossSyncRegistry.h"
#include <mutex>

namespace AnchorBossSync {

std::shared_ptr<BossSyncAdapter> CreateBarinadeBossAdapter();
std::shared_ptr<BossSyncAdapter> CreateMorphaBossAdapter();
std::shared_ptr<BossSyncAdapter> CreateBongoBongoAdapter();
std::shared_ptr<BossSyncAdapter> CreateVolvagiaAdapter();
std::shared_ptr<BossSyncAdapter> CreateTwinrovaAdapter();
std::shared_ptr<BossSyncAdapter> CreateGanon2Adapter();
std::shared_ptr<BossSyncAdapter> CreateKingDodongoAdapter();
std::shared_ptr<BossSyncAdapter> CreatePhantomGanonAdapter();
std::shared_ptr<BossSyncAdapter> CreateGenericBossHealthPhaseAdapter();
std::shared_ptr<BossSyncAdapter> CreateBigOctoMinibossAdapter();

namespace {
std::vector<std::shared_ptr<BossSyncAdapter>> gBossSyncAdapters;
std::once_flag gRegisterOnce;
}

void RegisterDefaultBossSyncAdapters() {
    std::call_once(gRegisterOnce, []() {
        // Register specific adapters before the generic fallback so they win
        // dispatch for overlapping actor ids.
        gBossSyncAdapters.push_back(CreateBarinadeBossAdapter());
        gBossSyncAdapters.push_back(CreateMorphaBossAdapter());
        gBossSyncAdapters.push_back(CreateBongoBongoAdapter());
        gBossSyncAdapters.push_back(CreateVolvagiaAdapter());
        gBossSyncAdapters.push_back(CreateTwinrovaAdapter());
        gBossSyncAdapters.push_back(CreateGanon2Adapter());
        gBossSyncAdapters.push_back(CreateKingDodongoAdapter());
        gBossSyncAdapters.push_back(CreatePhantomGanonAdapter());
        gBossSyncAdapters.push_back(CreateBigOctoMinibossAdapter());
        gBossSyncAdapters.push_back(CreateGenericBossHealthPhaseAdapter());
    });
}

const std::vector<std::shared_ptr<BossSyncAdapter>>& GetBossSyncAdapters() {
    RegisterDefaultBossSyncAdapters();
    return gBossSyncAdapters;
}

std::shared_ptr<BossSyncAdapter> FindBossSyncAdapter(s16 sceneNum, s16 actorId) {
    RegisterDefaultBossSyncAdapters();
    for (const auto& adapter : gBossSyncAdapters) {
        if (adapter && adapter->CanHandle(sceneNum, actorId)) {
            return adapter;
        }
    }
    return nullptr;
}

void ResetAllBossSyncAdapters() {
    RegisterDefaultBossSyncAdapters();
    for (const auto& adapter : gBossSyncAdapters) {
        if (adapter) {
            adapter->Reset();
        }
    }
}

} // namespace AnchorBossSync
