#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t byte;
typedef double f64;
typedef uint64_t u64;

#define MAX(x, y) ((x) >= (y) ? (x) : (y))

void *xrealloc(void *ptr, size_t new_size)
{
    ptr = realloc(ptr, new_size);
    if (!ptr) {
        perror("realloc failed");
        exit(1);
    }
    return ptr;
}

void *xmalloc(size_t size)
{
    void *ptr = malloc(size);
    if (!ptr) {
        perror("malloc failed");
        exit(1);
    }
    return ptr;
}

void fatal(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    printf("FATAL: ");
    vprintf(fmt, args);
    printf("\n");
    va_end(args);
    exit(1);
}

void syntax_error(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    printf("SYNTAX ERROR: ");
    vprintf(fmt, args);
    printf("\n");
    va_end(args);
}

typedef struct {
    size_t len;
    size_t cap;
    char buf[];
} buf_hdr_t;

// clang-format off
#define buf__raw(b) ((size_t *)(b)-2)
#define buf__len(b) buf__raw(b)[0]
#define buf__cap(b) buf__raw(b)[1]

#define buf__fit(b, n)  (buf__fits(b, n) ? 0 : buf__grow(b, buf_len(b)+(n)))
#define buf__fits(b, n) ((b) && buf__len(b)+(n) <= buf__cap(b))
#define buf__grow(b, n) (*((void **)&(b)) = buf___grow((b), (n), sizeof(*(b))))

#define buf_cap(b)        ((b) ? buf__cap(b) : 0)
#define buf_end(b)        ((b) + buf_len(b))
#define buf_free(b)       ((b) ? (free(buf__raw(b)), (b) = NULL) : 0)
#define buf_hdr(b)        ((buf_hdr_t *)buf__raw(b))
#define buf_len(b)        ((b) ? buf__len(b) : 0)
#define buf_push(b, x)    (buf__fit(b, 1), (b)[buf__len(b)++] = (x))
#define buf_reserve(b, n) (buf__fit(b, n), (b)[buf__len(b)])

// for use in debugger
size_t bufcap(const void *b) { return buf_cap(b); }
size_t buflen(const void *b) { return buf_len(b); }
buf_hdr_t *bufhdr(const void *b) { return buf_hdr(b); }

void *buf___grow(const void *b, size_t len, size_t elem_size)
{
    assert(buf_cap(b) <= (SIZE_MAX - 1)/2);
    size_t cap = MAX(2 * buf_cap(b), len);
    assert(len <= cap && cap <= (SIZE_MAX - offsetof(buf_hdr_t, buf))/elem_size);
    size_t size = offsetof(buf_hdr_t, buf) + elem_size * cap;
    buf_hdr_t *hdr = (buf_hdr_t *)xrealloc(b ? buf__raw(b) : NULL, size);
    hdr->cap = cap;
    if (!b) hdr->len = 0;
    return hdr->buf;
}
// clang-format on

void buf_test(void)
{
    // setup
    int *b = NULL;
    int n = 1024;

    assert(b == buf_end(b));
    assert(buf_len(b) == 0);
    assert(buf_cap(b) == 0);

    // push increases len and cap
    assert(buf_len(b) == 0);
    for (int i = 0; i < n; i++) {
        buf_push(b, i);
    }
    assert(buf_len(b) == n);
    assert(buf_cap(b) >= n);
    assert(buf_end(b) - b == n);

    // push sets values
    for (int i = 0, max = buf_len(b); i < max; i++) {
        assert(b[i] == i);
    }

    buf_free(b);
    assert(buf_free(b) == NULL);
    assert(!buf_len(b));
}

typedef struct {
    size_t len;
    const char *str;
} intern_t;

static intern_t *interns;

const char *str_intern_range(const char *restrict start, const char *restrict end)
{
    size_t len = end - start;
    for (intern_t *it = interns; it != buf_end(interns); it++) {
        if (it->len == len && strncmp(it->str, start, len) == 0) {
            return it->str;
        }
    }

    char *str = xmalloc(len + 1);
    memcpy(str, start, len);
    str[len] = 0;
    buf_push(interns, ((intern_t){ len, str }));
    return str;
}

const char *str_intern(const char *str)
{
    return str_intern_range(str, str + strlen(str));
}

void str_intern_test()
{
    char a[] = "hello";
    assert(strcmp(a, str_intern(a)) == 0);
    assert(str_intern(a) == str_intern(a));
    assert(str_intern(str_intern(a)) == str_intern(a));
    char b[] = "hello";
    assert(a != b);
    assert(str_intern(a) == str_intern(b));
    char c[] = "hello!";
    assert(str_intern(a) != str_intern(c));
    char d[] = "hell";
    assert(str_intern(a) != str_intern(d));
}

