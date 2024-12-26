#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <semaphore.h>
#include <time.h>
#include <signal.h>
#include <string.h>

#define MAX_PROCESSES 10 // Defines maximum number of processes
#define RAM_SIZE 1024 // Defines the RAM size
#define L1_SIZE 64 // Defines the L1 Cache size
#define L2_SIZE 256 // Defines the L2 Cache size
#define MAX_BLOCKS 10 // Defines max number of mem blocks

pthread_mutex_t interrupt_mutex; // Protects interrupt handling

// Defines interrupts, 0=Timer, 1=IO, 2=Syscall, 3=Trap
void (*IVT[4])();

// Memory representation
int RAM[RAM_SIZE]; // Represents RAM
int L1Cache[L1_SIZE]; // Represents L1 Cache
int L2Cache[L2_SIZE]; // Represents L2 Cache

// Represents a memory block in a table
struct MemoryBlock {
    int processID; // Process ID for the block
    int startAddress; // Start address of the block
    int endAddress; // End address of the block
    int isFree; // Flag indicating if block is free
};

// Memory block managements
struct MemoryBlock memoryTable[MAX_BLOCKS];

int PC = 0; // Program counter
int ACC = 0; // Accumulator
int IR = 0; // Instruction Register
int ZF = 0; // Zero flag

// Scheduler and queues framework
#define TIME_QUANTUM 2 // Time quantum for Round-Robin scheduling
#define MAX_PRIORITY 10 // Maximum priority level for a process
#define QUEUE_LEVELS 3 // Number of feedback queue levels
#define TIME_QUANTUM_LEVEL1 2 // Time quantum for Level 1 feedback queue
#define TIME_QUANTUM_LEVEL2 4 // Time quantum for Level 2 feedback queue
#define TIME_QUANTUM_LEVEL3 8 // Time quantum for Level 3 feedback queue

// Feedback queues for feedback scheduler
int feedbackQueues[QUEUE_LEVELS][MAX_PROCESSES]; // Multi-level feedback queues
int front[QUEUE_LEVELS] = {0}; // Front indices for queues
int rear[QUEUE_LEVELS] = {0}; // Rear indices for queues
int feedbackCount[QUEUE_LEVELS] = {0}; // Count of processes in each queue

// Process state:
typedef enum { READY, RUNNING, BLOCKED, FINISHED } ProcessState;

// Represents the Process Control Block
struct PCB {
    int pid; // Process ID
    int pc; // Program counter
    int acc; // Accumulator
    ProcessState state; // State of process
    int priority; // Priority of process
    int timeRemaining; // Time remaining for process
    float responseRatio; // Response ratio for HRRN scheduling
    int arrivalTime; // Time when process arrived
    int burstTime; // Burst time of the process
    int finishTime; // Time when process finished
};

// Process management
struct PCB processTable[MAX_PROCESSES];
int totalProcesses = 0; // Total number of processes

pthread_mutex_t memory_mutex; // Protects memory operations
sem_t buffer_full, buffer_empty; // Consumer/Producer operations

// Shared memory boundaries
#define SHARED_MEM_START 900 // Start address for shared memory
#define SHARED_MEM_END 999 // End address for shared memory

// Consumer/Producer initialization
void *producer(void *arg); // Producer thread
void *consumer(void *arg); // Consumre thread

// Interrupt initialization
void handleInterrupt(int interruptType); // Interrupt handler
void timerInterrupt(); // Timer interrupt
void ioInterrupt(); // IO interrupt
void systemCallInterrupt(); // System Call Interrupt
void trapInterrupt(); // Trap interrupt

void initMutex(); // Mutex initialization

// Cache management initialization
int checkL1(int address); // Checks if address is in L1 cache
int checkL2(int address); // Check if address is in L2 cache
int cacheLookup(int address); // Perform cache lookup
void cacheWriteThrough(int address, int value); // Write-through to cache

// Memory management initilization
int allocateMemory(int processID, int size); // Allocates memory for a process
void deallocateMemory(int processID); // Deallocated memory from a process
void writeSharedMemory(int address, int value); // Write to shared memory
int readSharedMemory(int address); // Read from shared memory

// Queue management
void enqueue(int pid); // Enqueue a process to the ready queue
int dequeue(); // Dequeue a process from the ready queue
void enqueueFeedback(int queueLevel, int pid); // Enqueue to a feedback queue
int dequeueFeedback(int queueLevel); // Dequeue from a feedback queue

// Context switching
void contextSwitch(int fromProcess, int toProcess); // Switch between processes

// Scheduling algorithm initialization
void roundRobinScheduler(); // Round-Robin scheduling
void priorityScheduler(); // Priority scheduling
void shortestRemainingTimeScheduler(); // Shortest time remaining scheduling
void hrrnScheduler(); // Highest response ratio next scheduling
void fcfsScheduler(); // First come First serve scheduling
void shortestProcessNextScheduler(); // Shortest process next scheduling
void feedbackScheduler(); // Feedback scheduling

// CPU simulation initialization
void fetch(int pid); // Fetch instruction for a process
void decodeAndExecute(int pid); // Decode and execute instructions for a process
void simulateProcesses(); // Simulate process execution
void fetchDecodeExecute(int pid); // Fetch-Decode-Execute cycle

