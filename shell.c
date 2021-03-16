#include <ctype.h>        // isprint
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <locale.h>
#include <ncurses.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_PATH 4096
#define MAX_COMMAND_LENGHT 4096
#define BUFFER_SIZE 512
#define MAX_HISTORY_COUNT 10
#define NOT_ENOUGH_PARAMS "Za malo parametrow"
#define TOO_MANY_PARAMS "Za duzo parametrow"
#define PAIR_MAGENTA 1
#define PAIR_YELLOW 2
#define PAIR_BLUE 3
#define PAIR_CYAN 4
#define PAIR_GREEN 5

int view_rows, view_cols, scrolled_rows = 0;
WINDOW *w, *p;
char **history;
int history_current_index = -1;
int is_history_full = FALSE;

void keyLoop();
void parseRawCommand(char *raw_command);
void runCommand(char *command, char **params, int params_count);
int checkParams(int minimum_params, int maximum_params, int params_count);
void append(char *str, char character);
int startsWith(char *source, char *prefix);
void printBackspace();
int printPrompt();
void refreshTerminal();
void scrollDown();
void clearLineAfter(int y, int x);
void execute(char **arguments);
void printHistory();
void cd(char *path);
void cp(char *source, char *dest, int recursive, int override, int is_first_call);
void grep(char *file, char *pattern, int case_insensitive);
void help();
void runExit();

int main() {
  setlocale(LC_ALL, "");
  initscr();
  cbreak();
  noecho();

  getmaxyx(stdscr, view_rows, view_cols);

  w = newwin(0, 0, 0, 0);
  p = newpad(1000, view_cols);

  keypad(p, TRUE);
  scrollok(p, TRUE);
  signal(SIGINT, runExit);

  history = malloc(MAX_HISTORY_COUNT * sizeof(char *));
  if (history == NULL) {
    wprintw(p, "Nie mozna przypisac pamieci");
    return 1;
  }

  if (has_colors() == TRUE) {
    use_default_colors();
    start_color();
    init_pair(PAIR_MAGENTA, COLOR_MAGENTA, -1);
    init_pair(PAIR_YELLOW, COLOR_YELLOW, -1);
    init_pair(PAIR_BLUE, COLOR_BLUE, -1);
    init_pair(PAIR_CYAN, COLOR_CYAN, -1);
    init_pair(PAIR_GREEN, COLOR_GREEN, -1);
  }

  umask(0);
  keyLoop();
  runExit();
  return 0;
}

