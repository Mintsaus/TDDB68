#include <stdio.h>
#include <string.h>
#include <syscall.h>
#include <stdarg.h>

int exec(const char *);

int main(int argc, char *argv[]){
  int c1, c2, c3, s1, s2;
  c1 = exec("lab3testchild");
  s1 = wait(c1);
  printf("\n Child 1 exited with status %d \n\n", s1);
  c2 = exec("lab3testchild");
  c3 = exec("lab3testchild");
  s2 = wait(c2);
  printf("\n Child 2 exited with status %d \n\n", s2);
  int i;
  for(i = 0; i < argc ; i++){
    printf("Argument %d: %s \n", i, argv[i]);
  }
  
  
  printf("Test passed\n");
  exit(0);
}
