// TODO: We should get the same diagnostics with/without compound_literal_init (rdar://138982703)
// RUN: %clang_cc1 -fsyntax-only -fbounds-safety -verify=expected,both -fbounds-safety-bringup-missing-checks=compound_literal_init %s
// RUN: %clang_cc1 -fsyntax-only -fbounds-safety -verify=both %s
// RUN: %clang_cc1 -fsyntax-only -fbounds-safety -x objective-c -fbounds-attributes-objc-experimental -verify=both %s
// RUN: %clang_cc1 -fsyntax-only -fbounds-safety -x objective-c -fbounds-attributes-objc-experimental -verify=expected,both -fbounds-safety-bringup-missing-checks=compound_literal_init %s
#include <ptrcheck.h>

int side_effect(void);
int get_count(void);
char* get_single_ptr();
struct cb_with_other_data {
  int count;
  char* __counted_by(count) buf;
  int other;
};
void consume_cb_with_other_data(struct cb_with_other_data);

struct NestedCB {
  struct cb_with_other_data inner;
  int count;
  char* __counted_by(count) buf;
  int other;
};

struct no_attr_with_other_data {
  int count;
  char* buf;
  int other;
};
_Static_assert(sizeof(struct cb_with_other_data) == sizeof(struct no_attr_with_other_data), "size mismatch");

union TransparentUnion {
  struct cb_with_other_data cb;
  struct no_attr_with_other_data no_cb;
} __attribute__((__transparent_union__));

void receive_transparent_union(union TransparentUnion);


void no_diag(void) {
  // No diagnostics
  consume_cb_with_other_data((struct cb_with_other_data){
    0x0,
    0x0,
    0x0
  });
}

struct cb_with_other_data global_no_diags = (struct cb_with_other_data) {
  0x0,
  0x0,
  0x0
};


struct cb_with_other_data field_initializers_with_side_effects(struct cb_with_other_data* s) {
  *s = (struct cb_with_other_data){
    0,
    0x0,
    // expected-warning@+1{{initializer side_effect() has a side effect; this may lead to an unexpected result because the evaluation order of initialization list expressions is indeterminate}}
    side_effect()
  };

  struct cb_with_other_data s2 = (struct cb_with_other_data){
    0,
    0x0,
    // expected-warning@+1{{initializer side_effect() has a side effect; this may lead to an unexpected result because the evaluation order of initialization list expressions is indeterminate}}
    side_effect()
  };

  consume_cb_with_other_data((struct cb_with_other_data){
    0,
    0x0,
    // expected-warning@+1{{initializer side_effect() has a side effect; this may lead to an unexpected result because the evaluation order of initialization list expressions is indeterminate}}
    side_effect()}
  );
  consume_cb_with_other_data((struct cb_with_other_data){
    // expected-warning@+1{{initializer side_effect() has a side effect; this may lead to an unexpected result because the evaluation order of initialization list expressions is indeterminate}} 
    .other = side_effect(),
    .buf = 0x0,
    .count = 0}
  );

  (void) (struct cb_with_other_data){
    // expected-warning@+1{{initializer side_effect() has a side effect; this may lead to an unexpected result because the evaluation order of initialization list expressions is indeterminate}} 
    .other = side_effect(),
    .buf = 0x0,
    .count = 0};

  // no diags for structs without attributes
  struct no_attrs {
    int count;
    char* buf;
  };
  (void) (struct no_attrs) { side_effect(), 0x0};

  // Nested
  struct Contains_cb_with_other_data {
    struct cb_with_other_data s;
    int other;
  };
  (void)(struct Contains_cb_with_other_data) {
    .s = {
      // expected-warning@+1{{initializer side_effect() has a side effect; this may lead to an unexpected result because the evaluation order of initialization list expressions is indeterminate}} 
      .other = side_effect(),
      .buf = 0x0,
      .count = 0
    },
    .other = 0x0
  };

