#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_ZEBRAADJUSTMEMORY_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_ZEBRAADJUSTMEMORY_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// Pass for adjusting the memory layout for ciphertext side-channel instrumentation
    class ZebraAdjustMemoryPass : public PassInfoMixin<ZebraAdjustMemoryPass> {
    public:
        ZebraAdjustMemoryPass();

        PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

        static bool isRequired() { return true; }

    private:
        Module *M;
    };

} // namespace llvm

#endif // LLVM_TRANSFORMS_INSTRUMENTATION_ZEBRAADJUSTMEMORY_H
