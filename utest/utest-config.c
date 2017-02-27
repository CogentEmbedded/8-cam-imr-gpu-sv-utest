/*******************************************************************************
 * utest-config.c
 *
 * Configuration parameters parsing
 *
 * Copyright (c) 2016 Cogent Embedded Inc. ALL RIGHTS RESERVED.
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

#define MODULE_TAG                      CFG

/*******************************************************************************
 * Includes
 ******************************************************************************/

#include "sv/trace.h"
#include "utest-app.h"

/*******************************************************************************
 * Tracing configuration
 ******************************************************************************/

TRACE_TAG(INIT, 1);
TRACE_TAG(INFO, 1);
TRACE_TAG(DEBUG, 0);

/*******************************************************************************
 * Global variables definition
 ******************************************************************************/

#define RESOURCES_DIR       "./"
#define __IMR_SV_THUMB      RESOURCES_DIR "thumb"
#define __IMR_SV_SCENE      RESOURCES_DIR "car"

#define __IMR_SV_VIEW(rx, ry, rz, s1, s2)                                   \
    {                                                                       \
        .thumb = __IMR_SV_THUMB ":" #rx ":" #ry ":" #rz ":" #s1 ".png",     \
        .scene = __IMR_SV_SCENE ":" #rx ":" #ry ":" #rz ":" #s2 ".png",     \
    }

/* ...static IMR-based surround-view scenes */
app_view_cfg_t  __app_cfg_view_default[] = {
    __IMR_SV_VIEW(0.0, 0.0, 0.0, 1.5, 1.0),
    __IMR_SV_VIEW(-67.5, 0.0, 0.0, 1.5, 1.0),
    __IMR_SV_VIEW(-67.5, 0.0, 45.0, 1.5, 1.0),
    __IMR_SV_VIEW(-67.5, 0.0, 90.0, 1.5, 1.0),
    __IMR_SV_VIEW(-67.5, 0.0, 135.0, 1.5, 1.0),
    __IMR_SV_VIEW(-67.5, 0.0, 180.0, 1.5, 1.0),
    __IMR_SV_VIEW(-67.5, 0.0, 225.0, 1.5, 1.0),
    __IMR_SV_VIEW(-67.5, 0.0, 270.0, 1.5, 1.0),
    __IMR_SV_VIEW(-67.5, 0.0, 315.0, 1.5, 1.0),
};

/* ...default camera intrinsics */
#define __CAM_CFG_DEFAULT                                       \
    {                                                           \
        .K = {                                                  \
            3.357587e+02,   0,              6.355935e+02,       \
            0,              3.358816e+02,   3.932316e+02,       \
            0,              0,              1,                  \
        },                                                      \
                                                                \
        .D = {                                                  \
             -1.7359490832433585e-03,  5.1147541423526843e-02,  \
             -7.5079833577096832e-03, -1.7857924267113917e-03,  \
        }                                                       \
    }

/* ...application configuration data */
app_cfg_t   __app_cfg = {
    /* ...static views for IMR carousel */
    .views = __app_cfg_view_default,
    .views_number = sizeof(__app_cfg_view_default) / sizeof(__app_cfg_view_default[0]),

    /* ...cameras intrinsic parameters */
    .camera = {
        [0] = __CAM_CFG_DEFAULT,
        [1] = __CAM_CFG_DEFAULT,
        [2] = __CAM_CFG_DEFAULT,
        [3] = __CAM_CFG_DEFAULT,
    },

    /* ...carousel menu dimensions */
    .carousel_x = 9,
    .carousel_y = 3,

    /* ...gradient color for carousel menu */
    .carousel_border = {
        .c0 = 0xD2691E00,
        .c1 = 0x00000000,
        .sharpness = 4.0,
    },

    /* ...border color for surround-view scene selection */
    .sv_border = {
        .c0 = 0xD2691E00,
        .c1 = 0x00000000,
        .sharpness = 1.0,
    },

    /* ...border color for smart-cameras windows in active state */
    .sc_active_border = {
        .c0 = 0xD2691E00,
        .c1 = 0x00000000,
        .sharpness = 1.0,
    },

    /* ...border color for smart-cameras windows in inactive state */
    .sc_inactive_border = {
        .c0 = 0x000000FF,
        .c1 = 0x00000000,
        .sharpness = 1.0,
    },
};
    
/*******************************************************************************
 * Local typedefs
 ******************************************************************************/

