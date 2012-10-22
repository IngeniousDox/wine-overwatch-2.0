/*
 * CMD - Wine-compatible command line interface - built-in functions.
 *
 * Copyright (C) 1999 D A Pickles
 * Copyright (C) 2007 J Edmeades
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

/*
 * FIXME:
 * - No support for pipes, shell parameters
 * - Lots of functionality missing from builtins
 * - Messages etc need international support
 */

#define WIN32_LEAN_AND_MEAN

#include "wcmd.h"
#include <shellapi.h>
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(cmd);

extern int defaultColor;
extern BOOL echo_mode;
extern BOOL interactive;

struct env_stack *pushd_directories;
const WCHAR dotW[]    = {'.','\0'};
const WCHAR dotdotW[] = {'.','.','\0'};
const WCHAR nullW[]   = {'\0'};
const WCHAR starW[]   = {'*','\0'};
const WCHAR slashW[]  = {'\\','\0'};
const WCHAR equalW[]  = {'=','\0'};
const WCHAR wildcardsW[] = {'*','?','\0'};
const WCHAR slashstarW[] = {'\\','*','\0'};
const WCHAR inbuilt[][10] = {
        {'C','A','L','L','\0'},
        {'C','D','\0'},
        {'C','H','D','I','R','\0'},
        {'C','L','S','\0'},
        {'C','O','P','Y','\0'},
        {'C','T','T','Y','\0'},
        {'D','A','T','E','\0'},
        {'D','E','L','\0'},
        {'D','I','R','\0'},
        {'E','C','H','O','\0'},
        {'E','R','A','S','E','\0'},
        {'F','O','R','\0'},
        {'G','O','T','O','\0'},
        {'H','E','L','P','\0'},
        {'I','F','\0'},
        {'L','A','B','E','L','\0'},
        {'M','D','\0'},
        {'M','K','D','I','R','\0'},
        {'M','O','V','E','\0'},
        {'P','A','T','H','\0'},
        {'P','A','U','S','E','\0'},
        {'P','R','O','M','P','T','\0'},
        {'R','E','M','\0'},
        {'R','E','N','\0'},
        {'R','E','N','A','M','E','\0'},
        {'R','D','\0'},
        {'R','M','D','I','R','\0'},
        {'S','E','T','\0'},
        {'S','H','I','F','T','\0'},
        {'S','T','A','R','T','\0'},
        {'T','I','M','E','\0'},
        {'T','I','T','L','E','\0'},
        {'T','Y','P','E','\0'},
        {'V','E','R','I','F','Y','\0'},
        {'V','E','R','\0'},
        {'V','O','L','\0'},
        {'E','N','D','L','O','C','A','L','\0'},
        {'S','E','T','L','O','C','A','L','\0'},
        {'P','U','S','H','D','\0'},
        {'P','O','P','D','\0'},
        {'A','S','S','O','C','\0'},
        {'C','O','L','O','R','\0'},
        {'F','T','Y','P','E','\0'},
        {'M','O','R','E','\0'},
        {'C','H','O','I','C','E','\0'},
        {'E','X','I','T','\0'}
};
static const WCHAR externals[][10] = {
        {'A','T','T','R','I','B','\0'},
        {'X','C','O','P','Y','\0'}
};
static const WCHAR fslashW[] = {'/','\0'};
static const WCHAR onW[]  = {'O','N','\0'};
static const WCHAR offW[] = {'O','F','F','\0'};
static const WCHAR parmY[] = {'/','Y','\0'};
static const WCHAR parmNoY[] = {'/','-','Y','\0'};

static HINSTANCE hinst;
struct env_stack *saved_environment;
static BOOL verify_mode = FALSE;

/**************************************************************************
 * WCMD_ask_confirm
 *
 * Issue a message and ask for confirmation, waiting on a valid answer.
 *
 * Returns True if Y (or A) answer is selected
 *         If optionAll contains a pointer, ALL is allowed, and if answered
 *                   set to TRUE
 *
 */
static BOOL WCMD_ask_confirm (const WCHAR *message, BOOL showSureText,
                              BOOL *optionAll) {

    UINT msgid;
    WCHAR confirm[MAXSTRING];
    WCHAR options[MAXSTRING];
    WCHAR Ybuffer[MAXSTRING];
    WCHAR Nbuffer[MAXSTRING];
    WCHAR Abuffer[MAXSTRING];
    WCHAR answer[MAX_PATH] = {'\0'};
    DWORD count = 0;

    /* Load the translated valid answers */
    if (showSureText)
      LoadStringW(hinst, WCMD_CONFIRM, confirm, sizeof(confirm)/sizeof(WCHAR));
    msgid = optionAll ? WCMD_YESNOALL : WCMD_YESNO;
    LoadStringW(hinst, msgid, options, sizeof(options)/sizeof(WCHAR));
    LoadStringW(hinst, WCMD_YES, Ybuffer, sizeof(Ybuffer)/sizeof(WCHAR));
    LoadStringW(hinst, WCMD_NO,  Nbuffer, sizeof(Nbuffer)/sizeof(WCHAR));
    LoadStringW(hinst, WCMD_ALL, Abuffer, sizeof(Abuffer)/sizeof(WCHAR));

    /* Loop waiting on a valid answer */
    if (optionAll)
        *optionAll = FALSE;
    while (1)
    {
      WCMD_output_asis (message);
      if (showSureText)
        WCMD_output_asis (confirm);
      WCMD_output_asis (options);
      WCMD_ReadFile(GetStdHandle(STD_INPUT_HANDLE), answer, sizeof(answer)/sizeof(WCHAR), &count);
      answer[0] = toupperW(answer[0]);
      if (answer[0] == Ybuffer[0])
        return TRUE;
      if (answer[0] == Nbuffer[0])
        return FALSE;
      if (optionAll && answer[0] == Abuffer[0])
      {
        *optionAll = TRUE;
        return TRUE;
      }
    }
}

/****************************************************************************
 * WCMD_clear_screen
 *
 * Clear the terminal screen.
 */

void WCMD_clear_screen (void) {

  /* Emulate by filling the screen from the top left to bottom right with
        spaces, then moving the cursor to the top left afterwards */
  CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
  HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);

  if (GetConsoleScreenBufferInfo(hStdOut, &consoleInfo))
  {
      COORD topLeft;
      DWORD screenSize;

      screenSize = consoleInfo.dwSize.X * (consoleInfo.dwSize.Y + 1);

      topLeft.X = 0;
      topLeft.Y = 0;
      FillConsoleOutputCharacterW(hStdOut, ' ', screenSize, topLeft, &screenSize);
      SetConsoleCursorPosition(hStdOut, topLeft);
  }
}

/****************************************************************************
 * WCMD_change_tty
 *
 * Change the default i/o device (ie redirect STDin/STDout).
 */

void WCMD_change_tty (void) {

  WCMD_output_stderr (WCMD_LoadMessage(WCMD_NYI));

}

/****************************************************************************
 * WCMD_choice
 *
 */

void WCMD_choice (const WCHAR * args) {

    static const WCHAR bellW[] = {7,0};
    static const WCHAR commaW[] = {',',0};
    static const WCHAR bracket_open[] = {'[',0};
    static const WCHAR bracket_close[] = {']','?',0};
    WCHAR answer[16];
    WCHAR buffer[16];
    WCHAR *ptr = NULL;
    WCHAR *opt_c = NULL;
    WCHAR *my_command = NULL;
    WCHAR opt_default = 0;
    DWORD opt_timeout = 0;
    DWORD count;
    DWORD oldmode;
    DWORD have_console;
    BOOL opt_n = FALSE;
    BOOL opt_s = FALSE;

    have_console = GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &oldmode);
    errorlevel = 0;

    my_command = WCMD_strdupW(WCMD_skip_leading_spaces((WCHAR*) args));
    if (!my_command)
        return;

    ptr = WCMD_skip_leading_spaces(my_command);
    while (*ptr == '/') {
        switch (toupperW(ptr[1])) {
            case 'C':
                ptr += 2;
                /* the colon is optional */
                if (*ptr == ':')
                    ptr++;

                if (!*ptr || isspaceW(*ptr)) {
                    WINE_FIXME("bad parameter %s for /C\n", wine_dbgstr_w(ptr));
                    HeapFree(GetProcessHeap(), 0, my_command);
                    return;
                }

                /* remember the allowed keys (overwrite previous /C option) */
                opt_c = ptr;
                while (*ptr && (!isspaceW(*ptr)))
                    ptr++;

                if (*ptr) {
                    /* terminate allowed chars */
                    *ptr = 0;
                    ptr = WCMD_skip_leading_spaces(&ptr[1]);
                }
                WINE_TRACE("answer-list: %s\n", wine_dbgstr_w(opt_c));
                break;

            case 'N':
                opt_n = TRUE;
                ptr = WCMD_skip_leading_spaces(&ptr[2]);
                break;

            case 'S':
                opt_s = TRUE;
                ptr = WCMD_skip_leading_spaces(&ptr[2]);
                break;

            case 'T':
                ptr = &ptr[2];
                /* the colon is optional */
                if (*ptr == ':')
                    ptr++;

                opt_default = *ptr++;

                if (!opt_default || (*ptr != ',')) {
                    WINE_FIXME("bad option %s for /T\n", opt_default ? wine_dbgstr_w(ptr) : "");
                    HeapFree(GetProcessHeap(), 0, my_command);
                    return;
                }
                ptr++;

                count = 0;
                while (((answer[count] = *ptr)) && isdigitW(*ptr) && (count < 15)) {
                    count++;
                    ptr++;
                }

                answer[count] = 0;
                opt_timeout = atoiW(answer);

                ptr = WCMD_skip_leading_spaces(ptr);
                break;

            default:
                WINE_FIXME("bad parameter: %s\n", wine_dbgstr_w(ptr));
                HeapFree(GetProcessHeap(), 0, my_command);
                return;
        }
    }

    if (opt_timeout)
        WINE_FIXME("timeout not supported: %c,%d\n", opt_default, opt_timeout);

    if (have_console)
        SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), 0);

    /* use default keys, when needed: localized versions of "Y"es and "No" */
    if (!opt_c) {
        LoadStringW(hinst, WCMD_YES, buffer, sizeof(buffer)/sizeof(WCHAR));
        LoadStringW(hinst, WCMD_NO, buffer + 1, sizeof(buffer)/sizeof(WCHAR) - 1);
        opt_c = buffer;
        buffer[2] = 0;
    }

    /* print the question, when needed */
    if (*ptr)
        WCMD_output_asis(ptr);

    if (!opt_s) {
        struprW(opt_c);
        WINE_TRACE("case insensitive answer-list: %s\n", wine_dbgstr_w(opt_c));
    }

    if (!opt_n) {
        /* print a list of all allowed answers inside brackets */
        WCMD_output_asis(bracket_open);
        ptr = opt_c;
        answer[1] = 0;
        while ((answer[0] = *ptr++)) {
            WCMD_output_asis(answer);
            if (*ptr)
                WCMD_output_asis(commaW);
        }
        WCMD_output_asis(bracket_close);
    }

    while (TRUE) {

        /* FIXME: Add support for option /T */
        WCMD_ReadFile(GetStdHandle(STD_INPUT_HANDLE), answer, 1, &count);

        if (!opt_s)
            answer[0] = toupperW(answer[0]);

        ptr = strchrW(opt_c, answer[0]);
        if (ptr) {
            WCMD_output_asis(answer);
            WCMD_output_asis(newlineW);
            if (have_console)
                SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), oldmode);

            errorlevel = (ptr - opt_c) + 1;
            WINE_TRACE("answer: %d\n", errorlevel);
            HeapFree(GetProcessHeap(), 0, my_command);
            return;
        }
        else
        {
            /* key not allowed: play the bell */
            WINE_TRACE("key not allowed: %s\n", wine_dbgstr_w(answer));
            WCMD_output_asis(bellW);
        }
    }
}

/****************************************************************************
 * WCMD_AppendEOF
 *
 * Adds an EOF onto the end of a file
 * Returns TRUE on success
 */
