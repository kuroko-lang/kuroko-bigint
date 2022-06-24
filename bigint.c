#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#define DIGIT_SHIFT 31
#define DIGIT_MAX   0x7FFFFFFF

struct BigInteger {
	ssize_t    width;
	uint32_t *digits;
};

typedef struct BigInteger KrkLong;

static int krk_long_init_si(KrkLong * num, int64_t val) {
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

static int krk_long_clear(KrkLong * num) {
	if (num->digits) free(num->digits);
	num->width = 0;
	num->digits = NULL;
	return 0;
}

static int krk_long_clear_many(KrkLong *a, ...) {
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

static int krk_long_init_many(KrkLong *a, ...) {
	va_list argp;
	va_start(argp, a);

	KrkLong * next = a;
	while (next) {
		krk_long_init_si(next, 0);
		next = va_arg(argp, KrkLong *);
	}

	va_end(argp);
	return 0;
}

static int krk_long_init_copy(KrkLong * out, const KrkLong * in) {
	size_t abs_width = in->width < 0 ? -in->width : in->width;
	out->width = in->width;
	out->digits = out->width ? malloc(sizeof(uint32_t) * abs_width) : NULL;
	for (size_t i = 0; i < abs_width; ++i) {
		out->digits[i] = in->digits[i];
	}
	return 0;
}

static int krk_long_resize(KrkLong * num, ssize_t newdigits) {
	if (newdigits == 0) {
		krk_long_clear(num);
		return 0;
	}

	size_t abs = newdigits < 0 ? -newdigits : newdigits;
	size_t eabs = num->width < 0 ? -num->width : num->width;
	if (num->width == 0) {
		num->digits = malloc(sizeof(uint32_t) * newdigits);
	} else if (eabs < abs) {
		num->digits = realloc(num->digits, sizeof(uint32_t) * newdigits);
	}

	num->width = newdigits;
	return 0;
}

static int krk_long_set_sign(KrkLong * num, int sign) {
	num->width = num->width < 0 ? (-num->width) * sign : num->width * sign;
	return 0;
}

static int krk_long_trim(KrkLong * num) {
	int invert = num->width < 0;
	size_t owidth = invert ? -num->width : num->width;
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
		if (invert) krk_long_set_sign(num, -1);
	}
}

static int krk_long_compare(const KrkLong * a, const KrkLong * b) {
	if (a->width > b->width) return 1;
	if (b->width > a->width) return -1;
	int sign = a->width < 0 ? -1 : 1;
	size_t abs_width = a->width < 0 ? -a->width : a->width;
	for (size_t i = 0; i < abs_width; ++i) {
		if (a->digits[abs_width-i-1] > b->digits[abs_width-i-1]) return sign; /* left is bigger */
		if (a->digits[abs_width-i-1] < b->digits[abs_width-i-1]) return -sign; /* right is bigger */
	}
	return 0; /* they are the same */
}

static int krk_long_compare_abs(const KrkLong * a, const KrkLong * b) {
	size_t a_width = a->width < 0 ? -a->width : a->width;
	size_t b_width = b->width < 0 ? -b->width : b->width;
	if (a_width > b_width) return 1;
	if (b_width > a_width) return -1;
	size_t abs_width = a_width;
	for (size_t i = 0; i < abs_width; ++i) {
		if (a->digits[abs_width-i-1] > b->digits[abs_width-i-1]) return 1; /* left is bigger */
		if (a->digits[abs_width-i-1] < b->digits[abs_width-i-1]) return -1; /* right is bigger */
	}
	return 0; /* they are the same */
}

static int krk_long_add_ignore_sign(KrkLong * res, const KrkLong * a, const KrkLong * b) {
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

static int _sub_big_small(KrkLong * res, const KrkLong * a, const KrkLong * b) {
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

static int _swap(KrkLong * a, KrkLong * b) {
	ssize_t width = a->width;
	uint32_t * digits = a->digits;
	a->width = b->width;
	a->digits = b->digits;
	b->width = width;
	b->digits = digits;
	return 0;
}

#define PREP_OUTPUT(res,a,b) KrkLong _tmp_out_ ## res, *_swap_out_ ## res = NULL; do { if (res == a || res == b) { krk_long_init_si(&_tmp_out_ ## res, 0); _swap_out_ ## res = res; res = &_tmp_out_ ## res; } } while (0)
#define PREP_OUTPUT1(res,a) KrkLong _tmp_out_ ## res, *_swap_out_ ## res = NULL; do { if (res == a) { krk_long_init_si(&_tmp_out_ ## res, 0); _swap_out_ ## res = res;  res = &_tmp_out_ ## res; } } while (0)
#define FINISH_OUTPUT(res) do { if (_swap_out_ ## res) { _swap(_swap_out_ ## res, res); krk_long_clear(&_tmp_out_ ## res); } } while (0)

static int krk_long_add(KrkLong * res, const KrkLong * a, const KrkLong * b) {
	PREP_OUTPUT(res,a,b);

	if (a->width == 0) {
		krk_long_clear(res);
		krk_long_init_copy(res,b);
		FINISH_OUTPUT(res);
		return 0;
	} else if (b->width == 0) {
		krk_long_clear(res);
		krk_long_init_copy(res,a);
		FINISH_OUTPUT(res);
		return 0;
	}

	if (a->width < 0 && b->width > 0) {
		switch (krk_long_compare_abs(a,b)) {
			case -1:
				_sub_big_small(res,b,a);
				krk_long_set_sign(res,1);
				FINISH_OUTPUT(res);
				return 0;
			case 1:
				_sub_big_small(res,a,b);
				krk_long_set_sign(res,-1);
				FINISH_OUTPUT(res);
				return 0;
		}
		return 1;
	} else if (a->width > 0 && b->width < 0) {
		switch (krk_long_compare_abs(a,b)) {
			case -1:
				_sub_big_small(res,b,a);
				krk_long_set_sign(res,-1);
				FINISH_OUTPUT(res);
				return 0;
			case 1:
				_sub_big_small(res,a,b);
				krk_long_set_sign(res,1);
				FINISH_OUTPUT(res);
				return 0;
		}
		FINISH_OUTPUT(res);
		return 1;
	}

	/* sign must match for this, so take it from whichever */
	int sign = a->width < 0 ? -1 : 1;
	if (krk_long_add_ignore_sign(res,a,b)) {
		FINISH_OUTPUT(res);
		return 1;
	}
	krk_long_set_sign(res,sign);
	FINISH_OUTPUT(res);
	return 0;
}

static int krk_long_sub(KrkLong * res, const KrkLong * a, const KrkLong * b) {
	PREP_OUTPUT(res,a,b);
	if (a->width == 0) {
		krk_long_clear(res);
		krk_long_init_copy(res,b);
		krk_long_set_sign(res, b->width < 0 ? 1 : -1);
		FINISH_OUTPUT(res);
		return 0;
	} else if (b->width == 0) {
		krk_long_clear(res);
		krk_long_init_copy(res,a);
		FINISH_OUTPUT(res);
		return 0;
	}

	if (a->width < 0 && b->width > 0 || a->width > 0 && b->width < 0) {
		if (krk_long_add_ignore_sign(res,a,b)) { FINISH_OUTPUT(res); return 1; }
		krk_long_set_sign(res,a->width < 0 ? -1 : 1);
		FINISH_OUTPUT(res);
		return 0;
	}

	/* Which is bigger? */
	switch (krk_long_compare_abs(a,b)) {
		case 0:
			krk_long_clear(res);
			FINISH_OUTPUT(res);
			return 0;
		case 1:
			_sub_big_small(res,a,b);
			if (a->width < 0) krk_long_set_sign(res, -1);
			FINISH_OUTPUT(res);
			return 0;
		case -1:
			_sub_big_small(res,b,a);
			if (b->width > 0) krk_long_set_sign(res, -1);
			FINISH_OUTPUT(res);
			return 0;
	}
}

static int krk_long_zero(KrkLong * num) {
	size_t abs_width = num->width < 0 ? -num->width : num->width;
	for (size_t i = 0; i < abs_width; ++i) {
		num->digits[i] = 0;
	}
	return 0;
}

static int _mul_abs(KrkLong * res, const KrkLong * a, const KrkLong * b) {

	size_t awidth = a->width < 0 ? -a->width : a->width;
	size_t bwidth = b->width < 0 ? -b->width : b->width;

	krk_long_resize(res, awidth+bwidth);
	krk_long_zero(res);

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

static int krk_long_mul(KrkLong * res, const KrkLong * a, const KrkLong * b) {
	PREP_OUTPUT(res,a,b);

	if (a->width == 0) {
		krk_long_clear(res);
		krk_long_init_copy(res,a);
		FINISH_OUTPUT(res);
		return 0;
	}

	if (b->width == 0) {
		krk_long_clear(res);
		krk_long_init_copy(res,b);
		FINISH_OUTPUT(res);
		return 0;
	}

	if (_mul_abs(res,a,b)) {
		FINISH_OUTPUT(res);
		return 1;
	}

	if (a->width < 0 && b->width < 0 || a->width > 0 && b->width > 0) {
		krk_long_set_sign(res,1);
	} else {
		krk_long_set_sign(res,-1);
	}

	FINISH_OUTPUT(res);
	return 0;
}

static int _lshift_one(KrkLong * in) {
	if (in->width == 0) {
		return 0;
	}

	size_t abs_width = in->width < 0 ? -in->width : in->width;
	size_t out_width = abs_width;

	if (in->digits[abs_width-1] >> (DIGIT_SHIFT - 1)) {
		out_width += 1;
	}

	krk_long_resize(in, out_width);

	int carry = 0;

	for (size_t i = 0; i < abs_width; ++i) {
		uint32_t digit = in->digits[i];
		in->digits[i] = ((digit << 1) + carry) & DIGIT_MAX;
		carry = (digit >> (DIGIT_SHIFT -1));
	}

	if (carry) {
		in->digits[out_width-1] = 1;
	}

	return 0;
}

static size_t _bits_in(const KrkLong * num) {
	if (num->width == 0) return 0;

	size_t abs_width = num->width < 0 ? -num->width : num->width;

	/* Top bit in digits[abs_width-1] */
	size_t c = 0;
	uint32_t digit = num->digits[abs_width-1];
	while (digit) {
		c++;
		digit >>= 1;
	}

	return c + (abs_width-1) * DIGIT_SHIFT;
}

static size_t _bit_is_set(const KrkLong * num, size_t bit) {
	size_t abs_width = num->width < 0 ? -num->width : num->width;
	size_t digit_offset = bit / DIGIT_SHIFT;
	size_t digit_bit    = bit % DIGIT_SHIFT;
	return !!(num->digits[digit_offset] & (1 << digit_bit));
}

static int _bit_set_zero(KrkLong * num, int val) {
	if (num->width == 0) {
		krk_long_clear(num);
		krk_long_init_si(num, !!val);
		return 0;
	}

	num->digits[0] = (num->digits[0] & ~1) | (!!val);
	return 0;
}

static int krk_long_bit_set(KrkLong * num, size_t bit) {
	size_t abs_width = num->width < 0 ? -num->width : num->width;
	size_t digit_offset = bit / DIGIT_SHIFT;
	size_t digit_bit    = bit % DIGIT_SHIFT;

	if (digit_offset >= abs_width) {
		krk_long_resize(num, digit_offset+1);
		for (size_t i = abs_width; i < digit_offset + 1; ++i) {
			num->digits[i] = 0;
		}
	}

	num->digits[digit_offset] |= (1 << digit_bit);
	return 0;
}

static int _div_abs(KrkLong * quot, KrkLong * rem, const KrkLong * a, const KrkLong * b) {
	/* quot = a / b; rem = a % b */

	/* Zero quotiant and remainder */
	krk_long_clear(quot);
	krk_long_clear(rem);

	if (b->width == 0) return 1; /* div by zero */
	if (a->width == 0) return 0; /* div of zero */

	size_t awidth = a->width < 0 ? -a->width : a->width;
	size_t bwidth = b->width < 0 ? -b->width : b->width;

	size_t bits = _bits_in(a);

	KrkLong absa, absb;
	krk_long_init_copy(&absa, a);
	krk_long_set_sign(&absa, 1);
	krk_long_init_copy(&absb, b);
	krk_long_set_sign(&absb, 1);

	for (size_t i = 0; i < bits; ++i) {
		size_t _i = bits - i - 1;

		/* Shift remainder by one */
		_lshift_one(rem);

		int is_set = _bit_is_set(&absa, _i);
		_bit_set_zero(rem, is_set);
		if (krk_long_compare(rem,&absb) >= 0) {
			_sub_big_small(rem,rem,&absb);
			krk_long_bit_set(quot, _i);
		}
	}

	krk_long_clear_many(&absa,&absb,NULL);

	return 0;
}

static int krk_long_div_rem(KrkLong * quot, KrkLong * rem, const KrkLong * a, const KrkLong * b) {
	PREP_OUTPUT(quot,a,b);
	PREP_OUTPUT(rem,a,b);
	if (_div_abs(quot,rem,a,b)) {
		FINISH_OUTPUT(rem);
		FINISH_OUTPUT(quot);
		return 1;
	}

	if ((a->width < 0) != (b->width < 0)) {
		/* Round down if remainder */
		if (rem->width) {
			KrkLong one;
			krk_long_init_si(&one, 1);
			krk_long_add(quot, quot, &one);
			_sub_big_small(rem, b, rem);
			krk_long_clear(&one);
		}

		/* Signs are different, negate and round down if necessary */
		krk_long_set_sign(quot, -1);
	}

	if (b->width < 0) {
		krk_long_set_sign(rem, -1);
	}

	FINISH_OUTPUT(rem);
	FINISH_OUTPUT(quot);
	return 0;
}

static int krk_long_abs(KrkLong * out, const KrkLong * in) {
	PREP_OUTPUT1(out,in);
	krk_long_clear(out);
	krk_long_init_copy(out, in);
	krk_long_set_sign(out, 1);
	FINISH_OUTPUT(out);
	return 0;
}

static int krk_long_sign(const KrkLong * num) {
	if (num->width == 0) return 0;
	return num->width < 0 ? -1 : 1;
}

size_t krk_long_digits_in_base(KrkLong * num, int base) {
	if (num->width == 0) return 1;

	size_t bits = _bits_in(num);

	if (base <  4)  return bits;
	if (base <  8)  return (bits+1)/2;
	if (base < 16)  return (bits+2)/3;
	if (base == 16) return (bits+3)/4;
	return 0;
}

uint32_t krk_long_short(KrkLong * num) {
	if (num->width == 0) return 0;
	return num->digits[0];
}

static int64_t krk_long_medium(KrkLong * num) {
	if (num->width == 0) return 0;

	if (num->width < 0) {
		uint64_t val = num->digits[0];
		if (num->width < 1) {
			val |= (num->digits[1]) << 31;
		}
		return -val;
	} else {
		uint64_t val = num->digits[0];
		if (num->width > 1) {
			val |= (num->digits[1]) << 31;
		}
		return val;
	}
}

static int do_bin_op(KrkLong * res, const KrkLong * a, const KrkLong * b, char op) {
	size_t awidth = a->width < 0 ? -a->width : a->width;
	size_t bwidth = b->width < 0 ? -b->width : b->width;
	size_t owidth = ((awidth > bwidth) ? awidth : bwidth) + 1;

	int aneg = (a->width < 0);
	int bneg = (b->width < 0);
	int rneg = 0;

	switch (op) {
		case '|': rneg = aneg | bneg; break;
		case '^': rneg = aneg ^ bneg; break;
		case '&': rneg = aneg & bneg; break;
	}

	krk_long_resize(res, owidth);

	int acarry = aneg ? 1 : 0;
	int bcarry = bneg ? 1 : 0;
	int rcarry = rneg ? 1 : 0;

	for (size_t i = 0; i < owidth; ++i) {
		uint32_t a_digit = (i < awidth ? a->digits[i] : 0);
		a_digit = aneg ? ((a_digit ^ DIGIT_MAX) + acarry) : a_digit;
		acarry = a_digit >> DIGIT_SHIFT;

		uint32_t b_digit = (i < bwidth ? b->digits[i] : 0);
		b_digit = bneg ? ((b_digit ^ DIGIT_MAX) + bcarry) : b_digit;
		bcarry = b_digit >> DIGIT_SHIFT;

		uint32_t r;
		switch (op) {
			case '|': r = a_digit | b_digit; break;
			case '^': r = a_digit ^ b_digit; break;
			case '&': r = a_digit & b_digit; break;
		}

		r = rneg ? (((r & DIGIT_MAX) ^ DIGIT_MAX) + rcarry) : r;
		res->digits[i] = r & DIGIT_MAX;
		rcarry = r >> DIGIT_SHIFT;
	}

	krk_long_trim(res);

	if (rneg) {
		krk_long_set_sign(res,-1);
	}

	return 0;
}

static int krk_long_or(KrkLong * res, const KrkLong * a, const KrkLong * b) {
	PREP_OUTPUT(res,a,b);
	if (a->width == 0) {
		krk_long_clear(res);
		krk_long_init_copy(res,b);
		FINISH_OUTPUT(res);
		return 0;
	} else if (b->width == 0) {
		krk_long_clear(res);
		krk_long_init_copy(res,a);
		FINISH_OUTPUT(res);
		return 0;
	}

	int out = do_bin_op(res,a,b,'|');
	FINISH_OUTPUT(res);
	return out;
}

static int krk_long_xor(KrkLong * res, const KrkLong * a, const KrkLong * b) {
	PREP_OUTPUT(res,a,b);
	int out = do_bin_op(res,a,b,'^');
	FINISH_OUTPUT(res);
	return out;
}

static int krk_long_and(KrkLong * res, const KrkLong * a, const KrkLong * b) {
	PREP_OUTPUT(res,a,b);
	if (a->width == 0) {
		krk_long_clear(res);
		krk_long_init_copy(res,a);
		FINISH_OUTPUT(res);
		return 0;
	} else if (b->width == 0) {
		krk_long_clear(res);
		krk_long_init_copy(res,b);
		FINISH_OUTPUT(res);
		return 0;
	}

	int out = do_bin_op(res,a,b,'&');
	FINISH_OUTPUT(res);
	return out;
}

char * krk_long_to_str(const KrkLong * n, int _base, const char * prefix, size_t *size) {
	static const char vals[] = "0123456789abcdef";
	KrkLong abs, mod, base;

	krk_long_init_si(&abs, 0);
	krk_long_init_si(&mod, 0);
	krk_long_init_si(&base, _base);

	krk_long_abs(&abs, n);

	int sign = krk_long_sign(n);   /* -? +? 0? */

	size_t len = (sign == -1 ? 1 : 0) + krk_long_digits_in_base(&abs,_base) + strlen(prefix) + 1;
	char * tmp = malloc(len);
	char * writer = tmp;

	if (sign == 0) {
		*writer++ = '0';
	} else {
		while (krk_long_sign(&abs) > 0) {
			krk_long_div_rem(&abs,&mod,&abs,&base);
			*writer++ = vals[krk_long_short(&mod)];
		}
	}

	while (*prefix) { *writer++ = *prefix++; }
	if (sign < 0) *writer++ = '-';

	char * rev = malloc(len);
	char * out = rev;
	while (writer != tmp) {
		writer--;
		*out++ = *writer;
	}
	*out = '\0';

	free(tmp);

	krk_long_clear_many(&abs,&mod,&base,NULL);
	*size = strlen(rev);

	return rev;
}

static int is_valid(int base, char c) {
	if (c == '_') return 1;
	if (c < '0') return 0;
	if (base <= 10) {
		return c < ('0' + base);
	}

	if (c >= 'a' && c < 'a' + (base - 10)) return 1;
	if (c >= 'A' && c < 'A' + (base - 10)) return 1;
	if (c >= '0' && c <= '9') return 1;
	return 0;
}

static int convert_digit(char c) {
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'a' && c <= 'z') {
		return c - 'a' + 0xa;
	}
	if (c >= 'A' && c <= 'Z') {
		return c - 'A' + 0xa;
	}
	return 0;
}

static int krk_long_parse_string(const char * str, KrkLong * num) {
	const char * c = str;
	int base = 10;
	int sign = 1;
	while (*c && (*c == ' ' || *c == '\t')) c++;

	if (*c == '-') {
		sign = -1;
		c++;
	} else if (*c == '+') {
		c++;
	}

	if (*c == '0') {
		c++;

		if (*c == 'x') {
			base = 16;
			c++;
		} else if (*c == 'o') {
			base = 8;
			c++;
		} else if (*c == 'b') {
			base = 2;
			c++;
		}
	}

	krk_long_init_si(num, 0);

	KrkLong _base, scratch;
	krk_long_init_si(&_base, base);
	krk_long_init_si(&scratch, 0);

	while (is_valid(base, *c)) {
		if (*c == '_') {
			c++;
			continue;
		}
		krk_long_mul(num, num, &_base);
		krk_long_clear(&scratch);
		krk_long_init_si(&scratch, convert_digit(*c));
		krk_long_add(num, num, &scratch);
		c++;
	}

	if (sign == -1) {
		krk_long_set_sign(num, -1);
	}

	krk_long_clear_many(&_base, &scratch, NULL);
}

#ifndef AS_LIB
#define PRINTER(name,base,prefix) \
	static void print_base_ ## name (FILE * f, const KrkLong * num) { \
		size_t unused; \
		char * s = krk_long_to_str(num, base, prefix, &unused); \
		fprintf(f, "%s", s); \
		free(s); \
	}

PRINTER(str,10,"")
PRINTER(hex,16,"x0")
PRINTER(oct,8,"o0")
PRINTER(bin,2,"b0")

static void verbose_operation(char * op, int (*func)(KrkLong*,const KrkLong*,const KrkLong*), KrkLong *c, const KrkLong *a, const KrkLong *b) {
	print_base_str(stderr, a);
	fprintf(stderr, " %s ", op);
	print_base_str(stderr, b);
	fprintf(stderr, " == ");
	func(c,a,b);
	print_base_str(stderr, c);
	fprintf(stderr, "\n");
}

static void verbose_div(KrkLong *c, KrkLong * d, const KrkLong *a, const KrkLong *b) {
	krk_long_div_rem(c,d,a,b);

	print_base_str(stderr, a);
	fprintf(stderr, " // ");
	print_base_str(stderr, b);
	fprintf(stderr, " == ");
	print_base_str(stderr, c);
	fprintf(stderr, "\n");
	print_base_str(stderr, a);
	fprintf(stderr, " %% ");
	print_base_str(stderr, b);
	fprintf(stderr, " == ");
	print_base_str(stderr, d);
	fprintf(stderr, "\n");
}

#define do_calc(op,name,left,right) do { \
	krk_long_init_si(&a, left); \
	krk_long_init_si(&b, right); \
	krk_long_init_si(&c, 0); \
	verbose_operation(#op,krk_long_ ## name, &c, &a, &b); \
	krk_long_clear_many(&a,&b,&c,NULL); \
} while (0)

#define do_div(left,right) do { \
	krk_long_parse_string(#left,&a); \
	krk_long_parse_string(#right,&b); \
	krk_long_init_si(&c, 0); \
	krk_long_init_si(&d, 0); \
	verbose_div(&c,&d,&a,&b); \
	krk_long_clear_many(&a,&b,&c,&d,NULL); \
} while (0)

int main(int argc, char * argv[]) {
	KrkLong a, b, c, d;

	do_calc(+,add,0x7FFFeeee,0x7EEEffff);
	do_calc(+,add,0x7eeeFFFF,0x7fffeeee);
	do_calc(-,sub,0x7fffeeee,0x7eeeffff);
	do_calc(-,sub,0x7eeeFFFF,0x7fffeeee);

	krk_long_init_si(&a, 0x7FFFeeee);
	krk_long_init_si(&b, 0x7eeeFFFF);

	verbose_operation("+",krk_long_add,&c,&a,&b);
	verbose_operation("-",krk_long_sub,&a,&c,&b);
	verbose_operation("-",krk_long_sub,&b,&c,&a);

	krk_long_clear_many(&a,&b,&c,NULL);

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

	krk_long_clear_many(&a,&b,&c,NULL);

	do_calc(*,mul,0x7eeeFFFF,-0x7fffeeee);
	do_calc(*,mul,-0x7eeeFFFF,-0x7fffeeee);
	do_calc(*,mul,-0x7eeeFFFF,0x7fffeeee);

	do_div(9324932533295, 392);
	do_div(0x953289537218528853293826328432432, 0x823852983523);
	do_div(2325,-2);
	do_div(2,-4);
	do_div(5,7);
	do_div(5,-7);
	do_div(-5,7);
	do_div(-5,-7);

	krk_long_parse_string("0x123456789abcdef0123456789abcdef",&a);
	print_base_str(stderr, &a);
	fprintf(stderr, " == ");
	print_base_hex(stderr, &a);
	fprintf(stderr, "\n");

	krk_long_clear(&a);

	do_calc(|,or,0x1234,0x2345);
	do_calc(&,and,0x1234,0x2345);
	do_calc(^,xor,0x1234,0x2345);

	do_calc(|,or,-632632,-25832);
	do_calc(&,and,-632632,-25832);
	do_calc(^,xor,-632632,-25832);
	do_calc(^,xor,-632632,25832);
	do_calc(^,xor,632632,-25832);

	do_calc(|,or,0x12345678abcdef01,-0x1245abcdef);
	do_calc(^,xor,0x12345678abcdef01,-0x1245abcdef);
	do_calc(&,and,0x12345678abcdef01,-0x1245abcdef);

	return 0;
}
#endif
