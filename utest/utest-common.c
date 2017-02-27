/*******************************************************************************
 * utest-common.c
 *
 * Common helpers for a unit-test application
 *
 * Copyright (c) 2014 Cogent Embedded Inc. ALL RIGHTS RESERVED.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *******************************************************************************/

#define MODULE_TAG                      COMMON

/*******************************************************************************
 * Includes
 ******************************************************************************/

#include "sv/trace.h"
#include "utest-common.h"

#include <fcntl.h>
#include <sys/timerfd.h>

#include <sys/syscall.h>
#include <sys/types.h>

#define gettid()    syscall(SYS_gettid)

#if !GLIB_CHECK_VERSION(2,36,0)
#define g_source_add_unix_fd(s, f, flags)       \
({                                              \
    GPollFD   *__fd = malloc(sizeof(*__fd));    \
    __fd->fd = (f), __fd->events = (flags);     \
    g_source_add_poll((s), __fd);               \
    (void *)__fd;                               \
})

#define g_source_remove_unix_fd(s, t)           \
({                                              \
    GPollFD    *__fd = (GPollFD *)(t);          \
    g_source_remove_poll((s), __fd);            \
    free(__fd);                                 \
})

#define g_source_query_unix_fd(s, t)            \
({                                              \
    GPollFD    *__fd = (GPollFD *)(t);          \
    (__fd->revents);                            \
})    
#endif
    
/*******************************************************************************
 * Tracing configuration
 ******************************************************************************/

TRACE_TAG(DEBUG, 0);

#if 0
/*******************************************************************************
 * Network source
 ******************************************************************************/

/* ...data-source handle */
typedef struct netif_source
{
    /* ...generic source handle */
    GSource             source;
    
    /* ...network stream data */
    netif_stream_t     *stream;

    /* ...polling object tag */
    gpointer            tag;

}   netif_source_t;

/* ...prepare handle */
static gboolean netif_source_prepare(GSource *source, gint *timeout)
{
    netif_source_t     *nsrc = (netif_source_t *)source;
    
    if (nsrc->tag && netif_stream_rx_ready(nsrc->stream))
    {
        TRACE(1, _b("camera source: %p - prepare - ready"), source);
        
        /* ...there is a buffer available for reading */
        return TRUE;
    }
    else
    {
        TRACE(1, _b("camera source: %p - prepare - nothing"), source);

        /* ...no buffer available; wait indefinitely */
        *timeout = -1;
        return FALSE;
    }
}

/* ...check function called after polling returns */
static gboolean netif_source_check(GSource *source)
{
    netif_source_t     *nsrc = (netif_source_t *) source;

    TRACE(1, _b("camera source: %p - check"), source);

    /* ...check if there is input data already */
    return (nsrc->tag && netif_stream_rx_ready(nsrc->stream));
}

/* ...dispatch function */
static gboolean netif_source_dispatch(GSource *source, GSourceFunc callback, gpointer user_data)
{
    netif_source_t     *nsrc = (netif_source_t *) source;

    TRACE(1, _b("camera source: %p - dispatch"), source);

    /* ...call dispatch function (if source has been removed, still return TRUE) */
    return (nsrc->tag ? callback(user_data) : TRUE);
}

/* ...finalization function */
static void netif_source_finalize(GSource *source)
{
    TRACE(1, _b("network source destroyed"));
}

/* ...source callbacks */
static GSourceFuncs netif_source_funcs = {
    .prepare = netif_source_prepare,
    .check = netif_source_check,
    .dispatch = netif_source_dispatch,
    .finalize = netif_source_finalize,
};

/*******************************************************************************
 * Public API
 ******************************************************************************/

