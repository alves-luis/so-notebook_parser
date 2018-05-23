#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#define BUFFER_SIZE 4096
#define ARGS 100

typedef struct command {
  char* command;
  int index;
  int input_from_whom;
  int how_many_need_output;
  int* who_needs_output;
} * COMMAND;

char* read_from_file (char* path_to_file) {
  int fd;
  if ((fd = open(path_to_file, O_RDONLY, 0666)) < 0) {
    char str_err[strlen(path_to_file) + 20];
    sprintf(str_err, "Couldn't open file: %s", path_to_file);
    perror(str_err);
    _exit(-1);
  }

  int byte_count;

  int pipe_fd[2];
  pipe(pipe_fd);
  if (fork() == 0) {
    close(pipe_fd[0]);
    dup2(pipe_fd[1], 1);
    close(pipe_fd[1]);
    execlp("wc", "wc", "--bytes", path_to_file, NULL);
    _exit(-1);
  } else {
    wait(NULL);
    close(pipe_fd[1]);
    char str[1024];
    read(pipe_fd[0], str, sizeof(str));
    close(pipe_fd[0]);
    sscanf(str, "%d", &byte_count);
    // printf("Bytes: %d\n", byte_count);
  }
  
  char* contents = malloc(sizeof(char)*(byte_count));
  read(fd, contents, byte_count);
  close(fd);
  contents[byte_count] = 0;

  return contents;
}

void split_args (char* command, char** dest, int dest_size) {
  int i = 0;
  const char c[2] = " ";

  char arg_not_literal[strlen(command)+1];
  for (int i=0; i<=strlen(command); i++)
    arg_not_literal[i] = *(command + i);

  char * arg = strtok (arg_not_literal, c);
  while (i < dest_size && arg != NULL) {
    dest[i] = malloc(strlen(arg)+1);
    strcpy(dest[i], arg);
    arg = strtok (NULL, " ");
    i++;
  }

  if (i == dest_size && arg != NULL) puts("Didn't receive all the args. Input error here.");
}

int next_line (char* text) {
  for (int i=0; i<strlen(text); i++)
    if (text[i] == '\n' || i+1 == strlen(text))
      return i+1;
  
  return -1;
}

int should_ignore_or_not (char* str_pointer, int previous_ignore_value) {
  int count, i;
  for(i=0, count=0; i<3; i++) {
    if (str_pointer[i] == '>') count++;
    if (str_pointer[i] == '<') count--;
  }

  int should_ignore = previous_ignore_value;
  if (count == 3 && !previous_ignore_value && str_pointer[i] == '\n') should_ignore = 1;
  if (count == -3 && previous_ignore_value && str_pointer[i] == '\n') should_ignore = 0;

  return should_ignore;
}

char* read_lines_notebook(char* nb_content, int index) {
  int i, index_count = 0, should_ignore = 0, bytes = 0, its_command = 0;
  for (i=0; index_count <= index; i++) {
    if (nb_content[i] == '<' && i+4 < strlen(nb_content)) {
      should_ignore = should_ignore_or_not(nb_content + i, should_ignore);
      i += 5;
    }
    if (should_ignore) continue;
    if (nb_content[i] == '>' && i+4 < strlen(nb_content)) should_ignore = should_ignore_or_not(nb_content + i, should_ignore);
    if (index_count == index) bytes++;
    if (nb_content[i] == '$') its_command = 1;
    if (nb_content[i] == '\n' && its_command) {index_count++; its_command=0;}
  }

  char* lines = malloc(sizeof(char) * bytes);
  strncpy(lines, nb_content + i - bytes, bytes);

  return lines;
}

char* get_all_commands(char* nb_content) {
  int i=0, size = 0, pos = 0;
  char* lines, *commands;
  
  do {
    if (i != 0) {
      while (pos < strlen(lines)) {
        int bytes = next_line(lines + pos);
        if (lines[pos] == '$') {
          if (size == 0) {
            commands = (char*) malloc(sizeof(char) * (bytes+1));
            strncpy(commands, lines+pos, bytes);
            // puts(commands);
          } else {
            char* tmp_ptr = (char*) realloc(commands, sizeof(char) * (size+bytes+1));
            if (tmp_ptr) commands = tmp_ptr;
            else return NULL;
            strncat(commands, lines+pos, bytes);
            // puts(commands);
          }
          size += bytes;
        }
        pos += bytes;
      }
      free(lines);
    }
    lines = read_lines_notebook(nb_content, i);
    pos = 0; i++;
  } while (strlen(lines) != 0);

  free(lines);
  return commands;
}

int get_how_many_commands_above (char* command) {
  int number = 0;
  if (command[0] == '|') number = 1;
  else if (command[0] >= '0' && command[0] <= '9') sscanf(command, "%d", &number);
  return number;
}

