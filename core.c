#include "core.h"
#include "value.h"
#include "object.h"
#include "table.h"
#include "memory.h"

#include <string.h>
#include <stdio.h>

static inline void
define_on_table(VM* vm, Table* table, const char* name, Value value) {
    vm_push_root(vm, value);
    Value key = OBJ_TO_VAL(objstring_copy(vm, name, strlen(name)));
    vm_push_root(vm, key);

    table_set(table, vm, key, value);
    vm_pop_root(vm);
    vm_pop_root(vm);
}

#define DEFINE_NATIVE(name) \
    static bool name(VM* vm, Value* args, int num_args)

#define ARG_ERROR(arg_idx, msg) \
    do { \
        if ((arg_idx) == 0) \
            ERROR("%s expected 'this' to be %s.", __func__, msg); \
        else \
            ERROR("%s expected arg %d to be %s.", __func__, (arg_idx)-1, msg); \
    } while (false)

#define ARGSPEC(spec) do { \
        for (int i=0; i < strlen(spec); i++) \
            ARG_CHECK_SINGLE(vm, (spec)[i], i); \
    } while(false)

#define ARG_CHECK_SINGLE(vm, ch, idx) do { \
    if (num_args < (idx)) \
        ERROR("%s expected %d args, got %d instead.", __func__, idx, num_args); \
    Value arg = args[idx]; \
    switch (ch) { \
        case 'O': if (!IS_OBJECT(arg)) ARG_ERROR(idx, "an Object"); break; \
        case 'S': if (!IS_STRING(arg)) ARG_ERROR(idx, "a String"); break; \
        case 'N': if (!IS_NUMBER(arg)) ARG_ERROR(idx, "a Number"); break; \
        case 'n': if (!IS_NATIVE(arg)) ARG_ERROR(idx, "a Native"); break; \
        case 'F': if (!IS_CLOSURE(arg)) ARG_ERROR(idx, "an Fn"); break; \
        case 'f': if (!IS_FIBER(arg)) ARG_ERROR(idx, "a Fiber"); break; \
        case 'r': if (!IS_RANGE(arg)) ARG_ERROR(idx, "a Range"); break; \
        case '*': break; \
        default: UNREACHABLE(); \
    } \
    } while(false)

#define ERROR(...) \
    do { \
        vm_runtime_error(vm, __VA_ARGS__); \
        return false; \
    } while (false)

#define RETURN(EXPR) \
    do { \
        *(vm->fiber->stack_top - num_args - 1) = EXPR; \
        vm_drop(vm, num_args); \
        return true; \
    } while (false)


// ============================= Object =============================

DEFINE_NATIVE(Object_proto) {
    Value proto = vm_get_prototype(vm, args[0]);
    RETURN(proto);
}

DEFINE_NATIVE(Object_setProto) {
    ARGSPEC("O*");

    ObjObject* object = VAL_TO_OBJECT(args[0]);
    object->proto = args[1];
    RETURN(NIL_VAL);
}

DEFINE_NATIVE(Object_rawGetSlot) {
    ARGSPEC("**");

    Value this = args[0];
    Value slot;
    if (!vm_get_slot(vm, this, args[1], &slot))
        slot = NIL_VAL;
    RETURN(slot);
}

DEFINE_NATIVE(Object_setSlot) {
    ARGSPEC("O**");

    objobject_set(VAL_TO_OBJECT(args[0]), vm, args[1], args[2]);
    RETURN(NIL_VAL);
}

DEFINE_NATIVE(Object_hasSlot) {
    ARGSPEC("**");

    Value slot;
    bool has_slot = vm_get_slot(vm, args[0], args[1], &slot);
    RETURN(BOOL_TO_VAL(has_slot));
}

DEFINE_NATIVE(Object_getOwnSlot) {
    ARGSPEC("**");

    if (!IS_OBJECT(args[0]))
        RETURN(NIL_VAL);

    ObjObject* this = VAL_TO_OBJECT(args[0]);
    Value rv;
    if (objobject_get(this, args[1], &rv)) {
        RETURN(rv);
    }
    RETURN(NIL_VAL);
}

