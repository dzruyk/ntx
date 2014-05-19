#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include <glib.h>

#include "os.h"

gchar*
os_get_temporary_directory (gchar *buf, gsize bufsz)
{
  gchar dirname[PATH_MAX];
  struct stat st;
  gchar *tmp;

  if ((tmp = getenv ("TMP")) == NULL && (tmp = getenv ("TEMP")) == NULL)
    {
      const gchar *list[] = { "tmp", "temp", ".tmp", NULL };
      struct passwd *pw;
      const gchar **p;

      tmp = NULL;

      do
        {
          errno = 0;
          pw = getpwuid (getuid());
        }
      while (pw == NULL && errno == EINTR);

      for (p = list; *p != NULL; p++)
        {
          snprintf (dirname, sizeof (dirname), "/home/%s/%s", pw->pw_name, *p);
          if (stat (dirname, &st) != -1 && S_ISDIR (st.st_mode))
            {
              tmp = dirname;
              break;
            }
        }

      if (tmp == NULL)
        {
          g_strlcpy (dirname, OS_DEFAULT_TMP_DIR, sizeof (dirname));
          tmp = dirname;
        }
    }

  g_strlcpy (buf, tmp, bufsz);
  return buf;
}

gboolean
os_process_is_exited (GPid pid, gint status)
{
  return WIFEXITED (status);
}

gint
os_process_get_exit_status (GPid pid, gint status)
{
  gint code;

  code = WEXITSTATUS (status);
  g_assert (code >= 0);

  return code;
}

gboolean
os_process_is_signaled (GPid pid, gint status)
{
  return WIFSIGNALED (status);
}

gint
os_process_get_signal (GPid pid, gint status)
{
  gint code;

  code = WTERMSIG (status);
  g_assert (code >= 256);

  return code;
}

