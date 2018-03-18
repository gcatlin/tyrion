#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    TOKEN_NAME,
    // ...
} TokenKind;

const char *token_kind_names[] = {
    // clang-format off
    [TOKEN_INT]  = "TOKEN_INT",
    [TOKEN_NAME] = "TOKEN_NAME",
    // clang-format on
};

typedef struct {
    TokenKind kind;
    const char *start;
    const char *end;
    union {
        int num;
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

void next_token()
{
    token.start = stream;
    switch (*stream) {
        // clang-format off
        case '0': case '1': case '2': case '3': case '4': case '5': case '6':
        case '7': case '8': case '9': { // clang-format on
            int val = 0;
            while (isdigit(*stream)) {
                val *= 10;
                val += *stream++ - '0';
            }
            token.kind = TOKEN_INT;
            token.num = val;
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
        default:
            token.kind = *stream++;
            break;
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
        case TOKEN_INT:
            printf(" %d", token.num);
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
    fatal("expected token %s, got %s\n", token_kind_name(kind), token_kind_name(token.kind));
    return false;
}

#define assert_token(x) assert(match_token(x))
#define assert_token_eof() assert(is_token(0))
#define assert_token_int(x) assert(token.num == (x) && match_token(TOKEN_INT))
#define assert_token_name(x) assert(token.name == str_intern(x) && match_token(TOKEN_NAME))

void lex_test(void)
{
    const char *str = "XY+(XY)_HELLO1,234+994";
    init_stream(str);
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

int parse_expr();

int parse_expr3()
{
    if (is_token(TOKEN_INT)) {
        int val = token.num;
        next_token();
        return val;
    } else if (match_token('(')) {
        int val = parse_expr();
        expect_token(')');
        return val;
    }
    fatal("expected integer of (, got \"%s\"", token_kind_name(token.kind));
    return 0;
}

int parse_expr2()
{
    if (match_token('-')) {
        return -parse_expr2();
    } else if (match_token('+')) {
        return parse_expr2();
    }
    return parse_expr3();
}

int parse_expr1()
{
    int val = parse_expr2();
    while (is_token('*') || is_token('/')) {
        char op = token.kind;
        next_token();
        int rhs = parse_expr2();
        if (op == '*') {
            val *= rhs;
        } else {
            assert(op == '/');
            assert(rhs != 0);
            val /= rhs;
        }
    }
    return val;
}

int parse_expr0()
{
    int val = parse_expr1();
    while (is_token('+') || is_token('-')) {
        char op = token.kind;
        next_token();
        int rhs = parse_expr1();
        if (op == '+') {
            val += rhs;
        } else {
            assert(op == '-');
            val -= rhs;
        }
    }
    return val;
}

int parse_expr()
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
}

#undef assert_expr

void run_tests()
{
    buf_test();
    lex_test();
    str_intern_test();
    parse_test();
}

int main(int argc, char *argv[])
{
    run_tests();
}
