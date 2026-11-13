#ifndef SOLIS_DOOM_ASSERT_H
#define SOLIS_DOOM_ASSERT_H

void solis_assert_fail(const char *expression, const char *file, int line)
    __attribute__((noreturn));

#ifndef NDEBUG
#define assert(expression) \
    ((expression) ? (void)0 : solis_assert_fail(#expression, __FILE__, __LINE__))
#else
#define assert(expression) ((void)0)
#endif

#endif