// Initialization functions
void initializeProcesses(); // Initialize process table
void loadInstructions(); // Load instructions into memory
void initializeMemory(); // Initialize memory
void initializeMemoryTable(); // Initialize memory table with blocks

// Scheduling algorithm layout
enum {ALG_FCFS=0, ALG_RR, ALG_PRIORITY, ALG_SRT, ALG_HRRN, ALG_SPN, ALG_FEEDBACK, ALG_COUNT};

// Time metrics to keep track
struct Metrics {
    double avg_waiting_time; // Average waiting time for processes
    double avg_turnaround_time; // Average turnaround time for processes
    double cpu_utilization; // CPU utilization percentage
};

struct Metrics metrics_table[ALG_COUNT]; // Metrics table for scheduling algorithms
int current_alg = -1; // Current scheduling algorithm index

// Initialize mutexes
void initMutex() {
    pthread_mutex_init(&interrupt_mutex, NULL); // Initialize interrupt mutex
}

// Interrupt handler
void handleInterrupt(int interruptType) {
    pthread_mutex_lock(&interrupt_mutex); // Locks the interrupt mutex

    // Checks if type is valid
    if (interruptType >= 0 && interruptType < 4) {
        IVT[interruptType](); // Calls the interrupt based on type
    }

    pthread_mutex_unlock(&interrupt_mutex); // Unlocks interrupt mutex
}

// Timer interrupt
void timerInterrupt() {
    printf("[Interrupt] Timer interrupt handled.\n"); // Print statement showing interrupt happened
}

// IO interrupt
void ioInterrupt() {
    printf("[Interrupt] I/O interrupt has occurred.\n"); // Print statement showing interrupt happened
}

// System call interrupt
void systemCallInterrupt() {
    printf("[Interrupt] System call interrupt handled.\n"); // Print statement showing interrupt happened
}

// Trap interrupt
void trapInterrupt() {
    printf("[Interrupt] Trap interrupt handled.\n"); // Print statement showing interrupt happened
}

// Thread for the timer
void* timerThread(void* arg) {
    while (1) { // Infinite loop for the timer thread
        sleep(5); // Pause execution for 5 seconds
        handleInterrupt(0); // Triggers a timer interrupt
    }
    return NULL;
}
// Thread for trap interrupt
void* trapThread(void* arg) {
    while (1) { // Infinite loop for the trap interrupt thread
        int sleepTime = 3 + rand() % 10; // Generate a random sleep time
        sleep(sleepTime); // Pause execution for the generated time
        handleInterrupt(3); // Triggers a trap interrupt
    }
    return NULL;
}

// Thread for user input
void* inputThread(void* arg) {
    char input[10]; // Buffer to store the input
    while (1) { // Infinite loop for the trap interrupt thread

        // Reads the input from the user
        if (fgets(input, sizeof(input), stdin) != NULL) {
            input[strcspn(input, "\n")] = 0; // Removes newline

            // Checks if input is 'sys'
            if (strcmp(input, "sys") == 0) {
                handleInterrupt(2); // .. if so, call a system interrput
            }
            // Checks if user input is 'io'
            else if (strcmp(input, "io") == 0) {
                handleInterrupt(1); // .. if so, call a IO interrput
            }
        }
    }
    return NULL;
}

// Loads instructions into RAM
void loadInstructions() {
    RAM[0] = 1;  RAM[1] = 5;  RAM[2] = 2;  RAM[3] = 3;  RAM[4] = 0; // Load instructions for process 1
    RAM[10] = 1; RAM[11] = 10; RAM[12] = 2; RAM[13] = 2; RAM[14] = 0; // Load instructions for process 2
    RAM[20] = 2; RAM[21] = 7;  RAM[22] = 1; RAM[23] = 3; RAM[24] = 0; // Load instructions for process 3
}

// Fetch Function
void fetch(int pid) {
    PC = processTable[pid].pc; // Set the program counter to the process's current PC
    IR = RAM[PC]; // Fetches the instruction from RAM
    printf("[Process %d] Fetched instruction %d at PC=%d\n", pid, IR, PC); // Prints fetched statement
}

// Decond and Execute method
void decodeAndExecute(int pid) {
    struct PCB *process = &processTable[pid]; // Get the process's PCB
    int operand;

    // Decodes the instruction based on the value of IR
    switch (IR) {
        case 1: // ADD instruction
            operand = RAM[PC + 1]; // Fetches the operand
            process->acc += operand; // Adds the operand to the accumulator
            printf("[Process %d] Executing ADD %d, ACC=%d\n", pid, operand, process->acc); // Prints execution statement
            PC += 2; // Increments PC by 2
            break;

        case 2: // SUB instruction
            operand = RAM[PC + 1]; // Fetches the operand
            process->acc -= operand; // Subtracts the operand from the accumulator
            printf("[Process %d] Executing SUB %d, ACC=%d\n", pid, operand, process->acc); // Prints execution statement
            PC += 2; // Increments PC by 2
            break;

        case 0: // HALT instruction
            process->state = FINISHED; // Sets process state to finished
            process->finishTime = process->pc; // Records the finish time
            printf("[Process %d] HALT instruction, Process Completed\n", pid); // Prints HALT instruction statement, process done
            break;

        default: // Invalid instruction
            printf("[Process %d] Invalid instruction at PC=%d, Terminating...\n", pid, PC); // Prints invalid instruction statement
            process->state = FINISHED; // Sets process to finished
            process->finishTime = process->pc; // Records the finish time
            break;
    }
    process->pc = PC; // Update the process PC
}

