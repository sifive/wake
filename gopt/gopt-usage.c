/* gopt-usage.c   PUBILC DOMAIN 2015   t.gopt@purposeful.co.uk */

/* <http:///www.purposeful.co.uk/software/gopt> */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "gopt.h"

int main (int argc, char **argv)
{
  struct option options[5];

  options[0].long_name  = "help";
  options[0].short_name = 'h';
  options[0].flags      = GOPT_ARGUMENT_FORBIDDEN;

  options[1].long_name  = "version";
  options[1].short_name = 'V';
  options[1].flags      = GOPT_ARGUMENT_FORBIDDEN;

  options[2].long_name  = "verbose";
  options[2].short_name = 'v';
  options[2].flags      = GOPT_ARGUMENT_FORBIDDEN;

  options[3].long_name  = "output";
  options[3].short_name = 'o';
  options[3].flags      = GOPT_ARGUMENT_REQUIRED;

  options[4].flags      = GOPT_LAST;

  argc = gopt (argv, options);

  gopt_errors (argv[0], options);

  FILE *fout;
  int   i;

  if (options[0].count)
  {
    fprintf (stdout, "see the manual\n");
    exit (EXIT_SUCCESS);
  }

  if (options[1].count)
  {
    fprintf (stdout, "version 1.0\n");
    exit (EXIT_SUCCESS);
  }

  if (options[2].count >= 1)
  {
    fputs ("being verbose\n", stderr);
  }

  if (options[2].count >= 2)
  {
    fputs ("being very verbose\n", stderr);
  }

  if (options[3].count)
  {
    fout = fopen (options[3].argument, "w");

    if (!fout)
    {
      perror (options[3].argument);
      exit (EXIT_FAILURE);
    }
  }
  else
  {
    fout = stdout;
  }

  for (i = 0; i < argc; i++)
  {
    fputs (argv[i], fout);
    fputs ("\n",    fout);
  }

  return 0;
}
