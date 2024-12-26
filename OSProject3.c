#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#define BUFFER_SIZE 5 // Buffer size for producer-consumer
#define NUM_THREADS 4 // Number of threads for each task
pthread_mutex_t memory_mutex;
pthread_mutex_t buffer_mutex;
sem_t buffer_full, buffer_empty;

int buffer[BUFFER_SIZE]; // Converts the buffer size to an array
int buffer_index = 0; // Keeps track of buffer position

#define RAM_SIZE 1024 // Amount of RAM
#define L1_SIZE 64 // L1 cache size
#define L2_SIZE 128 // L2 cache size
#define MAX_PROCESSES 3 // Max amount of processes
#define TOTAL_MEM 1024 // Total memory size
#define NUM_BLOCKS 10 // Number of blocks for allocation
#define TIME_SLICE 2 // Time slice for scheduling

// Different levels of memory
int RAM[RAM_SIZE]; // Array for RAM
int L1Cache[L1_SIZE]; // Array for L1
int L2Cache[L2_SIZE]; // Array for L2

// Represents memory blocks for allocation
struct MemoryBlock {
    int processID; // Process id for the block
    int memoryStart; // Start address of the block
    int memoryEnd; // End address of the block
    int isFree; // Free flag: 1 = free, 0 = allocated
};

// Memory table of all the blocks
struct MemoryBlock memoryTable[NUM_BLOCKS];

// CPU Registers and flags
int PC = 0; // Program counter
int ACC = 0; // Accumulator
int IR = 0; // Instruction register
int ZF = 0; // Zero flag
int CF = 0; // Carry flag
int OF = 0; // Overflow flag

// Represents the Process Control Block
struct PCB {
    int pid; // Process ID
    int pc; // Program counter 
    int acc; // Accumulator 
    int state; // Process state: 0 not started, 1 running, 2 finished
};

// Table with the Process Control Block 
struct PCB processTable[MAX_PROCESSES];

// Interrupt handling 
void (*IVT[3])(); // Pointer for different types of interrupts
pthread_mutex_t interrupt_mutex; // Mutex syncs iterrupt handling

// Time tracking
double cpu_execution_time = 0; // Execution of CPU task
double memory_execution_time = 0; // Execution of memory management task
double scheduling_execution_time = 0; // Execution time of scheduling task
double interrupt_execution_time = 0; // Execution time of interrupt handling task

// Function initialization of the operations
void fetch(); // Fetch the next instruction
void decode(); // Decode the fetched instruction
void execute(); // Execute the decoded instruction
void updateStatusFlags(int result); // Updates CPU status flags based on the result
void loadProgram(); // Load a program into RAM

// Memory management initialization
void initMemoryTable(); // Initializes memory table
void allocateMemory(int processID, int size); // Allocates memory
void allocateBestFit(int processID, int size); // Allocates memory 

// Process initialization
void initProcesses(); // Initializes process tables 
void scheduler(); // Initializes scheduler 
void contextSwitch(int currentProcess, int nextProcess); // Context switch between processes 

// Interrupt handling Initialization
void initMutex(); // Initializes mutex
void handleInterrupt(int interruptType); // Basic interrupt handler 
void timerInterrupt(); // Timer interrupt handler 
void ioInterrupt(); // I/O interrupt handler 
void systemCallInterrupt(); // System call interrupt handler 
void dispatcher(int currentProcess, int nextProcess); // Dispatcher to switch processes 

// CPU Task: fetch-decode-execute cycle
void* cpuTask(void* arg) {
    clock_t start_time = clock(); // Records start time for time measurement 

    while (1) {
        pthread_mutex_lock(&memory_mutex); // Locks the mutex for memory allocation 
        fetch(); // Fetches the next instruction 
        decode(); // Decodes the fetched instruction 
        execute(); // Executes the instruction

        // Checks if the instruction its a halt instruction
        if (IR == 0) {
            pthread_mutex_unlock(&memory_mutex); // Unlocks the memory
            break; // Exits the loop 
        }

        pthread_mutex_unlock(&memory_mutex); // Unlocks memory for more threads to access 
        usleep(1000); // Goes to sleep 
    }

    clock_t end_time = clock(); // Records the end time of execution 
    cpu_execution_time = (double)(end_time - start_time) / CLOCKS_PER_SEC; // Calculates execution time 
    return NULL;
}

