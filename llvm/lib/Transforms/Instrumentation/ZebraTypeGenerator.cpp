#include "llvm/Transforms/Instrumentation/ZebraTypeGenerator.h"

using namespace llvm;

ZebraTypeGenerator::ZebraTypeGenerator(LLVMContext &Ctx) {
    Context = &Ctx;

    ZebraI1Type = GetZebraI1Type();
    ZebraI8Type = GetZebraI8Type();
    ZebraI16Type = GetZebraI16Type();
    ZebraI32Type = GetZebraI32Type();
    ZebraI64Type = GetZebraI64Type();
    ZebraDefaultType = GetZebraDefaultType();
}

StructType * ZebraTypeGenerator::GetZebraTypeForType(Type *PureType) const {
    if (PureType->isIntegerTy(1)) {
        return ZebraI1Type;
    }
    if (PureType->isIntegerTy(8)) {
        return ZebraI8Type;
    }
    if (PureType->isIntegerTy(16)) {
        return ZebraI16Type;
    }
    if (PureType->isIntegerTy(32)) {
        return ZebraI32Type;
    }
    if (PureType->isIntegerTy(64)) {
        return ZebraI64Type;
    }

    PureType->dump();
    return ZebraDefaultType;
}

StructType* ZebraTypeGenerator::GetZebraDefaultType() {
    Type *Args[] = {Type::getInt64Ty(*Context), Type::getInt64Ty(*Context)};
    StructType *ZebraStruct = StructType::create(*Context, Args, "DefaultStruct", false);

    return ZebraStruct;
}

StructType* ZebraTypeGenerator::GetZebraI1Type() {
    Type *Args[] = {Type::getInt1Ty(*Context),
                    Type::getInt1Ty(*Context), Type::getInt1Ty(*Context),Type::getInt1Ty(*Context),Type::getInt1Ty(*Context),Type::getInt1Ty(*Context),Type::getInt1Ty(*Context),Type::getInt1Ty(*Context),
                    Type::getInt8Ty(*Context), Type::getInt16Ty(*Context), Type::getInt32Ty(*Context),
                    Type::getInt64Ty(*Context)};
    StructType *ZebraStruct = StructType::create(Args, "struct.zi1");

    return ZebraStruct;
}

StructType* ZebraTypeGenerator::GetZebraI8Type() {
    Type *Args[] = {Type::getInt8Ty(*Context),
                    Type::getInt8Ty(*Context), Type::getInt16Ty(*Context), Type::getInt32Ty(*Context),
                    Type::getInt64Ty(*Context)};
    StructType *ZebraStruct = StructType::create(Args, "struct.zi8");

    return ZebraStruct;
}

StructType* ZebraTypeGenerator::GetZebraI16Type() {
    Type *Args[] = {Type::getInt16Ty(*Context),
                    Type::getInt16Ty(*Context), Type::getInt32Ty(*Context),
                    Type::getInt64Ty(*Context)};
    StructType *ZebraStruct = StructType::create(Args, "struct.zi16");

    return ZebraStruct;
}

StructType* ZebraTypeGenerator::GetZebraI32Type() {
    Type *Args[] = {Type::getInt32Ty(*Context),
                    Type::getInt32Ty(*Context),
                    Type::getInt64Ty(*Context)};
    StructType *ZebraStruct = StructType::create(Args, "struct.zi32");

    return ZebraStruct;
}

StructType* ZebraTypeGenerator::GetZebraI64Type() {
    Type *Args[] = {Type::getInt64Ty(*Context),
                    Type::getInt64Ty(*Context)};
    StructType *ZebraStruct = StructType::create(Args, "struct.zi64");

    return ZebraStruct;
}