// Fetch-Decode-Execute function
void fetchDecodeExecute(int pid) {
    struct PCB *process = &processTable[pid]; // Get the process's PCB

    // Checks if the process is in a running state
    if (process->state != RUNNING) {
        printf("Process %d isn't in running state, skipping cycle\n", pid); // Print statement showing process isn't running
        return;
    }

    printf("Process %d fetching instruction at PC=%d.\n", pid, process->pc); // Print statement showing process is fetching instrution
    int instruction = RAM[process->pc]; // Fetches the instruction
    printf("Process %d decoding and executing instruction %d.\n", pid, instruction); // Print statement showing process is being executed
    process->acc += instruction; // Executes the operation
    process->pc++; // Increments the program counter

    static int elapsedTime = 0; // Tracks time for quantum
    elapsedTime += 1; // Increments elapsed time

    // Checks if elapsed time exceeds time quantum...
    if (elapsedTime >= TIME_QUANTUM) {
        int nextProcess = dequeue();  // Gets the next process from the ready queue
        contextSwitch(pid, nextProcess); // Performs context switch
        enqueue(pid); // Enqueues the current process
        elapsedTime = 0; // Resets elapsed time
    }

    // Checks for high-priority processes
    for (int i = 0; i < MAX_PROCESSES; i++) {

        // If a higher priority process is found...
        if (processTable[i].state == READY && processTable[i].priority < process->priority) {
            printf("Higher priority process found, Process %d [Priority %d]\n", i, processTable[i].priority); // Print statement stating process is found
            contextSwitch(pid, i); // Performs context switch
            break;
        }
    }

    // Checks if the process is finished executing
    if (process->timeRemaining <= 0) {
        process->state = FINISHED; // Sets the process state to finished
        process->finishTime = process->pc; // Records the finish time
        printf("Process %d finished execution\n", pid); // Print statement showing process is done
    }
}

// Initializes the memory simulation
void initializeMemory() {

    // Initializes all RAM locations, all with -1 (empty)
    for (int i = 0; i < RAM_SIZE; i++) {
        RAM[i] = -1;
    }

    // Initializes all L1 locations, all with -1 (empty)
    for (int i = 0; i < L1_SIZE; i++) {
        L1Cache[i] = -1;
    }

    // Initializes all L2 locations, all with -1 (empty)
    for (int i = 0; i < L2_SIZE; i++) {
        L2Cache[i] = -1;
    }
}

// Initializes the memory table
void initializeMemoryTable() {
    int blockSize = RAM_SIZE / MAX_BLOCKS; // Calculates the size of each memory block

    // Loops through all the memory blocks
    for (int i = 0; i < MAX_BLOCKS; i++) {
        memoryTable[i].processID = -1; // Marks the block as unallocated
        memoryTable[i].startAddress = i * blockSize; // Sets the start address of the block
        memoryTable[i].endAddress = (i + 1) * blockSize - 1; // Sets the end address of the block
        memoryTable[i].isFree = 1; // Marks the block as free
    }
}

// Checks if the given address is in L1
int checkL1(int address) {

    // If so, return the value from L1 if not valid return -1
    return (address < L1_SIZE && L1Cache[address] != -1) ? L1Cache[address] : -1;
}

// Checks if the given address is in L2
int checkL2(int address) {

    // If so, return the value from L2 if not valid return -1
    return (address < L2_SIZE && L2Cache[address] != -1) ? L2Cache[address] : -1;
}

// Looks up the address in cache and returns the value
int cacheLookup(int address) {
    pthread_mutex_lock(&memory_mutex); // Locks memory mutex
    int value = checkL1(address); // Checks if the address is in the L1 cache

    // If found in L1...
    if (value != -1) {
        pthread_mutex_unlock(&memory_mutex); // Unlock the memory mutex
        return value; // Return the value
    }

    value = checkL2(address); // Checks if the address is in the L2 cache
    if (value != -1) {
        pthread_mutex_unlock(&memory_mutex); // Unlock the memory mutex
        return value; // Returns the value
    }

    int ram_val = RAM[address]; // Gets the value from RAM if not found in caches
    pthread_mutex_unlock(&memory_mutex); // Unlocks the memory mutex
    return ram_val; // Returns the value from RAM
}

// Writes a value to a given address
void cacheWriteThrough(int address, int value) {
    pthread_mutex_lock(&memory_mutex); // Locks the memory mutex

    // If the address fits in L1 cache...
    if (address < L1_SIZE) {
        L1Cache[address] = value; // Write the value
    }

    // If the address fits in L2 cache...
    if (address < L2_SIZE) {
        L2Cache[address] = value; // Write the value
    }

    RAM[address] = value; // Writes the value to RAM
    pthread_mutex_unlock(&memory_mutex); // Unlocks the memory mutex
}

