#include "ZebraRewriteCallsPass.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/ZebraProperties.h"

using namespace llvm;

ZebraRewriteCallsPass::ZebraRewriteCallsPass() { }

PreservedAnalyses ZebraRewriteCallsPass::run(llvm::Function &F, llvm::FunctionAnalysisManager &AM) {
    M = F.getParent();

    // Rewrite all functions that call instrumented functions.
    // Everything else is taken care of in the call graph traversal pass.
    if (F.hasFnAttribute(Attribute::AttrKind::Zebra)) {
        return PreservedAnalyses::all();
    }

    // Handle all relevant instructions
    for (BasicBlock *BB = &F.front(); BB != nullptr; BB = BB->getNextNode()) {
        for (Instruction *I = &BB->front(); I != nullptr; I = I->getNextNode()) {

            CallInst *CI = dyn_cast<CallInst>(I);
            if (CI == nullptr)
                continue;

            Function *Callee = CI->getCalledFunction();
            if (Callee == nullptr)
                continue;

            // Is the called function annotated for protection?
            // If not, no change needed
            if (!Callee->hasFnAttribute(Attribute::AttrKind::Zebra))
                continue;

            const Attribute &CalleeZAttr = Callee->getFnAttribute(Attribute::AttrKind::Zebra);
            ZebraProperties::State CalleeState = CalleeZAttr.getZebraProperties().getState();

            assert(CalleeState != ZebraProperties::AutoCopy && "Callee has impossible state attribute value");

            // If we already call a transformed function (i.e., the name was rewritten), we don't need to touch this call.
            if (CalleeState == ZebraProperties::Copy || CalleeState == ZebraProperties::CopyRewritten)
                continue;

            dbgs() << "[ZEBRA RewrCalls] Editing call " << F.getName() << " -> " << Callee->getName() << "\n";

            // If this is a call within the same compile unit, rewrite the name of the called function.
            // For everything else, we rely on the user using the right name at the moment.
            // TODO: make this more convenient and robust
            if (CalleeState != ZebraProperties::State::Extern) {
                std::string TransformedCalleeName = Callee->getName().str() + "__zebra";

                Function *TransformedCallee = M->getFunction(TransformedCalleeName);
                if (TransformedCallee == nullptr)
                    llvm_unreachable("Cannot find transformed equivalent");

                CI->setCalledFunction(TransformedCallee);
            }
        }
    }

    return PreservedAnalyses::none(); // TODO: refine none
}

raw_ostream &llvm::operator<<(raw_ostream &OS, const ZebraProperties &ZP) {
    OS << "State: " << ZebraProperties::getStateString(ZP.getState());
    return OS;
}
