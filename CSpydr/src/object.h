#ifndef cspydr_object_h
#define cspydr_object_h

#include "value.h"
#include "common.h"
#include "chunk.h"

#define OBJ_TYPE(value) (AS_OBJ(value)->type)

#define IS_CLOSURE(value) isObjType(value, OBJ_CLOSURE)
#define IS_STRING(value) isObjType(value, OBJ_STRING)
#define IS_FUNCTION(value) isObjType(value, OBJ_FUNCTION)
#define IS_NATIVE(value)       isObjType(value, OBJ_NATIVE)

#define AS_CLOSURE(value) ((ObjClosure*)AS_OBJ(value))
#define AS_FUNCTION(value) ((ObjFunction*)AS_OBJ(value))
#define AS_STRING(value) ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString*)AS_OBJ(value))->chars)
#define AS_NATIVE(value) (((ObjNative*)AS_OBJ(value))->function)

typedef enum
{
	OBJ_FUNCTION,
	OBJ_STRING,
	OBJ_NATIVE,
	OBJ_CLOSURE,
	OBJ_UPVALUE
} ObjType;

struct sObj
{
	ObjType type;
	bool isMarked;
	struct sObj *next;
};

typedef struct
{
	Obj obj;
	int arity;
	int upvalueCount;
	Chunk chunk;
	ObjString* name;
} ObjFunction;

typedef struct ObjUpvalue
{
	Obj obj;
	Value* location;
	Value closed;
	struct ObjUpvalue* next;
} ObjUpvalue;

typedef struct
{
	Obj obj;
	ObjFunction* function;
	ObjUpvalue** upvalues;
	int upvalueCount;
} ObjClosure;

typedef Value(*NativeFn) (int argCount, Value* args);

typedef struct
{
	Obj obj;
	NativeFn function;
} ObjNative;

struct sObjString
{
	Obj obj;
	int length;
	char *chars;
	uint32_t hash;
};

ObjNative* newNative(NativeFn function);
ObjFunction* newFunction();
ObjUpvalue* newUpvalue(Value* slot);
ObjClosure* newClosure();
ObjString *takeString(char *chars, int length);
ObjString *copyString(const char *chars, int length);
void printObject(Value value);

static inline bool isObjType(Value value, ObjType type)
{
	return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif