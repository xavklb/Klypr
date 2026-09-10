#include "calc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <wctype.h>
#include <windows.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_E
#define M_E 2.71828182845904523536
#endif

typedef enum {
    TOK_EOF = 0,
    TOK_NUM,
    TOK_PLUS,
    TOK_MINUS,
    TOK_MUL,
    TOK_DIV,
    TOK_MOD,
    TOK_POW,
    TOK_PERCENT,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_COMMA,
    TOK_IDENT
} CalcTokenType;

typedef struct {
    CalcTokenType type;
    double num_val;
    bool is_percent;
    wchar_t ident[64];
} Token;

typedef struct {
    const wchar_t *src;
    size_t pos;
    Token cur;
    bool has_operator;
    bool has_digit;
    bool error;
    bool starts_with_equal;
    bool last_was_percent;
} Parser;

static void next_token(Parser *p);

static void init_parser(Parser *p, const wchar_t *src, bool starts_with_equal)
{
    p->src = src;
    p->pos = 0;
    p->has_operator = false;
    p->has_digit = false;
    p->error = false;
    p->starts_with_equal = starts_with_equal;
    p->last_was_percent = false;
    memset(&p->cur, 0, sizeof(Token));
    next_token(p);
}

static void skip_whitespace(Parser *p)
{
    while (p->src[p->pos] == L' ' || p->src[p->pos] == L'\t') {
        p->pos++;
    }
}

static void next_token(Parser *p)
{
    skip_whitespace(p);
    wchar_t c = p->src[p->pos];

    if (c == L'\0') {
        p->cur.type = TOK_EOF;
        return;
    }

    // Number (e.g. 123, 12.34, 12,34)
    if (iswdigit(c) || ((c == L'.' || c == L',') && iswdigit(p->src[p->pos + 1]))) {
        p->has_digit = true;
        size_t start = p->pos;
        bool has_dec = false;

        while (p->src[p->pos] != L'\0') {
            wchar_t ch = p->src[p->pos];
            if (iswdigit(ch)) {
                p->pos++;
            } else if ((ch == L'.' || ch == L',') && !has_dec && iswdigit(p->src[p->pos + 1])) {
                has_dec = true;
                p->pos++;
            } else {
                break;
            }
        }

        wchar_t buf[64];
        size_t len = p->pos - start;
        if (len >= 64) len = 63;
        wcsncpy(buf, p->src + start, len);
        buf[len] = L'\0';

        // Normalize decimal comma to dot
        for (size_t i = 0; i < len; i++) {
            if (buf[i] == L',') buf[i] = L'.';
        }

        wchar_t *endptr = NULL;
        p->cur.num_val = wcstod(buf, &endptr);
        p->cur.type = TOK_NUM;
        p->cur.is_percent = false;
        return;
    }

    // Operators
    if (c == L'+') {
        p->pos++;
        p->has_operator = true;
        p->cur.type = TOK_PLUS;
        return;
    }
    if (c == L'-') {
        p->pos++;
        p->has_operator = true;
        p->cur.type = TOK_MINUS;
        return;
    }
    if (c == L'*') {
        p->pos++;
        p->has_operator = true;
        if (p->src[p->pos] == L'*') {
            p->pos++;
            p->cur.type = TOK_POW;
        } else {
            p->cur.type = TOK_MUL;
        }
        return;
    }
    if (c == L'/') {
        p->pos++;
        p->has_operator = true;
        p->cur.type = TOK_DIV;
        return;
    }
    if (c == L'^') {
        p->pos++;
        p->has_operator = true;
        p->cur.type = TOK_POW;
        return;
    }
    if (c == L'%') {
        p->pos++;
        p->has_operator = true;

        // Peek ahead to distinguish percentage from modulo
        size_t peek = p->pos;
        while (p->src[peek] == L' ' || p->src[peek] == L'\t') {
            peek++;
        }
        wchar_t next_ch = p->src[peek];

        bool is_of = false;
        if ((next_ch == L'o' || next_ch == L'O') &&
            (p->src[peek + 1] == L'f' || p->src[peek + 1] == L'F') &&
            !iswalnum(p->src[peek + 2])) {
            is_of = true;
        }

        if (next_ch == L'\0' || next_ch == L'+' || next_ch == L'-' ||
            next_ch == L'*' || next_ch == L'/' || next_ch == L'^' ||
            next_ch == L')' || next_ch == L',' || is_of) {
            p->cur.type = TOK_PERCENT;
        } else {
            p->cur.type = TOK_MOD;
        }
        return;
    }
    if (c == L'(') {
        p->pos++;
        p->cur.type = TOK_LPAREN;
        return;
    }
    if (c == L')') {
        p->pos++;
        p->cur.type = TOK_RPAREN;
        return;
    }
    if (c == L',') {
        p->pos++;
        p->cur.type = TOK_COMMA;
        return;
    }

    // Check multiplication alias 'x' or 'X' (e.g. 12 x 45 or 12x45)
    if ((c == L'x' || c == L'X') &&
        (p->src[p->pos + 1] == L' ' || p->src[p->pos + 1] == L'\t' ||
         iswdigit(p->src[p->pos + 1]) || p->src[p->pos + 1] == L'(')) {
        p->pos++;
        p->has_operator = true;
        p->cur.type = TOK_MUL;
        return;
    }

    // Identifiers (functions, constants, keywords like "of", "mod")
    if (iswalpha(c)) {
        size_t start = p->pos;
        while (iswalnum(p->src[p->pos])) {
            p->pos++;
        }
        size_t len = p->pos - start;
        if (len >= 64) len = 63;
        wcsncpy(p->cur.ident, p->src + start, len);
        p->cur.ident[len] = L'\0';

        if (_wcsicmp(p->cur.ident, L"mod") == 0) {
            p->has_operator = true;
            p->cur.type = TOK_MOD;
            return;
        }

        if (_wcsicmp(p->cur.ident, L"pi") == 0 || _wcsicmp(p->cur.ident, L"e") == 0) {
            p->has_digit = true;
        } else if (_wcsicmp(p->cur.ident, L"of") == 0) {
            p->has_operator = true;
        } else {
            p->has_operator = true;
        }

        p->cur.type = TOK_IDENT;
        return;
    }

    // Unknown character
    p->error = true;
    p->cur.type = TOK_EOF;
}

