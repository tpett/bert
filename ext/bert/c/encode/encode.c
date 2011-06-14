#include "encode.h"

// FBuffer implementation

static FBuffer *fbuffer_alloc() {
  FBuffer *fb = ALLOC(FBuffer);
  memset((void *) fb, 0, sizeof(FBuffer));
  fb->initial_length = FBUFFER_INITIAL_LENGTH;
  return fb;
}

static void fbuffer_free(FBuffer *fb) {
  if (fb->ptr) ruby_xfree(fb->ptr);
  ruby_xfree(fb);
}

static void fbuffer_clear(FBuffer *fb) {
  fb->len = 0;
}

static void fbuffer_inc_capa(FBuffer *fb, unsigned long requested) {
  unsigned long required;

  if(fb->len == 0) {
    fb->ptr = ALLOC_N(char, fb->initial_length);
    fb->capa = fb->initial_length;
  }

  // Allocate additional memory in exponentially larger chunks to avoid excessive calls
  for(required = fb->capa; required < fb->len + requested; required <<= 1);

  if(required > fb->capa) {
    REALLOC_N(fb->ptr, char, required);
    fb->capa = required;
  }
}

static void fbuffer_append(FBuffer *fb, const char *newstr, unsigned long len) {
  if(len > 0) {
    fbuffer_inc_capa(fb, len);

    int i;
    for(i = 0; i < len; i++) {
      *(fb->ptr + fb->len + i) = *(newstr + i);
    }

    fb->len += len;
  }
}

static void fbuffer_append_char(FBuffer *fb, char newchr) {
  fbuffer_inc_capa(fb, 1);
  *(fb->ptr + fb->len) = newchr;
  fb->len++;
}

static void fbuffer_append_number(FBuffer *fb, long number, short int length) {
  char output[length];
  char *input = (char*) &number;
  int i;

  for(i = 0; i < length; i++) {
    output[i] = *(input + (length - 1 - i));
  }

  fbuffer_append(fb, (char *) &output, length);
}

static void fbuffer_append_long(FBuffer *fb, long number) {
  fbuffer_append_number(fb, number, 4);
}

static void fbuffer_append_short(FBuffer *fb, short int number) {
  fbuffer_append_number(fb, number, 2);
}

// Convert buffer to Ruby string
static VALUE fbuffer_to_s(FBuffer *fb) {
  return rb_str_new(fb->ptr, fb->len);
}


//
// BERT Encoding Implementation
//

static void fail(VALUE rObject) {
  rb_raise(rb_eStandardError, "BERT: Failed to encode object");
}

static void write_1(FBuffer *fb, unsigned char out) {
  fbuffer_append_char(fb, out);
}


//
// Binary (strings) and ATOMs
//

static void write_string(FBuffer *fb, VALUE rString) {
  fbuffer_append(fb, RSTRING_PTR(rString), RSTRING_LEN(rString));
}

static void write_symbol(FBuffer *fb, VALUE rObject) {
  VALUE rString = rb_funcall(rObject, rb_intern("to_s"), 0);

  write_1(fb, ERL_ATOM);
  fbuffer_append_short(fb, (short int) RSTRING_LEN(rString));
  write_string(fb, rString);
}

static void write_binary(FBuffer *fb, VALUE rObject) {
  write_1(fb, ERL_BIN);
  fbuffer_append_long(fb, RSTRING_LEN(rObject));
  write_string(fb, rObject);
}


//
// Bignums
//

// C transcription of Ruby implementation.  I didn't want to dig into the internal implementation
// of Ruby's Bignum, but a much faster implementation could be written by converting the
// immediate Bignum value to the BERT Bignum format
// TODO: Invent faster algorithm for parsing Bignums

static void write_bignum_guts(FBuffer *fb, VALUE rObject) {
  // Ruby: write_1 (num >= 0 ? 0 : 1)
  write_1(fb, RTEST(rb_funcall(rObject, rb_intern(">="), 1, INT2FIX(0))) ? 0 : 1);
  // Ruby: num = num.abs
  VALUE bigNum = rb_funcall(rObject, rb_intern("abs"), 0);
  // Ruby: while(num != 0)
  while( !rb_funcall(bigNum, rb_intern("=="), 1, INT2FIX(0)) ) {
    // Ruby: rem = num % 256
    short int rem = (short int) FIX2INT(rb_funcall(bigNum, rb_intern("%"), 1, INT2FIX(256)));
    // Ruby: write_1 rem
    write_1(fb, rem);
    // Ruby: num = num >> 8
    bigNum = rb_funcall(bigNum, rb_intern(">>"), 1, INT2FIX(8));
  }
}