DEFINE_NATIVE(Object_hasOwnSlot) {
    ARGSPEC("**");

    if (!IS_OBJECT(args[0]))
        return false;

    ObjObject* this = VAL_TO_OBJECT(args[0]);
    RETURN(BOOL_TO_VAL(objobject_has(this, args[1])));
}

DEFINE_NATIVE(Object_deleteSlot) {
    ARGSPEC("O*");

    ObjObject* this = VAL_TO_OBJECT(args[0]);
    bool has_slot = objobject_delete(this, vm, args[1]);
    RETURN(BOOL_TO_VAL(has_slot));
}

DEFINE_NATIVE(Object_same) {
    ARGSPEC("***");
    RETURN(BOOL_TO_VAL(value_equal(args[1], args[2])));
}

DEFINE_NATIVE(Object_equal) {
    ARGSPEC("**");
    RETURN(BOOL_TO_VAL(value_equal(args[0], args[1])));
}

DEFINE_NATIVE(Object_notEqual) {
    ARGSPEC("**");
    RETURN(BOOL_TO_VAL(!value_equal(args[0], args[1])));
}

DEFINE_NATIVE(Object_not) {
    RETURN(BOOL_TO_VAL(!value_truthy(args[0])));
}

DEFINE_NATIVE(Object_clone) {
    ObjObject* obj = objobject_new(vm);
    obj->proto = args[0];
    RETURN(OBJ_TO_VAL(obj));
}

static bool
has_ancestor(VM* vm, Value src, Value target)
{
    bool rv;
    if (value_equal(src, target)) return true;
    if (IS_OBJ(src)) {
        Obj* obj = VAL_TO_OBJ(src);
        if (obj->visited) return false;
        obj->visited = true;
    }
    rv = has_ancestor(vm, vm_get_prototype(vm, src), target);
    if (IS_OBJ(src))
        VAL_TO_OBJ(src)->visited = false;
    return rv;
}

// obj.hasAncestor(x) returns true if the obj has x anywhere
// on obj's prototype chain (including obj itself, i.e.
// obj.hasAncestor(obj) is always true).
DEFINE_NATIVE(Object_hasAncestor) {
    ARGSPEC("**");
    RETURN(BOOL_TO_VAL(has_ancestor(vm, args[0], args[1])));
}

DEFINE_NATIVE(Object_toString) {
    Value this = args[0];

    ssize_t buf_size;
    char* buffer;
    ObjString* str;

    switch (this.type) {
    case VALUE_NIL:  RETURN(OBJ_TO_VAL(objstring_copy(vm, "nil", 3)));
    case VALUE_TRUE: RETURN(OBJ_TO_VAL((Obj*)objstring_copy(vm, "true", 4)));
    case VALUE_FALSE: RETURN(OBJ_TO_VAL((Obj*)objstring_copy(vm, "false", 5)));
    case VALUE_NUMBER:
        buf_size = snprintf(NULL, 0, "%g", VAL_TO_NUMBER(this)) + 1;
        buffer = ALLOCATE_ARRAY(vm, char, buf_size);
        snprintf(buffer, buf_size, "%g", VAL_TO_NUMBER(this));
        break;
    case VALUE_OBJ: {
        Obj* obj = VAL_TO_OBJ(this);
        const char* fmt;
        switch (obj->type) {
        case OBJ_STRING:  RETURN(this);
        case OBJ_CLOSURE: fmt = "Fn_%p"; break;
        case OBJ_OBJECT:  fmt = "Object_%p"; break;
        case OBJ_NATIVE:  fmt = "Native_%p"; break;
        case OBJ_FIBER:   fmt = "Fiber_%p"; break;
        default: UNREACHABLE();
        }
        buf_size = snprintf(NULL, 0, fmt, (void*) obj) + 1;
        buffer = ALLOCATE_ARRAY(vm, char, buf_size);
        snprintf(buffer, buf_size, fmt, (void*) obj);
        break;
    }
    default:
        UNREACHABLE();
    }

    str = objstring_copy(vm, buffer, buf_size - 1 /* exclude the \0 byte */);
    FREE_ARRAY(vm, buffer, char, buf_size);
    RETURN(OBJ_TO_VAL(str));
}