// Allocates a block of memory for a process, returning the starting address
int allocateMemory(int processID, int size) {
    pthread_mutex_lock(&memory_mutex); // Locks the memory mutex

    // Loops through all memory blocks to find a free block big enough
    for (int i = 0; i < MAX_BLOCKS; i++) {
        int blockSize = memoryTable[i].endAddress - memoryTable[i].startAddress + 1; // Calculates the block size

        // Checks if the block is free and big enough
        if (memoryTable[i].isFree && blockSize >= size) {
            memoryTable[i].isFree = 0; // Marks the block as allocated
            memoryTable[i].processID = processID; // Assigns the block to the process
            printf("Allocated %d bytes to process %d in block %d\n", size, processID, i); // Print statement stating block is allocated
            pthread_mutex_unlock(&memory_mutex); // Unlocks the memory mutex
            return memoryTable[i].startAddress; // Returns the starting address of the block
        }
    }

    pthread_mutex_unlock(&memory_mutex); // Unlock the memory mutex
    printf("Memory allocation failed for process %d\n", processID); // Print statement showing allocation failed
    return -1;
}

// Decallocated memory from a process
void deallocateMemory(int processID) {
    pthread_mutex_lock(&memory_mutex); // Locks the memory mutex

    // Loops through the memory blocks
    for (int i = 0; i < MAX_BLOCKS; i++) {

        // Checks if the block belongs to a process
        if (memoryTable[i].processID == processID) {
            memoryTable[i].isFree = 1; // Marks the block as free
            memoryTable[i].processID = -1; // Resets the process ID
            printf("Deallocated memory for process %d in block %d\n", processID, i); // Print statement showing deallocation happened
            break;
        }
    }
    pthread_mutex_unlock(&memory_mutex); // Unlocks the memory mutex
}

// Writes to shared memory
void writeSharedMemory(int address, int value) {

    // Checks if the address is within range
    if (address >= SHARED_MEM_START && address <= SHARED_MEM_END) {
        pthread_mutex_lock(&memory_mutex); // Locks the memory thread
        RAM[address] = value; // Writes values to the shared memory address
        printf("Wrote %d to shared memory at: %d\n", value, address); // Print statement showing values were writted
        pthread_mutex_unlock(&memory_mutex); // Unlocks the memory thread
    } else {
        printf("Invalid shared memory address: %d\n", address); // Print statement for invalid memory address
    }
}

// Reads from shared memory
int readSharedMemory(int address) {
    int value = -1; // Initializes to -1

    // Checks if the address is within range
    if (address >= SHARED_MEM_START && address <= SHARED_MEM_END) {
        pthread_mutex_lock(&memory_mutex); // Locks the memory thread
        value = RAM[address]; // Reads from the shared memory address
        printf("Read %d from shared memory at: %d\n", value, address); // Print statement showing values were read
        pthread_mutex_unlock(&memory_mutex); // Unlocks the memory thread
    } else {
        printf("Invalid shared memory address: %d\n", address); // Print statement for invalid memory address
    }
    return value; // Returns the read value
}

int readyQueue[MAX_PROCESSES]; // Initializes the readyQueue
int queueFront = 0, queueRear = 0; // Initializes the front and rear queues

// Enqueues a process ID to the ready queue
void enqueue(int pid) {
    readyQueue[queueRear] = pid; // Adds the process ID to the ready queue
    queueRear = (queueRear + 1) % MAX_PROCESSES; // Updates the rear queue index
}

// Dequeues a process ID from the ready queue
int dequeue() {
    int pid = readyQueue[queueFront]; // Gets the process ID from the front of the queue
    queueFront = (queueFront + 1) % MAX_PROCESSES; // Updates the front of the queue
    return pid; // Returns the dequeued process ID
}

// Enqueues a process ID into the feedback queue
void enqueueFeedback(int queueLevel, int pid) {
    feedbackQueues[queueLevel][rear[queueLevel]] = pid; // Adds the process ID to the rear of the queue
    rear[queueLevel] = (rear[queueLevel] + 1) % MAX_PROCESSES; // Updates the rear of the queue
    feedbackCount[queueLevel]++; // Increments the count process in the queue
}

// Dequeues a process ID from the queue
int dequeueFeedback(int queueLevel) {
    int pid = feedbackQueues[queueLevel][front[queueLevel]]; // Gets the process ID from the front of the queue
    front[queueLevel] = (front[queueLevel] + 1) % MAX_PROCESSES; // Updates the front of the queue
    feedbackCount[queueLevel]--; // Decrements the process count in the queue
    return pid; // Returns the dequed process ID
}

// Performs a context switch between two processes
void contextSwitch(int fromProcess, int toProcess) {
    struct PCB *currentProcess = &processTable[fromProcess]; // Gets the PCB of the current process
    struct PCB *nextProcess = &processTable[toProcess]; // Gets the PCB of the next process

    currentProcess->pc += 1; // Saves the PC of the current process
    currentProcess->state = READY; // Sets the current process state to READY

    nextProcess->state = RUNNING; // Sets the next state to running

    printf("Context switch from process %d to process %d\n", fromProcess, toProcess); // Print statement showing context switch happening
    printf("Restoring Process %d: PC=%d, ACC=%d\n", toProcess, nextProcess->pc, nextProcess->acc); // Print statement showing the state is restored
}

