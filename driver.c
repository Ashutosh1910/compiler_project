#include <stdio.h>
#include <stdlib.h>
#include "lexer.h"
int main(){
    int n=1;
    while (n!=0) {
        printf("Enter your choice:\n");
        scanf("%d",&n);
        switch (n) {
            case 0: continue;
            case 1: {
                removeComments();
                break;
            }
            case 2:{
                printTokens();
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