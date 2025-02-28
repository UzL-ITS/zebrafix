#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/ZebraProperties.h"
#include "ZebraCallgraphTraversalPass.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Analysis/CallGraph.h"

using namespace llvm;

ZebraCallgraphTraversalPass::ZebraCallgraphTraversalPass() {}

Function *ZebraCallgraphTraversalPass::cloneFunction(Function *F, const std::string &NameSuffix, bool IsAutoCopy) {
    // Update attribute of original function
    if (!IsAutoCopy)
        F->addFnAttr(Attribute::getWithZebraProperties(F->getContext(), ZebraProperties(ZebraProperties::State::Original)));

    // Create function clone for subsequent instrumentation
    Function *FCopy = Function::Create(F->getFunctionType(), F->getLinkage(), F->getName() + NameSuffix, M);

    ValueToValueMapTy VMap;
    Function::arg_iterator FCopyArgIt = FCopy->arg_begin();
    for (Function::const_arg_iterator FArgIt = F->arg_begin(); FArgIt != F->arg_end(); ++FArgIt) {
        VMap[FArgIt] = FCopyArgIt++;
    }
    SmallVector<ReturnInst*, 8> FCopyReturns;
    CloneFunctionInto(FCopy, F, VMap, CloneFunctionChangeType::GlobalChanges, FCopyReturns);

    if(IsAutoCopy)
        FCopy->addFnAttr(Attribute::getWithZebraProperties(F->getContext(), ZebraProperties(ZebraProperties::State::AutoCopy)));
    else
        FCopy->addFnAttr(Attribute::getWithZebraProperties(F->getContext(), ZebraProperties(ZebraProperties::State::Copy)));

    return FCopy;
}

