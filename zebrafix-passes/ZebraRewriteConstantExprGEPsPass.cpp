#include "llvm/IR/Module.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/ZebraProperties.h"
#include "ZebraRewriteConstantExprGEPsPass.h"

using namespace llvm;

ZebraRewriteConstantExprGEPsPass::ZebraRewriteConstantExprGEPsPass() {}

// As global variables are always instrumented in order to ensure data correctness, the accesses also have to be
// adjusted inside of load/store operations that work with elements of global arrays/structs, the type information
// has to be updated to correctly depict zebra structs instead of integers.
// Parameters:
//      - PtrOperand: pointer operand of the load (LoadInst.GetPointerOperand() or store (StoreInst.GetOperand(1)))
//      - Addr: variable to fill with the correct address for the load/store (state keeping for the Builder results)
//      - Z: ZebraGenerator of the respective module
//      - NextInstruction: pointer to the correct next instruction for the Builder
// Returns: the correct pointer (from GEP) for the load/store
Value *RewriteConstantGEPs(Value &PtrOperand, Value *Addr, ZebraTypeGenerator Z, Instruction *NextInstruction, LLVMContext &Ctx) {
    IRBuilder<> B(NextInstruction);
    auto *ConstantGEPOperator = dyn_cast<GEPOperator>(&PtrOperand);
    SmallVector<Value *, 16> Idxs(ConstantGEPOperator->indices());
    Type *GEPInstType;
    Type *SrcElemType = ConstantGEPOperator->getSourceElementType();

    std::string str;
    llvm::raw_string_ostream output(str);
    ConstantGEPOperator->getSourceElementType()->print(output);
    bool GEPIsZebrafied = str.find("struct.zi") != str.npos;

    if (SrcElemType->isIntegerTy()) {
        GEPInstType = Z.GetZebraTypeForType(SrcElemType);
    }
    else if (SrcElemType->isArrayTy() && SrcElemType->getArrayElementType()->isIntegerTy()) {
        ArrayType *GEPZebraArrType = ArrayType::get(Z.GetZebraTypeForType(SrcElemType->getArrayElementType()), SrcElemType->getArrayNumElements());
        GEPInstType = GEPZebraArrType;
    }
    else if (SrcElemType->isArrayTy() && SrcElemType->getArrayElementType()->isArrayTy() && SrcElemType->getArrayElementType()->getArrayElementType()->isIntegerTy()) {
        Type *SubArrayElementType = SrcElemType->getArrayElementType();
        uint64_t SubArrayLength = SrcElemType->getArrayNumElements();
        StructType *ZebraStruct = Z.GetZebraTypeForType(SubArrayElementType->getArrayElementType());
        ArrayType *ZebraArraySubType = ArrayType::get(ZebraStruct, SubArrayLength);
        ArrayType *ZebraArrayType = ArrayType::get(ZebraArraySubType, SrcElemType->getArrayNumElements());
        GEPInstType = ZebraArrayType;
    }
    else if (SrcElemType->isStructTy() && !GEPIsZebrafied) {
        // Get zebrafied struct from AdjustIdentifiedStructs pass
        StructType *ZebraStructElems = StructType::getTypeByName(Ctx, SrcElemType->getStructName().str() + "__zebra");
        GEPInstType = ZebraStructElems;
    }
    else if (GEPIsZebrafied) {
        dbgs() << "[ZEBRA RwrConGEP] GEP has already been zebrafied. ";
        ConstantGEPOperator->dump();

        GEPInstType = SrcElemType;
    }
    else {
        llvm_unreachable("[ZEBRA RwrConGEP] Unsupported type for constant GEP expression rewriting!");
    }

    Addr = B.CreateGEP(GEPInstType, ConstantGEPOperator->getPointerOperand(), Idxs, "", true);

    return Addr;
}