static BOOL WCMD_AppendEOF(WCHAR *filename)
{
    HANDLE h;

    char eof = '\x1a';

    WINE_TRACE("Appending EOF to %s\n", wine_dbgstr_w(filename));
    h = CreateFileW(filename, GENERIC_WRITE, 0, NULL,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (h == NULL) {
      WINE_ERR("Failed to open %s (%d)\n", wine_dbgstr_w(filename), GetLastError());
      return FALSE;
    } else {
      SetFilePointer (h, 0, NULL, FILE_END);
      if (!WriteFile(h, &eof, 1, NULL, NULL)) {
        WINE_ERR("Failed to append EOF to %s (%d)\n", wine_dbgstr_w(filename), GetLastError());
        return FALSE;
      }
      CloseHandle(h);
    }
    return TRUE;
}

/****************************************************************************
 * WCMD_ManualCopy
 *
 * Copies from a file
 *    optionally reading only until EOF (ascii copy)
 *    optionally appending onto an existing file (append)
 * Returns TRUE on success
 */
static BOOL WCMD_ManualCopy(WCHAR *srcname, WCHAR *dstname, BOOL ascii, BOOL append)
{
    HANDLE in,out;
    BOOL   ok;
    DWORD  bytesread, byteswritten;

    WINE_TRACE("ASCII Copying %s to %s (append?%d)\n",
               wine_dbgstr_w(srcname), wine_dbgstr_w(dstname), append);

    in  = CreateFileW(srcname, GENERIC_READ, 0, NULL,
                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (in == NULL) {
      WINE_ERR("Failed to open %s (%d)\n", wine_dbgstr_w(srcname), GetLastError());
      return FALSE;
    }

    /* Open the output file, overwriting if not appending */
    out = CreateFileW(dstname, GENERIC_WRITE, 0, NULL,
                      append?OPEN_EXISTING:CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (out == NULL) {
      WINE_ERR("Failed to open %s (%d)\n", wine_dbgstr_w(dstname), GetLastError());
      return FALSE;
    }

    /* Move to end of destination if we are going to append to it */
    if (append) {
      SetFilePointer(out, 0, NULL, FILE_END);
    }

    /* Loop copying data from source to destination until EOF read */
    ok = TRUE;
    do
    {
      char buffer[MAXSTRING];

      ok = ReadFile(in, buffer, MAXSTRING, &bytesread, NULL);
      if (ok) {

        /* Stop at first EOF */
        if (ascii) {
          char *ptr = (char *)memchr((void *)buffer, '\x1a', bytesread);
          if (ptr) bytesread = (ptr - buffer);
        }

        if (bytesread) {
          ok = WriteFile(out, buffer, bytesread, &byteswritten, NULL);
          if (!ok || byteswritten != bytesread) {
            WINE_ERR("Unexpected failure writing to %s, rc=%d\n",
                     wine_dbgstr_w(dstname), GetLastError());
          }
        }
      } else {
        WINE_ERR("Unexpected failure reading from %s, rc=%d\n",
                 wine_dbgstr_w(srcname), GetLastError());
      }
    } while (ok && bytesread > 0);

    CloseHandle(out);
    CloseHandle(in);
    return ok;
}

/****************************************************************************
 * WCMD_copy
 *
 * Copy a file or wildcarded set.
 * For ascii/binary type copies, it gets complex:
 *  Syntax on command line is
 *   ... /a | /b   filename  /a /b {[ + filename /a /b]}  [dest /a /b]
 *  Where first /a or /b sets 'mode in operation' until another is found
 *  once another is found, it applies to the file preceding the /a or /b
 *  In addition each filename can contain wildcards
 * To make matters worse, the + may be in the same parameter (i.e. no
 *  whitespace) or with whitespace separating it
 *
 * ASCII mode on read == read and stop at first EOF
 * ASCII mode on write == append EOF to destination
 * Binary == copy as-is
 *
 * Design of this is to build up a list of files which will be copied into a
 * list, then work through the list file by file.
 * If no destination is specified, it defaults to the name of the first file in
 * the list, but the current directory.
 *
 */

void WCMD_copy(WCHAR * args) {

  BOOL    opt_d, opt_v, opt_n, opt_z, opt_y, opt_noty;
  WCHAR  *thisparam;
  int     argno = 0;
  WCHAR  *rawarg;
  WIN32_FIND_DATAW fd;
  HANDLE  hff;
  int     binarymode = -1;            /* -1 means use the default, 1 is binary, 0 ascii */
  BOOL    concatnextfilename = FALSE; /* True if we have just processed a +             */
  BOOL    anyconcats         = FALSE; /* Have we found any + options                    */
  BOOL    appendfirstsource  = FALSE; /* Use first found filename as destination        */
  BOOL    writtenoneconcat   = FALSE; /* Remember when the first concatenated file done */
  BOOL    prompt;                     /* Prompt before overwriting                      */
  WCHAR   destname[MAX_PATH];         /* Used in calculating the destination name       */
  BOOL    destisdirectory = FALSE;    /* Is the destination a directory?                */
  BOOL    status;
  WCHAR   copycmd[4];
  DWORD   len;
  static const WCHAR copyCmdW[] = {'C','O','P','Y','C','M','D','\0'};

  typedef struct _COPY_FILES
  {
    struct _COPY_FILES *next;
    BOOL                concatenate;
    WCHAR              *name;
    int                 binarycopy;
  } COPY_FILES;
  COPY_FILES *sourcelist    = NULL;
  COPY_FILES *lastcopyentry = NULL;
  COPY_FILES *destination   = NULL;
  COPY_FILES *thiscopy      = NULL;
  COPY_FILES *prevcopy      = NULL;

  /* Assume we were successful! */
  errorlevel = 0;

  /* If no args supplied at all, report an error */
  if (param1[0] == 0x00) {
    WCMD_output_stderr (WCMD_LoadMessage(WCMD_NOARG));
    errorlevel = 1;
    return;
  }

  opt_d = opt_v = opt_n = opt_z = opt_y = opt_noty = FALSE;

  /* Walk through all args, building up a list of files to process */
  thisparam = WCMD_parameter(args, argno++, &rawarg, NULL, TRUE, FALSE);
  while (*(thisparam)) {
    WCHAR *pos1, *pos2;
    BOOL inquotes;

    WINE_TRACE("Working on parameter '%s'\n", wine_dbgstr_w(thisparam));

    /* Handle switches */
    if (*thisparam == '/') {
        while (*thisparam == '/') {
        thisparam++;
        if (toupperW(*thisparam) == 'D') {
          opt_d = TRUE;
          if (opt_d) WINE_FIXME("copy /D support not implemented yet\n");
        } else if (toupperW(*thisparam) == 'Y') {
          opt_y = TRUE;
        } else if (toupperW(*thisparam) == '-' && toupperW(*(thisparam+1)) == 'Y') {
          opt_noty = TRUE;
        } else if (toupperW(*thisparam) == 'V') {
          opt_v = TRUE;
          if (opt_v) WINE_FIXME("copy /V support not implemented yet\n");
        } else if (toupperW(*thisparam) == 'N') {
          opt_n = TRUE;
          if (opt_n) WINE_FIXME("copy /N support not implemented yet\n");
        } else if (toupperW(*thisparam) == 'Z') {
          opt_z = TRUE;
          if (opt_z) WINE_FIXME("copy /Z support not implemented yet\n");
        } else if (toupperW(*thisparam) == 'A') {
          if (binarymode != 0) {
            binarymode = 0;
            WINE_TRACE("Subsequent files will be handled as ASCII\n");
            if (destination != NULL) {
              WINE_TRACE("file %s will be written as ASCII\n", wine_dbgstr_w(destination->name));
              destination->binarycopy = binarymode;
            } else if (lastcopyentry != NULL) {
              WINE_TRACE("file %s will be read as ASCII\n", wine_dbgstr_w(lastcopyentry->name));
              lastcopyentry->binarycopy = binarymode;
            }
          }
        } else if (toupperW(*thisparam) == 'B') {
          if (binarymode != 1) {
            binarymode = 1;
            WINE_TRACE("Subsequent files will be handled as binary\n");
            if (destination != NULL) {
              WINE_TRACE("file %s will be written as binary\n", wine_dbgstr_w(destination->name));
              destination->binarycopy = binarymode;
            } else if (lastcopyentry != NULL) {
              WINE_TRACE("file %s will be read as binary\n", wine_dbgstr_w(lastcopyentry->name));
              lastcopyentry->binarycopy = binarymode;
            }
          }
        } else {
          WINE_FIXME("Unexpected copy switch %s\n", wine_dbgstr_w(thisparam));
        }
        thisparam++;
      }

      /* This parameter was purely switches, get the next one */
      thisparam = WCMD_parameter(args, argno++, &rawarg, NULL, TRUE, FALSE);
      continue;
    }

    /* We have found something which is not a switch. If could be anything of the form
         sourcefilename (which could be destination too)
         + (when filename + filename syntex used)
         sourcefilename+sourcefilename
         +sourcefilename
         +/b[tests show windows then ignores to end of parameter]
     */

    if (*thisparam=='+') {
      if (lastcopyentry == NULL) {
        WCMD_output_stderr(WCMD_LoadMessage(WCMD_SYNTAXERR));
        errorlevel = 1;
        goto exitreturn;
      } else {
        concatnextfilename = TRUE;
        anyconcats         = TRUE;
      }

      /* Move to next thing to process */
      thisparam++;
      if (*thisparam == 0x00)
        thisparam = WCMD_parameter(args, argno++, &rawarg, NULL, TRUE, FALSE);
      continue;
    }

    /* We have found something to process - build a COPY_FILE block to store it */
    thiscopy = HeapAlloc(GetProcessHeap(),0,sizeof(COPY_FILES));
    if (thiscopy == NULL) goto exitreturn;


    WINE_TRACE("Not a switch, but probably a filename/list %s\n", wine_dbgstr_w(thisparam));
    thiscopy->concatenate = concatnextfilename;
    thiscopy->binarycopy  = binarymode;
    thiscopy->next        = NULL;

    /* Time to work out the name. Allocate at least enough space (deliberately too much to
       leave space to append \* to the end) , then copy in character by character. Strip off
       quotes if we find them.                                                               */
    len = strlenW(thisparam) + (sizeof(WCHAR) * 5);  /* 5 spare characters, null + \*.*      */
    thiscopy->name = HeapAlloc(GetProcessHeap(),0,len*sizeof(WCHAR));
    memset(thiscopy->name, 0x00, len);

    pos1 = thisparam;
    pos2 = thiscopy->name;
    inquotes = FALSE;
    while (*pos1 && (inquotes || (*pos1 != '+' && *pos1 != '/'))) {
      if (*pos1 == '"') {
        inquotes = !inquotes;
        pos1++;
      } else *pos2++ = *pos1++;
    }
    *pos2 = 0;
    WINE_TRACE("Calculated file name %s\n", wine_dbgstr_w(thiscopy->name));

    /* This is either the first source, concatenated subsequent source or destination */
    if (sourcelist == NULL) {
      WINE_TRACE("Adding as first source part\n");
      sourcelist = thiscopy;
      lastcopyentry = thiscopy;
    } else if (concatnextfilename) {
      WINE_TRACE("Adding to source file list to be concatenated\n");
      lastcopyentry->next = thiscopy;
      lastcopyentry = thiscopy;
    } else if (destination == NULL) {
      destination = thiscopy;
    } else {
      /* We have processed sources and destinations and still found more to do - invalid */
      WCMD_output_stderr(WCMD_LoadMessage(WCMD_SYNTAXERR));
      errorlevel = 1;
      goto exitreturn;
    }
    concatnextfilename    = FALSE;

    /* We either need to process the rest of the parameter or move to the next */
    if (*pos1 == '/' || *pos1 == '+') {
      thisparam = pos1;
      continue;
    } else {
      thisparam = WCMD_parameter(args, argno++, &rawarg, NULL, TRUE, FALSE);
    }
  }

  /* Ensure we have at least one source file */
  if (!sourcelist) {
    WCMD_output_stderr(WCMD_LoadMessage(WCMD_SYNTAXERR));
    errorlevel = 1;
    goto exitreturn;
  }

  /* Default whether automatic overwriting is on. If we are interactive then
     we prompt by default, otherwise we overwrite by default
     /-Y has the highest priority, then /Y and finally the COPYCMD env. variable */
  if (opt_noty) prompt = TRUE;
  else if (opt_y) prompt = FALSE;
  else {
    /* By default, we will force the overwrite in batch mode and ask for
     * confirmation in interactive mode. */
    prompt = interactive;
    /* If COPYCMD is set, then we force the overwrite with /Y and ask for
     * confirmation with /-Y. If COPYCMD is neither of those, then we use the
     * default behavior. */
    len = GetEnvironmentVariableW(copyCmdW, copycmd, sizeof(copycmd)/sizeof(WCHAR));
    if (len && len < (sizeof(copycmd)/sizeof(WCHAR))) {
      if (!lstrcmpiW (copycmd, parmY))
        prompt = FALSE;
      else if (!lstrcmpiW (copycmd, parmNoY))
        prompt = TRUE;
    }
  }

  /* Calculate the destination now - if none supplied, its current dir +
     filename of first file in list*/
  if (destination == NULL) {

    WINE_TRACE("No destination supplied, so need to calculate it\n");
    strcpyW(destname, dotW);
    strcatW(destname, slashW);

    destination = HeapAlloc(GetProcessHeap(),0,sizeof(COPY_FILES));
    if (destination == NULL) goto exitreturn;
    destination->concatenate = FALSE;           /* Not used for destination */
    destination->binarycopy  = binarymode;
    destination->next        = NULL;            /* Not used for destination */
    destination->name        = NULL;            /* To be filled in          */
    destisdirectory          = TRUE;

  } else {
    WCHAR *filenamepart;
    DWORD  attributes;

    WINE_TRACE("Destination supplied, processing to see if file or directory\n");

    /* Convert to fully qualified path/filename */
    GetFullPathNameW(destination->name, sizeof(destname)/sizeof(WCHAR), destname, &filenamepart);
    WINE_TRACE("Full dest name is '%s'\n", wine_dbgstr_w(destname));

    /* If parameter is a directory, ensure it ends in \ */
    attributes = GetFileAttributesW(destname);
    if ((destname[strlenW(destname) - 1] == '\\') ||
        ((attributes != INVALID_FILE_ATTRIBUTES) &&
         (attributes & FILE_ATTRIBUTE_DIRECTORY))) {

      destisdirectory = TRUE;
      if (!(destname[strlenW(destname) - 1] == '\\')) strcatW(destname, slashW);
      WINE_TRACE("Directory, so full name is now '%s'\n", wine_dbgstr_w(destname));
    }
  }

  /* Normally, the destination is the current directory unless we are
     concatenating, in which case its current directory plus first filename.
     Note that if the
     In addition by default it is a binary copy unless concatenating, when
     the copy defaults to an ascii copy (stop at EOF). We do not know the
     first source part yet (until we search) so flag as needing filling in. */

  if (anyconcats) {
    /* We have found an a+b type syntax, so destination has to be a filename
       and we need to default to ascii copying. If we have been supplied a
       directory as the destination, we need to defer calculating the name   */
    if (destisdirectory) appendfirstsource = TRUE;
    if (destination->binarycopy == -1) destination->binarycopy = 0;

  } else if (!destisdirectory) {
    /* We have been asked to copy to a filename. Default to ascii IF the
       source contains wildcards (true even if only one match)           */
    if (strpbrkW(sourcelist->name, wildcardsW) != NULL) {
      anyconcats = TRUE;  /* We really are concatenating to a single file */
      if (destination->binarycopy == -1) {
        destination->binarycopy = 0;
      }
    } else {
      if (destination->binarycopy == -1) {
        destination->binarycopy = 1;
      }
    }
  }

  /* Save away the destination name*/
  HeapFree(GetProcessHeap(), 0, destination->name);
  destination->name = WCMD_strdupW(destname);
  WINE_TRACE("Resolved destination is '%s' (calc later %d)\n",
             wine_dbgstr_w(destname), appendfirstsource);

  /* Now we need to walk the set of sources, and process each name we come to.
     If anyconcats is true, we are writing to one file, otherwise we are using
     the source name each time.
     If destination exists, prompt for overwrite the first time (if concatenating
     we ask each time until yes is answered)
     The first source file we come across must exist (when wildcards expanded)
     and if concatenating with overwrite prompts, each source file must exist
     until a yes is answered.                                                    */

  thiscopy = sourcelist;
  prevcopy = NULL;

  while (thiscopy != NULL) {

    WCHAR  srcpath[MAX_PATH];
    WCHAR *filenamepart;
    DWORD  attributes;

    /* If it was not explicit, we now know whether we are concatenating or not and
       hence whether to copy as binary or ascii                                    */
    if (thiscopy->binarycopy == -1) thiscopy->binarycopy = !anyconcats;

    /* Convert to fully qualified path/filename in srcpath, file filenamepart pointing
       to where the filename portion begins (used for wildcart expansion.              */
    GetFullPathNameW(thiscopy->name, sizeof(srcpath)/sizeof(WCHAR), srcpath, &filenamepart);
    WINE_TRACE("Full src name is '%s'\n", wine_dbgstr_w(srcpath));

    /* If parameter is a directory, ensure it ends in \* */
    attributes = GetFileAttributesW(srcpath);
    if (srcpath[strlenW(srcpath) - 1] == '\\') {

      /* We need to know where the filename part starts, so append * and
         recalculate the full resulting path                              */
      strcatW(thiscopy->name, starW);
      GetFullPathNameW(thiscopy->name, sizeof(srcpath)/sizeof(WCHAR), srcpath, &filenamepart);
      WINE_TRACE("Directory, so full name is now '%s'\n", wine_dbgstr_w(srcpath));

    } else if ((strpbrkW(srcpath, wildcardsW) == NULL) &&
               (attributes != INVALID_FILE_ATTRIBUTES) &&
               (attributes & FILE_ATTRIBUTE_DIRECTORY)) {

      /* We need to know where the filename part starts, so append \* and
         recalculate the full resulting path                              */
      strcatW(thiscopy->name, slashstarW);
      GetFullPathNameW(thiscopy->name, sizeof(srcpath)/sizeof(WCHAR), srcpath, &filenamepart);
      WINE_TRACE("Directory, so full name is now '%s'\n", wine_dbgstr_w(srcpath));
    }

    WINE_TRACE("Copy source (calculated): path: '%s' (Concats: %d)\n",
                    wine_dbgstr_w(srcpath), anyconcats);

    /* Loop through all source files */
    WINE_TRACE("Searching for: '%s'\n", wine_dbgstr_w(srcpath));
    hff = FindFirstFileW(srcpath, &fd);
    if (hff != INVALID_HANDLE_VALUE) {
      do {
        WCHAR outname[MAX_PATH];
        BOOL  overwrite;

        /* Skip . and .., and directories */
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
          WINE_TRACE("Skipping directories\n");
        } else {

          /* Build final destination name */
          strcpyW(outname, destination->name);
          if (destisdirectory || appendfirstsource) strcatW(outname, fd.cFileName);

          /* Build source name */
          strcpyW(filenamepart, fd.cFileName);

          /* Do we just overwrite */
          overwrite = !prompt;
          if (anyconcats && writtenoneconcat) {
            overwrite = TRUE;
          }

          WINE_TRACE("Copying from : '%s'\n", wine_dbgstr_w(srcpath));
          WINE_TRACE("Copying to : '%s'\n", wine_dbgstr_w(outname));
          WINE_TRACE("Flags: srcbinary(%d), dstbinary(%d), over(%d), prompt(%d)\n",
                     thiscopy->binarycopy, destination->binarycopy, overwrite, prompt);

          /* Prompt before overwriting */
          if (!overwrite) {
            DWORD attributes = GetFileAttributesW(outname);
            if (attributes != INVALID_FILE_ATTRIBUTES) {
              WCHAR* question;
              question = WCMD_format_string(WCMD_LoadMessage(WCMD_OVERWRITE), outname);
              overwrite = WCMD_ask_confirm(question, FALSE, NULL);
              LocalFree(question);
            }
            else overwrite = TRUE;
          }

          /* If we needed tyo save away the first filename, do it */
          if (appendfirstsource && overwrite) {
            HeapFree(GetProcessHeap(), 0, destination->name);
            destination->name = WCMD_strdupW(outname);
            WINE_TRACE("Final resolved destination name : '%s'\n", wine_dbgstr_w(outname));
            appendfirstsource = FALSE;
            destisdirectory = FALSE;
          }

          /* Do the copy as appropriate */
          if (overwrite) {
            if (anyconcats && writtenoneconcat) {
              if (thiscopy->binarycopy) {
                status = WCMD_ManualCopy(srcpath, outname, FALSE, TRUE);
              } else {
                status = WCMD_ManualCopy(srcpath, outname, TRUE, TRUE);
              }
            } else if (!thiscopy->binarycopy) {
              status = WCMD_ManualCopy(srcpath, outname, TRUE, FALSE);
            } else {
              status = CopyFileW(srcpath, outname, FALSE);
            }
            if (!status) {
              WCMD_print_error ();
              errorlevel = 1;
            } else {
              WINE_TRACE("Copied successfully\n");
              if (anyconcats) writtenoneconcat = TRUE;

              /* Append EOF if ascii destination and we are not going to add more onto the end
                 Note: Testing shows windows has an optimization whereas if you have a binary
                 copy of a file to a single destination (ie concatenation) then it does not add
                 the EOF, hence the check on the source copy type below.                       */
              if (!destination->binarycopy && !anyconcats && !thiscopy->binarycopy) {
                if (!WCMD_AppendEOF(outname)) {
                  WCMD_print_error ();
                  errorlevel = 1;
                }
              }
            }
          }
        }
      } while (FindNextFileW(hff, &fd) != 0);
      FindClose (hff);
    } else {
      /* Error if the first file was not found */
      if (!anyconcats || (anyconcats && !writtenoneconcat)) {
        WCMD_print_error ();
        errorlevel = 1;
      }
    }

    /* Step on to the next supplied source */
    thiscopy = thiscopy -> next;
  }

  /* Append EOF if ascii destination and we were concatenating */
  if (!errorlevel && !destination->binarycopy && anyconcats && writtenoneconcat) {
    if (!WCMD_AppendEOF(destination->name)) {
      WCMD_print_error ();
      errorlevel = 1;
    }
  }

  /* Exit out of the routine, freeing any remaining allocated memory */
exitreturn:

  thiscopy = sourcelist;
  while (thiscopy != NULL) {
    prevcopy = thiscopy;
    /* Free up this block*/
    thiscopy = thiscopy -> next;
    HeapFree(GetProcessHeap(), 0, prevcopy->name);
    HeapFree(GetProcessHeap(), 0, prevcopy);
  }

  /* Free up the destination memory */
  if (destination) {
    HeapFree(GetProcessHeap(), 0, destination->name);
    HeapFree(GetProcessHeap(), 0, destination);
  }

  return;
}

/****************************************************************************
 * WCMD_create_dir
 *
 * Create a directory (and, if needed, any intermediate directories).
 *
 * Modifies its argument by replacing slashes temporarily with nulls.
 */

static BOOL create_full_path(WCHAR* path)
{
    WCHAR *p, *start;

    /* don't mess with drive letter portion of path, if any */
    start = path;
    if (path[1] == ':')
        start = path+2;

    /* Strip trailing slashes. */
    for (p = path + strlenW(path) - 1; p != start && *p == '\\'; p--)
        *p = 0;

    /* Step through path, creating intermediate directories as needed. */
    /* First component includes drive letter, if any. */
    p = start;
    for (;;) {
        DWORD rv;
        /* Skip to end of component */
        while (*p == '\\') p++;
        while (*p && *p != '\\') p++;
        if (!*p) {
            /* path is now the original full path */
            return CreateDirectoryW(path, NULL);
        }
        /* Truncate path, create intermediate directory, and restore path */
        *p = 0;
        rv = CreateDirectoryW(path, NULL);
        *p = '\\';
        if (!rv && GetLastError() != ERROR_ALREADY_EXISTS)
            return FALSE;
    }
    /* notreached */
    return FALSE;
}

void WCMD_create_dir (WCHAR *args) {
    int   argno = 0;
    WCHAR *argN = args;

    if (param1[0] == 0x00) {
        WCMD_output_stderr(WCMD_LoadMessage(WCMD_NOARG));
        return;
    }
    /* Loop through all args */
    while (TRUE) {
        WCHAR *thisArg = WCMD_parameter(args, argno++, &argN, NULL, FALSE, FALSE);
        if (!argN) break;
        if (!create_full_path(thisArg)) {
            WCMD_print_error ();
            errorlevel = 1;
        }
    }
}

/* Parse the /A options given by the user on the commandline
 * into a bitmask of wanted attributes (*wantSet),
 * and a bitmask of unwanted attributes (*wantClear).
 */
static void WCMD_delete_parse_attributes(DWORD *wantSet, DWORD *wantClear) {
    static const WCHAR parmA[] = {'/','A','\0'};
    WCHAR *p;

    /* both are strictly 'out' parameters */
    *wantSet=0;
    *wantClear=0;

    /* For each /A argument */
    for (p=strstrW(quals, parmA); p != NULL; p=strstrW(p, parmA)) {
        /* Skip /A itself */
        p += 2;

        /* Skip optional : */
        if (*p == ':') p++;

        /* For each of the attribute specifier chars to this /A option */
        for (; *p != 0 && *p != '/'; p++) {
            BOOL negate = FALSE;
            DWORD mask  = 0;

            if (*p == '-') {
                negate=TRUE;
                p++;
            }

            /* Convert the attribute specifier to a bit in one of the masks */
            switch (*p) {
            case 'R': mask = FILE_ATTRIBUTE_READONLY; break;
            case 'H': mask = FILE_ATTRIBUTE_HIDDEN;   break;
            case 'S': mask = FILE_ATTRIBUTE_SYSTEM;   break;
            case 'A': mask = FILE_ATTRIBUTE_ARCHIVE;  break;
            default:
                WCMD_output_stderr(WCMD_LoadMessage(WCMD_SYNTAXERR));
            }
            if (negate)
                *wantClear |= mask;
            else
                *wantSet |= mask;
        }
    }
}

/* If filename part of parameter is * or *.*,
 * and neither /Q nor /P options were given,
 * prompt the user whether to proceed.
 * Returns FALSE if user says no, TRUE otherwise.
 * *pPrompted is set to TRUE if the user is prompted.
 * (If /P supplied, del will prompt for individual files later.)
 */
static BOOL WCMD_delete_confirm_wildcard(const WCHAR *filename, BOOL *pPrompted) {
    static const WCHAR parmP[] = {'/','P','\0'};
    static const WCHAR parmQ[] = {'/','Q','\0'};

    if ((strstrW(quals, parmQ) == NULL) && (strstrW(quals, parmP) == NULL)) {
        static const WCHAR anyExt[]= {'.','*','\0'};
        WCHAR drive[10];
        WCHAR dir[MAX_PATH];
        WCHAR fname[MAX_PATH];
        WCHAR ext[MAX_PATH];
        WCHAR fpath[MAX_PATH];

        /* Convert path into actual directory spec */
        GetFullPathNameW(filename, sizeof(fpath)/sizeof(WCHAR), fpath, NULL);
        WCMD_splitpath(fpath, drive, dir, fname, ext);

        /* Only prompt for * and *.*, not *a, a*, *.a* etc */
        if ((strcmpW(fname, starW) == 0) &&
            (*ext == 0x00 || (strcmpW(ext, anyExt) == 0))) {

            WCHAR question[MAXSTRING];
            static const WCHAR fmt[] = {'%','s',' ','\0'};

            /* Caller uses this to suppress "file not found" warning later */
            *pPrompted = TRUE;

            /* Ask for confirmation */
            wsprintfW(question, fmt, fpath);
            return WCMD_ask_confirm(question, TRUE, NULL);
        }
    }
    /* No scary wildcard, or question suppressed, so it's ok to delete the file(s) */
    return TRUE;
}

/* Helper function for WCMD_delete().
 * Deletes a single file, directory, or wildcard.
 * If /S was given, does it recursively.
 * Returns TRUE if a file was deleted.
 */
static BOOL WCMD_delete_one (const WCHAR *thisArg) {

    static const WCHAR parmP[] = {'/','P','\0'};
    static const WCHAR parmS[] = {'/','S','\0'};
    static const WCHAR parmF[] = {'/','F','\0'};
    DWORD wanted_attrs;
    DWORD unwanted_attrs;
    BOOL found = FALSE;
    WCHAR argCopy[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE hff;
    WCHAR fpath[MAX_PATH];
    WCHAR *p;
    BOOL handleParm = TRUE;

    WCMD_delete_parse_attributes(&wanted_attrs, &unwanted_attrs);

    strcpyW(argCopy, thisArg);
    WINE_TRACE("del: Processing arg %s (quals:%s)\n",
               wine_dbgstr_w(argCopy), wine_dbgstr_w(quals));

    if (!WCMD_delete_confirm_wildcard(argCopy, &found)) {
        /* Skip this arg if user declines to delete *.* */
        return FALSE;
    }

    /* First, try to delete in the current directory */
    hff = FindFirstFileW(argCopy, &fd);
    if (hff == INVALID_HANDLE_VALUE) {
      handleParm = FALSE;
    } else {
      found = TRUE;
    }

    /* Support del <dirname> by just deleting all files dirname\* */
    if (handleParm
        && (strchrW(argCopy,'*') == NULL)
        && (strchrW(argCopy,'?') == NULL)
        && (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
    {
      WCHAR modifiedParm[MAX_PATH];
      static const WCHAR slashStar[] = {'\\','*','\0'};

      strcpyW(modifiedParm, argCopy);
      strcatW(modifiedParm, slashStar);
      FindClose(hff);
      found = TRUE;
      WCMD_delete_one(modifiedParm);

    } else if (handleParm) {

      /* Build the filename to delete as <supplied directory>\<findfirst filename> */
      strcpyW (fpath, argCopy);
      do {
        p = strrchrW (fpath, '\\');
        if (p != NULL) {
          *++p = '\0';
          strcatW (fpath, fd.cFileName);
        }
        else strcpyW (fpath, fd.cFileName);
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
          BOOL ok;

          /* Handle attribute matching (/A) */
          ok =  ((fd.dwFileAttributes & wanted_attrs) == wanted_attrs)
             && ((fd.dwFileAttributes & unwanted_attrs) == 0);

          /* /P means prompt for each file */
          if (ok && strstrW (quals, parmP) != NULL) {
            WCHAR* question;

            /* Ask for confirmation */
            question = WCMD_format_string(WCMD_LoadMessage(WCMD_DELPROMPT), fpath);
            ok = WCMD_ask_confirm(question, FALSE, NULL);
            LocalFree(question);
          }

          /* Only proceed if ok to */
          if (ok) {

            /* If file is read only, and /A:r or /F supplied, delete it */
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_READONLY &&
                ((wanted_attrs & FILE_ATTRIBUTE_READONLY) ||
                strstrW (quals, parmF) != NULL)) {
                SetFileAttributesW(fpath, fd.dwFileAttributes & ~FILE_ATTRIBUTE_READONLY);
            }

            /* Now do the delete */
            if (!DeleteFileW(fpath)) WCMD_print_error ();
          }

        }
      } while (FindNextFileW(hff, &fd) != 0);
      FindClose (hff);
    }

    /* Now recurse into all subdirectories handling the parameter in the same way */
    if (strstrW (quals, parmS) != NULL) {

      WCHAR thisDir[MAX_PATH];
      int cPos;

      WCHAR drive[10];
      WCHAR dir[MAX_PATH];
      WCHAR fname[MAX_PATH];
      WCHAR ext[MAX_PATH];

      /* Convert path into actual directory spec */
      GetFullPathNameW(argCopy, sizeof(thisDir)/sizeof(WCHAR), thisDir, NULL);
      WCMD_splitpath(thisDir, drive, dir, fname, ext);

      strcpyW(thisDir, drive);
      strcatW(thisDir, dir);
      cPos = strlenW(thisDir);

      WINE_TRACE("Searching recursively in '%s'\n", wine_dbgstr_w(thisDir));

      /* Append '*' to the directory */
      thisDir[cPos] = '*';
      thisDir[cPos+1] = 0x00;

      hff = FindFirstFileW(thisDir, &fd);

      /* Remove residual '*' */
      thisDir[cPos] = 0x00;

      if (hff != INVALID_HANDLE_VALUE) {
        DIRECTORY_STACK *allDirs = NULL;
        DIRECTORY_STACK *lastEntry = NULL;

        do {
          if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
              (strcmpW(fd.cFileName, dotdotW) != 0) &&
              (strcmpW(fd.cFileName, dotW) != 0)) {

            DIRECTORY_STACK *nextDir;
            WCHAR subParm[MAX_PATH];

            /* Work out search parameter in sub dir */
            strcpyW (subParm, thisDir);
            strcatW (subParm, fd.cFileName);
            strcatW (subParm, slashW);
            strcatW (subParm, fname);
            strcatW (subParm, ext);
            WINE_TRACE("Recursive, Adding to search list '%s'\n", wine_dbgstr_w(subParm));

            /* Allocate memory, add to list */
            nextDir = HeapAlloc(GetProcessHeap(),0,sizeof(DIRECTORY_STACK));
            if (allDirs == NULL) allDirs = nextDir;
            if (lastEntry != NULL) lastEntry->next = nextDir;
            lastEntry = nextDir;
            nextDir->next = NULL;
            nextDir->dirName = HeapAlloc(GetProcessHeap(),0,
	 (strlenW(subParm)+1) * sizeof(WCHAR));
            strcpyW(nextDir->dirName, subParm);
          }
        } while (FindNextFileW(hff, &fd) != 0);
        FindClose (hff);

        /* Go through each subdir doing the delete */
        while (allDirs != NULL) {
          DIRECTORY_STACK *tempDir;

          tempDir = allDirs->next;
          found |= WCMD_delete_one (allDirs->dirName);

          HeapFree(GetProcessHeap(),0,allDirs->dirName);
          HeapFree(GetProcessHeap(),0,allDirs);
          allDirs = tempDir;
        }
      }
    }

    return found;
}

/****************************************************************************
 * WCMD_delete
 *
 * Delete a file or wildcarded set.
 *
 * Note on /A:
 *  - Testing shows /A is repeatable, eg. /a-r /ar matches all files
 *  - Each set is a pattern, eg /ahr /as-r means
 *         readonly+hidden OR nonreadonly system files
 *  - The '-' applies to a single field, ie /a:-hr means read only
 *         non-hidden files
 */

BOOL WCMD_delete (WCHAR *args) {
    int   argno;
    WCHAR *argN;
    BOOL  argsProcessed = FALSE;
    BOOL  foundAny      = FALSE;

    errorlevel = 0;

    for (argno=0; ; argno++) {
        BOOL found;
        WCHAR *thisArg;

        argN = NULL;
        thisArg = WCMD_parameter (args, argno, &argN, NULL, FALSE, FALSE);
        if (!argN)
            break;       /* no more parameters */
        if (argN[0] == '/')
            continue;    /* skip options */

        argsProcessed = TRUE;
        found = WCMD_delete_one(thisArg);
        if (!found) {
            errorlevel = 1;
            WCMD_output_stderr(WCMD_LoadMessage(WCMD_FILENOTFOUND), thisArg);
        }
        foundAny |= found;
    }

    /* Handle no valid args */
    if (!argsProcessed)
        WCMD_output_stderr(WCMD_LoadMessage(WCMD_NOARG));

    return foundAny;
}

/*
 * WCMD_strtrim
 *
 * Returns a trimmed version of s with all leading and trailing whitespace removed
 * Pre: s non NULL
 *
 */
static WCHAR *WCMD_strtrim(const WCHAR *s)
{
    DWORD len = strlenW(s);
    const WCHAR *start = s;
    WCHAR* result;

    if (!(result = HeapAlloc(GetProcessHeap(), 0, (len + 1) * sizeof(WCHAR))))
        return NULL;

    while (isspaceW(*start)) start++;
    if (*start) {
        const WCHAR *end = s + len - 1;
        while (end > start && isspaceW(*end)) end--;
        memcpy(result, start, (end - start + 2) * sizeof(WCHAR));
        result[end - start + 1] = '\0';
    } else {
        result[0] = '\0';
    }

    return result;
}

/****************************************************************************
 * WCMD_echo
 *
 * Echo input to the screen (or not). We don't try to emulate the bugs
 * in DOS (try typing "ECHO ON AGAIN" for an example).
 */

void WCMD_echo (const WCHAR *args)
{
  int count;
  const WCHAR *origcommand = args;
  WCHAR *trimmed;

  if (   args[0]==' ' || args[0]=='\t' || args[0]=='.'
      || args[0]==':' || args[0]==';')
    args++;

  trimmed = WCMD_strtrim(args);
  if (!trimmed) return;

  count = strlenW(trimmed);
  if (count == 0 && origcommand[0]!='.' && origcommand[0]!=':'
                 && origcommand[0]!=';') {
    if (echo_mode) WCMD_output (WCMD_LoadMessage(WCMD_ECHOPROMPT), onW);
    else WCMD_output (WCMD_LoadMessage(WCMD_ECHOPROMPT), offW);
    return;
  }

  if (lstrcmpiW(trimmed, onW) == 0)
    echo_mode = TRUE;
  else if (lstrcmpiW(trimmed, offW) == 0)
    echo_mode = FALSE;
  else {
    WCMD_output_asis (args);
    WCMD_output_asis (newlineW);
  }
  HeapFree(GetProcessHeap(), 0, trimmed);
}

/*****************************************************************************
 * WCMD_part_execute
 *
 * Execute a command, and any && or bracketed follow on to the command. The
 * first command to be executed may not be at the front of the
 * commands->thiscommand string (eg. it may point after a DO or ELSE)
 */
static void WCMD_part_execute(CMD_LIST **cmdList, const WCHAR *firstcmd,
                              const WCHAR *variable, const WCHAR *value,
                              BOOL isIF, BOOL executecmds)
{
  CMD_LIST *curPosition = *cmdList;
  int myDepth = (*cmdList)->bracketDepth;

  WINE_TRACE("cmdList(%p), firstCmd(%p), with variable '%s'='%s', doIt(%d)\n",
             cmdList, wine_dbgstr_w(firstcmd),
             wine_dbgstr_w(variable), wine_dbgstr_w(value),
             executecmds);

  /* Skip leading whitespace between condition and the command */
  while (firstcmd && *firstcmd && (*firstcmd==' ' || *firstcmd=='\t')) firstcmd++;

  /* Process the first command, if there is one */
  if (executecmds && firstcmd && *firstcmd) {
    WCHAR *command = WCMD_strdupW(firstcmd);
    WCMD_execute (firstcmd, (*cmdList)->redirects, variable, value, cmdList, FALSE);
    HeapFree(GetProcessHeap(), 0, command);
  }


  /* If it didn't move the position, step to next command */
  if (curPosition == *cmdList) *cmdList = (*cmdList)->nextcommand;

  /* Process any other parts of the command */
  if (*cmdList) {
    BOOL processThese = executecmds;

    while (*cmdList) {
      static const WCHAR ifElse[] = {'e','l','s','e'};

      /* execute all appropriate commands */
      curPosition = *cmdList;

      WINE_TRACE("Processing cmdList(%p) - delim(%d) bd(%d / %d)\n",
                 *cmdList,
                 (*cmdList)->prevDelim,
                 (*cmdList)->bracketDepth, myDepth);

      /* Execute any statements appended to the line */
      /* FIXME: Only if previous call worked for && or failed for || */
      if ((*cmdList)->prevDelim == CMD_ONFAILURE ||
          (*cmdList)->prevDelim == CMD_ONSUCCESS) {
        if (processThese && (*cmdList)->command) {
          WCMD_execute ((*cmdList)->command, (*cmdList)->redirects, variable,
                        value, cmdList, FALSE);
        }
        if (curPosition == *cmdList) *cmdList = (*cmdList)->nextcommand;

      /* Execute any appended to the statement with (...) */
      } else if ((*cmdList)->bracketDepth > myDepth) {
        if (processThese) {
          *cmdList = WCMD_process_commands(*cmdList, TRUE, variable, value, FALSE);
          WINE_TRACE("Back from processing commands, (next = %p)\n", *cmdList);
        }
        if (curPosition == *cmdList) *cmdList = (*cmdList)->nextcommand;

      /* End of the command - does 'ELSE ' follow as the next command? */
      } else {
        if (isIF
            && WCMD_keyword_ws_found(ifElse, sizeof(ifElse)/sizeof(ifElse[0]),
                                     (*cmdList)->command)) {

          /* Swap between if and else processing */
          processThese = !processThese;

          /* Process the ELSE part */
          if (processThese) {
            const int keyw_len = sizeof(ifElse)/sizeof(ifElse[0]) + 1;
            WCHAR *cmd = ((*cmdList)->command) + keyw_len;

            /* Skip leading whitespace between condition and the command */
            while (*cmd && (*cmd==' ' || *cmd=='\t')) cmd++;
            if (*cmd) {
              WCMD_execute (cmd, (*cmdList)->redirects, variable, value, cmdList, FALSE);
            }
          }
          if (curPosition == *cmdList) *cmdList = (*cmdList)->nextcommand;
        } else {
          WINE_TRACE("Found end of this IF statement (next = %p)\n", *cmdList);
          break;
        }
      }
    }
  }
  return;
}

/*****************************************************************************
 * WCMD_parse_forf_options
 *
 * Parses the for /f 'options', extracting the values and validating the
 * keywords. Note all keywords are optional.
 * Parameters:
 *  options    [I] The unparsed parameter string
 *  eol        [O] Set to the comment character (eol=x)
 *  skip       [O] Set to the number of lines to skip (skip=xx)
 *  delims     [O] Set to the token delimiters (delims=)
 *  tokens     [O] Set to the requested tokens, as provided (tokens=)
 *  usebackq   [O] Set to TRUE if usebackq found
 *
 * Returns TRUE on success, FALSE on syntax error
 *
 */
static BOOL WCMD_parse_forf_options(WCHAR *options, WCHAR *eol, int *skip,
                                    WCHAR *delims, WCHAR *tokens, BOOL *usebackq)
{

  WCHAR *pos = options;
  int    len = strlenW(pos);
  static const WCHAR eolW[] = {'e','o','l','='};
  static const WCHAR skipW[] = {'s','k','i','p','='};
  static const WCHAR tokensW[] = {'t','o','k','e','n','s','='};
  static const WCHAR delimsW[] = {'d','e','l','i','m','s','='};
  static const WCHAR usebackqW[] = {'u','s','e','b','a','c','k','q'};
  static const WCHAR forf_defaultdelims[] = {' ', '\t', '\0'};
  static const WCHAR forf_defaulttokens[] = {'1', '\0'};

  /* Initialize to defaults */
  strcpyW(delims, forf_defaultdelims);
  strcpyW(tokens, forf_defaulttokens);
  *eol      = 0;
  *skip     = 0;
  *usebackq = FALSE;

  /* Strip (optional) leading and trailing quotes */
  if ((*pos == '"') && (pos[len-1] == '"')) {
    pos[len-1] = 0;
    pos++;
  }

  /* Process each keyword */
  while (pos && *pos) {
    if (*pos == ' ' || *pos == '\t') {
      pos++;

    /* Save End of line character (Ignore line if first token (based on delims) starts with it) */
    } else if (CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE | SORT_STRINGSORT,
                       pos, sizeof(eolW)/sizeof(WCHAR),
                       eolW, sizeof(eolW)/sizeof(WCHAR)) == CSTR_EQUAL) {
      *eol = *(pos + sizeof(eolW)/sizeof(WCHAR));
      pos = pos + sizeof(eolW)/sizeof(WCHAR) + 1;
      WINE_TRACE("Found eol as %c(%x)\n", *eol, *eol);

    /* Save number of lines to skip (Can be in base 10, hex (0x...) or octal (0xx) */
    } else if (CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE | SORT_STRINGSORT,
                       pos, sizeof(skipW)/sizeof(WCHAR),
                       skipW, sizeof(skipW)/sizeof(WCHAR)) == CSTR_EQUAL) {
      WCHAR *nextchar = NULL;
      pos = pos + sizeof(skipW)/sizeof(WCHAR);
      *skip = strtoulW(pos, &nextchar, 0);
      WINE_TRACE("Found skip as %d lines\n", *skip);
      pos = nextchar;

    /* Save if usebackq semantics are in effect */
    } else if (CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE | SORT_STRINGSORT,
                       pos, sizeof(usebackqW)/sizeof(WCHAR),
                       usebackqW, sizeof(usebackqW)/sizeof(WCHAR)) == CSTR_EQUAL) {
      *usebackq = TRUE;
      pos = pos + sizeof(usebackqW)/sizeof(WCHAR);
      WINE_FIXME("Found usebackq\n");

    /* Save the supplied delims. Slightly odd as space can be a delimiter but only
       if you finish the optionsroot string with delims= otherwise the space is
       just a token delimiter!                                                     */
    } else if (CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE | SORT_STRINGSORT,
                       pos, sizeof(delimsW)/sizeof(WCHAR),
                       delimsW, sizeof(delimsW)/sizeof(WCHAR)) == CSTR_EQUAL) {
      int i=0;

      pos = pos + sizeof(delimsW)/sizeof(WCHAR);
      while (*pos && *pos != ' ') {
        delims[i++] = *pos;
        pos++;
      }
      if (*pos==' ' && *(pos+1)==0) delims[i++] = *pos;
      delims[i++] = 0; /* Null terminate the delims */
      WINE_FIXME("Found delims as '%s'\n", wine_dbgstr_w(delims));

    /* Save the tokens being requested */
    } else if (CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE | SORT_STRINGSORT,
                       pos, sizeof(tokensW)/sizeof(WCHAR),
                       tokensW, sizeof(tokensW)/sizeof(WCHAR)) == CSTR_EQUAL) {
      int i=0;

      pos = pos + sizeof(tokensW)/sizeof(WCHAR);
      while (*pos && *pos != ' ') {
        tokens[i++] = *pos;
        pos++;
      }
      tokens[i++] = 0; /* Null terminate the tokens */
      WINE_FIXME("Found tokens as '%s'\n", wine_dbgstr_w(tokens));

    } else {
      WINE_WARN("Unexpected data in optionsroot: '%s'\n", wine_dbgstr_w(pos));
      return FALSE;
    }
  }
  return TRUE;
}

