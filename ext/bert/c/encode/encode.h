#ifndef _ENCODE_H_
#define _ENCODE_H_

#include "ruby.h"
#include <math.h>

#define ERL_VERSION       131
#define ERL_SMALL_INT     97
#define ERL_INT           98
#define ERL_SMALL_BIGNUM  110
#define ERL_LARGE_BIGNUM  111
#define ERL_FLOAT         99
#define ERL_ATOM          100
#define ERL_SMALL_TUPLE   104
#define ERL_LARGE_TUPLE   105
#define ERL_NIL           106
#define ERL_STRING        107
#define ERL_LIST          108
#define ERL_BIN           109

#define ERL_MIN_INT       -134217728  // -(1 << 27)
#define ERL_MAX_INT       134217727   // (1 << 27) - 1

static VALUE mBERT;
static VALUE cEncode;
static VALUE cTuple;
void Init_encode();

// Create a FIFO buffer
typedef struct FBufferStruct {
  unsigned long initial_length;
  char *ptr;
  unsigned long len;
  unsigned long capa; // capacity
} FBuffer;

#define FBUFFER_INITIAL_LENGTH 4096

static FBuffer *fbuffer_alloc();
static FBuffer *fbuffer_alloc_with_length(unsigned long initial_length);
static void fbuffer_free(FBuffer *fb);
static void fbuffer_free_only_buffer(FBuffer *fb);
static void fbuffer_clear(FBuffer *fb);
static void fbuffer_append(FBuffer *fb, const char *newstr, unsigned long len);
static void fbuffer_append_long(FBuffer *fb, long number);
static void fbuffer_append_short(FBuffer *fb, short int number);
static void fbuffer_append_char(FBuffer *fb, char newchr);
static VALUE fbuffer_to_s(FBuffer *fb);

static VALUE method_encode(VALUE klass, VALUE rString);
static VALUE method_impl(VALUE klass);
static void write_any(FBuffer *fb, VALUE rObject);
static void write_any_raw(FBuffer *fb, VALUE rObject);
static void write_binary(FBuffer *fb, VALUE rObject);
static void write_symbol(FBuffer *fb, VALUE rObject);
static void write_string(FBuffer *fb, VALUE rString);
static void write_integer(FBuffer *fb, VALUE rObject);
static void write_float(FBuffer *fb, VALUE rObject);
static void write_array(FBuffer *fb, VALUE rObject);
// static void write_2(FBuffer *fb, short int out);
// static void write_1(FBuffer *fb, unsigned char out);
static void fail(VALUE rObject);

#endif
