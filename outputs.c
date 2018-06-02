#define _GNU_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <time.h>

char* PATH_TO_FOLDER;

// This handler is used only by the master parent and only him will delete the temporary files
void handler_parent(int sig) {
  puts("Aborting!");
  // If a path to a folder has been set the folder and its content must be removed
  if (PATH_TO_FOLDER != NULL) {
      execlp("rm", "rm", "-r", PATH_TO_FOLDER, NULL);
      _exit(-1);
  }
  _exit(-1);
}

// This handler simply overwrites the parent one and kills the child
void handler_children(int sig) {
  _exit(-1);
}

// Struct where the info needed to execute a command is stored
typedef struct command {
  char* command; // The string containing the command itself
  int index; // The index assigned to this command (aka the nth-1 command in the file as read top to bottom)
  int input_from_whom; // The index of the command whose output is needed as input for this one (it will match the index of the command if no input is needed)
  int how_many_need_output; // The number of commands needing the output of this command
  int* who_needs_output; // The indexes of the commands needing the output of this command
} * COMMAND;

// This reads the contents of a file to a string and returns it
char* read_from_file (char* path_to_file) {
  // Opening the file
  int fd;
  if ((fd = open(path_to_file, O_RDONLY, 0666)) < 0) {
    char str_err[strlen(path_to_file) + 20];
    sprintf(str_err, "Couldn't open file: %s", path_to_file);
    perror(str_err);
    kill(0, SIGINT);
  }

  // Creating a child process to use the command 'wc -c' to know how many bytes we need to read
  int byte_count;
  // Using a pipe to establish communication between parent and child
  int pipe_fd[2];
  pipe(pipe_fd);

  // Forking
  switch (fork()) {
    case -1:
      perror("Couldn't create fork!");
      kill(0, SIGINT);
    case 0:
      // Child executing the command to count the bytes and redirecting the output to the pipe
      close(pipe_fd[0]);
      dup2(pipe_fd[1], 1);
      close(pipe_fd[1]);
      execlp("wc", "wc", "--bytes", path_to_file, NULL);
      _exit(-1);
    default: {
      // Parent waiting for the child and reading the pipe
      wait(NULL);
      close(pipe_fd[1]);
      char str[1024];
      read(pipe_fd[0], str, sizeof(str));
      close(pipe_fd[0]);
      sscanf(str, "%d", &byte_count);
    }
  }

  // Allocating the memory necessary (acording to the byte_count returned by the 'wc' command) and reading to that memory the file
  char* contents = malloc(sizeof(char)*(byte_count));
  read(fd, contents, byte_count);
  close(fd);
  contents[byte_count] = 0;

  return contents;
}

// This counts how many arguments a command in the format of string has
int count_args (char* command) {
  int i, count=0, its_command = 0;

  // Counting every command by counting the number of separators between them (be it a space or more)
  for (i=0; i<strlen(command); i++) {
    if (command[i] != ' ' && command[i] != '\n' && !its_command) {
      its_command = 1;
      count++;
    } else if (command[i] == ' ' || command[i] == '\n') its_command = 0;
  }

  return count;
}

// This receives a command in the format of string and an array where its arguments will be store individually
int get_args (char* command, int n_args, char* args[]) {
  int bytes, i, index; // Bytes is the size of each arg
  // Index will count how many args have been read
  for(i=0, bytes=0, index=0; i < strlen(command) && index<n_args; i++) {
    // Only commands with characters in between [34,125] of the ASCII table are considered valid, so any other is considered a separator between arguments
    if (command[i] < 33 || command[i] > 126) {
      if (bytes != 0) {
        // Allocating memory acording to bytes and copying the arg to the array
        char* arg = malloc(sizeof(char)* bytes+1);
        strncpy(arg, command + i - bytes, bytes);
        arg[bytes] = 0;
        args[index++] = arg;
      }
      // Getting bytes to 0 as new arg may show up and its bytes count must not be linked to the previous one
      bytes = 0;
    } else
      // Incrementing the bytes because the character is valid
      bytes++;
  }

  // Copying the last argument in case the command didn't end with an invalid character
  if (index < n_args) {
    char* arg = malloc(sizeof(char)* bytes);
    strncpy(arg, command + i - bytes, bytes);
    args[index++] = arg;
  }

  return (index == n_args) ? 0 : -1;
}

// Executes a command by its string
void execute_single_command (char* command) {
  // Counting how many arguments its got
  int n_args = count_args (command);
  char* args[n_args+1];
  // Getting those arguments
  get_args(command, n_args, args);
  args[n_args] = NULL;

  // Creating a child process to execute the command
  switch (fork()) {
    case -1:
      perror("Couldn't create fork!");
      kill(0,SIGINT);
    case 0: {
      // Executing the command
      execvp(args[0], args);
      char str_err[24 + strlen(command)];
      sprintf(str_err, "Could execute command (%s)", command);
      perror(str_err);
      kill(0,SIGINT);
    } default:
      // Waiting for the child to free the memory used by the arguments
      wait(NULL);
      for (int i=0; i <= n_args; i++)
        free(args[i]);
      break;
  }
}

// Returns the byte count until the next line
int next_line (char* text) {
  for (int i=0; i<strlen(text); i++)
    if (text[i] == '\n' || i+1 == strlen(text))
      return i+1;

  return -1;
}

// Gets if the character and its sequence must change the state of ignorable text or not
int should_ignore_or_not (char* str_pointer, int previous_ignore_value) {
  int count, i;
  // Counting to see if the sequence of characters that act as separators from the commands and the outputs matches
  for(i=0, count=0; i<3; i++) {
    if (str_pointer[i] == '>') count++;
    if (str_pointer[i] == '<') count--;
  }

  // Getting the return value correct according to the previous ignore state value
  int should_ignore = previous_ignore_value;
  if (count == 3 && !previous_ignore_value && str_pointer[i] == '\n') should_ignore = 1;
  if (count == -3 && previous_ignore_value && str_pointer[i] == '\n') should_ignore = 0;

  return should_ignore;
}

// Returns the lines with useful information from the string with the contents of file by index (The nth-1 lines of content that are not the outputs of a reprocessed notebook)
char* read_lines_notebook(char* nb_content, int index) {
  // The variable should_ignore is used so that when iterating the characters if the character is from content that is needed its value is 0 (do not ignore), else it's 1 (ignore)
  int i, index_count = 0, should_ignore = 0, bytes = 0, its_command = 0;
  for (i=0; index_count <= index; i++) {
    // Checking if it's the end of an 'ignore' state
    if (nb_content[i] == '<' && i+4 < strlen(nb_content)) {
      should_ignore = should_ignore_or_not(nb_content + i, should_ignore);
      i += 4;
    }
    if (i >= strlen(nb_content)) break; // If by jumping to the content after and end of an ignore we leave the boundaries of the string of content we must break the cycle
    if (should_ignore) continue; // Skips to the next character
    // Checking if an ignore state must be set
    if (nb_content[i] == '>' && i+4 < strlen(nb_content)) {
      should_ignore = should_ignore_or_not(nb_content + i, should_ignore);
      continue;
    }
    // If the index of the lines matches the index that was initially intended, bytes starts counting
    if (index_count == index) bytes++;
    // The next 2 lines are used to iterate the index of the lines by checking if the end of a command has happened
    if (nb_content[i] == '$') its_command = 1;
    if (nb_content[i] == '\n' && its_command) { index_count++; its_command=0; }
  }

  // Safety precaution
  if (bytes == 0) return NULL;

  // Allocating the space and copying the lines
  char* lines = malloc(sizeof(char) * bytes);
  strncpy(lines, nb_content + i - bytes, bytes);
  lines[bytes] = 0;

  return lines;
}

// Filters the string of the content from to notebook to a string of commands only (separated by '\n')
char* get_all_commands(char* nb_content) {
  int i=0, size = 0, pos = 0;
  char* lines, *commands;

  do {
    if (i != 0) {
      while (pos < strlen(lines)) {
        int bytes = next_line(lines + pos); // Bytes to the next line (or in this case the size of the 'command')
        // Checking if it's a command
        if (lines[pos] == '$') {
          // If it's the first time it allocs the memory needed
          if (size == 0) {
            commands = (char*) malloc(sizeof(char) * (bytes+1));
            strncpy(commands, lines+pos, bytes);
          }
          // Else, allocs more memory, copies the previous commands and concatenates them with the new one
          else {
            char* tmp_ptr = malloc(sizeof(char)*(size+bytes+1));
            memcpy(tmp_ptr,commands,size);
            if (tmp_ptr) commands = tmp_ptr;
            else return NULL;
            strncat(commands, lines+pos, bytes);
          }
          // Incrementing the total size of the string containing the commands
          size += bytes;
        }
        // Iterating the lines
        pos += bytes;
      }
    }
    // Getting the lines from the notebook at the certain index to get all commands from the useful info
    lines = read_lines_notebook(nb_content, i);
    pos = 0; i++;
  } while (lines);

  return commands;
}

