/* GStreamer unit tests for queue2
 *
 * Copyright (C) 2011 Tim-Philipp Müller <tim centricular net>
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

#include <gst/check/gstcheck.h>

static GstElement *
setup_queue2 (GstElement * pipe, GstElement * input, GstElement * output)
{
  GstElement *queue2;
  GstPad *sinkpad, *srcpad;

  queue2 = gst_element_factory_make ("queue2", NULL);
  fail_unless (queue2 != NULL, "failed to create 'queue2' element");

  gst_bin_add (GST_BIN (pipe), queue2);
  gst_bin_add (GST_BIN (pipe), input);
  gst_bin_add (GST_BIN (pipe), output);

  sinkpad = gst_element_get_static_pad (queue2, "sink");
  fail_unless (sinkpad != NULL, "failed to get queue2 sink pad");

  srcpad = gst_element_get_static_pad (input, "src");
  fail_unless (srcpad != NULL, "failed to find src pad for input element");

  fail_unless_equals_int (GST_PAD_LINK_OK, gst_pad_link (srcpad, sinkpad));
  gst_object_unref (srcpad);
  gst_object_unref (sinkpad);

  srcpad = gst_element_get_static_pad (queue2, "src");
  fail_unless (srcpad != NULL);

  sinkpad = gst_element_get_static_pad (output, "sink");
  fail_unless (sinkpad != NULL, "failed to find sink pad of output element");

  fail_unless_equals_int (GST_PAD_LINK_OK, gst_pad_link (srcpad, sinkpad));

  gst_object_unref (srcpad);
  gst_object_unref (sinkpad);

  return queue2;
}

GST_START_TEST (test_simple_pipeline)
{
  GstElement *pipe, *input, *output;
  GstMessage *msg;

  pipe = gst_pipeline_new ("pipeline");

  input = gst_element_factory_make ("fakesrc", NULL);
  fail_unless (input != NULL, "failed to create 'fakesrc' element");
  g_object_set (input, "num-buffers", 256, "sizetype", 3, NULL);

  output = gst_element_factory_make ("fakesink", NULL);
  fail_unless (output != NULL, "failed to create 'fakesink' element");

  setup_queue2 (pipe, input, output);

  gst_element_set_state (pipe, GST_STATE_PLAYING);

  msg = gst_bus_poll (GST_ELEMENT_BUS (pipe),
      GST_MESSAGE_EOS | GST_MESSAGE_ERROR, -1);

  fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR,
      "Expected EOS message, got ERROR message");
  gst_message_unref (msg);

  GST_LOG ("Got EOS, cleaning up");

  gst_element_set_state (pipe, GST_STATE_NULL);
  gst_object_unref (pipe);
}

GST_END_TEST;

GST_START_TEST (test_simple_pipeline_ringbuffer)
{
  GstElement *pipe, *queue2, *input, *output;
  GstMessage *msg;

  pipe = gst_pipeline_new ("pipeline");

  input = gst_element_factory_make ("fakesrc", NULL);
  fail_unless (input != NULL, "failed to create 'fakesrc' element");
  g_object_set (input, "num-buffers", 256, "sizetype", 3, NULL);

  output = gst_element_factory_make ("fakesink", NULL);
  fail_unless (output != NULL, "failed to create 'fakesink' element");

  queue2 = setup_queue2 (pipe, input, output);
  g_object_set (queue2, "ring-buffer-max-size", (guint64) 1024 * 50, NULL);

  gst_element_set_state (pipe, GST_STATE_PLAYING);

  msg = gst_bus_poll (GST_ELEMENT_BUS (pipe),
      GST_MESSAGE_EOS | GST_MESSAGE_ERROR, -1);

  fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR,
      "Expected EOS message, got ERROR message");
  gst_message_unref (msg);

  GST_LOG ("Got EOS, cleaning up");

  gst_element_set_state (pipe, GST_STATE_NULL);
  gst_object_unref (pipe);
}

GST_END_TEST;

static void
do_test_simple_shutdown_while_running (guint64 ring_buffer_max_size)
{
  GstElement *pipe, *q2;
  GstElement *input;
  GstElement *output;
  GstMessage *msg;

  pipe = gst_pipeline_new ("pipeline");

  input = gst_element_factory_make ("fakesrc", NULL);
  fail_unless (input != NULL, "failed to create 'fakesrc' element");
  g_object_set (input, "format", GST_FORMAT_TIME, "sizetype", 2,
      "sizemax", 10, NULL);

  output = gst_element_factory_make ("fakesink", NULL);
  fail_unless (output != NULL, "failed to create 'fakesink' element");

  q2 = setup_queue2 (pipe, input, output);

  if (ring_buffer_max_size > 0) {
    g_object_set (q2, "ring-buffer-max-size", ring_buffer_max_size,
        "temp-template", NULL, NULL);
  }

  gst_element_set_state (pipe, GST_STATE_PAUSED);

  /* wait until pipeline is up and running */
  msg = gst_bus_poll (GST_ELEMENT_BUS (pipe),
      GST_MESSAGE_ERROR | GST_MESSAGE_ASYNC_DONE, -1);
  fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR, "Got ERROR message");
  gst_message_unref (msg);

  GST_LOG ("pipeline is running now");
  gst_element_set_state (pipe, GST_STATE_PLAYING);
  g_usleep (G_USEC_PER_SEC / 20);

  /* now shut down only the sink, so the queue gets a wrong-state flow return */
  gst_element_set_state (output, GST_STATE_NULL);
  GST_LOG ("Cleaning up");

  gst_element_set_state (pipe, GST_STATE_NULL);
  gst_object_unref (pipe);
}

