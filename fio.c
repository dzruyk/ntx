/** 1
 *   fio - client side file I/O protocol
 * SYNOPSIS
 *   fio [-rwa] file
 * DESCRIPTION
 *   The fio utility implements client's side of legacy Telix file I/O protocol.
 *   This program opens the \fIfile\fP in read-only (\fB-r\fP), write-only (\fB-w\fP)
 *   or write-append (\fB-a\fP) mode and waits for a command line on its standard input.
 *   A command line is a sequence of bytes headed by a single character command identifier,
 *   terminated by an ASCII line feed character.
 *
 *   The communication protocol is very simple and most of the `on-wire'
 *   original Telix client file I/O protocol transparently travels between the
 *   server side and fio utility, running as a cooperating process of a
 *   client program. The simplicity of implementation will permit future changes
 *   within the file I/O protocol in a "throw-away-and-replace" manner without
 *   excessive rewrite of the code.
 *
 * BYTE STUFFING
 *   Original Telix file I/O protocol uses a specific byte stuffing method for
 *   binary strings, desribed in the following subsections.
 *  STUFFING
 *   Bytes with octal codes 000, 001, 012, 015 or 033 are replaced by
 *   two-byte sequence always headed by the byte with octal code 001 and
 *   followed by the byte with octal code 144+\fIc\fP, where \fIc\fP is a
 *   replaced byte. Thus a byte with octal code 012 is translated to octal
 *   two-byte sequence 001 156.
 *  UNSTUFFING
 *   Unstuffing is the reciprocal of stuffing. A two-byte sequence
 *   started by byte with octal code 001 and followed by byte with octal
 *   code 144, 145, 156, 161 or 177 is replaced by a single byte with
 *   octal code 000, 001, 012, 015 or 033 respectively. Thus a two-byte
 *   sequence 001 156 is replaced by a single byte with octal code 012.
 *
 *   This byte stuffing method ensures that ASCII control character \fC<lf>\fP
 *   (line feed) will never be present in the binary string and can be used
 *   as a command terminator symbol. The same rule applies to ASCII control
 *   characters \fC<nul>\fP, \fC<soh>\fP, \fC<cr>\fP and \fC<esc>\fP.
 * COMMANDS
 *   The following commands are implemented in the fio:
 *
 *   'w' str <lf>
 *                   Writes the binary string \fIstr\fP to the file as if it was
 *                   written by the write(2) system call.
 *                   The binary string is processed in the way described in UNSTUFFING
 *                   subsection before being written to the file.
 *                   It prints nothing to standard output.
 *
 *   'W' str <lf>
 *                   Writes a printable string \fIstr\fP to file as if it was
 *                   written by the write(2) system call.
 *                   The printable string is not processed and left intact.
 *                   It prints nothing to standard output.
 *
 *   'r' len <lf>
 *                   Reads a binary string of \fIlen\fP bytes from the file as if
 *                   it was read by the read(2) system call and prints
 *                   it to standard output. The binary string read is processed in
 *                   the way described in STUFFING subsection. Byte sequence
 *
 *                    '1' str <esc> <lf>
 *
 *                   is printed to standard output on success, where \fCstr\fP is
 *                   a sequence of read and stuffed bytes; on failure byte sequence
 *
 *                    '0' <esc> <lf>
 *
 *                   is printed to standard output.
 *   'R' len <lf>
 *                   Reads a printable string of \fIlen\fP bytes from file as if
 *                   it was read by the read(2) system call and prints it to
 *                   standard output. The string read is not processed and left
 *                   intact. Byte sequence
 *
 *                    '1' str <esc> <lf>
 *
 *                   is printed to standard output on success, where \fCstr\fP
 *                   is a sequence of read bytes; on failure byte sequence
 *
 *                    '0' <esc> <lf>
 *
 *                   is printed to standard output.
 * BUGS
 *   The file I/O model used by the Telix client protocol is broken by design.
 *
 * AUTHORS
 *   Grigoriy A. Sitkarev, <sitkarev@unixkomi.ru>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

#define BUFMAX      8192

enum
{
  NUL = 0x0,
  SOH = 0x1,
  CR  = 0x0d,
  LF  = 0x0a,
  ESC = 0x1b
};

static char *program_name;

static void
usage ()
{
  fprintf (stderr, "Usage: %s [-rwa] file\n", program_name);
}

int
main (int argc, char *argv[])
{
  char buf[BUFMAX+2];
  FILE *fp;
  int opt;
  char *s, *mode, *c;

  program_name = ((s = strrchr (argv[0], '/')) != NULL ? ++s : argv[0]);

  mode = "r";

  while ((opt = getopt (argc, argv, "rwa")) != -1)
    {
      switch (opt)
        {
        case 'r':
                  mode = "r";
          break;
        case 'w':
                  mode = "w";
          break;
        case 'a':
                  mode = "a";
          break;
        default:
          usage ();
          exit (1);
        }
    }

  argc -= optind;
  argv += optind;

  if (argv[0] == NULL)
    {
      usage ();
      exit (1);
    }

  c = argv[0];

  /* HACK: replace win bslashes with unix slashes. */
  while ((c = strchr(c, '\\')) != NULL)
        *c++ = '/';

  fp = fopen (argv[0], mode);
  if (fp == NULL)
    {
      fprintf (stderr, "can't open file `%s': %s\n", argv[0], strerror (errno));
      exit(2);
    }

  while (!feof (stdin) && !ferror (stdin) && fgets (buf, sizeof (buf), stdin) != NULL)
    {
      unsigned int n;

      n = strlen (buf);

      if (n < 2)
        {
          fprintf (stderr, "too short command sequence\n");
          continue;
        }

      if (buf[n-1] == LF)
        {
          buf[n-1] = '\0';
          n--;
        }
      else
        {
          fprintf (stderr, "no <lf> at end of line\n");
        }

      if (n >= 2)
        {
          char buf2[BUFMAX*2];
          size_t len, nbytes;
          char *p, *cp, *endptr, chr;

          chr = buf[0];

          switch (chr)
            {
            case 'r':
            case 'R':
              cp = buf + 1;
              errno = 0;
              len = strtoul (cp, &endptr, 10);
              if (((len == 0 || len == LONG_MAX) && errno != 0) ||
                  (*cp != '\0' && (endptr == cp || *endptr != '\0')))
                {
                  fprintf (stderr, "r: invalid read length\n");
                  continue;
                }
              if (len > BUFMAX)
                {
                  fprintf (stderr, "%c: can't read that much\n", chr);
                  len = BUFMAX;
                }

              nbytes = fread (buf, 1, len, fp);

              if (chr == 'r')
                {
                  char c;
                  int i;

                  p = buf2;

                  for (i = 0; i < nbytes; i++)
                    {
                      c = buf[i];

                      if (c == NUL || c == SOH || c == LF || c == CR || c == ESC)
                        {
                          *p++ = 0x1;
                          *p++ = c + 0x64;
                        }
                      else
                        *p++ = c;
                    }

                  nbytes = p - buf2;
                  p = buf2;
                }
              else
                {
                  p = buf;
                }

              /* Catch error or EOF condition on file. */
              if (nbytes == 0 || ferror (fp))
                chr = '0';
              else
                chr = '1';

              fputc (chr, stdout);
              fwrite (p, 1, nbytes, stdout);
              fputc (ESC, stdout);
              fputc (LF, stdout);

              if (ferror (stdout) || fflush (stdout) != 0)
                {
                  fprintf (stderr, "error writing out\n");
                  clearerr (stdout);
                }

              break;

            case 'w':
            case 'W':

              if (chr == 'w')
                {
                  char *c;

                  for (c = buf+1, p = buf; *c != '\0'; )
                    {
                      if (*c == 0x01 && c[1] != '\0')
                        {
                          *p++ = c[1] - 0x64;
                          c += 2;
                        }
                      else
                        {
                          *p++ = *c;
                          c += 1;
                        }
                    }

                  len = p - buf;
                  p = buf;
                }
              else
                {
                  p = buf + 1;
                  len = n - 1;
                }

              nbytes = fwrite (p, 1, len, fp);

              if ((nbytes < len && ferror (fp)) || fflush (fp) != 0)
                fprintf (stderr, "error writing file: %s\n", strerror (errno));

              break;

            default:
              fprintf(stderr, "unknown command 0x%02x\n", (unsigned char)*cp);
              break;
            }
        }
      else
        {
          fprintf (stderr, "too short command sequence\n");
        }
    }

  fclose (fp);

  exit (EXIT_SUCCESS);
}
