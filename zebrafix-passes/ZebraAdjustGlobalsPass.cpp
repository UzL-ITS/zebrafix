#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ZebraProperties.h"
#include "ZebraAdjustGlobalsPass.h"

using namespace llvm;

ZebraAdjustGlobalsPass::ZebraAdjustGlobalsPass() { }

Constant *FillZebraStructs(Constant& OrigVal, Constant& CounterValue, StructType& ZebraStruct) {
    if (!ZebraStruct.isStructTy()) {
        llvm_unreachable("[ZEBRA AdjustGlo] Error! This should not happen.");
    }
    switch (ZebraStruct.getNumElements()) {
        case 2: { // i64
            Constant *Args[] = {&OrigVal, &CounterValue};
            return ConstantStruct::get(&ZebraStruct, Args);
        }
        case 3: { // i32
            Constant *Args[] = {&OrigVal,
                                Constant::getIntegerValue(ZebraStruct.getTypeAtIndex(1), APInt(ZebraStruct.getTypeAtIndex(1)->getIntegerBitWidth(), 0)),
                                &CounterValue};
            return ConstantStruct::get(&ZebraStruct, Args);
        }
        case 4: { // i16
            Constant *Args[] = {&OrigVal,
                                Constant::getIntegerValue(ZebraStruct.getTypeAtIndex(1), APInt(ZebraStruct.getTypeAtIndex(1)->getIntegerBitWidth(), 0)),
                                Constant::getIntegerValue(ZebraStruct.getTypeAtIndex(2), APInt(ZebraStruct.getTypeAtIndex(2)->getIntegerBitWidth(), 0)),
                                &CounterValue};
            return ConstantStruct::get(&ZebraStruct, Args);
        }
        case 5: { // i8
            Constant *Args[] = {&OrigVal,
                                Constant::getIntegerValue(ZebraStruct.getTypeAtIndex(1), APInt(ZebraStruct.getTypeAtIndex(1)->getIntegerBitWidth(), 0)),
                                Constant::getIntegerValue(ZebraStruct.getTypeAtIndex(2), APInt(ZebraStruct.getTypeAtIndex(2)->getIntegerBitWidth(), 0)),
                                Constant::getIntegerValue(ZebraStruct.getTypeAtIndex(3), APInt(ZebraStruct.getTypeAtIndex(3)->getIntegerBitWidth(), 0)),
                                &CounterValue};
            return ConstantStruct::get(&ZebraStruct, Args);
        }
        case 12: { // i1
            Constant *Args[] = {&OrigVal,
                                Constant::getIntegerValue(ZebraStruct.getTypeAtIndex(1), APInt(ZebraStruct.getTypeAtIndex(1)->getIntegerBitWidth(), 0)),
                                Constant::getIntegerValue(ZebraStruct.getTypeAtIndex(2), APInt(ZebraStruct.getTypeAtIndex(2)->getIntegerBitWidth(), 0)),
                                Constant::getIntegerValue(ZebraStruct.getTypeAtIndex(3), APInt(ZebraStruct.getTypeAtIndex(3)->getIntegerBitWidth(), 0)),
                                Constant::getIntegerValue(ZebraStruct.getTypeAtIndex(4), APInt(ZebraStruct.getTypeAtIndex(4)->getIntegerBitWidth(), 0)),
                                Constant::getIntegerValue(ZebraStruct.getTypeAtIndex(5), APInt(ZebraStruct.getTypeAtIndex(5)->getIntegerBitWidth(), 0)),
                                Constant::getIntegerValue(ZebraStruct.getTypeAtIndex(6), APInt(ZebraStruct.getTypeAtIndex(6)->getIntegerBitWidth(), 0)),
                                Constant::getIntegerValue(ZebraStruct.getTypeAtIndex(7), APInt(ZebraStruct.getTypeAtIndex(7)->getIntegerBitWidth(), 0)),
                                Constant::getIntegerValue(ZebraStruct.getTypeAtIndex(8), APInt(ZebraStruct.getTypeAtIndex(8)->getIntegerBitWidth(), 0)),
                                Constant::getIntegerValue(ZebraStruct.getTypeAtIndex(9), APInt(ZebraStruct.getTypeAtIndex(9)->getIntegerBitWidth(), 0)),
                                Constant::getIntegerValue(ZebraStruct.getTypeAtIndex(10), APInt(ZebraStruct.getTypeAtIndex(10)->getIntegerBitWidth(), 0)),
                                &CounterValue};
            return ConstantStruct::get(&ZebraStruct, Args);
        }
        default:
            llvm_unreachable("[ZEBRA AdjustGlo] No suitable zebra type available!");
    }
}

