#ifndef UTIL_H
#define UTIL_H

#define LEN(x) (sizeof (x) / sizeof *(x))

__attribute__ ((noreturn))
void eprintf(const char *fmt, ...);

void *emalloc(size_t size);

void *ecalloc(size_t nmemb, size_t size);

double estrtod(const char *str);

size_t strlcpy(char *dest, const char *src, size_t size);

#endif
