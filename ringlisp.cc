// g++ -Wall --std=c++17 ringlisp.cc
#include <array>
#include <charconv>
#include <ctype.h>
#include <iostream>
#include <map>
#include <memory>
#include <stdio.h>
#include <string>
#include <string_view>
#include <vector>

using std::int64_t;
using std::uint8_t;
using std::uint64_t;
using std::uintptr_t;

constexpr int kWordSize = 16;
constexpr int kConsAreaByteSize = 1024 * kWordSize;

constexpr char kLPar = '(';
constexpr char kRPar = ')';
constexpr char kQuote = '\'';

struct Cons {
  uintptr_t car;
  uintptr_t cdr;
};

enum class Type {
  kNil,
  kSym,
  kSbr,
  kErr,
  kStl,
};

typedef uintptr_t(*Subr)(uintptr_t);

struct alignas(kWordSize) Data {
  Type type;
  union {
    std::string* str;
    uintptr_t obj;
    Subr func;
  } data;

  explicit Data(Type t) : type(t) {}
};

std::vector<uint8_t>* cons_area;
void* cons_area_begin;
void* cons_area_end;
uint8_t* alloc_head;
uint8_t* saved_area_end;
int generation;

std::vector<std::unique_ptr<std::string>>* symbols;
std::map<std::string, Data*>* symbol_map;

uintptr_t nil;
uintptr_t sym_t;
uintptr_t sym_quote;
uintptr_t sym_if;
uintptr_t sym_lambda;
uintptr_t sym_defun;
uintptr_t sym_setq;
uintptr_t sym_expr;
uintptr_t g_env;
uintptr_t user_env;

void initHeap() {
  generation = 0;
  cons_area = new std::vector<uint8_t>(kConsAreaByteSize);
  cons_area_begin =
    reinterpret_cast<void*>(
        (reinterpret_cast<uintptr_t>(cons_area->data()) + (kWordSize - 1))
        & ~(kWordSize - 1));
  cons_area_end =
    reinterpret_cast<void*>(
        (reinterpret_cast<uintptr_t>(cons_area->data()) + kConsAreaByteSize)
        & ~(kWordSize - 1));
  alloc_head = reinterpret_cast<uint8_t*>(cons_area_begin);
  saved_area_end = alloc_head;

  symbols = new std::vector<std::unique_ptr<std::string>>;
  symbol_map = new std::map<std::string, Data*>;
}

void* alloc() {
  if (alloc_head >= cons_area_end) {
    alloc_head = saved_area_end;
    generation = (generation + 1) & ((kWordSize - 1) >> 1);
    std::cout << "... generation: " << generation << std::endl;
  }
  void* ret = reinterpret_cast<void*>(alloc_head);
  alloc_head += kWordSize;
  return ret;
}

void* ptr(uintptr_t obj) {
  return reinterpret_cast<void*>(obj & ~(kWordSize - 1));
}

int gen(uintptr_t obj) {
  return static_cast<int>((obj & (kWordSize - 1)) >> 1);
}

int current_gen(uintptr_t obj) {
  void* p = ptr(obj);
  void* head = reinterpret_cast<void*>(alloc_head);
  if (p >= head) {
    return (generation - 1) & ((kWordSize - 1) >> 1);
  }
  return generation;
}

bool isFnum(uintptr_t obj) {
  return obj & 1;
}

bool isCons(uintptr_t obj) {
  void* p = ptr(obj);
  return !isFnum(obj) && cons_area_begin <= p && p < cons_area_end;
}

Cons* toCons(uintptr_t cons) {
  return reinterpret_cast<Cons*>(ptr(cons));
}

int64_t fnum(uintptr_t obj) {
  return static_cast<int64_t>(obj) >> 1;
}

Data* toData(uintptr_t data) {
  return reinterpret_cast<Data*>(ptr(data));
}

bool isType(uintptr_t obj, Type type) {
  return !isFnum(obj) && !isCons(obj) && toData(obj)->type == type;
}

bool isFragile(uintptr_t obj) {
  return isCons(obj) && obj >= reinterpret_cast<uintptr_t>(saved_area_end);
}

bool isStale(uintptr_t obj) {
  return (isFragile(obj) && gen(obj) != current_gen(obj)) ||
         isType(obj, Type::kStl);
}

// Used as a breakpoint for debuggers.
void bp(uintptr_t obj) {}

#define RETURN_IF_STALE(obj) \
  if (isStale(obj)) { bp(obj);  return makeStale(obj); }