  // Nested CompoundLiteralExpr
  (void)(struct NestedCB) {
    // expected-warning@+1{{initializer (struct cb_with_other_data){.count = 0, .buf = 0, .other = side_effect()} has a side effect; this may lead to an unexpected result because the evaluation order of initialization list expressions is indeterminate}}
    .inner = (struct cb_with_other_data) {
      .count = 0,
      .buf = 0x0,
      // expected-warning@+1{{initializer side_effect() has a side effect; this may lead to an unexpected result because the evaluation order of initialization list expressions is indeterminat}}
      .other = side_effect()
    },
    .count = 0,
    .buf = 0x0,
    // expected-warning@+1{{initializer side_effect() has a side effect; this may lead to an unexpected result because the evaluation order of initialization list expressions is indeterminate}}
    .other = side_effect()
  };

  // Test array initializer list that initializes structs
  (void)(struct cb_with_other_data[]){
    // expected-warning@+1{{initializer side_effect() has a side effect; this may lead to an unexpected result because the evaluation order of initialization list expressions is indeterminate}} 
    {0, 0x0, side_effect()},
    {0, 0x0, 0x0}
  };

  union UnionWith_cb_with_other_data {
    struct cb_with_other_data u;
    int other;
  };

  (void)(union UnionWith_cb_with_other_data) {
    {
      // expected-warning@+1{{initializer side_effect() has a side effect; this may lead to an unexpected result because the evaluation order of initialization list expressions is indeterminate}}
      .other = side_effect(),
      .buf = 0x0,
      .count = 0
    }
  };

  // Call a function that takes a transparent union
  receive_transparent_union(
    // Test very "untransparent"
    (union TransparentUnion) {.cb = 
      {
        // expected-warning@+1{{initializer side_effect() has a side effect; this may lead to an unexpected result because the evaluation order of initialization list expressions is indeterminate}}
        .other = side_effect(),
        .buf = 0x0,
        .count = 0
      }
    }
  );
  // Call using
  receive_transparent_union(
    // Transparent
    (struct cb_with_other_data){
      // expected-warning@+1{{initializer side_effect() has a side effect; this may lead to an unexpected result because the evaluation order of initialization list expressions is indeterminate}}
      .other = side_effect(),
      .buf = 0x0,
      .count = 0
    }
  );

  // expected-warning@+1{{initializer side_effect() has a side effect; this may lead to an unexpected result because the evaluation order of initialization list expressions is indeterminate}}
  return (struct cb_with_other_data) { 0, 0x0, side_effect()};
}

struct cb_with_other_data global_cb_with_other_data_side_effect_init = 
  (struct cb_with_other_data) {
    .buf = 0x0,
    .count = 0,
    .other = side_effect() // both-error{{initializer element is not a compile-time constant}}
};


struct cb_with_other_data side_effects_in_ptr(struct cb_with_other_data* s) {
  *s = (struct cb_with_other_data){
    0,
    // expected-error@+1{{initalizer for '__counted_by' pointer with side effects is not yet supported}}
    get_single_ptr(),
    0x0
  };

  consume_cb_with_other_data((struct cb_with_other_data){
    0,
    // expected-error@+1{{initalizer for '__counted_by' pointer with side effects is not yet supported}}
    get_single_ptr(),
    0x0}
  );

  (void) (struct cb_with_other_data){
    0,
    // expected-error@+1{{initalizer for '__counted_by' pointer with side effects is not yet supported}}
    get_single_ptr(),
    0x0
  };

  (void)(struct NestedCB) {
    .inner = (struct cb_with_other_data) {
      .count = 0,
      // expected-error@+1{{initalizer for '__counted_by' pointer with side effects is not yet supported}}
      .buf = get_single_ptr(),
      .other = 0
    },
    .count = 0,
    // expected-error@+1{{initalizer for '__counted_by' pointer with side effects is not yet supported}}
    .buf = get_single_ptr(),
    .other = 0
  };

  return (struct cb_with_other_data ){
    0,
    // expected-error@+1{{initalizer for '__counted_by' pointer with side effects is not yet supported}}
    get_single_ptr(),
    0x0
  };
}

struct cb_with_other_data side_effects_in_count(struct cb_with_other_data* s) {
  *s = (struct cb_with_other_data){
    // expected-error@+1{{initalizer for count with side effects is not yet supported}}
    get_count(),
    0x0,
    0x0
  };

  consume_cb_with_other_data((struct cb_with_other_data){
    // expected-error@+1{{initalizer for count with side effects is not yet supported}}
    get_count(),
    0x0,
    0x0}
  );

  (void) (struct cb_with_other_data){
    // expected-error@+1{{initalizer for count with side effects is not yet supported}}
    get_count(),
    0x0,
    0x0
  };

