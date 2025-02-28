//==================================================================================
// FILE:
//    ZebrafixInit.cpp
//
// DESCRIPTION:
//      Initialization for using out-of-tree Zebrafix passes
//      Adding protection against ciphertext side-channel leakages via interleaving
//
// USAGE:
//     clang <infile> -fpass-plugin=path/to/libZebrafixPasses.so  -o <outfile>
//==================================================================================
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "ZebraAdjustGlobalsPass.h"
#include "ZebraAdjustIdentifiedStructsPass.h"
#include "ZebraAdjustMemoryPass.h"
#include "ZebraCallgraphTraversalPass.h"
#include "ZebraCounterPass.h"
#include "ZebraHeapAllocsPass.h"
#include "ZebraInsertStructsPass.h"
#include "ZebraRewriteCallsPass.h"
#include "ZebraRewriteConstantExprGEPsPass.h"
#include "ZebraSVFMemIntrinsics.h"
#include "MSSA/SVFGBuilder.h"
#include "SVF-LLVM/LLVMUtil.h"
#include "llvm/IRPrinter/IRPrintingPasses.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/FileSystem.h"

using namespace llvm;

llvm::PassPluginLibraryInfo getZebrafixPassesPluginInfo() {
    return {LLVM_PLUGIN_API_VERSION, "ZebrafixPasses", LLVM_VERSION_STRING,
            [](PassBuilder &PB) {
                PB.registerOptimizerLastEPCallback([&PB](ModulePassManager &MPM, auto) {
                    MPM.addPass(ZebraCallgraphTraversalPass());
                    MPM.addPass(createModuleToFunctionPassAdaptor(ZebraRewriteCallsPass()));
                    MPM.addPass(ZebraInsertStructsPass());
                    MPM.addPass(ZebraCounterPass());
                    MPM.addPass(ZebraAdjustIdentifiedStructsPass());
                    MPM.addPass(ZebraAdjustGlobalsPass());
                    MPM.addPass(createModuleToFunctionPassAdaptor(ZebraRewriteConstantExprGEPsPass()));
                    MPM.addPass(ZebraSVFMemIntrinsicsPass());
                    MPM.addPass(createModuleToFunctionPassAdaptor(ZebraHeapAllocsPass()));
                    MPM.addPass(createModuleToFunctionPassAdaptor(ZebraAdjustMemoryPass()));

                    std::error_code EC;
                    raw_ostream *OutFileStream;
                    OutFileStream = new raw_fd_ostream("app-instr.ll", EC, sys::fs::OF_None);
                    if (EC) {
                        errs() << "Error opening file: " << EC.message() << "\n";
                    }

                    MPM.addPass(PrintModulePass(*OutFileStream));

                    return true;
                });
            }};
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
    return getZebrafixPassesPluginInfo();
}
