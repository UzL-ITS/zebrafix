#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_ZEBRAHEAPALLOCS_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_ZEBRAHEAPALLOCS_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// Pass for rewriting calls to instrumented functions
    class ZebraHeapAllocsPass : public PassInfoMixin<ZebraHeapAllocsPass> {
    public:
        ZebraHeapAllocsPass();

        PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

        static bool isRequired() { return true; }

    private:
        Module *M;
    };

} // namespace llvm

#endif // LLVM_TRANSFORMS_INSTRUMENTATION_ZEBRAHEAPALLOCS_H