  (void)(struct NestedCB) {
    .inner = (struct cb_with_other_data) {
      // expected-error@+1{{initalizer for count with side effects is not yet supported}}
      .count = get_count(),
      .buf = 0x0,
      .other = 0
    },
    // expected-error@+1{{initalizer for count with side effects is not yet supported}}
    .count = get_count(),
    .buf = 0x0,
    .other = 0
  };

  return (struct cb_with_other_data ){
    // expected-error@+1{{initalizer for count with side effects is not yet supported}}
    get_count(),
    0x0,
    0x0
  };
}

// To keep this test case small just test the `*s = CompoundLiteralExpr` variant
// in the remaining test cases.

void constant_neg_count(struct cb_with_other_data* s) {
  *s = (struct cb_with_other_data){
    -1,
    // expected-error@+1{{negative count value of -1 for 'char *__single __counted_by(count)' (aka 'char *__single')}}
    0x0,
    0x0
  };
}

struct cb_with_other_data global_cb_with_other_data_constant_neg_count = 
  (struct cb_with_other_data) {
    // expected-error@+1{{negative count value of -1 for 'global_cb_with_other_data_constant_neg_count.buf' of type 'char *__single __counted_by(count)' (aka 'char *__single')}}
    .buf = 0x0,
    .count = -1,
    .other = 0x0
};

struct NestedCB global_nested_cb_constant_neg_count = (struct NestedCB) {
  .inner = (struct cb_with_other_data) {
    .count = -1,
      // expected-error@+1{{negative count value of -1 for 'global_nested_cb_constant_neg_count.inner.buf' of type 'char *__single __counted_by(count)' (aka 'char *__single')}}
    .buf = 0x0,
    .other = 0x0
  },
  .count = -1,
  // expected-error@+1{{negative count value of -1 for 'global_nested_cb_constant_neg_count.buf' of type 'char *__single __counted_by(count)' (aka 'char *__single')}}
  .buf = 0x0,
  .other = 0
};

void constant_pos_count_nullptr(struct cb_with_other_data* s) {
  *s = (struct cb_with_other_data){
    100,
    // expected-error@+1{{initializing 'char *__single __counted_by(count)' (aka 'char *__single') and count value of 100 with null always fails}}
    0x0,
    0x0
  };
}

struct cb_with_other_data global_cb_with_other_data_constant_pos_count_nullptr = 
  (struct cb_with_other_data) {
    // expected-error@+1{{initializing 'global_cb_with_other_data_constant_pos_count_nullptr.buf' of type 'char *__single __counted_by(count)' (aka 'char *__single') and count value of 100 with null always fails}}
    .buf = 0x0,
    .count = 100,
    .other = 0x0
};

struct NestedCB global_nested_cb_constant_post_count_nullptr = (struct NestedCB) {
  .inner = (struct cb_with_other_data) {
    .count = 100,
    // expected-error@+1{{initializing 'global_nested_cb_constant_post_count_nullptr.inner.buf' of type 'char *__single __counted_by(count)' (aka 'char *__single') and count value of 100 with null always fails}}
    .buf = 0x0,
    .other = 0x0
    },
  .count = 100,
  // expected-error@+1{{initializing 'global_nested_cb_constant_post_count_nullptr.buf' of type 'char *__single __counted_by(count)' (aka 'char *__single') and count value of 100 with null always fails}}
  .buf = 0x0,
  .other = 0
};

void bad_arr_size(struct cb_with_other_data* s) {
  char arr[3]; // expected-note{{'arr' declared here}}
    *s = (struct cb_with_other_data){
    100,
    // expected-error@+1{{initializing 'char *__single __counted_by(count)' (aka 'char *__single') and count value of 100 with array 'arr' (which has 3 elements) always fails}}
    arr,
    0x0
  };
}

// Can't refer to non constant expressions at file scope so the bad `count`
// diagnostic doesn't get emitted.
char global_arr[3] = {0};
struct cb_with_other_data global_cb_with_other_data_bad_arr_size = 
  (struct cb_with_other_data) {
    .buf = global_arr, // both-error{{initializer element is not a compile-time constant}}
    .count = 100,
    .other = 0x0
};

