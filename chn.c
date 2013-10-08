#include <gtk/gtk.h>
#include <glib.h>

#include "chn.h"


ChannelCallbacks channel_callbacks;
ChannelFuncs channel_funcs;

static ChannelFuncs *vfuncs = &channel_funcs;

const gchar *
chn_get_name ()
{
  if (vfuncs->get_name != NULL)
    return (*vfuncs->get_name) ();
  else
    return NULL;
}

gsize
chn_write (const void *buf, gsize len)
{
  if (vfuncs->write != NULL)
    return (*vfuncs->write) (buf, len);
  else
    return 0;
}

gsize
chn_prepend (const void *buf, gsize len)
{
  if (vfuncs->prepend != NULL)
    return (*vfuncs->prepend) (buf, len);
  else
    return 0;
}

gboolean
chn_connect ()
{
  if (vfuncs->connect != NULL)
    return (*vfuncs->connect) ();
  else
    return 0;
}

void
chn_disconnect ()
{
  if (vfuncs->disconnect != NULL)
    (*vfuncs->disconnect) ();
}

void
chn_finalize ()
{
  if (vfuncs->finalize != NULL)
    (*vfuncs->finalize) ();
}

gboolean
chn_is_connected ()
{
  if (vfuncs->is_connected != NULL)
    return (*vfuncs->is_connected) ();
  else
    return TRUE;
}

void
chn_set_callbacks (const ChannelCallbacks *callbacks)
{
  if (callbacks != NULL)
    {
      channel_callbacks.disconnect = callbacks->disconnect;
      channel_callbacks.error = callbacks->error;
      channel_callbacks.input = callbacks->input;
      channel_callbacks.user_data = callbacks->user_data;
    }
  else
    {
      channel_callbacks.disconnect = NULL;
      channel_callbacks.error = NULL;
      channel_callbacks.input = NULL;
      channel_callbacks.user_data = NULL;
    }
}

void
chn_get_callbacks (ChannelCallbacks *callbacks)
{
  if (callbacks != NULL)
    {
      callbacks->disconnect = channel_callbacks.disconnect;
      callbacks->error = channel_callbacks.error;
      callbacks->input = channel_callbacks.input;
      callbacks->user_data = channel_callbacks.user_data;
    }
}