PreservedAnalyses ZebraCallgraphTraversalPass::run(llvm::Module &Mod, llvm::ModuleAnalysisManager &AM) {

    M = &Mod;
    auto &CG = AM.getResult<CallGraphAnalysis>(*M);

    // Iterate through all functions manually marked as needing instrumentation.
    // For each function, we traverse its call graph, create specific clones of all
    // callees, and rewrite calls accordingly.

    // Find all initially marked functions
    std::vector<CallGraphNode *> MarkedFunctions;
    for (auto &F: *M) {
        if (F.hasFnAttribute(Attribute::AttrKind::Zebra)) {
            const Attribute &FZAttr = F.getFnAttribute(Attribute::AttrKind::Zebra);
            const ZebraProperties &ZP = FZAttr.getZebraProperties();

            if (ZP.getState() == ZebraProperties::Marked) {
                MarkedFunctions.push_back(CG[&F]);
            }
        }
    }
    // Apart from initially marked functions, we also need to instrument functions that are "called"
    // via function pointers that are held in identified structs
    dbgs() << "[ZEBRA Callgraph] Checking function pointers in structs.\n";
    for (auto &F: *M) {
        for (auto User : F.users()) {
            if (auto S = dyn_cast<StructType>(User->getType())) {
                dbgs() << "[ZEBRA Callgraph] Found user in function " << F.getName() << ": ";
                User->dump();
                MarkedFunctions.push_back(CG[&F]);
            }
        }
    }

    // Visit all marked functions
    int CloneSuffixCounter = 0;
    for (auto *MarkedCGNode: MarkedFunctions) {
        dbgs() << "[ZEBRA Callgraph] Cloning \"" << MarkedCGNode->getFunction()->getName().str() << "\"\n";

        Function *NewMarked = cloneFunction(MarkedCGNode->getFunction(), "__zebra", false);
        CG.addToCallGraph(NewMarked);
        CallGraphNode *NewMarkedCGNode = CG[NewMarked];

        // The functions in the queue have already been cloned, and wait for their callees to be processed.
        std::vector<CallGraphNode *> Queue;
        Queue.push_back(NewMarkedCGNode);

        if (NewMarkedCGNode->size() > 0)
            ++CloneSuffixCounter;

        while (!Queue.empty()) {
            // Current child function
            CallGraphNode *CGNode = Queue.back();
            Queue.pop_back();
            Function *CurF = CGNode->getFunction();

            dbgs() << "[ZEBRA Callgraph]   Processing callees of  \"" << CurF->getName() << "\"\n";

            // Iterate callees of child function
            SmallVector<CallGraphNode::CallRecord, 8> CGNodeCallees(CGNode->begin(), CGNode->end());
            for (auto &CalleeCGInfo: CGNodeCallees) {

                auto *CalleeCGNode = CalleeCGInfo.second;
                auto *Callee = CalleeCGNode->getFunction();

                if (Callee == nullptr) {

                    // Try to extract associated call instructions
                    auto CalleeCallInfo = CalleeCGInfo.first;
                    if (CalleeCallInfo.has_value() && CalleeCallInfo.value().pointsToAliveValue()) {
                        Value *CalleeCallValue = CalleeCallInfo.value();
                        if (CallBase *CalleeCallInst = dyn_cast<CallBase>(CalleeCallValue)) {
                            if (CalleeCallInst->isInlineAsm()) {
                                // Skip inline assembly
                                dbgs() << "[ZEBRA Callgraph]     WARNING: Skipping inline assembly.\n";
                                continue;
                            }
                        }
                    }

                    dbgs() << "[ZEBRA Callgraph]     WARNING: Encountered nullptr callee. Probably an indirect call\n";
                    continue;
                }

                Function *ToClone = Callee;
                if (Callee->isIntrinsic()) {

                    switch (Callee->getIntrinsicID()) {
                        // Ignore certain intrinsics that won't be translated to function calls
                        case Intrinsic::lifetime_start:
                        case Intrinsic::lifetime_end:
                        case Intrinsic::fshl:
                        case Intrinsic::bswap:
                        case Intrinsic::abs:
                        case Intrinsic::umin:
                        case Intrinsic::smin:
                        case Intrinsic::umax:
                        case Intrinsic::ctlz:
                        case Intrinsic::cttz:
                        case Intrinsic::umul_with_overflow:
                        case Intrinsic::stacksave:
                        case Intrinsic::stackrestore:
                        case Intrinsic::assume:
                        case Intrinsic::experimental_noalias_scope_decl:
                        case Intrinsic::objectsize:
                        case Intrinsic::prefetch:
                            continue;

                        // Replace others by our custom implementations

                        case Intrinsic::memset:
                            ToClone = M->getFunction("_zebra__memset");
                            dbgs() << "[ZEBRA Callgraph]    memset cloning\n";
                            break;

                        case Intrinsic::memcpy:
                            ToClone = M->getFunction("_zebra__memcpy");
                            dbgs() << "[ZEBRA Callgraph]    memcpy cloning\n";
                            break;

                        case Intrinsic::memmove:
                            ToClone = M->getFunction("_zebra__memmove");
                            dbgs() << "[ZEBRA Callgraph]    memmove cloning\n";
                            break;

                        default:
                            dbgs() << "[ZEBRA Callgraph]     Encountered intrinsic callee \"" << Callee->getName() << "\", which cannot be instrumented\n";
                            llvm_unreachable("Unexpected intrinsic");
                    }
                }

                // Make sure that the original malloc/calloc is kept as is until we are in the heap allocation pass
                // Fixme: refine the malloc check
                std::string FuncName = Callee->getName().str();
                bool IsMalloc = FuncName.find("malloc") != FuncName.npos;
                bool IsCalloc = FuncName.find("calloc") != FuncName.npos;
                bool IsFree = FuncName.find("free") != FuncName.npos;
                if (Callee->isDeclaration() && !Callee->isIntrinsic() && (IsMalloc || IsCalloc || IsFree)) {
                    // At this point, we do not have information about the function argument values.
                    // Therefore, we wait until ZebraHeapAllocsPass find the CallInst to malloc/calloc and adjust the
                    // allocated size there.
                    // Also, we do not want to further instrument malloc/calloc and free or their child functions
                    dbgs() << "[ZEBRA Callgraph]   Encountered malloc, calloc or free - skipping for now.\n";
                    continue;
                }

                bool IsPrintf = FuncName.find("printf") != FuncName.npos;
                bool IsFWrite = FuncName.find("fwrite") != FuncName.npos;
                bool IsFPutC = FuncName.find("fputc") != FuncName.npos;
                bool IsPuts = FuncName.find("puts") != FuncName.npos;
                bool IsPutChar = FuncName.find("putchar") != FuncName.npos;
                if (Callee->isDeclaration() && !Callee->isIntrinsic() && (IsPrintf || IsFWrite || IsFPutC || IsPutChar || IsPuts)) {
                    dbgs() << "[ZEBRA Callgraph]   Encountered printing - skipping for now.\n";
                    continue;
                }

                bool IsClockTime = FuncName.find("clock_gettime") != FuncName.npos;
                if (Callee->isDeclaration() && !Callee->isIntrinsic() && (IsClockTime)) {
                    dbgs() << "[ZEBRA Callgraph]   Encountered performance measurement - skipping for now.\n";
                    continue;
                }

                bool IsGetRandom = FuncName.find("getrandom") != FuncName.npos;
                bool IsRead = FuncName.find("read") != FuncName.npos;
                bool IsOpen = FuncName.find("open") != FuncName.npos;
                bool IsClose = FuncName.find("close") != FuncName.npos;
                bool IsFstat = FuncName.find("fstat") != FuncName.npos;
                bool IsFcntl = FuncName.find("fcntl") != FuncName.npos;
                bool IsPoll = FuncName.find("poll") != FuncName.npos;
                if (Callee->isDeclaration() && !Callee->isIntrinsic() && (IsGetRandom || IsRead || IsOpen || IsClose ||  IsFstat || IsFcntl || IsPoll)) {
                    dbgs() << "[ZEBRA Callgraph]   Encountered getrandom or read or open/close/fstat/fcntl or poll - skipping for now.\n";
                    continue;
                }

                bool IsAbort = FuncName.find("abort") != FuncName.npos;
                bool IsAssertFail = FuncName.find("__assert_fail") != FuncName.npos;
                bool IsErrNo = FuncName.find("__errno_location") != FuncName.npos;
                if (Callee->isDeclaration() && !Callee->isIntrinsic() && (IsAbort || IsAssertFail || IsErrNo)) {
                    dbgs() << "[ZEBRA Callgraph]   Encountered abort or assert_fail - skipping for now.\n";
                    continue;
                }

                bool IsZeroCheck = FuncName.find("__explicit_bzero_chk") != FuncName.npos;
                bool IsMemCpyCheck = FuncName.find("__memcpy_chk") != FuncName.npos;
                bool IsMemMoveCheck = FuncName.find("__memmove_chk") != FuncName.npos;

                if (Callee->isDeclaration() && !Callee->isIntrinsic() && IsZeroCheck) {
                    dbgs() << "[ZEBRA Callgraph]   Encountered non-intrinsic libc explicit bzero check.\n";
                    ToClone = M->getFunction("_zebra_explicit_bzero");
                }

                if (Callee->isDeclaration() && !Callee->isIntrinsic() && IsMemCpyCheck) {
                    dbgs() << "[ZEBRA Callgraph]   Encountered non-intrinsic libc memcpy check.\n";
                    ToClone = M->getFunction("_zebra__memcpy_chk");
                }

                if (Callee->isDeclaration() && !Callee->isIntrinsic() && IsMemMoveCheck) {
                    dbgs() << "[ZEBRA Callgraph]   Encountered non-intrinsic libc memmove check.\n";
                    ToClone = M->getFunction("_zebra__memmove_chk");
                }

                // Replace external functions by our custom ones, as we cannot instrument them otherwise
                if (Callee->isDeclaration() && !Callee->isIntrinsic() && !(IsZeroCheck || IsMemCpyCheck || IsMemMoveCheck)) {
                    // Do we have a custom definition for that function?
                    std::string CustomExternalName = "_zebra_" + Callee->getName().str();
                    Function *CustomExternal = M->getFunction(CustomExternalName);
                    if (CustomExternal == nullptr) {
                        dbgs() << "[ZEBRA Callgraph]      Trying to get " << CustomExternalName << "\n";
                        dbgs() << "[ZEBRA Callgraph]      Encountered declaration-only callee \"" << Callee->getName()
                               << "\", probably an external function?\n";
                        llvm_unreachable("Undefined child function");
                    }

                    ToClone = CustomExternal;
                }

                // Did we already process that callee elsewhere?
                std::string ClonedNameSuffix = "__zebra_" + std::to_string(CloneSuffixCounter);
                Function *Cloned = M->getFunction(ToClone->getName().str() + ClonedNameSuffix);
                bool AlreadyProcessed = Cloned != nullptr;

                // Clone
                if (!AlreadyProcessed) {
                    dbgs() << "[ZEBRA Callgraph]     Cloning child \"" << ToClone->getName() << "\"\n";
                    Cloned = cloneFunction(ToClone, ClonedNameSuffix, true);

                    CG.addToCallGraph(Cloned);
                }
                CallGraphNode *ClonedCGNode = CG[Cloned];

                // Rewrite all calls to current callee
                for (BasicBlock *BB = &CurF->front(); BB != nullptr; BB = BB->getNextNode()) {
                    for (Instruction *I = &BB->front(); I != nullptr; I = I->getNextNode()) {

                        CallInst *CI = dyn_cast<CallInst>(I);
                        if (CI == nullptr || CI->getCalledFunction() != Callee)
                            continue;

                        // Replace call target
                        if (CI->getIntrinsicID() != Intrinsic::not_intrinsic) {
                            switch (CI->getIntrinsicID()) {
                                case Intrinsic::memset:
                                case Intrinsic::memcpy:
                                case Intrinsic::memmove:
                                    CI->setCalledFunction(Cloned);
                                    break;

                                default:
                                    dbgs() << "[ZEBRA Callgraph]     Cannot translate call to \"" << Callee->getName()
                                           << "\"\n";
                                    llvm_unreachable("Unhandled intrinsic");
                            }
                        } else {
                            CI->setCalledFunction(Cloned);
                        }

                        CGNode->replaceCallEdge(*CI, *CI, ClonedCGNode);
                    }
                }

                // Enqueue this callee
                if (!AlreadyProcessed)
                    Queue.push_back(ClonedCGNode);
            }
        }
    }

    return PreservedAnalyses::none(); // Fixme: refine none
}