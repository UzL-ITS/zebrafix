#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/ZebraProperties.h"
#include "ZebraAdjustMemoryPass.h"
#include "llvm/Transforms/Instrumentation/ZebraTypeGenerator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicsX86.h"
#include "llvm/IR/InlineAsm.h"

using namespace llvm;

ZebraAdjustMemoryPass::ZebraAdjustMemoryPass() {}

Type *AllocateZebraTypes(Module &M, Type *AllocType) {
    IRBuilder<> Builder(M.getContext());

    if (AllocType->isIntegerTy()) {

        return M.getZebraGenerator().GetZebraTypeForType(AllocType);
    }
    else if (AllocType->isPointerTy()) {

        return AllocType;
    }
    else if (AllocType->isArrayTy()) {
        // We replace %a = alloca [2 x i32], align 4 with
        // %a = alloca [2 x %struct.zi32], align 16 (and later adjust GEPs)
        return ArrayType::get(AllocateZebraTypes(M, AllocType->getArrayElementType()), AllocType->getArrayNumElements());
    }
    else if (AllocType->isStructTy()) {
        // Get zebrafied struct from AdjustIdentifiedStructs pass
        StructType *ZebraStructType = StructType::getTypeByName(M.getContext(), AllocType->getStructName().str() + "__zebra");

        if (ZebraStructType == nullptr) {
            dbgs() << "[ZEBRA AdjustMem] Trying to build allocation for struct type ";
            AllocType->dump();
            llvm_unreachable("[ZEBRA AdjustMem] Cannot determine type for struct.");
        }

        return ZebraStructType;
    }
    else {
        dbgs() << "[ZEBRA AdjustMem] Trying to build allocation for type ";
        AllocType->dump();
        llvm_unreachable("[ZEBRA AdjustMem] Found no zebra struct for allocating suitable input type.");
    }
}

Type *GetGEPElemType(Type *ElemTy, Module &M) {
    ZebraTypeGenerator ZebraGenerator = M.getZebraGenerator();

    if (ElemTy->isIntegerTy()) {

        return ZebraGenerator.GetZebraTypeForType(ElemTy);
    }
    else if (ElemTy->isArrayTy()) {
        uint64_t ArrayLength = ElemTy->getArrayNumElements();
        Type *ArrayElemType = ElemTy->getArrayElementType();

        return ArrayType::get(GetGEPElemType(ArrayElemType, M), ArrayLength);
    }
    else if (ElemTy->isStructTy()) {
        auto *ZebraStructType = dyn_cast<StructType>(ElemTy);
        if (ZebraStructType == nullptr) {
            dbgs() << "[ZEBRA AdjustMem] Trying to find element type of struct ";
            ElemTy->dump();
            llvm_unreachable("[ZEBRA AdjustMem] GEP initialization failed because no struct type was found.");
        }

        if (ZebraStructType->isLiteral()) {
            dbgs() << "[ZEBRA AdjustMem] Trying to find element type of struct ";
            ElemTy->dump();
            dbgs() << "[ZEBRA AdjustMem] Found literal struct ";
            ZebraStructType->dump();
            return StructType::get(GetGEPElemType(ZebraStructType->getElementType(0), M));
        }

        if (ElemTy->getStructName().contains("struct.zi")) {
            return ZebraStructType;
        }

        // Get zebrafied struct from AdjustIdentifiedStructs pass
        StructType *IdentifiedZebraStructType = StructType::getTypeByName(M.getContext(), ElemTy->getStructName().str() + "__zebra");
        if (IdentifiedZebraStructType != nullptr)
            return IdentifiedZebraStructType;

        std::vector<Type*> Elems;
        uint64_t StructNumElem = ElemTy->getStructNumElements();
        for (uint i = 0; i < StructNumElem; ++i) {
            Elems.push_back(GetGEPElemType(ElemTy->getStructElementType(i), M));
        }

        return StructType::get(M.getContext(), Elems);
    }
    else if (ElemTy->isPointerTy()) {
        dbgs() << "[ZEBRA AdjustMem] No handling needed for pointer type.\n";

        return PointerType::get(M.getContext(), 0);
    }
    else {
        dbgs() << "[ZEBRA AdjustMem] Cannot find zebra struct type: ";
        ElemTy->dump();
        llvm_unreachable("[ZEBRA AdjustMem] GEP initialization failed.");
    }
}

