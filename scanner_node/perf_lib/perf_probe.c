#include "perf_probe.h"
#include "perf_probe_hal.h"

#include <string.h>
#include <inttypes.h>

#if CONFIG_PERF_ENABLE

typedef struct
{
#if CONFIG_PERF_STRINGS
    char name[CONFIG_PERF_NAME_LEN];
#endif

    uint32_t id;
    perf_stats_t stats;
    uint8_t used;
} perf_entry_t;

static perf_entry_t g_perf_entries[CONFIG_PERF_PROBES_MAX];

#ifndef CONFIG_PERF_SCOPE_STACK_MAX
#define CONFIG_PERF_SCOPE_STACK_MAX 8
#endif

static perf_scope_t *g_scope_stack[CONFIG_PERF_SCOPE_STACK_MAX];
static uint32_t g_scope_stack_top;

static uint64_t g_last_report_us;

static uint8_t g_perf_enabled = 0u;
static uint8_t g_perf_logging_enabled = 0u;

static void perf_stats_clear(perf_stats_t *s)
{
    if (!s)
        return;

    s->count = 0u;
    s->fail_count = 0u;

    s->min_us = UINT64_MAX;
    s->max_us = 0u;
    s->sum_us = 0u;

    s->min_cnt = UINT64_MAX;
    s->max_cnt = 0u;
    s->sum_cnt = 0u;
}

static void perf_stats_add(perf_stats_t *s, uint64_t elapsed_us, uint64_t elapsed_cnt)
{
    if (!s)
        return;

    if (elapsed_us < s->min_us)
        s->min_us = elapsed_us;

    if (elapsed_us > s->max_us)
        s->max_us = elapsed_us;

    s->sum_us += elapsed_us;

    if (elapsed_cnt < s->min_cnt)
        s->min_cnt = elapsed_cnt;

    if (elapsed_cnt > s->max_cnt)
        s->max_cnt = elapsed_cnt;

    s->sum_cnt += elapsed_cnt;

    s->count++;
}

static uint64_t perf_stats_avg_us(const perf_stats_t *s)
{
    return (s && s->count) ? s->sum_us / s->count : 0u;
}

#if CONFIG_PERF_STRINGS
static int perf_find_slot_by_name(const char *name)
{
    if (!name)
        return -1;

    for (int i = 0; i < CONFIG_PERF_PROBES_MAX; i++)
    {
        if (g_perf_entries[i].used
            && strncmp(g_perf_entries[i].name, name, CONFIG_PERF_NAME_LEN) == 0)
        {
            return i;
        }
    }

    return -1;
}
#endif

static int perf_find_slot_by_id(uint32_t id)
{
    for (int i = 0; i < CONFIG_PERF_PROBES_MAX; i++)
    {
        if (g_perf_entries[i].used && g_perf_entries[i].id == id)
            return i;
    }

    return -1;
}

static int perf_alloc_slot(uint32_t id, const char *name)
{
    for (int i = 0; i < CONFIG_PERF_PROBES_MAX; i++)
    {
        if (!g_perf_entries[i].used)
        {
            g_perf_entries[i].used = 1u;
            g_perf_entries[i].id = id;

#if CONFIG_PERF_STRINGS
            if (name)
            {
                strncpy(g_perf_entries[i].name, name, CONFIG_PERF_NAME_LEN - 1u);
                g_perf_entries[i].name[CONFIG_PERF_NAME_LEN - 1u] = '\0';
            }
            else
            {
                g_perf_entries[i].name[0] = '\0';
            }
#else
            (void)name;
#endif

            perf_stats_clear(&g_perf_entries[i].stats);

            return i;
        }
    }

    return -1;
}

static int perf_get_or_create_slot(const char *name, uint32_t id)
{
#if CONFIG_PERF_STRINGS
    if (name)
    {
        int slot = perf_find_slot_by_name(name);
        if (slot >= 0)
            return slot;
    }
#endif

    int slot = perf_find_slot_by_id(id);

    if (slot >= 0)
        return slot;

    return perf_alloc_slot(id, name);
}

