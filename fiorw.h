#ifndef __FIORW_H__
#define __FIORW_H__

/** 3
 *   FIOCallbacks - fio callback functions structure
 * DESCRIPTION
 *   This structure contains the following fields:
 *
 *   user_data
 *                  Arbitrary pointer passed to each callback function.
 *   read_data
 *                  This is set to an address of a user function which will be
 *                  called each time data received from fio(1) coprocess.
 *   kick_writer
 *                  This is set to an address of a user function which will be
 *                  called each time write buffer space increases. User can call
 *                  fio_write_buffer_space() to get idea how much data he can
 *                  safely write.
 *   io_error
 *                  This is set to an address of a user function which will be
 *                  called when I/O error signaled on the channel or fio(1)
 *                  coprocess closed its pipe end.
 *   coproc_exited
 *                  This is set to an address of a user function which will be
 *                  called when coprocess exited. The exit code or signal number
 *                  is passed in \fIcode\fP. If the coprocess was terminated by
 *                  a signal its number is encoded as signo+255.
 *
 */
typedef struct _FIOCallbacks
{
  /* Arbitrary pointer passed to each callback function. */
  gpointer user_data;

  /* Receives data read by companion process. */
  void (*read_data)     (guchar *buffer, gsize len, gpointer user_data);

  /* Invoked when write buffer is ready to accept more data. */
  void (*kick_writer)   (gpointer user_data);

  /* Invoked when I/O error signaled or pipe end closed. */
  void (*io_error)      (gboolean hangup, gpointer user_data);

  /* Invoked when child coprocess exits. */
  void (*coproc_exited) (gint pid, int code, gpointer user_data);

} FIOCallbacks;


gboolean fio_open_readonly      (const gchar *filename);
gboolean fio_open_writeonly     (const gchar *filename);
gboolean fio_open_append        (const gchar *filename);
void     fio_close              ();
gssize   fio_write              (const void *buf, gsize len);
gsize    fio_write_buffer_space ();
void     fio_set_callbacks      (const FIOCallbacks *cbs, FIOCallbacks *old);


#endif /* __FIORW_H__ */
