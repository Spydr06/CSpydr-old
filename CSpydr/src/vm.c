#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "common.h"
#include "debug.h"
#include "compiler.h"
#include "vm.h"
#include "value.h"
#include "object.h"
#include "memory.h"
#include "natives.h"

VM vm;

static InterpretResult run();
static void resetStack();
static bool callValue(Value callee, int argCount);
static void defineNative(const char* name, NativeFn function);
void push(Value value);
Value pop();

void initVM()
{
	resetStack();
	vm.objects = NULL;

	vm.grayCount = 0;
	vm.grayCapacity = 0;
	vm.grayStack = NULL;
	vm.bytesAllocated = 0;
	vm.nextGC = 1024 * 1024;

	initTable(&vm.strings);
	initTable(&vm.globals);

	vm.initString = NULL;
	vm.initString = copyString("init", 4);

	defineNative("clock", clockNative);
	defineNative("to_int", toIntNative);
	defineNative("sin", sinNative);
	defineNative("cos", cosNative);
	defineNative("c_in", consoleInputNative);
	defineNative("clear", clearNative);
	defineNative("err", errorNative);
	defineNative("pi", piNative);
	defineNative("endl", endLineNative);
}

void freeVM()
{
	freeTable(&vm.strings);
	freeTable(&vm.globals);
	vm.initString = NULL;
	freeObjects();
}

InterpretResult interpret(const char *source)
{
	ObjFunction* function = compile(source);
	if (function == NULL) return INTERPRET_COMPILE_ERROR;

	push(OBJ_VAL(function));
	ObjClosure* closure = newClosure(function);
	pop();
	push(OBJ_VAL(closure));
	callValue(OBJ_VAL(closure), 0);

	return run();
}

static void resetStack()
{
	vm.stackTop = vm.stack;
	vm.frameCount = 0;
	vm.openUpvalues = NULL;
}

static void runtimeError(const char *format, ...)
{
	PRINT_ERROR(stderr);
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fputs("\n", stderr);

	for (int i = vm.frameCount - 1; i >= 0; i--) {
		CallFrame* frame = &vm.frames[i];
		ObjFunction* function = frame->closure->function;
		// -1 because the IP is sitting on the next instruction to be
		// executed.
		size_t instruction = frame->ip - function->chunk.code - 1;
		fprintf(stderr, "[line %d] in ",
			function->chunk.lines[instruction]);
		if (function->name == NULL) {
			fprintf(stderr, "script\n");
		}
		else {
			fprintf(stderr, "%s()\n", function->name->chars);
		}
	}


	resetStack();
	PRINT_RESET(stderr);
}

static void defineNative(const char* name, NativeFn function)
{
	push(OBJ_VAL(copyString(name, (int)strlen(name))));
	push(OBJ_VAL(newNative(function)));
	tableSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
	pop();
	pop();
}

void push(Value value)
{
	*vm.stackTop = value;
	vm.stackTop++;
}

Value pop()
{
	vm.stackTop--;
	return *vm.stackTop;
}

static Value peek(int distance)
{
	return vm.stackTop[-1 - distance];
}

static bool call(ObjClosure* closure, int argCount)
{
	if (argCount != closure->function->arity) {
		runtimeError("Expected %d arguments but got %d.",
			closure->function->arity, argCount);
	}

	if (vm.frameCount == FRAMES_MAX) {
		runtimeError("Stack overflow.");
		return false;
	}

	CallFrame* frame = &vm.frames[vm.frameCount++];
	frame->closure = closure;
	frame->ip = closure->function->chunk.code;

	frame->slots = vm.stackTop - argCount - 1;
	return true;
}

