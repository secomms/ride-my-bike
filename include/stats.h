#include <stdint.h>
#include <math.h>
#include <x86intrin.h>

typedef struct {
    uint64_t n;
    double mean;
    double M2;
} stats_t;

static inline void stats_update(stats_t *s, double x)
{
    s->n++;
    double delta = x - s->mean;
    s->mean += delta / s->n;
    double delta2 = x - s->mean;
    s->M2 += delta * delta2;
}

static inline double stats_mean(const stats_t *s)
{
    return s->mean;
}

static inline double stats_variance(const stats_t *s)
{
    return (s->n > 1) ? s->M2 / (s->n - 1) : 0.0;
}

static inline double stats_stddev(const stats_t *s)
{
    return sqrt(stats_variance(s));
}
