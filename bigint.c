#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>

#define DIGIT_SHIFT 31
#define DIGIT_MAX   0x7FFFFFFF

struct BigInteger {
	ssize_t    width;
	uint32_t *digits;
};

typedef struct BigInteger KrkLong;

int krk_long_init_si(KrkLong * num, int64_t val) {
	if (val == 0) {
		num->width = 0;
		num->digits = NULL;
		return 0;
	}

	int sign = (val < 0) ? -1 : 1;
	uint64_t abs = (val < 0) ? -val : val;

	if (abs <= DIGIT_MAX) {
		num->width = sign;
		num->digits = malloc(sizeof(uint32_t));
		num->digits[0] = abs;
		return 0;
	}

	uint64_t tmp = abs;
	int64_t cnt = 1;

	while (tmp > DIGIT_MAX) {
		cnt++;
		tmp >>= DIGIT_SHIFT;
	}

	num->width = cnt * sign;
	num->digits = malloc(sizeof(uint32_t) * cnt);

	for (int64_t i = 0; i < cnt; ++i) {
		num->digits[i] = (abs & DIGIT_MAX);
		abs >>= DIGIT_SHIFT;
	}

	return 0;
}

int krk_long_clear(KrkLong * num) {
	if (num->width) free(num->digits);
	num->width = 0;
	num->digits = NULL;
	return 0;
}

int krk_long_clear_many(KrkLong *a, ...) {
	va_list argp;
	va_start(argp, a);

	KrkLong * next = a;
	while (next) {
		krk_long_clear(next);
		next = va_arg(argp, KrkLong *);
	}

	va_end(argp);
	return 0;
}

static void printnum(FILE * out, KrkLong * a) {
	fprintf(out, "[%ld,(", a->width);

	size_t abs_width = a->width < 0 ? -a->width : a->width;

	for (size_t i = 0; i < abs_width; ++i) {
		fprintf(out, "%#x", a->digits[i]);
		if (i + 1 != abs_width) {
			fprintf(out, ",");
		}
	}

	fprintf(out, ")]");
}

int krk_long_init_copy(KrkLong * out, KrkLong * in) {
	size_t abs_width = in->width < 0 ? -in->width : in->width;
	out->width = in->width;
	out->digits = out->width ? malloc(sizeof(uint32_t) * abs_width) : NULL;
	for (size_t i = 0; i < abs_width; ++i) {
		out->digits[i] = in->digits[i];
	}
	return 0;
}

int krk_long_resize(KrkLong * num, ssize_t newdigits) {
	if (newdigits == 0) {
		krk_long_clear(num);
		return 0;
	}

	size_t abs = newdigits < 0 ? -newdigits : newdigits;
	if (num->width == 0) {
		num->digits = malloc(sizeof(uint32_t) * newdigits);
	} else {
		num->digits = realloc(num->digits, sizeof(uint32_t) * newdigits);
	}

	num->width = newdigits;
	return 0;
}

static int krk_long_trim(KrkLong * num) {
	size_t owidth = num->width < 0 ? -num->width : num->width;
	size_t redundant = 0;
	for (size_t i = 0; i < owidth; i++) {
		if (num->digits[owidth-i-1] == 0) {
			redundant++;
		} else {
			break;
		}
	}

	if (redundant) {
		krk_long_resize(num, owidth - redundant);
	}
}

static int krk_long_compare(KrkLong * a, KrkLong * b) {
	if (a->width > b->width) return -1;
	if (b->width > a->width) return 1;
	size_t abs_width = a->width < 0 ? -a->width : a->width;
	for (size_t i = 0; i < abs_width; ++i) {
		if (a->digits[abs_width-i-1] > b->digits[abs_width-i-1]) return -1; /* left is bigger */
		if (a->digits[abs_width-i-1] < b->digits[abs_width-i-1]) return 1; /* right is bigger */
	}
	return 0; /* they are the same */
}

static int krk_long_compare_abs(KrkLong * a, KrkLong * b) {
	size_t a_width = a->width < 0 ? -a->width : a->width;
	size_t b_width = b->width < 0 ? -b->width : b->width;
	if (a_width > b_width) return -1;
	if (b_width > a_width) return 1;
	size_t abs_width = a_width;
	for (size_t i = 0; i < abs_width; ++i) {
		if (a->digits[abs_width-i-1] > b->digits[abs_width-i-1]) return -1; /* left is bigger */
		if (a->digits[abs_width-i-1] < b->digits[abs_width-i-1]) return 1; /* right is bigger */
	}
	return 0; /* they are the same */
}

static int krk_long_set_sign(KrkLong * num, int sign) {
	num->width = num->width < 0 ? (-num->width) * sign : num->width * sign;
	return 0;
}