// Recursive Descent Grammar
static double parse_expr(Parser *p);
static double parse_term(Parser *p);
static double parse_power(Parser *p);
static double parse_unary(Parser *p);
static double parse_primary(Parser *p);

static double parse_expr(Parser *p)
{
    double val = parse_term(p);
    if (p->error) return 0;

    while (p->cur.type == TOK_PLUS || p->cur.type == TOK_MINUS) {
        CalcTokenType op = p->cur.type;
        next_token(p);
        p->last_was_percent = false;
        double rhs = parse_term(p);
        if (p->error) return 0;

        // Percentage addition/subtraction: 100 + 20% = 120, 100 - 20% = 80
        if (p->last_was_percent) {
            rhs = val * rhs;
        }

        if (op == TOK_PLUS) {
            val += rhs;
        } else {
            val -= rhs;
        }
    }
    return val;
}

static double parse_term(Parser *p)
{
    double val = parse_power(p);
    if (p->error) return 0;

    while (p->cur.type == TOK_MUL || p->cur.type == TOK_DIV || p->cur.type == TOK_MOD) {
        CalcTokenType op = p->cur.type;
        next_token(p);
        double rhs = parse_power(p);
        if (p->error) return 0;

        if (op == TOK_MUL) {
            val *= rhs;
        } else if (op == TOK_DIV) {
            if (fabs(rhs) < 1e-15) {
                p->error = true;
                return 0;
            }
            val /= rhs;
        } else if (op == TOK_MOD) {
            if (fabs(rhs) < 1e-15) {
                p->error = true;
                return 0;
            }
            val = fmod(val, rhs);
        }
    }
    return val;
}

