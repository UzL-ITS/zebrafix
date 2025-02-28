#include "ZebraCounterPass.h"
#include "llvm/Support/ZebraProperties.h"
#include "llvm/IR/IRBuilder.h"
#include <immintrin.h>
#include "llvm/IR/InlineAsm.h"

using namespace llvm;

ZebraCounterPass::ZebraCounterPass() {}

PreservedAnalyses ZebraCounterPass::run(Module &Mod, ModuleAnalysisManager &AM) {
    M = &Mod;
    bool FoundZebra = false;
    Function *MainFunc;

    // Go through all functions in the module to check whether there is one that needs Zebra instrumentation
    auto &FunctionList = M->getFunctionList();
    for (auto &F : FunctionList) {

        if (F.hasFnAttribute(Attribute::AttrKind::Zebra)) {
            FoundZebra = true;

            const Attribute &FZAttr = F.getFnAttribute(Attribute::AttrKind::Zebra);
            const ZebraProperties &ZP = FZAttr.getZebraProperties();
            if (ZP.getState() == ZebraProperties::CopyRewritten) {
                return PreservedAnalyses::all();
            }
        }

        if (F.getName() == "main") {
            MainFunc = &F;
        }
    }

    if (MainFunc != nullptr) {
        IRBuilder<> B(&MainFunc->front().front());
        InlineAsm *PaddqAsm = InlineAsm::get(FunctionType::get(B.getVoidTy(), {}, false),
                                             // Use the following line for debugging only (constant counter)
                                             //"movabsq $$0xc0ffee11, %rax\n"
                                             "rdrand %rax\n"
                                             "vpinsrq $$1, %rax, %xmm15, %xmm15\n"
                                             "movabsq $$1, %rax\n"
                                             "vpinsrq $$1, %rax, %xmm14, %xmm14",
                                             "~{rax},~{xmm14},~{xmm15},~{dirflag},~{fpsr},~{flags}",  // Clobbers
                                             false);       // Side effect
        B.CreateCall(PaddqAsm, {});
        dbgs() << "[ZEBRA   Counter] Inserted register-based counter.\n";
    }

    if (FoundZebra) {
        // This is still needed for initializing global variables although the counter is then replaced by the in-register version
        IRBuilder<> Builder(M->getContext());
        std::string ZebraCounterString = "ZebraCounter_" + M->getName().str();
        M->getOrInsertGlobal(ZebraCounterString, Builder.getInt64Ty());
        GlobalVariable *ZebraCounter = M->getNamedGlobal(ZebraCounterString);
        ZebraCounter->setAlignment(Align(8));
        unsigned int SeedVal = 0xc0ffee11;
        unsigned int* Seed = &SeedVal;
        ZebraCounter->setInitializer(Builder.getInt64(rand_r(Seed)));
        //ZebraCounter->setInitializer(Builder.getInt64(0xc0ffee11));
        //ZebraCounter->setInitializer(Builder.getInt64(0));
        ZebraCounter->setConstant(false);

        dbgs() << "[ZEBRA   Counter] Inserted global counter.\n";

        return PreservedAnalyses::none(); // Fixme: refine none
    }

    return PreservedAnalyses::all();
}