// Memory Management Task: memory allocation, cache management
void* memoryTask(void* arg) {
    clock_t start_time = clock(); // Records start time for time measurement 
    int iterations = 0; // Iteration for limiting 

    // Allocates memory 3 times 
    while (iterations < 3) {
        pthread_mutex_lock(&memory_mutex); // Locks the memory 
        allocateBestFit(1, 50); // Allocates 50 bytes to P1 
        pthread_mutex_unlock(&memory_mutex); // Unlocks memory 
        sleep(5); // Sleeps 
        iterations++; // Increases iteration count 
    }

    clock_t end_time = clock(); // Records the end time of execution
    memory_execution_time = (double)(end_time - start_time) / CLOCKS_PER_SEC; // Calculates execution time 
    return NULL;
}

// Scheduling Task: Handles process scheduling logic
void* schedulingTask(void* arg) {
    clock_t start_time = clock(); // Records start time for time measurement 


    while (1) {
        scheduler(); // Calls the scheduler for process execution 
        sleep(1); // Sleeps for 1 second 
        int allFinished = 1;

        // Loops through the processes to check their state 
        for (int i = 0; i < MAX_PROCESSES; i++) {
            pthread_mutex_lock(&memory_mutex); // Locks the memory   

            // If the process isn't finished 
            if (processTable[i].state != 2) {
                allFinished = 0; // Sets flag showing process isn't done 
                pthread_mutex_unlock(&memory_mutex); // Unlocks the memory 
                break; // Exits loop
            }

            pthread_mutex_unlock(&memory_mutex); // Unlocks the memory 
        }

        // If the processes are done 
        if (allFinished) {
            break; // Exits loop 
        }
    }
    clock_t end_time = clock(); // Records the end time of execution
    scheduling_execution_time = (double)(end_time - start_time) / CLOCKS_PER_SEC; // Calculates execution time 
    return NULL;
}

// Interrupt Handling Task: Different interrupts
void* interruptTask(void* arg) {
    clock_t start_time = clock(); // Records start time for time measurement 
    int count = 0; // counter to limit the loop 

    // Handles interrupts 3 times 
    while (count < 3) {
        sleep(3); 
        handleInterrupt(0); // Handles timer interrupt 
        handleInterrupt(1); // Handles I/O interrupt 
        handleInterrupt(2); // Handles system called interrupt 
        count++; // Increases loop counter 
    }

    clock_t end_time = clock(); // Records the end time of execution
    interrupt_execution_time = (double)(end_time - start_time) / CLOCKS_PER_SEC; // Calculates execution time 
    return NULL;
}

// Producer-Consumer Task: Producer function
void* producer(void* arg) {
    while (1) {
        sem_wait(&buffer_empty); // Waits if the buffer is full
        pthread_mutex_lock(&buffer_mutex); // Locks the buffer mutex 
        int data = rand() % 100; // Generates random data 
        buffer[buffer_index] = data; // Adds the produced data to the index 
        printf("Produced data: %d\n", data); // Prints the produced data 
        buffer_index = (buffer_index + 1) % BUFFER_SIZE; // Updates the buffer index 
        pthread_mutex_unlock(&buffer_mutex); // Unlocks the buffer mutex 
        sem_post(&buffer_full); // Signals that the buffer new data 
        sleep(1); // Sleeps
    }
    return NULL;
}

// Producer-Consumer Task: Consumer function
void* consumer(void* arg) {
    while (1) {
        sem_wait(&buffer_full); // Waits if the buffer is full 
        pthread_mutex_lock(&buffer_mutex); // Locks the buffer mutex 
        buffer_index = (buffer_index - 1 + BUFFER_SIZE) % BUFFER_SIZE; // Moves the buffer index backwards 
        int data = buffer[buffer_index]; // Gets the data from the buffer index 
        printf("Consumed data: %d\n", data); // Prints the consumed data
        pthread_mutex_unlock(&buffer_mutex); // Unlocks the buffer mutex
        sem_post(&buffer_empty); // Signal's that the buffer isn't empty
        sleep(1); // Sleeps 
    }
    return NULL;
}

// Gets the instruction from RAM
void fetch() {
    IR = RAM[PC]; // Gets the instruction from RAM at the given index 
    printf("Fetched instruction %d at PC=%d\n", IR, PC); // Prints the fetched instruction
}

void decode() {} // Function to simulate decoding 

// Updates the status flags 
void updateStatusFlags(int result) {
    ZF = (result == 0) ? 1 : 0; // Sets zero flag if result is 0
    CF = (result < 0) ? 1 : 0; // Sets carry flag if result is negative 
    OF = (result > ACC) ? 1 : 0; // Sets overflow flag if results are higher than ACC
}

