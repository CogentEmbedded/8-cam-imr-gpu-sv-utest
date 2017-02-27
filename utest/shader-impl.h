/*******************************************************************************
 * shader-impl.h
 *
 * Shader names/indices lists building (helper)
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

#ifndef SHADER_TAG
#error "SHADER_TAG must be set"
#endif

#ifndef SHADER_ATTRIBUTES
#error "SHADER_ATTRIBUTES must be set"
#endif

#ifndef SHADER_UNIFORMS
#error "SHADER_UNIFORMS must be set"
#endif

#if !defined(SHADER_VERTEX_SOURCE) && !defined(SHADER_VERTEX_BINARY)
#error "either SHADER_VERTEX_SOURCE or SHADER_VERTEX_BINARY must be set"
#endif

#if !defined(SHADER_FRAGMENT_SOURCE) && !defined(SHADER_FRAGMENT_BINARY)
#error "either SHADER_FRAGMENT_SOURCE or SHADER_FRAGMENT_BINARY must be set"
#endif

/*******************************************************************************
 * Convenience macros definition
 ******************************************************************************/

#ifndef __SHADER_IMPL_H
#define __SHADER_IMPL_H

/* ...uniform list item id */
#define __UNIFORM(tag, name)        __UNIFORM_##tag##_##name

/* ...uniforms list name */
#define __UNIFORM_LIST(tag)         __UNIFORM_LIST2(tag)
#define __UNIFORM_LIST2(tag)        __uniform_##tag
#define __UNIFORM_LIST_SIZE(tag)    __UNIFORM_LIST_SIZE2(tag)
#define __UNIFORM_LIST_SIZE2(tag)   __UNIFORM_LIST_SIZE_##tag

/* ...uniform index accessor */
#define UNIFORM(tag, name)          __UNIFORM(tag, name)

/* ...uniforms list */
#define UNIFORMS(tag)               __UNIFORM_LIST(tag)

/* ...uniforms number in a tagged set */
#define UNIFORMS_NUM(tag)           __UNIFORM_LIST_SIZE(tag)

/* ...attribute list name */
#define __ATTRIBUTE(tag, name)      __ATTRIBUTE_##tag##_##name

/* ...attributes list name */
#define __ATTRIBUTE_LIST(tag)       __ATTRIBUTE_LIST2(tag)
#define __ATTRIBUTE_LIST2(tag)      __attribute_##tag
#define __ATTRIBUTE_LIST_SIZE(tag)  __ATTRIBUTE_LIST_SIZE2(tag)
#define __ATTRIBUTE_LIST_SIZE2(tag) __ATTRIBUTE_LIST_SIZE_##tag

/* ...attribute index definition */
#define ATTRIBUTE(tag, name)        __ATTRIBUTE(tag, name)

/* ...attributes list */
#define ATTRIBUTES(tag)             __ATTRIBUTE_LIST(tag)

/* ...attributes number in a tagged set */
#define ATTRIBUTES_NUM(tag)         __ATTRIBUTE_LIST_SIZE(tag)

/* ...shader descriptor name */
#define __SHADER_DESC(tag)          __SHADER_DESC2(tag)
#define __SHADER_DESC2(tag)         __shader_desc_##tag
#define SHADER_DESC(tag)            __SHADER_DESC(tag)

#endif  /* __SHADER_IMPL_H */

/*******************************************************************************
 * Attributes list
 ******************************************************************************/

/* ...uniforms index definitions */
#define __U(name)      UNIFORM(SHADER_TAG, name)
enum __UNIFORM_LIST(SHADER_TAG) {
    SHADER_UNIFORMS
    __UNIFORM_LIST_SIZE(SHADER_TAG)
};
#undef  __U

/* ...uniforms symbolic names definitions */
#define __U(name)       #name
static const char * const __UNIFORM_LIST(SHADER_TAG)[__UNIFORM_LIST_SIZE(SHADER_TAG) + 1] = {
    SHADER_UNIFORMS
    NULL
};
#undef __U

/* ...attributes index definitions */
#define __A(name)      ATTRIBUTE(SHADER_TAG, name)
enum __ATTRIBUTE_LIST(SHADER_TAG) {
    SHADER_ATTRIBUTES
    __ATTRIBUTE_LIST_SIZE(SHADER_TAG)
};
#undef  __A

/* ...attributes symbolic names definitions */
#define __A(name)       #name
static const char * const __ATTRIBUTE_LIST(SHADER_TAG)[__ATTRIBUTE_LIST_SIZE(SHADER_TAG) + 1] = {
    SHADER_ATTRIBUTES
    NULL
};
#undef __A

/* ...define shader descriptor */
static const __attribute__((unused)) shader_desc_t SHADER_DESC(SHADER_TAG) = {
#ifdef SHADER_VERTEX_SOURCE
    .v_src = &SHADER_VERTEX_SOURCE,
#endif
#ifdef SHADER_VERTEX_BINARY
    .v_bin = &SHADER_VERTEX_BINARY,
#endif
#ifdef SHADER_FRAGMENT_SOURCE
    .f_src = &SHADER_FRAGMENT_SOURCE,
#endif
#ifdef SHADER_FRAGMENT_BINARY
    .f_bin = &SHADER_FRAGMENT_BINARY,
#endif
    .attr = __ATTRIBUTE_LIST(SHADER_TAG),
    .attr_num = __ATTRIBUTE_LIST_SIZE(SHADER_TAG),
    .uni = __UNIFORM_LIST(SHADER_TAG),
    .uni_num = __UNIFORM_LIST_SIZE(SHADER_TAG),
};
    
/* ...cleanup context */
#undef SHADER_TAG
#undef SHADER_ATTRIBUTES
#undef SHADER_UNIFORMS
#undef SHADER_VERTEX_SOURCE
#undef SHADER_FRAGMENT_SOURCE
#undef SHADER_VERTEX_BINARY
#undef SHADER_FRAGMENT_BINARY