/* ...file parser data */
typedef struct cfg_parser
{
    FILE               *f;
    const char         *fname;
    int                 line;
    char               *saveptr;
    char               *buffer;

}   cfg_parser_t;

/*******************************************************************************
 * Auxiliary macros
 ******************************************************************************/

/* ...floating point parsing */
#define STRTOF(c, v)                            \
({                                              \
    char   *__p;                                \
    (v) = strtof((c), &__p);                    \
    *__p != '\0';                               \
})

/* ...integer value parsing */
#define STRTOU(c, v)                            \
({                                              \
    char   *__p;                                \
    (v) = strtoul((c), &__p, 0);                \
    *__p != '\0';                               \
})

#define STRTOU2(c, v, p)                        \
({                                              \
    (v) = (int)strtoul((c), &(p), 0);           \
    *(p) != '\0';                               \
})
    
/*******************************************************************************
 * Configuration file parsing
 ******************************************************************************/

/*******************************************************************************
 * Internal helpers
 ******************************************************************************/

/* ...open parser */
static inline int parser_open(cfg_parser_t *p, const char *fname)
{
    /* ...open file descriptor */
    CHK_ERR(p->f = fopen(fname, "rt"), -errno);

    /* ...reset line number */
    p->line = 0;

    /* ...clear line buffer pointer */
    p->buffer = NULL;

    return 0;
}

/* ...close parser */
static void parser_close(cfg_parser_t *p)
{
    /* ...destroy scratch line buffer */
    (p->buffer ? free(p->buffer) : 0);

    /* ...close file handle */
    fclose(p->f);
}

/* ...read next non-empty line */
static char * read_line(cfg_parser_t *p)
{
    int             n = 0;
    char           *t;
    size_t          length = 0;
    
    /* ...fetch next line */
    while (getline(&p->buffer, &length, p->f) >= 0)
    {
        /* ...advance line number */
        n++;

        TRACE(DEBUG, _b("%d: %s"), p->line + n, p->buffer);

        /* ...parse string into sequence of tokens; skip empty line */
        if ((t = strtok_r(p->buffer, " \t", &p->saveptr)) == NULL)    continue;
        
        /* ...skip comments */
        if (t[0] == '#')      continue;

        /* ...found non-empty line */
        TRACE(DEBUG, _b("%d: %s"), n, t);

        /* ...put line number */
        p->line += n;

        return t;
    }

    /* ...end-of-file */
    return NULL;
}

/* ...parse next token */
static inline char * read_token(cfg_parser_t *p)
{
    char   *t = strtok_r(NULL, " \t=,\n", &p->saveptr);

    /* ...strip comments */
    return (t && t[0] != '#' ? t : NULL);
}

/*******************************************************************************
 * Tokens parsing
 ******************************************************************************/

static int parse_views(app_cfg_t *cfg, cfg_parser_t *p)
{
    char   *t;
    char   *thumb, *scene;

    /* ...drop default config */
    (!cfg->views_alloc ? cfg->views = NULL, cfg->views_number = 0 : 0);
    
    /* ...get the thumbnail image filename */
    if ((t = read_token(p)) == NULL)        return -1;
    scene = t;

    /* ...get the full-scale scene filename */
    if ((t = read_token(p)) == NULL)        return -1;
    thumb = t;

    /* ...make sure we have reached end of line */
    if ((t = read_token(p)) != NULL)        return -1;
    
    /* ...make sure we have a room in the views array */
    if (cfg->views_alloc == cfg->views_number)
    {
        CHK_ERR(cfg->views = realloc(cfg->views, (cfg->views_alloc += 4) * sizeof(*cfg->views)), -(errno = ENOMEM));
    }

    TRACE(INIT, _b("view #%d: %s, %s"), cfg->views_number, thumb, scene);
    
    /* ...save filenames */
    CHK_ERR(cfg->views[cfg->views_number].thumb = strdup(thumb), -(errno = ENOMEM));
    CHK_ERR(cfg->views[cfg->views_number].scene = strdup(scene), -(errno = ENOMEM));
    cfg->views_number++;
    
    return 0;
}

/* ...parse smart-cameras transformation parameters */
static int parse_intrinsics(app_camera_cfg_t *cfg, cfg_parser_t *p)
{
    char   *t;
    int     i, j;

    /* ...read matrix */
    for (i = 0; i < 3; i++)
    {
        for (j = 0; j < 3; j++)
        {
            if ((t = read_token(p)) == NULL)        return -1;
            if (STRTOF(t, __M(3, cfg->K, j, i)))    return -1;
        }
    }
        
    /* ...read vector */
    for (i = 0; i < 4; i++)
    {
        if ((t = read_token(p)) == NULL)    return -1;
        if (STRTOF(t, cfg->D[i]))           return -1;
    }

    return 0;
}