// Gets how many commands above was the input that it's needed to this command
int get_how_many_commands_above (char* command) {
  int number = 0;
  if (command[0] == '|') number = 1;
  else if (command[0] >= '0' && command[0] <= '9') sscanf(command, "%d", &number);
  return number;
}

// Isolates a command from the string containing all the commands and returning it
char* get_command (char* command_str) {
  int pos;
  for(pos=0; pos < strlen(command_str); pos++)
    if (command_str[pos] == ' ') {
      pos++;
      break;
    }
  int bytes;
  for (bytes=0; bytes + pos < strlen(command_str) && command_str[bytes] != '\n';  bytes++);
  for (; command_str[bytes] == ' ' || command_str[bytes] == '\n'; bytes--);

  // Allocating the memory necessary to hold the command
  char* command = malloc(bytes-pos+3);
  strncpy(command, command_str+pos, bytes-pos+2);

  return command;
}

// Sets the struct command (above defined) with the info of the command of index 'index'
int set_command(COMMAND c, char* commands, int n_comm, int index) {
  int i, pos;
  for(i=0, pos=0; i<n_comm; i++, pos+=next_line(commands+pos)) {
    // If the index matches most attributes are set (index, the command string, the index of the command whose output is needed)
    if (i == index) {
      c->index = index;
      c->command = get_command(commands+pos);
      c->input_from_whom = index - get_how_many_commands_above(commands+pos+1);
      c->how_many_need_output = 0; // Starts the counter of commands needing its output at 0
    // Checking which of the following commands need the output of this one
    } else if (i > index) {
      if (i - get_how_many_commands_above(commands+pos+1) == index) {
        // If it's the first command to need this ones output only memory is allocated
        if (c->how_many_need_output == 0) {
          c->who_needs_output = malloc(sizeof(int));
        // Else, a realloc must happen
        } else {
          int* tmp_ptr = realloc(c->who_needs_output, sizeof(int)*(c->how_many_need_output+1));
          if (tmp_ptr) c->who_needs_output = tmp_ptr;
          else return -1;
        }
        // Setting the index of the command that needs the output and incrementing the number of needy commands
        c->who_needs_output[c->how_many_need_output] = i;
        c->how_many_need_output++;
      }
    }
  }

  return 0;
}

// Prints a struct command to the string for debugging purposes
void print_struct_command (COMMAND c) {
    printf("Index: %d; Input from who: %d; How many need output: %d; "
    , c->index, c->input_from_whom, c->how_many_need_output);
    printf("Who needs output: ");
    for (int i=0; i<c->how_many_need_output; i++) {
      printf("%d", c->who_needs_output[i]);
      if (i != c->how_many_need_output-1)
        printf(" ");
      else
        printf("; ");
    }
    printf("Command: %s\n", c->command);
}

// Counts how many commands there are in a string of content
int count_commands (char* commands) {
 int count = 0;
  for (int i=0; i<strlen(commands); i++)
    if (commands[i] == '$') count++;
  return count;
}

// Returns a path of the format PREFIX_INDEX_INDEX (used for pipes of input)
char* get_path_input(char* path, int index_from, int index_to) {
  char* full_path = malloc(sizeof(char) * (strlen(path) + 22));
  sprintf(full_path, "%s_%d_%d", path, index_from, index_to);
  return full_path;
}

// Returns a path of the format PREFIX_INDEX (used for pipes of output)
char* get_path_output(char* path, int index) {
  char* full_path = malloc(sizeof(char) * (strlen(path) + 11));
  sprintf(full_path, "%s_%d", path, index);
  return full_path;
}

// Duplicates the decryptor of the pipe of input so when the command is executed the input is received
int duplicate_pipe (char* pipe_prefix, COMMAND c) {
  char* path_to_pipe_input = get_path_input(pipe_prefix, c->input_from_whom, c->index);
  // Creating the pipe in case it hasn't been yet
  mkfifo(path_to_pipe_input, 0666);
  int fdr;
  if ((fdr = open(path_to_pipe_input, O_RDONLY)) < 0) {
    char str_err[strlen(path_to_pipe_input) + 20];
    sprintf(str_err, "Couldn't open pipe: %s", path_to_pipe_input);
    perror(str_err);
    kill(0,SIGINT);
  } else {
    // Duplicating the decrytor of the pipe to the stdin
    dup2(fdr, 0);
    close(fdr);
  }
  free(path_to_pipe_input);
  return 0;
}