void bad_count_and_single_ptr_src(struct cb_with_other_data* s, char* ptr) { // expected-note{{consider adding '__counted_by(100)' to 'ptr'}}
  *s = (struct cb_with_other_data){
    100,
    // expected-error@+1{{initializing 'char *__single __counted_by(count)' (aka 'char *__single') and count value of 100 with 'char *__single' always fails}}
    ptr,
    0x0
  };
}

// Can't refer to non constant expressions at file scope so the bad `count`
// diagnostic doesn't get emitted.
char* global_ptr;
struct cb_with_other_data global_cb_with_other_data_bad_count_single_ptr = 
    (struct cb_with_other_data) {
    .buf = global_ptr , // both-error{{initializer element is not a compile-time constant}}
    .count = 100,
    .other = 0x0
};

void bad_implicit_zero_count(struct cb_with_other_data* s, char*__bidi_indexable ptr, struct NestedCB* ncb) {
  *s = (struct cb_with_other_data){
    // expected-warning@+1{{possibly initializing 'char *__single __counted_by(count)' (aka 'char *__single') and implicit count value of 0 with non-null, which creates a non-dereferenceable pointer; explicitly set count value to 0 to remove this warning}}
    .buf = ptr
  };

  *ncb = (struct NestedCB) {
    // expected-warning@+1{{possibly initializing 'char *__single __counted_by(count)' (aka 'char *__single') and implicit count value of 0 with non-null, which creates a non-dereferenceable pointer; explicitly set count value to 0 to remove this warning}}
    .buf = ptr,
    .inner = (struct cb_with_other_data) {
      // expected-warning@+1{{possibly initializing 'char *__single __counted_by(count)' (aka 'char *__single') and implicit count value of 0 with non-null, which creates a non-dereferenceable pointer; explicitly set count value to 0 to remove this warning}}
      .buf = ptr
    }
  };
}

void unknown_count_and_single_ptr_src(struct cb_with_other_data* s, char* ptr, struct NestedCB* ncb) {
  int count = get_count();
  *s = (struct cb_with_other_data){
    count, // expected-note{{count initialized here}}
    // expected-warning@+1{{count value is not statically known: initializing 'char *__single __counted_by(count)' (aka 'char *__single') with 'char *__single' is invalid for any count other than 0 or 1}}
    ptr,
    0x0
  };

  *ncb = (struct NestedCB) {
    .inner = (struct cb_with_other_data) {
      // expected-warning@+1{{count value is not statically known: initializing 'char *__single __counted_by(count)' (aka 'char *__single') with 'char *__single' is invalid for any count other than 0 or 1}}
      .buf = ptr,
      .count = count // expected-note{{count initialized here}}
    },
    // expected-warning@+1{{count value is not statically known: initializing 'char *__single __counted_by(count)' (aka 'char *__single') with 'char *__single' is invalid for any count other than 0 or 1}}
    .buf = ptr,
    .count = count // expected-note{{count initialized here}}
  };
}

void var_init_from_compound_literal_with_side_effect(char*__bidi_indexable ptr) {
  // FIXME: This diagnostic is misleading. The actually restriction is the
  // compound literal can't contain side-effects but the diagnostic talks about
  // "non-constant array". If the side-effect (call to `get_count()`) is removed
  // then this error goes away even though the compound literal is still
  // non-constant due to initializing from `ptr`.
  // both-error@+1{{cannot initialize array of type 'struct cb_with_other_data[]' with non-constant array of type 'struct cb_with_other_data[2]'}}
  struct cb_with_other_data arr[] = (struct cb_with_other_data[]){
    {.buf = ptr, .count = 0x0, .other = 0x0},
    // expected-warning@+1{{initializer get_count() has a side effect; this may lead to an unexpected result because the evaluation order of initialization list expressions is indeterminate}}
    {.buf = ptr, .count = 0x0, .other = get_count()},
  };
}

void call_null_ptr_cb(int new_count) {
  consume_cb_with_other_data((struct cb_with_other_data) {
    .buf = (char*)0,
    .count = new_count
  });
  consume_cb_with_other_data((struct cb_with_other_data) {
    .buf = (void*)0,
    .count = new_count
  });
  consume_cb_with_other_data((struct cb_with_other_data) {
    .buf = ((char*)(void*)(char*)0),
    .count = new_count
  });
}