// Round Robin Scheduler
void roundRobinScheduler() {

    // Loops while processes are available
    while (totalProcesses > 0) {
        int pid = dequeue(); // Dequeues the next process ID from the ready queue
        struct PCB *process = &processTable[pid]; // Gets the PCB of the dequeued process

        // Checks if the process is ready/running
        if (process->state == READY || process->state == RUNNING) {
            process->state = RUNNING; // Sets the process state to running
            int timeToRun = (process->timeRemaining < TIME_QUANTUM) ? process->timeRemaining : TIME_QUANTUM; // Determines how long the process should run
            printf("Running Process %d for %d time units.\n", pid, timeToRun); // Print statement showing process is running
            sleep(timeToRun); // Simulates process execution time for the given time
            process->timeRemaining -= timeToRun; // Reduces the time remaining on the process

            // Checks if the process is finished
            if (process->timeRemaining <= 0) {
                process->state = FINISHED; // Sets the process state to finished
                process->finishTime = process->pc; // Records the finish time
                printf("Process %d finished execution\n", pid); // Print statement showing process is done
                totalProcesses--; // Decrements the total process count
            } else {
                enqueue(pid); // Enqueues process if time is still remaining
            }
        }
    }
}

// Priority Scheduler
void priorityScheduler() {

    // Loops while processes are available
    while (totalProcesses > 0) {
        int highestPriority = MAX_PRIORITY + 1; // Initializes with a value greater than max priority
        int selectedProcess = -1; // Initializes the selected process

        // Finds the process with the highest priority
        for (int i = 0; i < MAX_PROCESSES; i++) {

            // If found...
            if (processTable[i].state == READY && processTable[i].priority < highestPriority) {
                highestPriority = processTable[i].priority; // Updates the highest priority
                selectedProcess = i; // Updates the selected process ID
            }
        }

        // If a process is found...
        if (selectedProcess != -1) {
            processTable[selectedProcess].state = RUNNING; // Sets the process state to running
            printf("Running Process %d (Priority %d)\n", selectedProcess, highestPriority); // Print statement showing process is running
            sleep(processTable[selectedProcess].burstTime); // Simulates execution for burst time
            processTable[selectedProcess].state = FINISHED; // Sets the process state to finished
            processTable[selectedProcess].finishTime = processTable[selectedProcess].pc + processTable[selectedProcess].burstTime; // Records the finish time
            printf("Process %d finished execution\n", selectedProcess); // Print statement showing process is done
            totalProcesses--; // Decremenets the total process count
        }
    }
}

// Shortest Remaining Time Scheduler
void shortestRemainingTimeScheduler() {

    // Loops while processes are available
    while (totalProcesses > 0) {
        int shortestTime = 100000; // Initializes with large time value
        int selectedProcess = -1; // Initializes the selected process

        // Find the process with the shortest remaining time
        for (int i = 0; i < MAX_PROCESSES; i++) {

            // If found...
            if (processTable[i].state == READY && processTable[i].timeRemaining < shortestTime) {
                shortestTime = processTable[i].timeRemaining; // Updates the shortest time
                selectedProcess = i; // Updates the selected process
            }
        }

        // If a process is found
        if (selectedProcess != -1) {
            struct PCB *process = &processTable[selectedProcess]; // Selects the process from the PCB
            process->state = RUNNING; // Sets the process state to running
            printf("Running Process %d (Shortest Remaining Time %d)\n", selectedProcess, process->timeRemaining); // Print statement showing process is running
            sleep(process->timeRemaining); // Simulates process execution time
            process->timeRemaining = 0; // Sets the remaining time to 0
            process->state = FINISHED; // Sets the process state to finished
            process->finishTime = process->pc + process->burstTime; // Records the finish time
            printf("Process %d finished execution.\n", selectedProcess); // Print statement showing process is completed
            totalProcesses--; // Decrements the total process count
        }
    }
}

// Highest Response Ratio Next Scheduler
void hrrnScheduler() {

    // Loops while processes are available
    while (totalProcesses > 0) {
        int selectedProcess = -1; // Initializes the selected process
        float highestResponseRatio = -1.0; // Initializes the highestResponseRatio

        // Calculates the response ratio and selects the highest
        for (int i = 0; i < MAX_PROCESSES; i++) {
            struct PCB *p = &processTable[i]; // Selects the process from the PCB

            // If the process state is running
            if (p->state == READY) {
                int waitingTime = (p->finishTime>0)?0:p->arrivalTime; // Waiting time simulation
                float responseRatio = (float)(waitingTime + p->burstTime) / p->burstTime; // Calculates response ratio

                //If the response ratio is greater...
                if (responseRatio > highestResponseRatio) {
                    highestResponseRatio = responseRatio; // Updates the highest response ratio
                    selectedProcess = i; // Updates the selected process ID
                }
            }
        }

        // If a process is found
        if (selectedProcess != -1) {
            struct PCB *proc = &processTable[selectedProcess]; // Selects process from the PCB
            proc->state = RUNNING; // Sets the process state to running
            printf("Running Process %d (HRRN %.2f)\n", selectedProcess, highestResponseRatio); // Print statement showing process is running
            sleep(proc->burstTime); // Sleeps to simulate busrt time
            proc->state = FINISHED; // Sets process state to finished
            proc->finishTime = proc->pc + proc->burstTime; // Records the finish time
            printf("Process %d finished execution\n", selectedProcess); // Print statement showing process is done
            totalProcesses--; // Decrements the total process count
        }
    }
}

