/*******************************************************************************
 * utest-meta.c
 *
 * Custom buffer metadata processing
 *
 * Copyright (c) 2015 Cogent Embedded Inc. ALL RIGHTS RESERVED.
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

#define MODULE_TAG                      META

/*******************************************************************************
 * Includes
 ******************************************************************************/

#include "sv/trace.h"
#include "utest-common.h"
#include "utest-meta.h"
#include "utest-display.h"

/*******************************************************************************
 * Tracing configuration
 ******************************************************************************/

TRACE_TAG(DEBUG, 0);

/*******************************************************************************
 * Custom buffer metadata implementation
 ******************************************************************************/

/* ...metadata type registration */
GType objdet_meta_api_get_type(void)
{
    static volatile GType type;
    static const gchar *tags[] = { GST_META_TAG_MEMORY_STR, NULL };

    if (g_once_init_enter(&type))
    {
        GType _type = gst_meta_api_type_register("ObjDetMetaAPI", tags);
        g_once_init_leave(&type, _type);
    }

    return type;
}

/* ...low-level interface */
static gboolean objdet_meta_init(GstMeta *meta, gpointer params, GstBuffer *buffer)
{
    objdet_meta_t  *_meta = (objdet_meta_t *) meta;

    /* ...reset fields */
    memset(&_meta->meta + 1, 0, sizeof(*_meta) - sizeof(GstMeta));

    return TRUE;
}

/* ...metadata transformation */
static gboolean objdet_meta_transform(GstBuffer *transbuf, GstMeta *meta,
        GstBuffer *buffer, GQuark type, gpointer data)
{
    objdet_meta_t  *_meta = (objdet_meta_t *) meta, *_tmeta;

    /* ...add road scene metadata for a transformed buffer */
    _tmeta = gst_buffer_add_objdet_meta(transbuf);

    /* ...just copy data regardless of transform type? */
    memcpy(&_tmeta->meta + 1, &_meta->meta + 1, sizeof(*_meta) - sizeof(GstMeta));

    return TRUE;
}

/* ...metadata release */
static void objdet_meta_free(GstMeta *meta, GstBuffer *buffer)
{
    objdet_meta_t  *_meta = (objdet_meta_t *) meta;

    /* ...anything to destroy? - tbd */
    TRACE(DEBUG, _b("free metadata %p"), _meta);
}

/* ...register metadata implementation */
const GstMetaInfo * objdet_meta_get_info(void)
{
    static const GstMetaInfo *meta_info = NULL;

    if (g_once_init_enter(&meta_info))
    {
        const GstMetaInfo *mi = gst_meta_register(
            OBJDET_META_API_TYPE,
            "ObjDetMeta",
            sizeof(objdet_meta_t),
            objdet_meta_init,
            objdet_meta_free,
            objdet_meta_transform);

        g_once_init_leave (&meta_info, mi);
    }

    return meta_info;
}
