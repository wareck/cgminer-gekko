#include "math.h"
#include "miner.h"
#include "usbutils.h"

#if defined(WIN32) || defined(__APPLE__)
#define thread_yield() sched_yield()
#else
#define thread_yield() pthread_yield(NULL)
#endif

#define JOB_MAX      0x7F
#define BUFFER_MAX   0xFF
#define SAMPLE_SIZE  0x78
#define MS_SECOND_1  1000
#define MS_SECOND_5  1000 * 5
#define MS_SECOND_15 1000 * 15
#define MS_SECOND_30 1000 * 30
#define MS_MINUTE_1  1000 * 60
#define MS_MINUTE_2  1000 * 60 * 2
#define MS_MINUTE_5  1000 * 60 * 5
#define MS_MINUTE_10 1000 * 60 * 10
#define MS_MINUTE_30 1000 * 60 * 30
#define MS_HOUR_1    1000 * 60 * 60

enum miner_state {
	MINER_INIT = 1,
	MINER_CHIP_COUNT,
	MINER_CHIP_COUNT_XX,
	MINER_CHIP_COUNT_OK,
	MINER_OPEN_CORE,
	MINER_OPEN_CORE_OK,
	MINER_MINING,
	MINER_MINING_DUPS,
	MINER_SHUTDOWN,
	MINER_SHUTDOWN_OK,
	MINER_RESET
};

enum miner_asic {
	BM1384 = 1,
	BM1387
};

enum plateau_type {
	PT_NONONCE = 1,
	PT_FREQSET,
	PT_FREQNR,
	PT_DUPNONCE
};

enum micro_command {
	M1_GET_FAN    = (0x00 << 3),
	M1_GET_RPM    = (0x01 << 3),
	M1_GET_VIN    = (0x02 << 3),
	M1_GET_IIN    = (0x03 << 3),
	M1_GET_TEMP   = (0x04 << 3),
	M1_GET_VNODE0 = (0x05 << 3),

	M1_CLR_BEN    = (0x08 << 3),
	M1_SET_BEN    = (0x09 << 3),
	M1_CLR_LED    = (0x0A << 3),
	M1_SET_LED    = (0x0B << 3),
	M1_CLR_RST    = (0x0C << 3),
	M1_SET_RST    = (0x0D << 3),

	M2_SET_FAN    = (0x18 << 3),
	M2_SET_VCORE  = (0x1C << 3)
};

enum asic_state {
	ASIC_HEALTHY = 0,
	ASIC_HALFDEAD,
	ASIC_ALMOST_DEAD,
	ASIC_DEAD
};

struct ASIC_INFO {
	struct timeval last_nonce;              // Last time nonce was found
	float frequency;                        // Current frequency
	float frequency_set;                    // set_frequency
	bool frequency_updated;                 // Initiate check for new frequency
	uint32_t frequency_attempt;             // attempts of set_frequency
	uint32_t dups;                          // Duplicate nonce counter
	enum asic_state state;
	enum asic_state last_state;
	struct timeval state_change_time;       // Device startup time
	struct timeval last_frequency_adjust;   // Last time of frequency adjust
	struct timeval last_frequency_ping;     // Last time of frequency ping
	struct timeval last_frequency_reply;    // Last time of frequency reply
	uint32_t prev_nonce;         // Last nonce found
	float fullscan_ms;           // Estimated time(ms) for full nonce range
	uint32_t fullscan_us;        // Estimated time(us) for full nonce range
	uint64_t hashrate;           // Estimated hashrate = cores x chips x frequency
};

struct COMPAC_INFO {

	enum sub_ident ident;            // Miner identity
	enum miner_state mining_state;   // Miner state
	enum miner_asic asic_type;       // ASIC Type
	struct thr_info *thr;            // Running Thread
	struct thr_info rthr;            // Listening Thread
	struct thr_info wthr;            // Miner Work Thread

	pthread_mutex_t lock;        // Mutex
	pthread_mutex_t wlock;       // Mutex Serialize Writes
	pthread_mutex_t rlock;       // Mutex Serialize Reads

	float frequency;             // Chip Average Frequency
	float frequency_asic;        // Highest of current asics.
	float frequency_default;     // ASIC Frequency on RESET
	float frequency_requested;   // Requested Frequency
	float frequency_start;       // Starting Frequency
	float frequency_fail_high;   // Highest Frequency of Chip Failure
	float frequency_fail_low;    // Lowest Frequency of Chip Failure
	float frequency_computed;    // Highest hashrate seen as a frequency value
	float eff_gs;                // hash : expected hash
	float eff_tm;                // hash : expected hash
	float eff_li;                // hash : expected hash
	float eff_1m;                // hash : expected hash
	float eff_5m;                // hash : expected hash
	float eff_15;                // hash : expected hash
	float eff_wu;                // wu : expected wu
	float tune_up;               // Increase frequency when eff_gs is above value
	float tune_down;             // Decrease frequency when eff_gs is below value

