#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

typedef struct {
    size_t len;
    size_t cap;
    char buf[];
} buf_hdr_t;

// clang-format off
#define buf__raw(b) ((size_t *)(b)-2)
#define buf__len(b) buf__raw(b)[0]
#define buf__cap(b) buf__raw(b)[1]

#define buf__fit(b, n)  (buf__fits(b, n) ? 0 : buf__grow(b, n))
#define buf__fits(b, n) ((b) && buf__len(b)+(n) <= buf__cap(b))
#define buf__grow(b, n) (*((void **)&(b)) = buf___grow((b), buf_len(b)+(n), sizeof(*(b))))

#define buf_cap(b)        ((b) ? buf__cap(b) : 0)
#define buf_len(b)        ((b) ? buf__len(b) : 0)
#define buf_hdr(b)        ((buf_hdr_t *)buf__raw(b))
#define buf_free(b)       ((b) ? free(buf__raw(b)), (b) = NULL : 0)
#define buf_push(b, x)    (buf__fit(b, 1), (b)[buf__len(b)++] = (x), (b))
#define buf_reserve(b, n) (buf__fit(b, (n)), (b)[buf__len(b)])

// for use in debugger
size_t bufcap(const void *b) { return buf_cap(b); }
size_t buflen(const void *b) { return buf_len(b); }
buf_hdr_t *_bufhdr(const void *b) { return buf_hdr(b); }

void *buf___grow(const void *b, size_t len, size_t elem_size)
{
    size_t cap = MAX(2 * buf_cap(b), len);
    assert(0 < cap && len <= cap);
    size_t size = offsetof(buf_hdr_t, buf) + elem_size * cap;
    buf_hdr_t *hdr = xrealloc(b ? buf__raw(b) : NULL, size);
    hdr->cap = cap;
    if (!b) hdr->len = 0;
    return hdr->buf;
}
// clang-format on

void buf_test(void)
{
    // setup
    int *b = NULL;
    int N = 1024;

    // push increases len and cap
    assert(buf_len(b) == 0);
    for (int i = 0; i < N; i++) {
        buf_push(b, i);
    }
    assert(buf_len(b) == N);
    assert(buf_cap(b) >= N);

    // push sets values
    for (int i = 0, max = buf_len(b); i < max; i++) {
        assert(b[i] == i);
    }

    buf_free(b);
    assert(buf_free(b) == NULL);
    assert(!buf_len(b));
}

typedef enum {
    TOKEN_INT = 128,
    TOKEN_NAME,
    // ...
} TokenKind;

typedef struct {
    size_t len;
    const char *str;
} intern_str_t;

static intern_str_t *interns;

const char *str_intern_range(const char *restrict start, const char *restrict end)
{
    size_t len = end - start;
    for (size_t i = 0, n = buf_len(interns); i < n; i++) {
        if (interns[i].len == len && strncmp(interns[i].str, start, len) == 0) {
            return interns[i].str;
        }
    }

    char *str = xmalloc(len + 1);
    memcpy(str, start, len);
    str[len] = 0;
    buf_push(interns, ((intern_str_t){ len, str }));
    return str;
}

const char *str_intern(const char *str)
{
    return str_intern_range(str, str + strlen(str));
}

void str_intern_test()
{
    char x[] = "hello";
    char y[] = "hello";
    char z[] = "hello!";
    assert(x != y && x != z);
    const char *xp = str_intern(x);
    const char *yp = str_intern(y);
    const char *zp = str_intern(z);
    assert(xp == yp && xp != zp);
}

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
        u64 u64;
        const char *name;
    };
    // ...
} Token;

const char *token_kind_name(TokenKind kind)
{
    if (kind >= TOKEN_INT) {
        return token_kind_names[kind];
    }
    return "ASCII";
}

Token token;
const char *stream;

void next_token()
{
    token.start = stream;
    switch (*stream) {
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9': {
            uint64_t val = 0;
            while (isdigit(*stream)) {
                val *= 10;
                val += *stream++ - '0';
            }
            token.kind = TOKEN_INT;
            token.u64 = val;
            break;
        }
        case 'a':
        case 'b':
        case 'c':
        case 'd':
        case 'e':
        case 'f':
        case 'g':
        case 'h':
        case 'i':
        case 'j':
        case 'k':
        case 'l':
        case 'm':
        case 'n':
        case 'o':
        case 'p':
        case 'q':
        case 'r':
        case 's':
        case 't':
        case 'u':
        case 'v':
        case 'w':
        case 'x':
        case 'y':
        case 'z':
        case 'A':
        case 'B':
        case 'C':
        case 'D':
        case 'E':
        case 'F':
        case 'G':
        case 'H':
        case 'I':
        case 'J':
        case 'K':
        case 'L':
        case 'M':
        case 'N':
        case 'O':
        case 'P':
        case 'Q':
        case 'R':
        case 'S':
        case 'T':
        case 'U':
        case 'V':
        case 'W':
        case 'X':
        case 'Y':
        case 'Z':
        case '_': {
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

void print_token(Token t)
{
    TokenKind k = token.kind;
    printf("TOKEN: ");
    switch (k) {
        case TOKEN_INT:
            printf(" %llu", token.u64);
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

void lex_text(void)
{
    const char *source = "XY+(XY)_HELLO1,234+foo!994";
    printf("SOURCE: \"%s\"\n", source);
    stream = source;
    next_token();
    while (token.kind) {
        print_token(token);
        next_token();
    }
}

int main()
{
    buf_test();
    str_intern_test();
    lex_text();
}