static double parse_power(Parser *p)
{
    double val = parse_unary(p);
    if (p->error) return 0;

    if (p->cur.type == TOK_POW) {
        next_token(p);
        double rhs = parse_power(p); // right-associative
        if (p->error) return 0;
        val = pow(val, rhs);
    }
    return val;
}

static double parse_unary(Parser *p)
{
    if (p->cur.type == TOK_PLUS) {
        next_token(p);
        return parse_unary(p);
    }
    if (p->cur.type == TOK_MINUS) {
        next_token(p);
        return -parse_unary(p);
    }
    return parse_primary(p);
}

static double call_func(const wchar_t *fn, double arg1, double arg2, bool has_arg2, Parser *p)
{
    if (_wcsicmp(fn, L"sqrt") == 0) {
        if (arg1 < 0) { p->error = true; return 0; }
        return sqrt(arg1);
    }
    if (_wcsicmp(fn, L"abs") == 0 || _wcsicmp(fn, L"fabs") == 0) {
        return fabs(arg1);
    }
    if (_wcsicmp(fn, L"sin") == 0) return sin(arg1);
    if (_wcsicmp(fn, L"cos") == 0) return cos(arg1);
    if (_wcsicmp(fn, L"tan") == 0) return tan(arg1);
    if (_wcsicmp(fn, L"asin") == 0) {
        if (arg1 < -1.0 || arg1 > 1.0) { p->error = true; return 0; }
        return asin(arg1);
    }
    if (_wcsicmp(fn, L"acos") == 0) {
        if (arg1 < -1.0 || arg1 > 1.0) { p->error = true; return 0; }
        return acos(arg1);
    }
    if (_wcsicmp(fn, L"atan") == 0) return atan(arg1);
    if (_wcsicmp(fn, L"ln") == 0) {
        if (arg1 <= 0) { p->error = true; return 0; }
        return log(arg1);
    }
    if (_wcsicmp(fn, L"log") == 0 || _wcsicmp(fn, L"log10") == 0) {
        if (arg1 <= 0) { p->error = true; return 0; }
        return log10(arg1);
    }
    if (_wcsicmp(fn, L"log2") == 0) {
        if (arg1 <= 0) { p->error = true; return 0; }
        return log2(arg1);
    }
    if (_wcsicmp(fn, L"exp") == 0) return exp(arg1);
    if (_wcsicmp(fn, L"round") == 0) return round(arg1);
    if (_wcsicmp(fn, L"floor") == 0) return floor(arg1);
    if (_wcsicmp(fn, L"ceil") == 0) return ceil(arg1);
    if (_wcsicmp(fn, L"pow") == 0) {
        if (!has_arg2) { p->error = true; return 0; }
        return pow(arg1, arg2);
    }

    p->error = true;
    return 0;
}

static double parse_primary(Parser *p)
{
    p->last_was_percent = false;

    if (p->cur.type == TOK_NUM) {
        double val = p->cur.num_val;
        next_token(p);

        if (p->cur.type == TOK_PERCENT) {
            val /= 100.0;
            p->last_was_percent = true;
            next_token(p);

            // Check for "of" following percentage (e.g. 20% of 150)
            if (p->cur.type == TOK_IDENT && _wcsicmp(p->cur.ident, L"of") == 0) {
                next_token(p);
                val *= parse_primary(p);
                p->last_was_percent = false;
            }
        }
        return val;
    }

    if (p->cur.type == TOK_LPAREN) {
        next_token(p);
        double val = parse_expr(p);
        if (p->cur.type != TOK_RPAREN) {
            p->error = true;
            return 0;
        }
        next_token(p);

        // Optional % after parentheses e.g. (10 + 10)%
        if (p->cur.type == TOK_PERCENT) {
            val /= 100.0;
            p->last_was_percent = true;
            next_token(p);
        }
        return val;
    }

    if (p->cur.type == TOK_IDENT) {
        wchar_t id[64];
        wcsncpy(id, p->cur.ident, 63);
        id[63] = L'\0';

        // Constants
        if (_wcsicmp(id, L"pi") == 0) {
            next_token(p);
            return M_PI;
        }
        if (_wcsicmp(id, L"e") == 0) {
            next_token(p);
            return M_E;
        }

        // Functions: id(arg1, arg2)
        next_token(p);
        if (p->cur.type != TOK_LPAREN) {
            p->error = true;
            return 0;
        }
        next_token(p);

        double arg1 = parse_expr(p);
        double arg2 = 0;
        bool has_arg2 = false;

        if (p->cur.type == TOK_COMMA) {
            next_token(p);
            arg2 = parse_expr(p);
            has_arg2 = true;
        }

        if (p->cur.type != TOK_RPAREN) {
            p->error = true;
            return 0;
        }
        next_token(p);

        return call_func(id, arg1, arg2, has_arg2, p);
    }

    p->error = true;
    return 0;
}