/* ...parse border gradient configuration */
static int parse_border(app_border_cfg_t *cfg, cfg_parser_t *p)
{
    char    *t;

    if ((t = read_token(p)) == NULL)    return -1;
    if (STRTOU(t, cfg->c0))             return -1;

    if ((t = read_token(p)) == NULL)    return -1;
    if (STRTOU(t, cfg->c1))             return -1;

    if ((t = read_token(p)) == NULL)    return -1;
    if (STRTOF(t, cfg->sharpness))  return -1;

    return 0;
}

/*******************************************************************************
 * Parsing function
 ******************************************************************************/

enum {
    CFG_STATIC_VIEW,
    CFG_CAMERA_0,
    CFG_CAMERA_1,
    CFG_CAMERA_2,
    CFG_CAMERA_3,
    CFG_CAROUSEL_BORDER,
    CFG_SV_BORDER,
    CFG_SC_ACTIVE_BORDER,
    CFG_SC_INACTIVE_BORDER,
};

/* ...parameter name parsing */
static inline int cfg_parameter(char *t)
{
    if (!strcmp(t, "view"))                     return CFG_STATIC_VIEW;
    else if (!strcmp(t, "cam0"))                return CFG_CAMERA_0;
    else if (!strcmp(t, "cam1"))                return CFG_CAMERA_1;
    else if (!strcmp(t, "cam2"))                return CFG_CAMERA_2;
    else if (!strcmp(t, "cam3"))                return CFG_CAMERA_3;
    else if (!strcmp(t, "carousel_border"))     return CFG_CAROUSEL_BORDER;
    else if (!strcmp(t, "sc_active_border"))    return CFG_SC_ACTIVE_BORDER;
    else if (!strcmp(t, "sc_inactive_border"))  return CFG_SC_INACTIVE_BORDER;
    else if (!strcmp(t, "sv_border"))           return CFG_SV_BORDER;
    else                                        return -1;
}

int config_parse(char *fname)
{
    app_cfg_t      *cfg = &__app_cfg;
    cfg_parser_t    p;
    char           *t;
    int             r;
    
    /* ...create parser */
    CHK_API(parser_open(&p, fname));

    /* ...go process the file; find all sections */
    while ((t = read_line(&p)) != NULL)
    {
        int     param = cfg_parameter(t);
 
        /* ...ignore unrecognized parameter */
        if (param < 0)  continue;

        switch (param)
        {
        case CFG_STATIC_VIEW:
            /* ...parse static views */
            if (parse_views(cfg, &p) < 0)   goto error;
            break;

        case CFG_CAMERA_0:
            /* ...parse static views */
            if (parse_intrinsics(&cfg->camera[0], &p) < 0)   goto error;
            break;

        case CFG_CAMERA_1:
            /* ...parse static views */
            if (parse_intrinsics(&cfg->camera[1], &p) < 0)   goto error;
            break;

        case CFG_CAMERA_2:
            /* ...parse static views */
            if (parse_intrinsics(&cfg->camera[2], &p) < 0)   goto error;
            break;

        case CFG_CAMERA_3:
            /* ...parse static views */
            if (parse_intrinsics(&cfg->camera[3], &p) < 0)   goto error;
            break;

        case CFG_CAROUSEL_BORDER:
            if (parse_border(&cfg->carousel_border, &p) < 0)    goto error;
            break;

        case CFG_SC_ACTIVE_BORDER:
            if (parse_border(&cfg->sc_active_border, &p) < 0)    goto error;
            break;

        case CFG_SC_INACTIVE_BORDER:
            if (parse_border(&cfg->sc_inactive_border, &p) < 0)    goto error;
            break;

        case CFG_SV_BORDER:
            if (parse_border(&cfg->sv_border, &p) < 0)    goto error;
            break;

        default:
            /* ...unrecognized command; ignore */
            TRACE(INFO, _b("unrecognized parameter: '%s'"), t);
        }
    }

    TRACE(INIT, _b("parsing successful"));
    r = 0;
    
out:
    /* ...close parser afterwards */
    parser_close(&p); 
    return r;

error:
    TRACE(ERROR, _b("parsing failed: %s:%d"), fname, p.line);
    r = -EINVAL;
    goto out;

}