static bool callValue(Value callee, int argCount)
{
	if (IS_OBJ(callee)) {
		switch (OBJ_TYPE(callee)) {
		case OBJ_BOUND_METHOD:
		{
			ObjBoundMethod* bound = AS_BOUND_METHOD(callee);
			vm.stackTop[-argCount - 1] = bound->reciever;
			return call(bound->method, argCount);
		}

		case OBJ_CLASS:
		{
			ObjClass* _class = AS_CLASS(callee);
			vm.stackTop[-argCount - 1] = OBJ_VAL(newInstance(_class));
			Value initializer;
			if (tableGet(&_class->methods, vm.initString, &initializer)) {
				return call(AS_CLOSURE(initializer), argCount);
			}
			else if (argCount != 0) {
				runtimeError("Expected 0 arguments but got %d", argCount);
				return false;
			}
			return true;
		}

		case OBJ_CLOSURE:
			return call(AS_CLOSURE(callee), argCount);

		case OBJ_NATIVE:
		{
			NativeFn native = AS_NATIVE(callee);
			Value result = native(argCount, vm.stackTop - argCount);
			if (IS_NIL(result)) {
				return false;
			}

			vm.stackTop -= argCount + 1;
			push(result);
			return true;
		}

		case OBJ_FUNCTION:
			return call(AS_FUNCTION(callee), argCount);

		default:
			//Non-callable object type.
			break;
		}
	}

	runtimeError("Can only call functions and classes.");
	return false;
}

static bool invokeFromClass(ObjClass* _class, ObjString* name, int argCount)
{
	Value method;
	if (!tableGet(&_class->methods, name, &method)) {
		runtimeError("Undefined property '%s'", name->chars);
		return false;
	}

	return call(AS_CLOSURE(method), argCount);
}

static bool invoke(ObjString* name, int argCount)
{
	Value reciever = peek(argCount);

	if (!IS_INSTANCE(reciever)) {
		runtimeError("Only instances have methods.");
		return false;
	}

	ObjInstance* instance = AS_INSTANCE(reciever);

	Value value;
	if (tableGet(&instance->fields, name, &value)) {
		vm.stackTop[-argCount - 1] = value;
		return callValue(value, argCount);
	}

	return invokeFromClass(instance->_class, name, argCount);
}

static bool bindMethod(ObjClass* _class, ObjString* name)
{
	Value method;
	if (!tableGet(&_class->methods, name, &method)) {
		runtimeError("Undefined property '%s'.", name->chars);
		return false;
	}

	ObjBoundMethod* bound = newBoundMethod(peek(0), AS_CLOSURE(method));
	pop();
	push(OBJ_VAL(bound));
	return true;
}

static ObjUpvalue* captureUpvalue(Value* local)
{
	ObjUpvalue* prevUpvalue = NULL;
	ObjUpvalue* upvalue = vm.openUpvalues;

	while (upvalue != NULL && upvalue->location > local) {
		prevUpvalue = upvalue;
		upvalue = upvalue->next;
	}

	if (upvalue != NULL && upvalue->location == local) {
		return upvalue;
	}

	ObjUpvalue* createdUpvalue = newUpvalue(local);
	createdUpvalue->next = upvalue;

	if (prevUpvalue == NULL) {
		vm.openUpvalues = createdUpvalue;
	}
	else {
		prevUpvalue->next = createdUpvalue;
	}

	return createdUpvalue;
}

static void closeUpvalues(Value* last)
{
	while (vm.openUpvalues != NULL &&
		vm.openUpvalues->location >= last) {
		ObjUpvalue* upvalue = vm.openUpvalues;
		upvalue->closed = *upvalue->location;
		upvalue->location = &upvalue->closed;
		vm.openUpvalues = upvalue->next;
	}
}

static void defineMethod(ObjString* name)
{
	Value method = peek(0);
	ObjClass* _class = AS_CLASS(peek(1));
	tableSet(&_class->methods, name, method);
	pop();
}

