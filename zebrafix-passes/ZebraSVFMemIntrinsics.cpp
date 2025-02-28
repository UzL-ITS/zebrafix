#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/ZebraProperties.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "ZebraSVFMemIntrinsics.h"
#include "SVF-LLVM/LLVMModule.h"
#include "SVF-LLVM/SVFIRBuilder.h"
#include "WPA/Andersen.h"
#include "WPA/AndersenPWC.h"
#include "SVF-LLVM/ObjTypeInference.h"
#include "MSSA/SVFGBuilder.h"

using namespace llvm;

DenseMap<CallInst *, std::tuple<Type *, Type *>> IntrinsicInfoMap;

ZebraSVFMemIntrinsicsPass::ZebraSVFMemIntrinsicsPass() { }

Type* GetBaseType(Type *Ty, Value *Val, Module &M) {
    // The base type is important to make sure that the instrumented variables are properly copied or set.
    // Depending on the byte width of a type, we need to make adjustments to the mem* intrinsics as we can
    // no longer assume that the data chunks will be placed in memory in a continuous manner.

    if (Ty->isIntegerTy())
        return Ty;

    if (Ty->isPointerTy())
        return Ty;

    if (Ty->isArrayTy())
        return GetBaseType(Ty->getArrayElementType(), Val, M);

    if (Ty->isStructTy()) {
        auto *StructTy = dyn_cast<StructType>(Ty);
        if (!StructTy->isLiteral() && StructTy->getStructName().contains("struct.zi")) {
            return Ty;
        }
    }

    dbgs() << "[SVF MemIntrinsi] Trying to find base type of "; Ty->dump();
    llvm_unreachable("[SVF MemIntrinsi] Cannot determine base type for mem* argument.");
}

Type *FindType(Value *Operand, SVF::LLVMModuleSet &moduleSet, SVF::SVFIR &pag, SVF::PointerAnalysis &pta, Module &M, Function &F) {
    if (const GlobalValue *G = dyn_cast<GlobalValue>(Operand)) {
        dbgs() << "[SVF MemIntrinsi] \t \t Found global mem* value with type ";
        auto *GlobalTy = G->getValueType();
        GlobalTy->dump();
        Type *Ty = GlobalTy;
        if (!GlobalTy->isStructTy())
            return Ty;

        auto *StructTy = dyn_cast<StructType>(GlobalTy);
        if (!StructTy->isLiteral() && StructTy->getStructName().contains("struct.zi")) {
            return Ty;
        }
        if (!isa<GEPOperator>(G)) {
            // Assumption: If we just have a pointer to the global without GEP, we need the type of the first struct element
            return GetBaseType(StructTy->getElementType(0), Operand, M);
        }
    }

    if (!Operand->getType()->isPointerTy()) {
        dbgs() << "[SVF MemIntrinsi] \t \t Found mem* argument with type ";
        Operand->getType()->dump();
        return Operand->getType();
    }

    SVF::SVFValue *SvfOp = moduleSet.getSVFValue(Operand);
    auto ValNode = pag.getValueNode(SvfOp);

    auto &pts = pta.getPts(ValNode);
    for (unsigned int pt: pts) {
        if (!pag.hasGNode(pt))
            continue;

        auto *TargetPtr = pag.getGNode(pt);
        if (!TargetPtr->hasValue())
            continue;
        const SVF::SVFValue *SVFVal = TargetPtr->getValue();
        const Value *LLVMVal = moduleSet.getLLVMValue(SVFVal);
        if (const auto &AllocInstr = dyn_cast<AllocaInst>(LLVMVal)) {
            dbgs() << "[SVF MemIntrinsi] \t \t Found mem* allocation with type ";
            AllocInstr->getAllocatedType()->dump();
            Type *AllocTy = AllocInstr->getAllocatedType();
            if (!AllocTy->isStructTy())
                return AllocTy;

            dbgs() << "[SVF MemIntrinsi] \t \t Setting mem allocation struct type ";
            AllocTy->dump();
            dbgs() << "[SVF MemIntrinsi] \t \t \t to first type found: ";
            AllocTy->getStructElementType(0)->dump();
            auto Z = M.getZebraGenerator();
            Type *StructElemTy = AllocTy->getStructElementType(0);
            auto StructTy = dyn_cast<StructType>(AllocTy);
            if (!StructTy->isLiteral() && StructType::getTypeByName(M.getContext(), StructTy->getStructName().str() + "__zebra") != nullptr) {
                return Z.GetZebraTypeForType(GetBaseType(StructElemTy, (Value *) LLVMVal, M));
            }
            return GetBaseType(StructElemTy, (Value *) LLVMVal, M);
        }
        else if (const auto &GEPInstr = dyn_cast<GetElementPtrInst>(LLVMVal)) {
            dbgs() << "[SVF MemIntrinsi] \t \t Found mem* GEP with type ";
            GEPInstr->getResultElementType()->dump();
            if (!GEPInstr->getSourceElementType()->isStructTy())
                return GEPInstr->getResultElementType();

            auto StructTy = dyn_cast<StructType>(GEPInstr->getSourceElementType());
            if (!StructTy->isLiteral() && StructTy->getStructName().contains("struct.zi")) {
                auto Z = M.getZebraGenerator();
                return Z.GetZebraTypeForType(GEPInstr->getResultElementType());
            }
        }
        else {
            dbgs() << "[SVF MemIntrinsi] \t \t Found mem* malloc ";
            LLVMVal->dump();
            return LLVMVal->getType();
        }
    }

    dbgs() << "[SVF MemIntrinsi] \t \t Found mem* unknown ";
    Operand->dump();
    if (const auto &GEPInstr = dyn_cast<GetElementPtrInst>(Operand)) {
        dbgs() << "[SVF MemIntrinsi] \t \t \t Found mem* unknown GEP ";
        GEPInstr->getResultElementType()->dump();
        if (!GEPInstr->getSourceElementType()->isStructTy())
            return GEPInstr->getResultElementType();

        auto StructTy = dyn_cast<StructType>(GEPInstr->getSourceElementType());
        auto Z = M.getZebraGenerator();
        StructType *IdentifiedZebraStructType = StructType::getTypeByName(M.getContext(), StructTy->getStructName().str() + "__zebra");
        if (IdentifiedZebraStructType != nullptr) {
            Type *BaseTy = GetBaseType(GEPInstr->getResultElementType(), GEPInstr, M);
            if (BaseTy->isPointerTy())
                return BaseTy;

            return Z.GetZebraTypeForType(BaseTy);
        }
    }
    else {
        dbgs() << "[SVF MemIntrinsi] \t \t \t Found mem* unknown ??? ";
        Operand->print(dbgs());
        dbgs() << "\n\t in function " << F.getName() << "\n";

        // Returning a pointer type here; this will be handled in pass
        // when inserting new mem* function
        return Operand->getType();
    }

    dbgs() << "[SVF MemIntrinsi] Trying to find source or dest type for "; Operand->dump();
    llvm_unreachable("[SVF MemIntrinsi] Cannot find type for mem* argument.");
}

