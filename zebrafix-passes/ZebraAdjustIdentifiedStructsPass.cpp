#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/ZebraProperties.h"
#include "ZebraAdjustMemoryPass.h"
#include "llvm/Transforms/Instrumentation/ZebraTypeGenerator.h"
#include "llvm/IR/Instructions.h"
#include "ZebraAdjustIdentifiedStructsPass.h"

using namespace llvm;

ZebraAdjustIdentifiedStructsPass::ZebraAdjustIdentifiedStructsPass() {}

PreservedAnalyses ZebraAdjustIdentifiedStructsPass::run(Module &Mod, ModuleAnalysisManager &AM) {
    M = &Mod;
    LLVMContext &Ctx = M->getContext();
    if (!M->hasZebraGenerator())
        return PreservedAnalyses::all();

    std::vector<StructType*> IdentifiedStructTys = M->getIdentifiedStructTypes();
    ZebraTypeGenerator Z = M->getZebraGenerator();

    for (auto S : IdentifiedStructTys) {
        // Store all subtypes of the struct to adjust them with zebra types
        std::vector<Type *> Args(S->subtypes());
        Type* Arg;
        for (auto & i : Args) {
            Arg = i;
            if (Arg->isStructTy()) {
                i = StructType::getTypeByName(Ctx, Arg->getStructName().str() + "__zebra");
                if (i == nullptr)
                    i = StructType::create(Ctx, Arg->getStructName().str() + "__zebra");
            }
            else if (Arg->isIntegerTy()) {
                i = Z.GetZebraTypeForType(Arg);
            }
            else if (Arg->isArrayTy()) {
                ArrayType *ZebraArrayType = ArrayType::get(Z.GetZebraTypeForType(Arg->getArrayElementType()), Arg->getArrayNumElements());
                i = ZebraArrayType;
            }
            else {
                dbgs() << "[ZEBRA IdStructs] No handling for type "; Arg->dump();
            }
        }

        StructType *ZebraStructType = StructType::getTypeByName(Ctx, S->getStructName().str() + "__zebra");
        if (ZebraStructType == nullptr) {
            ZebraStructType = StructType::create(Ctx, Args, S->getStructName().str() + "__zebra");
            dbgs() << "[ZEBRA IdStructs] Inserted "; ZebraStructType->dump();
        }
        else {
            ZebraStructType->setBody(Args);
            dbgs() << "[ZEBRA IdStructs] Adjusted "; ZebraStructType->dump();
        }
    }

    // Add support for replacing function pointers in structs
    for (auto &F: *M) {
        for (auto &U : F.uses()) {
            auto IsFuncPtr = [](Use &U) -> bool {
                User *Usr = U.getUser();
                if (auto S = dyn_cast<StructType>(Usr->getType())) {
                    return true;
                } else
                    return false;
            };
            Function *ZebraFunc = M->getFunction(F.getName().str() + "__zebra");
            if (ZebraFunc != nullptr) {
                F.replaceUsesWithIf(ZebraFunc, IsFuncPtr);
                dbgs() << "[ZEBRA IdStructs] FuncPtrs: " << F.getName() << " successfully replaced with " << ZebraFunc->getName() << "\n";
            }
        }
    }

    return PreservedAnalyses::none(); // Fixme: refine none
}