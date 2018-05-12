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
  //for (int i=0; i<ARG_SIZE; i++)
    //if (arr_args[i]) puts(arr_args[i]);
  execvp(arr_args[0], arr_args);
}
/*
int guiao6ex2();
  int fdm, fdf;
  mkfifo("/tmp/logd", 0666);
  if (fork() != 0) {
    return 0;
  }
  fdm = open("/tmp/logmsgs", O_CREAT|O_WRONLY|O_APPEND, 0666);
  while ((fdf = open("/tmp/logd", O_RDONLY)) != -1) {
    int n;
    char buffer[512];
    n = read(fdf, buffer, sizeof(buffer));
    write(fdm, buffer, n);
    close(fdf);
  }
  return 0;
}
*/
int execute_commands_piped(char* commands[], int n_comm) {
  int fd[2];
  pipe(fd);

  for(int i=0; i<n_comm-1; i++) {
    if (fork() == 0) {
      dup2(fd[0],0);
      close(fd[0]);
      dup2(fd[1],1);
      close(fd[1]);
      execute_command(commands[i]);
      _exit(-1);
    } else {
      close(fd[1]);
      wait(NULL);
      char str[BUFFER_SIZE];
      int rd_count = read(fd[0], str, sizeof(str));
      str[rd_count] = '\0';
      close(fd[0]);
      puts("------------------");
      printf("This is what command %d put in the pipe: \n%s", i, str);
      //write(1, str, strlen(str));
      puts("------------------");
    }
  }
  //char final[BUFFER_SIZE];
  //if (!read(fd[0], final, sizeof(final))) return(-1);
  //write(1, final, strlen(final)+1);

  return 1;
}

int main (int argc, char* argv[]) {
  if (argc == 2) {
    char str[2000000];
    sprintf(str, "sed '/^\\$/ !d' < %s > temp.nb", argv[1]);
    printf("%s\n", str);
    system(str);
    outputs("temp.nb");
  } else {
    char* str[4];
    str[0] = "ls";
    str[1] = "sort";
    //str[2] = "head -1";
    printf("1st command: %s\n", str[0]);
    printf("2nd command: %s\n", str[1]);
    //puts("---------------------------");
    //printf("3rd command: %s\n", str[2]);
    execute_commands_piped(str, 2);
//    execute_command(str[0]);
    //execute_command(str[0]);
    //execute_command(str[1]);
    //execute_command(str[2]);
  }
  return 0;
}
