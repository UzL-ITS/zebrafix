#include "ZebraHeapAllocsPass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/ZebraProperties.h"

using namespace llvm;

ZebraHeapAllocsPass::ZebraHeapAllocsPass() { }

PreservedAnalyses ZebraHeapAllocsPass::run(llvm::Function &F, llvm::FunctionAnalysisManager &AM) {
    M = F.getParent();

    // Instrument heap allocations in all functions if not already instrumented
    if (F.hasFnAttribute(Attribute::AttrKind::Zebra)) {
        const Attribute &FZAttr = F.getFnAttribute(Attribute::AttrKind::Zebra);
        ZebraProperties::State FZState = FZAttr.getZebraProperties().getState();
        if (FZState == ZebraProperties::CopyRewritten)
            return PreservedAnalyses::all();
    }

    std::vector<Instruction*> UninstrumentedInstructions;

    // Handle all relevant instructions
    for (BasicBlock *BB = &F.front(); BB != nullptr; BB = BB->getNextNode()) {
        for (Instruction *I = &BB->front(); I != nullptr; I = I->getNextNode()) {

            // Find malloc calls in order to allocate enough space for later zebrafying the stored objects
            if (I->getOpcode() != Instruction::Call)
                continue;

            auto *CllInst = dyn_cast<CallInst>(I);
            Function *Callee = CllInst->getCalledFunction();
            std::string FuncName;
            if (Callee)
                FuncName = Callee->getName().str();
            else {
                //dbgs() << "[ZEBRA HeapAlloc] Callee is null in " << F.getName() << ": ";
                //CllInst->dump();
                continue;
            }

            // Fixme: refine this check
            bool IsMalloc = FuncName.find("malloc") != FuncName.npos;
            bool IsCalloc = FuncName.find("calloc") != FuncName.npos;
            if (!(IsMalloc || IsCalloc))
                continue;

            Instruction *NextInst = CllInst->getNextNode();
            IRBuilder<> Builder(NextInst);

            if (IsMalloc) {
                Value *AllocSize = CllInst->getOperand(0);
                // Worst case calculation of needed zebra space: malloc for bytes
                // Each byte is put into a block of size 16 bytes, so we need 15 additional bytes per byte
                Value *Multiplier = ConstantInt::get(Builder.getInt64Ty(), 16);
                Value *ExtendedAllocSize = Builder.CreateMul(AllocSize, Multiplier);
                //Value *ArraySize = ConstantInt::get(Type::getInt64Ty(M->getContext()), 1);
                //CallInst *ZebraMalloc = Builder.CreateMalloc(AllocSize->getType(), CllInst->getType(), ExtendedAllocSize, ArraySize, Callee);
                Value *Args[] = {ExtendedAllocSize};
                CallInst *ZebraMalloc = Builder.CreateCall(Callee, Args, "malloc__zebra");
                CllInst->replaceAllUsesWith(ZebraMalloc);
                I = ZebraMalloc;
                dbgs() << "[ZEBRA HeapAlloc] Replaced heap allocation (malloc) in " << F.getName() << ": ";
                ZebraMalloc->dump();
            }
            else { // calloc
                Value *AllocElemCt = CllInst->getOperand(0);
                // Worst case calculation of needed zebra space: calloc for 16-byte block
                Value *ExtendedAllocSize = ConstantInt::get(Builder.getInt64Ty(), 16);
                Value *Args[] = {AllocElemCt, ExtendedAllocSize};
                CallInst *ZebraCalloc = Builder.CreateCall(Callee, Args, "calloc__zebra");
                CllInst->replaceAllUsesWith(ZebraCalloc);
                I = ZebraCalloc;
                dbgs() << "[ZEBRA HeapAlloc] Replaced heap allocation (calloc) in " << F.getName() << ": ";
                ZebraCalloc->dump();
            }

            // Collect instructions to remove
            UninstrumentedInstructions.push_back(CllInst);
        }
    }

    return PreservedAnalyses::none(); // Fixme: refine none
}