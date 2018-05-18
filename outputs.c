#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#define BUFFER_SIZE 10
#define ARGS 100
#define ARG_SIZE 100

char* read_from_file (char* path_to_file) {
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

  int fd;
  if ((fd = open(path_to_file, O_RDONLY, 0666)) < 0) {
    char str_err[strlen(path_to_file) + 20];
    sprintf(str_err, "Couldn't open file: %s", path_to_file);
    perror(str_err);
    _exit(-1);
  }
  
  char* contents = malloc(sizeof(char)*(byte_count));
  read(fd, contents, byte_count);
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

void execute_single_command (char* command) {
  char* arr_args[ARG_SIZE] = {0};
  split_args(command, arr_args, ARG_SIZE);  
  switch (fork()) {
    case -1:
      puts("Couldn't create fork!");
      break;  
    case 0:
      execvp(arr_args[0], arr_args);
      _exit(-1);
    default:
      wait(NULL);
      for (int i=0; arr_args[i] != NULL && i<ARG_SIZE; i++) 
        free(arr_args[i]);
      break;
  }
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

int next_line (char* text) {
  for (int i=0; i<strlen(text); i++)
    if (text[i] == '\n' || i+1 == strlen(text))
      return i+1;
  
  return -1;
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

int count_commands (char* commands) {
 int count = 0;
  for (int i=0; i<strlen(commands); i++)
    if (commands[i] == '$') count++;
  return count;
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
    return process_notebook(argv[1]);
  } else {
    puts("Please execute the program and provide a path to the notebook!");
  }

  return 1;
}