// First come first serve scheduler
void fcfsScheduler() {

    // Loops while processes are available
    while (totalProcesses > 0) {
        int pid = dequeue(); // Dequeues the next process ID
        struct PCB *process = &processTable[pid]; // Selects process from the PCB

        // If process state is ready
        if (process->state == READY) {
            process->state = RUNNING; // Sets the process state to running
            printf("Running Process %d (FCFS)\n", pid); // Print statement showing process is running
            sleep(process->burstTime); // Sleeps to simulate burst time
            process->state = FINISHED; // Sets the process state to finished
            process->finishTime = process->pc + process->burstTime; // Records the finish time
            printf("Process %d finished execution\n", pid); // Print statement showing process is done
            totalProcesses--; // Decrements the total process count
        }
    }
}

// Shortest process next scheduler
void shortestProcessNextScheduler() {

    // Loops while processes are available
    while (totalProcesses > 0) {
        int shortestBurst = 100000; // Initializes the shortest burst time
        int selectedProcess = -1; // Initializes the selected process

        // Finds the process with the shortest burst time
        for (int i = 0; i < MAX_PROCESSES; i++) {
            if (processTable[i].state == READY && processTable[i].burstTime < shortestBurst) {
                shortestBurst = processTable[i].burstTime; // Updates the shortest burst time
                selectedProcess = i; // Updates the selected process
            }
        }

        // If a process is found
        if (selectedProcess != -1) {
            struct PCB *p = &processTable[selectedProcess]; // Selects process from the PCB
            p->state = RUNNING; // Sets the process state to running
            printf("Running Process %d (SPN, Burst Time: %d)\n", selectedProcess, p->burstTime); // Print statement showing the process is running
            sleep(p->burstTime); // Sleeps to simulate burst time
            p->state = FINISHED; // Sets the process state to finished
            p->finishTime = p->pc + p->burstTime; // Records the finish time
            printf("Process %d finished execution\n", selectedProcess); //Print statement showing process is done
            totalProcesses--; // Decrements the total process count
        }
    }
}

// Feedback scheduler
void feedbackScheduler() {

    // Loops while processes are available
    while (totalProcesses > 0) {

        // Loops through each feedback queue level
        for (int level = 0; level < QUEUE_LEVELS; level++) {

            // Processes all the tasks in the current queue level
            while (feedbackCount[level] > 0 && totalProcesses > 0) {
                int pid = dequeueFeedback(level); // Dequeus the next process ID in the queue
                struct PCB *process = &processTable[pid]; // Selects process from the queue

                // Skips process if already finished
                if (process->state == FINISHED) {
                    continue;
                }

                process->state = RUNNING; // Sets the current process to running

                // Determines the time quantum for the current queue level
                int timeQuantum = (level == 0) ? TIME_QUANTUM_LEVEL1 :
                                  (level == 1) ? TIME_QUANTUM_LEVEL2 : TIME_QUANTUM_LEVEL3;
                int timeToRun = (process->timeRemaining < timeQuantum) ? process->timeRemaining : timeQuantum;

                printf("Running Process %d in Queue Level %d for %d time units\n", pid, level, timeToRun); // Print statement showing process is running
                sleep(timeToRun); // Sleeps to simulate execution time
                process->timeRemaining -= timeToRun; // Reduces the time remaining on the process

                // Checks if the current process is finished
                if (process->timeRemaining <= 0) {
                    process->state = FINISHED; // Sets process state to finished
                    process->finishTime = process->pc + process->burstTime; // Records the finish time
                    printf("Process %d finished execution\n", pid); // Print statement showing process is finished
                    totalProcesses--; // Decrements the total process count

                } else {
                    // Enqueues the next process to the lower priority queue
                    int nextLevel = (level + 1 < QUEUE_LEVELS) ? level + 1 : level;
                    enqueueFeedback(nextLevel, pid);
                }

                // Loop breaks if all the processes are done
                if (totalProcesses == 0) {
                    break;
                }
            }

            // Loop breaks if all the processes are done
            if (totalProcesses == 0) {
                break;
            }
        }
    }
    printf("All processes completed. Exiting Feedback Scheduler\n"); // Print statement showing all the processes are done for feedback scheduler
}

