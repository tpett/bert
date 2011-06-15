#ifndef _ENCODE_H_
#define _ENCODE_H_

#include <math.h>

#include "ruby.h"
#include "st.h"
#include "regex.h"

#include "../bert.h"

static VALUE mBERT;
static VALUE cEncode;
static VALUE cTuple;

// Initialization Method
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
static void fbuffer_free(FBuffer *fb);
static void fbuffer_clear(FBuffer *fb);
static void fbuffer_inc_capa(FBuffer *fb, unsigned long requested);
static void fbuffer_append(FBuffer *fb, const char *newstr, unsigned long len);
static void fbuffer_append_char(FBuffer *fb, char newchr);
static void fbuffer_append_number(FBuffer *fb, long number, short int length);
static void fbuffer_append_long(FBuffer *fb, long number);
static void fbuffer_append_short(FBuffer *fb, short int number);
static VALUE fbuffer_to_s(FBuffer *fb);

// BERT Encoding Implementation
static void fail(VALUE rObject);
static void write_1(FBuffer *fb, unsigned char out);

static void write_string(FBuffer *fb, VALUE rString);
static void write_symbol(FBuffer *fb, VALUE rObject);
static void write_binary(FBuffer *fb, VALUE rObject);

static void write_bignum_guts(FBuffer *fb, VALUE rObject);
static void write_bignum(FBuffer *fb, VALUE rObject);

static void write_integer(FBuffer *fb, VALUE rObject);

static void write_float(FBuffer *fb, VALUE rObject);

static void write_array(FBuffer *fb, VALUE rObject);

static void write_any_raw(FBuffer *fb, VALUE rObject);
static void write_any(FBuffer *fb, VALUE rObject);

static VALUE method_encode(VALUE klass, VALUE rObject);
static VALUE method_encoder(VALUE klass, VALUE rObject);
static VALUE method_impl(VALUE klass);

#endif
