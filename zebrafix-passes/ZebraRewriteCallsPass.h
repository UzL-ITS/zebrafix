#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_ZEBRAREWRITECALLS_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_ZEBRAREWRITECALLS_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// Pass for rewriting calls to instrumented functions
    class ZebraRewriteCallsPass : public PassInfoMixin<ZebraRewriteCallsPass> {
    public:
        ZebraRewriteCallsPass();

        PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

        static bool isRequired() { return true; }

    private:
        Module *M;
    };

} // namespace llvm

#endif // LLVM_TRANSFORMS_INSTRUMENTATION_ZEBRAREWRITECALLS_H