typedef enum {
    // Reserve first 128 values for one-char tokens
    TOKEN_LAST_CHAR = 127,
    TOKEN_INT,
    TOKEN_FLOAT,
    TOKEN_NAME,
    // ...
} TokenKind;

typedef enum {
    TOKENMOD_NONE,
    TOKENMOD_BIN,
    TOKENMOD_OCT,
    TOKENMOD_DEC,
    TOKENMOD_HEX,
    TOKENMOD_CHAR,
    // ...
} TokenMod;

const char *token_kind_names[] = {
    // clang-format off
    [TOKEN_INT]  = "TOKEN_INT",
    [TOKEN_NAME] = "TOKEN_NAME",
    // clang-format on
};

typedef struct {
    TokenKind kind;
    TokenMod mod;
    const char *start;
    const char *end;
    union {
        f64 float_val;
        u64 int_val;
        const char *name;
    };
} Token;

const char *token_kind_name(TokenKind kind)
{
    if (kind > TOKEN_LAST_CHAR) {
        return token_kind_names[kind];
    }
    return "ASCII";
}

Token token;
const char *stream;

const char *keyword_if;
const char *keyword_for;
const char *keyword_while;

void init_keywords()
{
    keyword_if = str_intern("if");
    keyword_for = str_intern("for");
    keyword_while = str_intern("while");
    // ...
}

// clang format off
u64 char_to_digit[256] = {
    ['0'] = 0,  ['1'] = 1,  ['2'] = 2,  ['3'] = 3,  ['4'] = 4,  ['5'] = 5,
    ['6'] = 6,  ['7'] = 7,  ['8'] = 8,  ['9'] = 9,  ['a'] = 10, ['b'] = 11,
    ['c'] = 12, ['d'] = 13, ['e'] = 14, ['f'] = 15, ['A'] = 10, ['B'] = 11,
    ['C'] = 12, ['D'] = 13, ['E'] = 14, ['F'] = 15,
};
// clang format on

char escape_to_char[256] = {
    ['0'] = 0,
    ['\''] = '\'',
    ['"'] = '\"',
    ['?'] = '\?',
    ['\\'] = '\\',
    ['a'] = '\a',
    ['b'] = '\b',
    ['f'] = '\f',
    ['n'] = '\n',
    ['r'] = '\r',
    ['t'] = '\t',
    ['v'] = '\v',
};

void scan_char()
{
    assert(*stream == '\'');
    stream++;

    char val = 0;
    if (*stream == '\'') {
        syntax_error("Char literal cannot be empty");
        stream++;
    } else if (*stream == '\n') {
        syntax_error("Char literal cannot contain newline");
        stream++;
    } else if (*stream == '\\') {
        stream++;
        val = escape_to_char[(byte)*stream];
        if (val == 0 && *stream != '0') {
            syntax_error("Invalid char literal escape '\\%c'", *stream);
        }
        stream++;
    } else {
        val = *stream;
        stream++;
    }
    if (*stream != '\'') {
        syntax_error("Expected closing char quote, got '%c'", *stream);
    } else {
        stream++;
    }

    token.kind = TOKEN_INT;
    token.mod = TOKENMOD_CHAR;
    token.int_val = val;
}

void scan_float()
{
    const char *start = stream;
    while (isdigit(*stream)) {
        stream++;
    }
    if (*stream == '.') {
        stream++;
    }
    while (isdigit(*stream)) {
        stream++;
    }
    if (tolower(*stream) == 'e') {
        stream++;
        if (*stream == '-' || *stream == '+') {
            stream++;
        }
        if (!isdigit(*stream)) {
            syntax_error("Expected digit after float literal exponent, found '%c'", *stream);
        }
        while (isdigit(*stream)) {
            stream++;
        }
    }
    f64 val = strtod(start, NULL);
    if (val == HUGE_VAL || val == -HUGE_VAL) {
        syntax_error("Float literal out of range");
    }
    token.kind = TOKEN_FLOAT;
    token.float_val = val;
}