// Prints and stores the results from the schedulers
void printResultsAndStore(int alg_index, const char* algName) {
    double total_wait=0.0, total_turn=0.0; // Vars for waiting time and turn around times
    int count=0; // Count of processes finished

    printf("PROCESS BURST TIME WAITING TIME TURNAROUND TIME\n"); // Print statement to appear as a header
    printf("======================================================\n"); // Print divider

    // Loops through all the processes to calculated the metrics
    for (int i=0; i<MAX_PROCESSES; i++){

        // Only goes through processes that finished
        if (processTable[i].state == FINISHED) {
            int waiting = (processTable[i].finishTime - processTable[i].arrivalTime - processTable[i].burstTime); // Calculates waiting time
            int turnaround = (processTable[i].finishTime - processTable[i].arrivalTime); // Calculates turnaround time
            total_wait += waiting; // Accumulates waiting time
            total_turn += turnaround; // Accumulates turnaround time
            count++; // Increments the count
            printf("P%d %d %d %d\n", i, processTable[i].burstTime, waiting, turnaround); // Prints the results
        }
    }

    double avg_wait = (count==0)?0:total_wait/count; // Calculates average wait time
    double avg_turn = (count==0)?0:total_turn/count; // Calculates average turn around time
    double cpu_usage = 100.0; // Simulates CPU usage at 100%

    // Prints the calculated averages
    printf("Average waiting time -> %.6f\n", avg_wait);
    printf("Average turnaround time -> %.6f\n", avg_turn);
    printf("Overall CPU usage -> %.1f%%\n", cpu_usage);

    // Prints the results with the algorithm
    printf("Algorithm: %s\n", algName);
    printf("Average Waiting Time: %.6f\n", avg_wait);
    printf("Average Turnaround Time: %.6f\n", avg_turn);
    printf("CPU Usage: %.0f%%\n", cpu_usage);

    // Stores all the results
    metrics_table[alg_index].avg_waiting_time = avg_wait;
    metrics_table[alg_index].avg_turnaround_time = avg_turn;
    metrics_table[alg_index].cpu_utilization = cpu_usage;
}

// Runs the schedulers with the given algorithms storing the results
void runScheduler(int alg_index, void (*scheduler)()) {
    scheduler(); // Runs the scheduler
    const char* name; // Determines which scheduler to run
    switch(alg_index) {
        case ALG_FCFS: name = "FCFS"; break; // First come first served algorithm
        case ALG_RR: name = "RR (Quantum = 4)"; break; // Round Robin algorithm
        case ALG_PRIORITY: name = "Priority"; break; // Priority algorithm
        case ALG_SRT: name = "SJF"; break; // Shortest job first algorithm
        case ALG_HRRN: name = "HRRN"; break; // Highest response ratio next algorithm
        case ALG_SPN: name = "SPN"; break; // Shorted process next algorithm
        case ALG_FEEDBACK: name = "Feedback"; break; // Feedback algorithm
        default: name = "INVALID"; // Invalid algorithm if given
    }
    printResultsAndStore(alg_index, name); // Prints and stores the results for the algorithm
}

// Simulates the process execution until finished
void simulateProcesses() {
    int allFinished = 0; // Initiaizes all finished counter

    // Loops until all the processes are finished
    while (!allFinished) {
        allFinished = 1; // Assumes processes are finished

        // Iterates through all the processes
        for (int i = 0; i < MAX_PROCESSES; i++) {
            struct PCB *p = &processTable[i]; // Selects process from the PCB

            // Checks if the process is ready/running
            if (p->state == READY || p->state == RUNNING) {
                allFinished = 0; // Resets the flag counter
                p->state = RUNNING; // Sets the process state to finished
                fetch(i); // Fetches the next instruction for the process
                decodeAndExecute(i); // Decodes and executes the fetched process

                // If process isn't finished
                if (p->state != FINISHED) {
                    p->state = READY; // Set the state to ready
                }
            }
        }
    }
}


// Initializes the process table and enqueues them
void initializeProcesses() {
    queueFront = 0; queueRear = 0; // Resets the queues

    // Loops through all the processes to initialize them
    for (int i = 0; i < MAX_PROCESSES; i++) {
        processTable[i].pid = i; // Sets the process ID
        processTable[i].pc = 0; // Initializes the program counter
        processTable[i].acc = 0; // Initializes the accumulator
        processTable[i].state = READY; // Initializes state to ready
        processTable[i].priority = rand() % MAX_PRIORITY; // Assigns random prirority
        processTable[i].timeRemaining = 5 + rand() % 10; // Assigns random execution time
        processTable[i].arrivalTime = i; // Sets the arrival time
        processTable[i].burstTime = processTable[i].timeRemaining; // Sets the burst time to the initial time remaining
        processTable[i].finishTime = 0; // Initializes finished time to 0
        enqueue(i); // Enqueues the process ID into the ready queue
    }
    totalProcesses = MAX_PROCESSES; // Sets the total number of processes
}