void keyLoop() {
  if (printPrompt()) return;

  int y_after_prompt;
  int x_after_prompt;
  getyx(p, y_after_prompt, x_after_prompt);

  char raw_command[MAX_COMMAND_LENGHT] = {'\0'};

  //-1 => brak
  // 0 do MAX_HISTORY_COUNT - 1 => przegladany element
  int history_traveler_index = -1;

  int tab_index = -1;
  char *tab_prefix;

  MEVENT event;
  mousemask(ALL_MOUSE_EVENTS, NULL);

  while (1) {
    refreshTerminal();
    int charcode = wgetch(p);

    if (charcode == KEY_MOUSE) {
      if (getmouse(&event) == OK) {
        if (event.bstate & BUTTON4_PRESSED) {
          scrolled_rows -= 3;
          if (scrolled_rows < 0) scrolled_rows = 0;
          if (scrolled_rows != getcury(p) - view_rows + 1) curs_set(0);
        } else if (event.bstate & BUTTON5_PRESSED) {
          int p_length = getcury(p);
          scrolled_rows += 3;
          if (scrolled_rows > p_length - view_rows - 1) scrollDown();
          else if (scrolled_rows == p_length - view_rows + 1) curs_set(1);
        }
      }
    }

    if (charcode == KEY_RESIZE) {
      getmaxyx(stdscr, view_rows, view_cols);
      delwin(w);
      w = newwin(0, 0, 0, 0);
      scrollDown();
    }

    if (charcode != '\t' && tab_index > -1) {
      free(tab_prefix);
      tab_index = -1;
    }

    if (charcode != KEY_UP && charcode != KEY_DOWN && history_traveler_index > -1) {
      // jesli wczesniej przeszukiwano historie to resetuj wyszukiwanie
      history_traveler_index = -1;
    }

    // wcisnieto strzalke i historia nie jest pusta
    if ((charcode == KEY_UP || charcode == KEY_DOWN) && history_current_index != -1) {
      clearLineAfter(y_after_prompt, x_after_prompt);
      raw_command[0] = '\0';

      if (charcode == KEY_UP) {
        history_traveler_index++;
        if ((is_history_full && history_traveler_index == MAX_HISTORY_COUNT) || (!is_history_full && history_traveler_index > history_current_index)) {
          history_traveler_index--;
        }
      } else if (charcode == KEY_DOWN && history_traveler_index != -1) {
        history_traveler_index--;
      }

      if (history_traveler_index != -1) {
        int history_entry_index = history_current_index - history_traveler_index;
        if (history_entry_index < 0) {
          history_entry_index = MAX_HISTORY_COUNT + history_entry_index;
        }

        if (has_colors() == FALSE)
          wprintw(p, history[history_entry_index]);
        else {
          wattron(p, COLOR_PAIR(PAIR_CYAN));
          wprintw(p, history[history_entry_index]);
          wattroff(p, COLOR_PAIR(PAIR_CYAN));
        }

        strcpy(raw_command, history[history_entry_index]);
      }
    }

    switch (charcode) {
    case KEY_BACKSPACE:
    case 127: // backspace
      if (strlen(raw_command) > 0) {
        raw_command[strlen(raw_command) - 1] = '\0';
        printBackspace();
      }
      break;

    case '\n': // zatwierdzanie komendy
      waddch(p, '\n');

      // usun mozliwe spacje na koncu (trim)
      for (int i = strlen(raw_command) - 1; raw_command[i] == ' ' && i >= 0; i--)
        raw_command[i] = '\0';

      // pusta komenda
      if (strlen(raw_command) == 0) {
        if (printPrompt()) return;
        getyx(p, y_after_prompt, x_after_prompt);
        break;
      }

      // dodaj komende do historii
      if (history_current_index == MAX_HISTORY_COUNT - 1) {
        history_current_index = 0;
        is_history_full = TRUE;
      } else {
        history_current_index++;
      }

      if (is_history_full == TRUE) {
        free(history[history_current_index]);
      }
      history[history_current_index] = malloc(strlen(raw_command) * sizeof(char));
      strcpy(history[history_current_index], raw_command);

      // parsuj
      parseRawCommand(raw_command);

      // wyczysc bufor
      raw_command[0] = '\0';

      if (printPrompt()) return;
      getyx(p, y_after_prompt, x_after_prompt);
      break;

    case '\t': { // TAB
      DIR *dir;
      struct dirent *entry;

      if (tab_index == -1) {
        tab_prefix = malloc(strlen(raw_command) * sizeof(char));
        strcpy(tab_prefix, raw_command);
      }

      tab_index++;

      if (startsWith(tab_prefix, "./")) {
        if ((dir = opendir(".")) == NULL) {
          wprintw(p, "Blad funkcji opendir()");
        } else {
          int i = 0;
          while ((entry = readdir(dir)) != NULL) {
            if (startsWith(entry->d_name, &tab_prefix[2])) {
              if (i == tab_index) {
                clearLineAfter(y_after_prompt, x_after_prompt);
                mvwprintw(p, y_after_prompt, x_after_prompt, "./%s", entry->d_name);
                strcpy(&raw_command[2], entry->d_name);
                break;
              }
              i++;
            }
          }

          closedir(dir);
        }
      } else {
        char path_env[MAX_PATH] = {'\0'};
        strcpy(path_env, getenv("PATH"));
        if (strlen(path_env) > 0) {
          int searching = TRUE;
          char *buff = strtok(path_env, ":");
          int i = 0;
          while (searching && buff != NULL) {
            if ((dir = opendir(buff)) != NULL) {
              while ((entry = readdir(dir)) != NULL) {
                if (startsWith(entry->d_name, tab_prefix)) {
                  if (tab_index == i) {
                    clearLineAfter(y_after_prompt, x_after_prompt);
                    mvwprintw(p, y_after_prompt, x_after_prompt, "%s", entry->d_name);
                    strcpy(raw_command, entry->d_name);
                    searching = FALSE;
                    break;
                  }
                  i++;
                }
              }

              closedir(dir);
            }
            buff = strtok(NULL, ":");
          }
        }
      }

      break;
    }

    default:
      if (isprint(charcode)) {
        waddch(p, charcode);
        append(raw_command, charcode);
      }
      break;
    }
  }
}

