#include "mpc.h"

#include <stdio.h>
#include <stdlib.h>

/* requires libedit-dev from apt */
#include <editline/readline.h>
#include <editline/history.h>

long eval_op(char* op, long x, long y) {
  if (strcmp(op, "+") == 0) { return x + y; }
  if (strcmp(op, "-") == 0) { return x - y; }
  if (strcmp(op, "*") == 0) { return x * y; }
  if (strcmp(op, "/") == 0) { return x / y; }
  return 0;
}

long eval(mpc_ast_t* tree) {
  if (strstr(tree->tag, "number")) {
    return atoi(tree->contents);
  }

  /* the operator will be the second child */
  char* op = tree->children[1]->contents;

  /* the remaining children are the arguments */
  long result = eval(tree->children[2]);
  int i = 3;
  while (strstr(tree->children[i]->tag, "expr")) {
    long child = eval(tree->children[i]);
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
      long result = eval(r.output);
      printf("%li\n", result);
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
