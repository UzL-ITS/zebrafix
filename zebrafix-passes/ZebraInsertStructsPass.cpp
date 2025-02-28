#include "ZebraInsertStructsPass.h"
#include "llvm/Support/ZebraProperties.h"
#include "llvm/IR/IRBuilder.h"

using namespace llvm;

ZebraInsertStructsPass::ZebraInsertStructsPass() { }

PreservedAnalyses ZebraInsertStructsPass::run(Module &Mod, ModuleAnalysisManager &AM) {
    M = &Mod;
    bool FoundZebra = false;
    bool FoundCopyRewritten = false;

    // Go through all functions in the module to check whether there is one that needs Zebra instrumentation
    // If we need instrumentation, we need to check what Zebra structs we have to add
    auto &FunctionList = M->getFunctionList();
    for (auto &F : FunctionList) {

        if (F.hasFnAttribute(Attribute::AttrKind::Zebra)) {
            FoundZebra = true;

            const Attribute &FZAttr = F.getFnAttribute(Attribute::AttrKind::Zebra);
            const ZebraProperties &ZP = FZAttr.getZebraProperties();
            if (ZP.getState() == ZebraProperties::CopyRewritten) {
                FoundCopyRewritten = true;
            }
        }
    }

    if (FoundZebra && !FoundCopyRewritten) {
        // Initialize the zebra generator in the module to generate all needed structs
        // TODO: get all needed types for the module/function and add the struct automagically
        M->setZebraGenerator();
        M->setHasZebraGenerator();
        dbgs() << "[ZEBRA InsStruct] Created ZebraTypeGenerator.\n";
    }

    return PreservedAnalyses::none();
}