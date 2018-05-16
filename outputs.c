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

char* read_from_file_and_delete (char* path_to_file) {
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
    printf("Bytes: %d\n", byte_count);
  }

  int fd;
  if ((fd = open(path_to_file, O_RDONLY, 0666)) < 0) {
    perror("Couldn't open file specified!");
    _exit(-1);
  }
  
  char* contents = malloc(sizeof(char)*(byte_count));
  read(fd, contents, byte_count);
  contents[byte_count] = 0;

  remove(path_to_file);

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

void create_pipe(char* prefix, int no_pipe, char** pipe_storage) {
  char path_to_pipe[strlen(prefix) + 11];
  sprintf(path_to_pipe, "%s%d", prefix, no_pipe);
  mkfifo(path_to_pipe, 0666);
  pipe_storage[no_pipe] = malloc(sizeof(char) * strlen(path_to_pipe));
  strcpy(pipe_storage[no_pipe], path_to_pipe);
}

int execute_maybe_piped_between_pos (char* commands[], int begin, int end, char* output_path) {
  char pipe_prefix[] = "/tmp/SO_PIPE_";
  
  char** pipes = malloc(sizeof(char) * (strlen(pipe_prefix) + 11) * (end-begin+1));

  for(int i=begin; i <= end; i++) {
    if (i != end) create_pipe(pipe_prefix, i, pipes);
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
        if (i != end) {
          fdw = open(pipes[i], O_CREAT|O_WRONLY|O_TRUNC, 0666);
          //printf("Pipe open for write: %s\n", pipes[i]);
          dup2(fdw, 1);
          close(fdw);
          execute_single_command(commands[i]);
          _exit(-1);
        } else {
          fdw = open(output_path, O_CREAT|O_WRONLY|O_TRUNC, 0666);
          //printf("Outputs open for write: %s\n", pipes[i]);
          dup2(fdw, 1);
          close(fdw);
          execute_single_command(commands[i]);
          _exit(-1);
        }
      default:
        break;      
    }
  }

  while(wait(NULL) > 0);

  for (int i=begin; i<end; i++) {
    unlink(pipes[i]);
    free(pipes[i]);
    free(pipes);
  }

  return(0);
}

int execute_commands (char** commands, int begin, int end) {
  char out_prefix[] = "./SO_OUTPUT";
  for (int i=begin; i <= end; i++) {
    char output_path[strlen(out_prefix) + 21];
    sprintf(output_path, "%s_%d", out_prefix, i);
    puts(commands[i]);
    execute_maybe_piped_between_pos(commands, begin, i, output_path);
  }

  return 0;
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

int next_line (char* text) {
  for (int i=0; i<strlen(text); i++)
    if (text[i] == '\n' && i+1 != strlen(text))
      return i+1;
  
  return -1;
}

int parse_and_execute (char* commands) {
  int n_comm = count_commands(commands);
  char* commands_parsed[n_comm];
  int command_start[n_comm];

  for (int i=0, begin=0; i<n_comm; i++) {
    commands_parsed[i] = get_command(commands + begin, &command_start[i]);
    printf("Piped: %d\nBytes: %d\nCommand: %s\n", command_start[i], begin, commands_parsed[i]);
    begin += next_line(commands + begin);
  }

  int begin=0, end;
  while (begin < n_comm) {
    if (command_start[begin]) {
      for(end=begin+1; end<n_comm; end++)
        if (command_start[end]) break;
      printf("Begin: %d; End: %d\n", begin, end-1);
      execute_commands(commands_parsed, begin, end-1);
      begin = end;
    }
  }

  return 0;
}

int process_notebook(char* file_path) {
  char commands_path[] = "/tmp/SO_COMMANDS.nb";
  
  int fd_nb;
  if ((fd_nb = open(file_path, O_RDONLY, 0666)) < 0) {
    perror("Couldn't open file specified!");
    return(1);
  }

  char cm1[strlen(file_path) + 21];
  sprintf(cm1, "grep '^\\$' %s > %s", file_path, commands_path);
  system(cm1);
  
  char* commands = read_from_file_and_delete(commands_path);
  
  puts(commands);

  // return parse_and_execute(commands);^
  return 1;
}

int main (int argc, char* argv[]) {
  if (argc == 2) {
    process_notebook(argv[1]);
  } else {
    char* str[5];
    str[0] = "ls";
    str[1] = "sort";
    str[2] = "head -1";
    str[3] = "wc -c";
    printf("1st command: %s\n", str[0]);
    printf("2nd command: %s\n", str[1]);
    printf("3rd command: %s\n", str[2]);
    printf("4th command: %s\n", str[3]);
    //execute_(str, 0, 3);
    execute_maybe_piped_between_pos(str, 0, 0, "./SO_OUTPUT_0");
    execute_maybe_piped_between_pos(str, 0, 1, "./SO_OUTPUT_1");
    execute_maybe_piped_between_pos(str, 0, 2, "./SO_OUTPUT_2");
    execute_maybe_piped_between_pos(str, 0, 3, "./SO_OUTPUT_3");
    //char commands[] = "$ ls\n$| sort\n$| head -1\n$| wc -c";
    //char *command;
    //int n = get_command("$| head -1", &command);
    //parse_and_execute(commands);
    // puts("You either haven't specified a file path or you've input too many!");

    return 1;
  }

  return 0;
}
