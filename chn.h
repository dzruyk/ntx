#ifndef __CHN_H__
#define __CHN_H__


typedef struct _ChannelCallbacks ChannelCallbacks;
typedef struct _ChannelFuncs ChannelFuncs;

struct _ChannelFuncs {
  const gchar * (*get_name)     ();
  gsize         (*write)        (const void *buf, gsize len);
  gsize         (*prepend)      (const void *buf, gsize len);
  gboolean      (*connect)      ();
  void          (*disconnect)   ();
  void          (*finalize)     ();
  gboolean      (*is_connected) ();
};

struct _ChannelCallbacks {
  void      (*error)      (const GError *err, gpointer user_data);
  void      (*disconnect) (const GError *err, gpointer user_data);
  void      (*input)      (guchar *buf, gsize len, gpointer user_data);
  gpointer  user_data;
};

gint         chn_telnet_init   (const gchar *host, gint port);
gint         chn_pty_init      (const gchar *cmdline);
gint         chn_echo_init     ();

const gchar* chn_get_name      ();
gsize        chn_write         (const void *buf, gsize len);
gsize        chn_prepend       (const void *buf, gsize len);
gboolean     chn_connect       ();
void         chn_disconnect    ();
void         chn_finalize      ();
gboolean     chn_is_connected  ();
void         chn_set_callbacks (const ChannelCallbacks *callbacks);
void         chn_get_callbacks (ChannelCallbacks *callbacks);


#endif /* __CHN_H__ */
