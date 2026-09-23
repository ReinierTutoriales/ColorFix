#pragma once

namespace colorfix::native {

class HookEngine {
public:
    virtual ~HookEngine() = default;
    virtual bool Initialize() = 0;
    virtual bool BeginBatch() = 0;
    virtual bool AddHook(void* target, void* detour, void** original) = 0;
    virtual bool CommitBatch() = 0;
    virtual void Shutdown() = 0;
};

} // namespace colorfix::native