PreservedAnalyses ZebraSVFMemIntrinsicsPass::run(llvm::Module &Mod, llvm::ModuleAnalysisManager &AM) {
    M = &Mod;
    if (!M->hasZebraGenerator())
        return PreservedAnalyses::all();

    dbgs() << "[SVF MemIntrinsi] Analyzing " << M->getName() << "\n";

    auto *moduleSet = SVF::LLVMModuleSet::getLLVMModuleSet();
    auto svfModule = SVF::LLVMModuleSet::buildSVFModule(*M);

    SVF::SVFIRBuilder builder(svfModule);
    SVF::SVFIR *pag = builder.build();

    SVF::Andersen *ander = SVF::AndersenWaveDiff::createAndersenWaveDiff(pag);
    ander->disablePrintStat();

    SVF::SVFGBuilder svfgBuilder(true);
    SVF::SVFG *svfg = svfgBuilder.buildFullSVFG(ander);

    dbgs() << "[SVF MemIntrinsi] Checking pointers...\n";
    SVF::PointerAnalysis *pta = new SVF::AndersenSCD(pag);
    pta->initialize();
    pta->disablePrintStat();

    for (Function &F: *M) {
        // If we are not in a function (auto) copy, we do not instrument
        if (F.hasFnAttribute(Attribute::AttrKind::Zebra)) {
            const Attribute &FZAttr = F.getFnAttribute(Attribute::AttrKind::Zebra);
            const ZebraProperties &ZP = FZAttr.getZebraProperties();

            bool IsFunctionCopy = ZP.getState() == ZebraProperties::Copy || ZP.getState() == ZebraProperties::AutoCopy;
            if (!IsFunctionCopy)
                continue;
        }

        for (BasicBlock &BB: F) {
            //dbgs() << "[SVF MemIntrinsi] Function " << F.getName() << "\n";
            for (Instruction &I: BB) {
                SVF::SVFValue *svfValue = moduleSet->getSVFValue(&I);
                if (!svfValue)
                    continue;
                if (!pag->hasValueNode(svfValue))
                    continue;

                SVF::NodeID ValueNodeID = pag->getValueNode(svfValue);
                SVF::SVFVar *CallPtr = pag->getGNode(ValueNodeID);
                if (!CallPtr->hasValue())
                    continue;

                const SVF::SVFValue *SVFCallVal = CallPtr->getValue();
                const Value *LLVMCallVal = moduleSet->getLLVMValue(SVFCallVal);
                if (!isa<CallInst>(LLVMCallVal))
                    continue;

                const auto *CallInstr = dyn_cast<CallInst>(LLVMCallVal);
                if (CallInstr->getCalledFunction() == nullptr) {
                    //dbgs() << "[SVF MemIntrinsi] Found indirect call in " << F.getName() << ": ";
                    //CallInstr->dump();
                    continue;
                }
                auto CalleeName = CallInstr->getCalledFunction()->getName();

                bool IsMemcpy = CalleeName.contains("_zebra__memcpy__zebra");
                bool IsMemset = CalleeName.contains("_zebra__memset__zebra");
                if (!(IsMemcpy || IsMemset))
                    continue;

                dbgs() << "[SVF MemIntrinsi]  Found mem* call ";
                CallInstr->print(dbgs());
                dbgs() << "\n \t in function " << F.getName() << "\n";

                // Get information about pointers and check base types
                Type *DstType = FindType(CallInstr->getOperand(0), *moduleSet, *pag, *pta, *M, F);
                Type *SrcType = FindType(CallInstr->getOperand(1), *moduleSet, *pag, *pta, *M, F);
                Type *DstBaseType = GetBaseType(DstType, CallInstr->getOperand(0), *M);
                Type *SrcBaseType = GetBaseType(SrcType, CallInstr->getOperand(1), *M);
                std::tuple<Type *, Type *> Info = std::make_tuple(DstBaseType, SrcBaseType);
                IntrinsicInfoMap.insert(std::make_pair((CallInst*)CallInstr, Info));
            }
        }
    }

    // Adjust LLVM IR file for mem* intrinsics
    // TODO: there are hard-coded function names which is really ugly and unstable --> find workaround
    std::vector<Instruction *> UninstrumentedInstructions;
    DataLayout DL = M->getDataLayout();

    for (auto Intr: IntrinsicInfoMap) {
        dbgs() << "[SVF MemIntrinsi] "; Intr.getFirst()->dump();

        CallInst* CallInstr = Intr.getFirst();
        Function* Callee = CallInstr->getCalledFunction();
        auto CalleeName = Callee->getName();
        bool IsMemset = CalleeName.contains("_zebra__memset__zebra");
        std::tuple<Type*, Type*> Info = Intr.getSecond();
        Type* DstBaseType = get<0>(Info);
        Type* SrcBaseType = get<1>(Info);
        auto DstByteCount = DL.getTypeAllocSize(DstBaseType);
        auto SrcByteCount = DL.getTypeAllocSize(SrcBaseType);

        IRBuilder<> B(CallInstr->getNextNode());
        if (IsMemset) {

            Function *NewMemset = M->getFunction("_zebra__helper_memset");
            NewMemset->addFnAttr(Attribute::getWithZebraProperties(M->getContext(), Callee->getFnAttribute(Attribute::AttrKind::Zebra).getZebraProperties()));

            if (DstBaseType->isPointerTy()) {
                if (CallInstr->getFunction()->getName().contains("chacha20_encrypt_bytes") || CallInstr->getFunction()->getName().contains("stream_ietf_ext_ref")) {
                    dbgs() << "[SVF MemIntrinsi] Fixing pointer type with zebra-i8 in memset!\n";
                    DstBaseType = M->getZebraGenerator().ZebraI8Type;
                    DstByteCount = DL.getTypeAllocSize(DstBaseType);
                }
                else {
                    //llvm_unreachable("[SVF MemIntrinsi] Cannot generate memset for pointer type.");
                    dbgs() << "[SVF MemIntrinsi] Fixing pointer type with zebra-i64 in memset!\n";
                    //DstBaseType = B.getInt64Ty();
                    //DstByteCount = DL.getTypeAllocSize(DstBaseType);
                    DstBaseType = M->getZebraGenerator().ZebraI64Type;
                    DstByteCount = DL.getTypeAllocSize(DstBaseType);
                }
            }

            bool ZebraDst = DstBaseType->isStructTy();

            if (ZebraDst) {
                DstByteCount = DL.getTypeAllocSize(DstBaseType->getStructElementType(0));
            }

            CallInst *NewCall = B.CreateCall(NewMemset, {CallInstr->getOperand(0), CallInstr->getOperand(1), CallInstr->getOperand(2), B.getInt64(DstByteCount), B.getInt1(ZebraDst)}, CallInstr->getName());

            CallInstr->replaceAllUsesWith(NewCall);
            UninstrumentedInstructions.push_back(CallInstr);
            dbgs() << "[SVF MemIntrinsi] Using new memset: " << NewMemset->getName() << "\n";
            continue;
        }

        Function *NewMemcpy = M->getFunction("_zebra__helper_memcpy");
        NewMemcpy->addFnAttr(Attribute::getWithZebraProperties(M->getContext(), Callee->getFnAttribute(Attribute::AttrKind::Zebra).getZebraProperties()));

        if (DstBaseType->isPointerTy() && SrcBaseType->isIntegerTy()) {
            if (CallInstr->getFunction()->getName().contains("mbedtls_put_unaligned_uint32")) {
                DstBaseType = B.getInt8Ty();
                DstByteCount = DL.getTypeAllocSize(DstBaseType);
            } else {
                DstBaseType = SrcBaseType;
                DstByteCount = SrcByteCount;
            }
            dbgs() << "[SVF MemIntrinsi] \t WARNING: assumed heap allocation in " << NewMemcpy->getName() << "\n";
        }

        if (SrcBaseType->isPointerTy() && DstBaseType->isIntegerTy()) {
            if (CallInstr->getFunction()->getName().contains("chacha20_block")) {
                SrcBaseType = B.getInt8Ty();
                SrcByteCount = DL.getTypeAllocSize(SrcBaseType);
            } else {
                SrcBaseType = DstBaseType;
                SrcByteCount = DstByteCount;
            }
            dbgs() << "[SVF MemIntrinsi] \t WARNING: assumed heap allocation in " << NewMemcpy->getName() << "\n";
        }

        if (SrcBaseType->isPointerTy() && DstBaseType->isStructTy()) {
            dbgs() << "[SVF MemIntrinsi] Fixing pointer type with zebra-i64 in memcpy!\n";
            SrcBaseType = M->getZebraGenerator().ZebraI64Type;
            SrcByteCount = DL.getTypeAllocSize(SrcBaseType);
            //SrcBaseType = DstBaseType;
            //SrcByteCount = DstByteCount;
        }

        if (DstBaseType->isPointerTy() && SrcBaseType->isPointerTy()) {
            if (CallInstr->getFunction()->getName().contains("memcpy_chk__zebra")) {
                dbgs() << "[SVF MemIntrinsi] Fixing pointer type with zebra-i8 and zebra-i64 in memcpy_chk!\n";
                DstBaseType = M->getZebraGenerator().ZebraI8Type;
                DstByteCount = DL.getTypeAllocSize(DstBaseType);
                SrcBaseType = M->getZebraGenerator().ZebraI64Type;
                SrcByteCount = DL.getTypeAllocSize(SrcBaseType);
            }
            else {
                dbgs() << "[SVF MemIntrinsi] Fixing pointer type with zebra-i64 and i64 in memcpy!\n";

                DstBaseType = M->getZebraGenerator().ZebraI64Type;
                DstByteCount = DL.getTypeAllocSize(DstBaseType);
                SrcBaseType = B.getInt64Ty();
                SrcByteCount = DL.getTypeAllocSize(SrcBaseType);
            }
        }

        // GetBaseType only returns a struct for the zebrafied structs
        // All other struct types are "extracted" beforehand
        bool ZebraDst = DstBaseType->isStructTy();
        bool ZebraSrc = SrcBaseType->isStructTy();

        StructType *DstTy;
        StructType *SrcTy;

        if (ZebraDst) {
            DstTy = dyn_cast<StructType>(DstBaseType);
            if (!DstTy->isLiteral() && DstTy->getStructName().contains("struct.zi")) {
                DstByteCount = DL.getTypeAllocSize(DstTy->getStructElementType(0));
                dbgs() << "[SVF MemIntrinsi] Adjusting DstByteCount to " << DstByteCount << "\n";
            }
        }
        if (ZebraSrc) {
            SrcTy = dyn_cast<StructType>(SrcBaseType);
            if (!SrcTy->isLiteral() && SrcTy->getStructName().contains("struct.zi")) {
                SrcByteCount = DL.getTypeAllocSize(SrcTy->getStructElementType(0));
                dbgs() << "[SVF MemIntrinsi] Adjusting SrcByteCount to " << SrcByteCount << "\n";
            }
        }

        bool Zebra = ZebraDst || ZebraSrc;
        CallInst *NewCall = B.CreateCall(NewMemcpy, {CallInstr->getOperand(0), CallInstr->getOperand(1), CallInstr->getOperand(2), B.getInt1(Zebra), B.getInt64(DstByteCount), B.getInt64(SrcByteCount)}, CallInstr->getName());

        CallInstr->replaceAllUsesWith(NewCall);
        UninstrumentedInstructions.push_back(CallInstr);
        dbgs() << "[SVF MemIntrinsi] Using new memcpy: " << NewMemcpy->getName() << "\n";
    }

    for (auto *Inst: UninstrumentedInstructions) {
        Inst->eraseFromParent();
    }

    // Clean up memory
    SVF::AndersenWaveDiff::releaseAndersenWaveDiff();
    SVF::SVFIR::releaseSVFIR();
    SVF::LLVMModuleSet::releaseLLVMModuleSet();

    return PreservedAnalyses::none();
}