char* get_command2 (char* command_str) {
  int pos;
  for(pos=0; pos < strlen(command_str); pos++)
    if (command_str[pos] == ' ') {
      pos++; 
      break;
    }

  int bytes = next_line(command_str+pos);

  char* command = malloc(bytes+1);
  strncpy(command, command_str+pos, bytes);

  return command;
}

int set_command(COMMAND c, char* commands, int n_comm, int index) {
  int i, pos;
  for(i=0, pos=0; i<n_comm; i++, pos+=next_line(commands+pos)) {
    if (i == index) {
      c->index = index;
      c->command = get_command2(commands+pos);
      c->input_from_whom = index - get_how_many_commands_above(commands+pos+1);
      c->how_many_need_output = 0;
    } else if (i > index) {
      if (i - get_how_many_commands_above(commands+pos+1) == index) {
        if (c->how_many_need_output == 0) {
          c->who_needs_output = malloc(sizeof(int));
        } else {
          int* tmp_ptr = realloc(c->who_needs_output, sizeof(int)*(c->how_many_need_output+1));
          if (tmp_ptr) c->who_needs_output = tmp_ptr;
          else return -1;
        }
        c->who_needs_output[c->how_many_need_output] = i;
        c->how_many_need_output++;
      } 
    }
  }

  return 0;
}

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
    printf("Command: %s", c->command);
}

int count_commands (char* commands) {
 int count = 0;
  for (int i=0; i<strlen(commands); i++)
    if (commands[i] == '$') count++;
  return count;
}

char* get_path_input(char* path, int index_from, int index_to) {
  char* full_path = malloc(sizeof(char) * (strlen(path) + 22));
  sprintf(full_path, "%s_%d_%d", path, index_from, index_to);
  return full_path;
}

char* get_path_output(char* path, int index) {
  char* full_path = malloc(sizeof(char) * (strlen(path) + 11));
  sprintf(full_path, "%s_%d", path, index);
  return full_path;
}

void execute_single_command (char* command) {
  char* arr_args[ARGS] = {0};
  split_args(command, arr_args, ARGS);  
  switch (fork()) {
    case -1:
      puts("Couldn't create fork!");
      break;  
    case 0:
      execvp(arr_args[0], arr_args);
      _exit(-1);
    default:
      wait(NULL);
      for (int i=0; arr_args[i] != NULL && i<ARGS; i++) 
        free(arr_args[i]);
      break;
  }
}

int duplicate_pipe (char* path_prefix, COMMAND c) {
  char* path_to_pipe_input = get_path_input(path_prefix, c->input_from_whom, c->index);
  mkfifo(path_to_pipe_input, 0666);
  int fdr;
  if ((fdr = open(path_to_pipe_input, O_RDONLY, 0666)) < 0) {
    char str_err[strlen(path_to_pipe_input) + 20];
    sprintf(str_err, "Couldn't open pipe: %s", path_to_pipe_input);
    perror(str_err);
    return -1;
  } else {
    dup2(fdr, 0);
    close(fdr);
  }
  free(path_to_pipe_input);
  return 0;
}

int write_to_output_pipe (char* path_prefix, COMMAND c) {
  char* path_to_pipe_output = get_path_output(path_prefix, c->index);
  mkfifo(path_to_pipe_output, 0666);
  int fdw;
  if ((fdw = open(path_to_pipe_output, O_CREAT|O_WRONLY|O_TRUNC, 0666)) < 0) {
    char str_err[strlen(path_to_pipe_output) + 20];
    sprintf(str_err, "Couldn't open pipe: %s", path_to_pipe_output);
    perror(str_err);
    return -1;
  } else {
    dup2(fdw, 1);
    close(fdw);
    execute_single_command(c->command);
  }
  free(path_to_pipe_output);
  return 0;
}

int write_in_input_pipes (char* path_prefix, COMMAND c, char* buffer) {
  for (int i=0; i<c->how_many_need_output; i++)
    switch (fork()) {
      case -1:
        perror("Couldn't create fork!");
        _exit(-1);
      case 0: {
        char* path_to_input_pipe = get_path_input(path_prefix, c->index, c->who_needs_output[i]);
        mkfifo(path_to_input_pipe, 0666);
        int fdw;
        if ((fdw = open(path_to_input_pipe, O_RDONLY, 0666)) < 0) {
          char str_err[strlen(path_to_input_pipe) + 20];
          sprintf(str_err, "Couldn't open pipe: %s", path_to_input_pipe);
          perror(str_err);
          return -1;
        } else {
          write(fdw, buffer, BUFFER_SIZE);
          close(fdw);
        }
        free(path_to_input_pipe);
        _exit(0);
      } default:
        break;
    }
    return 0;
}

