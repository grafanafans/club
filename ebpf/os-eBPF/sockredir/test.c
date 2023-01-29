#include<stdio.h>
#include<stdlib.h>
int main()
{
	int op=4;
	switch(op){
	   case 4: 
	   case 5:
	        printf("op:%d",op);break;
           case 6: 
		printf("op:%d",op);
	   default:
		break;
	}
	return 0;
}