void scan_int()
{
    u64 base = 10;
    // TokenMod mod;
    if (*stream == '0') {
        stream++;
        if (tolower(*stream) == 'x') {
            // Hexadecimal
            base = 16;
            stream++;
        } else if (isdigit(*stream)) {
            // Octal
            // TODO ensure trailing digits are 0, 1, 2, 3, 4, 5, 6, 7
            base = 8;
        } else if (tolower(*stream) == 'b') {
            // Binary
            // TODO ensure trailing digits are 0, 1
            base = 2;
            stream++;
        } else {
            syntax_error("Invalid integer literal prefix '%.*s'", 2, stream - 1);
            stream++;
        }
    }

    u64 val = 0;
    for (;;) {
        u64 digit = char_to_digit[(byte)*stream];
        if (digit == 0 && *stream != '0') {
            if (*stream == '_') {
                stream++;
                continue;
            }
            break;
        }
        if (digit >= base) {
            syntax_error("Digit '%c' out of range for base %llu", *stream, base);
        }
        if (val > (UINT64_MAX - digit) / base) {
            syntax_error("Integer literal overflow");
            while (isdigit(*stream)) {
                stream++;
            }
            val = 0;
            break;
        }
        val = val * base + digit;
        stream++;
    }
    token.kind = TOKEN_INT;
    token.int_val = val;
}

void scan_str()
{
    assert(0);
    token.kind = TOKEN_NAME;
    // token.mod = TOKENMOD_NONE;
}

void next_token()
{
repeat:
    token.start = stream;
    token.mod = 0;
    switch (*stream) {
        // clang-format off
        case ' ': case '\t': case '\r': case '\n': case '\v': case '\f': { // clang-format on
            while (isspace(*stream)) {
                stream++;
            }
            goto repeat;
            break;
        }
        case '\'': {
            scan_char();
            break;
        }
        case '"': {
            scan_str();
            break;
        }
        case '.': {
            scan_float();
            break;
        }
        // clang-format off
        case '0': case '1': case '2': case '3': case '4': case '5': case '6':
        case '7': case '8': case '9': { // clang-format on
            while (isdigit(*stream)) {
                stream++;
            }
            if (*stream == '.' || tolower(*stream) == 'e') {
                stream = token.start;
                scan_float();
            } else {
                stream = token.start;
                scan_int();
            }
            break;
        }
        // clang-format off
        case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g':
        case 'h': case 'i': case 'j': case 'k': case 'l': case 'm': case 'n':
        case 'o': case 'p': case 'q': case 'r': case 's': case 't': case 'u':
        case 'v': case 'w': case 'x': case 'y': case 'z':
        case 'A': case 'B': case 'C': case 'D': case 'E': case 'F': case 'G':
        case 'H': case 'I': case 'J': case 'K': case 'L': case 'M': case 'N':
        case 'O': case 'P': case 'Q': case 'R': case 'S': case 'T': case 'U':
        case 'V': case 'W': case 'X': case 'Y': case 'Z':
        case '_': { // clang-format on
            while (isalnum(*stream) || *stream == '_') {
                stream++;
            }
            token.kind = TOKEN_NAME;
            token.name = str_intern_range(token.start, stream);
            break;
        }
        default: {
            token.kind = *stream++;
            break;
        }
    }
    token.end = stream;
}

void init_stream(const char *str)
{
    stream = str;
    next_token();
}

void print_token(Token token)
{
    TokenKind k = token.kind;
    printf("TOKEN: ");
    switch (k) {
        case TOKEN_FLOAT:
            printf(" %f", token.float_val);
            break;
        case TOKEN_INT:
            printf(" %llu", token.int_val);
            break;
        case TOKEN_NAME:
            break;
        default:
            // printf("'%c'", token.kind);
            break;
    }
    printf("\t\"%.*s\"", (int)(token.end - token.start), token.start);
    printf("\t(%s)", token_kind_name(token.kind));
    printf("\n");
}

// static inline bool is_token_name(const char *name)
// {
//     return token.kind == TOKEN_NAME && token.name == name;
// }

static inline bool is_token(TokenKind kind)
{
    return token.kind == kind;
}

static inline bool match_token(TokenKind kind)
{
    if (is_token(kind)) {
        next_token();
        return true;
    }
    return false;
}

static inline bool expect_token(TokenKind kind)
{
    if (is_token(kind)) {
        next_token();
        return true;
    }
    fatal("expected token %s, got %s", token_kind_name(kind), token_kind_name(token.kind));
    return false;
}

#define assert_token(x) assert(match_token(x))
#define assert_token_eof() assert(is_token(0))
#define assert_token_float(x) assert(token.float_val == (x) && match_token(TOKEN_FLOAT))
#define assert_token_int(x) assert(token.int_val == (x) && match_token(TOKEN_INT))
#define assert_token_name(x) assert(token.name == str_intern(x) && match_token(TOKEN_NAME))