static bool isFalsey(Value value)
{
	return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void concatenate()
{
	ObjString* b = AS_STRING(peek(0));
	ObjString* a = AS_STRING(peek(1));

	int length = a->length + b->length;
	char *chars = ALLOCATE(char, length + 1);
	memcpy(chars, a->chars, a->length);
	memcpy(chars + a->length, b->chars, b->length);
	chars[length] = '\0';

	ObjString *result = takeString(chars, length);
	pop();
	pop();
	push(OBJ_VAL(result));
}

static ObjString* doubleToObjString(double in)
{
	int length = sizeof(double) * 24;
	char* charArray = ALLOCATE(char, length);
	sprintf(charArray, "%g", in);

	return takeString(charArray, length);
}

static ObjString* boolToObjString(bool in)
{
	int length = sizeof(char) * (in ? 4 : 5);
	char* charArray = (in ? "true" : "false");

	return takeString(charArray, length);
}

static InterpretResult run()
{
	CallFrame* frame = &vm.frames[vm.frameCount - 1];

#define READ_BYTE() (*frame->ip++)
#define READ_SHORT() (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONSTANT() (frame->closure->function->chunk.constants.values[READ_BYTE()])
#define READ_STRING() AS_STRING(READ_CONSTANT())

#define BINARY_OP(valueType, op)                        \
	do                                                  \
	{													\
		if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) \
		{                                               \
			runtimeError("Operands must be numbers.");  \
			return INTERPRET_RUNTIME_ERROR;             \
		}                                               \
		double b = AS_NUMBER(pop());                    \
		double a = AS_NUMBER(pop());                    \
		push(valueType(a op b));                        \
	} while (false)

#define BINARY_SHIFT_OP(op)								\
	do                                                  \
	{                                                   \
		if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) \
		{                                               \
			runtimeError("Operands must be numbers.");  \
			return INTERPRET_RUNTIME_ERROR;             \
		}                                               \
		long b = (long) AS_NUMBER(pop());               \
		long a = (long) AS_NUMBER(pop());               \
		push(NUMBER_VAL(a op b));                       \
	} while (false)

#define MOD_OP()										\
	do{													\
		if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1)))	\
		{												\
														\
			runtimeError("Operands must be numbers.");  \
			return INTERPRET_RUNTIME_ERROR;				\
		}												\
		double b = AS_NUMBER(pop());                    \
		double a = AS_NUMBER(pop());					\
		push(NUMBER_VAL(fmod(a, b)));					\
	} while (false);

