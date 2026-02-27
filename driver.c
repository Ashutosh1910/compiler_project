#include <stdio.h>
#include <stdlib.h>
#include "lexer.h"
int main(int argc, const char ** args){
    int n=1;
    if(argc<2){
        printf("Enter file name to parse/lex\n");
        exit(1);
    }
    while (n!=0) {
        printf("Enter your choice:\n");
        scanf("%d",&n);
        switch (n) {
            case 0: continue;
            case 1: {
                removeComments(args[1]);
                break;
            }
            case 2:{
                printTokens(args[1]);
                break;
            }
            default: {
                printf("wrong choice\n"); 
                
                //return -1;
            }
            
        }   
    }
    return 0;
}