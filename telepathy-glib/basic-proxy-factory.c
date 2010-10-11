/*
 * Simple client channel factory creating TpChannel
 *
 * Copyright © 2010 Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

/**
 * SECTION:basic-proxy-factory
 * @title: TpBasicProxyFactory
 * @short_description: channel factory creating TpChannel objects
 * @see_also: #TpAutomaticProxyFactory
 *
 * This factory implements the #TpClientChannelFactory interface to create
 * plain #TpChannel objects. Unlike #TpAutomaticProxyFactory, it will
 * not create higher-level subclasses like #TpStreamTubeChannel.
 *
 * TpProxy subclasses other than TpChannel are not currently supported.
 */

/**
 * TpBasicProxyFactory:
 *
 * Data structure representing a #TpBasicProxyFactory
 *
 * Since: 0.13.UNRELEASED
 */

/**
 * TpBasicProxyFactoryClass:
 * @parent_class: the parent class
 *
 * The class of a #TpBasicProxyFactory.
 *
 * Since: 0.13.UNRELEASED
 */

#include "telepathy-glib/basic-proxy-factory.h"

#include <telepathy-glib/client-channel-factory.h>

#define DEBUG_FLAG TP_DEBUG_CLIENT
#include "telepathy-glib/debug-internal.h"

static void client_channel_factory_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE(TpBasicProxyFactory, tp_basic_proxy_factory, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CLIENT_CHANNEL_FACTORY,
      client_channel_factory_iface_init))

static void
tp_basic_proxy_factory_init (TpBasicProxyFactory *self)
{
}

static void
tp_basic_proxy_factory_class_init (TpBasicProxyFactoryClass *cls)
{
}

static void
client_channel_factory_iface_init (gpointer g_iface,
    gpointer unused G_GNUC_UNUSED)
{
  TpClientChannelFactoryInterface *iface = g_iface;

  /* We rely on the default implementation of create_channel */
  iface->create_channel = NULL;
}

/**
 * tp_basic_proxy_factory_new:
 *
 * Convenient function to create a new #TpBasicProxyFactory instance.
 *
 * Returns: a new #TpBasicProxyFactory
 *
 * Since: 0.13.UNRELEASED
 */
TpBasicProxyFactory *
tp_basic_proxy_factory_new (void)
{
  return g_object_new (TP_TYPE_BASIC_PROXY_FACTORY,
      NULL);
}