PreservedAnalyses ZebraAdjustMemoryPass::run(llvm::Function &F, llvm::FunctionAnalysisManager &AM) {

    // If this function is in suitable state, instrument memory accesses so that they are interleaved with a counter
    // and add necessary logic for allocations and GEPs
    if (!F.hasFnAttribute(Attribute::AttrKind::Zebra))
        return PreservedAnalyses::all();

    M = F.getParent();

    const Attribute &FZAttr = F.getFnAttribute(Attribute::AttrKind::Zebra);
    const ZebraProperties &ZP = FZAttr.getZebraProperties();

    // If we are not in a function (auto) copy, we do not instrument
    bool IsFunctionCopy = ZP.getState() == ZebraProperties::Copy || ZP.getState() == ZebraProperties::AutoCopy;
    if (!IsFunctionCopy)
        return PreservedAnalyses::all();

    dbgs() << "[ZEBRA AdjustMem] Run ZebraAdjustMemoryPass for " << F.getName() << "\n";

    std::vector<Instruction*> UninstrumentedInstructions;
    const ZebraTypeGenerator ZebraGenerator = M->getZebraGenerator();

    Type *Int64Type = Type::getInt64Ty(M->getContext());
    uint64_t DataIdx = 0;
    uint64_t CtrIdx = 1;
    VectorType *ZebraVecType = VectorType::get(Int64Type, 2, false);

    for (BasicBlock *BB = &F.front(); BB != nullptr; BB = BB->getNextNode()) {
        for (Instruction *I = &BB->front(); I != nullptr; I = I->getNextNode()) {

            if (I->getOpcode() == Instruction::MemoryOps::Alloca) {
                // Instruction *AllocInst = I;
                // Avoiding the cast in the next line is not a good idea as this will negatively affect the getter for allocation types
                auto *AllocInst = dyn_cast<AllocaInst>(I);
                // Exchanging: Type *AllocaType = AllocInst->getOperand(0)->getType();
                // with the next lines gives the correct type instead of the first struct element
                Type *AllocaType = AllocInst->getAllocatedType();

                Type *ZebraAlloc = AllocateZebraTypes(*M, AllocaType);

                Instruction *NextInst = AllocInst->getNextNode();
                IRBuilder<> Builder(NextInst);
                AllocaInst *AllocResult = Builder.CreateAlloca(ZebraAlloc, nullptr, AllocInst->getName());

                if (!AllocaType->isPointerTy())
                    AllocResult->setAlignment(Align(16));

                AllocInst->replaceAllUsesWith(AllocResult);
                I = AllocResult;

                // Collect instructions to remove
                UninstrumentedInstructions.push_back(AllocInst);
            }

            if (I->getOpcode() == Instruction::GetElementPtr) {
                auto *GEPInst = dyn_cast<GetElementPtrInst>(I);
                Type *SrcElemType = GEPInst->getSourceElementType();
                Type *ResElemType = GEPInst->getResultElementType();

                // Replace an integer type with the suitable zebra integer type etc.
                GEPInst->setSourceElementType(GetGEPElemType(SrcElemType, *M));
                GEPInst->setResultElementType(GetGEPElemType(ResElemType, *M));
            }

            if (I->getOpcode() == Instruction::MemoryOps::Store) {
                //Instruction *StrInst = I;
                auto *StrInst = dyn_cast<StoreInst>(I);
                Instruction *NextInst = I->getNextNode();
                IRBuilder<> Builder(NextInst);

                Value *StoreValue = StrInst->getOperand(0);
                Value *StoreAddr = StrInst->getOperand(1);
                Type *StoreType = StoreValue->getType();

                if (!StoreType->isIntegerTy()) {
                    //dbgs() << "[ZEBRA AdjustMem]    Non-integer store WARNING!: ";
                    //StrInst->dump();
                    continue;
                }

                InlineAsm *PaddqAsm = InlineAsm::get(FunctionType::get(ZebraVecType, {}, false),
                                                     "vpaddq %xmm14, %xmm15, %xmm15\nvmovdqa %xmm15, $0",
                                                     "=x,~{xmm15},~{dirflag},~{fpsr},~{flags}",  // Clobbers
                                                     false);       // Side effect
                CallInst *InlnAsm = Builder.CreateCall(PaddqAsm, {});
                Value *StoreValueZExt = Builder.CreateZExt(StoreValue, Int64Type);
                Value *ZebrafiedValue = Builder.CreateInsertElement(InlnAsm, StoreValueZExt, DataIdx);
                StoreInst *ZebraStoreResult = Builder.CreateStore(ZebrafiedValue, StoreAddr);
                StrInst->replaceAllUsesWith(ZebraStoreResult);
                I = ZebraStoreResult;

                // Collect instructions to remove
                UninstrumentedInstructions.push_back(StrInst);
            }

            if (I->getOpcode() == Instruction::MemoryOps::Load) {
                auto *LdInst = dyn_cast<LoadInst>(I);
                Instruction *NextInst = LdInst->getNextNode();
                IRBuilder<> Builder(NextInst);
                // Load instruction has one parameter: memory address from which to load
                // Type that is to be loaded is specified, e.g. %val = load i32, ptr %ptr
                Value *LoadAddr = LdInst->getOperand(0);
                Type *LoadType = LdInst->getType();

                if (!LoadType->isIntegerTy()) {
                    //dbgs() << "[ZEBRA AdjustMem]    Non-integer load WARNING!: ";
                    //LdInst->dump();
                    continue;
                }

                Value *GEPLoadAddr = Builder.CreateStructGEP(ZebraGenerator.GetZebraTypeForType(LoadType), LoadAddr, 0);

                LoadInst *ZebraLoadResult = Builder.CreateLoad(LoadType, GEPLoadAddr);
                LdInst->replaceAllUsesWith(ZebraLoadResult);
                I = ZebraLoadResult;

                // Collect instructions to remove
                UninstrumentedInstructions.push_back(LdInst);
            }
        }
    }

    // Once we have rewritten this function to use interleaving, we update the function state correspondingly
    F.addFnAttr(Attribute::getWithZebraProperties(F.getContext(), ZebraProperties(ZebraProperties::State::CopyRewritten)));

    for (auto *Inst : UninstrumentedInstructions) {
        Inst->eraseFromParent();
    }

    return PreservedAnalyses::none(); // Fixme: refine none
}