/*****************************************************************************
 * WCMD_add_dirstowalk
 *
 * When recursing through directories (for /r), we need to add to the list of
 * directories still to walk, any subdirectories of the one we are processing.
 *
 * Parameters
 *  options    [I] The remaining list of directories still to process
 *
 * Note this routine inserts the subdirectories found between the entry being
 * processed, and any other directory still to be processed, mimicing what
 * Windows does
 */
static void WCMD_add_dirstowalk(DIRECTORY_STACK *dirsToWalk) {
  DIRECTORY_STACK *remainingDirs = dirsToWalk;
  WCHAR fullitem[MAX_PATH];
  WIN32_FIND_DATAW fd;
  HANDLE hff;

  /* Build a generic search and add all directories on the list of directories
     still to walk                                                             */
  strcpyW(fullitem, dirsToWalk->dirName);
  strcatW(fullitem, slashstarW);
  hff = FindFirstFileW(fullitem, &fd);
  if (hff != INVALID_HANDLE_VALUE) {
    do {
      WINE_TRACE("Looking for subdirectories\n");
      if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
          (strcmpW(fd.cFileName, dotdotW) != 0) &&
          (strcmpW(fd.cFileName, dotW) != 0))
      {
        /* Allocate memory, add to list */
        DIRECTORY_STACK *toWalk = HeapAlloc(GetProcessHeap(), 0, sizeof(DIRECTORY_STACK));
        WINE_TRACE("(%p->%p)\n", remainingDirs, remainingDirs->next);
        toWalk->next = remainingDirs->next;
        remainingDirs->next = toWalk;
        remainingDirs = toWalk;
        toWalk->dirName = HeapAlloc(GetProcessHeap(), 0,
                                    sizeof(WCHAR) *
                                    (strlenW(dirsToWalk->dirName) + 2 + strlenW(fd.cFileName)));
        strcpyW(toWalk->dirName, dirsToWalk->dirName);
        strcatW(toWalk->dirName, slashW);
        strcatW(toWalk->dirName, fd.cFileName);
        WINE_TRACE("Added to stack %s (%p->%p)\n", wine_dbgstr_w(toWalk->dirName),
                   toWalk, toWalk->next);
      }
    } while (FindNextFileW(hff, &fd) != 0);
    WINE_TRACE("Finished adding all subdirectories\n");
    FindClose (hff);
  }
}

