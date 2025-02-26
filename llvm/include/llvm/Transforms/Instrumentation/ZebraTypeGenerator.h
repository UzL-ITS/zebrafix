#ifndef LLVM_ZEBRATYPEGENERATOR_H
#define LLVM_ZEBRATYPEGENERATOR_H

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Type.h"
namespace llvm {

class Module;

class ZebraTypeGenerator {

    public:
        ZebraTypeGenerator() {};

        ZebraTypeGenerator(LLVMContext &Ctx);

        StructType *ZebraI1Type;
        StructType *ZebraI8Type;
        StructType *ZebraI16Type;
        StructType *ZebraI32Type;
        StructType *ZebraI64Type;
        StructType *ZebraDefaultType;

        StructType *GetZebraI1Type();
        StructType *GetZebraI8Type();
        StructType *GetZebraI16Type();
        StructType *GetZebraI32Type();
        StructType *GetZebraI64Type();
        StructType *GetZebraDefaultType();

        StructType * GetZebraTypeForType(Type*) const;

        Module *M;

    private:
        LLVMContext *Context;

};

} // namespace llvm




#endif //LLVM_ZEBRATYPEGENERATOR_H
