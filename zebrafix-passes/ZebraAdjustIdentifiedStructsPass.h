#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_ZEBRAADJUSTIDENTIFIEDSTRUCTS_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_ZEBRAADJUSTIDENTIFIEDSTRUCTS_H

#include "llvm/IR/PassManager.h"
#include "llvm/Transforms/Instrumentation/ZebraTypeGenerator.h"

namespace llvm {

/// Pass for zebrafying identified structs for ciphertext side-channel instrumentation
    class ZebraAdjustIdentifiedStructsPass : public PassInfoMixin<ZebraAdjustIdentifiedStructsPass> {
        Module *M;

    public:
        ZebraAdjustIdentifiedStructsPass();

        PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);

        static bool isRequired() { return true; }

        ZebraTypeGenerator *ZebraGenerator;
    };

} // namespace llvm


#endif // LLVM_TRANSFORMS_INSTRUMENTATION_ZEBRAADJUSTIDENTIFIEDSTRUCTS_H