// Executes the instructions 
void execute() {
    int result; // Stores results from the operation 

    // Switches between the instruction based on IR 
    switch (IR) {
        case 1: // add operation
            printf("Executing ADD instruction at PC=%d\n", PC); // Print execution statement 
            result = ACC + RAM[PC + 1]; // Performs the operation
            ACC = result; // Stores the result 
            updateStatusFlags(result); // Updates the flag 
            break;

        case 2: // subtract operation
            printf("Executing SUB instruction at PC=%d\n", PC); // Print execution statement 
            result = ACC - RAM[PC + 1]; // Performs the operation
            ACC = result; // Stores the result 
            updateStatusFlags(result); // Updates the flag 
            break;

        case 3: // multiply operation
            printf("Executing MUL instruction at PC=%d\n", PC); // Print execution statement 
            result = ACC * RAM[PC + 1]; // Performs the operation
            ACC = result; // Stores the result 
            updateStatusFlags(result); // Updates the flag 
            break;

        case 4: // load operation
            printf("Executing LOAD instruction at PC=%d\n", PC); // Print execution statement 
            ACC = RAM[PC + 1]; // Performs the operation
            updateStatusFlags(ACC); // Updates the flag 
            break;

        case 5: // store operation
            printf("Executing STORE instruction at PC=%d\n", PC); // Print execution statement 
            RAM[PC + 1] = ACC; // Performs the operation
            break;

        case 6: // jump operation
            printf("Executing JUMP instruction to PC=%d\n", RAM[PC + 1]); // Print execution statement 
            PC = RAM[PC + 1]; // Performs the operation
            return;

        case 7: // divide operation
            printf("Executing DIV instruction at PC=%d\n", PC); // Print execution statement 

            // If statement to check if dividing by 0
            if (RAM[PC + 1] != 0) {
                result = ACC / RAM[PC + 1]; // Performs the operation
                ACC = result; // Stores the result 
                updateStatusFlags(result); // Updates flag 

            // Division by zero case:
            } else {
                printf("Division by zero at PC = %d\n", PC); // Print message for dividing by 0
                pthread_mutex_unlock(&memory_mutex); // Unlocks memory 
                pthread_exit(NULL); // Exits 
            }
            break;

        case 8: // and operation 
            printf("Executing AND instruction at PC=%d\n", PC); // Print execution statement 
            result = ACC & RAM[PC + 1]; // Performs the operation
            ACC = result; // Stores the result 
            updateStatusFlags(result); // Updates the flag 
            break;

        case 9: // or operation
            printf("Executing OR instruction at PC=%d\n", PC); // Print execution statement 
            result = ACC | RAM[PC + 1]; // Performs the operation
            ACC = result; // Stores the result 
            updateStatusFlags(result); // Updates the flag 
            break;

        case 10: // jump if zero
            printf("Executing JZ instruction to PC=%d if ZF=%d\n", RAM[PC + 1], ZF); // Print execution statement 

            // If the zero flag was set...
            if (ZF) {
                PC = RAM[PC + 1]; // Updates program counter to new address 
                return; // Exit function
            }
            break;

        // Halt operation
        case 0:
            printf("Program halted.\n"); // Message stating program is halted 
            pthread_mutex_unlock(&memory_mutex); // Unlocks the memory 
            pthread_exit(NULL); // Exits thread 

        // For unknown instructions 
        default:
            printf("Unknown instruction at PC = %d\n", PC); // Prints unknown instruction message
            pthread_mutex_unlock(&memory_mutex); // Unlocks the memory 
            pthread_exit(NULL); // Exits thread 
    }
    PC += 2; // Increments program counter by 2
}

// Loads the program instructions into RAM
void loadProgram() {
    RAM[0] = 1; // add
    RAM[1] = 5; // operand
    RAM[2] = 7; // divide
    RAM[3] = 1; // operand
    RAM[4] = 8; // and
    RAM[5] = 3; // operand
    RAM[6] = 9; // or
    RAM[7] = 2; // operand
    RAM[8] = 10; // jump if zero
    RAM[9] = 0; // jump target (address)
    RAM[10] = 0; // halt
}