#define RETURN_IF_ERROR(obj) \
  if (isType(obj, Type::kErr)) { bp(obj); return obj; }

uintptr_t makeCons(uintptr_t car, uintptr_t cdr) {
  Cons* cons = reinterpret_cast<Cons*>(alloc());
  cons->car = car;
  cons->cdr = cdr;
  uintptr_t ret = reinterpret_cast<uintptr_t>(cons);
  ret |= (generation << 1);
  return ret;
}

uintptr_t makeSymbol(const std::string& name) {
  auto it = symbol_map->find(name);
  if (it == symbol_map->end() || it->second == nullptr) {
    Data* d = new Data(Type::kSym);
    auto str = std::make_unique<std::string>(name);
    d->data.str = str.get();
    symbols->push_back(std::move(str));
    auto [ret, _] = symbol_map->insert({name, d});
    return  reinterpret_cast<uintptr_t>(ret->second);
  } else {
    return  reinterpret_cast<uintptr_t>(it->second);
  }
}

uintptr_t makeNil() {
  Data* nl = toData(makeSymbol("nil"));
  nl->type = Type::kNil;
  return  reinterpret_cast<uintptr_t>(nl);
}

uintptr_t makeError(const std::string& msg) {
  Data* sym = toData(makeSymbol(msg));
  Data* err = new Data(Type::kErr);
  err->data.str = sym->data.str;
  return  reinterpret_cast<uintptr_t>(err);
}

uintptr_t makeFixnum(int64_t num) {
  return static_cast<uintptr_t>((num << 1) | 1);
}

uintptr_t makeStale(uintptr_t obj) {
  Data* d = new Data(Type::kStl);
  d->data.obj = obj;
  return  reinterpret_cast<uintptr_t>(d);
}

uintptr_t makeSubr(Subr func) {
  Data* d = new Data(Type::kSbr);
  d->data.func = func;
  return reinterpret_cast<uintptr_t>(d);
}

bool isDelimiter(char c) {
  return c == kLPar || c == kRPar || c == kQuote || isspace(c);
}

std::string_view skipSpaces(std::string_view str) {
  int i;
  for (i = 0; i < str.size(); ++i) {
    if (!isspace(str[i])) {
      break;
    }
  }
  return str.substr(i, str.size() - i);
}

uintptr_t makeNumOrSym(std::string_view str) {
  int64_t num = 0;
  auto [p, _] = std::from_chars(str.data(), str.data() + str.size(), num);
  if (p != str.data() + str.size()) {
    return makeSymbol(std::string(str));
  }
  return makeFixnum(num);
}

uintptr_t readAtom(std::string_view* str) {
  std::string_view atom_str;
  int i;
  for (i = 0; i < str->size(); ++i) {
    if (isDelimiter((*str)[i])) {
      break;
    }
  }
  atom_str = str->substr(0, i);
  *str = str->substr(i);
  return makeNumOrSym(atom_str);
}

uintptr_t nreverse(uintptr_t lst) {
  RETURN_IF_STALE(lst);
  uintptr_t ret = nil;
  while (isCons(lst)) {
    Cons* cons = toCons(lst);
    uintptr_t tmp = cons->cdr;
    cons->cdr = ret;
    ret = lst;
    lst = tmp;
    RETURN_IF_STALE(lst);
  }
  return ret;
}

uintptr_t read(std::string_view* str);
uintptr_t readList(std::string_view* str) {
  uintptr_t ret = nil;
  for (;;) {
    *str = skipSpaces(*str);
    if (str->empty()) {
      return makeError("unfinished parenthesis");
    } else if ((*str)[0] == kRPar) {
      *str = str->substr(1);
      break;
    }
    uintptr_t elm = read(str);
    RETURN_IF_ERROR(elm);
    ret = makeCons(elm, ret);
  }
  return nreverse(ret);
}

uintptr_t read(std::string_view* str) {
  *str = skipSpaces(*str);
  if (str->empty()) {
    return makeError("empty input");
  } else if ((*str)[0] == kRPar) {
    return makeError("invalid syntax");
  } else if ((*str)[0] == kLPar) {
    *str = str->substr(1);
    return readList(str);
  } else if ((*str)[0] == kQuote) {
    *str = str->substr(1);
    uintptr_t elm = read(str);
    return makeCons(makeSymbol("quote"), makeCons(elm, nil));
  }
  return readAtom(str);
}

