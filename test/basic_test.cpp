#include <iostream>

#include "lib/ac_fte.h"
#include <cstring>
#include <stdlib.h>
#include <unistd.h>

using namespace std;

int main(int argc, char **argv) {
    start_checkpointer();
    
    cout << "Alloc 16 MB of data..." << endl;
    
    unsigned int size = 1 << 24, i;

    char *buffer = (char *)malloc(size);
    memset(buffer, 'A', size);

    cout << "Checkpointing..." << endl;
    
    checkpoint();
    memset(buffer, 'B', size);

    cout << "Testing result...";
    for (i = 0; i < size; i++) 
	if (buffer[i] != 'B' ) {
	    cout << "FAILED at offset: " << i << endl;
	    for (unsigned int j = 0; j < size; j++)
		cout << buffer[j];
	    cout << endl;
	    break;
	}
    if (i == size)
	cout << "OK!" << endl;
    sleep(5);
    terminate_checkpointer();
    return 0;
}
