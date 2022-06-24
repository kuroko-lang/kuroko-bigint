/**
 * @file    module_bigint.c
 * @brief   Big ints, implemented through my own makeshift thing
 * @author  K. Lange <klange@toaruos.org>
 *
 */
#include <kuroko/vm.h>
#include <kuroko/util.h>

#define AS_LIB
#include "bigint.c"

static KrkClass * _long;

typedef KrkLong krk_long[1];

struct BigInt {
	KrkInstance inst;
	krk_long value;
};

#define AS_long(o) ((struct BigInt *)AS_OBJECT(o))
#define IS_long(o) (krk_isInstanceOf(o, _long))

#define CURRENT_CTYPE struct BigInt *
#define CURRENT_NAME  self

static void make_long(krk_integer_type t, struct BigInt * self) {
	krk_long_init_si(self->value, t);
}

static void _long_gcsweep(KrkInstance * self) {
	krk_long_clear(((struct BigInt*)self)->value);
}

KRK_METHOD(long,__init__,{
	METHOD_TAKES_AT_MOST(1);
	if (argc < 2) make_long(0,self);
	else if (IS_INTEGER(argv[1])) make_long(AS_INTEGER(argv[1]),self);
	//else if (IS_FLOATING(argv[1])) make_from_double(AS_FLOATING(argv[1]),self);
	else if (IS_BOOLEAN(argv[1])) make_long(AS_BOOLEAN(argv[1]),self);
	else if (IS_STRING(argv[1])) krk_long_parse_string(AS_CSTRING(argv[1]),self->value);
	else return krk_runtimeError(vm.exceptions->typeError, "%s() argument must be a string or a number, not '%s'", "int", krk_typeName(argv[1]));
	return argv[0];
})

#if 0
KRK_METHOD(long,__float__,{
	return FLOATING_VAL(krk_long_get_double(self->value));
})
#endif

#define PRINTER(name,base,prefix) \
	KRK_METHOD(long,__ ## name ## __,{ \
		size_t size; \
		char * rev = krk_long_to_str(self->value, base, prefix, &size); \
		return OBJECT_VAL(krk_takeString(rev,size)); \
	})

PRINTER(str,10,"")
PRINTER(hex,16,"x0")
PRINTER(oct,8,"o0")
PRINTER(bin,2,"b0")

KRK_METHOD(long,__hash__,{
	return INTEGER_VAL((uint32_t)(krk_long_medium(self->value)));
})

static KrkValue make_long_obj(krk_long val) {
	krk_push(OBJECT_VAL(krk_newInstance(_long)));
	*AS_long(krk_peek(0))->value = *val;
	return krk_pop();
}

KRK_METHOD(long,__int__,{
	return INTEGER_VAL(krk_long_medium(self->value));
})

#define BASIC_BIN_OP(name, long_func) \
	KRK_METHOD(long,__ ## name ## __,{ \
		krk_long tmp; \
		if (IS_long(argv[1])) krk_long_init_copy(tmp, AS_long(argv[1])->value); \
		else if (IS_INTEGER(argv[1])) krk_long_init_si(tmp, AS_INTEGER(argv[1])); \
		else return NOTIMPL_VAL(); \
		long_func(tmp,self->value,tmp); \
		return make_long_obj(tmp); \
	}) \
	KRK_METHOD(long,__r ## name ## __,{ \
		krk_long tmp; \
		if (IS_long(argv[1])) krk_long_init_copy(tmp, AS_long(argv[1])->value); \
		else if (IS_INTEGER(argv[1])) krk_long_init_si(tmp, AS_INTEGER(argv[1])); \
		else return NOTIMPL_VAL(); \
		long_func(tmp,tmp,self->value); \
		return make_long_obj(tmp); \
	})

BASIC_BIN_OP(add,krk_long_add)
BASIC_BIN_OP(sub,krk_long_sub)
BASIC_BIN_OP(mul,krk_long_mul)
BASIC_BIN_OP(or, krk_long_or)
BASIC_BIN_OP(xor,krk_long_xor)
BASIC_BIN_OP(and,krk_long_and)

static void _krk_long_lshift(krk_long out, krk_long val, krk_long shift) {
	if (krk_long_sign(shift) < 0) { krk_runtimeError(vm.exceptions->valueError, "negative shift"); return; }
	krk_long multiplier;
	krk_long_init_si(multiplier,0);
	krk_long_bit_set(multiplier, krk_long_medium(shift));
	krk_long_mul(out,val,multiplier);
	krk_long_clear(multiplier);
}

