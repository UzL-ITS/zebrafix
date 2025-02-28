#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_ZEBRACOUNTER_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_ZEBRACOUNTER_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// Pass for adjusting the memory layout for ciphertext side-channel instrumentation
    class ZebraCounterPass : public PassInfoMixin<ZebraCounterPass> {
        Module *M;

    public:
        ZebraCounterPass();

        PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);

        static bool isRequired() { return true; }
    };

} // namespace llvm

#endif // LLVM_TRANSFORMS_INSTRUMENTATION_ZEBRACOUNTER_H