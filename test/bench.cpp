#include "lib/ac_fte.h"

#include <iostream>
#include <cstdio>

#include "common/debug.hpp"

unsigned page_size;

void perform_test(char *desc, unsigned int *order, char *buff, long unsigned size) {
    std::cout << "Starting " << desc << " access test..." << std::endl;
    TIMER_START(ti);
    for (unsigned int i = 1; i < 39; i++) {
        for (unsigned int j = 0; j < size / page_size; j++) {
            for (unsigned int k = 0; k < page_size; k++)
                buff[order[j] * page_size + k]++;
            //usleep(1);
        }
        if (i % 10 == 0)
            checkpoint();
        std::cout << ".";
        std::cout.flush();
    }
    TIMER_STOP(ti, desc << " access iterations complete");
}

int main(int argc, char *argv[]) {
    long unsigned size;
    unsigned *order;
    char *buff;
    
    if (argc != 2 || sscanf(argv[1], "%lu", &size) != 1)
        size = 1 << 30;

    page_size = getpagesize();
    order = (unsigned *)malloc(size * sizeof(unsigned) / page_size);
    
    start_checkpointer();
    buff = (char *)malloc(size);

    for (unsigned int i = 0; i < size / page_size; i++)
        order[i] = i;
    perform_test((char *)"ascending", order, buff, size);

    for (unsigned int i = 0; i < size / page_size; i++) {
        unsigned int j = rand() % (size / page_size), k = order[i];
        order[i] = order[j]; 
        order[j] = k;
    }
    perform_test((char *)"random", order, buff, size);

    for (unsigned int i = 0; i < size / page_size; i++)
        order[i] = (size / page_size) - i - 1;
    perform_test((char *)"descending", order, buff, size);

    terminate_checkpointer();
    
    return 0;
}
