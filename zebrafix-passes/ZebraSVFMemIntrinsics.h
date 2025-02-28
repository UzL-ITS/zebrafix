#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_ZEBRASVFMEMINTRINSICS_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_ZEBRASVFMEMINTRINSICS_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// Pass for adjusting memcpy and memset intrinsics
    class ZebraSVFMemIntrinsicsPass : public PassInfoMixin<ZebraSVFMemIntrinsicsPass> {
        Module *M;
    public:
        ZebraSVFMemIntrinsicsPass();

        PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);

        static bool isRequired() { return true; }
    };

} // namespace llvm

#endif // LLVM_TRANSFORMS_INSTRUMENTATION_ZEBRASVFMEMINTRINSICS_H