/* ...create network stream source */
netif_source_t * netif_source_create(netif_stream_t *stream, gint prio, GSourceFunc func, gpointer user_data, GDestroyNotify notify)
{
    netif_source_t     *nsrc;
    GSource            *source;
    
    /* ...allocate source handle */
    CHK_ERR(source = g_source_new(&netif_source_funcs, sizeof(*nsrc)), NULL);
    
    /* ...set network stream pointer */
    (nsrc = (netif_source_t *)source)->stream = stream;
    
    /* ...add stream file handle - here? - no, postpone until explicit resume command */
    nsrc->tag = NULL;//g_source_add_unix_fd(source, netif_stream_fd(stream), G_IO_IN | G_IO_ERR);

    /* ...set priority */
    g_source_set_priority(source, prio);

    /* ...set callback function */
    g_source_set_callback(source, func, user_data, notify);
    
    /* ...attach source to the default thread context */
    g_source_attach(source, g_main_context_get_thread_default());

    /* ...pass ownership to the loop */
    g_source_unref(source);

    return nsrc;
}

/* ...suspend network source */
void netif_source_suspend(netif_source_t *nsrc)
{
    GSource    *source = (GSource *)nsrc;
    
    if (nsrc->tag)
    {
        g_source_remove_unix_fd(source, nsrc->tag);
        nsrc->tag = NULL;
        TRACE(DEBUG, _b("net-source [%p] suspended"), nsrc);
    }
}

/* ...resume network source */
void netif_source_resume(netif_source_t *nsrc, int purge)
{
    GSource    *source = (GSource *)nsrc;
    
    if (!nsrc->tag)
    {
        /* ...purge stream content if needed */
        (purge ? netif_stream_rx_purge(nsrc->stream) : 0);
        nsrc->tag = g_source_add_unix_fd(source, netif_stream_fd(nsrc->stream), G_IO_IN | G_IO_ERR);
        TRACE(DEBUG, _b("net-source[%p] resumed"), nsrc);
    }
}

/* ...check if data source is active */
int netif_source_is_active(netif_source_t *nsrc)
{
    return (nsrc->tag != NULL);
}
#endif

/*******************************************************************************
 * File source
 ******************************************************************************/

/* ...data-source handle */
typedef struct fd_source
{
    /* ...generic source handle */
    GSource             source;
    
    /* ...file descriptor */
    int                 fd;

    /* ...polling object tag */
    gpointer            tag;

}   fd_source_t;

/* ...prepare handle */
static gboolean fd_source_prepare(GSource *source, gint *timeout)
{
    /* ...we need to go to "poll" call anyway to understand if there is a data */
    *timeout = -1;
    return FALSE;
}

/* ...check function called after polling returns */
static gboolean fd_source_check(GSource *source)
{
    fd_source_t     *fsrc = (fd_source_t *) source;

    TRACE(DEBUG, _b("src-check[%p]: tag=%p, poll=%u"), fsrc, fsrc->tag, (fsrc->tag ? !!(g_source_query_unix_fd(source, fsrc->tag) & G_IO_IN) : 0));
    
    /* ...test if last poll returned data availability */
    return (fsrc->tag && (g_source_query_unix_fd(source, fsrc->tag) & G_IO_IN));
}

/* ...dispatch function */
static gboolean fd_source_dispatch(GSource *source, GSourceFunc callback, gpointer user_data)
{
    fd_source_t     *fsrc = (fd_source_t *) source;
    
    TRACE(DEBUG, _b("src-dispatch[%p]: tag=%p"), fsrc, fsrc->tag);

    /* ...call dispatch function */
    return (fsrc->tag ? callback(user_data) : TRUE);
}

/* ...finalization function */
static void fd_source_finalize(GSource *source)
{
    TRACE(1, _b("fd-source destroyed"));
}

/* ...source callbacks */
static GSourceFuncs fd_source_funcs = {
    .prepare = fd_source_prepare,
    .check = fd_source_check,
    .dispatch = fd_source_dispatch,
    .finalize = fd_source_finalize,
};

