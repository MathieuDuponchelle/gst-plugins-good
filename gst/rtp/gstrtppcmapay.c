/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2005> Edgard Lima <edgard.lima@gmail.com>
 * Copyright (C) <2005> Nokia Corporation <kai.vehmanen@nokia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <gst/rtp/gstrtpbuffer.h>
#include <gst/audio/audio.h>

#include "gstrtppcmapay.h"

typedef struct _GstRtpBufferPool GstRtpBufferPool;
typedef struct _GstRtpBufferPoolClass GstRtpBufferPoolClass;

#define GST_TYPE_RTP_BUFFER_POOL      (gst_rtp_buffer_pool_get_type())
#define GST_IS_RTP_BUFFER_POOL(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_RTP_BUFFER_POOL))
#define GST_RTP_BUFFER_POOL(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_RTP_BUFFER_POOL, GstRtpBufferPool))
#define GST_RTP_BUFFER_POOL_CAST(obj) ((GstRtpBufferPool*)(obj))

static GType gst_rtp_buffer_pool_get_type (void);

struct _GstRtpBufferPool
{
  GstBufferPool parent;

  GstAllocator *allocator;
  GstAllocationParams params;
};

struct _GstRtpBufferPoolClass
{
  GstBufferPoolClass parent_class;
};

#define gst_rtp_buffer_pool_parent_class pool_parent_class
G_DEFINE_TYPE (GstRtpBufferPool, gst_rtp_buffer_pool, GST_TYPE_BUFFER_POOL);

static void
gst_rtp_buffer_pool_dispose (GObject * object)
{
  G_OBJECT_CLASS (pool_parent_class)->dispose (object);
}

static GstFlowReturn
gst_rtp_buffer_pool_acquire_buffer (GstBufferPool * bpool, GstBuffer ** buffer,
    GstBufferPoolAcquireParams * params)
{
  GstBufferPoolClass *pclass = GST_BUFFER_POOL_CLASS (pool_parent_class);
  gsize size, offset;
  guint header_len = gst_rtp_buffer_calc_header_len (0);

  GstFlowReturn flow = pclass->acquire_buffer (bpool, buffer, params);

  if (flow != GST_FLOW_OK)
    goto done;

  if (!gst_rtp_buffer_initialize_header (*buffer, 0, 0)) {
    flow = GST_FLOW_ERROR;
    gst_buffer_pool_release_buffer (bpool, *buffer);
    goto done;
  }

  size = gst_buffer_get_sizes (*buffer, &offset, NULL);
  gst_buffer_resize (*buffer, offset + header_len, size - header_len);

done:
  return flow;
}

static void
gst_rtp_buffer_pool_init (GstRtpBufferPool * self)
{
}

static void
gst_rtp_buffer_pool_class_init (GstRtpBufferPoolClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstBufferPoolClass *bufferpool_class = GST_BUFFER_POOL_CLASS (klass);

  object_class->dispose = gst_rtp_buffer_pool_dispose;
  bufferpool_class->acquire_buffer = gst_rtp_buffer_pool_acquire_buffer;
}

static GstBufferPool *
gst_rtp_buffer_pool_new (void)
{
  GstBufferPool *ret =
      GST_BUFFER_POOL (g_object_new (GST_TYPE_RTP_BUFFER_POOL, NULL));

  return ret;
}

static GstStaticPadTemplate gst_rtp_pcma_pay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-alaw, channels=(int)1, rate=(int)8000")
    );

static GstStaticPadTemplate gst_rtp_pcma_pay_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) " GST_RTP_PAYLOAD_PCMA_STRING ", "
        "clock-rate = (int) 8000, " "encoding-name = (string) \"PCMA\"; "
        "application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) [1, MAX ], " "encoding-name = (string) \"PCMA\"")
    );

static gboolean gst_rtp_pcma_pay_setcaps (GstRTPBasePayload * payload,
    GstCaps * caps);

#define gst_rtp_pcma_pay_parent_class parent_class
G_DEFINE_TYPE (GstRtpPcmaPay, gst_rtp_pcma_pay,
    GST_TYPE_RTP_BASE_AUDIO_PAYLOAD);