// Initializes the memory table with blocks 
void initMemoryTable() {

    // Loops through the amount of blocks 
    for (int i = 0; i < NUM_BLOCKS; i++) {
        memoryTable[i].processID = -1; // -1 indicates that the block is free 
        memoryTable[i].memoryStart = i * (TOTAL_MEM / NUM_BLOCKS); // Records the start of the block
        memoryTable[i].memoryEnd = (i + 1) * (TOTAL_MEM / NUM_BLOCKS) - 1; // Records the end of the block 
        memoryTable[i].isFree = 1; // Marks block free 
    }
}

// Allocates memory to a process
void allocateMemory(int processID, int size) {

    // Loops through the blocks to find a free one
    for (int i = 0; i < NUM_BLOCKS; i++) {

        // Checks if the block is free and has the needed size 
        if (memoryTable[i].isFree && (memoryTable[i].memoryEnd - memoryTable[i].memoryStart + 1) >= size) {
            memoryTable[i].processID = processID; // Assigns block to the process 
            memoryTable[i].isFree = 0; // Marks block as allocated
            printf("Allocated %d bytes to process %d in block %d\n", size, processID, i); // Print message for allocation
            return;
        }
    }

    printf("Memory allocation failed for process %d\n", processID); // Print message in case allocation fails
}


// Function for best fit allocation
void allocateBestFit(int processID, int size) {
    int bestIdx = -1; // Starts with base index of -1
    int bestCapacity = TOTAL_MEM; // Initializes the best capacity to the max allowed

    // Loops through the memory blocks to find the best fit for the size 
    for (int i = 0; i < NUM_BLOCKS; i++) {

        // Checks if the block is free
        if (memoryTable[i].isFree) {
            int blockCapacity = memoryTable[i].memoryEnd - memoryTable[i].memoryStart + 1; // Finds the capacity of the block 

            // Checks if the block can fit the size and is a better fit than the previous one 
            if (blockCapacity >= size && blockCapacity < bestCapacity) {
                bestCapacity = blockCapacity; // Updates the best capacity 
                bestIdx = i; // Updates the best index
            }
        }
    }

    // Checks if a block is found
    if (bestIdx != -1) {
        memoryTable[bestIdx].processID = processID; // Assigns the block to the process 
        memoryTable[bestIdx].isFree = 0; // Block is marked as allocated 
        printf("Best-fit allocated %d bytes to process %d in block %d\n", size, processID, bestIdx); // Prints the details of the allocation

    } else {
        printf("Best-fit allocation failed for process %d\n", processID); // Prints messasge if fails 
    }
}

// Initializes the processes
void initProcesses() {

    // Loops through and initializes the processes
    for (int i = 0; i < MAX_PROCESSES; i++) {
        processTable[i].pid = i + 1; // PID starts from 1
        processTable[i].pc = 0; // Program counter starts at 9
        processTable[i].acc = 0; // Accumulator starts at 0
        processTable[i].state = 0; // Initial state is 0
    }
}

void scheduler() {
    static int currentProcess = 0; // Keeps track of the current process index 
    pthread_mutex_lock(&memory_mutex); // Locks the memory 

    // Checks if the current process is started or running 
    if (processTable[currentProcess].state == 0 || processTable[currentProcess].state == 1) {
        processTable[currentProcess].state = 1; // Sets the process state to running (1)
        printf("Process %d is running.\n", processTable[currentProcess].pid); // Print message showing its running 

        processTable[currentProcess].pc += 1; // Simulates processing 

        // Checks if the process is completed
        if (processTable[currentProcess].pc >= 5) {
            processTable[currentProcess].state = 2; // If so, state is set to finished (2)
            printf("Process %d has finished execution.\n", processTable[currentProcess].pid); // Print message stating its finished 
        }
    }

    pthread_mutex_unlock(&memory_mutex); // Unlocks memory 
    currentProcess = (currentProcess + 1) % MAX_PROCESSES; // Moves to the next process 
}

// Performs context switching
void contextSwitch(int currentProcess, int nextProcess) {

    // Saves the state of the current process before switching 
    processTable[currentProcess].pc = PC; // Saves the program counter 
    processTable[currentProcess].acc = ACC; // Saves the accumulator 
    processTable[currentProcess].state = 0; // Sets the state to not running (0)

    // Loads the state of the next process to switch into 
    PC = processTable[nextProcess].pc; // Loads the program counter 
    ACC = processTable[nextProcess].acc; // Loads the accumulator 
    processTable[nextProcess].state = 1; // Sets the state to running (1)
}