// Executes the command and redirects its output to a pipe of output
int write_to_output_pipe_command (char* pipe_prefix, COMMAND c) {
  int fdw;
  char* path_to_pipe_output = get_path_output(pipe_prefix, c->index);
  // Creating the pipe in case it hasn't been yet
  mkfifo(path_to_pipe_output, 0666);
  if ((fdw = open(path_to_pipe_output, O_WRONLY)) < 0) {
    char str_err[strlen(path_to_pipe_output) + 20];
    sprintf(str_err, "Couldn't open pipe: %s", path_to_pipe_output);
    perror(str_err);
    kill(0,SIGINT);
  }

  // Duplicating the decrytor of the pipe to the stdout
  dup2(fdw, 1);
  close(fdw);
  // Executing the command
  execute_single_command(c->command);

  free(path_to_pipe_output);
  // If input was necessary, the input pipe is no longer necessary (it's unlinked)
  if (c->index != c->input_from_whom) {
    char* path_to_pipe_input = get_path_input(pipe_prefix, c->input_from_whom, c->index);
    unlink(path_to_pipe_input);
    free(path_to_pipe_input);
  }
  return 0;
}

// Spreads the buffer message across all pipes of input that need the message
int write_in_input_pipes (char* pipe_prefix, COMMAND c, char* buffer) {
  // Iterating all the needy commands
  for (int i=0; i<c->how_many_need_output; i++)
    switch (fork()) {
      case -1:
        perror("Couldn't create fork!");
        kill(0,SIGINT);
      case 0: {
        char* path_to_input_pipe = get_path_input(pipe_prefix, c->index, c->who_needs_output[i]);
        // Creating the pipe in case it hasn't been yet
        mkfifo(path_to_input_pipe, 0666);
        int fdw;
        if ((fdw = open(path_to_input_pipe, O_WRONLY)) < 0) {
          char str_err[strlen(path_to_input_pipe) + 20];
          sprintf(str_err, "Couldn't open pipe: %s", path_to_input_pipe);
          perror(str_err);
          kill(0,SIGINT);
        } else {
          // Writing the buffer in the pipe
          write(fdw, buffer, strlen(buffer));
          close(fdw);
        }
        free(path_to_input_pipe);
        _exit(0);
      } default:
        break;
    }
  return 0;
}

// Writes the buffer message to a pipe that's gonna be read to make the final file with the notebook processed
int write_to_pipe_to_file (char* file_prefix, int index, char* buffer) {
  switch (fork()) {
    case -1:
      perror("Couldn't create fork!");
      kill(0,SIGINT);
    case 0: {
      int fdw;
      char*  path_to_file = get_path_output(file_prefix, index);
      // Creating the pipe in case it hasn't been yet
      mkfifo(path_to_file, 0666);
      fdw = open(path_to_file, O_WRONLY);

      if (fdw < 0) {
        char str_err[strlen(path_to_file) + 20];
        sprintf(str_err, "Couldn't open file: %s", path_to_file);
        perror(str_err);
        kill(0,SIGINT);
      } else {
        // Writing the message in the pipe
        write(fdw, buffer, strlen(buffer));
        close(fdw);
      }
      free(path_to_file);
      _exit(0);
    } default:
      break;
  }
  return 0;
}

// Appends the buffer message to a file with the path 'path_to_file'
int append_to_file (char* path_to_file, char* buffer) {
  int fdw;

  if ((fdw = open(path_to_file, O_CREAT|O_WRONLY|O_APPEND, 0666)) < 0) {
    char str_err[strlen(path_to_file) + 20];
    sprintf(str_err, "Couldn't open file: %s", path_to_file);
    perror(str_err);
    kill(0,SIGINT);
  } else {
    // Writing the buffer in the file
    write(fdw, buffer, strlen(buffer));
    close(fdw);
  }
  return 0;
}