int write_in_file (char* path_prefix, COMMAND c, char* buffer) {
  int fdw;
  char* path_to_file = get_path_output(path_prefix, c->index);
  if ((fdw = open(path_to_file, O_CREAT|O_WRONLY|O_APPEND, 0666)) < 0) {
    char str_err[strlen(path_to_file) + 20];
    sprintf(str_err, "Couldn't open file: %s", path_to_file);
    perror(str_err);
    return -1;
  } else {
    write(fdw, buffer, BUFFER_SIZE);
    close(fdw);
  }
  return 0;
}

int output_pipe_to_input_pipes (char* path_prefix, COMMAND c) {
  char* path_to_output_pipe = get_path_output(path_prefix, c->index);
  mkfifo(path_to_output_pipe, 0666);
  int fdr;
  if ((fdr = open(path_to_output_pipe, O_RDONLY, 0666)) < 0) {
    char str_err[strlen(path_to_output_pipe) + 20];
    sprintf(str_err, "Couldn't open pipe: %s", path_to_output_pipe);
    perror(str_err);
    return -1;
  } else {
    char* buffer = malloc(sizeof(char)*BUFFER_SIZE);
    while (read (fdr, buffer, BUFFER_SIZE) < 0) {
      write_in_input_pipes(path_prefix, c, buffer);
      write_in_file("/tmp/Final", c, buffer);
    }
    close(fdr);
  }
  free(path_to_output_pipe);
  return 0;
}

int execute_all_commands(COMMAND commands[], int n_comm, char* path_prefix) {
  for(int i=0; i<n_comm; i++) {
    COMMAND c = commands[i];
    switch (fork()) {
      case -1:
        perror("Couldn't create fork!");
        _exit(-1);
      case 0: {
        if (c->index != c->input_from_whom) duplicate_pipe(path_prefix, c);
        write_to_output_pipe(path_prefix, c);
        output_pipe_to_input_pipes(path_prefix, c);
        _exit(0);
      } default:
        break;
    }
  }
  return 0;
}

int read_notebook (char* path_to_file) {
  // Gets the content of the file to a string
  char* nb_content = read_from_file(path_to_file);
  // Isolates all the commands in a single string
  char* str_commands = get_all_commands(nb_content);
  // Counts the number of commands
  int n_comm = count_commands(str_commands);
  // Initiates an array of COMMANDS
  COMMAND commands[n_comm];

  for(int i=0; i<n_comm; i++) {
    commands[i] = malloc(sizeof(struct command));
    set_command(commands[i], str_commands, n_comm, i);
    print_struct_command(commands[i]);
  }

  char path_to_pipe[] = "/tmp/Pipe";
  execute_all_commands(commands, n_comm, path_to_pipe);

  for(int i=0; i<n_comm; i++) {
    free(commands[i]->who_needs_output);
    free(commands[i]->command);
    free(commands[i]);
  }

  free(nb_content);
  free(str_commands);

  return 0;
}

void create_pipe(char* prefix, int no_pipe, char** pipe_storage, int no_comm) {
  char path_to_pipe[strlen(prefix) + 22];
  sprintf(path_to_pipe, "%s_%d_%d", prefix, no_comm, no_comm+1);
  mkfifo(path_to_pipe, 0666);
  pipe_storage[no_pipe] = malloc(sizeof(char) * strlen(path_to_pipe));
  strcpy(pipe_storage[no_pipe], path_to_pipe);
}

int execute_maybe_piped_between_pos (char* commands[], int begin, int end, char* output_path) {
  char pipe_prefix[] = "/tmp/SO_PIPE";
  
  char* pipes[end-begin];

  for(int i=0; i <= end-begin; i++) {
    if (i != end-begin) create_pipe(pipe_prefix, i, pipes, i+begin);
    int fdw, fdr;
    
    switch (fork()) {
      case -1:
        perror("Couldn't create fork!");
        _exit(-1);
      case 0:
        if (i!=0) {
          while ((fdr = open(pipes[i-1], O_RDONLY, 0666)) < 0);
          dup2(fdr, 0);
          close(fdr);
        }
        if (i != end-begin) {
          fdw = open(pipes[i], O_CREAT|O_WRONLY|O_TRUNC, 0666);
          //printf("Pipe open for write: %s\n", pipes[i]);
          dup2(fdw, 1);
          close(fdw);
          execute_single_command(commands[i+begin]);
          _exit(-1);
        } else {
          fdw = open(output_path, O_CREAT|O_WRONLY|O_APPEND, 0666);
          //printf("Outputs open for write: %s\n", pipes[i]);
          dup2(fdw, 1);
          close(fdw);
          execute_single_command(commands[i+begin]);
          _exit(-1);
        }
      default:
        break;      
    }
  }

  while(wait(NULL) > 0);

  for (int i=0; i<end-begin; i++) {
    unlink(pipes[i]);
    free(pipes[i]);
    //free(pipes);
  }

  return(0);
}