/**************************************************************************
 * WCMD_parse_line
 *
 * When parsing file or string contents (for /f), once the string to parse
 * has been identified, handle the various options and call the do part
 * if appropriate.
 *
 * Parameters:
 *  cmdStart     [I]    - Identifies the list of commands making up the
 *                           for loop body (especially if brackets in use)
 *  firstCmd     [I]    - The textual start of the command after the DO
 *                           which is within the first item of cmdStart
 *  cmdEnd       [O]    - Identifies where to continue after the DO
 *  variable     [I]    - The variable identified on the for line
 *  buffer       [I]    - The string to parse
 *  doExecuted   [O]    - Set to TRUE if the DO is ever executed once
 *  forf_skip    [I/O]  - How many lines to skip first
 *  forf_eol     [I]    - The 'end of line' (comment) character
 */
static void WCMD_parse_line(CMD_LIST    *cmdStart,
                            const WCHAR *firstCmd,
                            CMD_LIST   **cmdEnd,
                            const WCHAR *variable,
                            WCHAR       *buffer,
                            BOOL        *doExecuted,
                            int         *forf_skip,
                            WCHAR        forf_eol) {

  WCHAR *parm, *where;

  /* Skip lines if requested */
  if (*forf_skip) {
    (*forf_skip)--;
    return;
  }

  /* Extract the parameter */
  parm = WCMD_parameter (buffer, 0, &where, NULL, FALSE, FALSE);
  WINE_TRACE("Parsed parameter: %s from %s\n", wine_dbgstr_w(parm),
             wine_dbgstr_w(buffer));

  if (where && where[0] != forf_eol) {
    CMD_LIST *thisCmdStart = cmdStart;
    *doExecuted = TRUE;
    WCMD_part_execute(&thisCmdStart, firstCmd, variable, parm, FALSE, TRUE);
    *cmdEnd = thisCmdStart;
  }

}

/**************************************************************************
 * WCMD_for
 *
 * Batch file loop processing.
 *
 * On entry: cmdList       contains the syntax up to the set
 *           next cmdList and all in that bracket contain the set data
 *           next cmdlist  contains the DO cmd
 *           following that is either brackets or && entries (as per if)
 *
 */