// Redirects the output stored in a output pipe to the input pipes and the pipe used to make the final notebook
int output_pipe_to_pipes_files (char* pipe_prefix, char* file_prefix, COMMAND c) {
  char* path_to_output_pipe = get_path_output(pipe_prefix, c->index);
  // Creating the pipe in case it hasn't been yet
  mkfifo(path_to_output_pipe, 0666);
  int fdr;
  if ((fdr = open(path_to_output_pipe, O_RDONLY)) < 0) {
    char str_err[strlen(path_to_output_pipe) + 20];
    sprintf(str_err, "Couldn't open pipe: %s", path_to_output_pipe);
    perror(str_err);
    kill(0,SIGINT);
  } else {
    // Getting the size of the pipe in order to perform a read of the biggest size possible
    long pipe_size = (long) fcntl (fdr, F_GETPIPE_SZ);
    char buffer [pipe_size];
    while (read (fdr, buffer, pipe_size) > 0) {
      write_in_input_pipes(pipe_prefix, c, buffer); // Spreading the message to the input pipes
      write_to_pipe_to_file(file_prefix, c->index, buffer); // Spreading the message to the pipe used to make the final notebook
      for (int i=0; i<pipe_size; i++) buffer[i] = 0; // Cleaning the buffer
      while(wait(NULL) >= 0);
    }
  }
  free(path_to_output_pipe);
  return 0;
}

// Executes a command and creates all the pipes necessary for the the other commands
int execute_command_create_pipes (char* pipe_prefix, char* file_prefix, COMMAND c) {
  switch (fork()) {
    case -1:
      perror("Couldn't create fork!");
      kill(0,SIGINT);
    case 0: {
      if (c->index != c->input_from_whom) duplicate_pipe(pipe_prefix, c); // Duplicates an input pipe if input is needed
      write_to_output_pipe_command(pipe_prefix, c); // Writes the output of the command to a pipe
      _exit(0);
    }
    default:
      output_pipe_to_pipes_files(pipe_prefix, file_prefix, c); // Reads the output pipe and spreads its message
      char* path_out = get_path_output(pipe_prefix, c->index);
      unlink(path_out); // Deletes the pipe of output
      free(path_out);
      break;
    }
  return 0;
}

// Executes all commands stored in a list of COMMAND(s) concurrently
int execute_all_commands(COMMAND commands[], int n_comm, char* pipe_prefix, char* file_prefix) {
  // Iterating all commands
  for(int i=0; i<n_comm; i++) {
    COMMAND c = commands[i];
    // Forking to make the concurrency possible
    switch (fork()) {
      case -1:
        perror("Couldn't create fork!");
        kill(0,SIGINT);
      case 0: {
        execute_command_create_pipes(pipe_prefix, file_prefix, c); // Executes the command and sets the pipes
        _exit(0);
      } default:
        break;
    }
  }
  return 0;
}

// Gets the output stored in the pipe assigned to deliver the message to the final notebook and appends it to a temporary final notebook
void append_to_file_output (char* path_of_file, char* prefix_to_output, int index) {
  char* path_to_output_of_index = get_path_output(prefix_to_output, index);
  // Creating the pipe in case it hasn't been yet
  mkfifo(path_to_output_of_index, 0666);
  int fdr;
  if ((fdr = open(path_to_output_of_index, O_RDONLY)) < 0) {
    char str_err[strlen(path_of_file) + 20];
    sprintf(str_err, "Couldn't open pipe: %s", path_of_file);
    perror(str_err);
    kill(0,SIGINT);
  } else {
    // Getting the size of the pipe
    long pipe_size = (long) fcntl (fdr, F_GETPIPE_SZ);
    char buffer [pipe_size];
    while (read (fdr, buffer, pipe_size) > 0) {
      // Appending what's read
      append_to_file(path_of_file, buffer);
      for (int i=0; i<pipe_size; i++) buffer[i] = 0;
    }
  }
  // Deleting the leftover pipe
  unlink(path_to_output_of_index);
  free(path_to_output_of_index);
}

// Replaces the initial notebook with the temporary notebook created to avoid data corruption if the program failed
void replace_file (char* path_to_file, char* path_to_temp_file) {
  char* final_content = read_from_file(path_to_temp_file); // Reading from the temporary file

  int fdw;
  // Opening the initial file by truncating it
  if ((fdw = open(path_to_file, O_WRONLY|O_TRUNC)) < 0) {
    char str_err[strlen(path_to_file) + 20];
    sprintf(str_err, "Couldn't open pipe: %s", path_to_file);
    perror(str_err);
    kill(0,SIGINT);
  } else {
    write(fdw, final_content, strlen(final_content)); // Writing all the content of the temporary file to the initial one all at once
    close(fdw);
  }

  free(final_content);
  remove(path_to_temp_file); // Deleting the temporary file used
}