Constant *GetGlobalInitValue(Module &M, Type *ValType, Value* Val, Constant &CounterValue, Type &ZebraStruct) {

    if (ValType->isIntegerTy()) {
        StructType *ZebraType = M.getZebraGenerator().GetZebraTypeForType(ValType);
        return FillZebraStructs(*dyn_cast<Constant>(Val), CounterValue, *ZebraType);
    }
    else if (ValType->isArrayTy()) {
        IRBuilder<> Builder(M.getContext());
        std::vector<Constant*> ZebraStructValues;
        for (uint i = 0; i < ValType->getArrayNumElements(); ++i) {
            Value *ArrayElem = Builder.CreateExtractValue(Val, {i});
            ZebraStructValues.push_back(GetGlobalInitValue(M, ValType->getArrayElementType(), ArrayElem, CounterValue, *ZebraStruct.getArrayElementType()));
        }

        return ConstantArray::get(ArrayType::get(ZebraStruct.getArrayElementType(), ZebraStruct.getArrayNumElements()), ZebraStructValues);
    }
    else if (ValType->isStructTy()) {
        std::vector<Constant*> ZebraStructValues;
        std::vector<Type*> StructElemTypes;
        IRBuilder<> Builder(M.getContext());
        for (uint i = 0; i < ValType->getStructNumElements(); ++i) {
            Value *StructElem = Builder.CreateExtractValue(Val, {i});
            ZebraStructValues.push_back(GetGlobalInitValue(M, ValType->getStructElementType(i), StructElem, CounterValue, *ZebraStruct.getStructElementType(i)));
            StructElemTypes.push_back(ZebraStruct.getStructElementType(i));
        }

        return ConstantStruct::get(StructType::get(M.getContext(), StructElemTypes), ZebraStructValues);
    }
    else if (ValType->isPointerTy()) {
        return (Constant*) Val;
    }
    else {
        dbgs() << "[ZEBRA AdjustGlo] Did not find suitable init value for ";
        ZebraStruct.dump();
        llvm_unreachable("[ZEBRA AdjustGlo] Global not correctly initialized.");
    }
}

Constant *GetGlobalInitializer(GlobalVariable& Global, Module& M, Constant& CounterValue, Type &ZebraStruct) {
    Type *GlobalValueType = Global.getValueType();

    if (GlobalValueType->isIntegerTy()) {

        Constant *OrigInitializer = Global.getInitializer();
        return GetGlobalInitValue(M, GlobalValueType, OrigInitializer, CounterValue, ZebraStruct);
    }
    else if (GlobalValueType->isArrayTy() || GlobalValueType->isStructTy()) {

        return GetGlobalInitValue(M, GlobalValueType, Global.getOperand(0), CounterValue, ZebraStruct);
    }
    else {
        dbgs() << "[ZEBRA AdjustGlo] Did not find suitable initializer for ";
        Global.dump();
        llvm_unreachable("[ZEBRA AdjustGlo] Global not correctly initialized.");
    }
}