GST_START_TEST (test_simple_shutdown_while_running)
{
  int i;

  /* run a couple of iterations, gives higher chance of different code paths
   * being executed at time the flush is detected (esp. useful to make sure
   * things are cleaned up properly when running under valgrind) */
  for (i = 0; i < 10; ++i) {
    do_test_simple_shutdown_while_running (0);
  }
}

GST_END_TEST;

GST_START_TEST (test_simple_shutdown_while_running_ringbuffer)
{
  int i;

  /* run a couple of iterations, gives higher chance of different code paths
   * being executed at time the flush is detected (esp. useful to make sure
   * things are cleaned up properly when running under valgrind) */
  for (i = 0; i < 10; ++i) {
    do_test_simple_shutdown_while_running (1024 * 1024);
  }
}

GST_END_TEST;

GST_START_TEST (test_simple_create_destroy)
{
  GstElement *queue2;

  queue2 = gst_element_factory_make ("queue2", NULL);
  gst_object_unref (queue2);
}

GST_END_TEST;

static gpointer
push_buffer (GstPad * sinkpad)
{
  GstBuffer *buffer;

  buffer = gst_buffer_new_and_alloc (1 * 1024);

  gst_pad_chain (sinkpad, buffer);

  return NULL;
}

GST_START_TEST (test_filled_read)
{
  GstElement *queue2;
  GstBuffer *buffer;
  GstPad *sinkpad, *srcpad;
  GThread *thread;
  GstSegment segment;

  queue2 = gst_element_factory_make ("queue2", NULL);
  sinkpad = gst_element_get_static_pad (queue2, "sink");
  srcpad = gst_element_get_static_pad (queue2, "src");

  g_object_set (queue2, "ring-buffer-max-size", (guint64) 5 * 1024,
      "use-buffering", FALSE,
      "max-size-buffers", (guint) 0, "max-size-time", (guint64) 0,
      "max-size-bytes", (guint) 4 * 1024, NULL);

  gst_pad_activate_mode (srcpad, GST_PAD_MODE_PULL, TRUE);
  gst_element_set_state (queue2, GST_STATE_PLAYING);

  gst_segment_init (&segment, GST_FORMAT_BYTES);
  gst_pad_send_event (sinkpad, gst_event_new_stream_start ("test"));
  gst_pad_send_event (sinkpad, gst_event_new_segment (&segment));

  /* fill up the buffer */
  buffer = gst_buffer_new_and_alloc (4 * 1024);
  fail_unless (gst_pad_chain (sinkpad, buffer) == GST_FLOW_OK);

  thread =
      g_thread_try_new ("gst-check", (GThreadFunc) push_buffer, sinkpad, NULL);
  fail_unless (thread != NULL);

  buffer = NULL;
  fail_unless (gst_pad_get_range (srcpad, 1024, 4 * 1024,
          &buffer) == GST_FLOW_OK);

  fail_unless (gst_buffer_get_size (buffer) == 4 * 1024);
  gst_buffer_unref (buffer);

  gst_element_set_state (queue2, GST_STATE_NULL);

  g_thread_join (thread);

  gst_object_unref (sinkpad);
  gst_object_unref (srcpad);
  gst_object_unref (queue2);
}