static int krk_long_add_ignore_sign(KrkLong * res, KrkLong * a, KrkLong * b) {
	size_t awidth = a->width < 0 ? -a->width : a->width;
	size_t bwidth = b->width < 0 ? -b->width : b->width;
	size_t owidth = awidth < bwidth ? bwidth + 1 : awidth + 1;
	size_t carry  = 0;
	krk_long_resize(res, owidth);
	for (size_t i = 0; i < owidth - 1; ++i) {
		uint32_t out = (i < awidth ? a->digits[i] : 0) + (i < bwidth ? b->digits[i] : 0) + carry;
		res->digits[i] = out & DIGIT_MAX;
		carry = out > DIGIT_MAX;
	}
	if (carry) {
		res->digits[owidth-1] = 1;
	} else {
		krk_long_resize(res, owidth - 1);
	}
	return 0;
}

static int _sub_big_small(KrkLong * res, KrkLong * a, KrkLong * b) {
	/* Subtract b from a, where a is bigger */
	size_t awidth = a->width < 0 ? -a->width : a->width;
	size_t bwidth = b->width < 0 ? -b->width : b->width;
	size_t owidth = awidth;

	krk_long_resize(res, owidth);

	int carry = 0;

	for (size_t i = 0; i < owidth; ++i) {
		/* We'll do long subtraction? */
		uint64_t a_digit = (i < awidth ? a->digits[i] : 0) - carry;
		uint64_t b_digit = i < bwidth ? b->digits[i] : 0;

		if (a_digit < b_digit) {
			a_digit += 1 << DIGIT_SHIFT;
			carry = 1;
		} else {
			carry = 0;
		}
		res->digits[i] = a_digit - b_digit;
	}

	krk_long_trim(res);

	return 0;
}

static int _sub_small_big(KrkLong * res, KrkLong * a, KrkLong * b) {
	_sub_big_small(res,b,a);
	res->width = -res->width;
	return 0;
}

int krk_long_add(KrkLong * res, KrkLong * a, KrkLong * b) {
	/* TODO res == a || res == b */
	if (a->width == 0) {
		krk_long_clear(res);
		krk_long_init_copy(res,b);
		return 0;
	} else if (b->width == 0) {
		krk_long_clear(res);
		krk_long_init_copy(res,a);
		return 0;
	}

	if (a->width < 0 && b->width > 0) {
		switch (krk_long_compare_abs(a,b)) {
			case 1:
				_sub_big_small(res,b,a);
				krk_long_set_sign(res,1);
				return 0;
			case -1:
				_sub_big_small(res,a,b);
				krk_long_set_sign(res,-1);
				return 0;
		}
		return 1;
	} else if (a->width > 0 && b->width < 0) {
		switch (krk_long_compare_abs(a,b)) {
			case 1:
				_sub_big_small(res,b,a);
				krk_long_set_sign(res,-1);
				return 0;
			case -1:
				_sub_big_small(res,a,b);
				krk_long_set_sign(res,1);
				return 0;
		}
		return 1;
	}

	/* sign must match for this, so take it from whichever */
	int sign = a->width < 0 ? -1 : 1;
	if (krk_long_add_ignore_sign(res,a,b)) return 1;
	krk_long_set_sign(res,sign);
	return 0;
}

int krk_long_sub(KrkLong * res, KrkLong * a, KrkLong * b) {
	if (a->width == 0) {
		krk_long_clear(res);
		krk_long_init_copy(res,b);
		return 0;
	} else if (b->width == 0) {
		krk_long_clear(res);
		krk_long_init_copy(res,a);
		return 0;
	}

	if (a->width < 0 && b->width > 0 || a->width > 0 && b->width < 0) {
		if (krk_long_add_ignore_sign(res,a,b)) return 1;
		krk_long_set_sign(res,a->width < 0 ? -1 : 1);
		return 0;
	}

	/* Which is bigger? */
	switch (krk_long_compare(a,b)) {
		case 0:
			krk_long_clear(res);
			return 0;
		case -1:
			return _sub_big_small(res,a,b);
		case 1:
			return _sub_small_big(res,a,b);
	}
}

static int krk_long_zero(KrkLong * num) {
	size_t abs_width = num->width < 0 ? -num->width : num->width;
	for (size_t i = 0; i < abs_width; ++i) {
		num->digits[i] = 0;
	}
	return 0;
}

static int _mul_abs(KrkLong * res, KrkLong * a, KrkLong * b) {

	size_t awidth = a->width < 0 ? -a->width : a->width;
	size_t bwidth = b->width < 0 ? -b->width : b->width;

	krk_long_resize(res, awidth+bwidth);
	krk_long_zero(res);

	fprintf(stderr, "width = %zu\n", awidth+bwidth);

	for (size_t i = 0; i < bwidth; ++i) {
		uint64_t b_digit = b->digits[i];
		uint64_t carry = 0;
		for (size_t j = 0; j < awidth; ++j) {
			uint64_t a_digit = a->digits[j];
			uint64_t tmp = carry + a_digit * b_digit + res->digits[i+j];
			carry = tmp >> DIGIT_SHIFT;
			res->digits[i+j] = tmp & DIGIT_MAX;
		}
		res->digits[i + awidth] = carry;
	}

	krk_long_trim(res);

	return 0;
}