uintptr_t safeCar(uintptr_t obj) {
  RETURN_IF_STALE(obj);
  if (isCons(obj)) {
    return toCons(obj)->car;
  }
  return nil;
}

uintptr_t safeCdr(uintptr_t obj) {
  RETURN_IF_STALE(obj);
  if (isCons(obj)) {
    return toCons(obj)->cdr;
  }
  return nil;
}

std::string objToString(uintptr_t obj);
std::string listToString(uintptr_t obj) {
  std::string ret;
  bool first = true;
  while (isCons(obj)) {
    if (first) {
      first = false;
    } else {
      ret += ' ';
    }
    Cons* cons = toCons(obj);
    ret += objToString(cons->car);
    obj = cons->cdr;
  }
  if (obj == nil) {
    return '(' + ret + ')';
  }
  return '(' + ret + " . " + objToString(obj) + ')';
}

std::string objToString(uintptr_t obj) {
  if (isCons(obj)) {
    if (safeCar(obj) == sym_expr) {
      return "<expr>";
    } else {
      return listToString(obj);
    }
  } else if (isFnum(obj)) {
    return std::to_string(fnum(obj));
  } else {
    Data* d = toData(obj);
    switch (d->type) {
    case Type::kNil:
      return "nil";
    case Type::kSym:
      return *d->data.str;
    case Type::kSbr:
      return "<subr>";
    case Type::kErr:
      return "<error: " + *d->data.str + ">";
    case Type::kStl: {
      std::array<char, 64> buf;
      auto [p, _] =
        std::to_chars(buf.data(), buf.data() + buf.size(), d->data.obj, 16);
      std::string hex(buf.data(), p - buf.data());
      return "<stale value: " + hex + ">";
    }
    default:
      return "<unknown object>";
    }
  }
}

uintptr_t findVar(uintptr_t sym, uintptr_t env) {
  while (isCons(env)) {
    if (isStale(env)) {
      env = user_env;
    }
    uintptr_t alist = toCons(env)->car;
    while (isCons(alist)) {
      if (isStale(alist)) {
        break;
      }
      if (safeCar(safeCar(alist)) == sym) {
        return safeCar(alist);
      }
      alist = safeCdr(alist);
    }
    env = toCons(env)->cdr;
  }
  return nil;
}

uintptr_t addToEnv(uintptr_t sym, uintptr_t val, uintptr_t env) {
  RETURN_IF_STALE(env);
  Cons* e = toCons(env);
  uintptr_t head = makeCons(makeCons(sym, val), e->car);
  RETURN_IF_STALE(env);
  e->car = head;
  return nil;
}

uintptr_t makeExpr(uintptr_t obj, uintptr_t env) {
  return makeCons(sym_expr, makeCons(env, obj));
}

uintptr_t pairlis(uintptr_t lst1, uintptr_t lst2) {
  RETURN_IF_STALE(lst1);
  RETURN_IF_STALE(lst2);
  uintptr_t ret = nil;
  while (isCons(lst1) && isCons(lst2)) {
    uintptr_t x = safeCar(lst1);
    uintptr_t y = safeCar(lst2);
    lst1 = safeCdr(lst1);
    lst2 = safeCdr(lst2);
    ret = makeCons(makeCons(x, y), ret);
    RETURN_IF_STALE(lst1);
    RETURN_IF_STALE(lst2);
  }
  return nreverse(ret);
}

uintptr_t eval(uintptr_t obj, uintptr_t env);
uintptr_t evlis(uintptr_t lst, uintptr_t env) {
  RETURN_IF_STALE(lst);
  uintptr_t ret = nil;
  while (isCons(lst)) {
    uintptr_t a = safeCar(lst);
    lst = safeCdr(lst);
    uintptr_t elm = eval(a, env);
    RETURN_IF_ERROR(elm);
    ret = makeCons(elm, ret);
    RETURN_IF_STALE(lst);
  }
  return nreverse(ret);
}