void append_to_file (char* file_path, char* str_to_append) {
  int fd = open(file_path, O_CREAT|O_WRONLY|O_APPEND, 0666);
  write(fd, str_to_append, strlen(str_to_append));
  close(fd);
}


int read_lines_append (char* contents, char* output_path) {
  int bytes = 0;
  for (int i=0; i<2; i++)
    bytes += next_line(contents+ bytes);

  char temp[bytes];
  strncpy(temp, contents, bytes);
  puts("::::::::::::::::::::::::");
  printf("%s", temp);

  append_to_file(output_path, temp);

  if (contents[bytes] == '>') puts("It's already processed!");

  return bytes;
}

int execute_commands (char** commands, char* content_file, char* output_path, int begin, int end) {
  int pos_content = 0;

  for (int i=begin; i <= end; i++) {
    pos_content += read_lines_append(content_file + pos_content, output_path);
    append_to_file(output_path, ">>>\n");
    execute_maybe_piped_between_pos(commands, begin, i, output_path);
    append_to_file(output_path, "<<<\n");
  }

  return pos_content;
}

char* get_command (char* command_str, int* pipe) {
  int begin=0, end = 0, size = strlen(command_str);
  
  if (command_str[1] == '|') { begin = 4; *pipe = 0;}
  else {begin = 3; *pipe = 1;}

  for (end = begin; end < size; end++)
    if (command_str[end] == '\n') {
      end--;
      break;
    } else if (end == size-1) 
      break;
  
  char* command = malloc(end-begin+2);
  strncpy(command, command_str+begin-1, end-begin+2);
  command[end-begin+2] = 0;

  return command;
}

int parse_and_execute (char* commands, char* initial_path, char* final_path) {
  int n_comm = count_commands(commands);
  char* commands_parsed[n_comm];
  int command_start[n_comm];

  for (int i=0, begin=0; i<n_comm; i++) {
    commands_parsed[i] = get_command(commands + begin, &command_start[i]);
    // printf("Piped: %d\nBytes: %d\nCommand: %s\n", command_start[i], begin, commands_parsed[i]);
    begin += next_line(commands + begin);
  }

  char* file_content = read_from_file(initial_path);
  
  int begin=0, end, pos_content=0;
  while (begin < n_comm) {
    if (command_start[begin]) {
      for(end=begin+1; end<n_comm; end++)
        if (command_start[end]) break;
      // printf("Begin: %d; End: %d\n", begin, end-1);
      pos_content += execute_commands(commands_parsed, file_content+pos_content, final_path, begin, end-1);
      begin = end;
    }
  }


  for (int i=0; i<n_comm; i++)
    free(commands_parsed[i]);
  return 0;
}

int process_notebook(char* file_path) {
  char commands_path[] = "/tmp/SO_COMMANDS.nb";
  
  int fd_nb;
  if ((fd_nb = open(file_path, O_RDONLY, 0666)) < 0) {
    char str_err[strlen(file_path) + 20];
    sprintf(str_err, "Couldn't open file: %s", file_path);
    perror(str_err);
    return(1);
  }

  char cm1[strlen(file_path) + 21];
  sprintf(cm1, "grep '^\\$' %s > %s", file_path, commands_path);
  system(cm1);
  
  char* commands = read_from_file(commands_path);
  remove(commands_path);

  // puts(commands);

  char final_path[] = "/tmp/FINAL_NB.nb";

  int ret = parse_and_execute(commands, file_path, final_path);
  free(commands);

  /*char* final_text = read_from_file(final_path);
  int fd = open(file_path, O_CREAT|O_WRONLY|O_TRUNC, 0666);
  write(fd, final_text, strlen(final_text));
  close(fd);
  free(final_text);

  remove(final_path);
*/
  return ret;
  // return 1;
}

int main (int argc, char* argv[]) {
  if (argc == 2) {
    //return process_notebook(argv[1]);
    read_notebook(argv[1]);
    // char* output = execute_command_return_output("ls -l", 0, "/tmp/Pipe");
    // puts(":::::::::::::::::::::::");
    // printf("%s", output);
    // puts(":::::::::::::::::::::::");
    // free(output);
    // char* command = get_command2("$1| head -1");
    // printf("Command: %s", command);
  } else {
    puts("Please execute the program and provide a path to the notebook!");
  }

  return 1;
}
