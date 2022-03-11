/* gopt.c   PUBILC DOMAIN 2015   t.gopt@purposeful.co.uk */

/* <http:///www.purposeful.co.uk/software/gopt> */

/*
  I, Tom Vajzovic, am the author of this software and its documentation.
  I permanently abandon all intellectual property rights in them, including
  copyright, trademarks, design rights, database right, patents, and the right
  to be identified as the author.

  I am fairly certain that the software does what the documentation says it
  does, but I do not guarantee that it does, or that it does what you think it
  should.  I do not guarantee that it will not have undesirable side effects.

  You are free to use, modify and distribute this software as you please, but
  you do so at your own risk.  If you do not pass on this warning then you may
  be responsible for any problems encountered by those who obtain the software
  through you.
*/

#include <string.h>

#ifdef _WIN32
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif

#include "gopt.h"


/* returns index of first exact match or only prefix match, or last index */
static unsigned int long_option_get_index (const char *arg, const struct option *options)
{
  unsigned int count = 0;
  unsigned int found = 0;
  unsigned int i;

  size_t arg_len = strcspn (arg, "=");

  for (i = 0; !(options[i].flags & GOPT_LAST); i++)
  {
    if (options[i].long_name)
    {
      size_t full_len = strlen (options[i].long_name);

      if ((arg_len <= full_len) && !strncasecmp (options[i].long_name, arg, arg_len))
      {
        if (arg_len == full_len)
        {
          return i;
        }

        found = i;
        count++;
      }
    }
  }

  return ((count == 1) ? found : i);
}

/* returns index of first match, or last index */
static unsigned int short_option_get_index (char c, const struct option *options)
{
  unsigned int i;

  for (i = 0; !(options[i].flags & GOPT_LAST); i++)
  {
    if (options[i].short_name == c)
    {
      return i;
    }
  }

  return i;
}

void add_argument_value(struct option options[], int option_index, char* value) {
  int arg_index = options[option_index].count - 1;

  if (options[option_index].flags & GOPT_REPEATABLE_VALUE) {
    if (arg_index < (int)options[option_index].max_args) {
      options[option_index].arguments[arg_index] = value;
    } else {
      options[option_index].flags |= GOPT_BUFFER_FULL;
    }
  } else {
    options[option_index].argument = value;
  }
}

int gopt (char **argv, struct option *options)
{
  unsigned int operand_count = 1;
  unsigned int doubledash    = 0;
  unsigned int expecting     = 0;
  unsigned int option_index  = 0;
  unsigned int i, j; // i is the current token, j is a character index in the token.

  // Initial output-only values in option array.
  for (i = 0; !(options[i].flags & GOPT_LAST); i++)
  {
    options[i].argument = NULL;
    options[i].count    = 0;
  }

  // End of option array marker.
  options[i].short_name = 0;
  options[i].long_name  = NULL;
  options[i].argument   = NULL;
  options[i].count      = 0;

  // Ignore the 'process name' argument in position 0.
  for (i = 1; argv[i]; i++)
  {
    // If a '--' was seen earlier, stop parsing arguments.
    if (doubledash)
    {
      argv[operand_count] = argv[i];
      operand_count++;
      continue;
    }

    if (expecting)
    {
      if ((argv[i][0] != '-') || !argv[i][1] || !(options[option_index].flags & GOPT_ARGUMENT_NO_HYPHEN))
      {
        options[option_index].flags |= expecting;
        add_argument_value(options, option_index, argv[i]);
        expecting = 0;
        continue;
      }
      else
      {
        options[option_index].flags    |= (expecting >> 1); /* change _WITH to _WITHOUT */
        options[option_index].argument  =  NULL;
        expecting = 0;
      }
    }

    if ((argv[i][0] == '-') && (argv[i][1] == '-') && (argv[i][2] == 0))
    {
      doubledash = 1;
      continue;
    }

    if ((argv[i][0] == '-') && (argv[i][1] == '-'))
    {
      char *eq = strchr (&argv[i][2], '=');

      option_index = long_option_get_index (&argv[i][2], options);

      options[option_index].count++;

      if ((options[option_index].flags & GOPT_LAST) && !options[option_index].long_name)
      {
        options[option_index].long_name = &argv[i][2];
      }

      if (eq)
      {
        options[option_index].argument  = (eq + 1);
        options[option_index].flags    |= GOPT_SEEN_LONG_WITH;
      }
      else if (options[option_index].flags & GOPT_ARGUMENT_REQUIRED)
      {
        expecting = GOPT_SEEN_LONG_WITH;
      }
      else
      {
        options[option_index].argument  = NULL;
        options[option_index].flags    |= GOPT_SEEN_LONG_WITHOUT;
      }
    }
    else if ((argv[i][0] == '-') && argv[i][1])
    {
      for (j = 1; argv[i][j]; j++)
      {
        option_index = short_option_get_index (argv[i][j], options);

        options[option_index].count++;

        if (options[option_index].flags & GOPT_LAST)
        {
          if (!options[option_index].short_name)
          {
            options[option_index].short_name = argv[i][j];
          }

          if (argv[i][j+1])
          {
            char* arg_value = &argv[i][j+1];
            add_argument_value(options, option_index, arg_value);
            options[option_index].flags |= GOPT_SEEN_SHORT_WITH;
          }
          else
          {
            options[option_index].argument  = NULL;
            options[option_index].flags    |= GOPT_SEEN_SHORT_WITHOUT;
          }
          break;
        }

        if (options[option_index].flags & GOPT_ARGUMENT_FORBIDDEN)
        {
          options[option_index].argument  = NULL;
          options[option_index].flags    |= GOPT_SEEN_SHORT_WITHOUT;
        }

        else if (argv[i][j+1])
        {
          char* arg_value = &argv[i][j+1];
          add_argument_value(options, option_index, arg_value);
          options[option_index].flags |= GOPT_SEEN_SHORT_WITH;
          break;
        }

        else if (options[option_index].flags & GOPT_ARGUMENT_REQUIRED)
        {
          expecting = GOPT_SEEN_SHORT_WITH;
        }

        else
        {
          options[option_index].argument  = NULL;
          options[option_index].flags    |= GOPT_SEEN_SHORT_WITHOUT;
        }
      }
    }
    else
    {
      doubledash = 1; // added for wake to pass options after target to target commands
      argv[operand_count] = argv[i];
      operand_count++;
    }
  }

  if (expecting)
  {
    options[option_index].flags    |= (expecting >> 1); /* change _WITH to _WITHOUT */
    options[option_index].argument  =  NULL;
  }

  argv[operand_count] = NULL;
  return operand_count;
}