// Initializes mutex 
void initMutex() {
    pthread_mutex_init(&interrupt_mutex, NULL); // Initializes the interrupt for interrupt handling 
}

// Handles interrupt depending which one is raised 
void handleInterrupt(int interruptType) {
    pthread_mutex_lock(&interrupt_mutex); // Locks the interrupt 

    // Checks if the interrupt is valid 
    if (interruptType >= 0 && interruptType < 3) {
        IVT[interruptType](); // Calls the interrupt based on it's value 
    }

    pthread_mutex_unlock(&interrupt_mutex); // Unlocks the interrupt 
}

// Interrupt handler for timer interrupts
void timerInterrupt() {
    printf("Timer interrupt handled.\n"); // Print message stating timer interrupt occured 
}

// Interrupt handler for I/O interrupts
void ioInterrupt() {
    printf("I/O interrupt handled.\n"); // Print message stating I/O interrupt occured 
}

// Interrupt handler for system call interrupts
void systemCallInterrupt() {
    printf("System call interrupt handled.\n"); // Print message stating syscall interrupt occured 
}

// Dispatches processes through context switching 
void dispatcher(int currentProcess, int nextProcess) {
    contextSwitch(currentProcess, nextProcess); // Calls the context switch to switch the processes 
}

// Main function to run the simulation 
int main() {
    loadProgram(); // Loads the programs into memory 
    initMemoryTable(); // Initializes the memory table 
    initProcesses(); // Initializes the process table 

    // Initializes mutex and semaphores
    pthread_mutex_init(&memory_mutex, NULL); // Initializes memory mutex 
    pthread_mutex_init(&buffer_mutex, NULL); // Initializes buffer mutex
    sem_init(&buffer_full, 0, 0); // Buffer starts as empty
    sem_init(&buffer_empty, 0, BUFFER_SIZE); // Buffer has BUFFER_SIZE empty slots

    initMutex(); // Initializes interrupt mutex 

    // Initializes the interrupts
    IVT[0] = timerInterrupt; // Initilizes timer interrupt 
    IVT[1] = ioInterrupt; // Initilizes I/O interrupt
    IVT[2] = systemCallInterrupt; // Initilizes system call 

    // Creates threads for each main module task
    pthread_t threads[NUM_THREADS];
    pthread_create(&threads[0], NULL, cpuTask, NULL); // Creates thread for CPU task
    pthread_create(&threads[1], NULL, memoryTask, NULL); // Creates thread for Memory management
    pthread_create(&threads[2], NULL, schedulingTask, NULL); // Creates thread for Scheduling
    pthread_create(&threads[3], NULL, interruptTask, NULL); // Creates thread for Interrupt handling

    // Creates threads for producer and consumer 
    pthread_t producer_thread, consumer_thread;
    pthread_create(&producer_thread, NULL, producer, NULL); // Creates thread for producer 
    pthread_create(&consumer_thread, NULL, consumer, NULL); // Creates thread for consumer

    sleep(20); // Runs the simulation for 20 seconds

    // Cancels threads after the simulation is completed 
    pthread_cancel(producer_thread); // Cancels producer thread
    pthread_cancel(consumer_thread); // Cancels consumer thread

    pthread_join(producer_thread, NULL); // Waits for producer thread to complete 
    pthread_join(consumer_thread, NULL); // Waits for consumer thread to complete 

    // Waits for all the module threads to complete
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL); // Waits for each thread to complete
    }

    // Cleans up mutexes and semaphores
    pthread_mutex_destroy(&memory_mutex); // Destroys memory mutex
    pthread_mutex_destroy(&buffer_mutex); // Destroys buffer mutex
    sem_destroy(&buffer_full); // Destroys semaphore full buffer mutex
    sem_destroy(&buffer_empty); // Destroys semaphore empty buffer mutex
    pthread_mutex_destroy(&interrupt_mutex); // Destroys interrupt mutex 

    // Prints the execution times
    printf("\n--- Execution Times ---\n");
    printf("CPU Task Execution Time: %.6f seconds\n", cpu_execution_time); // Prints CPU task execution time 
    printf("Memory Management Task Execution Time: %.6f seconds\n", memory_execution_time); // Prints memory management execution time 
    printf("Scheduling Task Execution Time: %.6f seconds\n", scheduling_execution_time); // Displays scheduling execution time
    printf("Interrupt Handling Task Execution Time: %.6f seconds\n", interrupt_execution_time); // Displays interrupt handling execution time 

    return 0; // Exits
}