DEFINE_NATIVE(Object_print) {
    Value this = args[0];
    InterpretResult rv;
    vm_ensure_stack(vm, 1);
    vm_push(vm, this);
    if (!vm_invoke(vm, this, OBJ_TO_VAL(objstring_copy(vm, "toString", 8)), 0, &rv))
        return rv;

    Value string_slot = vm_pop(vm);
    if (!IS_STRING(string_slot))
        ERROR("Object_print expected .toString to be a string.");
    fprintf(stdout, "%s", VAL_TO_STRING(string_slot)->chars);
    fflush(stdout);
    RETURN(NIL_VAL);
}

DEFINE_NATIVE(Object_println) {
    Value this = args[0];
    InterpretResult rv;
    vm_ensure_stack(vm, 1);
    vm_push(vm, this);
    if (!vm_invoke(vm, this, OBJ_TO_VAL(objstring_copy(vm, "print", 5)), 0, &rv))
        return rv;
    vm_pop(vm);

    fprintf(stdout, "\n");
    fflush(stdout);
    RETURN(NIL_VAL);
}

// ============================= Fn =============================

DEFINE_NATIVE(Fn_new) {
    ARGSPEC("*F");
    RETURN(args[1]);
}

DEFINE_NATIVE(Fn_call) {
    ARGSPEC("F");
    vm_push_frame(vm, VAL_TO_CLOSURE(args[0]), num_args);
    return true;
}

DEFINE_NATIVE(Fn_callWithThis) {
    ARGSPEC("F*");
    // Shift the arguments, so that we set up the stack properly.
    //            0    1         2      3            num_args
    // We have: | fn | newThis | arg1 | arg2 | ... | arg_{num_args} |
    // we want:      | newThis | arg1 | arg2 | ... | arg_{num_args} |
    //                 0         1      2            num_args-1
    ObjClosure* closure = VAL_TO_CLOSURE(args[0]);
    for (int i = 0; i < num_args; i++)
        args[i] = args[i + 1];
    vm_pop(vm); // this will be a duplicate
    vm_push_frame(vm, closure, num_args - 1);
    return true;
}

// ============================= Native =============================

DEFINE_NATIVE(Native_call) {
    ARGSPEC("n");

    ObjNative* native = VAL_TO_NATIVE(args[0]);
    return native->fn(vm, args, num_args);
}

DEFINE_NATIVE(Native_callWithThis) {
    ARGSPEC("n*");

    ObjNative* native = VAL_TO_NATIVE(args[0]);
    for (int i = 0; i < num_args; i++)
        args[i] = args[i + 1];
    vm_pop(vm);
    return native->fn(vm, args, num_args - 1);
}

// ============================= Number =============================

#define DEFINE_NUMBER_METHOD(name, cast_to, op, return_type) \
    DEFINE_NATIVE(Number_##name) {\
        ARGSPEC("NN"); \
        cast_to a = (cast_to) VAL_TO_NUMBER(args[0]); \
        cast_to b = (cast_to) VAL_TO_NUMBER(args[1]); \
        RETURN(return_type(a op b)); \
    }

DEFINE_NUMBER_METHOD(plus,     double, +,  NUMBER_TO_VAL)
DEFINE_NUMBER_METHOD(minus,    double, -,  NUMBER_TO_VAL)
DEFINE_NUMBER_METHOD(multiply, double, *,  NUMBER_TO_VAL)
DEFINE_NUMBER_METHOD(divide,   double, /,  NUMBER_TO_VAL)
DEFINE_NUMBER_METHOD(lt,       double, <,  BOOL_TO_VAL)
DEFINE_NUMBER_METHOD(gt,       double, >,  BOOL_TO_VAL)
DEFINE_NUMBER_METHOD(leq,      double, <=, BOOL_TO_VAL)
DEFINE_NUMBER_METHOD(geq,      double, >=, BOOL_TO_VAL)
DEFINE_NUMBER_METHOD(lor,      int32_t, |, NUMBER_TO_VAL)
DEFINE_NUMBER_METHOD(land,     int32_t, &, NUMBER_TO_VAL)
#undef DEFINE_NUMBER_METHOD

DEFINE_NATIVE(Number_negate) {
    ARGSPEC("N");

    RETURN(NUMBER_TO_VAL(-VAL_TO_NUMBER(args[0])));
}