void lex_test(void)
{
    // Ensure UINT64_MAX doesn't trigger overflow
    // init_stream("0x10000000000000000");

    // Integer literal tests
    init_stream("18446744073709551615 0xffff_ffff_ffff_ffff 0b1111 042");
    assert_token_int(18446744073709551615ull);
    assert_token_int(0xffffffffffffffffull);
    assert_token_int(0xf);
    assert_token_int(042);
    assert_token_eof();

    // Float literal tests
    init_stream("3.14 .123 42. 3e10");
    assert_token_float(3.14);
    assert_token_float(.123);
    assert_token_float(42.);
    assert_token_float(3e10);
    assert_token_eof();

    // Char literal tests
    init_stream("'a' '\\n' '\\r'");
    assert_token_int('a');
    assert_token_int('\n');
    assert_token_int('\r');
    assert_token_eof();

    // Misc tests
    init_stream("XY+(XY)_HELLO1,234+994");
    assert_token_name("XY");
    assert_token('+');
    assert_token('(');
    assert_token_name("XY");
    assert_token(')');
    assert_token_name("_HELLO1");
    assert_token(',');
    assert_token_int(234);
    assert_token('+');
    assert_token_int(994);
    assert_token_eof();
}

#undef assert_token
#undef assert_token_eof
#undef assert_token_int
#undef assert_token_name

//
// Grammar
//
// expr3 = INT | '(' expr ')'
// expr2 = '-' expr2 | expr3
// expr1 = expr2 ([*/] expr2)*
// expr0 = expr1 ([+-] expr1)*
// expr  = expr0

static byte *code;

enum {
    ADD,
    SUB,
    MUL,
    DIV,
    NEG,
    LIT,
    HALT,
};

static const struct {
    const char name[4];
    int size;
} instr_info[] = {
    [ADD] = { "ADD ", 1 },  [SUB] = { "SUB ", 1 }, [MUL] = { "MUL ", 1 },
    [DIV] = { "DIV ", 1 },  [NEG] = { "NEG ", 1 }, [LIT] = { "LIT ", 5 },
    [HALT] = { "HALT", 1 },
};

u64 parse_expr();

u64 parse_expr3()
{
    u64 val;
    if (is_token(TOKEN_INT)) {
        val = token.int_val;
        next_token();
        buf_push(code, LIT);
        buf_push(code, val << 0);
        buf_push(code, val << 8);
        buf_push(code, val << 16);
        buf_push(code, val << 24);
        return val;
    } else if (match_token('(')) {
        val = parse_expr();
        expect_token(')');
        return val;
    }
    fatal("expected integer of (, got \"%s\"", token_kind_name(token.kind));
    return 0;
}

u64 parse_expr2()
{
    u64 val;
    if (match_token('-')) {
        val = parse_expr2();
        buf_push(code, NEG);
        return -val;
    } else if (match_token('+')) {
        val = parse_expr2();
        buf_push(code, ADD);
        return val;
    }
    return parse_expr3();
}

u64 parse_expr1()
{
    int val = parse_expr2();
    while (is_token('*') || is_token('/')) {
        char op = token.kind;
        next_token();
        int rhs = parse_expr2();
        if (op == '*') {
            buf_push(code, MUL);
            val *= rhs;
        } else {
            assert(op == '/');
            assert(rhs != 0);
            buf_push(code, DIV);
            val /= rhs;
        }
    }
    return val;
}

u64 parse_expr0()
{
    int val = parse_expr1();
    while (is_token('+') || is_token('-')) {
        char op = token.kind;
        next_token();
        int rhs = parse_expr1();
        if (op == '+') {
            buf_push(code, ADD);
            val += rhs;
        } else {
            buf_push(code, SUB);
            assert(op == '-');
            val -= rhs;
        }
    }
    return val;
}

u64 parse_expr()
{
    return parse_expr0();
}

int parse_expr_str(const char *str)
{
    init_stream(str);
    return parse_expr();
}

#define assert_expr(x) assert(parse_expr_str(#x) == (x))

void parse_test(void)
{
    // clang-format off
    assert_expr(1);
    assert_expr(-1);
    assert_expr(1);
    assert_expr(-1);
    assert_expr(1-(-1));
    assert_expr((1));
    assert_expr(1-2-3);
    assert_expr(2*3+4*5);
    assert_expr(2+-3);
    assert_expr(2*(3+4)*5);
    // clang-format on
}

#undef assert_expr

#define PUSH(x) (*top++ = (x))
#define POP() (*--top)
#define assert_pops(n) assert(top - stack >= (n))
#define assert_pushes(n) assert(top + (n) <= stack + MAX_STACK)

