#include "stdio.h"

int vec_a[10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
int vec_b[10] = { 9, 8, 7, 6, 5, 4, 3, 2, 1, 0};

int ip()
{
	int i, sum = 0;

	for(i=0; i<10; i++)
		sum += vec_a[i] * vec_b[i];

	return sum;
}

int main()
{
	int a = ip() + 1;

	return 0;
}