static void perf_scope_stack_push(perf_scope_t *scope)
{
    if (!scope)
        return;

    if (g_scope_stack_top >= CONFIG_PERF_SCOPE_STACK_MAX)
        return;

    g_scope_stack[g_scope_stack_top++] = scope;
}

static void perf_scope_stack_pop(perf_scope_t *scope)
{
    if (!scope || g_scope_stack_top == 0u)
        return;

    if (g_scope_stack[g_scope_stack_top - 1u] == scope)
    {
        g_scope_stack_top--;
        return;
    }

    for (uint32_t i = 0; i < g_scope_stack_top; i++)
    {
        if (g_scope_stack[i] == scope)
        {
            for (uint32_t j = i; j + 1u < g_scope_stack_top; j++)
                g_scope_stack[j] = g_scope_stack[j + 1u];

            g_scope_stack_top--;
            return;
        }
    }
}

void perf_init(void)
{
    perf_hal_init();

    uint32_t lock = perf_hal_critical_enter();

    memset(g_perf_entries, 0, sizeof(g_perf_entries));
    memset(g_scope_stack, 0, sizeof(g_scope_stack));

    g_scope_stack_top = 0u;
    g_last_report_us = perf_hal_now_us();

    g_perf_enabled = 1u;
    g_perf_logging_enabled = 1u;

    perf_hal_critical_exit(lock);
}

void perf_set_enabled(bool enabled)
{
    uint32_t lock = perf_hal_critical_enter();
    g_perf_enabled = enabled ? 1u : 0u;
    perf_hal_critical_exit(lock);
}

bool perf_is_enabled(void) { return g_perf_enabled != 0u; }

void perf_set_logging(bool enabled)
{
    uint32_t lock = perf_hal_critical_enter();
    g_perf_logging_enabled = enabled ? 1u : 0u;
    perf_hal_critical_exit(lock);
}

bool perf_is_logging_enabled(void) { return g_perf_logging_enabled != 0u; }

void perf_reset_all(void)
{
    uint32_t lock = perf_hal_critical_enter();

    for (int i = 0; i < CONFIG_PERF_PROBES_MAX; i++)
    {
        if (g_perf_entries[i].used)
            perf_stats_clear(&g_perf_entries[i].stats);
    }

    perf_hal_critical_exit(lock);
}

void perf_scope_begin(perf_scope_t *scope, const char *name, uint32_t id)
{
    if (!scope)
        return;

    if (!g_perf_enabled)
    {
        scope->slot = -1;
        scope->active = 0u;
        scope->failed = 0u;
        return;
    }

    uint32_t lock = perf_hal_critical_enter();

    int slot = perf_get_or_create_slot(name, id);

    scope->slot = slot;
    scope->start_us = perf_hal_now_us();
    scope->start_cnt = perf_hal_counter();
    scope->active = (slot >= 0) ? 1u : 0u;
    scope->failed = 0u;

    if (scope->active)
        perf_scope_stack_push(scope);

    perf_hal_critical_exit(lock);
}

void perf_scope_end(perf_scope_t *scope)
{
    if (!scope || !scope->active)
        return;

    uint64_t end_us = perf_hal_now_us();
    uint64_t end_cnt = perf_hal_counter();

    uint64_t elapsed_us = end_us - scope->start_us;
    uint64_t elapsed_cnt = end_cnt - scope->start_cnt;

    uint32_t lock = perf_hal_critical_enter();

    if (scope->slot >= 0 && scope->slot < CONFIG_PERF_PROBES_MAX)
    {
        perf_entry_t *e = &g_perf_entries[scope->slot];

        if (e->used)
        {
            if (scope->failed)
                e->stats.fail_count++;

            perf_stats_add(&e->stats, elapsed_us, elapsed_cnt);
        }
    }

    perf_scope_stack_pop(scope);
    scope->active = 0u;

    perf_hal_critical_exit(lock);
}

