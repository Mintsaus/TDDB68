#include <stdio.h>
#include <string.h>
#include <syscall.h>
#include <stdarg.h>

int exec(const char *);

int main(void){
  int c1, c2, c3;
  c1 = exec("lab3testchild");
  c2 = exec("lab3testchild");
  c3 = exec("lab3testchild");
  printf("Test passed\n");
  printf("%d\n%d\n%d\n", c1, c2, c3);
  exit(0);
}