static void _krk_long_rshift(krk_long out, krk_long val, krk_long shift) {
	if (krk_long_sign(shift) < 0) { krk_runtimeError(vm.exceptions->valueError, "negative shift"); return; }
	krk_long multiplier, garbage;
	krk_long_init_many(multiplier,garbage,NULL);
	krk_long_bit_set(multiplier, krk_long_medium(shift));
	krk_long_div_rem(out,garbage,val,multiplier);
	krk_long_clear_many(multiplier,garbage,NULL);
}

static void _krk_long_mod(krk_long out, krk_long a, krk_long b) {
	if (krk_long_sign(b) == 0) { krk_runtimeError(vm.exceptions->valueError, "modulo by zero"); return; }
	krk_long garbage;
	krk_long_init_si(garbage,0);
	krk_long_div_rem(garbage,out,a,b);
	krk_long_clear(garbage);
}

static void _krk_long_div(krk_long out, krk_long a, krk_long b) {
	if (krk_long_sign(b) < 0) { krk_runtimeError(vm.exceptions->valueError, "division by zero"); return; }
	krk_long garbage;
	krk_long_init_si(garbage,0);
	krk_long_div_rem(out,garbage,a,b);
	krk_long_clear(garbage);
}

BASIC_BIN_OP(lshift,_krk_long_lshift)
BASIC_BIN_OP(rshift,_krk_long_rshift)
BASIC_BIN_OP(mod,_krk_long_mod)
BASIC_BIN_OP(floordiv,_krk_long_div)

#define COMPARE_OP(name, comp) \
	KRK_METHOD(long,__ ## name ## __,{ \
		krk_long tmp; \
		if (IS_long(argv[1])) krk_long_init_copy(tmp, AS_long(argv[1])->value); \
		else if (IS_INTEGER(argv[1])) krk_long_init_si(tmp, AS_INTEGER(argv[1])); \
		else return NOTIMPL_VAL(); \
		int cmp = krk_long_compare(self->value,tmp); \
		krk_long_clear(tmp); \
		return BOOLEAN_VAL(cmp comp 0); \
	})

COMPARE_OP(lt, <)
COMPARE_OP(gt, >)
COMPARE_OP(le, <=)
COMPARE_OP(ge, >=)
COMPARE_OP(eq, ==)

#undef BASIC_BIN_OP
#undef COMPARE_OP

KRK_METHOD(long,__len__,{
	return INTEGER_VAL(krk_long_sign(self->value));
})

#undef BIND_METHOD
#define BIND_METHOD(klass,method) do { krk_defineNative(& _ ## klass->methods, #method, _ ## klass ## _ ## method); } while (0)
KrkValue krk_module_onload_bigint(void) {
	KrkInstance * module = krk_newInstance(vm.baseClasses->moduleClass);
	krk_push(OBJECT_VAL(module));

	KRK_DOC(module, "Very large integers.");

	krk_makeClass(module, &_long, "long", vm.baseClasses->intClass);
	_long->allocSize = sizeof(struct BigInt);
	_long->_ongcsweep = _long_gcsweep;

	BIND_METHOD(long,__init__);
	BIND_METHOD(long,__str__);
	BIND_METHOD(long,__eq__);
	BIND_METHOD(long,__hash__);
	BIND_METHOD(long,__hex__);
	BIND_METHOD(long,__oct__);
	BIND_METHOD(long,__bin__);
	BIND_METHOD(long,__int__);
	BIND_METHOD(long,__len__);
	krk_defineNative(&_long->methods,"__repr__", FUNC_NAME(long,__str__));

#define BIND_TRIPLET(name) \
	BIND_METHOD(long,__ ## name ## __); \
	BIND_METHOD(long,__r ## name ## __); \
	krk_defineNative(&_long->methods,"__i" #name "__",_long___ ## name ## __);
	BIND_TRIPLET(add);
	BIND_TRIPLET(sub);
	BIND_TRIPLET(mul);
	BIND_TRIPLET(or);
	BIND_TRIPLET(xor);
	BIND_TRIPLET(and);
	BIND_TRIPLET(lshift);
	BIND_TRIPLET(rshift);
	BIND_TRIPLET(mod);
	//BIND_TRIPLET(truediv);
	BIND_TRIPLET(floordiv);
#undef BIND_TRIPLET

	BIND_METHOD(long,__lt__);
	BIND_METHOD(long,__gt__);
	BIND_METHOD(long,__le__);
	BIND_METHOD(long,__ge__);

	krk_finalizeClass(_long);

	return krk_pop();
}


