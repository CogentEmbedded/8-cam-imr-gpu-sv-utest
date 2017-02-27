/*******************************************************************************
 * utest-camera.h
 *
 * Camera interface for surround view application
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

#ifndef __UTEST_CAMERA_H
#define __UTEST_CAMERA_H

/* ...opaque camera data handle */
typedef struct camera_data  camera_data_t;

/*******************************************************************************
 * Camera interface
 ******************************************************************************/

typedef struct camera_callback
{
    /* ...buffer allocation hook */
    int       (*allocate)(void *data, int id, GstBuffer *buffer);

    /* ...buffer preparation hook */
    int       (*prepare)(void *data, int id, GstBuffer *buffer);
    
    /* ...buffer processing hook */
    int       (*process)(void *data, int id, GstBuffer *buffer);

}   camera_callback_t;

/* ...camera data source callback structure */
typedef struct camera_source_callback
{
    /* ...end-of-stream signalization */
    void      (*eos)(void *data);

    /* ...packet processing hook (ethernet frame) */
    void      (*pdu)(void *data, int id, u8 *pdu, u16 len, u64 ts);

    /* ...packet processing hook (CAN message) */
    void      (*can)(void *data, u32 can_id, u8 *msg, u8 dlc, u64 ts);
    
}   camera_source_callback_t;

/* ...camera set initialization function */
typedef GstElement * (*camera_init_func_t)(const camera_callback_t *cb, void *cdata);

/*******************************************************************************
 * Entry points
 ******************************************************************************/

/* ...camera back-ends */
extern GstElement * video_stream_create(const camera_callback_t *cb, void *cdata);
extern GstElement * camera_mjpeg_create(const camera_callback_t *cb, void *cdata);

/* ...individual camera creation */
extern camera_data_t * mjpeg_camera_create(int id, GstBuffer * (*get_buffer)(void *, int), void *cdata);
extern GstElement * mjpeg_camera_gst_element(camera_data_t *camera);

const char * video_stream_filename(void);

/* ...ethernet frame processing callback - tbd */
extern void camera_mjpeg_packet_receive(int id, u8 *pdu, u16 len, u64 ts);

#endif  /* __UTEST_CAMERA_H */
