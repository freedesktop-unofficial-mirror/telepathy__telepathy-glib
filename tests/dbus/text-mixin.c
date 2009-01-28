/* Basic test for the text mixin and the echo example CM.
 *
 * Copyright (C) 2007-2008 Collabora Ltd. <http://www.collabora.co.uk/>
 * Copyright (C) 2007-2008 Nokia Corporation
 *
 * Copying and distribution of this file, with or without modification,
 * are permitted in any medium without royalty provided the copyright
 * notice and this notice are preserved.
 */

#include <telepathy-glib/channel.h>
#include <telepathy-glib/connection.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/debug.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/interfaces.h>

#include "examples/cm/echo/chan.h"
#include "examples/cm/echo/conn.h"
#include "tests/lib/myassert.h"
#include "tests/lib/util.h"

static int fail = 0;

static void
myassert_failed (void)
{
  fail = 1;
}

static guint received_count = 0;
static guint last_received_id = 0;
static guint last_received_sender = 0;
static guint last_received_type = 0;
static guint last_received_flags = 0;
static gchar *last_received_text = NULL;

static guint sent_count = 0;
static guint last_sent_type = 0;
static gchar *last_sent_text = NULL;

static void
on_sent (TpChannel *chan,
         guint timestamp,
         guint type,
         const gchar *text,
         gpointer data,
         GObject *object)
{
  g_message ("%p: Sent: time %u, type %u, text '%s'",
      chan, timestamp, type, text);

  sent_count++;
  last_sent_type = type;
  g_free (last_sent_text);
  last_sent_text = g_strdup (text);
}

static void
on_received (TpChannel *chan,
             guint id,
             guint timestamp,
             guint sender,
             guint type,
             guint flags,
             const gchar *text,
             gpointer data,
             GObject *object)
{
  TpHandleRepoIface *contact_repo = data;

  g_message ("%p: Received #%u: time %u, sender %u '%s', type %u, flags %u, "
      "text '%s'", chan, id, timestamp, sender,
      tp_handle_inspect (contact_repo, sender), type, flags, text);

  received_count++;
  last_received_id = id;
  last_received_sender = sender;
  last_received_type = type;
  last_received_flags = flags;
  g_free (last_received_text);
  last_received_text = g_strdup (text);
}

