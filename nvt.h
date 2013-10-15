#ifndef __NVT_H__
#define __NVT_H__


/*
 * Network Virtual Terminal (TELNET) I/O protocol.
 */

enum
{
  NUL   = 0,
  SE    = 240,
  NOP   = 241,
  SB    = 250,
  WILL  = 251,
  WONT  = 252,
  DO    = 253,
  DONT  = 254,
  IAC   = 255
};

#define NVT_CALLBACKS(name) ((nvt_callbacks())->name)


typedef struct _NvtCallbacks
{
  /* NVT bytes received from the remote side. */
  void (*input_bytes)    (guchar *bytes, gint len, gpointer user_data);

  /* Telnet command with opcode (opcode is set to -1 if not applicable). */
  void (*command)        (gint command, gint opcode, gpointer user_data);

  /* Subnegotiated option agruments sent by the remote side. */
  void (*subnegotiation) (gint opcode, const guchar *arg, gint len, gpointer user_data);

  /* Notify user that the connection was established. */
  void (*connected)      (gpointer user_data);

  /* Disconnect notification. error can be NULL. */
  void (*disconnect)     (const GError *error, gpointer user_data);

  /* Error notification. error can be NULL. */
  void (*error)          (const GError *error, gpointer user_data);

  /* Arbitrary pointer to user data passed to every callback as a last argument. */
  gpointer user_data;

} NvtCallbacks;


void           nvt_init       ();
void           nvt_finalize   ();
gboolean       nvt_connect    (const gchar *host, gshort port);
void           nvt_disconnect ();
void           nvt_do         (gint opcode);
void           nvt_dont       (gint opcode);
void           nvt_will       (gint opcode);
void           nvt_wont       (gint opcode);
void           nvt_subneg     (gint cmd, guchar *arg, gsize len);
gsize          nvt_write      (const void *buf, gsize len);
gsize          nvt_prepend    (const void *buf, gsize len);
gboolean       nvt_is_connected ();
NvtCallbacks * nvt_callbacks  ();

#endif /* __NVT_H__ */