// Reads the notebook and performs every action necessary in order to process it
int read_notebook (char* path_to_file) {
  // Setting the paths (or the paths' prefixes) used in the program
  char prefix_to_pipe[strlen(PATH_TO_FOLDER) + 5];
  char prefix_to_pipe_of_output[strlen(PATH_TO_FOLDER) + 7];
  char path_to_temporary_final_file[strlen(PATH_TO_FOLDER) + 6];

  sprintf(prefix_to_pipe, "%s/Pipe", PATH_TO_FOLDER);
  sprintf(prefix_to_pipe_of_output, "%s/Output", PATH_TO_FOLDER);
  sprintf(path_to_temporary_final_file, "%s/Final", PATH_TO_FOLDER);

  // Getting the content of the file to a string
  char* nb_content = read_from_file(path_to_file);
  // Isolating all the commands in a single string
  char* str_commands = get_all_commands(nb_content);
  // Counting the number of commands
  int n_comm = count_commands(str_commands);

  // Initiating an array of COMMAND(s)
  COMMAND commands[n_comm];

  // Getting all the info from the commands string to the array of COMMAND(s)
  for(int i=0; i<n_comm; i++) {
    commands[i] = malloc(sizeof(struct command));
    set_command(commands[i], str_commands, n_comm, i);
  }

  // Executing all commands
  execute_all_commands(commands, n_comm, prefix_to_pipe, prefix_to_pipe_of_output);

  // Freeing the memory used
  for(int i=0; i<n_comm; i++) {
    free(commands[i]->who_needs_output);
    free(commands[i]->command);
    free(commands[i]);
  }
  free(str_commands);

  // Reading the output of the commands to a temporary file that will replace the initial's notebook content with its own
  for (int i=0; i<n_comm; i++) {
    char* lines = read_lines_notebook(nb_content, i);
    append_to_file(path_to_temporary_final_file, lines); // Appending the useful lines
    append_to_file(path_to_temporary_final_file, ">>>\n"); // Setting the 'start of output' separator
    append_to_file_output(path_to_temporary_final_file, prefix_to_pipe_of_output, i); // Appending the output of the command of index i
    append_to_file(path_to_temporary_final_file, "<<<\n"); // Setting the 'end of output' separator
  }

  free(nb_content);

  // Replacing the initial's notebook content with the content of the above created temporary file
  replace_file(path_to_file, path_to_temporary_final_file);

  return 0;
}

// Gets a path to a folder where the temporary files will be kept (makes sure that the folder does not exist and randomizes part of its name)
char* get_folder_path (char* folder_prefix) {
  struct stat st = {0};
  char* path = NULL;
  do {
    if (path != NULL) free(path);
    path = get_path_output(folder_prefix, rand());
  } while (stat(path, &st) != -1);
  return path;
}

// Initiates the program by setting signal catchers for the parent process and all of its offspring and its offspring's offspring and its offspring's offspring's offspring and so on
int init_program (char* path_name) {
  // Initiates the PATH_TO_FOLDER as NULL so that the signal catcher knows if there is a folder to delete
  PATH_TO_FOLDER = NULL;
  signal(SIGINT, handler_parent);
  srand(time(NULL)); // Starts the randomizer

  char* path = get_folder_path("/tmp/SO");
  mkdir(path, 0777); // Creating the folder where the temporary files will be kept
  PATH_TO_FOLDER = path;

  switch (fork()) {
     case -1:
      perror("Couldn't create fork!");
      kill(0,SIGINT);
    case 0: {
      signal(SIGINT, handler_children);
      read_notebook(path_name); // Actually functin that processes the notebook
      rmdir(path); // After the processing has been made the folder can be deleted
      _exit(0);
    } default:
      wait(NULL);
      PATH_TO_FOLDER = NULL;
      break;
  }
  return 0;
}

int main (int argc, char* argv[]) {
  // The program runs only if only one argument has been given (although by executing several init_program() if needed we could concurrently process several notebooks if it was asked)
  if (argc == 2)
    return init_program(argv[1]); // Initiating the program
  else
    puts("Please execute the program and provide a path to the notebook!");

  return 1;
}