void WCMD_for (WCHAR *p, CMD_LIST **cmdList) {

  WIN32_FIND_DATAW fd;
  HANDLE hff;
  int i;
  static const WCHAR inW[] = {'i','n'};
  static const WCHAR doW[] = {'d','o'};
  CMD_LIST *setStart, *thisSet, *cmdStart, *cmdEnd;
  WCHAR variable[4];
  WCHAR *firstCmd;
  int thisDepth;
  WCHAR optionsRoot[MAX_PATH];
  DIRECTORY_STACK *dirsToWalk = NULL;
  BOOL   expandDirs  = FALSE;
  BOOL   useNumbers  = FALSE;
  BOOL   doFileset   = FALSE;
  BOOL   doRecurse   = FALSE;
  BOOL   doExecuted  = FALSE;  /* Has the 'do' part been executed */
  LONG   numbers[3] = {0,0,0}; /* Defaults to 0 in native */
  int    itemNum;
  CMD_LIST *thisCmdStart;
  int    parameterNo = 0;
  WCHAR  forf_eol = 0;
  int    forf_skip = 0;
  WCHAR  forf_delims[256];
  WCHAR  forf_tokens[MAXSTRING];
  BOOL   forf_usebackq;

  /* Handle optional qualifiers (multiple are allowed) */
  WCHAR *thisArg = WCMD_parameter(p, parameterNo++, NULL, NULL, FALSE, FALSE);

  optionsRoot[0] = 0;
  while (thisArg && *thisArg == '/') {
      WINE_TRACE("Processing qualifier at %s\n", wine_dbgstr_w(thisArg));
      thisArg++;
      switch (toupperW(*thisArg)) {
      case 'D': expandDirs = TRUE; break;
      case 'L': useNumbers = TRUE; break;

      /* Recursive is special case - /R can have an optional path following it                */
      /* filenamesets are another special case - /F can have an optional options following it */
      case 'R':
      case 'F':
          {
              /* When recursing directories, use current directory as the starting point unless
                 subsequently overridden */
              doRecurse = (toupperW(*thisArg) == 'R');
              if (doRecurse) GetCurrentDirectoryW(sizeof(optionsRoot)/sizeof(WCHAR), optionsRoot);

              doFileset = (toupperW(*thisArg) == 'F');

              /* Retrieve next parameter to see if is root/options (raw form required
                 with for /f, or unquoted in for /r)                                  */
              thisArg = WCMD_parameter(p, parameterNo, NULL, NULL, doFileset, FALSE);

              /* Next parm is either qualifier, path/options or variable -
                 only care about it if it is the path/options              */
              if (thisArg && *thisArg != '/' && *thisArg != '%') {
                  parameterNo++;
                  strcpyW(optionsRoot, thisArg);
              }
              break;
          }
      default:
          WINE_FIXME("for qualifier '%c' unhandled\n", *thisArg);
      }

      /* Step to next token */
      thisArg = WCMD_parameter(p, parameterNo++, NULL, NULL, FALSE, FALSE);
  }

  /* Ensure line continues with variable */
  if (!*thisArg || *thisArg != '%') {
      WCMD_output_stderr (WCMD_LoadMessage(WCMD_SYNTAXERR));
      return;
  }

  /* With for /f parse the options if provided */
  if (doFileset) {
    if (!WCMD_parse_forf_options(optionsRoot, &forf_eol, &forf_skip,
                                 forf_delims, forf_tokens, &forf_usebackq))
    {
      WCMD_output_stderr (WCMD_LoadMessage(WCMD_SYNTAXERR));
      return;
    }

  /* Set up the list of directories to recurse if we are going to */
  } else if (doRecurse) {
       /* Allocate memory, add to list */
       dirsToWalk = HeapAlloc(GetProcessHeap(), 0, sizeof(DIRECTORY_STACK));
       dirsToWalk->next = NULL;
       dirsToWalk->dirName = HeapAlloc(GetProcessHeap(),0,
                                       (strlenW(optionsRoot) + 1) * sizeof(WCHAR));
       strcpyW(dirsToWalk->dirName, optionsRoot);
       WINE_TRACE("Starting with root directory %s\n", wine_dbgstr_w(dirsToWalk->dirName));
  }

  /* Variable should follow */
  strcpyW(variable, thisArg);
  WINE_TRACE("Variable identified as %s\n", wine_dbgstr_w(variable));

  /* Ensure line continues with IN */
  thisArg = WCMD_parameter(p, parameterNo++, NULL, NULL, FALSE, FALSE);
  if (!thisArg
       || !(CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE | SORT_STRINGSORT,
                           thisArg, sizeof(inW)/sizeof(inW[0]), inW,
                           sizeof(inW)/sizeof(inW[0])) == CSTR_EQUAL)) {
      WCMD_output_stderr (WCMD_LoadMessage(WCMD_SYNTAXERR));
      return;
  }

  /* Save away where the set of data starts and the variable */
  thisDepth = (*cmdList)->bracketDepth;
  *cmdList = (*cmdList)->nextcommand;
  setStart = (*cmdList);

  /* Skip until the close bracket */
  WINE_TRACE("Searching %p as the set\n", *cmdList);
  while (*cmdList &&
         (*cmdList)->command != NULL &&
         (*cmdList)->bracketDepth > thisDepth) {
    WINE_TRACE("Skipping %p which is part of the set\n", *cmdList);
    *cmdList = (*cmdList)->nextcommand;
  }

  /* Skip the close bracket, if there is one */
  if (*cmdList) *cmdList = (*cmdList)->nextcommand;

  /* Syntax error if missing close bracket, or nothing following it
     and once we have the complete set, we expect a DO              */
  WINE_TRACE("Looking for 'do ' in %p\n", *cmdList);
  if ((*cmdList == NULL)
      || !WCMD_keyword_ws_found(doW, sizeof(doW)/sizeof(doW[0]), (*cmdList)->command)) {

      WCMD_output_stderr (WCMD_LoadMessage(WCMD_SYNTAXERR));
      return;
  }

  cmdEnd   = *cmdList;

  /* Loop repeatedly per-directory we are potentially walking, when in for /r
     mode, or once for the rest of the time.                                  */
  do {

    /* Save away the starting position for the commands (and offset for the
       first one)                                                           */
    cmdStart = *cmdList;
    firstCmd = (*cmdList)->command + 3; /* Skip 'do ' */
    itemNum  = 0;

    /* If we are recursing directories (ie /R), add all sub directories now, then
       prefix the root when searching for the item */
    if (dirsToWalk) WCMD_add_dirstowalk(dirsToWalk);

    thisSet = setStart;
    /* Loop through all set entries */
    while (thisSet &&
           thisSet->command != NULL &&
           thisSet->bracketDepth >= thisDepth) {

      /* Loop through all entries on the same line */
      WCHAR *item;
      WCHAR *itemStart;
      WCHAR buffer[MAXSTRING];

      WINE_TRACE("Processing for set %p\n", thisSet);
      i = 0;
      while (*(item = WCMD_parameter (thisSet->command, i, &itemStart, NULL, TRUE, FALSE))) {

        /*
         * If the parameter within the set has a wildcard then search for matching files
         * otherwise do a literal substitution.
         */
        static const WCHAR wildcards[] = {'*','?','\0'};
        thisCmdStart = cmdStart;

        itemNum++;
        WINE_TRACE("Processing for item %d '%s'\n", itemNum, wine_dbgstr_w(item));

        if (!useNumbers && !doFileset) {
            WCHAR fullitem[MAX_PATH];

            /* Now build the item to use / search for in the specified directory,
               as it is fully qualified in the /R case */
            if (dirsToWalk) {
              strcpyW(fullitem, dirsToWalk->dirName);
              strcatW(fullitem, slashW);
              strcatW(fullitem, item);
            } else {
              strcpyW(fullitem, item);
            }

            if (strpbrkW (fullitem, wildcards)) {

              hff = FindFirstFileW(fullitem, &fd);
              if (hff != INVALID_HANDLE_VALUE) {
                do {
                  BOOL isDirectory = FALSE;

                  if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) isDirectory = TRUE;

                  /* Handle as files or dirs appropriately, but ignore . and .. */
                  if (isDirectory == expandDirs &&
                      (strcmpW(fd.cFileName, dotdotW) != 0) &&
                      (strcmpW(fd.cFileName, dotW) != 0))
                  {
                      thisCmdStart = cmdStart;
                      WINE_TRACE("Processing FOR filename %s\n", wine_dbgstr_w(fd.cFileName));

                      if (doRecurse) {
                          strcpyW(fullitem, dirsToWalk->dirName);
                          strcatW(fullitem, slashW);
                          strcatW(fullitem, fd.cFileName);
                      } else {
                          strcpyW(fullitem, fd.cFileName);
                      }
                      doExecuted = TRUE;
                      WCMD_part_execute (&thisCmdStart, firstCmd, variable,
                                                   fullitem, FALSE, TRUE);
                      cmdEnd = thisCmdStart;
                  }
                } while (FindNextFileW(hff, &fd) != 0);
                FindClose (hff);
              }
            } else {
              doExecuted = TRUE;
              WCMD_part_execute(&thisCmdStart, firstCmd, variable, fullitem, FALSE, TRUE);
              cmdEnd = thisCmdStart;
            }

        } else if (useNumbers) {
            /* Convert the first 3 numbers to signed longs and save */
            if (itemNum <=3) numbers[itemNum-1] = atolW(item);
            /* else ignore them! */

        /* Filesets - either a list of files, or a command to run and parse the output */
        } else if (doFileset && *itemStart != '"') {

            HANDLE input;
            WCHAR temp_file[MAX_PATH];

            WINE_TRACE("Processing for filespec from item %d '%s'\n", itemNum,
                       wine_dbgstr_w(item));

            /* If backquote or single quote, we need to launch that command
               and parse the results - use a temporary file                 */
            if (*itemStart == '`' || *itemStart == '\'') {

                WCHAR temp_path[MAX_PATH], temp_cmd[MAXSTRING];
                static const WCHAR redirOut[] = {'>','%','s','\0'};
                static const WCHAR cmdW[]     = {'C','M','D','\0'};

                /* Remove trailing character */
                itemStart[strlenW(itemStart)-1] = 0x00;

                /* Get temp filename */
                GetTempPathW(sizeof(temp_path)/sizeof(WCHAR), temp_path);
                GetTempFileNameW(temp_path, cmdW, 0, temp_file);

                /* Execute program and redirect output */
                wsprintfW(temp_cmd, redirOut, (itemStart+1), temp_file);
                WCMD_execute (itemStart, temp_cmd, NULL, NULL, NULL, FALSE);

                /* Open the file, read line by line and process */
                input = CreateFileW(temp_file, GENERIC_READ, FILE_SHARE_READ,
                                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            } else {

                /* Open the file, read line by line and process */
                input = CreateFileW(item, GENERIC_READ, FILE_SHARE_READ,
                                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            }

            /* Process the input file */
            if (input == INVALID_HANDLE_VALUE) {
              WCMD_print_error ();
              WCMD_output_stderr(WCMD_LoadMessage(WCMD_READFAIL), item);
              errorlevel = 1;
              return; /* FOR loop aborts at first failure here */

            } else {

              WCHAR buffer[MAXSTRING];

              while (WCMD_fgets(buffer, sizeof(buffer)/sizeof(WCHAR), input)) {
                WCMD_parse_line(cmdStart, firstCmd, &cmdEnd, variable, buffer, &doExecuted,
                                &forf_skip, forf_eol);
                buffer[0] = 0;
              }
              CloseHandle (input);
            }

            /* Delete the temporary file */
            if (*itemStart == '`' || *itemStart == '\'') {
                DeleteFileW(temp_file);
            }

        /* Filesets - A string literal */
        } else if (doFileset && *itemStart == '"') {

          /* Copy the item away from the global buffer used by WCMD_parameter */
          strcpyW(buffer, item);
          WCMD_parse_line(cmdStart, firstCmd, &cmdEnd, variable, buffer, &doExecuted,
                            &forf_skip, forf_eol);
        }

        WINE_TRACE("Post-command, cmdEnd = %p\n", cmdEnd);
        i++;
      }

      /* Move onto the next set line */
      thisSet = thisSet->nextcommand;
    }

    /* If /L is provided, now run the for loop */
    if (useNumbers) {
        WCHAR thisNum[20];
        static const WCHAR fmt[] = {'%','d','\0'};

        WINE_TRACE("FOR /L provided range from %d to %d step %d\n",
                   numbers[0], numbers[2], numbers[1]);
        for (i=numbers[0];
             (numbers[1]<0)? i>=numbers[2] : i<=numbers[2];
             i=i + numbers[1]) {

            sprintfW(thisNum, fmt, i);
            WINE_TRACE("Processing FOR number %s\n", wine_dbgstr_w(thisNum));

            thisCmdStart = cmdStart;
            doExecuted = TRUE;
            WCMD_part_execute(&thisCmdStart, firstCmd, variable, thisNum, FALSE, TRUE);
        }
        cmdEnd = thisCmdStart;
    }

    /* If we are walking directories, move on to any which remain */
    if (dirsToWalk != NULL) {
      DIRECTORY_STACK *nextDir = dirsToWalk->next;
      HeapFree(GetProcessHeap(), 0, dirsToWalk->dirName);
      HeapFree(GetProcessHeap(), 0, dirsToWalk);
      dirsToWalk = nextDir;
      if (dirsToWalk) WINE_TRACE("Moving to next directorty to iterate: %s\n",
                                 wine_dbgstr_w(dirsToWalk->dirName));
      else WINE_TRACE("Finished all directories.\n");
    }

  } while (dirsToWalk != NULL);

  /* Now skip over the do part if we did not perform the for loop so far.
     We store in cmdEnd the next command after the do block, but we only
     know this if something was run. If it has not been, we need to calculate
     it.                                                                      */
  if (!doExecuted) {
    thisCmdStart = cmdStart;
    WINE_TRACE("Skipping for loop commands due to no valid iterations\n");
    WCMD_part_execute(&thisCmdStart, firstCmd, NULL, NULL, FALSE, FALSE);
    cmdEnd = thisCmdStart;
  }

  /* When the loop ends, either something like a GOTO or EXIT /b has terminated
     all processing, OR it should be pointing to the end of && processing OR
     it should be pointing at the NULL end of bracket for the DO. The return
     value needs to be the NEXT command to execute, which it either is, or
     we need to step over the closing bracket                                  */
  *cmdList = cmdEnd;
  if (cmdEnd && cmdEnd->command == NULL) *cmdList = cmdEnd->nextcommand;
}

/**************************************************************************
 * WCMD_give_help
 *
 *	Simple on-line help. Help text is stored in the resource file.
 */

void WCMD_give_help (const WCHAR *args)
{
  size_t i;

  args = WCMD_skip_leading_spaces((WCHAR*) args);
  if (strlenW(args) == 0) {
    WCMD_output_asis (WCMD_LoadMessage(WCMD_ALLHELP));
  }
  else {
    /* Display help message for builtin commands */
    for (i=0; i<=WCMD_EXIT; i++) {
      if (CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE | SORT_STRINGSORT,
	  args, -1, inbuilt[i], -1) == CSTR_EQUAL) {
	WCMD_output_asis (WCMD_LoadMessage(i));
	return;
      }
    }
    /* Launch the command with the /? option for external commands shipped with cmd.exe */
    for (i = 0; i <= (sizeof(externals)/sizeof(externals[0])); i++) {
      if (CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE | SORT_STRINGSORT,
	  args, -1, externals[i], -1) == CSTR_EQUAL) {
        WCHAR cmd[128];
        static const WCHAR helpW[] = {' ', '/','?','\0'};
        strcpyW(cmd, args);
        strcatW(cmd, helpW);
        WCMD_run_program(cmd, FALSE);
        return;
      }
    }
    WCMD_output (WCMD_LoadMessage(WCMD_NOCMDHELP), args);
  }
  return;
}

/****************************************************************************
 * WCMD_go_to
 *
 * Batch file jump instruction. Not the most efficient algorithm ;-)
 * Prints error message if the specified label cannot be found - the file pointer is
 * then at EOF, effectively stopping the batch file.
 * FIXME: DOS is supposed to allow labels with spaces - we don't.
 */

void WCMD_goto (CMD_LIST **cmdList) {

  WCHAR string[MAX_PATH];
  WCHAR current[MAX_PATH];

  /* Do not process any more parts of a processed multipart or multilines command */
  if (cmdList) *cmdList = NULL;

  if (context != NULL) {
    WCHAR *paramStart = param1, *str;
    static const WCHAR eofW[] = {':','e','o','f','\0'};

    if (param1[0] == 0x00) {
      WCMD_output_stderr(WCMD_LoadMessage(WCMD_NOARG));
      return;
    }

    /* Handle special :EOF label */
    if (lstrcmpiW (eofW, param1) == 0) {
      context -> skip_rest = TRUE;
      return;
    }

    /* Support goto :label as well as goto label */
    if (*paramStart == ':') paramStart++;

    SetFilePointer (context -> h, 0, NULL, FILE_BEGIN);
    while (WCMD_fgets (string, sizeof(string)/sizeof(WCHAR), context -> h)) {
      str = string;
      while (isspaceW (*str)) str++;
      if (*str == ':') {
        DWORD index = 0;
        str++;
        while (((current[index] = str[index])) && (!isspaceW (current[index])))
            index++;

        /* ignore space at the end */
        current[index] = 0;
        if (lstrcmpiW (current, paramStart) == 0) return;
      }
    }
    WCMD_output_stderr(WCMD_LoadMessage(WCMD_NOTARGET));
  }
  return;
}

/*****************************************************************************
 * WCMD_pushd
 *
 *	Push a directory onto the stack
 */

void WCMD_pushd (const WCHAR *args)
{
    struct env_stack *curdir;
    WCHAR *thisdir;
    static const WCHAR parmD[] = {'/','D','\0'};

    if (strchrW(args, '/') != NULL) {
      SetLastError(ERROR_INVALID_PARAMETER);
      WCMD_print_error();
      return;
    }

    curdir  = LocalAlloc (LMEM_FIXED, sizeof (struct env_stack));
    thisdir = LocalAlloc (LMEM_FIXED, 1024 * sizeof(WCHAR));
    if( !curdir || !thisdir ) {
      LocalFree(curdir);
      LocalFree(thisdir);
      WINE_ERR ("out of memory\n");
      return;
    }

    /* Change directory using CD code with /D parameter */
    strcpyW(quals, parmD);
    GetCurrentDirectoryW (1024, thisdir);
    errorlevel = 0;
    WCMD_setshow_default(args);
    if (errorlevel) {
      LocalFree(curdir);
      LocalFree(thisdir);
      return;
    } else {
      curdir -> next    = pushd_directories;
      curdir -> strings = thisdir;
      if (pushd_directories == NULL) {
        curdir -> u.stackdepth = 1;
      } else {
        curdir -> u.stackdepth = pushd_directories -> u.stackdepth + 1;
      }
      pushd_directories = curdir;
    }
}


/*****************************************************************************
 * WCMD_popd
 *
 *	Pop a directory from the stack
 */

void WCMD_popd (void) {
    struct env_stack *temp = pushd_directories;

    if (!pushd_directories)
      return;

    /* pop the old environment from the stack, and make it the current dir */
    pushd_directories = temp->next;
    SetCurrentDirectoryW(temp->strings);
    LocalFree (temp->strings);
    LocalFree (temp);
}

/****************************************************************************
 * WCMD_if
 *
 * Batch file conditional.
 *
 * On entry, cmdlist will point to command containing the IF, and optionally
 *   the first command to execute (if brackets not found)
 *   If &&'s were found, this may be followed by a record flagged as isAmpersand
 *   If ('s were found, execute all within that bracket
 *   Command may optionally be followed by an ELSE - need to skip instructions
 *   in the else using the same logic
 *
 * FIXME: Much more syntax checking needed!
 */

