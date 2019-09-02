#include "mpc.h"

#include <stdio.h>
#include <stdlib.h>

/* requires libedit-dev from apt */
#include <editline/readline.h>
#include <editline/history.h>

enum lval_type { LVAL_NUM, LVAL_ERR };
enum lerr_type { LERR_DIV_ZERO, LERR_BAD_OP, LERR_BAD_NUM };

/* a lisp value; number or error */

typedef union {
  long num;
  enum lerr_type err;
} long_or_lerr;

typedef struct {
  enum lval_type type;
  long_or_lerr internal;
} lval;

lval lval_num(long x) {
  lval val;
  val.type = LVAL_NUM;
  val.internal.num = x;
  return val;
}

lval lval_err(int x) {
  lval val;
  val.type = LVAL_ERR;
  val.internal.err = x;
  return val;
}

char* lval_error_msg(int err_code) {
  switch (err_code) {
    case LERR_DIV_ZERO:
      return "Error: Division by Zero";
    case LERR_BAD_OP:
      return "Error: Bad Operator";
    case LERR_BAD_NUM:
      return "Error: Bad Number";
  }

  return "Error: Unknown error";
}

void lval_print(lval val) {
  switch (val.type) {
    case LVAL_NUM: printf("%li", val.internal.num); break;
    case LVAL_ERR:
      printf("%s", lval_error_msg(val.internal.err));
      break;
  }
}

void lval_println(lval val) {
  lval_print(val);
  putchar('\n');
}

lval eval_op(char* op, lval x, lval y) {
  if (x.type == LVAL_ERR) { return x; };
  if (y.type == LVAL_ERR) { return y; };

  if (strcmp(op, "+") == 0) { return lval_num(x.internal.num + y.internal.num); }
  if (strcmp(op, "-") == 0) { return lval_num(x.internal.num - y.internal.num); }
  if (strcmp(op, "*") == 0) { return lval_num(x.internal.num * y.internal.num); }

  if (strcmp(op, "/") == 0) {
    return y.internal.num == 0
      ? lval_err(LERR_DIV_ZERO)
      : lval_num(x.internal.num / y.internal.num);
  }

  return lval_err(LERR_BAD_OP);
}

lval eval(mpc_ast_t* tree) {
  if (strstr(tree->tag, "number")) {
    errno = 0;
    long x = strtol(tree->contents, NULL, 10);
    return errno != ERANGE
      ? lval_num(x)
      : lval_err(LERR_BAD_NUM);
  }

  /* the operator will be the second child */
  char* op = tree->children[1]->contents;
  /* the remaining children are the arguments */
  lval result = eval(tree->children[2]);
  int i = 3;
  while (strstr(tree->children[i]->tag, "expr")) {
    lval child = eval(tree->children[i]);
    result = eval_op(op, result, child);
    i++;
  }

  return result;
}

int main(int argc, char** argv) {

  mpc_parser_t* Number = mpc_new("number");
  mpc_parser_t* Operator = mpc_new("operator");
  mpc_parser_t* Expr = mpc_new("expr");
  mpc_parser_t* Lispy = mpc_new("lispy");

  mpca_lang(MPCA_LANG_DEFAULT,
    "                                                   \
     number   : /-?[0-9]+/ ;                            \
     operator : '+' | '-' | '*' | '/' ;                 \
     expr     : <number> | '(' <operator> <expr>+ ')' ; \
     lispy    : /^/ <operator> <expr>+ /$/ ;            \
    ",
    Number, Operator, Expr, Lispy);

  puts("Lispy Version 0.0.0.0.1");
  puts("Press Ctrl+c to Exit\n");

  while (1) {
    char* input = readline("lispy> ");
    add_history(input);

    mpc_result_t r;
    if (mpc_parse("<stdin>", input, Lispy, &r)) {
      lval result = eval(r.output);
      lval_println(result);
      mpc_ast_delete(r.output);
    } else {
      mpc_err_print(r.error);
      mpc_err_delete(r.error);
    }

    /* Free retrieved input */
    free(input);
  }

  mpc_cleanup(4, Number, Operator, Expr, Lispy);

  return 0;
}
