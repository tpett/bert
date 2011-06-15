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

void p(VALUE val) {
  rb_funcall(rb_mKernel, rb_intern("p"), 1, val);
}

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
  VALUE rString = rb_str_new2(rb_id2name(SYM2ID(rObject)));

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

// I'm not sure how well this performs...
static VALUE tuple_new(VALUE rArray) {
  return rb_funcall(cTuple, rb_intern("new"), 1, rArray);
}

// Passed to st_foreach to handle Hashes
static int collect_hash(st_data_t key, st_data_t val, VALUE *args) {
  VALUE converted_key = method_encoder(args[0], (VALUE) key);
  VALUE converted_val = method_encoder(args[0], (VALUE) val);
  rb_ary_push(args[1], tuple_new(rb_ary_new3(2, converted_key, converted_val)) );
  return ST_CONTINUE;
}

#define C2SYM(str) (ID2SYM(rb_intern(str)))

static VALUE method_encoder(VALUE klass, VALUE rObject) {
  switch(TYPE(rObject)) {
    case T_HASH:
    {
      VALUE args[2];
      VALUE pairs = rb_ary_new();
      args[0] = klass;
      args[1] = pairs;

      st_foreach(RHASH_TBL(rObject), &collect_hash, (st_data_t)&args);

      return tuple_new(rb_ary_new3(3, ID2SYM(rb_intern("bert")), ID2SYM(rb_intern("dict")), pairs));
    }
    case T_ARRAY:
    {
      VALUE new_array = rb_funcall(rObject, rb_intern("clone"), 0);
      int i;
      for(i = 0; i < RARRAY(new_array)->len; i++) {
        rb_ary_store(new_array, i, method_encoder(klass, rb_ary_entry(new_array, i)));
      }
      return new_array;
    }
    case T_NIL:
      return tuple_new(rb_ary_new3(2, C2SYM("bert"), C2SYM("nil")));
    case T_FALSE:
      return tuple_new(rb_ary_new3(2, C2SYM("bert"), C2SYM("false")));
    case T_TRUE:
      return tuple_new(rb_ary_new3(2, C2SYM("bert"), C2SYM("true")));
    case T_REGEXP:
    {
      VALUE source = rb_str_new(RREGEXP(rObject)->str, RREGEXP(rObject)->len);
      long options = RREGEXP(rObject)->ptr->options;
      VALUE options_ary = rb_ary_new();

      // Append Regex options
      if((options & RE_OPTION_IGNORECASE) > 0) rb_ary_push(options_ary, C2SYM("caseless"));
      if((options & RE_OPTION_EXTENDED) > 0) rb_ary_push(options_ary, C2SYM("extended"));
      if((options & RE_OPTION_MULTILINE) > 0) rb_ary_push(options_ary, C2SYM("multiline"));

      return tuple_new(rb_ary_new3(4, C2SYM("bert"), C2SYM("regex"), source, options_ary));
    }
    default:
    {
      if(rb_funcall(rObject, rb_intern("class"), 0) == rb_cTime) {
        long sec = FIX2LONG(rb_funcall(rObject, rb_intern("to_i"), 0));
        long usec = FIX2LONG(rb_funcall(rObject, rb_intern("usec"), 0));

        VALUE return_val = tuple_new(rb_ary_new3(5,
          C2SYM("bert"),
          C2SYM("time"),
          INT2FIX(sec / 1000000),
          INT2FIX(sec % 1000000),
          INT2FIX(usec)
        ));

        return return_val;
      } else {
        return rObject;
      }
    }
  }
}

void Init_encode() {
  mBERT = rb_const_get(rb_cObject, rb_intern("BERT"));
  cEncode = rb_define_class_under(mBERT, "Encode", rb_cObject);
  cTuple = rb_const_get(mBERT, rb_intern("Tuple"));
  VALUE cEncoder = rb_const_get(mBERT, rb_intern("Encoder"));

  rb_define_singleton_method(cEncode, "encode", method_encode, 1);
  rb_define_singleton_method(cEncode, "impl", method_impl, 0);

  rb_define_singleton_method(cEncoder, "convert", method_encoder, 1);
}
