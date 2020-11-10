#ifndef cspydr_natives_h
#define cspydr_natives_h

#include <math.h>
#include <string.h>
#include <stdlib.h>

#include "common.h"
#include "value.h"
#include "object.h"
#include "memory.h"
#include "vm.h"

static Value clockNative(int argCount, Value* args)
{
	return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

static Value consoleInputNative(int argCount, Value* args)
{
	if (argCount != 1) {
		runtimeError("Expect 1 arguments but got %d.", argCount);
		return NIL_VAL;
	}
	scannerIsMuted = true;

	char* input = malloc(sizeof(char) * (int) AS_NUMBER(args[0]));
	scanf("%s", input);

	ObjString* string = takeString(input, (int)AS_NUMBER(args[0]));
	scannerIsMuted = false;
	return OBJ_VAL(string);

}

static Value toIntNative(int argCount, Value* args)
{
	if (argCount != 1) {
		runtimeError("Expect 1 argument but got %d.", argCount);
		return NIL_VAL;
	}
	if (!IS_NUMBER(args[0])) {
		runtimeError("Expect number.");
		return NIL_VAL;
	}

	return NUMBER_VAL((int)AS_NUMBER(args[0]));
}

static Value sinNative(int argCount, Value* args)
{
	if (argCount != 1) {
		runtimeError("Expect 1 argument but got %d.", argCount);
		return NIL_VAL;
	}
	if (!IS_NUMBER(args[0])) {
		runtimeError("Expect number.");
		return NIL_VAL;
	}

	return NUMBER_VAL((double)sin(AS_NUMBER(args[0])));
}

static Value cosNative(int argCount, Value* args)
{
	if (argCount != 1) {
		runtimeError("Expect 1 argument but got %d.", argCount);
		return NIL_VAL;
	}
	if (!IS_NUMBER(args[0])) {
		runtimeError("Expect number.");
		return NIL_VAL;
	}

	return NUMBER_VAL((double)cos(AS_NUMBER(args[0])));
}

#endif
