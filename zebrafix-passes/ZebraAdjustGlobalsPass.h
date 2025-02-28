#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_ZEBRAADJUSTGLOBALS_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_ZEBRAADJUSTGLOBALS_H

#include "llvm/IR/PassManager.h"
#include "llvm/Transforms/Instrumentation/ZebraTypeGenerator.h"

namespace llvm {

/// Pass for zebrafying global variables for ciphertext side-channel instrumentation
    class ZebraAdjustGlobalsPass : public PassInfoMixin<ZebraAdjustGlobalsPass> {
        Module *M;

    public:
        ZebraAdjustGlobalsPass();

        PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);

        static bool isRequired() { return true; }

        ZebraTypeGenerator *ZebraGenerator;
    };

} // namespace llvm


#endif // LLVM_TRANSFORMS_INSTRUMENTATION_ZEBRAADJUSTGLOBALS_H
