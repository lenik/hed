#ifndef EDIT_FILE_H
#define EDIT_FILE_H

#include <stddef.h>
#include <sys/types.h>

/* Load entire file into malloc'd buffer. Sets *lenp and optionally *modep.
 * Returns NULL on error (errno set). Empty file => non-NULL buffer with len 0.
 */
char *read_file_bytes(const char *path, size_t *lenp, mode_t *modep);

/*
 * Write data to path atomically (temp beside path + rename).
 * Preserves mode. Returns 0 on success, -1 on error.
 */
int write_file_atomic(const char *path, const void *data, size_t len, mode_t mode);

/*
 * If neu differs from old, write neu atomically.
 * Returns 1 if written, 0 if unchanged (not written), -1 on error.
 */
int replace_file_if_changed(const char *path, const void *old, size_t old_len, const void *neu,
                            size_t neu_len, mode_t mode);

/* Append src to a growable buffer. Returns 0 or -1 (ENOMEM). */
int buf_append(char **buf, size_t *len, size_t *cap, const void *src, size_t n);

/* Append n copies of ch. */
int buf_append_ch(char **buf, size_t *len, size_t *cap, char ch, size_t n);

#endif /* EDIT_FILE_H */
