/* RTP funnel element for GStreamer
 *
 * gstrtpfunnel.c:
 *
 * Copyright (C) <2017> Pexip.
 *   Contact: Havard Graff <havard@pexip.com>
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
#include "config.h"
#endif

#include "gstrtpfunnel.h"

GST_DEBUG_CATEGORY_STATIC (gst_rtp_funnel_debug);
#define GST_CAT_DEFAULT gst_rtp_funnel_debug

struct _GstRtpFunnelPad
{
  GstAggregatorPad pad;
  guint32 ssrc;
};

enum
{
  PROP_0,
  PROP_COMMON_TS_OFFSET,
};

#define DEFAULT_COMMON_TS_OFFSET -1

G_DEFINE_TYPE (GstRtpFunnelPad, gst_rtp_funnel_pad, GST_TYPE_AGGREGATOR_PAD);

static void
gst_rtp_funnel_pad_class_init (GstRtpFunnelPadClass * klass)
{
  (void) klass;
}

static void
gst_rtp_funnel_pad_init (GstRtpFunnelPad * pad)
{
  GST_OBJECT_FLAG_SET (pad, GST_PAD_FLAG_PROXY_CAPS);
  GST_OBJECT_FLAG_SET (pad, GST_PAD_FLAG_PROXY_ALLOCATION);
}

struct _GstRtpFunnel
{
  GstAggregator element;

  GstPad *srcpad;
  GstCaps *srccaps;
  GHashTable *ssrc_to_pad;

  /* properties */
  gint common_ts_offset;
};

#define RTP_CAPS "application/x-rtp"

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS (RTP_CAPS));

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (RTP_CAPS));

#define gst_rtp_funnel_parent_class parent_class
G_DEFINE_TYPE (GstRtpFunnel, gst_rtp_funnel, GST_TYPE_AGGREGATOR);

static gboolean
gst_rtp_funnel_sink_event (GstAggregator * agg, GstAggregatorPad * agg_pad,
    GstEvent * event)
{
  GstRtpFunnel *funnel = GST_RTP_FUNNEL_CAST (agg);
  gboolean forward = TRUE;
  gboolean ret = TRUE;

  GST_DEBUG_OBJECT (agg_pad, "received event %" GST_PTR_FORMAT, event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_STREAM_START:
    case GST_EVENT_SEGMENT:
      forward = FALSE;
      break;
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;
      GstStructure *s;
      guint ssrc;
      gst_event_parse_caps (event, &caps);

      if (!gst_caps_can_intersect (funnel->srccaps, caps)) {
        GST_ERROR_OBJECT (funnel, "Can't intersect with caps %" GST_PTR_FORMAT,
            caps);
        g_assert_not_reached ();
      }

      s = gst_caps_get_structure (caps, 0);
      if (gst_structure_get_uint (s, "ssrc", &ssrc)) {
        GstRtpFunnelPad *fpad = GST_RTP_FUNNEL_PAD_CAST (agg_pad);
        fpad->ssrc = ssrc;
        GST_DEBUG_OBJECT (agg_pad, "Got ssrc: %u", ssrc);
        GST_OBJECT_LOCK (funnel);
        g_hash_table_insert (funnel->ssrc_to_pad, GUINT_TO_POINTER (ssrc),
            agg_pad);
        GST_OBJECT_UNLOCK (funnel);
      }

      forward = FALSE;
      break;
    }
    default:
      break;
  }

  if (forward) {
    ret = GST_AGGREGATOR_CLASS (parent_class)->sink_event (agg, agg_pad, event);
  } else {
    gst_event_unref (event);
  }

  return ret;
}

static gboolean
gst_rtp_funnel_sink_query (GstAggregator * agg, GstAggregatorPad * agg_pad,
    GstQuery * query)
{
  GstRtpFunnel *funnel = GST_RTP_FUNNEL_CAST (agg);
  gboolean res = FALSE;
  (void) funnel;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *filter_caps;
      GstCaps *new_caps;

      gst_query_parse_caps (query, &filter_caps);

      if (filter_caps) {
        new_caps = gst_caps_intersect_full (funnel->srccaps, filter_caps,
            GST_CAPS_INTERSECT_FIRST);
      } else {
        new_caps = gst_caps_copy (funnel->srccaps);
      }

      if (funnel->common_ts_offset >= 0)
        gst_caps_set_simple (new_caps, "timestamp-offset", G_TYPE_UINT,
            (guint) funnel->common_ts_offset, NULL);

      gst_query_set_caps_result (query, new_caps);
      GST_DEBUG_OBJECT (agg_pad, "Answering caps-query with caps: %"
          GST_PTR_FORMAT, new_caps);
      gst_caps_unref (new_caps);
      res = TRUE;
      break;
    }
    default:
      res =
          GST_AGGREGATOR_CLASS (parent_class)->sink_query (agg, agg_pad, query);
      break;
  }

  return res;
}

static gboolean
gst_rtp_funnel_src_event (GstAggregator * agg, GstEvent * event)
{
  GstRtpFunnel *funnel = GST_RTP_FUNNEL_CAST (agg);
  gboolean handled = FALSE;
  gboolean ret = TRUE;

  GST_DEBUG_OBJECT (agg, "received src event %" GST_PTR_FORMAT, event);

  if (GST_EVENT_TYPE (event) == GST_EVENT_CUSTOM_UPSTREAM) {
    const GstStructure *s = gst_event_get_structure (event);
    GstPad *fpad;
    guint ssrc;
    if (s && gst_structure_get_uint (s, "ssrc", &ssrc)) {
      handled = TRUE;

      GST_OBJECT_LOCK (funnel);
      fpad = g_hash_table_lookup (funnel->ssrc_to_pad, GUINT_TO_POINTER (ssrc));
      if (fpad)
        gst_object_ref (fpad);
      GST_OBJECT_UNLOCK (funnel);

      if (fpad) {
        GST_INFO_OBJECT (agg, "Sending %" GST_PTR_FORMAT " to %" GST_PTR_FORMAT,
            event, fpad);
        ret = gst_pad_push_event (fpad, event);
        gst_object_unref (fpad);
      } else {
        gst_event_unref (event);
      }
    }
  }

  if (!handled) {
    ret = GST_AGGREGATOR_CLASS (parent_class)->src_event (agg, event);
  }

  return ret;
}