uintptr_t eval(uintptr_t obj, uintptr_t env) {
 eval:  // eval(obj, env)
  if (isType(obj, Type::kNil) || isType(obj, Type::kErr) ||
      isType(obj, Type::kStl) || isType(obj, Type::kSbr) || isFnum(obj)) {
    return obj;
  } else if (isType(obj, Type::kSym)) {
    uintptr_t bind = findVar(obj, env);
    if (bind == nil) {
      return makeError(*toData(obj)->data.str + " has no value");
    }
    return toCons(bind)->cdr;
  } else if (!isCons(obj)) {
    return makeError("unknown object");
  }
  RETURN_IF_STALE(obj);
  uintptr_t op = safeCar(obj);
  RETURN_IF_STALE(op);
  uintptr_t args = safeCdr(obj);
  RETURN_IF_STALE(args);
  if (op == sym_quote) {
    return safeCar(args);
  } else if (op == sym_if) {
    uintptr_t c = eval(safeCar(args), env);
    RETURN_IF_ERROR(c);
    RETURN_IF_STALE(c);
    if (c == nil) {
      // Call eval(obj, env)
      obj = safeCar(safeCdr(safeCdr(args)));
      goto eval;
    } else {
      // Call eval(obj, env)
      obj = safeCar(safeCdr(args));
      goto eval;
    }
  } else if (op == sym_lambda) {
    return makeExpr(args, env);
  } else if (op == sym_defun) {
    uintptr_t expr = makeExpr(safeCdr(args), env);
    RETURN_IF_STALE(expr);
    uintptr_t sym = safeCar(args);
    if (!isType(sym, Type::kSym)) {
      return makeError("1st argument of defun must be a symbol");
    }
    addToEnv(sym, expr, user_env);
    return sym;
  } else if (op == sym_setq) {
    uintptr_t val = eval(safeCar(safeCdr(args)), env);
    RETURN_IF_ERROR(val);
    RETURN_IF_STALE(val);
    uintptr_t sym = safeCar(args);
    if (!isType(sym, Type::kSym)) {
      return makeError("1st argument of setq must be a symbol");
    }
    uintptr_t bind = findVar(sym, env);
    if (bind == nil) {
      addToEnv(sym, val, user_env);
    } else if (bind < reinterpret_cast<uintptr_t>(saved_area_end)) {
      return makeError(objToString(sym) + " is immutable");
    } else {
      toCons(bind)->cdr = val;
    }
    return val;
  }

  // Call apply(fn, args, env)
  uintptr_t fn = eval(op, env);
  args = evlis(args, env);
 apply:  // apply(fn, args, env)
  uintptr_t body;
  RETURN_IF_STALE(fn);
  RETURN_IF_STALE(args);
  RETURN_IF_ERROR(fn);
  RETURN_IF_ERROR(args);
  if (isType(fn, Type::kSbr)) {
    return toData(fn)->data.func(args);
  } else if (isCons(fn)) {
    if (safeCar(fn) == sym_expr) {
      uintptr_t o = safeCdr(fn);  // o = (env args . body)
      RETURN_IF_STALE(o);
      uintptr_t e = safeCar(o);
      RETURN_IF_STALE(e)
      o = safeCdr(o);  // o = (args . body)
      RETURN_IF_STALE(o);
      uintptr_t a = safeCar(o);
      RETURN_IF_STALE(a);
      // Call progn(body, env)
      body = safeCdr(o);
      env = makeCons(pairlis(a, args), e);
      goto progn;
    } else if (safeCar(fn) == sym_lambda) {
      uintptr_t o = safeCdr(fn);  // o = (args . body)
      RETURN_IF_STALE(o);
      uintptr_t a = safeCar(o);
      RETURN_IF_STALE(a);
      // Call progn(body, env)
      body = safeCdr(o);
      env = makeCons(pairlis(a, args), user_env);
      goto progn;
    }
  }
  return makeError("noimpl");

 progn:  // progn(body, env)
  RETURN_IF_STALE(body);
  uintptr_t ret = nil;
  while (isCons(body)) {
    Cons* c = toCons(body);
    body = c->cdr;
    if (body == nil) {
      // Call eval(obj, env)
      obj = c->car;
      goto eval;
    }
    ret = eval(c->car, env);
    RETURN_IF_ERROR(ret);
    RETURN_IF_STALE(body);
  }
  return ret;
}

uintptr_t subrCar(uintptr_t args) {
  return safeCar(safeCar(args));
}

uintptr_t subrCdr(uintptr_t args) {
  return safeCdr(safeCar(args));
}

uintptr_t subrCons(uintptr_t args) {
  return makeCons(safeCar(args), safeCar(safeCdr(args)));
}

uintptr_t subrEq(uintptr_t args) {
  uintptr_t x = safeCar(args);
  uintptr_t y = safeCar(safeCdr(args));
  return x == y ? sym_t : nil;
}

uintptr_t subrAtom(uintptr_t args) {
  return isCons(safeCar(args)) ? nil : sym_t;
}