static void write_bignum(FBuffer *fb, VALUE rObject) {
  // Ruby: n = (num.to_s(2).size / 8.0).ceil
  VALUE rStringRep = rb_funcall(rObject, rb_intern("to_s"), 1, INT2FIX(2));
  long bits = NUM2LONG(rb_funcall(rStringRep, rb_intern("size"), 0));
  int bytes = ceil(bits / 8.0);

  if(bytes < 256) {
    write_1(fb, ERL_SMALL_BIGNUM);
    write_1(fb, bytes);
    write_bignum_guts(fb, rObject);
  } else {
    write_1(fb, ERL_LARGE_BIGNUM);
    fbuffer_append_long(fb, bytes);
    write_bignum_guts(fb, rObject);
  }
}


//
// Integers
//

static void write_integer(FBuffer *fb, VALUE rObject) {
  long number = FIX2LONG(rObject);

  if(number >= 0 && number <= 0xff) {
    write_1(fb, ERL_SMALL_INT);
    write_1(fb, number);
  } else if (number <= ERL_MAX_INT && number >= ERL_MIN_INT) {
    write_1(fb, ERL_INT);
    fbuffer_append_long(fb, number);
  } else {
    write_bignum(fb, rObject);
  }
}


//
// Floats
//

static void write_float(FBuffer *fb, VALUE rObject) {
  write_1(fb, ERL_FLOAT);

  // This could be optimized
  VALUE string = rb_funcall(
      rb_intern("Kernel"),
      rb_intern("format"),
      2,
      rb_str_new2("\%15.15e"), rObject
  );

  char float_string[31];
  long string_len = RSTRING_LEN(string);

  int i;
  for(i = 0; i < 31; i++) {
    if(i < string_len) {
      float_string[i] = *(RSTRING_PTR(string) + i);
    } else {
      float_string[i] = 0;
    }
  }

  fbuffer_append(fb, float_string, 31);
}


//
// Arrays and Tuples
//

static void write_array(FBuffer *fb, VALUE rObject) {
  long length = RARRAY_LEN(rObject);
  short tuple = (rb_funcall(rObject, rb_intern("class"), 0) == cTuple);

  if(tuple) {
    if(length < 256) {
      write_1(fb, ERL_SMALL_TUPLE);
      write_1(fb, length);
    } else {
      write_1(fb, ERL_LARGE_TUPLE);
      fbuffer_append_long(fb, length);
    }
  } else {
    if(length == 0) {
      write_1(fb, ERL_NIL);
      return;
    }

    write_1(fb, ERL_LIST);
    fbuffer_append_long(fb, length);
  }

  long i;
  for(i = 0; i < length; i++) {
    write_any_raw(fb, rb_ary_entry(rObject, i));
  }

  if(!tuple) write_1(fb, ERL_NIL);
}


static void write_any_raw(FBuffer *fb, VALUE rObject) {
  switch(TYPE(rObject)) {
    case T_SYMBOL:
      write_symbol(fb, rObject);
      break;
    case T_FIXNUM:
    case T_BIGNUM:
      write_integer(fb, rObject);
      break;
    case T_FLOAT:
      write_float(fb, rObject);
      break;
    case T_ARRAY:
      write_array(fb, rObject);
      break;
    case T_STRING:
      write_binary(fb, rObject);
      break;
    default:
      fail(rObject);
  }
}

static void write_any(FBuffer *fb, VALUE rObject) {
  write_1(fb, ERL_VERSION);
  write_any_raw(fb, rObject);
}

static VALUE method_encode(VALUE klass, VALUE rObject) {
  FBuffer *fb = fbuffer_alloc();
  VALUE encoded;
  write_any(fb, rObject);
  encoded = fbuffer_to_s(fb);
  fbuffer_free(fb);
  return encoded;
}

static VALUE method_impl(VALUE klass) {
  return rb_str_new("C", 1);
}

void Init_encode() {
  mBERT = rb_const_get(rb_cObject, rb_intern("BERT"));
  cEncode = rb_define_class_under(mBERT, "Encode", rb_cObject);
  cTuple = rb_const_get(mBERT, rb_intern("Tuple"));
  rb_define_singleton_method(cEncode, "encode", method_encode, 1);
  rb_define_singleton_method(cEncode, "impl", method_impl, 0);
}