static void
gst_rtp_funnel_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRtpFunnel *funnel = GST_RTP_FUNNEL_CAST (object);

  switch (prop_id) {
    case PROP_COMMON_TS_OFFSET:
      funnel->common_ts_offset = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_funnel_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstRtpFunnel *funnel = GST_RTP_FUNNEL_CAST (object);

  switch (prop_id) {
    case PROP_COMMON_TS_OFFSET:
      g_value_set_int (value, funnel->common_ts_offset);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
_remove_pad_func (gpointer key, gpointer value, gpointer user_data)
{
  (void) key;
  if (GST_PAD_CAST (value) == GST_PAD_CAST (user_data))
    return TRUE;
  return FALSE;
}

static void
gst_rtp_funnel_release_pad (GstElement * element, GstPad * pad)
{
  GstRtpFunnel *funnel = GST_RTP_FUNNEL_CAST (element);

  GST_DEBUG_OBJECT (funnel, "releasing pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  g_hash_table_foreach_remove (funnel->ssrc_to_pad, _remove_pad_func, pad);

  GST_ELEMENT_CLASS (parent_class)->release_pad (element, pad);
}

static void
gst_rtp_funnel_finalize (GObject * object)
{
  GstRtpFunnel *funnel = GST_RTP_FUNNEL_CAST (object);

  gst_caps_unref (funnel->srccaps);
  g_hash_table_destroy (funnel->ssrc_to_pad);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* We only want to push buffers once, and we want to consume buffers
 * in a time-aware fashion.
 */
static GstFlowReturn
gst_rtp_funnel_aggregate (GstAggregator * agg, gboolean timeout)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GList *l;
  GstAggregatorPad *earliest = NULL;
  GstClockTime earliest_pts = GST_CLOCK_TIME_NONE;

  for (l = GST_ELEMENT_CAST (agg)->sinkpads; l; l = l->next) {
    GstRtpFunnelPad *pad = GST_RTP_FUNNEL_PAD (l->data);
    GstAggregatorPad *agg_pad = GST_AGGREGATOR_PAD (pad);
    GstBuffer *buffer = gst_aggregator_pad_peek_buffer (agg_pad);

    if (!buffer)
      continue;

    if (!earliest || GST_BUFFER_PTS (buffer) < earliest_pts) {
      earliest = agg_pad;
      earliest_pts = GST_BUFFER_PTS (buffer);
    }

    gst_buffer_unref (buffer);
  }

  if (earliest) {
    GstBuffer *buffer = gst_aggregator_pad_pop_buffer (earliest);

    ret = gst_aggregator_finish_buffer (agg, buffer);
  }

  return ret;
}

static void
gst_rtp_funnel_class_init (GstRtpFunnelClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GstAggregatorClass *gstaggregator_class = GST_AGGREGATOR_CLASS (klass);

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_rtp_funnel_finalize);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_rtp_funnel_get_property);
  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_rtp_funnel_set_property);
  gstelement_class->release_pad =
      GST_DEBUG_FUNCPTR (gst_rtp_funnel_release_pad);
  gstaggregator_class->aggregate = GST_DEBUG_FUNCPTR (gst_rtp_funnel_aggregate);
  gstaggregator_class->sink_event =
      GST_DEBUG_FUNCPTR (gst_rtp_funnel_sink_event);
  gstaggregator_class->sink_query =
      GST_DEBUG_FUNCPTR (gst_rtp_funnel_sink_query);
  gstaggregator_class->src_event = GST_DEBUG_FUNCPTR (gst_rtp_funnel_src_event);

  gst_element_class_set_static_metadata (gstelement_class, "RTP funnel",
      "RTP Funneling",
      "Funnel RTP buffers together for multiplexing",
      "Havard Graff <havard@gstip.com>");

  gst_element_class_add_static_pad_template_with_gtype (gstelement_class,
      &sink_template, GST_TYPE_RTP_FUNNEL_PAD);
  gst_element_class_add_static_pad_template_with_gtype (gstelement_class,
      &src_template, GST_TYPE_AGGREGATOR_PAD);

  g_object_class_install_property (gobject_class, PROP_COMMON_TS_OFFSET,
      g_param_spec_int ("common-ts-offset", "Common Timestamp Offset",
          "Use the same RTP timestamp offset for all sinkpads (-1 = disable)",
          -1, G_MAXINT32, DEFAULT_COMMON_TS_OFFSET,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  GST_DEBUG_CATEGORY_INIT (gst_rtp_funnel_debug,
      "gstrtpfunnel", 0, "funnel element");
}

static void
gst_rtp_funnel_init (GstRtpFunnel * funnel)
{
  funnel->srcpad = GST_AGGREGATOR_CAST (funnel)->srcpad;
  gst_pad_use_fixed_caps (funnel->srcpad);

  funnel->srccaps = gst_caps_new_empty_simple (RTP_CAPS);
  funnel->ssrc_to_pad = g_hash_table_new (NULL, NULL);
}