void parseRawCommand(char *raw_command) {
  int raw_command_lenght = strlen(raw_command);
  char command[MAX_COMMAND_LENGHT] = {'\0'};

  int i;
  for (i = 0; raw_command[i] != ' '; i++) {
    if (raw_command_lenght == i - 1) {
      // nie ma parametrow
      char **params = malloc(sizeof(char *));
      runCommand(command, params, 0);
      free(params);
      return;
    } else {
      append(command, raw_command[i]);
    }
  }

  int escaping_single_quote = 0, escaping_double_quote = 0, params_count = 1;

  //licz params_count
  for (int j = i + 1; j < raw_command_lenght; j++) {
    char charcode = raw_command[j];
    if (charcode == '\'' && !escaping_double_quote) escaping_single_quote = 1 - escaping_single_quote;
    else if (charcode == '"' && !escaping_single_quote) escaping_double_quote = 1 - escaping_double_quote;
    else if (charcode == ' ' && !escaping_single_quote && !escaping_double_quote) params_count++;
  }

  escaping_single_quote = 0;
  escaping_double_quote = 0;

  char **params = malloc(params_count * sizeof(char *));
  int current_param = 0;
  char current_param_buffer[MAX_COMMAND_LENGHT] = {'\0'};

  for (int j = i + 1; j < raw_command_lenght; j++) {
    char charcode = raw_command[j];
    if (charcode == '\'' && !escaping_double_quote) {
      escaping_single_quote = 1 - escaping_single_quote;
      continue;
    }

    if (charcode == '"' && !escaping_single_quote) {
      escaping_double_quote = 1 - escaping_double_quote;
      continue;
    }

    if (charcode == ' ' && !escaping_single_quote && !escaping_double_quote) {
      params[current_param] = malloc(strlen(current_param_buffer) * sizeof(char));
      strcpy(params[current_param], current_param_buffer);
      current_param++;
      current_param_buffer[0] = '\0';
      continue;
    }

    append(current_param_buffer, charcode);
  }

  if(escaping_single_quote) {
    wprintw(p, "Brakujacy ' na koncu polecenia\n");
    return;
  } else if(escaping_double_quote) {
    wprintw(p, "Brakujacy \" na koncu polecenia\n");
    return;
  }

  params[current_param] = malloc(strlen(current_param_buffer) * sizeof(char));
  strcpy(params[current_param], current_param_buffer);

  runCommand(command, params, params_count);

  for (int j = 0; j < params_count; j++)
    free(params[j]);
  free(params);
}

