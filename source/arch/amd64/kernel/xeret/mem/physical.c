/******************************************************************************
 * Xeret - Physical Page Frame Allocator & Security Ledger
 * Path: source|arch|amd64|kernel|xeret|mem|physical.c
 ******************************************************************************/

typedef unsigned long long uint64_t;
typedef unsigned int uint32_t;

#define PAGE_SIZE          4096
#define BITMAP_EMPTY       0x0000000000000000ULL
#define MAX_MEM_TOKENS     1024

typedef struct {
    uint64_t start_frame;
    uint64_t total_pages;
    uint64_t secure_token; // Cryptographic token assigned to a LibOS
} HardwareAllocationToken;

// Static kernel memory structures (No external runtime or heap dependency)
static uint64_t memory_bitmap[32768]; // Supports tracking up to 8GB of physical RAM
static HardwareAllocationToken active_allocations[MAX_MEM_TOKENS];
static uint32_t allocation_count = 0;

/**
 * Initializes the physical bitmap, marking available RAM frames as free.
 */
void physical_mem_init(uint64_t total_ram_bytes) {
    uint64_t total_pages = total_ram_bytes / PAGE_SIZE;
    uint64_t bitmap_quadwords = total_pages / 64;

    if (bitmap_quadwords > 32768) {
        bitmap_quadwords = 32768;
    }

    for (uint64_t i = 0; i < bitmap_quadwords; i++) {
        memory_bitmap[i] = BITMAP_EMPTY; // 0 indicates a free page frame
    }
}

/**
 * Exclusively assigns a contiguous block of physical pages to a client LibOS.
 */
uint64_t allocate_physical_block(uint64_t pages_requested, uint64_t client_token) {
    if (pages_requested == 0 || allocation_count >= MAX_MEM_TOKENS) {
        return 0;
    }

    uint64_t continuous_found = 0;
    uint64_t start_index = 0;
    uint64_t max_bits = 32768 * 64;

    // Linear flat scan across the bitmap
    for (uint64_t i = 0; i < max_bits; i++) {
        uint64_t word = i / 64;
        uint64_t bit = i % 64;

        if (!(memory_bitmap[word] & (1ULL << bit))) {
            if (continuous_found == 0) {
                start_index = i;
            }
            continuous_found++;
            
            if (continuous_found == pages_requested) {
                // Lock the physical pages in the bitmap
                for (uint64_t j = start_index; j < start_index + pages_requested; j++) {
                    memory_bitmap[j / 64] |= (1ULL << (j % 64));
                }
                
                // Register hardware ownership under the client token
                active_allocations[allocation_count].start_frame = start_index;
                active_allocations[allocation_count].total_pages = pages_requested;
                active_allocations[allocation_count].secure_token = client_token;
                allocation_count++;

                return start_index * PAGE_SIZE; // Returns the raw physical memory address
            }
        } else {
            continuous_found = 0; // Contiguity broken
        }
    }
    
    return 0; // Out of memory or severely fragmented
}
