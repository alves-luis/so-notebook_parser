#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#define BUFFER_SIZE 1024
#define ARGS 100
#define ARG_SIZE 100

int outputs(char* pathname) {
  printf("File name: %s\n", pathname);
  int file = open(pathname, O_RDONLY);
  if (file < 0) exit(-1);
  int byte_count;

  int fd[2];
  pipe(fd);
  if (fork() == 0) {
    close(fd[0]);
    dup2(fd[1], 1);
    close(fd[1]);
    execlp("wc", "wc", "--bytes", pathname, NULL);
    _exit(-1);
  } else {
    wait(NULL);
    close(fd[1]);
    char str[1024];
    read(fd[0], str, sizeof(str));
    close(fd[0]);
    sscanf(str, "%d", &byte_count);
    printf("Bytes: %d\n", byte_count);
  }

  char commands[byte_count];
  int flag;
  if ((flag = read(file, commands, sizeof(commands))) < 0) exit(-2);

  printf("%s", commands);

  close(file);

  return 0;
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

void execute_command (char* command) {
  char* arr_args[ARG_SIZE] = {0};
  split_args(command, arr_args, ARG_SIZE);
  execvp(arr_args[0], arr_args);
}

void create_pipe(char* prefix, int no_pipe, char** pipe_storage) {
  char path_to_pipe[strlen(prefix) + 11];
  sprintf(path_to_pipe, "%s%d", prefix, no_pipe);
  mkfifo(path_to_pipe, 0666);
  pipe_storage[no_pipe] = malloc(sizeof(char) * strlen(path_to_pipe));
  strcpy(pipe_storage[no_pipe], path_to_pipe);
}

int execute_commands_piped (char* commands[], int n_comm) {
  int child_pid, i;
  char pipe_prefix[] = "./SO_PIPE_";

  char** pipes = malloc(sizeof(char) * (strlen(pipe_prefix) + 11) * n_comm);

  for(i=0; i <= n_comm; i++) {
    create_pipe(pipe_prefix, i, pipes);
    
    int fdw, fdr;
    switch (child_pid = fork()) {
      case -1:
        perror("Couldn't create fork!");
        _exit(-1);
      case 0:
        if (i!=0) {
          while ((fdr = open(pipes[i-1], O_RDONLY, 0666)) < 0);
          //printf("Pipe open for read: %s\n", pipes[i-1]);
          if (i == n_comm) _exit(0);
          dup2(fdr, 0);
          close(fdr);
        }
        if (i != n_comm) {
          fdw = open(pipes[i], O_CREAT|O_WRONLY|O_TRUNC, 0666);
          //printf("Pipe open for write: %s\n", pipes[i]);
          dup2(fdw, 1);
          close(fdw);
          execute_command(commands[i]);
          _exit(-1);
        }
      default:
        break;      
    }
  }
  return(0);
}

int test1 () {
  char* str[5];
  str[0] = "ls";
  str[1] = "sort";
  str[2] = "head -1";
  str[3] = "wc -c";
  printf("1st command: %s\n", str[0]);
  printf("2nd command: %s\n", str[1]);
  printf("3rd command: %s\n", str[2]);
  printf("4th command: %s\n", str[3]);
  execute_commands_piped(str, 1);
  //execute_command(str[0]);
  //execute_command(str[0]);
  //execute_command(str[1]);
  //execute_command(str[2]);
  return 0;
}

int main (int argc, char* argv[]) {
  if (argc == 2) {
    char str[2000000];
    sprintf(str, "sed '/^\\$/ !d' < %s > temp.nb", argv[1]);
    printf("%s\n", str);
    system(str);
    outputs("temp.nb");
  } else {
    return test1();
    //return test2();
  }

  return 0;
}