void WCMD_if (WCHAR *p, CMD_LIST **cmdList) {

  int negate; /* Negate condition */
  int test;   /* Condition evaluation result */
  WCHAR condition[MAX_PATH], *command, *s;
  static const WCHAR notW[]    = {'n','o','t','\0'};
  static const WCHAR errlvlW[] = {'e','r','r','o','r','l','e','v','e','l','\0'};
  static const WCHAR existW[]  = {'e','x','i','s','t','\0'};
  static const WCHAR defdW[]   = {'d','e','f','i','n','e','d','\0'};
  static const WCHAR eqeqW[]   = {'=','=','\0'};
  static const WCHAR parmI[]   = {'/','I','\0'};
  int caseInsensitive = (strstrW(quals, parmI) != NULL);

  negate = !lstrcmpiW(param1,notW);
  strcpyW(condition, (negate ? param2 : param1));
  WINE_TRACE("Condition: %s\n", wine_dbgstr_w(condition));

  if (!lstrcmpiW (condition, errlvlW)) {
    WCHAR *param = WCMD_parameter(p, 1+negate, NULL, NULL, FALSE, FALSE);
    WCHAR *endptr;
    long int param_int = strtolW(param, &endptr, 10);
    if (*endptr) {
      WCMD_output_stderr(WCMD_LoadMessage(WCMD_SYNTAXERR));
      return;
    }
    test = ((long int)errorlevel >= param_int);
    WCMD_parameter(p, 2+negate, &command, NULL, FALSE, FALSE);
  }
  else if (!lstrcmpiW (condition, existW)) {
    test = (GetFileAttributesW(WCMD_parameter(p, 1+negate, NULL, NULL, FALSE, FALSE))
             != INVALID_FILE_ATTRIBUTES);
    WCMD_parameter(p, 2+negate, &command, NULL, FALSE, FALSE);
  }
  else if (!lstrcmpiW (condition, defdW)) {
    test = (GetEnvironmentVariableW(WCMD_parameter(p, 1+negate, NULL, NULL, FALSE, FALSE),
                                    NULL, 0) > 0);
    WCMD_parameter(p, 2+negate, &command, NULL, FALSE, FALSE);
  }
  else if ((s = strstrW (p, eqeqW))) {
    /* We need to get potential surrounding double quotes, so param1/2 can't be used */
    WCHAR *leftPart, *leftPartEnd, *rightPart, *rightPartEnd;
    s += 2;
    WCMD_parameter(p, 0+negate+caseInsensitive, &leftPart, &leftPartEnd, FALSE, FALSE);
    WCMD_parameter(p, 1+negate+caseInsensitive, &rightPart, &rightPartEnd, FALSE, FALSE);
    test = caseInsensitive
            ? (CompareStringW(LOCALE_SYSTEM_DEFAULT, NORM_IGNORECASE,
                              leftPart, leftPartEnd-leftPart+1,
                              rightPart, rightPartEnd-rightPart+1) == CSTR_EQUAL)
            : (CompareStringW(LOCALE_SYSTEM_DEFAULT, 0,
                              leftPart, leftPartEnd-leftPart+1,
                              rightPart, rightPartEnd-rightPart+1) == CSTR_EQUAL);
    WCMD_parameter(s, 1, &command, NULL, FALSE, FALSE);
  }
  else {
    WCMD_output_stderr(WCMD_LoadMessage(WCMD_SYNTAXERR));
    return;
  }

  /* Process rest of IF statement which is on the same line
     Note: This may process all or some of the cmdList (eg a GOTO) */
  WCMD_part_execute(cmdList, command, NULL, NULL, TRUE, (test != negate));
}

/****************************************************************************
 * WCMD_move
 *
 * Move a file, directory tree or wildcarded set of files.
 */

void WCMD_move (void)
{
  int             status;
  WIN32_FIND_DATAW fd;
  HANDLE          hff;
  WCHAR            input[MAX_PATH];
  WCHAR            output[MAX_PATH];
  WCHAR            drive[10];
  WCHAR            dir[MAX_PATH];
  WCHAR            fname[MAX_PATH];
  WCHAR            ext[MAX_PATH];

  if (param1[0] == 0x00) {
    WCMD_output_stderr(WCMD_LoadMessage(WCMD_NOARG));
    return;
  }

  /* If no destination supplied, assume current directory */
  if (param2[0] == 0x00) {
      strcpyW(param2, dotW);
  }

  /* If 2nd parm is directory, then use original filename */
  /* Convert partial path to full path */
  GetFullPathNameW(param1, sizeof(input)/sizeof(WCHAR), input, NULL);
  GetFullPathNameW(param2, sizeof(output)/sizeof(WCHAR), output, NULL);
  WINE_TRACE("Move from '%s'('%s') to '%s'\n", wine_dbgstr_w(input),
             wine_dbgstr_w(param1), wine_dbgstr_w(output));

  /* Split into components */
  WCMD_splitpath(input, drive, dir, fname, ext);

  hff = FindFirstFileW(input, &fd);
  if (hff == INVALID_HANDLE_VALUE)
    return;

  do {
    WCHAR  dest[MAX_PATH];
    WCHAR  src[MAX_PATH];
    DWORD attribs;
    BOOL ok = TRUE;

    WINE_TRACE("Processing file '%s'\n", wine_dbgstr_w(fd.cFileName));

    /* Build src & dest name */
    strcpyW(src, drive);
    strcatW(src, dir);

    /* See if dest is an existing directory */
    attribs = GetFileAttributesW(output);
    if (attribs != INVALID_FILE_ATTRIBUTES &&
       (attribs & FILE_ATTRIBUTE_DIRECTORY)) {
      strcpyW(dest, output);
      strcatW(dest, slashW);
      strcatW(dest, fd.cFileName);
    } else {
      strcpyW(dest, output);
    }

    strcatW(src, fd.cFileName);

    WINE_TRACE("Source '%s'\n", wine_dbgstr_w(src));
    WINE_TRACE("Dest   '%s'\n", wine_dbgstr_w(dest));

    /* If destination exists, prompt unless /Y supplied */
    if (GetFileAttributesW(dest) != INVALID_FILE_ATTRIBUTES) {
      BOOL force = FALSE;
      WCHAR copycmd[MAXSTRING];
      DWORD len;

      /* /-Y has the highest priority, then /Y and finally the COPYCMD env. variable */
      if (strstrW (quals, parmNoY))
        force = FALSE;
      else if (strstrW (quals, parmY))
        force = TRUE;
      else {
        static const WCHAR copyCmdW[] = {'C','O','P','Y','C','M','D','\0'};
        len = GetEnvironmentVariableW(copyCmdW, copycmd, sizeof(copycmd)/sizeof(WCHAR));
        force = (len && len < (sizeof(copycmd)/sizeof(WCHAR))
                     && ! lstrcmpiW (copycmd, parmY));
      }

      /* Prompt if overwriting */
      if (!force) {
        WCHAR* question;

        /* Ask for confirmation */
        question = WCMD_format_string(WCMD_LoadMessage(WCMD_OVERWRITE), dest);
        ok = WCMD_ask_confirm(question, FALSE, NULL);
        LocalFree(question);

        /* So delete the destination prior to the move */
        if (ok) {
          if (!DeleteFileW(dest)) {
            WCMD_print_error ();
            errorlevel = 1;
            ok = FALSE;
          }
        }
      }
    }

    if (ok) {
      status = MoveFileW(src, dest);
    } else {
      status = 1; /* Anything other than 0 to prevent error msg below */
    }

    if (!status) {
      WCMD_print_error ();
      errorlevel = 1;
    }
  } while (FindNextFileW(hff, &fd) != 0);

  FindClose(hff);
}

/****************************************************************************
 * WCMD_pause
 *
 * Suspend execution of a batch script until a key is typed
 */

void WCMD_pause (void)
{
  DWORD oldmode;
  BOOL have_console;
  DWORD count;
  WCHAR key;
  HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);

  have_console = GetConsoleMode(hIn, &oldmode);
  if (have_console)
      SetConsoleMode(hIn, 0);

  WCMD_output_asis(anykey);
  WCMD_ReadFile(hIn, &key, 1, &count);
  if (have_console)
    SetConsoleMode(hIn, oldmode);
}

/****************************************************************************
 * WCMD_remove_dir
 *
 * Delete a directory.
 */

void WCMD_remove_dir (WCHAR *args) {

  int   argno         = 0;
  int   argsProcessed = 0;
  WCHAR *argN          = args;
  static const WCHAR parmS[] = {'/','S','\0'};
  static const WCHAR parmQ[] = {'/','Q','\0'};

  /* Loop through all args */
  while (argN) {
    WCHAR *thisArg = WCMD_parameter (args, argno++, &argN, NULL, FALSE, FALSE);
    if (argN && argN[0] != '/') {
      WINE_TRACE("rd: Processing arg %s (quals:%s)\n", wine_dbgstr_w(thisArg),
                 wine_dbgstr_w(quals));
      argsProcessed++;

      /* If subdirectory search not supplied, just try to remove
         and report error if it fails (eg if it contains a file) */
      if (strstrW (quals, parmS) == NULL) {
        if (!RemoveDirectoryW(thisArg)) WCMD_print_error ();

      /* Otherwise use ShFileOp to recursively remove a directory */
      } else {

        SHFILEOPSTRUCTW lpDir;

        /* Ask first */
        if (strstrW (quals, parmQ) == NULL) {
          BOOL  ok;
          WCHAR  question[MAXSTRING];
          static const WCHAR fmt[] = {'%','s',' ','\0'};

          /* Ask for confirmation */
          wsprintfW(question, fmt, thisArg);
          ok = WCMD_ask_confirm(question, TRUE, NULL);

          /* Abort if answer is 'N' */
          if (!ok) return;
        }

        /* Do the delete */
        lpDir.hwnd   = NULL;
        lpDir.pTo    = NULL;
        lpDir.pFrom  = thisArg;
        lpDir.fFlags = FOF_SILENT | FOF_NOCONFIRMATION | FOF_NOERRORUI;
        lpDir.wFunc  = FO_DELETE;

        /* SHFileOperationW needs file list with a double null termination */
        thisArg[lstrlenW(thisArg) + 1] = 0x00;

        if (SHFileOperationW(&lpDir)) WCMD_print_error ();
      }
    }
  }

  /* Handle no valid args */
  if (argsProcessed == 0) {
    WCMD_output_stderr(WCMD_LoadMessage(WCMD_NOARG));
    return;
  }

}

/****************************************************************************
 * WCMD_rename
 *
 * Rename a file.
 */

void WCMD_rename (void)
{
  int             status;
  HANDLE          hff;
  WIN32_FIND_DATAW fd;
  WCHAR            input[MAX_PATH];
  WCHAR           *dotDst = NULL;
  WCHAR            drive[10];
  WCHAR            dir[MAX_PATH];
  WCHAR            fname[MAX_PATH];
  WCHAR            ext[MAX_PATH];

  errorlevel = 0;

  /* Must be at least two args */
  if (param1[0] == 0x00 || param2[0] == 0x00) {
    WCMD_output_stderr(WCMD_LoadMessage(WCMD_NOARG));
    errorlevel = 1;
    return;
  }

  /* Destination cannot contain a drive letter or directory separator */
  if ((strchrW(param2,':') != NULL) || (strchrW(param2,'\\') != NULL)) {
      SetLastError(ERROR_INVALID_PARAMETER);
      WCMD_print_error();
      errorlevel = 1;
      return;
  }

  /* Convert partial path to full path */
  GetFullPathNameW(param1, sizeof(input)/sizeof(WCHAR), input, NULL);
  WINE_TRACE("Rename from '%s'('%s') to '%s'\n", wine_dbgstr_w(input),
             wine_dbgstr_w(param1), wine_dbgstr_w(param2));
  dotDst = strchrW(param2, '.');

  /* Split into components */
  WCMD_splitpath(input, drive, dir, fname, ext);

  hff = FindFirstFileW(input, &fd);
  if (hff == INVALID_HANDLE_VALUE)
    return;

 do {
    WCHAR  dest[MAX_PATH];
    WCHAR  src[MAX_PATH];
    WCHAR *dotSrc = NULL;
    int   dirLen;

    WINE_TRACE("Processing file '%s'\n", wine_dbgstr_w(fd.cFileName));

    /* FIXME: If dest name or extension is *, replace with filename/ext
       part otherwise use supplied name. This supports:
          ren *.fred *.jim
          ren jim.* fred.* etc
       However, windows has a more complex algorithm supporting eg
          ?'s and *'s mid name                                         */
    dotSrc = strchrW(fd.cFileName, '.');

    /* Build src & dest name */
    strcpyW(src, drive);
    strcatW(src, dir);
    strcpyW(dest, src);
    dirLen = strlenW(src);
    strcatW(src, fd.cFileName);

    /* Build name */
    if (param2[0] == '*') {
      strcatW(dest, fd.cFileName);
      if (dotSrc) dest[dirLen + (dotSrc - fd.cFileName)] = 0x00;
    } else {
      strcatW(dest, param2);
      if (dotDst) dest[dirLen + (dotDst - param2)] = 0x00;
    }

    /* Build Extension */
    if (dotDst && (*(dotDst+1)=='*')) {
      if (dotSrc) strcatW(dest, dotSrc);
    } else if (dotDst) {
      if (dotDst) strcatW(dest, dotDst);
    }

    WINE_TRACE("Source '%s'\n", wine_dbgstr_w(src));
    WINE_TRACE("Dest   '%s'\n", wine_dbgstr_w(dest));

    status = MoveFileW(src, dest);

    if (!status) {
      WCMD_print_error ();
      errorlevel = 1;
    }
  } while (FindNextFileW(hff, &fd) != 0);

  FindClose(hff);
}

/*****************************************************************************
 * WCMD_dupenv
 *
 * Make a copy of the environment.
 */
static WCHAR *WCMD_dupenv( const WCHAR *env )
{
  WCHAR *env_copy;
  int len;

  if( !env )
    return NULL;

  len = 0;
  while ( env[len] )
    len += (strlenW(&env[len]) + 1);

  env_copy = LocalAlloc (LMEM_FIXED, (len+1) * sizeof (WCHAR) );
  if (!env_copy)
  {
    WINE_ERR("out of memory\n");
    return env_copy;
  }
  memcpy (env_copy, env, len*sizeof (WCHAR));
  env_copy[len] = 0;

  return env_copy;
}

/*****************************************************************************
 * WCMD_setlocal
 *
 *  setlocal pushes the environment onto a stack
 *  Save the environment as unicode so we don't screw anything up.
 */
void WCMD_setlocal (const WCHAR *s) {
  WCHAR *env;
  struct env_stack *env_copy;
  WCHAR cwd[MAX_PATH];

  /* setlocal does nothing outside of batch programs */
  if (!context) return;

  /* DISABLEEXTENSIONS ignored */

  env_copy = LocalAlloc (LMEM_FIXED, sizeof (struct env_stack));
  if( !env_copy )
  {
    WINE_ERR ("out of memory\n");
    return;
  }

  env = GetEnvironmentStringsW ();
  env_copy->strings = WCMD_dupenv (env);
  if (env_copy->strings)
  {
    env_copy->batchhandle = context->h;
    env_copy->next = saved_environment;
    saved_environment = env_copy;

    /* Save the current drive letter */
    GetCurrentDirectoryW(MAX_PATH, cwd);
    env_copy->u.cwd = cwd[0];
  }
  else
    LocalFree (env_copy);

  FreeEnvironmentStringsW (env);

}

/*****************************************************************************
 * WCMD_endlocal
 *
 *  endlocal pops the environment off a stack
 *  Note: When searching for '=', search from WCHAR position 1, to handle
 *        special internal environment variables =C:, =D: etc
 */
void WCMD_endlocal (void) {
  WCHAR *env, *old, *p;
  struct env_stack *temp;
  int len, n;

  /* setlocal does nothing outside of batch programs */
  if (!context) return;

  /* setlocal needs a saved environment from within the same context (batch
     program) as it was saved in                                            */
  if (!saved_environment || saved_environment->batchhandle != context->h)
    return;

  /* pop the old environment from the stack */
  temp = saved_environment;
  saved_environment = temp->next;

  /* delete the current environment, totally */
  env = GetEnvironmentStringsW ();
  old = WCMD_dupenv (GetEnvironmentStringsW ());
  len = 0;
  while (old[len]) {
    n = strlenW(&old[len]) + 1;
    p = strchrW(&old[len] + 1, '=');
    if (p)
    {
      *p++ = 0;
      SetEnvironmentVariableW (&old[len], NULL);
    }
    len += n;
  }
  LocalFree (old);
  FreeEnvironmentStringsW (env);

  /* restore old environment */
  env = temp->strings;
  len = 0;
  while (env[len]) {
    n = strlenW(&env[len]) + 1;
    p = strchrW(&env[len] + 1, '=');
    if (p)
    {
      *p++ = 0;
      SetEnvironmentVariableW (&env[len], p);
    }
    len += n;
  }

  /* Restore current drive letter */
  if (IsCharAlphaW(temp->u.cwd)) {
    WCHAR envvar[4];
    WCHAR cwd[MAX_PATH];
    static const WCHAR fmt[] = {'=','%','c',':','\0'};

    wsprintfW(envvar, fmt, temp->u.cwd);
    if (GetEnvironmentVariableW(envvar, cwd, MAX_PATH)) {
      WINE_TRACE("Resetting cwd to %s\n", wine_dbgstr_w(cwd));
      SetCurrentDirectoryW(cwd);
    }
  }

  LocalFree (env);
  LocalFree (temp);
}