/* ...file source creation */
fd_source_t * fd_source_create(const char *filename, gint prio, GSourceFunc func, gpointer user_data,
        GDestroyNotify notify, GMainContext *context)
{
    fd_source_t    *fsrc;
    GSource        *source;
    
    /* ...allocate source handle */
    CHK_ERR(source = g_source_new(&fd_source_funcs, sizeof(*fsrc)), NULL);
    
    /* ...create file descriptor */
    (fsrc = (fd_source_t *)source)->fd = open(filename, O_RDONLY | O_NONBLOCK);

    /* ...make sure descriptor is valid */
    CHK_ERR(fsrc->fd >= 0, NULL);
    
    /* ...add stream file handle - here? - no, postpone until explicit resume command */
    fsrc->tag = NULL;//g_source_add_unix_fd(source, fsrc->fd, G_IO_IN | G_IO_ERR);

    /* ...set priority */
    g_source_set_priority(source, prio);

    /* ...set callback function */
    g_source_set_callback(source, func, user_data, notify);
    
    /* ...attach source to the default thread context */
    g_source_attach(source, context);
    //g_source_attach(source, g_main_context_get_thread_default());

    /* ...pass ownership to the loop */
    g_source_unref(source);

    return fsrc;
}

/* ...retrive file descriptor */
int fd_source_get_fd(fd_source_t *fsrc)
{
    return fsrc->fd;
}

/* ...suspend file source */
void fd_source_suspend(fd_source_t *fsrc)
{
    GSource    *source = (GSource *)fsrc;

    if (fsrc->tag)
    {
        g_source_remove_unix_fd(source, fsrc->tag);
        fsrc->tag = NULL;
        TRACE(DEBUG, _b("fd-source [%p] suspended"), fsrc);
    }
}

/* ...resume file source */
void fd_source_resume(fd_source_t *fsrc)
{
    GSource    *source = (GSource *)fsrc;

    if (!fsrc->tag)
    {
        /* ...add stream descriptor to the data source */
        fsrc->tag = g_source_add_unix_fd(source, fsrc->fd, G_IO_IN | G_IO_ERR);
        TRACE(DEBUG, _b("fd-source[%p] resumed"), fsrc);
    }
}

/* ...check if data source is active */
int fd_source_is_active(fd_source_t *fsrc)
{
    return (fsrc->tag != NULL);
}

/*******************************************************************************
 * Timeout support
 ******************************************************************************/

/* ...data-source handle */
typedef struct timer_source
{
    /* ...generic source handle */
    GSource             source;
    
    /* ...timer file descriptor */
    int                 tfd;

    /* ...polling object tag */
    gpointer            tag;

}   timer_source_t;

/* ...prepare handle */
static gboolean timer_source_prepare(GSource *source, gint *timeout)
{
    /* ...we need to go to "poll" call anyway */
    *timeout = -1;
    return FALSE;
}