static void format_result(double val, wchar_t *out, size_t size)
{
    if (isnan(val) || isinf(val)) {
        wcsncpy(out, L"Erreur", size - 1);
        out[size - 1] = L'\0';
        return;
    }

    // Check if effectively an integer
    double r = round(val);
    if (fabs(val - r) < 1e-10 && fabs(val) < 1e14) {
        _snwprintf(out, size, L"%.0f", r);
        return;
    }

    // Very large or very small -> scientific notation
    if (fabs(val) >= 1e12 || (fabs(val) <= 1e-5 && fabs(val) > 0.0)) {
        _snwprintf(out, size, L"%.8g", val);
        return;
    }

    // Standard floating point, strip trailing zeroes
    _snwprintf(out, size, L"%.8f", val);
    wchar_t *dot = wcschr(out, L'.');
    if (dot != NULL) {
        wchar_t *end = out + wcslen(out) - 1;
        while (end > dot && *end == L'0') {
            *end = L'\0';
            end--;
        }
        if (end == dot) {
            *dot = L'\0';
        }
    }
}

bool calc_evaluate(const wchar_t *expr, double *out_value, wchar_t *out_result, size_t out_result_size)
{
    if (expr == NULL || out_result == NULL || out_result_size < 4) {
        return false;
    }

    while (*expr == L' ' || *expr == L'\t') {
        expr++;
    }

    if (*expr == L'\0') {
        return false;
    }

    bool starts_with_equal = false;
    if (*expr == L'=') {
        starts_with_equal = true;
        expr++;
        while (*expr == L' ' || *expr == L'\t') {
            expr++;
        }
        if (*expr == L'\0') {
            return false;
        }
    }

    // Reject commands starting with ':'
    if (*expr == L':') {
        return false;
    }

    Parser p;
    init_parser(&p, expr, starts_with_equal);

    double val = parse_expr(&p);

    if (p.error || p.cur.type != TOK_EOF) {
        return false;
    }

    // Unless started with '=', requires at least one operator/function and at least one number/constant
    if (!starts_with_equal) {
        if (!p.has_operator || !p.has_digit) {
            return false;
        }
    }

    if (out_value != NULL) {
        *out_value = val;
    }

    format_result(val, out_result, out_result_size);
    return true;
}

bool calc_copy_to_clipboard(const wchar_t *text)
{
    if (text == NULL || text[0] == L'\0') {
        return false;
    }
    if (!OpenClipboard(NULL)) {
        return false;
    }
    EmptyClipboard();

    size_t len = wcslen(text);
    size_t bytes = (len + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem == NULL) {
        CloseClipboard();
        return false;
    }

    wchar_t *pMem = (wchar_t *)GlobalLock(hMem);
    if (pMem != NULL) {
        wcscpy(pMem, text);
        GlobalUnlock(hMem);
        SetClipboardData(CF_UNICODETEXT, hMem);
    } else {
        GlobalFree(hMem);
    }

    CloseClipboard();
    return true;
}
