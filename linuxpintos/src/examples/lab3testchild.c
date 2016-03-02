#include <stdio.h>
#include <string.h>
#include <syscall.h>
#include <stdarg.h>

int main(void){
  printf("in child \n");
  exit(0);
}
