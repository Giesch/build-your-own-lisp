#include "mpc.h"

#include <stdio.h>
#include <stdlib.h>

/* requires libedit-dev from apt */
#include <editline/readline.h>
#include <editline/history.h>

typedef enum {
  LVAL_ERR,
  LVAL_NUM,
  LVAL_SYM,
  LVAL_SEXPR,
  LVAL_QEXPR
} lval_type;

typedef struct lval {
  lval_type type;

  long num;
  char* err;
  char* sym;

  int count;
  struct lval** cell;
} lval;

lval* lval_num(long x) {
  lval* val = malloc(sizeof(lval));
  val->type = LVAL_NUM;
  val->num = x;
  return val;
}

lval* lval_err(char* msg) {
  lval* val = malloc(sizeof(lval));
  val->type = LVAL_ERR;
  val->err = malloc(strlen(msg) + 1);
  strcpy(val->err, msg);
  return val;
}

lval* lval_sym(char* sym) {
  lval* val = malloc(sizeof(lval));
  val->type = LVAL_SYM;
  val->sym = malloc(strlen(sym) + 1);
  strcpy(val->sym, sym);
  return val;
}

lval* lval_sexpr(void) {
  lval* val = malloc(sizeof(lval));
  val->type = LVAL_SEXPR;
  val->count = 0;
  val->cell = NULL;
  return val;
}

lval* lval_qexpr(void) {
  lval* val = malloc(sizeof(lval));
  val->type = LVAL_QEXPR;
  val->count = 0;
  val->cell = NULL;
  return val;
}

void lval_del(lval* val) {
  switch (val->type) {
    case LVAL_NUM: break;

    case LVAL_ERR: free(val->err); break;
    case LVAL_SYM: free(val->sym); break;

    case LVAL_SEXPR:
    case LVAL_QEXPR:
      for (int i = 0; i < val->count; i++) {
        lval_del(val->cell[i]);
      }
      free(val->cell);
      break;
  }

  free(val);
}

lval* lval_read_num(mpc_ast_t* tree) {
  errno = 0;
  long x = strtol(tree->contents, NULL, 10);
  return errno != ERANGE
    ? lval_num(x)
    : lval_err("invalid number");
}

lval* lval_add(lval* val, lval* child) {
  val->count++;
  val->cell = realloc(val->cell, sizeof(lval*) * val->count);
  val->cell[val->count - 1] = child;
  return val;
}

lval* lval_read(mpc_ast_t* tree) {
  if (strstr(tree->tag, "number")) { return lval_read_num(tree); }
  if (strstr(tree->tag, "symbol")) { return lval_sym(tree->contents); }

  lval* result = NULL;
  if (strcmp(tree->tag, ">") == 0) { result = lval_sexpr(); }
  if (strstr(tree->tag, "sexpr")) { result = lval_sexpr(); }
  if (strstr(tree->tag, "qexpr")) { result = lval_qexpr(); }

  for (int i = 0; i < tree->children_num; i++) {
    if (strcmp(tree->children[i]->contents, "(") == 0) { continue; }
    if (strcmp(tree->children[i]->contents, ")") == 0) { continue; }
    if (strcmp(tree->children[i]->contents, "{") == 0) { continue; }
    if (strcmp(tree->children[i]->contents, "}") == 0) { continue; }

    if (strcmp(tree->children[i]->tag, "regex") == 0) { continue; }

    result = lval_add(result, lval_read(tree->children[i]));
  }

  return result;
}

void lval_print(lval* val);
void lval_expr_print(lval* val, char open, char close) {
  putchar(open);
  for (int i = 0; i < val->count; i++) {
    lval_print(val->cell[i]);
    if (i != (val->count - 1)) { putchar(' '); }
  }
  putchar(close);
}
void lval_print(lval* val) {
  switch (val->type) {
    case LVAL_ERR: printf("Error: %s", val->err); break;
    case LVAL_NUM: printf("%li", val->num); break;
    case LVAL_SYM: printf("%s", val->sym); break;
    case LVAL_SEXPR: lval_expr_print(val, '(', ')'); break;
    case LVAL_QEXPR: lval_expr_print(val, '{', '}'); break;
  }
}

void lval_println(lval* val) {
  lval_print(val);
  putchar('\n');
}

/* pop the ith expr from an lval; both must be freed later */
lval* lval_pop(lval* val, int i) {
  lval* result = val->cell[i];
  memmove(
    &val->cell[i],
    &val->cell[i + 1],
    sizeof(lval*) * (val->count - i - 1)
  );
  val->count--;
  val->cell = realloc(val->cell, sizeof(lval*) * val->count);
  return result;
}

/* like lval_pop, but it immediately frees the remainder of the lval */
lval* lval_take(lval* val, int i) {
  lval* result = lval_pop(val, i);
  lval_del(val);
  return result;
}

lval* builtin_op(lval* val, char* op) {
  /* ensure all args are numbers */
  for (int i = 0; i < val->count; i++) {
    if (val->cell[i]->type != LVAL_NUM) {
      lval_del(val);
      return lval_err("cannot operate on a non-number");
    }
  }

  /* first arg and accumulator */
  lval* first = lval_pop(val, 0);

  /* handle unary - operator */
  if ((strcmp(op, "-") == 0) && val->count == 0) {
    first->num = -first->num;
  }

  while (val->count > 0) {
    lval* arg = lval_pop(val, 0);
    if (strcmp(op, "+") == 0) { first->num += arg->num; }
    if (strcmp(op, "-") == 0) { first->num -= arg->num; }
    if (strcmp(op, "*") == 0) { first->num *= arg->num; }
    if (strcmp(op, "/") ==0) {
      if (arg->num == 0) {
        lval_del(first);
        lval_del(arg);
        first = lval_err("division by zero");
        break;
      }
      first->num /= arg->num;
    }

    lval_del(arg);
  }

  lval_del(val);
  return first;
}