DEFINE_NATIVE(Number_inclusiveRange) {
    ARGSPEC("N");
    double start = VAL_TO_NUMBER(args[0]);
    double end = VAL_TO_NUMBER(args[1]);
    RETURN(OBJ_TO_VAL(objrange_new(vm, start, end + 1)));
}

DEFINE_NATIVE(Number_exclusiveRange) {
    ARGSPEC("N");
    double start = VAL_TO_NUMBER(args[0]);
    double end = VAL_TO_NUMBER(args[1]);
    RETURN(OBJ_TO_VAL(objrange_new(vm, start, end)));
}

// ============================= String =============================

#define DEFINE_STRING_METHOD(name, op) \
    DEFINE_NATIVE(String_##name) {\
        ARGSPEC("SS"); \
        const char* a = VAL_TO_STRING(args[0])->chars; \
        const char* b = VAL_TO_STRING(args[1])->chars; \
        int cmp = strcmp(a, b); \
        RETURN(BOOL_TO_VAL(cmp op 0)); \
    }

DEFINE_STRING_METHOD(lt, <)
DEFINE_STRING_METHOD(gt, >)
DEFINE_STRING_METHOD(leq, <=)
DEFINE_STRING_METHOD(geq, >=)

DEFINE_NATIVE(String_plus) {
    ARGSPEC("SS");
    RETURN(OBJ_TO_VAL(objstring_concat(vm,
        VAL_TO_STRING(args[0]),
        VAL_TO_STRING(args[1])
        )));
}

DEFINE_NATIVE(String_length) {
    ARGSPEC("S");
    RETURN(NUMBER_TO_VAL(VAL_TO_STRING(args[0])->length));
}

// ============================= Fiber =============================

// Run the given `fiber`, transferring `value`.
static bool
run_fiber(VM* vm, ObjFiber* fiber,
          Value value, const char* verb)
{
    if (fiber->error != NULL)    ERROR("Cannot %s a fiber with an error.", verb);
    if (objfiber_is_done(fiber)) ERROR("Cannot %s a finished fiber.", verb);
    if (fiber->parent != NULL)   ERROR("Cannot %s a fiber with a parent.", verb);

    if (fiber->frames_count == 1
        && fiber->frames[0].ip == fiber->frames[0].closure->function->chunk.code) {
        if (fiber->frames[0].closure->function->arity == 1) {
            // The fiber has not ran yet, and is expecting some
            // data to be sent.
            *fiber->stack_top = value;
            fiber->stack_top++;
        }
    } else {
        // We're resuming the `fiber`. In this case, `fiber`'s stack
        // will be like so, since it has already been suspended (e.g.
        // from a Fiber.call or Fiber.yield).
        //   +---+-----------+
        //   |...| Fiber_... |
        //   +---+-----------+
        //                   ^-- stack_top
        // Replacing stack_top[-1] gives Fiber.call or Fiber.yield
        // a return value.
        fiber->stack_top[-1] = value;
    }
    fiber->parent = vm->fiber;
    vm->fiber = fiber;
    return true;
}

DEFINE_NATIVE(Fiber_current) {
    RETURN(OBJ_TO_VAL(vm->fiber));
}

DEFINE_NATIVE(Fiber_yield) {
    if (!vm->can_yield) {
        ERROR("Cannot yield from fiber.");
    }
    Value v = NIL_VAL;
    if (num_args >= 1) {
        vm_drop(vm, num_args - 1);
        v = vm_pop(vm);
    }
    ObjFiber* parent = vm->fiber->parent;
    vm->fiber->state = FIBER_OTHER;
    vm->fiber->parent = NULL;
    vm->fiber = parent;
    if (vm->fiber != NULL) {
        vm->fiber->stack_top[-1] = v;
    }
    return true;
}

DEFINE_NATIVE(Fiber_abort) {
    ARGSPEC("*S");
    vm->fiber->error = VAL_TO_STRING(vm_pop(vm));
    return true;
}

DEFINE_NATIVE(Fiber_new) {
    ARGSPEC("*F");
    ObjClosure* closure = VAL_TO_CLOSURE(args[1]);
    if (closure->function->arity != 0
        && closure->function->arity != 1) {
        ERROR("Cannot create fiber from function with arity %d.", closure->function->arity);
    }
    ObjFiber* fiber = objfiber_new(vm, closure);
    RETURN(OBJ_TO_VAL(fiber));
}