/*****************************************************************************
 * WCMD_setshow_default
 *
 *	Set/Show the current default directory
 */

void WCMD_setshow_default (const WCHAR *args) {

  BOOL status;
  WCHAR string[1024];
  WCHAR cwd[1024];
  WCHAR *pos;
  WIN32_FIND_DATAW fd;
  HANDLE hff;
  static const WCHAR parmD[] = {'/','D','\0'};

  WINE_TRACE("Request change to directory '%s'\n", wine_dbgstr_w(args));

  /* Skip /D and trailing whitespace if on the front of the command line */
  if (CompareStringW(LOCALE_USER_DEFAULT,
                     NORM_IGNORECASE | SORT_STRINGSORT,
                     args, 2, parmD, -1) == CSTR_EQUAL) {
    args += 2;
    while (*args && (*args==' ' || *args=='\t'))
      args++;
  }

  GetCurrentDirectoryW(sizeof(cwd)/sizeof(WCHAR), cwd);
  if (strlenW(args) == 0) {
    strcatW (cwd, newlineW);
    WCMD_output_asis (cwd);
  }
  else {
    /* Remove any double quotes, which may be in the
       middle, eg. cd "C:\Program Files"\Microsoft is ok */
    pos = string;
    while (*args) {
      if (*args != '"') *pos++ = *args;
      args++;
    }
    while (pos > string && (*(pos-1) == ' ' || *(pos-1) == '\t'))
      pos--;
    *pos = 0x00;

    /* Search for appropriate directory */
    WINE_TRACE("Looking for directory '%s'\n", wine_dbgstr_w(string));
    hff = FindFirstFileW(string, &fd);
    if (hff != INVALID_HANDLE_VALUE) {
      do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
          WCHAR fpath[MAX_PATH];
          WCHAR drive[10];
          WCHAR dir[MAX_PATH];
          WCHAR fname[MAX_PATH];
          WCHAR ext[MAX_PATH];
          static const WCHAR fmt[] = {'%','s','%','s','%','s','\0'};

          /* Convert path into actual directory spec */
          GetFullPathNameW(string, sizeof(fpath)/sizeof(WCHAR), fpath, NULL);
          WCMD_splitpath(fpath, drive, dir, fname, ext);

          /* Rebuild path */
          wsprintfW(string, fmt, drive, dir, fd.cFileName);
          break;
        }
      } while (FindNextFileW(hff, &fd) != 0);
      FindClose(hff);
    }

    /* Change to that directory */
    WINE_TRACE("Really changing to directory '%s'\n", wine_dbgstr_w(string));

    status = SetCurrentDirectoryW(string);
    if (!status) {
      errorlevel = 1;
      WCMD_print_error ();
      return;
    } else {

      /* Save away the actual new directory, to store as current location */
      GetCurrentDirectoryW (sizeof(string)/sizeof(WCHAR), string);

      /* Restore old directory if drive letter would change, and
           CD x:\directory /D (or pushd c:\directory) not supplied */
      if ((strstrW(quals, parmD) == NULL) &&
          (param1[1] == ':') && (toupper(param1[0]) != toupper(cwd[0]))) {
        SetCurrentDirectoryW(cwd);
      }
    }

    /* Set special =C: type environment variable, for drive letter of
       change of directory, even if path was restored due to missing
       /D (allows changing drive letter when not resident on that
       drive                                                          */
    if ((string[1] == ':') && IsCharAlphaW(string[0])) {
      WCHAR env[4];
      strcpyW(env, equalW);
      memcpy(env+1, string, 2 * sizeof(WCHAR));
      env[3] = 0x00;
      WINE_TRACE("Setting '%s' to '%s'\n", wine_dbgstr_w(env), wine_dbgstr_w(string));
      SetEnvironmentVariableW(env, string);
    }

   }
  return;
}

/****************************************************************************
 * WCMD_setshow_date
 *
 * Set/Show the system date
 * FIXME: Can't change date yet
 */

void WCMD_setshow_date (void) {

  WCHAR curdate[64], buffer[64];
  DWORD count;
  static const WCHAR parmT[] = {'/','T','\0'};

  if (strlenW(param1) == 0) {
    if (GetDateFormatW(LOCALE_USER_DEFAULT, 0, NULL, NULL,
		curdate, sizeof(curdate)/sizeof(WCHAR))) {
      WCMD_output (WCMD_LoadMessage(WCMD_CURRENTDATE), curdate);
      if (strstrW (quals, parmT) == NULL) {
        WCMD_output (WCMD_LoadMessage(WCMD_NEWDATE));
        WCMD_ReadFile(GetStdHandle(STD_INPUT_HANDLE), buffer, sizeof(buffer)/sizeof(WCHAR), &count);
        if (count > 2) {
          WCMD_output_stderr (WCMD_LoadMessage(WCMD_NYI));
        }
      }
    }
    else WCMD_print_error ();
  }
  else {
    WCMD_output_stderr (WCMD_LoadMessage(WCMD_NYI));
  }
}

/****************************************************************************
 * WCMD_compare
 * Note: Native displays 'fred' before 'fred ', so need to only compare up to
 *       the equals sign.
 */
static int WCMD_compare( const void *a, const void *b )
{
    int r;
    const WCHAR * const *str_a = a, * const *str_b = b;
    r = CompareStringW( LOCALE_USER_DEFAULT, NORM_IGNORECASE | SORT_STRINGSORT,
	  *str_a, strcspnW(*str_a, equalW), *str_b, strcspnW(*str_b, equalW) );
    if( r == CSTR_LESS_THAN ) return -1;
    if( r == CSTR_GREATER_THAN ) return 1;
    return 0;
}

/****************************************************************************
 * WCMD_setshow_sortenv
 *
 * sort variables into order for display
 * Optionally only display those who start with a stub
 * returns the count displayed
 */
static int WCMD_setshow_sortenv(const WCHAR *s, const WCHAR *stub)
{
  UINT count=0, len=0, i, displayedcount=0, stublen=0;
  const WCHAR **str;

  if (stub) stublen = strlenW(stub);

  /* count the number of strings, and the total length */
  while ( s[len] ) {
    len += (strlenW(&s[len]) + 1);
    count++;
  }

  /* add the strings to an array */
  str = LocalAlloc (LMEM_FIXED | LMEM_ZEROINIT, count * sizeof (WCHAR*) );
  if( !str )
    return 0;
  str[0] = s;
  for( i=1; i<count; i++ )
    str[i] = str[i-1] + strlenW(str[i-1]) + 1;

  /* sort the array */
  qsort( str, count, sizeof (WCHAR*), WCMD_compare );

  /* print it */
  for( i=0; i<count; i++ ) {
    if (!stub || CompareStringW(LOCALE_USER_DEFAULT,
                                NORM_IGNORECASE | SORT_STRINGSORT,
                                str[i], stublen, stub, -1) == CSTR_EQUAL) {
      /* Don't display special internal variables */
      if (str[i][0] != '=') {
        WCMD_output_asis(str[i]);
        WCMD_output_asis(newlineW);
        displayedcount++;
      }
    }
  }

  LocalFree( str );
  return displayedcount;
}

/****************************************************************************
 * WCMD_setshow_env
 *
 * Set/Show the environment variables
 */

void WCMD_setshow_env (WCHAR *s) {

  LPVOID env;
  WCHAR *p;
  int status;
  static const WCHAR parmP[] = {'/','P','\0'};

  if (param1[0] == 0x00 && quals[0] == 0x00) {
    env = GetEnvironmentStringsW();
    WCMD_setshow_sortenv( env, NULL );
    return;
  }

  /* See if /P supplied, and if so echo the prompt, and read in a reply */
  if (CompareStringW(LOCALE_USER_DEFAULT,
                     NORM_IGNORECASE | SORT_STRINGSORT,
                     s, 2, parmP, -1) == CSTR_EQUAL) {
    WCHAR string[MAXSTRING];
    DWORD count;

    s += 2;
    while (*s && (*s==' ' || *s=='\t')) s++;
    if (*s=='\"')
        WCMD_strip_quotes(s);

    /* If no parameter, or no '=' sign, return an error */
    if (!(*s) || ((p = strchrW (s, '=')) == NULL )) {
      WCMD_output_stderr(WCMD_LoadMessage(WCMD_NOARG));
      return;
    }

    /* Output the prompt */
    *p++ = '\0';
    if (strlenW(p) != 0) WCMD_output_asis(p);

    /* Read the reply */
    WCMD_ReadFile(GetStdHandle(STD_INPUT_HANDLE), string, sizeof(string)/sizeof(WCHAR), &count);
    if (count > 1) {
      string[count-1] = '\0'; /* ReadFile output is not null-terminated! */
      if (string[count-2] == '\r') string[count-2] = '\0'; /* Under Windoze we get CRLF! */
      WINE_TRACE("set /p: Setting var '%s' to '%s'\n", wine_dbgstr_w(s),
                 wine_dbgstr_w(string));
      status = SetEnvironmentVariableW(s, string);
    }

  } else {
    DWORD gle;

    if (*s=='\"')
        WCMD_strip_quotes(s);
    p = strchrW (s, '=');
    if (p == NULL) {
      env = GetEnvironmentStringsW();
      if (WCMD_setshow_sortenv( env, s ) == 0) {
        WCMD_output_stderr(WCMD_LoadMessage(WCMD_MISSINGENV), s);
        errorlevel = 1;
      }
      return;
    }
    *p++ = '\0';

    if (strlenW(p) == 0) p = NULL;
    WINE_TRACE("set: Setting var '%s' to '%s'\n", wine_dbgstr_w(s),
               wine_dbgstr_w(p));
    status = SetEnvironmentVariableW(s, p);
    gle = GetLastError();
    if ((!status) & (gle == ERROR_ENVVAR_NOT_FOUND)) {
      errorlevel = 1;
    } else if ((!status)) WCMD_print_error();
    else errorlevel = 0;
  }
}

/****************************************************************************
 * WCMD_setshow_path
 *
 * Set/Show the path environment variable
 */

void WCMD_setshow_path (const WCHAR *args) {

  WCHAR string[1024];
  DWORD status;
  static const WCHAR pathW[] = {'P','A','T','H','\0'};
  static const WCHAR pathEqW[] = {'P','A','T','H','=','\0'};

  if (strlenW(param1) == 0 && strlenW(param2) == 0) {
    status = GetEnvironmentVariableW(pathW, string, sizeof(string)/sizeof(WCHAR));
    if (status != 0) {
      WCMD_output_asis ( pathEqW);
      WCMD_output_asis ( string);
      WCMD_output_asis ( newlineW);
    }
    else {
      WCMD_output_stderr(WCMD_LoadMessage(WCMD_NOPATH));
    }
  }
  else {
    if (*args == '=') args++; /* Skip leading '=' */
    status = SetEnvironmentVariableW(pathW, args);
    if (!status) WCMD_print_error();
  }
}

/****************************************************************************
 * WCMD_setshow_prompt
 *
 * Set or show the command prompt.
 */

void WCMD_setshow_prompt (void) {

  WCHAR *s;
  static const WCHAR promptW[] = {'P','R','O','M','P','T','\0'};

  if (strlenW(param1) == 0) {
    SetEnvironmentVariableW(promptW, NULL);
  }
  else {
    s = param1;
    while ((*s == '=') || (*s == ' ') || (*s == '\t')) s++;
    if (strlenW(s) == 0) {
      SetEnvironmentVariableW(promptW, NULL);
    }
    else SetEnvironmentVariableW(promptW, s);
  }
}

/****************************************************************************
 * WCMD_setshow_time
 *
 * Set/Show the system time
 * FIXME: Can't change time yet
 */

void WCMD_setshow_time (void) {

  WCHAR curtime[64], buffer[64];
  DWORD count;
  SYSTEMTIME st;
  static const WCHAR parmT[] = {'/','T','\0'};

  if (strlenW(param1) == 0) {
    GetLocalTime(&st);
    if (GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &st, NULL,
		curtime, sizeof(curtime)/sizeof(WCHAR))) {
      WCMD_output (WCMD_LoadMessage(WCMD_CURRENTTIME), curtime);
      if (strstrW (quals, parmT) == NULL) {
        WCMD_output (WCMD_LoadMessage(WCMD_NEWTIME));
        WCMD_ReadFile(GetStdHandle(STD_INPUT_HANDLE), buffer, sizeof(buffer)/sizeof(WCHAR), &count);
        if (count > 2) {
          WCMD_output_stderr (WCMD_LoadMessage(WCMD_NYI));
        }
      }
    }
    else WCMD_print_error ();
  }
  else {
    WCMD_output_stderr (WCMD_LoadMessage(WCMD_NYI));
  }
}

/****************************************************************************
 * WCMD_shift
 *
 * Shift batch parameters.
 * Optional /n says where to start shifting (n=0-8)
 */

void WCMD_shift (const WCHAR *args) {
  int start;

  if (context != NULL) {
    WCHAR *pos = strchrW(args, '/');
    int   i;

    if (pos == NULL) {
      start = 0;
    } else if (*(pos+1)>='0' && *(pos+1)<='8') {
      start = (*(pos+1) - '0');
    } else {
      SetLastError(ERROR_INVALID_PARAMETER);
      WCMD_print_error();
      return;
    }

    WINE_TRACE("Shifting variables, starting at %d\n", start);
    for (i=start;i<=8;i++) {
      context -> shift_count[i] = context -> shift_count[i+1] + 1;
    }
    context -> shift_count[9] = context -> shift_count[9] + 1;
  }

}

/****************************************************************************
 * WCMD_start
 */
