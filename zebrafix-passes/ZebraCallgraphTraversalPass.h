#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_ZEBRACALLGRAPHTRAVERSAL_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_ZEBRACALLGRAPHTRAVERSAL_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// Pass for cloning functions that need ciphertext side-channel instrumentation
/// Including traversal of the callgraph to find called functions
    class ZebraCallgraphTraversalPass : public PassInfoMixin<ZebraCallgraphTraversalPass> {
        Module *M;

    public:
        ZebraCallgraphTraversalPass();

        Function *cloneFunction(Function *F, const std::string &NameSuffix, bool IsAutoCopy);

        PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);

        static bool isRequired() { return true; }
    };

} // namespace llvm

#endif // LLVM_TRANSFORMS_INSTRUMENTATION_ZEBRACALLGRAPHTRAVERSAL_H
