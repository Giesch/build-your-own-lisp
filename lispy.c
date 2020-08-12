#include "mpc.h"

#include <stdio.h>
#include <stdlib.h>

/* requires libedit-dev from apt */
#include <editline/readline.h>
#include <editline/history.h>


// Type Declarations

struct lval;
typedef struct lval lval;
struct lenv;
typedef struct lenv lenv;

typedef enum {
  LVAL_ERR,
  LVAL_NUM,
  LVAL_SYM,
  LVAL_FUN,
  LVAL_SEXPR,
  LVAL_QEXPR
} lval_type;

typedef lval*(*lbuiltin)(lenv*, lval*);

struct lval {
  lval_type type;

  long num;
  char* err;
  char* sym;
  lbuiltin fun;

  int count;
  struct lval** cell;
};

struct lenv {
  int count;
  char** syms;
  lval** vals;
};


// Constructors

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

lval* lval_fun(lbuiltin func) {
  lval* val = malloc(sizeof(lval));
  val->type = LVAL_FUN;
  val->fun = func;
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

lenv* lenv_new(void) {
  lenv* env = malloc(sizeof(lenv));
  env->count = 0;
  env->syms = NULL;
  env->vals = NULL;
  return env;
}


// Destructors

void lval_del(lval* val) {
  switch (val->type) {
    case LVAL_NUM: break;
    case LVAL_FUN: break;

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

void lenv_del(lenv* env) {
  for (int i = 0; i < env->count; i++) {
    free(env->syms[i]);
    lval_del(env->vals[i]);
  }

  free(env->syms);
  free(env->vals);
  free(env);
}


// Utilities

lval* lval_add(lval* val, lval* child) {
  val->count++;
  val->cell = realloc(val->cell, sizeof(lval*) * val->count);
  val->cell[val->count - 1] = child;
  return val;
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

lval* lval_copy(lval* val) {
  lval* result = malloc(sizeof(lval));
  result->type = val->type;

  switch (val->type) {
    case LVAL_FUN: result->fun = val->fun; break;
    case LVAL_NUM: result->num = val->num; break;

    case LVAL_ERR:
      result->err = malloc(strlen(val->err) + 1);
      strcpy(result->err, val->err);
      break;
    case LVAL_SYM:
      result->sym = malloc(strlen(val->err) + 1);
      strcpy(result->sym, val->sym);
      break;

    case LVAL_SEXPR:
    case LVAL_QEXPR:
      result->count = val->count;
      result->cell = malloc(sizeof(lval*) * result->count);
      for (int i = 0; i < val->count; i++) {
        result->cell[i] = lval_copy(val->cell[i]);
      }
      break;
  }

  return result;
}

lval* lenv_get(lenv* env, lval* key) {
  for (int i = 0; i < env->count; i++) {
    if (strcmp(env->syms[i], key->sym) == 0) {
      return lval_copy(env->vals[i]);
    }
  }

  return lval_err("unbound symbol");
}

void lenv_put(lenv* env, lval* key, lval* val) {
  for (int i = 0; i < env->count; i++) {
    if (strcmp(env->syms[i], key->sym) == 0) {
      lval_del(env->vals[i]);
      env->vals[i] = lval_copy(val);
      return;
    }
  }

  env->count++;
  env->syms = realloc(env->syms, sizeof(char*) * env->count);
  env->vals = realloc(env->vals, sizeof(lval*) * env->count);

  env->syms[env->count - 1] = malloc(strlen(key->sym) + 1);
  strcpy(env->syms[env->count - 1], key->sym);

  env->vals[env->count - 1] = lval_copy(val);
}


// Read

lval* lval_read_num(mpc_ast_t* tree) {
  errno = 0;
  long x = strtol(tree->contents, NULL, 10);
  return errno != ERANGE
    ? lval_num(x)
    : lval_err("invalid number");
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


// Print

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
    case LVAL_FUN: printf("<function>"); break;
    case LVAL_SEXPR: lval_expr_print(val, '(', ')'); break;
    case LVAL_QEXPR: lval_expr_print(val, '{', '}'); break;
  }
}

void lval_println(lval* val) {
  lval_print(val);
  putchar('\n');
}


// Eval

lval* lval_eval(lenv* env, lval* val);

#define LASSERT(args, cond, err) \
  if (!(cond)) { lval_del(args); return lval_err(err); }


lval* builtin_add(lenv* env, lval* val) {
  for (int i = 0; i < val->count; i++) {
    LASSERT(val, val->cell[i]->type == LVAL_NUM, "cannot add a non-number");
  }

  lval* acc = lval_pop(val, 0);
  
  while (val->count > 0) {
    lval* arg = lval_pop(val, 0);
    acc->num += arg->num;
    lval_del(arg);
  }

  lval_del(val);
  return acc;
}

lval* builtin_mul(lenv* env, lval* val) {
  for (int i = 0; i < val->count; i++) {
    LASSERT(val, val->cell[i]->type == LVAL_NUM, "cannot multiply a non-number");
  }

  lval* acc = lval_pop(val, 0);
  
  while (val->count > 0) {
    lval* arg = lval_pop(val, 0);
    acc->num *= arg->num;
    lval_del(arg);
  }

  lval_del(val);
  return acc;
}

lval* builtin_sub(lenv* env, lval* val) {
  for (int i = 0; i < val->count; i++) {
    LASSERT(val, val->cell[i]->type == LVAL_NUM, "cannot subtract a non-number");
  }

  lval* acc = lval_pop(val, 0);

  // unary minus
  if (val->count == 0) {
    acc->num = -acc->num;
  }

  while (val->count > 0) {
    lval* arg = lval_pop(val, 0);
    acc->num -= arg->num;
    lval_del(arg);
  }

  lval_del(val);
  return acc;
}

lval* builtin_div(lenv* env, lval* val) {
  for (int i = 0; i < val->count; i++) {
    LASSERT(val, val->cell[i]->type == LVAL_NUM, "cannot divide by a non-number");
  }

  lval* acc = lval_pop(val, 0);
  
  while (val->count > 0) {
    lval* arg = lval_pop(val, 0);

    if (arg->num == 0) {
      lval_del(arg);
      lval_del(acc);
      acc = lval_err("division by zero");
      break;
    }

    acc->num /= arg->num;
    lval_del(arg);
  }

  lval_del(val);
  return acc;
}

lval* builtin_head(lenv* env, lval* val) {
  LASSERT(val, val->count == 1, "Function 'head' passed too many arguments");
  LASSERT(val, val->cell[0]->type == LVAL_QEXPR, "Function 'head' passed incorrect type");
  LASSERT(val, val->cell[0]->count != 0, "Function 'head' passed '{}'");

  lval* result = lval_take(val, 0);
  while (result->count > 1) {
    lval_del(lval_pop(result, 1));
  }

  return result;
}

lval* builtin_tail(lenv* env, lval* val) {
  LASSERT(val, val->count == 1, "Function 'tail' passed too many arguments");
  LASSERT(val, val->cell[0]->type == LVAL_QEXPR, "Function 'tail' passed incorrect type");
  LASSERT(val, val->cell[0]->count != 0, "Function 'tail' passed '{}'");

  lval* result = lval_take(val, 0);
  lval_del(lval_pop(result, 0));

  return result;
}

lval* builtin_list(lenv* env, lval* val) {
  val->type = LVAL_QEXPR;
  return val;
}

lval* builtin_eval(lenv* env, lval* val) {
  LASSERT(val, val->count == 1, "Function 'eval' passed too many arguments");
  LASSERT(val, val->cell[0]->type == LVAL_QEXPR, "Function 'eval' passed incorrect type");

  lval* result = lval_take(val, 0);
  result->type = LVAL_SEXPR;
  return lval_eval(env, result);
}

lval* lval_join(lval* left, lval* right) {
  while (right->count) {
    left = lval_add(left, lval_pop(right, 0));
  }

  lval_del(right);
  return left;
}
lval* builtin_join(lenv* env, lval* val) {
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


lval* lval_eval_sexpr(lenv* env, lval* val) {
  for (int i = 0; i < val->count; i++) {
    val->cell[i] = lval_eval(env, val->cell[i]);
  }

  for (int i = 0; i < val->count; i++) {
    if (val->cell[i]->type == LVAL_ERR) {
      return lval_take(val, i);
    }
  }

  /* check for the empty expr and a single expr */
  if (val->count == 0) {
    return val;
  }
  if (val->count == 1) {
    return lval_take(val, 0);
  }

  /* ensure that the first val is a function */
  lval* first = lval_pop(val, 0);
  if (first->type != LVAL_FUN) {
    lval_del(first);
    lval_del(val);
    return lval_err("S-expr does not start with a function");
  }

  lval* result = first->fun(env, val);
  lval_del(first);
  return result;
}

lval* lval_eval(lenv* env, lval* val) {
  if (val->type == LVAL_SYM) {
    lval* result = lenv_get(env, val);
    lval_del(val);
    return result;
  }

  if (val->type == LVAL_SEXPR) {
    return lval_eval_sexpr(env, val);
  }

  return val;
}


// Main

void lenv_add_builtin(lenv* env, char* name, lbuiltin func) {
  lval* key = lval_sym(name);
  lval* val = lval_fun(func);
  lenv_put(env, key, val);

  lval_del(key);
  lval_del(val);
}

void lenv_add_all_builtins(lenv* env) {
  lenv_add_builtin(env, "list", builtin_list);
  lenv_add_builtin(env, "head", builtin_head);
  lenv_add_builtin(env, "tail", builtin_tail);
  lenv_add_builtin(env, "eval", builtin_eval);
  lenv_add_builtin(env, "join", builtin_join);

  lenv_add_builtin(env, "+", builtin_add);
  lenv_add_builtin(env, "*", builtin_mul);
  lenv_add_builtin(env, "-", builtin_sub);
  lenv_add_builtin(env, "/", builtin_div);
}


int main(int argc, char** argv) {
  mpc_parser_t* Number = mpc_new("number");
  mpc_parser_t* Symbol = mpc_new("symbol");
  mpc_parser_t* SExpr = mpc_new("sexpr");
  mpc_parser_t* QExpr = mpc_new("qexpr");
  mpc_parser_t* Expr = mpc_new("expr");
  mpc_parser_t* Lispy = mpc_new("lispy");


  // Grammar

  mpca_lang(MPCA_LANG_DEFAULT,
    "                                                   \
     number : /-?[0-9]+/ ;                              \
     symbol : /[a-zA-Z0-9_+\\-*\\/\\\\=<>!&]+/ ;        \
     sexpr  : '(' <expr>* ')' ;                         \
     qexpr  : '{' <expr>* '}' ;                         \
     expr   : <number> | <symbol> | <sexpr> | <qexpr> ; \
     lispy  : /^/ <expr>* /$/ ;                         \
    ",
    Number, Symbol, SExpr, QExpr, Expr, Lispy);

  puts("Lispy Version 0.0.0.0.1");
  puts("Press Ctrl+c to Exit\n");


  // REPL

  lenv* env = lenv_new();
  lenv_add_all_builtins(env);

  while (1) {
    char* input = readline("lispy> ");
    add_history(input);

    mpc_result_t r;
    if (mpc_parse("<stdin>", input, Lispy, &r)) {
      lval* val = lval_eval(env, lval_read(r.output));
      lval_println(val);
      lval_del(val);
      mpc_ast_delete(r.output);
    } else {
      mpc_err_print(r.error);
      mpc_err_delete(r.error);
    }

    free(input);
  }

  lenv_del(env);
  mpc_cleanup(6, Number, Symbol, SExpr, QExpr, Expr, Lispy);

  return 0;
}