static gboolean
gst_rtp_pcma_pay_propose_allocation (GstRtpPcmaPay * self, GstQuery * query)
{
  GstCaps *caps;
  guint size = GST_RTP_BASE_PAYLOAD_MTU (self);

  gst_query_parse_allocation (query, &caps, NULL);

  if (caps == NULL)
    return FALSE;

  if (gst_query_get_n_allocation_pools (query) == 0) {
    GstStructure *structure;
    GstAllocator *allocator = NULL;
    GstAllocationParams params = { 0, 15, 0, 0 };

    if (gst_query_get_n_allocation_params (query) > 0)
      gst_query_parse_nth_allocation_param (query, 0, &allocator, &params);
    else
      gst_query_add_allocation_param (query, allocator, &params);

    self->pool = gst_rtp_buffer_pool_new ();

    structure = gst_buffer_pool_get_config (self->pool);
    gst_buffer_pool_config_set_params (structure, caps, size, 0, 0);
    gst_buffer_pool_config_set_allocator (structure, allocator, &params);

    GST_RTP_BUFFER_POOL (self->pool)->allocator = allocator;
    GST_RTP_BUFFER_POOL (self->pool)->params = params;

    if (allocator)
      gst_object_unref (allocator);

    if (!gst_buffer_pool_set_config (self->pool, structure))
      goto config_failed;

    gst_buffer_pool_set_active (self->pool, TRUE);

    gst_query_add_allocation_pool (query, self->pool, size, 0, 0);
  }

  return TRUE;

  /* ERRORS */
config_failed:
  {
    GST_ERROR_OBJECT (self, "failed to set config");
    gst_object_unref (self->pool);
    return FALSE;
  }
}

static gboolean
gst_rtp_pcma_pay_query (GstRTPBasePayload * pay, GstPad * pad, GstQuery * query)
{
  gboolean ret;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_ALLOCATION:
      ret = gst_rtp_pcma_pay_propose_allocation (GST_RTP_PCMA_PAY (pay), query);
      break;
    default:
      ret = GST_RTP_BASE_PAYLOAD_CLASS (parent_class)->query (pay, pad, query);
      break;
  }

  return ret;
}

static gboolean
gst_rtp_pcma_pay_prepare_output_buffer (GstRTPBaseAudioPayload * pay,
    GstBuffer * paybuf, GstBuffer ** outbuf)
{
  GstRtpPcmaPay *self = GST_RTP_PCMA_PAY (pay);
  gboolean ret = TRUE;

  if (paybuf->pool && paybuf->pool == self->pool) {
    gsize size, offset;

    size = gst_buffer_get_sizes (paybuf, &offset, NULL);
    gst_buffer_resize (paybuf, offset * -1, size + offset);

    *outbuf = paybuf;
  } else {
    ret =
        GST_RTP_BASE_AUDIO_PAYLOAD_CLASS (parent_class)->prepare_output_buffer
        (pay, paybuf, outbuf);
  }

  return ret;
}

static void
gst_rtp_pcma_pay_class_init (GstRtpPcmaPayClass * klass)
{
  GstElementClass *gstelement_class;
  GstRTPBasePayloadClass *gstrtpbasepayload_class;
  GstRTPBaseAudioPayloadClass *gstrtpbaseaudiopayload_class;

  gstelement_class = (GstElementClass *) klass;
  gstrtpbasepayload_class = (GstRTPBasePayloadClass *) klass;
  gstrtpbaseaudiopayload_class = (GstRTPBaseAudioPayloadClass *) klass;

  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_rtp_pcma_pay_sink_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_rtp_pcma_pay_src_template);

  gst_element_class_set_static_metadata (gstelement_class, "RTP PCMA payloader",
      "Codec/Payloader/Network/RTP",
      "Payload-encodes PCMA audio into a RTP packet",
      "Edgard Lima <edgard.lima@gmail.com>");

  gstrtpbasepayload_class->set_caps = gst_rtp_pcma_pay_setcaps;
  gstrtpbasepayload_class->query = gst_rtp_pcma_pay_query;
  gstrtpbaseaudiopayload_class->prepare_output_buffer =
      gst_rtp_pcma_pay_prepare_output_buffer;
}

static void
gst_rtp_pcma_pay_init (GstRtpPcmaPay * rtppcmapay)
{
  GstRTPBaseAudioPayload *rtpbaseaudiopayload;

  rtpbaseaudiopayload = GST_RTP_BASE_AUDIO_PAYLOAD (rtppcmapay);

  GST_RTP_BASE_PAYLOAD (rtppcmapay)->pt = GST_RTP_PAYLOAD_PCMA;
  GST_RTP_BASE_PAYLOAD (rtppcmapay)->clock_rate = 8000;

  /* tell rtpbaseaudiopayload that this is a sample based codec */
  gst_rtp_base_audio_payload_set_sample_based (rtpbaseaudiopayload);

  /* octet-per-sample is 1 for PCM */
  gst_rtp_base_audio_payload_set_sample_options (rtpbaseaudiopayload, 1);
}

static gboolean
gst_rtp_pcma_pay_setcaps (GstRTPBasePayload * payload, GstCaps * caps)
{
  gboolean res;

  gst_rtp_base_payload_set_options (payload, "audio",
      payload->pt != GST_RTP_PAYLOAD_PCMA, "PCMA", 8000);
  res = gst_rtp_base_payload_set_outcaps (payload, NULL);

  return res;
}

gboolean
gst_rtp_pcma_pay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtppcmapay",
      GST_RANK_SECONDARY, GST_TYPE_RTP_PCMA_PAY);
}