int krk_long_mul(KrkLong * res, KrkLong * a, KrkLong * b) {
	if (a->width == 0) {
		krk_long_clear(res);
		krk_long_init_copy(res,a);
		return 0;
	}

	if (b->width == 0) {
		krk_long_clear(res);
		krk_long_init_copy(res,b);
		return 0;
	}

	if (_mul_abs(res,a,b)) return 1;

	if (a->width < 0 && b->width < 0 || a->width > 0 && b->width > 0) {
		krk_long_set_sign(res,1);
	} else {
		krk_long_set_sign(res,-1);
	}

	return 0;
}

static int _swap(KrkLong * a, KrkLong * b) {
	ssize_t width = a->width;
	uint32_t digits = a->digits;
	a->width = b->width;
	a->digits = b->digits;
	b->width = width;
	b->digits = digits;
	return 0;
}

static int _lshift_one(KrkLong * out, KrkLong * in) {
	if (in->width == 0) {
		krk_long_clear(out);
		return 0;
	}

	size_t abs_width = in->width < 0 ? -in->width : in->width;
	size_t out_width = abs_width;

	if (in->digits[abs_width-1] >> (DIGIT_SHIFT - 1)) {
		out_width += 1;
	}

	krk_long_resize(out, out_width);

	int carry = 0;

	for (size_t i = 0; i < abs_width; ++i) {
		out->digits[i] = (in->digits[i] << 1) + carry;
		carry = (in->digits[i] >> (DIGIT_SHIFT -1));
	}

	if (carry) {
		out->digits[out_width-1] = 1;
	}

	return 0;
}

static size_t _bits_in(KrkLong * num) {
	if (num->width == 0) return 0;

	size_t abs_width = num->width < 0 ? -num->width : num->width;

}

static int _div_abs(KrkLong * quot, KrkLong * rem, KrkLong * a, KrkLong * b) {
	/* quot = a / b; rem = a % b */

	/* Zero quotiant and remainder */
	krk_long_clear(quot);
	krk_long_clear(rem);

	if (b->width == 0) return 1; /* div by zero */
	if (a->width == 0) return 0; /* div of zero */

	size_t awidth = a->width < 0 ? -a->width : a->width;
	size_t bwidth = b->width < 0 ? -b->width : b->width;

	if (awidth < bwidth) {
		/* Small a, quot still 0, remainder is now a */
		krk_long_init_copy(rem, a);
		krk_long_set_sign(rem, 1); /* Ensure positive sign */
		return 0;
	}

	KrkLong tmp;
	krk_long_init_si(&tmp, 0);

	for (size_t i = 0; i < bwidth; ++i) {
		/* Shift remainder by one */
		_lshift_one(&tmp, rem);
		_swap(rem,&tmp);

	}
}

static void verbose_operation(char * op, int (*func)(KrkLong*,KrkLong*,KrkLong*), KrkLong *c, KrkLong *a, KrkLong *b) {
	printnum(stderr, a);
	fprintf(stderr, " %s ", op);
	printnum(stderr, b);
	fprintf(stderr, " = ");
	func(c,a,b);
	printnum(stderr, c);
	fprintf(stderr, "\n");
}

#define do_calc(op,name,left,right) do { \
	krk_long_init_si(&a, left); \
	krk_long_init_si(&b, right); \
	krk_long_init_si(&c, 0); \
	verbose_operation(#op,krk_long_ ## name, &c, &a, &b); \
	krk_long_clear_many(&a,&b,&c,NULL); \
} while (0)

int main(int argc, char * argv[]) {
	KrkLong a, b, c;

	do_calc(+,add,0x7FFFeeee,0x7EEEffff);
	do_calc(+,add,0x7eeeFFFF,0x7fffeeee);
	do_calc(-,sub,0x7fffeeee,0x7eeeffff);
	do_calc(-,sub,0x7eeeFFFF,0x7fffeeee);

	krk_long_init_si(&a, 0x7FFFeeee);
	krk_long_init_si(&b, 0x7eeeFFFF);

	verbose_operation("+",krk_long_add,&c,&a,&b);
	verbose_operation("-",krk_long_sub,&a,&c,&b);
	verbose_operation("-",krk_long_sub,&b,&c,&a);

	do_calc(+,add,42,-32);
	do_calc(+,add,32,-42);

	do_calc(-,sub,42,-32);
	do_calc(-,sub,32,-42);
	do_calc(-,sub,-42,32);
	do_calc(-,sub,-32,42);
	do_calc(-,sub,-42,-32);
	do_calc(-,sub,-32,-42);

	do_calc(*,mul,32,57);
	do_calc(*,mul,0x7eeeFFFF,0x7fffeeee);

	krk_long_init_si(&a, 0x7eeeFFFF);
	krk_long_init_si(&b, 0x7fffeeee);

	verbose_operation("*",krk_long_mul,&c,&a,&b);
	verbose_operation("*",krk_long_mul,&b,&c,&a);

	do_calc(*,mul,0x7eeeFFFF,-0x7fffeeee);
	do_calc(*,mul,-0x7eeeFFFF,-0x7fffeeee);
	do_calc(*,mul,-0x7eeeFFFF,0x7fffeeee);

	return 0;
}