int main() {
    srand(time(NULL)); // Initializes random generator

    // Initializes the memory system
    printf("Initializing Memory System...\n");
    initializeMemory();
    initializeMemoryTable();

    // Initializes the processes
    printf("Initializing Processes...\n");
    initializeProcesses();

    // Loads instructions into RAM
    printf("Loading Instructions into RAM...\n");
    loadInstructions();

    // Initializes the mutex and semaphores
    pthread_mutex_init(&memory_mutex, NULL);
    sem_init(&buffer_full, 0, 0);
    sem_init(&buffer_empty, 0, 5);

    // Initializes interrupt handling
    initMutex();
    IVT[0] = timerInterrupt;
    IVT[1] = ioInterrupt;
    IVT[2] = systemCallInterrupt;
    IVT[3] = trapInterrupt;

    // Creates thread for timer
    pthread_t tThread;
    pthread_create(&tThread, NULL, timerThread, NULL);

    // Creates thread for input
    pthread_t iThread;
    pthread_create(&iThread, NULL, inputThread, NULL);

    // Initializes thread for trap
    pthread_t trThread;
    pthread_create(&trThread, NULL, trapThread, NULL);

    // Runs all scheduling algorithms:
    // First come first serve
    current_alg = ALG_FCFS;
    initializeProcesses();
    printf("Running FCFS Scheduler:\n");
    runScheduler(ALG_FCFS, fcfsScheduler);

    // Round Robin
    current_alg = ALG_RR;
    initializeProcesses();
    printf("Running Round-Robin Scheduler:\n");
    runScheduler(ALG_RR, roundRobinScheduler);

    // Priority
    current_alg = ALG_PRIORITY;
    initializeProcesses();
    printf("Running Priority-Based Scheduler:\n");
    runScheduler(ALG_PRIORITY, priorityScheduler);

    // Shortest Job First
    current_alg = ALG_SRT;
    initializeProcesses();
    printf("Running SJF Scheduler:\n");
    runScheduler(ALG_SRT, shortestRemainingTimeScheduler);

    // Highest response ratio next
    current_alg = ALG_HRRN;
    initializeProcesses();
    printf("Running HRRN Scheduler:\n");
    runScheduler(ALG_HRRN, hrrnScheduler);

    // Shorted process next
    current_alg = ALG_SPN;
    initializeProcesses();
    printf("Running SPN Scheduler:\n");
    runScheduler(ALG_SPN, shortestProcessNextScheduler);

    // Feedback
    current_alg = ALG_FEEDBACK;
    initializeProcesses();
    printf("Running Feedback Scheduler:\n");

    // Resets feedback queues and counts
    for (int level = 0; level < QUEUE_LEVELS; level++) {
        front[level] = 0;
        rear[level] = 0;
        feedbackCount[level] = 0;
    }


    for (int i = 0; i < MAX_PROCESSES; i++) {
        enqueueFeedback(0, i); // Enqueues processes to the highest priority feedback queue
    }

    runScheduler(ALG_FEEDBACK, feedbackScheduler);

    // Prints algorithm comparison chart
    printf("\nComparison Chart\n");
    printf("Algorithm       Avg Waiting Time   Avg Turnaround Time   CPU Usage (%%)\n");
    printf("-----------------------------------------------------------------------\n");

    // First come first serve
    printf("FCFS            %.1f              %.1f                %.0f\n",
           metrics_table[ALG_FCFS].avg_waiting_time, metrics_table[ALG_FCFS].avg_turnaround_time, metrics_table[ALG_FCFS].cpu_utilization);

    // Round Robin
    printf("RR (Q=4)        %.1f              %.1f                %.0f\n",
           metrics_table[ALG_RR].avg_waiting_time, metrics_table[ALG_RR].avg_turnaround_time, metrics_table[ALG_RR].cpu_utilization);

    // Shortest Job First
    printf("SJF             %.1f              %.1f                %.0f\n",
           metrics_table[ALG_SRT].avg_waiting_time, metrics_table[ALG_SRT].avg_turnaround_time, metrics_table[ALG_SRT].cpu_utilization);

    // Highest response ratio next
    printf("HRRN            %.1f              %.1f                %.0f\n",
           metrics_table[ALG_HRRN].avg_waiting_time, metrics_table[ALG_HRRN].avg_turnaround_time, metrics_table[ALG_HRRN].cpu_utilization);

    // Priority
    printf("Priority        %.1f              %.1f                %.0f\n",
           metrics_table[ALG_PRIORITY].avg_waiting_time, metrics_table[ALG_PRIORITY].avg_turnaround_time, metrics_table[ALG_PRIORITY].cpu_utilization);

    // Shortest process next
    printf("SPN             %.1f              %.1f                %.0f\n",
           metrics_table[ALG_SPN].avg_waiting_time, metrics_table[ALG_SPN].avg_turnaround_time, metrics_table[ALG_SPN].cpu_utilization);

    // Feedback
    printf("Feedback        %.1f              %.1f                %.0f\n",
           metrics_table[ALG_FEEDBACK].avg_waiting_time, metrics_table[ALG_FEEDBACK].avg_turnaround_time, metrics_table[ALG_FEEDBACK].cpu_utilization);

    // Closes threads
    pthread_cancel(tThread);
    pthread_cancel(iThread);
    pthread_cancel(trThread);

    pthread_mutex_destroy(&memory_mutex);
    sem_destroy(&buffer_full);
    sem_destroy(&buffer_empty);
    pthread_mutex_destroy(&interrupt_mutex);

    printf("All tasks and analysis complete. Exiting...\n"); // Final print statement showing everything is finished
    return 0;
}
