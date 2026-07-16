#ifndef LINES_H
#define LINES_H

#include <stddef.h>

/* Byte offset of the start of 1-based line `lineno` in data[0..len).
 * If lineno <= 1, returns 0.
 * If lineno is past the last line, returns len (append).
 * Lines are separated by `sep` (usually '\n' or '\0').
 */
size_t line_byte_offset(const char *data, size_t len, long lineno, char sep);

/* Count lines in data (empty file => 0). A trailing separator does not add an extra line. */
size_t count_lines(const char *data, size_t len, char sep);

/*
 * Find end of the printable/text prefix for --binary-files=binary.
 * Returns the offset of the first non-printable byte (not tab/LF/CR/FF), or len.
 */
size_t text_prefix_len(const char *data, size_t len);

#endif /* LINES_H */
