/*
 * avoid accidental concatenation of strings
 * but make sure deliberate concatenation works
 */
#define a xa
#define b xb
#define A(x, y) x y
#define B(x, y) x ## y
#define C(x) x

A(r, s)
B(r, s)
C(r)s
A(a, b)
//B(a, b) // not working yet; gcc and clang give `ab`, we give `xaxb`
C(a)b