int32_t vm_exec(const uint8_t *code)
{
    enum { MAX_STACK = 1024 };
    int32_t stack[MAX_STACK];
    int32_t *top = stack;
    for (;;) {
        uint8_t op = *code++;
        switch (op) {
            case ADD: {
                assert_pops(2);
                int32_t right = POP();
                int32_t left = POP();
                assert_pushes(1);
                PUSH(left + right);
                break;
            }
            case SUB: {
                assert_pops(2);
                int32_t right = POP();
                int32_t left = POP();
                assert_pushes(1);
                PUSH(left - right);
                break;
            }
            case MUL: {
                assert_pops(2);
                int32_t right = POP();
                int32_t left = POP();
                assert_pushes(1);
                PUSH(left * right);
                break;
            }
            case DIV: {
                assert_pops(2);
                int32_t right = POP();
                int32_t left = POP();
                assert_pushes(1);
                assert(right != 0);
                PUSH(left / right);
                break;
            }
            case NEG: {
                assert_pops(1);
                int32_t right = POP();
                assert_pushes(1);
                PUSH(-right);
                break;
            }
            case LIT:
                assert_pushes(1);
                PUSH((code[0] << 0) | (code[1] << 8) | (code[2] << 16) | (code[3] << 24));
                code += sizeof(uint32_t);
                break;
            case HALT:
                assert_pops(1);
                return POP();
            default:
                fatal("vm_exec: illegal opcode");
                return 0;
        }
    }
}

void vm_test()
{
    assert(vm_exec((byte[]){ LIT, 1, 0, 0, 0, HALT }) == 1);
    assert(vm_exec((byte[]){ LIT, 2, 0, 0, 0, LIT, 3, 0, 0, 0, ADD, HALT }) == 5);
    assert(
        vm_exec((byte[]){ LIT, 1, 0, 0, 0, LIT, 2, 0, 0, 0, LIT, 3, 0, 0, 0, ADD, ADD,
                          HALT }) == 6);
    assert(vm_exec((byte[]){ LIT, 2, 0, 0, 0, LIT, 3, 0, 0, 0, ADD, HALT }) == 5);
    assert(vm_exec((byte[]){ LIT, 1, 0, 0, 0, NEG, HALT }) == -1);
    assert(vm_exec((byte[]){ LIT, 2, 0, 0, 0, LIT, 3, 0, 0, 0, MUL, HALT }) == 6);
    assert(vm_exec((byte[]){ LIT, 4, 0, 0, 0, LIT, 2, 0, 0, 0, DIV, HALT }) == 2);
}

void print_lit_instr(int offset)
{
    byte *pc = &code[offset + 1];
    int val = (pc[0] << 0) | (pc[1] << 8) | (pc[2] << 16) | (pc[3] << 24);
    printf("%-16s %4d\n", "LIT", val);
}

void print_simple_instr(int offset)
{
    byte instr = code[offset];
    printf("%-16s\n", instr_info[instr].name);
}

#define HEX "%02hhX"

int print_instr(int offset)
{
    byte instr = code[offset];
    int size = instr_info[instr].size; // TODO handle invalid instr (size=1)
    // printf("instr:%d size:%d\n", instr, size);

    // Instruction bytes
    printf("%06d ", offset);
    for (int i = 0; i < size; ++i) {
        printf(HEX " ", (byte)code[offset + i]);
    }
    for (int i = size; i < 5; ++i) {
        printf("   ");
    }

    switch (instr) {
        case LIT:
            print_lit_instr(offset);
            break;
        default:
            print_simple_instr(offset);
            break;
    }
    return size;
}

void print_disassembly()
{
    printf("OFFSET B0 B1 B2 B3 B4 OPCODE\n");
    printf("------ -- -- -- -- -- ----------------\n");
    for (int offset = 0, max = buf_len(code); offset < max;) {
        offset += print_instr(offset);
    }
    puts("");
}

#define assert_compile_expr(x) \
    (buf_free(code), parse_expr_str(#x), buf_push(code, HALT), assert(vm_exec(code) == (x)))

void compile_test()
{
    // clang-format off
    assert_compile_expr(1);
    assert_compile_expr(-1);
    assert_compile_expr(1+2);
    assert_compile_expr(2*3);
    assert_compile_expr((2*3)+(4*5));
    assert_compile_expr(10/2);
    // clang-format off
}

#undef assert_compile_expr

void run_tests()
{
    buf_test();
    str_intern_test();
    lex_test();
    parse_test();
    vm_test();
    compile_test();
}

int main(int argc, char *argv[])
{
    run_tests();
}