GST_END_TEST;


static GstPadProbeReturn
block_callback (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  return GST_PAD_PROBE_OK;
}

GST_START_TEST (test_percent_overflow)
{
  GstElement *queue2;
  GstBuffer *buffer;
  GstPad *sinkpad, *srcpad;
  gulong block_probe;
  guint64 i;
  guint64 current_level_time;
  GstSegment segment;

  queue2 = gst_element_factory_make ("queue2", NULL);
  sinkpad = gst_element_get_static_pad (queue2, "sink");
  srcpad = gst_element_get_static_pad (queue2, "src");

  block_probe = gst_pad_add_probe (srcpad,
      GST_PAD_PROBE_TYPE_BLOCK | GST_PAD_PROBE_TYPE_BUFFER,
      block_callback, NULL, NULL);

  g_object_set (queue2, "use-buffering", TRUE,
      "use-rate-estimate", FALSE,
      "max-size-buffers", 0,
      "max-size-time", 2 * GST_SECOND, "max-size-bytes", 0, NULL);

  gst_pad_activate_mode (srcpad, GST_PAD_MODE_PUSH, TRUE);
  gst_element_set_state (queue2, GST_STATE_PAUSED);

  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.start = 0;
  segment.time = 0;
  segment.position = 0;
  gst_pad_send_event (sinkpad, gst_event_new_stream_start ("test"));
  gst_pad_send_event (sinkpad, gst_event_new_segment (&segment));

  /* push 2 seconds of data with valid but excessively high timestamps */
  for (i = 0; i < 20; i++) {
    buffer = gst_buffer_new_and_alloc (1024);
    GST_BUFFER_PTS (buffer) =
        G_GUINT64_CONSTANT (18446744071709551616) + i * (GST_SECOND / 10);
    GST_BUFFER_DTS (buffer) =
        G_GUINT64_CONSTANT (18446744071709551616) + i * (GST_SECOND / 10);
    GST_BUFFER_DURATION (buffer) = (GST_SECOND / 10);
    fail_unless (gst_pad_chain (sinkpad, buffer) == GST_FLOW_OK);
  }

  g_object_get (queue2, "current-level-time", &current_level_time, NULL);

  gst_pad_remove_probe (srcpad, block_probe);

  gst_element_set_state (queue2, GST_STATE_NULL);

  gst_object_unref (sinkpad);
  gst_object_unref (srcpad);
  gst_object_unref (queue2);
}

GST_END_TEST;

GST_START_TEST (test_small_ring_buffer)
{
  GstElement *pipeline;
  GstElement *queue2;
  const gchar *desc;
  GstBus *bus;
  GstMessage *msg;

  /* buffer too small to seek used to crash, test for regression */
  desc = "fakesrc sizetype=2 sizemax=4096 num-buffers=100 datarate=1000 ! "
      "queue2 ring-buffer-max-size=1000 name=q2 ! fakesink sync=true";

  pipeline = gst_parse_launch (desc, NULL);
  fail_if (pipeline == NULL);

  queue2 = gst_bin_get_by_name (GST_BIN (pipeline), "q2");
  fail_if (queue2 == NULL);

  /* bring the pipeline to PLAYING, then start switching */
  bus = gst_element_get_bus (pipeline);
  fail_if (bus == NULL);
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  /* Wait for the pipeline to hit playing */
  gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

  /* now wait for completion or error */
  msg = gst_bus_poll (bus, GST_MESSAGE_EOS | GST_MESSAGE_ERROR, -1);
  fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR,
      "Expected EOS message, got ERROR message");
  gst_message_unref (msg);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (queue2);
  gst_object_unref (bus);
  gst_object_unref (pipeline);
}

GST_END_TEST;

static Suite *
queue2_suite (void)
{
  Suite *s = suite_create ("queue2");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_simple_create_destroy);
  tcase_add_test (tc_chain, test_simple_pipeline);
  tcase_add_test (tc_chain, test_simple_pipeline_ringbuffer);
  tcase_add_test (tc_chain, test_simple_shutdown_while_running);
  tcase_add_test (tc_chain, test_simple_shutdown_while_running_ringbuffer);
  tcase_add_test (tc_chain, test_filled_read);
  tcase_add_test (tc_chain, test_percent_overflow);
  tcase_add_test (tc_chain, test_small_ring_buffer);

  return s;
}

GST_CHECK_MAIN (queue2)
