#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <pwd.h>
#include <unistd.h>

#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <glib.h>

#include "os.h"

#define DEVNULL    "/dev/null"

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
          pw = getpwuid (getuid ());
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

/*
 * We hope this fork/exec wrapper more tiny than g_spawn_async_with_pipes
 */
gboolean
os_process_spawn_with_pipes(char **argv,
                            gint *input,
                            gint *output,
                            GPid *child_pid)
{
  gint i, rc, nullfd;
  pid_t pid;
  struct {
      gint fdread;
      gint fdwrite;
  } fdpair[2] = { { -1, -1 }, { -1, -1 } };

  nullfd = open (DEVNULL, O_WRONLY | O_NOCTTY);
  if (nullfd == -1)
    goto err_nullfd;

  g_assert (nullfd >= 3);
  g_assert (argv != NULL && argv[0] != NULL);

  for (i = 0; i < 2; i++) {
      rc = pipe ((void *)&fdpair[i]);
      if (rc == -1)
        goto err_pipe;
  }

  pid = fork ();

  if (pid == -1)
    goto err_fork;

  if (pid == 0)
    {
      struct rlimit rlim;
      gint fd, nofile;

      close (0);
      dup (fdpair[1].fdread);
      close (1);
      dup (fdpair[0].fdwrite);
      close (2);
      dup (nullfd);

      rc = getrlimit (RLIMIT_NOFILE, &rlim);
      if (rc == -1 || rlim.rlim_max == 0)
        nofile = 1024;
      else
        nofile = rlim.rlim_max;

      for (fd = 3; fd < nofile; fd++)
        close (fd);

      execvp (argv[0], argv);

      _exit (1);
    }
  else
    {
      *child_pid = pid;
      *input = fdpair[1].fdwrite;
      *output = fdpair[0].fdread;

      close (fdpair[0].fdwrite);
      close (fdpair[1].fdread);
      close (nullfd);
    }

  return TRUE;

err_fork:
  for (i = 0; i < 2; i++)
    {
      if (fdpair[i].fdread >= 0)
        close (fdpair[i].fdread);
      if (fdpair[i].fdwrite >= 0)
        close (fdpair[i].fdwrite);
    }
err_pipe:
  close (nullfd);
err_nullfd:

  return FALSE;
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

