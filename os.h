#ifndef __OS_H__
#define __OS_H__

/* default temporary directory */

#ifdef __unix__
#  define os_file_set_binary_mode(file)
#  define OS_DEFAULT_TMP_DIR            "/tmp"
#  define FIOPROG                       "./fio"
#  define os_g_io_channel_fd_new        g_io_channel_unix_new
#  define os_g_io_channel_get_fd        g_io_channel_unix_get_fd
#  define os_g_io_channel_sock_new      g_io_channel_unix_new
#else
#  define os_file_set_binary_mode(file) setmode (fileno (file), _O_BINARY)
#  define OS_DEFAULT_TMP_DIR            "C:\\"
#  define FIOPROG                       "./fio.exe"
#  define os_g_io_channel_get_fd        g_io_channel_win32_get_fd
#  define os_g_io_channel_fd_new        g_io_channel_win32_new_fd
#  define os_g_io_channel_sock_new      g_io_channel_win32_new_socket
#endif


gchar*   os_get_temporary_directory (gchar *buf, gsize bufsz);

gboolean os_process_is_exited       (GPid pid, gint status);

gint     os_process_get_exit_status (GPid pid, gint status);

gboolean os_process_is_signaled     (GPid pid, gint status);

gint     os_process_get_signal      (GPid pid, gint status);

#endif