#define POWER_OP()										\
	do{													\
		if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1)))	\
		{												\
														\
			runtimeError("Operands must be numbers.");  \
			return INTERPRET_RUNTIME_ERROR;				\
		}												\
		double b = AS_NUMBER(pop());                    \
		double a = AS_NUMBER(pop());					\
		push(NUMBER_VAL(pow(a, b)));					\
	} while (false);

	for (;;)
	{

#ifdef DEBUG_TRACE_EXECUTION
		printf("        ");
		for (Value *slot = vm.stack; slot < vm.stackTop; slot++)
		{
			printf("  [");
			printValue(*slot);
			printf("] ");
		}
		printf("\n");

		disassembleInstruction(&frame->closure->function->chunk,(int)(frame->ip - frame->closure->function->chunk.code));
#endif

		uint8_t instruction;
		switch (instruction = READ_BYTE())
		{
		case OP_CONSTANT:
		{
			Value constant = READ_CONSTANT();
			push(constant);
			break;
		}
		case OP_NIL:
			push(NIL_VAL);
			break;
		case OP_TRUE:
			push(BOOL_VAL(true));
			break;
		case OP_FALSE:
			push(BOOL_VAL(false));
			break;
		case OP_POP:
			pop();
			break;

		case OP_GET_LOCAL:
		{
			uint8_t slot = READ_BYTE();
			push(frame->slots[slot]);
			break;
		}

		case OP_SET_LOCAL:
		{
			uint8_t slot = READ_BYTE();

			Value value = vm.stack[slot];
			if (value.isConstant || peek(0).isConstant) {
				runtimeError("Can't change the value of constant.");
				return INTERPRET_RUNTIME_ERROR;
			}

			frame->slots[slot] = peek(0);
			break;
		}

		case OP_DEFINE_GLOBAL:
		{
			ObjString* name = READ_STRING();
			Value value = peek(0);
			value.isConstant = false;
			tableSet(&vm.globals, name, value);
			pop();
			break;
		}
		
		case OP_GET_GLOBAL:
		{
			ObjString* name = READ_STRING();
			Value value;
			if (!tableGet(&vm.globals, name, &value)) {
				runtimeError("Undefined variable '%s'.", name->chars);
				return INTERPRET_RUNTIME_ERROR;
			}
			push(value);
			break;
		}

		case OP_SET_GLOBAL:
		{
			ObjString* name = READ_STRING();
			Value global;
			if (tableGet(&vm.globals, name, &global)) {
				if (global.isConstant) {
					runtimeError("Can't change the value of a constant.");
					return INTERPRET_RUNTIME_ERROR;
				}
			}

			if (tableSet(&vm.globals, name, peek(0))) {
				tableDelete(&vm.globals, name);
				runtimeError("Undefined variable '%s'.", name->chars);
				return INTERPRET_RUNTIME_ERROR;
			}
			break;
		}

		case OP_GET_UPVALUE:
		{
			uint8_t slot = READ_BYTE();
			push(*frame->closure->upvalues[slot]->location);
			break;
		}

		case OP_SET_UPVALUE:
		{
			uint8_t slot = READ_BYTE();
			*frame->closure->upvalues[slot]->location = peek(0);
			break;
		}

		case OP_GET_PROPERTY:
		{
			Value value = peek(0);
			ObjString* name = READ_STRING();

			switch (value.type) {
			case VAL_NUMBER:
			{
				if (strcmp(name->chars, "to_str") == 0) {
					pop();
					push(OBJ_VAL(doubleToObjString(AS_NUMBER(value))));
				}
				else {
					runtimeError("Unknown number property %s.", name->chars);
					return INTERPRET_RUNTIME_ERROR;
				}
				break;
			}
			case VAL_BOOL:
			{
				if (strcmp(name->chars, "to_str") == 0) {
					pop();
					push(OBJ_VAL(boolToObjString(AS_BOOL(value))));
				}
				else {
					runtimeError("Unknown bool property %s.", name->chars);
					return INTERPRET_RUNTIME_ERROR;
				}
				break;
			}
			case VAL_NIL:
			{
				if (strcmp(name->chars, "to_str") == 0) {
					pop();
					push(OBJ_VAL(takeString("nil", 3)));
				}
				else {
					runtimeError("Unknown nil property %s.", name->chars);
					return INTERPRET_RUNTIME_ERROR;
				}
				break;
			}
			case VAL_OBJ:
			{
				if (IS_INSTANCE(value)) {
					ObjInstance* instance = AS_INSTANCE(value);

					Value value;
					if (tableGet(&instance->fields, name, &value)) {
						pop(); //Instance.
						push(value);
						break;
					}

					if (!bindMethod(instance->_class, name)) {
						return INTERPRET_RUNTIME_ERROR;
					}
				}
				break;
			}

			default:
				runtimeError("Unknown property %s.", name->chars);
				return INTERPRET_RUNTIME_ERROR;
			}
			break;
		}

		case OP_SET_PROPERTY:
		{
			if (!IS_INSTANCE(peek(1))) {
				runtimeError("Only instances have fields.");
				return INTERPRET_RUNTIME_ERROR;
			}

			ObjInstance* instance = AS_INSTANCE(peek(1));
			tableSet(&instance->fields, READ_STRING(), peek(0));

			Value value = pop(0);
			pop();
			push(value);
			break;
		}

		case OP_CLOSE_UPVALUE:
			closeUpvalues(vm.stackTop - 1);
			pop();
			break;

		case OP_GET_SUPER:
		{
			ObjString* name = READ_STRING();
			ObjClass* superclass = AS_CLASS(pop());
			if (!bindMethod(superclass, name)) {
				return INTERPRET_RUNTIME_ERROR;
			}
			break;
		}

		case OP_SUPER_INVOKE:
		{
			ObjString* method = READ_STRING();
			int argCount = READ_BYTE();
			ObjClass* superclass = AS_CLASS(pop());
			if (!invokeFromClass(superclass, method, argCount)) {
				return INTERPRET_RUNTIME_ERROR;
			}
			frame = &vm.frames[vm.frameCount - 1];
			break;
		}

		case OP_DEFINE_CONSTANT:
		{
			ObjString* name = READ_STRING();
			Value existent;
			if (tableGet(&vm.globals, name, &existent)) {
				runtimeError("%s '%s' is already defined.", ((existent.isConstant) ? "Constant" : "Variable"), name->chars);
				return INTERPRET_RUNTIME_ERROR;
			}

			Value value = peek(0);
			value.isConstant = true;
			tableSet(&vm.globals, name, value);
			pop();
			break;
		}

		case OP_EQUAL:
		{
			Value a = pop();
			Value b = pop();
			push(BOOL_VAL(valuesEqual(a, b)));
			break;
		}

		case OP_GREATER:
			BINARY_OP(BOOL_VAL, >);
			break;
		case OP_LESS:
			BINARY_OP(BOOL_VAL, <);
			break;
		case OP_ADD:
		{
			if (IS_STRING(peek(0)) && IS_STRING(peek(1)))
			{
				concatenate();
			}
			else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1)))
			{
				double b = AS_NUMBER(pop());
				double a = AS_NUMBER(pop());
				push(NUMBER_VAL(a + b));
			}
			else
			{
				runtimeError("Operands must be two numbers or two strings.");
				return INTERPRET_RUNTIME_ERROR;
			}
			break;
		}
		case OP_SUBTRACT:
			BINARY_OP(NUMBER_VAL, -);
			break;
		case OP_MULTIPLY:
			BINARY_OP(NUMBER_VAL, *);
			break;
		case OP_DIVIDE:
			BINARY_OP(NUMBER_VAL, /);
			break;
		case OP_MODULO:
			MOD_OP();
			break;
		case OP_POWER:
			POWER_OP();
			break;
		case OP_NOT:
			push(BOOL_VAL(isFalsey(pop())));
			break;
		case OP_SHIFT_LEFT:
			BINARY_SHIFT_OP(<<);
			break;
		case OP_SHIFT_RIGHT:
			BINARY_SHIFT_OP(>>);
			break;

		case OP_NEGATE:
			if (!IS_NUMBER(peek(0)))
			{
				runtimeError("Operand must be a number.");
				return INTERPRET_RUNTIME_ERROR;
			}

			push(NUMBER_VAL(-AS_NUMBER(pop())));
			break;

		case OP_PRINT:
		{
			printValue(pop());
			printf("\n");
			break;
		}

		case OP_EXIT:
			return INTERPRET_OK;

		case OP_JUMP:
		{
			uint16_t offset = READ_SHORT();
			frame->ip += offset;
			break;
		}

		case OP_JUMP_IF_FALSE:
		{
			uint16_t offset = READ_SHORT();
			if (isFalsey(peek(0))) frame->ip += offset;
			break;
		}

		case OP_LOOP:
		{
			uint16_t offset = READ_SHORT();
			frame->ip -= offset;
			break;
		}

		case OP_CALL:
		{
			int argCount = READ_BYTE();
			if (!callValue(peek(argCount), argCount)) {
				return INTERPRET_RUNTIME_ERROR;
			}
			frame = &vm.frames[vm.frameCount - 1];
			break;
		}

		case OP_INVOKE:
		{
			ObjString* method = READ_STRING();
			int argCount = READ_BYTE();
			if (!invoke(method, argCount)) {
				return INTERPRET_RUNTIME_ERROR;
			}
			frame = &vm.frames[vm.frameCount - 1];
			break;
		}

		case OP_INHERIT:
		{
			Value superclass = peek(1);

			if (!IS_CLASS(superclass)) {
				runtimeError("Superclass must be a class.");
				return INTERPRET_RUNTIME_ERROR;
			}

			ObjClass* subclass = AS_CLASS(peek(0));
			tableAddAll(&AS_CLASS(superclass)->methods, &subclass->methods);
			pop(); //Subclass.
			break;
		}

		case OP_METHOD:
			defineMethod(READ_STRING());
			break;

		case OP_CLOSURE:
		{
			ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
			ObjClosure* closure = newClosure(function);
			push(OBJ_VAL(closure));
			for (int i = 0; i < closure->upvalueCount; i++) {
				uint8_t isLocal = READ_BYTE();
				uint8_t index = READ_BYTE();
				if (isLocal) {
					closure->upvalues[i] = captureUpvalue(frame->slots + index);
				}
				else {
					closure->upvalues[i] = frame->closure->upvalues[index];
				}
			}
			break;
		}

		case OP_CLASS:
		{
			push(OBJ_VAL(newClass(READ_STRING())));
			break;
		}

		case OP_RETURN:
		{
			Value result = pop();

			closeUpvalues(frame->slots);

			vm.frameCount--;
			if (vm.frameCount == 0) {
				pop();
				return INTERPRET_OK;
			}

			vm.stackTop = frame->slots;
			push(result);

			frame = &vm.frames[vm.frameCount - 1];
			break;
		}
		}
	}
#undef READ_BYTE
#undef READ_CONSTANT
#undef READ_SHORT
#undef BINARY_OP
#undef READ_STRING
#undef MOD_OP
#undef BINARY_SHIFT_OP
#undef POWER_OP
}