DEFINE_NATIVE(Fiber_parent) {
    ARGSPEC("f");
    RETURN(vm->fiber->parent != NULL
            ? OBJ_TO_VAL(vm->fiber->parent)
            : NIL_VAL);
}

DEFINE_NATIVE(Fiber_call) {
    ARGSPEC("f");
    ObjFiber* fiber = VAL_TO_FIBER(args[0]);
    Value v = NIL_VAL;
    if (num_args >= 1) {
        vm_drop(vm, num_args - 1);
        v = vm_pop(vm);
    }
    return run_fiber(vm, fiber, v, "call");
}

DEFINE_NATIVE(Fiber_try) {
    ARGSPEC("f");
    ObjFiber* fiber = VAL_TO_FIBER(args[0]);
    Value v = NIL_VAL;
    if (num_args >= 1) {
        vm_drop(vm, num_args - 1);
        v = vm_pop(vm);
    }
    if (run_fiber(vm, fiber, v, "try")) {
        vm->fiber->state = FIBER_TRY;
        return true;
    }
    return false;
}

DEFINE_NATIVE(Fiber_error) {
    ARGSPEC("f");
    ObjFiber* fiber = VAL_TO_FIBER(args[0]);
    RETURN(fiber->error != NULL
            ? OBJ_TO_VAL(fiber->error)
            : NIL_VAL);
}

DEFINE_NATIVE(Fiber_isDone) {
    ARGSPEC("f");
    ObjFiber* fiber = VAL_TO_FIBER(args[0]);
    RETURN(BOOL_TO_VAL(objfiber_is_done(fiber)));
}

// ============================= Range =============================

DEFINE_NATIVE(Range_iterMore) {
    ARGSPEC("r");
    ObjRange* range = VAL_TO_RANGE(args[0]);
    RETURN(BOOL_TO_VAL(range->current < range->end));
}

DEFINE_NATIVE(Range_iterNext) {
    ARGSPEC("r");
    ObjRange* range = VAL_TO_RANGE(args[0]);
    double current = range->current;
    range->current++;
    RETURN(NUMBER_TO_VAL(current));
}