void WCMD_start(const WCHAR *args)
{
    static const WCHAR exeW[] = {'\\','c','o','m','m','a','n','d',
                                 '\\','s','t','a','r','t','.','e','x','e',0};
    WCHAR file[MAX_PATH];
    WCHAR *cmdline;
    STARTUPINFOW st;
    PROCESS_INFORMATION pi;

    GetWindowsDirectoryW( file, MAX_PATH );
    strcatW( file, exeW );
    cmdline = HeapAlloc( GetProcessHeap(), 0, (strlenW(file) + strlenW(args) + 2) * sizeof(WCHAR) );
    strcpyW( cmdline, file );
    strcatW( cmdline, spaceW );
    strcatW( cmdline, args );

    memset( &st, 0, sizeof(STARTUPINFOW) );
    st.cb = sizeof(STARTUPINFOW);

    if (CreateProcessW( file, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &st, &pi ))
    {
        WaitForSingleObject( pi.hProcess, INFINITE );
        GetExitCodeProcess( pi.hProcess, &errorlevel );
        if (errorlevel == STILL_ACTIVE) errorlevel = 0;
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    else
    {
        SetLastError(ERROR_FILE_NOT_FOUND);
        WCMD_print_error ();
        errorlevel = 9009;
    }
    HeapFree( GetProcessHeap(), 0, cmdline );
}

/****************************************************************************
 * WCMD_title
 *
 * Set the console title
 */
void WCMD_title (const WCHAR *args) {
  SetConsoleTitleW(args);
}

/****************************************************************************
 * WCMD_type
 *
 * Copy a file to standard output.
 */

void WCMD_type (WCHAR *args) {

  int   argno         = 0;
  WCHAR *argN          = args;
  BOOL  writeHeaders  = FALSE;

  if (param1[0] == 0x00) {
    WCMD_output_stderr(WCMD_LoadMessage(WCMD_NOARG));
    return;
  }

  if (param2[0] != 0x00) writeHeaders = TRUE;

  /* Loop through all args */
  errorlevel = 0;
  while (argN) {
    WCHAR *thisArg = WCMD_parameter (args, argno++, &argN, NULL, FALSE, FALSE);

    HANDLE h;
    WCHAR buffer[512];
    DWORD count;

    if (!argN) break;

    WINE_TRACE("type: Processing arg '%s'\n", wine_dbgstr_w(thisArg));
    h = CreateFileW(thisArg, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
      WCMD_print_error ();
      WCMD_output_stderr(WCMD_LoadMessage(WCMD_READFAIL), thisArg);
      errorlevel = 1;
    } else {
      if (writeHeaders) {
        static const WCHAR fmt[] = {'\n','%','1','\n','\n','\0'};
        WCMD_output(fmt, thisArg);
      }
      while (WCMD_ReadFile(h, buffer, sizeof(buffer)/sizeof(WCHAR) - 1, &count)) {
        if (count == 0) break;	/* ReadFile reports success on EOF! */
        buffer[count] = 0;
        WCMD_output_asis (buffer);
      }
      CloseHandle (h);
    }
  }
}

/****************************************************************************
 * WCMD_more
 *
 * Output either a file or stdin to screen in pages
 */

void WCMD_more (WCHAR *args) {

  int   argno         = 0;
  WCHAR *argN         = args;
  WCHAR  moreStr[100];
  WCHAR  moreStrPage[100];
  WCHAR  buffer[512];
  DWORD count;
  static const WCHAR moreStart[] = {'-','-',' ','\0'};
  static const WCHAR moreFmt[]   = {'%','s',' ','-','-','\n','\0'};
  static const WCHAR moreFmt2[]  = {'%','s',' ','(','%','2','.','2','d','%','%',
                                    ')',' ','-','-','\n','\0'};
  static const WCHAR conInW[]    = {'C','O','N','I','N','$','\0'};

  /* Prefix the NLS more with '-- ', then load the text */
  errorlevel = 0;
  strcpyW(moreStr, moreStart);
  LoadStringW(hinst, WCMD_MORESTR, &moreStr[3],
              (sizeof(moreStr)/sizeof(WCHAR))-3);

  if (param1[0] == 0x00) {

    /* Wine implements pipes via temporary files, and hence stdin is
       effectively reading from the file. This means the prompts for
       more are satisfied by the next line from the input (file). To
       avoid this, ensure stdin is to the console                    */
    HANDLE hstdin  = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hConIn = CreateFileW(conInW, GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ, NULL, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, 0);
    WINE_TRACE("No parms - working probably in pipe mode\n");
    SetStdHandle(STD_INPUT_HANDLE, hConIn);

    /* Warning: No easy way of ending the stream (ctrl+z on windows) so
       once you get in this bit unless due to a pipe, its going to end badly...  */
    wsprintfW(moreStrPage, moreFmt, moreStr);

    WCMD_enter_paged_mode(moreStrPage);
    while (WCMD_ReadFile(hstdin, buffer, (sizeof(buffer)/sizeof(WCHAR))-1, &count)) {
      if (count == 0) break;	/* ReadFile reports success on EOF! */
      buffer[count] = 0;
      WCMD_output_asis (buffer);
    }
    WCMD_leave_paged_mode();

    /* Restore stdin to what it was */
    SetStdHandle(STD_INPUT_HANDLE, hstdin);
    CloseHandle(hConIn);

    return;
  } else {
    BOOL needsPause = FALSE;

    /* Loop through all args */
    WINE_TRACE("Parms supplied - working through each file\n");
    WCMD_enter_paged_mode(moreStrPage);

    while (argN) {
      WCHAR *thisArg = WCMD_parameter (args, argno++, &argN, NULL, FALSE, FALSE);
      HANDLE h;

      if (!argN) break;

      if (needsPause) {

        /* Wait */
        wsprintfW(moreStrPage, moreFmt2, moreStr, 100);
        WCMD_leave_paged_mode();
        WCMD_output_asis(moreStrPage);
        WCMD_ReadFile(GetStdHandle(STD_INPUT_HANDLE), buffer, sizeof(buffer)/sizeof(WCHAR), &count);
        WCMD_enter_paged_mode(moreStrPage);
      }


      WINE_TRACE("more: Processing arg '%s'\n", wine_dbgstr_w(thisArg));
      h = CreateFileW(thisArg, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL, NULL);
      if (h == INVALID_HANDLE_VALUE) {
        WCMD_print_error ();
        WCMD_output_stderr(WCMD_LoadMessage(WCMD_READFAIL), thisArg);
        errorlevel = 1;
      } else {
        ULONG64 curPos  = 0;
        ULONG64 fileLen = 0;
        WIN32_FILE_ATTRIBUTE_DATA   fileInfo;

        /* Get the file size */
        GetFileAttributesExW(thisArg, GetFileExInfoStandard, (void*)&fileInfo);
        fileLen = (((ULONG64)fileInfo.nFileSizeHigh) << 32) + fileInfo.nFileSizeLow;

        needsPause = TRUE;
        while (WCMD_ReadFile(h, buffer, (sizeof(buffer)/sizeof(WCHAR))-1, &count)) {
          if (count == 0) break;	/* ReadFile reports success on EOF! */
          buffer[count] = 0;
          curPos += count;

          /* Update % count (would be used in WCMD_output_asis as prompt) */
          wsprintfW(moreStrPage, moreFmt2, moreStr, (int) min(99, (curPos * 100)/fileLen));

          WCMD_output_asis (buffer);
        }
        CloseHandle (h);
      }
    }

    WCMD_leave_paged_mode();
  }
}

/****************************************************************************
 * WCMD_verify
 *
 * Display verify flag.
 * FIXME: We don't actually do anything with the verify flag other than toggle
 * it...
 */

void WCMD_verify (const WCHAR *args) {

  int count;

  count = strlenW(args);
  if (count == 0) {
    if (verify_mode) WCMD_output (WCMD_LoadMessage(WCMD_VERIFYPROMPT), onW);
    else WCMD_output (WCMD_LoadMessage(WCMD_VERIFYPROMPT), offW);
    return;
  }
  if (lstrcmpiW(args, onW) == 0) {
    verify_mode = TRUE;
    return;
  }
  else if (lstrcmpiW(args, offW) == 0) {
    verify_mode = FALSE;
    return;
  }
  else WCMD_output_stderr(WCMD_LoadMessage(WCMD_VERIFYERR));
}

/****************************************************************************
 * WCMD_version
 *
 * Display version info.
 */

void WCMD_version (void) {

  WCMD_output_asis (version_string);

}

/****************************************************************************
 * WCMD_volume
 *
 * Display volume information (set_label = FALSE)
 * Additionally set volume label (set_label = TRUE)
 * Returns 1 on success, 0 otherwise
 */

int WCMD_volume(BOOL set_label, const WCHAR *path)
{
  DWORD count, serial;
  WCHAR string[MAX_PATH], label[MAX_PATH], curdir[MAX_PATH];
  BOOL status;

  if (strlenW(path) == 0) {
    status = GetCurrentDirectoryW(sizeof(curdir)/sizeof(WCHAR), curdir);
    if (!status) {
      WCMD_print_error ();
      return 0;
    }
    status = GetVolumeInformationW(NULL, label, sizeof(label)/sizeof(WCHAR),
                                   &serial, NULL, NULL, NULL, 0);
  }
  else {
    static const WCHAR fmt[] = {'%','s','\\','\0'};
    if ((path[1] != ':') || (strlenW(path) != 2)) {
      WCMD_output_stderr(WCMD_LoadMessage(WCMD_SYNTAXERR));
      return 0;
    }
    wsprintfW (curdir, fmt, path);
    status = GetVolumeInformationW(curdir, label, sizeof(label)/sizeof(WCHAR),
                                   &serial, NULL,
    	NULL, NULL, 0);
  }
  if (!status) {
    WCMD_print_error ();
    return 0;
  }
  if (label[0] != '\0') {
    WCMD_output (WCMD_LoadMessage(WCMD_VOLUMELABEL),
      	curdir[0], label);
  }
  else {
    WCMD_output (WCMD_LoadMessage(WCMD_VOLUMENOLABEL),
      	curdir[0]);
  }
  WCMD_output (WCMD_LoadMessage(WCMD_VOLUMESERIALNO),
    	HIWORD(serial), LOWORD(serial));
  if (set_label) {
    WCMD_output (WCMD_LoadMessage(WCMD_VOLUMEPROMPT));
    WCMD_ReadFile(GetStdHandle(STD_INPUT_HANDLE), string, sizeof(string)/sizeof(WCHAR), &count);
    if (count > 1) {
      string[count-1] = '\0';		/* ReadFile output is not null-terminated! */
      if (string[count-2] == '\r') string[count-2] = '\0'; /* Under Windoze we get CRLF! */
    }
    if (strlenW(path) != 0) {
      if (!SetVolumeLabelW(curdir, string)) WCMD_print_error ();
    }
    else {
      if (!SetVolumeLabelW(NULL, string)) WCMD_print_error ();
    }
  }
  return 1;
}

/**************************************************************************
 * WCMD_exit
 *
 * Exit either the process, or just this batch program
 *
 */

void WCMD_exit (CMD_LIST **cmdList) {

    static const WCHAR parmB[] = {'/','B','\0'};
    int rc = atoiW(param1); /* Note: atoi of empty parameter is 0 */

    if (context && lstrcmpiW(quals, parmB) == 0) {
        errorlevel = rc;
        context -> skip_rest = TRUE;
        *cmdList = NULL;
    } else {
        ExitProcess(rc);
    }
}


/*****************************************************************************
 * WCMD_assoc
 *
 *	Lists or sets file associations  (assoc = TRUE)
 *      Lists or sets file types         (assoc = FALSE)
 */
void WCMD_assoc (const WCHAR *args, BOOL assoc) {

    HKEY    key;
    DWORD   accessOptions = KEY_READ;
    WCHAR   *newValue;
    LONG    rc = ERROR_SUCCESS;
    WCHAR    keyValue[MAXSTRING];
    DWORD   valueLen = MAXSTRING;
    HKEY    readKey;
    static const WCHAR shOpCmdW[] = {'\\','S','h','e','l','l','\\',
                                     'O','p','e','n','\\','C','o','m','m','a','n','d','\0'};

    /* See if parameter includes '=' */
    errorlevel = 0;
    newValue = strchrW(args, '=');
    if (newValue) accessOptions |= KEY_WRITE;

    /* Open a key to HKEY_CLASSES_ROOT for enumerating */
    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, nullW, 0,
                      accessOptions, &key) != ERROR_SUCCESS) {
      WINE_FIXME("Unexpected failure opening HKCR key: %d\n", GetLastError());
      return;
    }

    /* If no parameters then list all associations */
    if (*args == 0x00) {
      int index = 0;

      /* Enumerate all the keys */
      while (rc != ERROR_NO_MORE_ITEMS) {
        WCHAR  keyName[MAXSTRING];
        DWORD nameLen;

        /* Find the next value */
        nameLen = MAXSTRING;
        rc = RegEnumKeyExW(key, index++, keyName, &nameLen, NULL, NULL, NULL, NULL);

        if (rc == ERROR_SUCCESS) {

          /* Only interested in extension ones if assoc, or others
             if not assoc                                          */
          if ((keyName[0] == '.' && assoc) ||
              (!(keyName[0] == '.') && (!assoc)))
          {
            WCHAR subkey[MAXSTRING];
            strcpyW(subkey, keyName);
            if (!assoc) strcatW(subkey, shOpCmdW);

            if (RegOpenKeyExW(key, subkey, 0, accessOptions, &readKey) == ERROR_SUCCESS) {

              valueLen = sizeof(keyValue)/sizeof(WCHAR);
              rc = RegQueryValueExW(readKey, NULL, NULL, NULL, (LPBYTE)keyValue, &valueLen);
              WCMD_output_asis(keyName);
              WCMD_output_asis(equalW);
              /* If no default value found, leave line empty after '=' */
              if (rc == ERROR_SUCCESS) {
                WCMD_output_asis(keyValue);
              }
              WCMD_output_asis(newlineW);
              RegCloseKey(readKey);
            }
          }
        }
      }

    } else {

      /* Parameter supplied - if no '=' on command line, its a query */
      if (newValue == NULL) {
        WCHAR *space;
        WCHAR subkey[MAXSTRING];

        /* Query terminates the parameter at the first space */
        strcpyW(keyValue, args);
        space = strchrW(keyValue, ' ');
        if (space) *space=0x00;

        /* Set up key name */
        strcpyW(subkey, keyValue);
        if (!assoc) strcatW(subkey, shOpCmdW);

        if (RegOpenKeyExW(key, subkey, 0, accessOptions, &readKey) == ERROR_SUCCESS) {

          rc = RegQueryValueExW(readKey, NULL, NULL, NULL, (LPBYTE)keyValue, &valueLen);
          WCMD_output_asis(args);
          WCMD_output_asis(equalW);
          /* If no default value found, leave line empty after '=' */
          if (rc == ERROR_SUCCESS) WCMD_output_asis(keyValue);
          WCMD_output_asis(newlineW);
          RegCloseKey(readKey);

        } else {
          WCHAR  msgbuffer[MAXSTRING];

          /* Load the translated 'File association not found' */
          if (assoc) {
            LoadStringW(hinst, WCMD_NOASSOC, msgbuffer, sizeof(msgbuffer)/sizeof(WCHAR));
          } else {
            LoadStringW(hinst, WCMD_NOFTYPE, msgbuffer, sizeof(msgbuffer)/sizeof(WCHAR));
          }
          WCMD_output_stderr(msgbuffer, keyValue);
          errorlevel = 2;
        }

      /* Not a query - its a set or clear of a value */
      } else {

        WCHAR subkey[MAXSTRING];

        /* Get pointer to new value */
        *newValue = 0x00;
        newValue++;

        /* Set up key name */
        strcpyW(subkey, args);
        if (!assoc) strcatW(subkey, shOpCmdW);

        /* If nothing after '=' then clear value - only valid for ASSOC */
        if (*newValue == 0x00) {

          if (assoc) rc = RegDeleteKeyW(key, args);
          if (assoc && rc == ERROR_SUCCESS) {
            WINE_TRACE("HKCR Key '%s' deleted\n", wine_dbgstr_w(args));

          } else if (assoc && rc != ERROR_FILE_NOT_FOUND) {
            WCMD_print_error();
            errorlevel = 2;

          } else {
            WCHAR  msgbuffer[MAXSTRING];

            /* Load the translated 'File association not found' */
            if (assoc) {
              LoadStringW(hinst, WCMD_NOASSOC, msgbuffer,
                          sizeof(msgbuffer)/sizeof(WCHAR));
            } else {
              LoadStringW(hinst, WCMD_NOFTYPE, msgbuffer,
                          sizeof(msgbuffer)/sizeof(WCHAR));
            }
            WCMD_output_stderr(msgbuffer, keyValue);
            errorlevel = 2;
          }

        /* It really is a set value = contents */
        } else {
          rc = RegCreateKeyExW(key, subkey, 0, NULL, REG_OPTION_NON_VOLATILE,
                              accessOptions, NULL, &readKey, NULL);
          if (rc == ERROR_SUCCESS) {
            rc = RegSetValueExW(readKey, NULL, 0, REG_SZ,
                                (LPBYTE)newValue,
                                sizeof(WCHAR) * (strlenW(newValue) + 1));
            RegCloseKey(readKey);
          }

          if (rc != ERROR_SUCCESS) {
            WCMD_print_error();
            errorlevel = 2;
          } else {
            WCMD_output_asis(args);
            WCMD_output_asis(equalW);
            WCMD_output_asis(newValue);
            WCMD_output_asis(newlineW);
          }
        }
      }
    }

    /* Clean up */
    RegCloseKey(key);
}

/****************************************************************************
 * WCMD_color
 *
 * Colors the terminal screen.
 */

void WCMD_color (void) {

  CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
  HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);

  if (param1[0] != 0x00 && strlenW(param1) > 2) {
    WCMD_output_stderr(WCMD_LoadMessage(WCMD_ARGERR));
    return;
  }

  if (GetConsoleScreenBufferInfo(hStdOut, &consoleInfo))
  {
      COORD topLeft;
      DWORD screenSize;
      DWORD color = 0;

      screenSize = consoleInfo.dwSize.X * (consoleInfo.dwSize.Y + 1);

      topLeft.X = 0;
      topLeft.Y = 0;

      /* Convert the color hex digits */
      if (param1[0] == 0x00) {
        color = defaultColor;
      } else {
        color = strtoulW(param1, NULL, 16);
      }

      /* Fail if fg == bg color */
      if (((color & 0xF0) >> 4) == (color & 0x0F)) {
        errorlevel = 1;
        return;
      }

      /* Set the current screen contents and ensure all future writes
         remain this color                                             */
      FillConsoleOutputAttribute(hStdOut, color, screenSize, topLeft, &screenSize);
      SetConsoleTextAttribute(hStdOut, color);
  }
}