PreservedAnalyses ZebraRewriteConstantExprGEPsPass::run(llvm::Function &F, llvm::FunctionAnalysisManager &AM) {
    M = F.getParent();
    LLVMContext &Ctx = M->getContext();
    bool HasZebraGenerator = M->hasZebraGenerator();
    bool IsCopyRewritten = false;
    bool HasZebraAttr = F.hasFnAttribute(Attribute::AttrKind::Zebra);
    if (HasZebraAttr) {
        auto ZebraState = F.getFnAttribute(Attribute::AttrKind::Zebra).getZebraProperties().getState();
        IsCopyRewritten = ZebraState == ZebraProperties::CopyRewritten;
    }

    if (!HasZebraGenerator || IsCopyRewritten)
        return PreservedAnalyses::all();

    const ZebraTypeGenerator ZebraGenerator = M->getZebraGenerator();
    std::vector<Instruction*> UninstrumentedInstructions;
    bool FoundConstantGEPExpr = false;

    // If there are functions with Zebra attribute in the module, we need to adjust constant GEP expression
    // because globals are always used in a zebrafied way for **all** functions in the module (data correctness)
    //dbgs() << "[ZEBRA RwrConGEP] Run ZebraRewriteConstantExprGEPsPass for " << F.getName() << "\n";

    for (BasicBlock *BB = &F.front(); BB != nullptr; BB = BB->getNextNode()) {
        for (Instruction *I = &BB->front(); I != nullptr; I = I->getNextNode()) {

            if (I->getOpcode() == Instruction::MemoryOps::Store) {
                StoreInst *StrInst = dyn_cast<StoreInst>(I);
                Value *StoreAddr = StrInst->getOperand(1);
                bool IsConstStrAddr = StoreAddr != nullptr && isa<GEPOperator>(StoreAddr) && isa<ConstantExpr>(StoreAddr);
                Value *StoreValue = StrInst->getOperand(0);
                bool IsConstValOpr = StoreValue != nullptr && isa<GEPOperator>(StoreValue) && isa<ConstantExpr>(StoreValue);

                if (!IsConstStrAddr && !IsConstValOpr) {
                    continue;
                }

                Instruction *NextInst = I->getNextNode();
                IRBuilder<> Builder(NextInst);
                StoreInst *StoreResult;

                // If we have instructions like
                // `store i32 123, ptr getelementptr inbounds ([2 x i32], ptr @test__zebra, i64 0, i64 1), align 4`
                // we have to make sure that we correctly adjust the source element type (`[2 x %struct.zi32]`)
                // in the access
                if (IsConstStrAddr && !IsConstValOpr) {
                    dbgs() << "[ZEBRA RwrConGEP]    Found store with constant GEP pointer operand (store address): ";
                    StrInst->dump();

                    Type *StoreType = StoreValue->getType();
                    if (!StoreType->isIntegerTy()) {
                        //dbgs() << "[ZEBRA RwrConGEP] WARNING Found store that is no integer.\n";
                        continue;
                    }

                    Value *RefinedStoreAddr = RewriteConstantGEPs(*StoreAddr, RefinedStoreAddr, ZebraGenerator, NextInst, Ctx);
                    StoreResult = Builder.CreateStore(StoreValue, RefinedStoreAddr);
                    StrInst->replaceAllUsesWith(StoreResult);
                    I = StoreResult;
                    FoundConstantGEPExpr = true;

                    // Collect instructions to remove
                    UninstrumentedInstructions.push_back(StrInst);
                }

                // If we have instructions like
                // `store ptr getelementptr inbounds (%struct.Sample, ptr @Testing__zebra, i32 0, i32 1), ptr @pointer, align 8`
                // we have to make sure that we correctly adjust the store value in the access
                else if (IsConstValOpr && !IsConstStrAddr) {
                    dbgs() << "[ZEBRA RwrConGEP]    Found store with constant GEP pointer operand (store value): ";
                    StrInst->dump();
                    Value *RefinedStoreValue = RewriteConstantGEPs(*StoreValue, RefinedStoreValue, ZebraGenerator, NextInst, Ctx);
                    StoreResult = Builder.CreateStore(RefinedStoreValue, StoreAddr);
                    StrInst->replaceAllUsesWith(StoreResult);
                    I = StoreResult;
                    FoundConstantGEPExpr = true;

                    // Collect instructions to remove
                    UninstrumentedInstructions.push_back(StrInst);
                }
                else {
                    StrInst->dump();
                    llvm_unreachable("[ZEBRA RwrConGEP] WARNING Check expr with const store addr and store value.\n");
                }
            }

            if (I->getOpcode() == Instruction::MemoryOps::Load) {
                LoadInst *LdInst = dyn_cast<LoadInst>(I);
                Value *PtrOperand = LdInst->getPointerOperand();
                if (!(PtrOperand != nullptr && isa<GEPOperator>(PtrOperand) && isa<ConstantExpr>(PtrOperand))) {
                    continue;
                }
                // If we have instructions like
                // `%4 = load i32, ptr getelementptr inbounds ([2 x i32], ptr @test__zebra, i64 0, i64 1), align 4`
                // we have to make sure that we correctly adjust the source element type (`[2 x %struct.zi32]`)
                // in the access
                dbgs() << "[ZEBRA RwrConGEP]    Found load with constant GEP pointer operand: ";
                LdInst->dump();
                Instruction *NextInst = LdInst->getNextNode();
                IRBuilder<> Builder(NextInst);
                // Load instruction has one parameter: memory address from which to load
                // Type that is to be loaded is specified, e.g. %val = load i32, ptr %ptr
                Type *LoadType = LdInst->getType();
                if (!LoadType->isIntegerTy()) {
                    //dbgs() << "[ZEBRA RwrConGEP] WARNING Found load that is no integer.\n";
                    continue;
                }

                Value *GEPLoadAddr = RewriteConstantGEPs(*PtrOperand, GEPLoadAddr, ZebraGenerator, NextInst, Ctx);
                LoadInst *ZebraLoadResult = Builder.CreateLoad(LoadType, GEPLoadAddr);
                LdInst->replaceAllUsesWith(ZebraLoadResult);
                I = ZebraLoadResult;
                FoundConstantGEPExpr = true;

                // Collect instructions to remove
                UninstrumentedInstructions.push_back(LdInst);
            }
        }
    }
    for (auto *Inst : UninstrumentedInstructions) {
        Inst->eraseFromParent();
    }

    return FoundConstantGEPExpr ? PreservedAnalyses::none() : PreservedAnalyses::all(); // TODO: refine none
}