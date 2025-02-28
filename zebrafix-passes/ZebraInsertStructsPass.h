#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_ZEBRAINSERTSTRUCTS_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_ZEBRAINSERTSTRUCTS_H

#include "llvm/IR/PassManager.h"
#include "llvm/Transforms/Instrumentation/ZebraTypeGenerator.h"

namespace llvm {

/// Pass for inserting structs into the memory layout for ciphertext side-channel instrumentation
    class ZebraInsertStructsPass : public PassInfoMixin<ZebraInsertStructsPass> {
        Module *M;

    public:
        ZebraInsertStructsPass();

        PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);

        static bool isRequired() { return true; }

        ZebraTypeGenerator *ZebraGenerator;
    };

} // namespace llvm

#endif // LLVM_TRANSFORMS_INSTRUMENTATION_ZEBRAINSERTSTRUCTS_H