/* ...check function called after polling returns */
static gboolean timer_source_check(GSource *source)
{
    timer_source_t     *tsrc = (timer_source_t *) source;

    TRACE(DEBUG, _b("timer-fd: %p, poll: %X"), tsrc->tag, (tsrc->tag ? g_source_query_unix_fd(source, tsrc->tag) : ~0U));

    /* ...test if last poll returned data availability */
    if (tsrc->tag && (g_source_query_unix_fd(source, tsrc->tag) & G_IO_IN))
    {
        guint64     value;
        
        /* ...read timer value to clear polling flag */
        read(tsrc->tfd, &value, sizeof(value));
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

/* ...dispatch function */
static gboolean timer_source_dispatch(GSource *source, GSourceFunc callback, gpointer user_data)
{
    timer_source_t     *tsrc = (timer_source_t *) source;
    
    /* ...call dispatch function if still enabled */
    return (tsrc->tag ? callback(user_data) : TRUE);
}

/* ...finalization function */
static void timer_source_finalize(GSource *source)
{
    TRACE(1, _b("timer-source destroyed"));
}

/* ...source callbacks */
static GSourceFuncs timer_source_funcs = {
    .prepare = timer_source_prepare,
    .check = timer_source_check,
    .dispatch = timer_source_dispatch,
    .finalize = timer_source_finalize,
};

/* ...file source creation */
timer_source_t * timer_source_create(GSourceFunc func, gpointer user_data,
        GDestroyNotify notify, GMainContext *context)
{
    timer_source_t *tsrc;
    GSource        *source;
    
    /* ...allocate source handle */
    CHK_ERR(source = g_source_new(&timer_source_funcs, sizeof(*tsrc)), NULL);
    
    /* ...create file descriptor */
    (tsrc = (timer_source_t *)source)->tfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);

    /* ...make sure descriptor is valid */
    CHK_ERR(tsrc->tfd >= 0, NULL);
    
    /* ...do not enable source until explicit start command received */
    tsrc->tag = NULL;

    /* ...set priority */
    g_source_set_priority(source, G_PRIORITY_DEFAULT);

    /* ...set callback function */
    g_source_set_callback(source, func, user_data, notify);
    
    /* ...attach source to the default thread context */
    g_source_attach(source, context);

    /* ...pass ownership to the loop */
    g_source_unref(source);

    return tsrc;
}

/* ...retrive file descriptor */
int timer_source_get_fd(timer_source_t *tsrc)
{
    return tsrc->tfd;
}

/* ...start timeout operation */
void timer_source_start(timer_source_t *tsrc, u32 interval, u32 period)
{
    GSource            *source = (GSource *)tsrc;
    struct itimerspec   ts;

    /* ...(re)set timer parameters */
    ts.it_interval.tv_sec = period / 1000;
    ts.it_interval.tv_nsec = (period % 1000) * 1000000;
    ts.it_value.tv_sec = interval / 1000;
    ts.it_value.tv_nsec = (interval % 1000) * 1000000;
    timerfd_settime(tsrc->tfd, 0, &ts, NULL);

    /* ...add timer-source to the poll loop as needed */
    (!tsrc->tag ? tsrc->tag = g_source_add_unix_fd(source, tsrc->tfd, G_IO_IN | G_IO_ERR) : 0);

    TRACE(DEBUG, _b("timer-source[%p] activated (int=%u, period=%u)"), tsrc, interval, period);
}

/* ...suspend file source */
void timer_source_stop(timer_source_t *tsrc)
{
    GSource    *source = (GSource *)tsrc;

    if (tsrc->tag)
    {
        struct itimerspec   ts;

        /* ...remove timer from poll loop */
        g_source_remove_unix_fd(source, tsrc->tag);
        tsrc->tag = NULL;
        
        /* ...disable timer operation */
        memset(&ts, 0, sizeof(ts));
        timerfd_settime(tsrc->tfd, 0, &ts, NULL);
        
        TRACE(DEBUG, _b("timer-source [%p] suspended"), tsrc);
    }
}

/* ...check if data source is active */
int timer_source_is_active(timer_source_t *tsrc)
{
    return (tsrc->tag != NULL);
}

/* ...tracing lock */
static pthread_mutex_t  intern_trace_mutex;

/* ...tracing to communication processor */
int intern_trace(const char *format, ...)
{
    va_list             args;
    struct timespec     ts;

    /* ...retrieve value of monotonic clock */
    clock_gettime(CLOCK_MONOTONIC, &ts);

    /* ...get global tracing lock */
    pthread_mutex_lock(&intern_trace_mutex);

    /* ...output timestamp */
    printf("[%02u.%06u] ", (u32)ts.tv_sec, (u32)ts.tv_nsec / 1000);

    /* ...output format string */
    va_start(args, format);
    vprintf(format, args);
    va_end(args);

    /* ...output string terminator */
    putchar('\n');

    /* ...release tracing lock */
    pthread_mutex_unlock(&intern_trace_mutex);

    return 0;
}

/* ...tracing facility initialization */
void intern_trace_init(const char *banner)
{
    /* ...initialize tracing lock */
    pthread_mutex_init(&intern_trace_mutex, NULL);

    /* ...output banner */
    intern_trace("%s", banner);
}
