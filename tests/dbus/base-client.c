/* Tests of TpBaseClient
 *
 * Copyright (C) 2010 Collabora Ltd. <http://www.collabora.co.uk/>
 *
 * Copying and distribution of this file, with or without modification,
 * are permitted in any medium without royalty provided the copyright
 * notice and this notice are preserved.
 */

#include <telepathy-glib/base-client.h>
#include <telepathy-glib/client.h>
#include <telepathy-glib/debug.h>
#include <telepathy-glib/defs.h>

#include "tests/lib/util.h"

typedef struct {
    GMainLoop *mainloop;
    TpDBusDaemon *dbus;

    TpBaseClient *base_client;
    /* client side object */
    TpClient *client;

    GError *error /* initialized where needed */;
    GStrv interfaces;
} Test;

static void
setup (Test *test,
       gconstpointer data)
{
  test->mainloop = g_main_loop_new (NULL, FALSE);
  test->dbus = test_dbus_daemon_dup_or_die ();
  g_assert (test->dbus != NULL);

  test->base_client = tp_base_client_new (test->dbus, "Test", FALSE);
  g_assert (test->base_client);

  test->client = g_object_new (TP_TYPE_CLIENT,
          "dbus-daemon", test->dbus,
          "dbus-connection", ((TpProxy *) test->dbus)->dbus_connection,
          "bus-name", "org.freedesktop.Telepathy.Client.Test",
          "object-path", "/org/freedesktop/Telepathy/Client/Test",
          NULL);

  g_assert (test->client != NULL);

  test->error = NULL;
  test->interfaces = NULL;
}

static void
teardown (Test *test,
          gconstpointer data)
{
  if (test->base_client != NULL)
    {
      g_object_unref (test->base_client);
      test->base_client = NULL;
    }

  if (test->client != NULL)
    {
      g_object_unref (test->client);
      test->client = NULL;
    }

  g_object_unref (test->dbus);
  test->dbus = NULL;
  g_main_loop_unref (test->mainloop);
  test->mainloop = NULL;

  if (test->interfaces != NULL)
    {
      g_strfreev (test->interfaces);
      test->interfaces = NULL;
    }
}

/* Test Basis */

static void
test_basis (Test *test,
    gconstpointer data G_GNUC_UNUSED)
{
  TpDBusDaemon *dbus;
  gchar *name;
  gboolean unique;

  g_object_get (test->base_client,
      "dbus-daemon", &dbus,
      "name", &name,
      "uniquify-name", &unique,
      NULL);

  g_assert (test->dbus == dbus);
  g_assert_cmpstr ("Test", ==, name);
  g_assert (!unique);

  g_object_unref (dbus);
  g_free (name);
}

/* Test register */

static void
get_client_prop_cb (TpProxy *proxy,
    GHashTable *properties,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  Test *test = user_data;

  if (error != NULL)
    {
      test->error = g_error_copy (error);
      goto out;
    }

  g_assert_cmpint (g_hash_table_size (properties), == , 1);

  test->interfaces = g_strdupv ((GStrv) tp_asv_get_strv (
        properties, "Interfaces"));

out:
  g_main_loop_quit (test->mainloop);
}

static void
test_register (Test *test,
    gconstpointer data G_GNUC_UNUSED)
{
  tp_base_client_be_a_handler (test->base_client);

  /* Client is not registered yet */
  tp_cli_dbus_properties_call_get_all (test->client, -1,
      TP_IFACE_CLIENT, get_client_prop_cb, test, NULL, NULL);
  g_main_loop_run (test->mainloop);

  g_assert_error (test->error, DBUS_GERROR, DBUS_GERROR_SERVICE_UNKNOWN);
  g_error_free (test->error);
  test->error = NULL;

  tp_base_client_register (test->base_client);

  tp_cli_dbus_properties_call_get_all (test->client, -1,
      TP_IFACE_CLIENT, get_client_prop_cb, test, NULL, NULL);
  g_main_loop_run (test->mainloop);

  g_assert_no_error (test->error);
}