void runCommand(char *command, char **params, int params_count) {
  if (strcmp("", command) == 0)
    return;

  if (strcmp("cd", command) == 0) {
    if (checkParams(1, 1, params_count))
      cd(params[0]);
    return;
  }

  if (strcmp("help", command) == 0) {
    help();
    return;
  }

  if (strcmp("exit", command) == 0) {
    runExit();
    return;
  }

  if (strcmp("clear", command) == 0) {
    if (checkParams(0, 0, params_count)) {
      werase(p);
      scrolled_rows = 0;
      scrollDown();
    }
    return;
  }

  if (strcmp("history", command) == 0) {
    if (checkParams(0, 0, params_count))
      printHistory();
    return;
  }

  if (strcmp("echo", command) == 0) {
    if (checkParams(1, -1, params_count)) {
      for (int i = 0; i < params_count; i++)
        wprintw(p, "%s ", params[i]);
      waddch(p, '\n');
    }
    return;
  }

  if (strcmp("cp", command) == 0) {
    if(params_count == 4) {
      if ((strcmp(params[0], "-R") == 0 && strcmp(params[1], "-O") == 0) || (strcmp(params[1], "-R") == 0 && strcmp(params[0], "-O") == 0))
        cp(params[2], params[3], 1, 1, 1); // -R -O
      else
        checkParams(2, 2, params_count);
    } else if(params_count == 3) {
      if (strcmp(params[0], "-R") == 0)
        cp(params[1], params[2], 1, 0, 1); // -R
      else if (strcmp(params[0], "-O") == 0)
        cp(params[1], params[2], 0, 1, 1); // -O
      else
        checkParams(2, 2, params_count);
    } else if(params_count == 2) {
      cp(params[0], params[1], 0, 0, 1);
    } else {
      checkParams(2, 2, params_count);
    }
    return;
  }

  if (strcmp("grep", command) == 0) {
    if(params_count == 3){
      if(strcmp(params[0], "-i") == 0)
        grep(params[2], params[1], 1);
      else
        checkParams(2, 2, params_count);
    } else if(checkParams(2, 2, params_count)) {
      grep(params[1], params[0], 0);
    }
    return;
  }

  char **arguments = malloc((params_count + 2) * sizeof(char *));
  arguments[0] = command;
  int i = 1;
  for (; i <= params_count; i++) {
    arguments[i] = params[i - 1];
  }
  arguments[i] = NULL;

  mkfifo(".fifo", 0666);
  if (fork() == 0) {
    int fd = open(".fifo", O_WRONLY);
    int trash = open("/dev/null", O_RDONLY);
    dup2(trash, 0);
    dup2(fd, 1);
    execute(arguments);
    close(fd);
    close(trash);
  } else {
    char buffer[BUFFER_SIZE] = {'\0'};
    int fd = open(".fifo", O_RDONLY);
    int num;
    while ((num = read(fd, &buffer, BUFFER_SIZE)) > 0) {
      buffer[num] = '\0';
      wprintw(p, "%s", buffer);
    }
    close(fd);
    unlink(".fifo");
  }

  free(arguments);
}

int checkParams(int minimum_params, int maximum_params, int params_count) {
  if (params_count < minimum_params) {
    wprintw(p, "%s (minimum %d)\n", NOT_ENOUGH_PARAMS, minimum_params, maximum_params);
    return 0;
  }

  if (maximum_params != -1 && params_count > maximum_params) {
    wprintw(p, "%s (maksimum %d)\n", TOO_MANY_PARAMS, minimum_params, maximum_params);
    return 0;
  }

  return 1;
}

void append(char *str, char character) {
  int lenght = strlen(str);
  str[lenght] = character;
  str[lenght + 1] = '\0';
}

int startsWith(char *source, char *prefix) {
  if (strlen(source) < strlen(prefix))
    return FALSE;

  for (int i = strlen(prefix) - 1; i >= 0; i--)
    if (source[i] != prefix[i])
      return FALSE;

  return TRUE;
}

void printBackspace() {
  int y, x;
  getyx(p, y, x);
  mvwdelch(p, y, x - 1);
}

int printPrompt() {
  char *cwd = malloc(sizeof(char) * MAX_PATH);
  if (getcwd(cwd, MAX_PATH) == NULL) {
    wprintw(p, "Nie mozna wypisac znaku zachety (getcwd)");
    free(cwd);
    return 1;
  }

  char *login = calloc(32, sizeof(char));
  getlogin_r(login, 32);

  if (has_colors() == FALSE) {
    wprintw(p, "[%s:%s] $ ", login, cwd);
  } else {
    wattron(p, COLOR_PAIR(PAIR_MAGENTA));
    waddch(p, '[');
    wattroff(p, COLOR_PAIR(PAIR_MAGENTA));
    wattron(p, COLOR_PAIR(PAIR_GREEN));
    wprintw(p, login);
    wattroff(p, COLOR_PAIR(PAIR_GREEN));
    waddch(p, ':');
    wattron(p, COLOR_PAIR(PAIR_YELLOW));
    wprintw(p, cwd);
    wattroff(p, COLOR_PAIR(PAIR_YELLOW));
    wattron(p, COLOR_PAIR(PAIR_MAGENTA));
    waddch(p, ']');
    wattroff(p, COLOR_PAIR(PAIR_MAGENTA));
    wattron(p, COLOR_PAIR(PAIR_BLUE));
    wattron(p, A_BOLD);
    wprintw(p, " $ ");
    wattroff(p, A_BOLD);
    wattroff(p, COLOR_PAIR(PAIR_BLUE));
  }

  free(cwd);
  free(login);

  scrollDown();

  return 0;
}