	float micro_temp;            // Micro Reported Temp
	float wait_factor;           // Used to compute max_task_wait

	float fullscan_ms;           // Estimated time(ms) for full nonce range
	float scanhash_ms;           // Sleep time inside scanhash loop
	float task_ms;               // Avg time(ms) between task sent to device
	uint32_t fullscan_us;        // Estimated time(us) for full nonce range
	uint64_t hashrate;           // Estimated hashrate = cores x chips x frequency
	uint64_t busy_work;

	uint64_t task_hcn;           // Hash Count Number - max nonce iter.
	uint32_t prev_nonce;         // Last nonce found

	int failing;                 // Flag failing sticks
	int fail_count;              // Track failures.
	int frequency_fo;            // Frequency check token
	int frequency_of;            // Frequency check token
	int accepted;                // Nonces accepted
	int dups;                    // Duplicates found
	int tracker;                 // Track code execution path
	int interface;               // USB interface
	int init_count;              // USB interface initialization counter
	int low_eff_resets;          // Count of low_eff resets
	int midstates;               // Number of midstates
	int nonceless;               // Tasks sent.  Resets when nonce is found.
	int nonces;                  // Nonces found
	int plateau_reset;           // Count plateau based resets
	int zero_check;              // Received nonces from zero work
	int vcore;                   // Core voltage
	int micro_found;             // Found a micro to communicate with

	bool vmask;                  // Current pool's vmask
	bool boosted;                // Good nonce found for midstate2/3/4
	bool report;
	bool frequency_syncd;        // All asics share same frequency

	double wu;
	double wu_max;               // Max WU since last frequency change
	double rolling;

	uint32_t bauddiv;            // Baudrate divider
	uint32_t chips;              // Stores number of chips found
	uint32_t cores;              // Stores number of core per chp
	uint32_t difficulty;         // For computing hashrate
	uint32_t expected_chips;     // Number of chips for device
	uint64_t hashes;             // Hashes completed
	uint64_t xhashes;            // Hashes completed / 0xffffffffull
	uint32_t job_id;             // JobId incrementer
	uint32_t log_wide;           // Extra output in widescreen mode
	uint32_t low_hash;           // Tracks of low hashrate
	uint32_t max_job_id;         // JobId cap
	uint64_t max_task_wait;      // Micro seconds to wait before next task is sent
	uint32_t ramping;            // Ramping incrementer
	uint32_t rx_len;             // rx length
	uint32_t task_len;           // task length
	uint32_t ticket_mask;        // Used to reduce flashes per second
	uint32_t update_work;        // Notification of work update

	struct timeval start_time;              // Device startup time
	struct timeval monitor_time;            // Health check reference point
	struct timeval last_computed_increase;  // Last frequency computed change
	struct timeval last_scanhash;           // Last time inside scanhash loop
	struct timeval last_dup_time;           // Last time nonce dup detected was attempted
	struct timeval last_reset;              // Last time reset was triggered
	struct timeval last_task;               // Last time work was sent
	struct timeval last_nonce;              // Last time nonce was found
	struct timeval last_hwerror;            // Last time hw error was detected
	struct timeval last_fast_forward;       // Last time of ramp jump to peak
	struct timeval last_frequency_adjust;   // Last time of frequency adjust
	struct timeval last_frequency_ping;     // Last time of frequency poll
	struct timeval last_frequency_report;   // Last change of frequency report
	struct timeval last_frequency_invalid;  // Last change of frequency report anomaly
	struct timeval last_chain_inactive;     // Last sent chain inactive
	struct timeval last_low_eff_reset;      // Last time responded to low_eff condition
	struct timeval last_micro_ping;         // Last time of micro controller poll
	struct timeval last_write_error;        // Last usb write error message
	struct timeval last_wu_increase;        // Last wu_max change
	struct timeval last_pool_lost;          // Last time we lost pool

	struct ASIC_INFO asics[64];
	bool active_work[JOB_MAX];              // Tag good and stale work
	struct work *work[JOB_MAX];             // Work ring buffer

	unsigned char task[BUFFER_MAX];         // Task transmit buffer
	unsigned char cmd[BUFFER_MAX];          // Command transmit buffer
	unsigned char rx[BUFFER_MAX];           // Receive buffer
	unsigned char tx[BUFFER_MAX];           // Transmit buffer
	unsigned char end[1024];                // buffer overrun test
};

void stuff_lsb(unsigned char *dst, uint32_t x);
void stuff_msb(unsigned char *dst, uint32_t x);
void stuff_reverse(unsigned char *dst, unsigned char *src, uint32_t len);
uint64_t bound(uint64_t value, uint64_t lower_bound, uint64_t upper_bound);