/* Test Observer */
static void
check_filters (GPtrArray *filters)
{
  GHashTable *filter;

  g_assert (filters != NULL);
  g_assert_cmpuint (filters->len, ==, 2);

  filter = g_ptr_array_index (filters, 0);
  g_assert_cmpuint (g_hash_table_size (filter), ==, 1);
  g_assert_cmpstr (tp_asv_get_string (filter, TP_PROP_CHANNEL_CHANNEL_TYPE), ==,
      TP_IFACE_CHANNEL_TYPE_TEXT);

  filter = g_ptr_array_index (filters, 1);
  g_assert_cmpuint (g_hash_table_size (filter), ==, 2);
  g_assert_cmpstr (tp_asv_get_string (filter, TP_PROP_CHANNEL_CHANNEL_TYPE), ==,
      TP_IFACE_CHANNEL_TYPE_STREAM_TUBE);
  g_assert_cmpuint (tp_asv_get_uint32 (filter,
        TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, NULL), ==, TP_HANDLE_TYPE_CONTACT);
}

static void
get_observer_prop_cb (TpProxy *proxy,
    GHashTable *properties,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  Test *test = user_data;
  GPtrArray *filters;
  gboolean recover;
  gboolean valid;

  if (error != NULL)
    {
      test->error = g_error_copy (error);
      goto out;
    }

  g_assert_cmpint (g_hash_table_size (properties), == , 2);

  filters = tp_asv_get_boxed (properties, "ObserverChannelFilter",
      TP_ARRAY_TYPE_CHANNEL_CLASS_LIST);
  check_filters (filters);

  recover = tp_asv_get_boolean (properties, "Recover", &valid);
  g_assert (valid);
  g_assert (recover);

out:
  g_main_loop_quit (test->mainloop);
}

static void
test_observer (Test *test,
    gconstpointer data G_GNUC_UNUSED)
{
  GHashTable *filter;

  filter = tp_asv_new (
      TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING, TP_IFACE_CHANNEL_TYPE_TEXT,
      NULL);

  tp_base_client_add_observer_filter (test->base_client, filter);
  g_hash_table_unref (filter);

  tp_base_client_take_observer_filter (test->base_client, tp_asv_new (
        TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING,
          TP_IFACE_CHANNEL_TYPE_STREAM_TUBE,
        TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT,
          TP_HANDLE_TYPE_CONTACT,
        NULL));

  tp_base_client_set_observer_recover (test->base_client, TRUE);

  tp_base_client_register (test->base_client);

  /* Check Client properties */
  tp_cli_dbus_properties_call_get_all (test->client, -1,
      TP_IFACE_CLIENT, get_client_prop_cb, test, NULL, NULL);
  g_main_loop_run (test->mainloop);

  g_assert_no_error (test->error);
  g_assert_cmpint (g_strv_length (test->interfaces), ==, 1);
  g_assert (tp_strv_contains ((const gchar * const *) test->interfaces,
        TP_IFACE_CLIENT_OBSERVER));

  /* Check Observer properties */
  tp_cli_dbus_properties_call_get_all (test->client, -1,
      TP_IFACE_CLIENT_OBSERVER, get_observer_prop_cb, test, NULL, NULL);
  g_main_loop_run (test->mainloop);

  g_assert_no_error (test->error);
}

/* Test Approver */
static void
get_approver_prop_cb (TpProxy *proxy,
    GHashTable *properties,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  Test *test = user_data;
  GPtrArray *filters;

  if (error != NULL)
    {
      test->error = g_error_copy (error);
      goto out;
    }

  g_assert_cmpint (g_hash_table_size (properties), == , 1);

  filters = tp_asv_get_boxed (properties, "ApproverChannelFilter",
      TP_ARRAY_TYPE_CHANNEL_CLASS_LIST);
  check_filters (filters);

out:
  g_main_loop_quit (test->mainloop);
}

static void
test_approver (Test *test,
    gconstpointer data G_GNUC_UNUSED)
{
  GHashTable *filter;

  filter = tp_asv_new (
      TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING, TP_IFACE_CHANNEL_TYPE_TEXT,
      NULL);

  tp_base_client_add_approver_filter (test->base_client, filter);
  g_hash_table_unref (filter);

  tp_base_client_take_approver_filter (test->base_client, tp_asv_new (
        TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING,
          TP_IFACE_CHANNEL_TYPE_STREAM_TUBE,
        TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT,
          TP_HANDLE_TYPE_CONTACT,
        NULL));

  tp_base_client_register (test->base_client);

  /* Check Client properties */
  tp_cli_dbus_properties_call_get_all (test->client, -1,
      TP_IFACE_CLIENT, get_client_prop_cb, test, NULL, NULL);
  g_main_loop_run (test->mainloop);

  g_assert_no_error (test->error);
  g_assert_cmpint (g_strv_length (test->interfaces), ==, 1);
  g_assert (tp_strv_contains ((const gchar * const *) test->interfaces,
        TP_IFACE_CLIENT_APPROVER));

  /* Check Approver properties */
  tp_cli_dbus_properties_call_get_all (test->client, -1,
      TP_IFACE_CLIENT_APPROVER, get_approver_prop_cb, test, NULL, NULL);
  g_main_loop_run (test->mainloop);

  g_assert_no_error (test->error);
}