void perf_scope_fail(perf_scope_t *scope)
{
    if (!scope || !scope->active)
        return;

    scope->failed = 1u;
}

void perf_fail_current(void)
{
    uint32_t lock = perf_hal_critical_enter();

    if (g_scope_stack_top > 0u)
    {
        perf_scope_t *scope = g_scope_stack[g_scope_stack_top - 1u];

        if (scope)
            scope->failed = 1u;
    }

    perf_hal_critical_exit(lock);
}

void perf_report(void)
{
#if CONFIG_PERF_LOG_ENABLE
    if (!g_perf_logging_enabled)
        return;

    for (int i = 0; i < CONFIG_PERF_PROBES_MAX; i++)
    {
        uint8_t used;
        uint32_t id;
        perf_stats_t snap;

#if CONFIG_PERF_STRINGS
        char name[CONFIG_PERF_NAME_LEN];
#endif

        uint32_t lock = perf_hal_critical_enter();

        used = g_perf_entries[i].used;

        if (used)
        {
            id = g_perf_entries[i].id;
            snap = g_perf_entries[i].stats;

#if CONFIG_PERF_STRINGS
            strncpy(name, g_perf_entries[i].name, CONFIG_PERF_NAME_LEN);
            name[CONFIG_PERF_NAME_LEN - 1u] = '\0';
#endif
        }

        perf_hal_critical_exit(lock);

        if (!used)
            continue;

        if (snap.count == 0u && snap.fail_count == 0u)
            continue;

#if CONFIG_PERF_STRINGS
        (void)id;
#endif

#if CONFIG_PERF_STRINGS
        perf_hal_printf("[PERF] %-32s ", name[0] ? name : "unnamed");
#else
        perf_hal_printf("[PERF] id=%lu ", (unsigned long)id);
#endif

        perf_hal_printf("n=%lu ", (unsigned long)snap.count);

        if (snap.fail_count != 0u)
            perf_hal_printf("fail=%lu ", (unsigned long)snap.fail_count);

        perf_hal_printf("min=%.3f ms avg=%.3f ms max=%.3f ms", (double)snap.min_us / 1000.0,
                (double)perf_stats_avg_us(&snap) / 1000.0,
                (double)snap.max_us / 1000.0);

        perf_hal_printf("\r\n");
    }

#else
    /*
     * Compile-time log stripping.
     * No format strings, no printf calls.
     */
#endif
}

void perf_report_and_reset(void)
{
    perf_report();
    perf_reset_all();
}

void perf_poll_report(void)
{
#if CONFIG_PERF_LOG_ENABLE
    if (!g_perf_logging_enabled)
        return;

    uint64_t now_us = perf_hal_now_us();

    if (now_us - g_last_report_us >= (uint64_t)CONFIG_PERF_REPORT_MS * 1000u)
    {
        g_last_report_us = now_us;
        perf_report_and_reset();
    }
#endif
}

#else

void perf_init(void) {}
void perf_set_enabled(bool enabled) { (void)enabled; }
bool perf_is_enabled(void) { return false; }
void perf_set_logging(bool enabled) { (void)enabled; }
bool perf_is_logging_enabled(void) { return false; }
void perf_reset_all(void) {}
void perf_report(void) {}
void perf_report_and_reset(void) {}
void perf_poll_report(void) {}
void perf_scope_begin(perf_scope_t *scope, const char *name, uint32_t id)
{
    (void)scope;
    (void)name;
    (void)id;
}
void perf_scope_end(perf_scope_t *scope) { (void)scope; }
void perf_scope_fail(perf_scope_t *scope) { (void)scope; }
void perf_fail_current(void) {}

#endif

#if defined(__GNUC__)
void perf_scope_cleanup(perf_scope_t *scope) { perf_scope_end(scope); }
#endif