int
main (int argc,
      char **argv)
{
  ExampleEchoConnection *service_conn;
  TpBaseConnection *service_conn_as_base;
  TpHandleRepoIface *contact_repo;
  ExampleEchoChannel *service_chan;
  TpDBusDaemon *dbus;
  TpConnection *conn;
  TpChannel *chan;
  GError *error = NULL;
  gchar *name;
  gchar *conn_path;
  gchar *chan_path;
  TpHandle handle;

  g_type_init ();
  /* tp_debug_set_flags ("all"); */
  dbus = tp_dbus_daemon_new (tp_get_bus ());

  service_conn = EXAMPLE_ECHO_CONNECTION (g_object_new (
        EXAMPLE_TYPE_ECHO_CONNECTION,
        "account", "me@example.com",
        "protocol", "example",
        NULL));
  service_conn_as_base = TP_BASE_CONNECTION (service_conn);
  MYASSERT (service_conn != NULL, "");
  MYASSERT (service_conn_as_base != NULL, "");

  MYASSERT (tp_base_connection_register (service_conn_as_base, "example",
        &name, &conn_path, &error), "");
  MYASSERT_NO_ERROR (error);

  conn = tp_connection_new (dbus, name, conn_path, &error);
  MYASSERT (conn != NULL, "");
  MYASSERT_NO_ERROR (error);

  MYASSERT (tp_connection_run_until_ready (conn, TRUE, &error, NULL),
      "");
  MYASSERT_NO_ERROR (error);

  contact_repo = tp_base_connection_get_handles (service_conn_as_base,
      TP_HANDLE_TYPE_CONTACT);
  MYASSERT (contact_repo != NULL, "");

  handle = tp_handle_ensure (contact_repo, "them@example.org", NULL, &error);
  MYASSERT_NO_ERROR (error);

  /* FIXME: exercise RequestChannel rather than just pasting on a channel */

  chan_path = g_strdup_printf ("%s/Channel", conn_path);

  service_chan = EXAMPLE_ECHO_CHANNEL (g_object_new (
        EXAMPLE_TYPE_ECHO_CHANNEL,
        "connection", service_conn,
        "object-path", chan_path,
        "handle", handle,
        NULL));

  chan = tp_channel_new (conn, chan_path, TP_IFACE_CHANNEL_TYPE_TEXT,
      TP_HANDLE_TYPE_CONTACT, handle, &error);
  MYASSERT_NO_ERROR (error);

  tp_channel_run_until_ready (chan, &error, NULL);
  MYASSERT_NO_ERROR (error);

  MYASSERT (tp_cli_channel_type_text_connect_to_received (chan, on_received,
      g_object_ref (contact_repo), g_object_unref, NULL, NULL) != NULL, "");
  MYASSERT (tp_cli_channel_type_text_connect_to_sent (chan, on_sent,
      NULL, NULL, NULL, NULL) != NULL, "");

  sent_count = 0;
  received_count = 0;
  tp_cli_channel_type_text_run_send (chan, -1,
      TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL, "Hello, world!",
      &error, NULL);
  MYASSERT_NO_ERROR (error);

  test_connection_run_until_dbus_queue_processed (conn);
  MYASSERT (sent_count == 1, ": %u != 1", sent_count);
  MYASSERT (received_count == 1, ": %u != 1", received_count);
  MYASSERT (last_sent_type == TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL,
      ": %u != NORMAL", last_sent_type);
  MYASSERT (!tp_strdiff (last_sent_text, "Hello, world!"),
      "'%s' != '%s'", last_sent_text, "Hello, world!");
  MYASSERT (last_received_type == TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL,
      ": %u != NORMAL", last_received_type);
  MYASSERT (last_received_flags == 0, ": %u != 0", last_received_flags);
  MYASSERT (last_received_sender == handle,
      ": %u != %u", last_received_sender, handle);
  MYASSERT (!tp_strdiff (last_received_text, "You said: Hello, world!"),
      "'%s'", last_received_text);

  sent_count = 0;
  received_count = 0;
  tp_cli_channel_type_text_run_send (chan, -1,
      TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION, "drinks coffee",
      &error, NULL);
  MYASSERT_NO_ERROR (error);

  test_connection_run_until_dbus_queue_processed (conn);
  MYASSERT (sent_count == 1, ": %u != 1", sent_count);
  MYASSERT (received_count == 1, ": %u != 1", received_count);
  MYASSERT (last_sent_type == TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION,
      ": %u != ACTION", last_sent_type);
  MYASSERT (!tp_strdiff (last_sent_text, "drinks coffee"),
      ": '%s' != '%s'", last_sent_text, "drinks coffee");
  MYASSERT (last_received_type == TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION,
      ": %u != ACTION", last_received_type);
  MYASSERT (last_received_flags == 0, ": %u != 0", last_received_flags);
  MYASSERT (last_received_sender == handle,
      ": %u != %u", last_received_sender, handle);
  MYASSERT (!tp_strdiff (last_received_text,
        "notices that the user drinks coffee"),
      ": '%s'", last_received_text);

  sent_count = 0;
  received_count = 0;
  tp_cli_channel_type_text_run_send (chan, -1,
      TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE, "Printer on fire",
      &error, NULL);
  MYASSERT_NO_ERROR (error);

  test_connection_run_until_dbus_queue_processed (conn);
  MYASSERT (sent_count == 1, ": %u != 1", sent_count);
  MYASSERT (received_count == 1, ": %u != 1", received_count);
  MYASSERT (last_sent_type == TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE,
      ": %u != NOTICE", last_sent_type);
  MYASSERT (!tp_strdiff (last_sent_text, "Printer on fire"),
      ": '%s' != '%s'", last_sent_text, "Printer on fire");
  MYASSERT (last_received_type == TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE,
      ": %u != NOTICE", last_received_type);
  MYASSERT (last_received_flags == 0, ": %u != 0", last_received_flags);
  MYASSERT (last_received_sender == handle,
      ": %u != %u", last_received_sender, handle);
  MYASSERT (!tp_strdiff (last_received_text,
        "You sent a notice: Printer on fire"),
      ": '%s'", last_received_text);

  g_print ("\n\n==== Listing messages ====\n");

    {
      GPtrArray *messages;

      tp_cli_channel_type_text_run_list_pending_messages (chan, -1,
          FALSE, &messages, &error, NULL);
      MYASSERT_NO_ERROR (error);

      g_print ("Freeing\n");
      g_boxed_free (TP_ARRAY_TYPE_PENDING_TEXT_MESSAGE_LIST, messages);
    }

  g_print ("\n\n==== Acknowledging messages using a wrong ID ====\n");

    {
      GArray *ids = g_array_sized_new (FALSE, FALSE, sizeof (guint), 2);
      /* we assume this ID won't be valid (implementation detail: message
       * IDs are increasing integers) */
      guint bad_id = 31337;

      g_array_append_val (ids, last_received_id);
      g_array_append_val (ids, bad_id);

      MYASSERT (
          !tp_cli_channel_type_text_run_acknowledge_pending_messages (chan, -1,
          ids, &error, NULL),
          "");
      MYASSERT (error != NULL, "");
      MYASSERT (error->domain == TP_ERRORS, "%s",
          g_quark_to_string (error->domain));
      MYASSERT (error->code == TP_ERROR_INVALID_ARGUMENT, "%u", error->code);
      g_error_free (error);
      error = NULL;

      g_array_free (ids, TRUE);

      /* The next test, "Acknowledging one message", will fail if the
       * last_received_id was acknowledged despite the error */
    }

  g_print ("\n\n==== Acknowledging one message ====\n");

    {
      GArray *ids = g_array_sized_new (FALSE, FALSE, sizeof (guint), 1);

      g_array_append_val (ids, last_received_id);

      tp_cli_channel_type_text_run_acknowledge_pending_messages (chan, -1,
          ids, &error, NULL);
      MYASSERT_NO_ERROR (error);

      g_array_free (ids, TRUE);
    }

  g_print ("\n\n==== Acknowledging all remaining messages using deprecated "
      "API ====\n");

    {
      GPtrArray *messages;

      tp_cli_channel_type_text_run_list_pending_messages (chan, -1,
          TRUE, &messages, &error, NULL);
      MYASSERT_NO_ERROR (error);

      g_print ("Freeing\n");
      g_boxed_free (TP_ARRAY_TYPE_PENDING_TEXT_MESSAGE_LIST, messages);
    }

  g_print ("\n\n==== Closing channel ====\n");

    {
      gboolean dead;

      MYASSERT (tp_cli_channel_run_close (chan, -1, &error, NULL), "");
      MYASSERT_NO_ERROR (error);
      MYASSERT (tp_proxy_get_invalidated (chan) != NULL, "");

      g_object_get (service_chan,
          "channel-destroyed", &dead,
          NULL);

      MYASSERT (dead, "");
    }

  g_print ("\n\n==== End of tests ====\n");

  MYASSERT (tp_cli_connection_run_disconnect (conn, -1, &error, NULL), "");
  MYASSERT_NO_ERROR (error);

  tp_handle_unref (contact_repo, handle);
  g_object_unref (chan);
  g_object_unref (conn);
  g_object_unref (service_chan);

  service_conn_as_base = NULL;
  g_object_unref (service_conn);
  g_object_unref (dbus);
  g_free (name);
  g_free (conn_path);
  g_free (chan_path);

  g_free (last_sent_text);
  g_free (last_received_text);

  return fail;
}