/* Test Handler */
static void
get_handler_prop_cb (TpProxy *proxy,
    GHashTable *properties,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  Test *test = user_data;
  GPtrArray *filters;
  gboolean bypass;
  gboolean valid;
  const gchar * const * capabilities;
  GPtrArray *handled;

  if (error != NULL)
    {
      test->error = g_error_copy (error);
      goto out;
    }

  g_assert_cmpint (g_hash_table_size (properties), == , 4);

  filters = tp_asv_get_boxed (properties, "HandlerChannelFilter",
      TP_ARRAY_TYPE_CHANNEL_CLASS_LIST);
  check_filters (filters);

  bypass = tp_asv_get_boolean (properties, "BypassApproval", &valid);
  g_assert (valid);
  g_assert (bypass);

  capabilities = tp_asv_get_strv (properties, "Capabilities");
  g_assert_cmpint (g_strv_length ((GStrv) capabilities), ==, 5);
  g_assert (tp_strv_contains (capabilities, "badger"));
  g_assert (tp_strv_contains (capabilities, "mushroom"));
  g_assert (tp_strv_contains (capabilities, "snake"));
  g_assert (tp_strv_contains (capabilities, "goat"));
  g_assert (tp_strv_contains (capabilities, "pony"));

  handled = tp_asv_get_boxed (properties, "HandledChannels",
      TP_ARRAY_TYPE_OBJECT_PATH_LIST);
  g_assert (handled != NULL);
  g_assert_cmpint (handled->len, ==, 0);

out:
  g_main_loop_quit (test->mainloop);
}

static void
test_handler (Test *test,
    gconstpointer data G_GNUC_UNUSED)
{
  GHashTable *filter;
  const gchar *caps[] = { "mushroom", "snake", NULL };

  filter = tp_asv_new (
      TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING, TP_IFACE_CHANNEL_TYPE_TEXT,
      NULL);

  tp_base_client_add_handler_filter (test->base_client, filter);
  g_hash_table_unref (filter);

  tp_base_client_take_handler_filter (test->base_client, tp_asv_new (
        TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING,
          TP_IFACE_CHANNEL_TYPE_STREAM_TUBE,
        TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT,
          TP_HANDLE_TYPE_CONTACT,
        NULL));

  tp_base_client_set_handler_bypass_approval (test->base_client, TRUE);

  tp_base_client_add_handler_capability (test->base_client, "badger");
  tp_base_client_add_handler_capabilities (test->base_client, caps);
  tp_base_client_add_handler_capabilities_varargs (test->base_client,
      "goat", "pony", NULL);

  tp_base_client_register (test->base_client);

  /* Check Client properties */
  tp_cli_dbus_properties_call_get_all (test->client, -1,
      TP_IFACE_CLIENT, get_client_prop_cb, test, NULL, NULL);
  g_main_loop_run (test->mainloop);

  g_assert_no_error (test->error);
  g_assert_cmpint (g_strv_length (test->interfaces), ==, 1);
  g_assert (tp_strv_contains ((const gchar * const *) test->interfaces,
        TP_IFACE_CLIENT_HANDLER));

  /* Check Handler properties */
  tp_cli_dbus_properties_call_get_all (test->client, -1,
      TP_IFACE_CLIENT_HANDLER, get_handler_prop_cb, test, NULL, NULL);
  g_main_loop_run (test->mainloop);

  g_assert_no_error (test->error);
}

int
main (int argc,
      char **argv)
{
  g_type_init ();
  tp_debug_set_flags ("all");

  g_test_init (&argc, &argv, NULL);
  g_test_bug_base ("http://bugs.freedesktop.org/show_bug.cgi?id=");

  g_test_add ("/base-client/basis", Test, NULL, setup, test_basis, teardown);
  g_test_add ("/base-client/register", Test, NULL, setup, test_register,
      teardown);
  g_test_add ("/base-client/observer", Test, NULL, setup, test_observer,
      teardown);
  g_test_add ("/base-client/approver", Test, NULL, setup, test_approver,
      teardown);
  g_test_add ("/base-client/handler", Test, NULL, setup, test_handler,
      teardown);

  return g_test_run ();
}