void refreshTerminal() {
  wrefresh(w);
  prefresh(p, scrolled_rows, 0, 0, 0, view_rows - 1, view_cols - 1);
}

void scrollDown() {
  scrolled_rows = getcury(p) - view_rows + 1;
  refreshTerminal();
  curs_set(1);
}

void clearLineAfter(int y, int x) {
  // wyczysc dotychczas wpisany tekst
  wmove(p, y, x); // przesun kursor
  wclrtoeol(p);
  refreshTerminal();
}

void execute(char **arguments) {
  if (execvp(arguments[0], arguments) == -1) {
    switch (errno) {
    case 1:
      printf("Brak uprawnien");
      break;
    case 2:
      printf("Brak takiego pliku w sciezkach z PATH");
      break;
    case 13:
      printf("Brak dostepu");
      break;
    default:
      printf("Blad o numerze errno = %d", errno);
      break;
    }
    putchar('\n');
  }
  exit(EXIT_SUCCESS);
}

void printHistory() {
  int count = 1;

  if (has_colors() == TRUE)
    wattron(p, COLOR_PAIR(PAIR_MAGENTA));

  for (int i = history_current_index; i >= 0; i--) {
    wprintw(p, "#%d ", count++);
    wprintw(p, history[i]);
    waddch(p, '\n');
  }

  if (is_history_full == TRUE) {
    for (int i = MAX_HISTORY_COUNT - 1; i > history_current_index; i--) {
      wprintw(p, "#%d ", count++);
      wprintw(p, history[i]);
      waddch(p, '\n');
    }
  }

  if (has_colors() == TRUE)
    wattroff(p, COLOR_PAIR(PAIR_MAGENTA));
}

char previous_path[MAX_PATH] = {'\0'};
void cd(char *path) {
  if (strcmp(path, "~") == 0) {
    if (!getenv("HOME")) {
      wprintw(p, "Brak zmiennej srodowiskowej HOME");
      waddch(p, '\n');
      return;
    }

    strcpy(path, getenv("HOME"));
  }

  if (strcmp(path, "-") == 0) {
    if (strlen(previous_path) == 0) {
      wprintw(p, "Brak poprzedniej sciezki");
      waddch(p, '\n');
      return;
    }

    strcpy(path, previous_path);
  }

  previous_path[0] = '\0';
  if (getcwd(previous_path, MAX_PATH) == NULL)
    wprintw(p, "Nie mozna pobrac sciezki");

  if (chdir(path) != 0)
    wprintw(p, "chdir() failed");
}

int isDir(const char *path) {
  struct stat st;
  if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
    return true;
  return false;
}

void cpFile(char *source, char *dest, int override) {
  int fd_in, fd_out, num;
  char buffer[BUFFER_SIZE];

  if (access(source, F_OK) != 0) {
    wprintw(p, "Brak pliku zrodlowego %s\n", source);
    return;
  }

  if (access(dest, F_OK) == 0 && !override)
    return;

  fd_in = open(source, O_RDONLY);
  fd_out = open(dest, O_WRONLY | O_CREAT);
  while ((num = read(fd_in, &buffer, BUFFER_SIZE)) > 0)
    write(fd_out, &buffer, num);
  close(fd_in);
  close(fd_out);
  struct stat st;
  stat(source, &st);
  chmod(dest, st.st_mode);
}

