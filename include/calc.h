#ifndef CALC_H
#define CALC_H

#include <stdbool.h>
#include <wchar.h>

/**
 * Evaluates a mathematical expression string.
 * Supports:
 *   - Basic operators: +, -, *, /, % (modulo), ^ or ** (power)
 *   - Functions: sqrt, abs, sin, cos, tan, asin, acos, atan, ln, log, log10, log2, exp, round, floor, ceil, pow
 *   - Constants: pi, e
 *   - Percentages: "20% of 150", "100 + 20%", "50 * 10%"
 *   - Multiplication aliases: "x", "X", "*"
 *   - Optional '=' prefix (e.g. "= 2 + 2")
 * 
 * Returns true if expression was successfully evaluated, false otherwise.
 */
bool calc_evaluate(const wchar_t *expr, double *out_value, wchar_t *out_result, size_t out_result_size);

/**
 * Copies the specified text to the Windows clipboard.
 */
bool calc_copy_to_clipboard(const wchar_t *text);

#endif
