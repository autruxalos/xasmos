/******************************************************************************
 * XASMOS - XQUEUE Protocol (Lockless Zero-Copy Hardware Asynchronous Ring)
 * Path: source|arch|amd64|syslib|core|xqueue.c
 ******************************************************************************/

typedef unsigned long long uint64_t;
typedef unsigned int uint32_t;

#define XQUEUE_RING_SIZE 256 // Must be a power of 2 for fast math optimization

typedef struct {
    uint32_t operation_type;  // e.g., 1 = RAW_DISK_READ, 2 = NETWORK_PACKET_TX
    uint64_t source_address;  // Physical source hardware address
    uint64_t target_address;  // Physical target hardware address
    uint64_t payload_length;  // Exact data block size to move via DMA
    uint64_t security_token;  // Rigorously validated by the Exokernel or coprocessor
} XQueueCommand;

typedef struct {
    XQueueCommand commands[XQUEUE_RING_SIZE];
    volatile uint32_t head;   // Modified by message consumer (Hardware/Kernel/SPE)
    volatile uint32_t tail;   // Modified by producer (Application / LibOS)
} XQueueChannel;

/**
 * Enqueues a hardware transfer command without triggering context switches.
 */
int xqueue_submit_command(XQueueChannel *channel, XQueueCommand cmd) {
    uint32_t current_tail = channel->tail;
    uint32_t next_tail = (current_tail + 1) & (XQUEUE_RING_SIZE - 1);

    // Check if the asynchronous ring buffer is completely full
    if (next_tail == channel->head) {
        return -1; // Queue overflow (Hardware/Consumer is saturated)
    }

    // Write command directly into the shared memory buffer
    channel->commands[current_tail] = cmd;

    // Native memory fence to ensure CPU writes data before updating index
    __asm__ volatile("mfence" ::: "memory");

    // Update tail making it visible to the hardware controller or SPE
    channel->tail = next_tail;

    return 0; // Command successfully submitted
}

/**
 * Non-blocking response polling (Used by dedicated processing threads)
 */
int xqueue_poll_completion(XQueueChannel *channel, XQueueCommand *out_cmd) {
    if (channel->head == channel->tail) {
        return 0; // No new pending commands to process
    }

    uint32_t current_head = channel->head;
    *out_cmd = channel->commands[current_head];

    __asm__ volatile("mfence" ::: "memory");
    
    channel->head = (current_head + 1) & (XQUEUE_RING_SIZE - 1);
    return 1; // Command successfully captured
}