#define LASSERT(args, cond, err) \
  if (!(cond)) { lval_del(args); return lval_err(err); }

lval* builtin_head(lval* val) {
  LASSERT(val, val->count == 1, "Function 'head' passed too many arguments");
  LASSERT(val, val->cell[0]->type == LVAL_QEXPR, "Function 'head' passed incorrect type");
  LASSERT(val, val->cell[0]->count != 0, "Function 'head' passed '{}'");

  lval* result = lval_take(val, 0);
  while (result->count > 1) {
    lval_del(lval_pop(result, 1));
  }

  return result;
}

lval* builtin_tail(lval* val) {
  LASSERT(val, val->count == 1, "Function 'tail' passed too many arguments");
  LASSERT(val, val->cell[0]->type == LVAL_QEXPR, "Function 'tail' passed incorrect type");
  LASSERT(val, val->cell[0]->count != 0, "Function 'tail' passed '{}'");

  lval* result = lval_take(val, 0);
  lval_del(lval_pop(result, 0));

  return result;
}

lval* builtin_list(lval* val) {
  val->type = LVAL_QEXPR;
  return val;
}

lval* lval_eval(lval* val);

lval* builtin_eval(lval* val) {
  LASSERT(val, val->count == 1, "Function 'eval' passed too many arguments");
  LASSERT(val, val->cell[0]->type == LVAL_QEXPR, "Function 'eval' passed incorrect type");

  lval* result = lval_take(val, 0);
  result->type = LVAL_SEXPR;
  return lval_eval(result);
}

lval* lval_join(lval* left, lval* right) {
  while (right->count) {
    left = lval_add(left, lval_pop(right, 0));
  }

  lval_del(right);
  return left;
}

lval* builtin_join(lval* val) {
  for (int i = 0; i < val->count; i++) {
    LASSERT(val, val->cell[i]->type == LVAL_QEXPR, "Function 'join' passed incorrect type");
  }

  lval* result = lval_pop(val, 0);

  while (val->count) {
    result = lval_join(val, lval_pop(val, 0));
  }

  lval_del(val);
  return result;
}

lval* builtin(lval* val, char* func) {
  if (strcmp("list", func) == 0) { return builtin_list(val); }
  if (strcmp("head", func) == 0) { return builtin_head(val); }
  if (strcmp("tail", func) == 0) { return builtin_tail(val); }
  if (strcmp("join", func) == 0) { return builtin_join(val); }
  if (strcmp("eval", func) == 0) { return builtin_eval(val); }
  if (strcmp("+-/*", func)) { return builtin_op(val, func); }

  lval_del(val);
  return lval_err("Unknown function");
}

lval* lval_eval_sexpr(lval* val) {
  /* eval each child */
  for (int i = 0; i < val->count; i++) {
    val->cell[i] = lval_eval(val->cell[i]);
  }

  /* return the first err if there is one */
  for (int i = 0; i < val->count; i++) {
    if (val->cell[i]->type == LVAL_ERR) { return lval_take(val, i); }
  }

  /* check for the empty expr and a single expr */
  if (val->count == 0) { return val; }
  if (val->count == 1) { return lval_take(val, 0); }

  /* ensure that the first val is a sym */
  lval* first = lval_pop(val, 0);
  if (first->type != LVAL_SYM) {
    lval_del(first);
    lval_del(val);
    return lval_err("S-expr does not start with a symbol");
  }

  lval* result = builtin(val, first->sym);
  lval_del(first);
  return result;
}
lval* lval_eval(lval* val) {
  if (val->type == LVAL_SEXPR) { return lval_eval_sexpr(val); }
  return val;
}

int main(int argc, char** argv) {
  mpc_parser_t* Number = mpc_new("number");
  mpc_parser_t* Symbol = mpc_new("symbol");
  mpc_parser_t* SExpr = mpc_new("sexpr");
  mpc_parser_t* QExpr = mpc_new("qexpr");
  mpc_parser_t* Expr = mpc_new("expr");
  mpc_parser_t* Lispy = mpc_new("lispy");

  mpca_lang(MPCA_LANG_DEFAULT,
    "                                                              \
     number : /-?[0-9]+/ ;                                         \
     symbol : \"list\" | \"head\" | \"tail\" | \"join\" | \"eval\" \
            | '+' | '-' | '*' | '/' ;                              \
     sexpr  : '(' <expr>* ')' ;                                    \
     qexpr  : '{' <expr>* '}' ;                                    \
     expr   : <number> | <symbol> | <sexpr> | <qexpr> ;            \
     lispy  : /^/ <expr>* /$/ ;                                    \
    ",
    Number, Symbol, SExpr, QExpr, Expr, Lispy);

  puts("Lispy Version 0.0.0.0.1");
  puts("Press Ctrl+c to Exit\n");

  while (1) {
    char* input = readline("lispy> ");
    add_history(input);

    mpc_result_t r;
    if (mpc_parse("<stdin>", input, Lispy, &r)) {
      lval* val = lval_eval(lval_read(r.output));
      lval_println(val);
      lval_del(val);
      mpc_ast_delete(r.output);
    } else {
      mpc_err_print(r.error);
      mpc_err_delete(r.error);
    }

    free(input);
  }

  mpc_cleanup(4, Number, Symbol, SExpr, QExpr, Expr, Lispy);
  return 0;
}