uintptr_t subrNumberp(uintptr_t args) {
  return isFnum(safeCar(args)) ? sym_t : nil;
}

uintptr_t subrSymbolp(uintptr_t args) {
  return isType(safeCar(args), Type::kSym) ? sym_t : nil;
}

uintptr_t addOrMul(std::function<int64_t(int64_t, int64_t)> fn, int64_t init,
                   uintptr_t args) {
  RETURN_IF_STALE(args);
  int64_t ret = init;
  while (isCons(args)) {
    uintptr_t a = safeCar(args);
    args = safeCdr(args);
    if (!isFnum(a)) {
      return makeError("number is expected");
    }
    ret = fn(ret, fnum(a));
    RETURN_IF_STALE(args);
  }
  return makeFixnum(ret);
}

uintptr_t subrAdd(uintptr_t args) {
  return addOrMul([](int64_t x, int64_t y) { return x + y; }, 0, args);
}

uintptr_t subrMul(uintptr_t args) {
  return addOrMul([](int64_t x, int64_t y) { return x * y; }, 1, args);
}

uintptr_t subOrDivOrMod(std::function<int64_t(int64_t, int64_t)> fn,
                        uintptr_t args) {
  uintptr_t x = safeCar(args);
  RETURN_IF_STALE(x);
  uintptr_t y = safeCar(safeCdr(args));
  RETURN_IF_STALE(y);
  if (!isFnum(x) || !isFnum(y)) {
    return makeError("number is expected");
  }
  return makeFixnum(fn(fnum(x), fnum(y)));
}

uintptr_t subrSub(uintptr_t args) {
  return subOrDivOrMod([](int64_t x, int64_t y) { return x - y; }, args);
}

uintptr_t subrDiv(uintptr_t args) {
  return subOrDivOrMod([](int64_t x, int64_t y) { return x / y; }, args);
}

uintptr_t subrMod(uintptr_t args) {
  return subOrDivOrMod([](int64_t x, int64_t y) { return x % y; }, args);
}

uintptr_t subrList(uintptr_t args) {
  return args;
}

uintptr_t copyRec(uintptr_t obj) {
  if (isCons(obj)) {
    RETURN_IF_STALE(obj);
    return makeCons(copyRec(safeCar(obj)), copyRec(safeCdr(obj)));
  } else {
    return obj;
  }
}

uintptr_t subrCopy(uintptr_t args) {
  return copyRec(safeCar(args));
}

int main() {
  initHeap();
  nil = makeNil();
  sym_t = makeSymbol("t");
  sym_quote = makeSymbol("quote");
  sym_if = makeSymbol("if");
  sym_lambda = makeSymbol("lambda");
  sym_defun = makeSymbol("defun");
  sym_setq = makeSymbol("setq");
  sym_expr = makeSymbol("expr");  // only for internal use
  symbol_map->erase("expr");  // unintern expr not to expose it

  g_env = makeCons(nil, nil);
  addToEnv(sym_t, sym_t, g_env);
  addToEnv(makeSymbol("car"), makeSubr(subrCar), g_env);
  addToEnv(makeSymbol("cdr"), makeSubr(subrCdr), g_env);
  addToEnv(makeSymbol("cons"), makeSubr(subrCons), g_env);
  addToEnv(makeSymbol("eq"), makeSubr(subrEq), g_env);
  addToEnv(makeSymbol("atom"), makeSubr(subrAtom), g_env);
  addToEnv(makeSymbol("numberp"), makeSubr(subrNumberp), g_env);
  addToEnv(makeSymbol("symbolp"), makeSubr(subrSymbolp), g_env);
  addToEnv(makeSymbol("+"), makeSubr(subrAdd), g_env);
  addToEnv(makeSymbol("*"), makeSubr(subrMul), g_env);
  addToEnv(makeSymbol("-"), makeSubr(subrSub), g_env);
  addToEnv(makeSymbol("/"), makeSubr(subrDiv), g_env);
  addToEnv(makeSymbol("mod"), makeSubr(subrMod), g_env);
  addToEnv(makeSymbol("list"), makeSubr(subrList), g_env);
  addToEnv(makeSymbol("copy"), makeSubr(subrCopy), g_env);
  user_env = makeCons(nil, g_env);

  saved_area_end = alloc_head;

  std::string str;
  printf("> ");
  while (std::getline(std::cin, str)) {
    std::string_view view(str);
    std::cout << objToString(eval(read(&view), user_env)) << std::endl;
    printf("> ");
  }

  return 0;
}
