/*
 * Copyright (C) 2026 Lenik <hed@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "digit_opt.h"

void digit_opt_init(digit_opt_t *d) {
    d->last_opt = 0;
    d->num = 0;
}

void digit_opt_note(digit_opt_t *d, int opt) {
    d->last_opt = opt;
}

int digit_opt_is_digit(int opt) {
    return opt >= '0' && opt <= '9';
}

int digit_opt_digit(digit_opt_t *d, int opt) {
    int digit = opt - '0';

    if (digit_opt_is_digit(d->last_opt)) {
        d->num = d->num * 10 + digit;
    } else {
        d->num = digit;
    }
    d->last_opt = opt;
    return digit;
}
