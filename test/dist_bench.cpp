#include <boost/mpi.hpp>

#include "lib/ac_fte.h"

#define __DEBUG
#include "common/debug.hpp"

void timer(const char *bench_type, boost::mpi::communicator &comm) {
    comm.barrier();
    if (comm.rank() == 0)
	std::cout << "Starting test " << bench_type << std::endl;
    TIMER_START(timer_ckpt);
    checkpoint();
    TIMER_STOP(timer_ckpt, bench_type << " - duration of checkpoint()");
    std::cout << ".";
    wait_for_checkpoint();
    TIMER_STOP(timer_ckpt, bench_type << " - duration of wait_for_checkpoint()");
    std::cout << "+";
    comm.barrier();
    if (comm.rank() == 0) {
	TIMER_STOP(timer_ckpt, bench_type << " - total duration");
	std::cout << "Finished!" << std::endl;
    }
}

int main(int argc, char *argv[]) {
    long unsigned size;
    char *buff;    
    
    boost::mpi::environment env(argc, argv);
    boost::mpi::communicator comm;

    if (argc != 2 || sscanf(argv[1], "%lu", &size) != 1)
        size = 1 << 30;
    else
	size = 1 << size;

    start_checkpointer();

    buff = (char *)malloc_protected(size);
    if (buff == NULL) {
	ERROR("could not allocate buffer of size " << size);
	return -1;
    }
	
    size_t page_size = getpagesize();

    // same everywhere
    for (unsigned int i = 0; i < size / page_size; i++)
	memset(buff + i * page_size, 0xFF, page_size);
    timer("SAME EVERYWHERE", comm);


    // different locally, same everywhere
    for (unsigned int i = 0; i < size / page_size; i++)
	*((unsigned int *)(buff + i * page_size)) = i;
    timer("DIFF LOCALLY, SAME EVERYWHERE", comm);

    // different everywhere
    for (unsigned int i = 0; i < size / page_size; i++)
	*((unsigned int *)(buff + i * page_size + sizeof(unsigned int))) = comm.rank();
    timer("DIFF EVERYWHERE", comm);
    
    free_protected(buff, size);
    terminate_checkpointer();
    return 0;
}
