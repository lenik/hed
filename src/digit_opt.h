#ifndef DIGIT_OPT_H
#define DIGIT_OPT_H

/*
 * Accumulate -0..-9 short options into a decimal number.
 * If the previous option was a digit: num = num * 10 + d.
 * Otherwise: num = d.
 */
typedef struct digit_opt {
    int last_opt; /* last accepted option character, 0 if none */
    long num;
} digit_opt_t;

void digit_opt_init(digit_opt_t *d);

/* Record a non-digit option character. */
void digit_opt_note(digit_opt_t *d, int opt);

/*
 * Handle digit option '0'..'9'. Updates num and last_opt.
 * Returns the digit value 0..9.
 */
int digit_opt_digit(digit_opt_t *d, int opt);

int digit_opt_is_digit(int opt);

#endif /* DIGIT_OPT_H */