void cp(char *source, char *dest, int recursive, int override, int is_first_call) {
  if (isDir(source)) {
    if (!recursive) {
      cpFile(source, dest, override);
      return;
    }

    DIR *dir;
    struct dirent *entry;
    struct stat st;

    if (!isDir(dest)) {
      stat(source, &st);
      if(mkdir(dest, st.st_mode) == -1) {
        wprintw(p, "Nie mozna stworzyc folderu %s\n", dest);
        return;
      }
    }

    if ((dir = opendir(source)) != NULL) {
      while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
          continue;

        char *fullpath_source = malloc(strlen(source) + strlen(entry->d_name) + 2);
        char *fullpath_dest = malloc(strlen(dest) + strlen(entry->d_name) + 2);
        sprintf(fullpath_source, "%s/%s", source, entry->d_name);
        sprintf(fullpath_dest, "%s/%s", dest, entry->d_name);
        if (isDir(fullpath_source)) {
          if (!isDir(fullpath_dest)) {
            stat(fullpath_source, &st);
            if(mkdir(fullpath_dest, st.st_mode) == -1) {
              wprintw(p, "Nie mozna stworzyc folderu %s\n", fullpath_dest);
              free(fullpath_source);
              free(fullpath_dest);
              return;
            }
          }
          cp(fullpath_source, fullpath_dest, recursive, override, 0);
        } else {
          cpFile(fullpath_source, fullpath_dest, override);
        }
        wprintw(p, "%s -> %s\n", fullpath_source, fullpath_dest);
        free(fullpath_source);
        free(fullpath_dest);
      }
      closedir(dir);
    }
  } else if (access(source, F_OK == 0)) {
    cpFile(source, dest, override);
  } else if (is_first_call) {
    wprintw(p, "Nie ma takiego pliku\n");
  }
}

// REGEX
// https://man7.org/linux/man-pages/man3/regex.3.html
void grep(char *file, char *pattern, int case_insensitive) {
  if (access(file, F_OK) != 0) {
    wprintw(p, "Brak pliku %s\n", file);
    return;
  }

  FILE *fd = fopen(file, "r");
  char source[2048];
  while (fgets(source, 2048, fd)) {
    int maximum_matches = 10, maximum_groups = 10;

    regex_t compiled_regex;
    regmatch_t groups[10];

    int flags = case_insensitive ? REG_ICASE : REG_EXTENDED;
    if (regcomp(&compiled_regex, pattern, flags)) {
      wprintw(p, "Blad skladni polecenia grep\n");
      return;
    };

    int match = 0;
    char *cur = source;
    int previous_end = 0;
    for (match = 0; match < maximum_matches; match++) {
      if (regexec(&compiled_regex, cur, maximum_groups, groups, 0))
        break;

      int group = 0, bytes_offset = 0;
      for (group = 0; group < maximum_groups; group++) {
        if (groups[group].rm_so == -1)
          break;

        if (group == 0)
          bytes_offset = groups[group].rm_eo;
        
        for (int i = previous_end; i < groups[group].rm_so + previous_end; i++)
          waddch(p, source[i]);

        if (has_colors() == TRUE) wattron(p, COLOR_PAIR(PAIR_YELLOW));
        wattron(p, A_BOLD);
        for (int i = groups[group].rm_so + previous_end; i < groups[group].rm_eo + previous_end; i++) {
          waddch(p, source[i]);
        }
        if (has_colors() == TRUE) wattroff(p, COLOR_PAIR(PAIR_YELLOW));
        wattroff(p, A_BOLD);
        previous_end = groups[group].rm_eo + previous_end;
      }
      cur += bytes_offset;
    }

    if (previous_end != 0)
      for (int i = previous_end; i < strlen(source); i++)
        waddch(p, source[i]);

    regfree(&compiled_regex);
  }
  waddch(p, '\n');
  fclose(fd);
}

void help() {
  const char *tekst = "\n\
  Shell\n\
  \n\
  Dostepne komendy:\n\
    - cp [-R] [-O] skad dokad\n\
      -R = recursive (kopiuj tez podfoldery)\n\
      -O = override (nadpisuj pliki o ile istnieja)\n\
    - grep [-i] wzorzec plik\n\
      -i = case insensitive (nie rozrozniaj wielkich liter)\n\
    - cd sciezka\n\
    - help\n\
    - programy znajdujace sie w katalogach w PATH\n\
  \n";

  if (has_colors() == FALSE)
    wprintw(p, tekst);
  else {
    wattron(p, COLOR_PAIR(PAIR_BLUE));
    wprintw(p, tekst);
    wattroff(p, COLOR_PAIR(PAIR_BLUE));
  }
}

void runExit() {
  if (is_history_full) {
    for (int i = 0; i < MAX_HISTORY_COUNT; i++)
      free(history[i]);
  } else {
    for (int i = 0; i <= history_current_index; i++)
      free(history[i]);
  }
  free(history);

  clear();
  endwin();
  exit(EXIT_SUCCESS);
}