Type *InitializeZebraStruct(Type* GlobalValueType, Module& M) {
    ZebraTypeGenerator ZebraGenerator = M.getZebraGenerator();
    std::vector<Type*> Elems;

    if (GlobalValueType->isIntegerTy()) {

        return ZebraGenerator.GetZebraTypeForType(GlobalValueType);
    }
    else if (GlobalValueType->isArrayTy()) {
        uint64_t ArrayLength = GlobalValueType->getArrayNumElements();
        Type *ArrayElemType = GlobalValueType->getArrayElementType();

        return ArrayType::get(InitializeZebraStruct(ArrayElemType, M), ArrayLength);
    }
    else if (GlobalValueType->isStructTy()) {
        uint64_t StructNumElem = GlobalValueType->getStructNumElements();
        for (uint i = 0; i < StructNumElem; ++i) {
            Elems.push_back(InitializeZebraStruct(GlobalValueType->getStructElementType(i), M));
        }

        return StructType::get(M.getContext(), Elems);
    }
    else if (GlobalValueType->isPointerTy()) {
        dbgs() << "[ZEBRA AdjustGlo] No handling needed for pointer type.\n";

        return PointerType::get(M.getContext(), 0);
    }
    else {
        dbgs() << "[ZEBRA AdjustGlo] Cannot initialize zebra struct type: ";
        GlobalValueType->dump();
        llvm_unreachable("[ZEBRA AdjustGlo] ZebraStruct initialization failed.");
    }
}

PreservedAnalyses ZebraAdjustGlobalsPass::run(Module &Mod, ModuleAnalysisManager &AM) {
    M = &Mod;
    if (!M->hasZebraGenerator())
        return PreservedAnalyses::all();

    std::string CounterName = "ZebraCounter_" + M->getName().str();
    Constant *CounterValue = M->getNamedGlobal(CounterName)->getInitializer();
    bool GlobalIsZebrafied = false;
    bool IsGlobalZebraCounter = false;
    std::vector<Instruction*> UninstrumentedInstructions;

    // "Copy" all global variables: create suitable interleaved version of each GV
    for (auto Global = M->global_begin(); Global != M->global_end(); ++Global) {
        GlobalVariable *OrigGlobal = &*Global;
        Type *GlobalValueType = Global->getValueType();
        if (GlobalValueType->isPointerTy())
            continue;

        GlobalIsZebrafied = Global->getName().contains("__zebra");
        IsGlobalZebraCounter = Global->getName().contains("ZebraCounter");
        std::string GlobalName = Global->getName().str();
        std::string GlobalZebraName = GlobalName.append("__zebra");
        // Fixme: refine this check
        bool IsGlobalStringFormat = Global->hasPrivateLinkage() && Global->getName().contains(".str");

        if (GlobalIsZebrafied || IsGlobalZebraCounter || IsGlobalStringFormat || GlobalValueType->isPointerTy()) {
            //dbgs() << "[ZEBRA AdjustGlo] No handling needed for ";
            //dbgs() << Global->getName() << " with type ";
            //GlobalValueType->dump();
            continue;
        }

        dbgs() << "[ZEBRA AdjustGlo] Found global " << GlobalName << " with value type ";
        GlobalValueType->dump();

        Type *ZebraStruct = InitializeZebraStruct(GlobalValueType, *M);
        M->getOrInsertGlobal(GlobalZebraName, ZebraStruct);
        GlobalVariable *GlobalZebraVar = M->getNamedGlobal(GlobalZebraName);

        GlobalZebraVar->setDSOLocal(Global->isDSOLocal());
        GlobalZebraVar->setLinkage(Global->getLinkage());
        GlobalZebraVar->setAlignment(Align(16));

        Constant *Initializer = GetGlobalInitializer(*Global, *M, *CounterValue, *ZebraStruct);
        GlobalZebraVar->setInitializer(Initializer);
        GlobalZebraVar->setConstant(false);

        Global->replaceAllUsesWith(GlobalZebraVar);
        if (OrigGlobal->getNumUses() > 0)
            llvm_unreachable("[ZEBRA AdjustGlo] Check global handling - something is not yet fully supported.");
    }
    dbgs() << "[ZEBRA AdjustGlo] Zebrafied global variables.\n";

    for (auto *Inst : UninstrumentedInstructions) {
        Inst->eraseFromParent();
    }

    return PreservedAnalyses::none(); // Fixme: refine none
}