void core_init_vm(VM* vm)
{
#define ADD_OBJECT(table, name, obj) (define_on_table(vm, table, name, OBJ_TO_VAL(obj)))
#define ADD_NATIVE(table, name, fn)  (ADD_OBJECT(table, name, objnative_new(vm, fn)))
#define ADD_METHOD(PROTO, name, fn)  (ADD_NATIVE(&vm->PROTO->slots, name, fn))

    vm->getSlot_string = OBJ_TO_VAL(objstring_copy(vm, "getSlot", 7));
    vm->setSlot_string = OBJ_TO_VAL(objstring_copy(vm, "setSlot", 7));

    vm->ObjectProto = objobject_new(vm);
    ADD_METHOD(ObjectProto, "proto",       Object_proto);
    ADD_METHOD(ObjectProto, "setProto",    Object_setProto);
    ADD_METHOD(ObjectProto, "rawGetSlot",  Object_rawGetSlot);
    ADD_METHOD(ObjectProto, "rawSetSlot",  Object_setSlot);
    ADD_METHOD(ObjectProto, "setSlot",     Object_setSlot);
    ADD_METHOD(ObjectProto, "hasSlot",     Object_hasSlot);
    ADD_METHOD(ObjectProto, "getOwnSlot",  Object_getOwnSlot);
    ADD_METHOD(ObjectProto, "setOwnSlot",  Object_setSlot);
    ADD_METHOD(ObjectProto, "hasOwnSlot",  Object_hasOwnSlot);
    ADD_METHOD(ObjectProto, "deleteSlot",  Object_deleteSlot);
    ADD_METHOD(ObjectProto, "same",        Object_same);
    ADD_METHOD(ObjectProto, "==",          Object_equal);
    ADD_METHOD(ObjectProto, "!=",          Object_notEqual);
    ADD_METHOD(ObjectProto, "!",           Object_not);
    ADD_METHOD(ObjectProto, "clone",       Object_clone);
    ADD_METHOD(ObjectProto, "hasAncestor", Object_hasAncestor);
    ADD_METHOD(ObjectProto, "toString",    Object_toString);
    ADD_METHOD(ObjectProto, "print",       Object_print);
    ADD_METHOD(ObjectProto, "println",     Object_println);

    vm->FnProto = objobject_new(vm);
    vm->FnProto->proto = OBJ_TO_VAL(vm->ObjectProto);
    ADD_METHOD(FnProto, "new",          Fn_new);
    ADD_METHOD(FnProto, "call",         Fn_call);
    ADD_METHOD(FnProto, "callWithThis", Fn_callWithThis);

    vm->NativeProto = objobject_new(vm);
    vm->NativeProto->proto = OBJ_TO_VAL(vm->ObjectProto);
    ADD_METHOD(NativeProto, "call",         Native_call);
    ADD_METHOD(NativeProto, "callWithThis", Native_callWithThis);

    vm->NumberProto = objobject_new(vm);
    vm->NumberProto->proto = OBJ_TO_VAL(vm->ObjectProto);
    ADD_METHOD(NumberProto, "+",     Number_plus);
    ADD_METHOD(NumberProto, "-",     Number_minus);
    ADD_METHOD(NumberProto, "*",     Number_multiply);
    ADD_METHOD(NumberProto, "/",     Number_divide);
    ADD_METHOD(NumberProto, "<",     Number_lt);
    ADD_METHOD(NumberProto, ">",     Number_gt);
    ADD_METHOD(NumberProto, "<=",    Number_leq);
    ADD_METHOD(NumberProto, ">=",    Number_geq);
    ADD_METHOD(NumberProto, "neg",   Number_negate);
    ADD_METHOD(NumberProto, "|",     Number_lor);
    ADD_METHOD(NumberProto, "&",     Number_land);
    ADD_METHOD(NumberProto, "..",    Number_inclusiveRange);
    ADD_METHOD(NumberProto, "...",   Number_exclusiveRange);

    vm->StringProto = objobject_new(vm);
    vm->StringProto->proto = OBJ_TO_VAL(vm->ObjectProto);
    ADD_METHOD(StringProto, "+",      String_plus);
    ADD_METHOD(StringProto, "length", String_length);
    ADD_METHOD(StringProto, "<",      String_lt);
    ADD_METHOD(StringProto, ">",      String_gt);
    ADD_METHOD(StringProto, "<=",     String_leq);
    ADD_METHOD(StringProto, ">=",     String_geq);

    vm->FiberProto = objobject_new(vm);
    vm->FiberProto->proto = OBJ_TO_VAL(vm->ObjectProto);
    ADD_METHOD(FiberProto, "current", Fiber_current);
    ADD_METHOD(FiberProto, "yield",   Fiber_yield);
    ADD_METHOD(FiberProto, "abort",   Fiber_abort);
    ADD_METHOD(FiberProto, "new",     Fiber_new);
    ADD_METHOD(FiberProto, "parent",  Fiber_parent);
    ADD_METHOD(FiberProto, "call",    Fiber_call);
    ADD_METHOD(FiberProto, "try",     Fiber_try);
    ADD_METHOD(FiberProto, "isDone",  Fiber_isDone);
    ADD_METHOD(FiberProto, "error",   Fiber_error);

    vm->RangeProto = objobject_new(vm);
    vm->RangeProto->proto = OBJ_TO_VAL(vm->ObjectProto);
    ADD_METHOD(RangeProto, "iterNext", Range_iterNext);
    ADD_METHOD(RangeProto, "iterMore", Range_iterMore);

    ADD_OBJECT(&vm->globals, "Object",  vm->ObjectProto);
    ADD_OBJECT(&vm->globals, "Fn",      vm->FnProto);
    ADD_OBJECT(&vm->globals, "Native",  vm->NativeProto);
    ADD_OBJECT(&vm->globals, "Number",  vm->NumberProto);
    ADD_OBJECT(&vm->globals, "String",  vm->StringProto);
    ADD_OBJECT(&vm->globals, "Fiber",   vm->FiberProto);
    ADD_OBJECT(&vm->globals, "Range",   vm->RangeProto);

#undef ADD_OBJECT
#undef ADD_NATIVE
#undef ADD_METHOD
}
