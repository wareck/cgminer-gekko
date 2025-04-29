#ifndef __MINER_H__
#define __MINER_H__

#include "config.h"

#ifdef __GNUC__
#ifdef __USE_FORTIFY_LEVEL
#undef __USE_FORTIFY_LEVEL
#endif
// ignore n truncation warnings
#define __USE_FORTIFY_LEVEL 1
#if __GNUC__ >= 7
// ignore the vast number of such non-bug warnings
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wformat-truncation"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wtautological-compare"
#endif
#endif

#include <stdbool.h>
#include <stdint.h>
#include <sys/time.h>
#include <pthread.h>
#include <jansson.h>
#include <inttypes.h>

#include <sched.h>

#include "elist.h"

#if HAVE_UTHASH_H
# include <uthash.h>
#else
# include "uthash.h"
#endif

#include "logging.h"
#include "util.h"
#include <sys/types.h>
#ifndef WIN32
# include <sys/socket.h>
# include <netdb.h>
#endif

#ifdef USE_USBUTILS
#include <semaphore.h>
#endif

#ifdef STDC_HEADERS
# include <stdlib.h>
# include <stddef.h>
#else
# ifdef HAVE_STDLIB_H
#  include <stdlib.h>
# endif
#endif
#ifdef HAVE_ALLOCA_H
# include <alloca.h>
#elif defined __GNUC__
# ifndef __FreeBSD__ /* FreeBSD has below #define in stdlib.h */
#  ifndef WIN32
#   define alloca __builtin_alloca
#  else
#   include <malloc.h>
#  endif
# endif
#elif defined _AIX
# define alloca __alloca
#elif defined _MSC_VER
# include <malloc.h>
# define alloca _alloca
#else
# ifndef HAVE_ALLOCA
#  ifdef  __cplusplus
extern "C"
#  endif
void *alloca (size_t);
# endif
#endif

#ifdef HAVE_LIBCURL
#include <curl/curl.h>
#else
typedef char CURL;
extern char *curly;
#define curl_easy_init(curl) (curly)
#define curl_easy_cleanup(curl) {}
#define curl_global_cleanup() {}
#define CURL_GLOBAL_ALL 0
#define curl_global_init(X) (0)
#endif

#ifdef __MINGW32__
#include <io.h>
static inline int fsync (int fd)
{
	return (FlushFileBuffers ((HANDLE) _get_osfhandle (fd))) ? 0 : -1;
}

#ifndef EWOULDBLOCK
# define EWOULDBLOCK EAGAIN
#endif

#ifndef MSG_DONTWAIT
# define MSG_DONTWAIT 0x1000000
#endif
#endif /* __MINGW32__ */

#if defined (__linux)
 #ifndef LINUX
  #define LINUX
 #endif
#endif

#ifdef WIN32
  #ifndef timersub
    #define timersub(a, b, result)                     \
    do {                                               \
      (result)->tv_sec = (a)->tv_sec - (b)->tv_sec;    \
      (result)->tv_usec = (a)->tv_usec - (b)->tv_usec; \
      if ((result)->tv_usec < 0) {                     \
        --(result)->tv_sec;                            \
        (result)->tv_usec += 1000000;                  \
      }                                                \
    } while (0)
  #endif
 #ifndef timeradd
 # define timeradd(a, b, result)			      \
   do {							      \
    (result)->tv_sec = (a)->tv_sec + (b)->tv_sec;	      \
    (result)->tv_usec = (a)->tv_usec + (b)->tv_usec;	      \
    if ((result)->tv_usec >= 1000000)			      \
      {							      \
	++(result)->tv_sec;				      \
	(result)->tv_usec -= 1000000;			      \
      }							      \
   } while (0)
 #endif
#endif


#ifdef USE_USBUTILS
  #include <libusb.h>
#endif

#ifdef USE_USBUTILS
  #include "usbutils.h"
#endif

#if (!defined(WIN32) && ((__GNUC__ > 4) || (__GNUC__ == 4 && __GNUC_MINOR__ >= 3))) \
    || (defined(WIN32) && ((__GNUC__ > 4) || (__GNUC__ == 4 && __GNUC_MINOR__ >= 7)))
#ifndef bswap_16
 #define bswap_16 __builtin_bswap16
 #define bswap_32 __builtin_bswap32
 #define bswap_64 __builtin_bswap64
#endif
#else
#if HAVE_BYTESWAP_H
#include <byteswap.h>
#elif defined(USE_SYS_ENDIAN_H)
#include <sys/endian.h>
#elif defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define bswap_16 OSSwapInt16
#define bswap_32 OSSwapInt32
#define bswap_64 OSSwapInt64
#else
#define	bswap_16(value)  \
 	((((value) & 0xff) << 8) | ((value) >> 8))

#define	bswap_32(value)	\
 	(((uint32_t)bswap_16((uint16_t)((value) & 0xffff)) << 16) | \
 	(uint32_t)bswap_16((uint16_t)((value) >> 16)))

#define	bswap_64(value)	\
 	(((uint64_t)bswap_32((uint32_t)((value) & 0xffffffff)) \
 	    << 32) | \
 	(uint64_t)bswap_32((uint32_t)((value) >> 32)))
#endif
#endif /* !defined(__GLXBYTEORDER_H__) */

/* This assumes htobe32 is a macro in endian.h, and if it doesn't exist, then
 * htobe64 also won't exist */
#ifndef htobe32
# if __BYTE_ORDER == __LITTLE_ENDIAN
#  define htole16(x) (x)
#  define le16toh(x) (x)
#  define htole32(x) (x)
#  define htole64(x) (x)
#  define le32toh(x) (x)
#  define le64toh(x) (x)
#  define be32toh(x) bswap_32(x)
#  define be64toh(x) bswap_64(x)
#  define htobe16(x) bswap_16(x)
#  define htobe32(x) bswap_32(x)
#  define htobe64(x) bswap_64(x)
# elif __BYTE_ORDER == __BIG_ENDIAN
#  define htole16(x) bswap_16(x)
#  define le16toh(x) bswap_16(x)
#  define htole32(x) bswap_32(x)
#  define le32toh(x) bswap_32(x)
#  define le64toh(x) bswap_64(x)
#  define htole64(x) bswap_64(x)
#  define be32toh(x) (x)
#  define be64toh(x) (x)
#  define htobe16(x) (x)
#  define htobe32(x) (x)
#  define htobe64(x) (x)
#else
#error UNKNOWN BYTE ORDER
#endif
#endif

#undef unlikely
#undef likely
#if defined(__GNUC__) && (__GNUC__ > 2) && defined(__OPTIMIZE__)
#define unlikely(expr) (__builtin_expect(!!(expr), 0))
#define likely(expr) (__builtin_expect(!!(expr), 1))
#else
#define unlikely(expr) (expr)
#define likely(expr) (expr)
#endif
#define __maybe_unused		__attribute__((unused))

#define uninitialised_var(x) x = x

#if defined(__i386__)
#define WANT_CRYPTOPP_ASM32
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

/* No semtimedop on apple so ignore timeout till we implement one */
#ifdef __APPLE__
#define semtimedop(SEM, SOPS, VAL, TIMEOUT) semop(SEM, SOPS, VAL)
#endif

#ifndef MIN
#define MIN(x, y)	((x) > (y) ? (y) : (x))
#endif
#ifndef MAX
#define MAX(x, y)	((x) > (y) ? (x) : (y))
#endif

#define MACSTR(_num) MACSTR2(_num)
#define MACSTR2(__num) #__num

/* Put avalon last to make it the last device it tries to detect to prevent it
 * trying to claim same chip but different devices. Adding a device here will
 * update all macros in the code that use the *_PARSE_COMMANDS macros for each
 * listed driver. */
#define FPGA_PARSE_COMMANDS(DRIVER_ADD_COMMAND) \
	DRIVER_ADD_COMMAND(bitforce) \
	DRIVER_ADD_COMMAND(modminer)

#define ASIC_PARSE_COMMANDS(DRIVER_ADD_COMMAND) \
	DRIVER_ADD_COMMAND(ants1) \
	DRIVER_ADD_COMMAND(ants2) \
	DRIVER_ADD_COMMAND(ants3) \
	DRIVER_ADD_COMMAND(avalon) \
	DRIVER_ADD_COMMAND(avalon2) \
	DRIVER_ADD_COMMAND(avalon4) \
	DRIVER_ADD_COMMAND(avalon7) \
	DRIVER_ADD_COMMAND(avalon8) \
	DRIVER_ADD_COMMAND(avalon9) \
	DRIVER_ADD_COMMAND(avalonlc3) \
	DRIVER_ADD_COMMAND(avalonm) \
	DRIVER_ADD_COMMAND(bab) \
	DRIVER_ADD_COMMAND(bflsc) \
	DRIVER_ADD_COMMAND(bitfury) \
	DRIVER_ADD_COMMAND(bitfury16) \
	DRIVER_ADD_COMMAND(bitmineA1) \
	DRIVER_ADD_COMMAND(blockerupter) \
	DRIVER_ADD_COMMAND(cointerra) \
	DRIVER_ADD_COMMAND(flow) \
	DRIVER_ADD_COMMAND(gekko) \
	DRIVER_ADD_COMMAND(dragonmintT1) \
	DRIVER_ADD_COMMAND(hashfast) \
	DRIVER_ADD_COMMAND(drillbit) \
	DRIVER_ADD_COMMAND(hashratio) \
	DRIVER_ADD_COMMAND(icarus) \
	DRIVER_ADD_COMMAND(klondike) \
	DRIVER_ADD_COMMAND(knc) \
	DRIVER_ADD_COMMAND(minion) \
	DRIVER_ADD_COMMAND(sp10) \
	DRIVER_ADD_COMMAND(sp30) \
	DRIVER_ADD_COMMAND(bitmain_soc)

#define DRIVER_PARSE_COMMANDS(DRIVER_ADD_COMMAND) \
	FPGA_PARSE_COMMANDS(DRIVER_ADD_COMMAND) \
	ASIC_PARSE_COMMANDS(DRIVER_ADD_COMMAND)

#define DRIVER_ENUM(X) DRIVER_##X,
#define DRIVER_PROTOTYPE(X) struct device_drv X##_drv;

/* Create drv_driver enum from DRIVER_PARSE_COMMANDS macro */
enum drv_driver {
	DRIVER_PARSE_COMMANDS(DRIVER_ENUM)
	DRIVER_MAX
};

/* Use DRIVER_PARSE_COMMANDS to generate extern device_drv prototypes */
DRIVER_PARSE_COMMANDS(DRIVER_PROTOTYPE)

enum alive {
	LIFE_WELL,
	LIFE_SICK,
	LIFE_DEAD,
	LIFE_NOSTART,
	LIFE_INIT,
};


enum pool_strategy {
	POOL_FAILOVER,
	POOL_ROUNDROBIN,
	POOL_ROTATE,
	POOL_LOADBALANCE,
	POOL_BALANCE,
};

#define TOP_STRATEGY (POOL_BALANCE)

struct strategies {
	const char *s;
};

struct cgpu_info;

extern void blank_get_statline_before(char *buf, size_t bufsiz, struct cgpu_info __maybe_unused *cgpu);

struct api_data;
struct thr_info;
struct work;

struct device_drv {
	enum drv_driver drv_id;

	char *dname;
	char *name;

	// DRV-global functions
	void (*drv_detect)(bool);

	// Device-specific functions
	void (*reinit_device)(struct cgpu_info *);
	void (*get_statline_before)(char *, size_t, struct cgpu_info *);
	void (*get_statline)(char *, size_t, struct cgpu_info *);
	struct api_data *(*get_api_stats)(struct cgpu_info *);
	struct api_data *(*get_api_debug)(struct cgpu_info *);
	bool (*get_stats)(struct cgpu_info *);
	void (*identify_device)(struct cgpu_info *); // e.g. to flash a led
	char *(*set_device)(struct cgpu_info *, char *option, char *setting, char *replybuf, size_t siz);

	// Thread-specific functions
	bool (*thread_prepare)(struct thr_info *);
	uint64_t (*can_limit_work)(struct thr_info *);
	bool (*thread_init)(struct thr_info *);
	bool (*prepare_work)(struct thr_info *, struct work *);

	/* Which hash work loop this driver uses. */
	void (*hash_work)(struct thr_info *);
	/* Two variants depending on whether the device divides work up into
	 * small pieces or works with whole work items and may or may not have
	 * a queue of its own. */
	int64_t (*scanhash)(struct thr_info *, struct work *, int64_t);
	int64_t (*scanwork)(struct thr_info *);

	/* Used to extract work from the hash table of queued work and tell
	 * the main loop that it should not add any further work to the table.
	 */
	bool (*queue_full)(struct cgpu_info *);
	/* Tell the driver of a block change */
	void (*flush_work)(struct cgpu_info *);
	/* Tell the driver of an updated work template for eg. stratum */
	void (*update_work)(struct cgpu_info *);

	void (*hw_error)(struct thr_info *);
	void (*thread_shutdown)(struct thr_info *);
	void (*thread_enable)(struct thr_info *);

	/* What should be zeroed in this device when global zero stats is sent */
	void (*zero_stats)(struct cgpu_info *);

	// Does it need to be free()d?
	bool copy;

	/* Highest target diff the device supports */
	double max_diff;

	/* Lowest diff the controller can safely run at */
	double min_diff;

	/* Does this device generate work itself and not require stratum work generation? */
	bool genwork;
};

extern struct device_drv *copy_drv(struct device_drv*);

enum dev_enable {
	DEV_ENABLED,
	DEV_DISABLED,
	DEV_RECOVER,
};

enum dev_reason {
	REASON_THREAD_FAIL_INIT,
	REASON_THREAD_ZERO_HASH,
	REASON_THREAD_FAIL_QUEUE,
	REASON_DEV_SICK_IDLE_60,
	REASON_DEV_DEAD_IDLE_600,
	REASON_DEV_NOSTART,
	REASON_DEV_OVER_HEAT,
	REASON_DEV_THERMAL_CUTOFF,
	REASON_DEV_COMMS_ERROR,
	REASON_DEV_THROTTLE,
};

#define REASON_NONE			"None"
#define REASON_THREAD_FAIL_INIT_STR	"Thread failed to init"
#define REASON_THREAD_ZERO_HASH_STR	"Thread got zero hashes"
#define REASON_THREAD_FAIL_QUEUE_STR	"Thread failed to queue work"
#define REASON_DEV_SICK_IDLE_60_STR	"Device idle for 60s"
#define REASON_DEV_DEAD_IDLE_600_STR	"Device dead - idle for 600s"
#define REASON_DEV_NOSTART_STR		"Device failed to start"
#define REASON_DEV_OVER_HEAT_STR	"Device over heated"
#define REASON_DEV_THERMAL_CUTOFF_STR	"Device reached thermal cutoff"
#define REASON_DEV_COMMS_ERROR_STR	"Device comms error"
#define REASON_DEV_THROTTLE_STR		"Device throttle"
#define REASON_UNKNOWN_STR		"Unknown reason - code bug"

#define MIN_SEC_UNSET 99999999

struct cgminer_stats {
	uint32_t getwork_calls;
	struct timeval getwork_wait;
	struct timeval getwork_wait_max;
	struct timeval getwork_wait_min;
};

// Just the actual network getworks to the pool
struct cgminer_pool_stats {
	uint32_t getwork_calls;
	uint32_t getwork_attempts;
	struct timeval getwork_wait;
	struct timeval getwork_wait_max;
	struct timeval getwork_wait_min;
	double getwork_wait_rolling;
	bool hadrolltime;
	bool canroll;
	bool hadexpire;
	uint32_t rolltime;
	double min_diff;
	double max_diff;
	double last_diff;
	uint32_t min_diff_count;
	uint32_t max_diff_count;
	uint64_t times_sent;
	uint64_t bytes_sent;
	uint64_t net_bytes_sent;
	uint64_t times_received;
	uint64_t bytes_received;
	uint64_t net_bytes_received;
};

struct cgpu_info {
	int cgminer_id;
	struct device_drv *drv;
	int device_id;
	char *name;
	char *device_path;
	void *device_data;
	void *dup_data;
	char *unique_id;
#ifdef USE_USBUTILS
	struct cg_usb_device *usbdev;
	struct cg_usb_info usbinfo;
	bool blacklisted;
	bool nozlp; // Device prefers no zero length packet
#endif
#if defined(USE_AVALON) || defined(USE_AVALON2) || defined (USE_AVALON_MINER)
	struct work **works;
	int work_array;
	int queued;
	int results;
#endif
#ifdef USE_MODMINER
	char fpgaid;
	unsigned char clock;
	pthread_mutex_t *modminer_mutex;
#endif
#ifdef USE_BITFORCE
	struct timeval work_start_tv;
	unsigned int wait_ms;
	unsigned int sleep_ms;
	double avg_wait_f;
	unsigned int avg_wait_d;
	uint32_t nonces;
	bool nonce_range;
	bool polling;
	bool flash_led;
#endif /* USE_BITFORCE */
#if defined(USE_BITFORCE) || defined(USE_BFLSC)
	pthread_mutex_t device_mutex;
#endif /* USE_BITFORCE || USE_BFLSC */
	enum dev_enable deven;
	int accepted;
	int rejected;
	int hw_errors;
	double rolling;
	double rolling1;
	double rolling5;
	double rolling15;
	double total_mhashes;
	double utility;
	enum alive status;
	char init[40];
	struct timeval last_message_tv;

	int threads;
	struct thr_info **thr;

	int64_t max_hashes;

	const char *kname;

	bool new_work;

	double temp;
#ifdef USE_DRAGONMINT_T1
	double temp_max;
	double temp_min;
	int fan_duty;
	int chainNum;
	double mhs_av;
#endif
	int cutofftemp;

	int64_t diff1;
	double diff_accepted;
	double diff_rejected;
	int last_share_pool;
	time_t last_share_pool_time;
	double last_share_diff;
	time_t last_device_valid_work;
	uint32_t last_nonce;

	time_t device_last_well;
	time_t device_last_not_well;
	enum dev_reason device_not_well_reason;
	int thread_fail_init_count;
	int thread_zero_hash_count;
	int thread_fail_queue_count;
	int dev_sick_idle_60_count;
	int dev_dead_idle_600_count;
	int dev_nostart_count;
	int dev_over_heat_count;	// It's a warning but worth knowing
	int dev_thermal_cutoff_count;
	int dev_comms_error_count;
	int dev_throttle_count;

	struct cgminer_stats cgminer_stats;

	pthread_rwlock_t qlock;
	struct work *queued_work;
	struct work *unqueued_work;
	unsigned int queued_count;

	bool shutdown;

	struct timeval dev_start_tv;

	/* For benchmarking only */
	int hidiff;
	int lodiff;
	int direction;
};

extern bool add_cgpu(struct cgpu_info*);

struct thread_q {
	struct list_head	q;

	bool frozen;

	pthread_mutex_t		mutex;
	pthread_cond_t		cond;
};

struct thr_info {
	int		id;
	int		device_thread;
	bool		primary_thread;

	pthread_t	pth;
	cgsem_t		sem;
	struct thread_q	*q;
	struct cgpu_info *cgpu;
	void *cgpu_data;
	struct timeval last;
	struct timeval sick;

	bool	pause;
	bool	getwork;

	bool	work_restart;
	bool	work_update;
#if defined (USE_AVALON2) || defined (USE_AVALON4) || defined (USE_AVALON7) || defined (USE_AVALON8) || defined (USE_AVALON9) || defined (USE_AVALONLC3) || defined (USE_AVALON_MINER) || defined (USE_HASHRATIO)
	bool	clean_jobs;
#endif
};

struct string_elist {
	char *string;
	bool free_me;

	struct list_head list;
};

static inline void string_elist_add(const char *s, struct list_head *head)
{
	struct string_elist *n;

	n = cgcalloc(1, sizeof(*n));
	n->string = strdup(s);
	n->free_me = true;
	list_add_tail(&n->list, head);
}

static inline void string_elist_del(struct string_elist *item)
{
	if (item->free_me)
		free(item->string);
	list_del(&item->list);
}


static inline uint32_t swab32(uint32_t v)
{
	return bswap_32(v);
}

static inline void swap256(void *dest_p, const void *src_p)
{
	uint32_t *dest = dest_p;
	const uint32_t *src = src_p;

	dest[0] = src[7];
	dest[1] = src[6];
	dest[2] = src[5];
	dest[3] = src[4];
	dest[4] = src[3];
	dest[5] = src[2];
	dest[6] = src[1];
	dest[7] = src[0];
}

static inline void swab256(void *dest_p, const void *src_p)
{
	uint32_t *dest = dest_p;
	const uint32_t *src = src_p;

	dest[0] = swab32(src[7]);
	dest[1] = swab32(src[6]);
	dest[2] = swab32(src[5]);
	dest[3] = swab32(src[4]);
	dest[4] = swab32(src[3]);
	dest[5] = swab32(src[2]);
	dest[6] = swab32(src[1]);
	dest[7] = swab32(src[0]);
}

static inline void flip12(void *dest_p, const void *src_p)
{
	uint32_t *dest = dest_p;
	const uint32_t *src = src_p;
	int i;

	for (i = 0; i < 3; i++)
		dest[i] = swab32(src[i]);
}

static inline void flip32(void *dest_p, const void *src_p)
{
	uint32_t *dest = dest_p;
	const uint32_t *src = src_p;
	int i;

	for (i = 0; i < 8; i++)
		dest[i] = swab32(src[i]);
}

static inline void flip64(void *dest_p, const void *src_p)
{
	uint32_t *dest = dest_p;
	const uint32_t *src = src_p;
	int i;

	for (i = 0; i < 16; i++)
		dest[i] = swab32(src[i]);
}

static inline void flip80(void *dest_p, const void *src_p)
{
	uint32_t *dest = dest_p;
	const uint32_t *src = src_p;
	int i;

	for (i = 0; i < 20; i++)
		dest[i] = swab32(src[i]);
}

static inline void flip128(void *dest_p, const void *src_p)
{
	uint32_t *dest = dest_p;
	const uint32_t *src = src_p;
	int i;

	for (i = 0; i < 32; i++)
		dest[i] = swab32(src[i]);
}

/* For flipping to the correct endianness if necessary */
#if defined(__BIG_ENDIAN__) || defined(MIPSEB)
static inline void endian_flip32(void *dest_p, const void *src_p)
{
	flip32(dest_p, src_p);
}

static inline void endian_flip128(void *dest_p, const void *src_p)
{
	flip128(dest_p, src_p);
}
#else
static inline void
endian_flip32(void __maybe_unused *dest_p, const void __maybe_unused *src_p)
{
}

static inline void
endian_flip128(void __maybe_unused *dest_p, const void __maybe_unused *src_p)
{
}
#endif

extern double cgpu_runtime(struct cgpu_info *cgpu);
extern double tsince_restart(void);
extern double tsince_update(void);
extern void __quit(int status, bool clean);
extern void _quit(int status);

/*
 * Set this to non-zero to enable lock tracking
 * Use the API lockstats command to see the locking status on stderr
 *  i.e. in your log file if you 2> log.log - but not on the screen
 * API lockstats is privilidged but will always exist and will return
 *	success if LOCK_TRACKING is enabled and warning if disabled
 * In production code, this should never be enabled since it will slow down all locking
 * So, e.g. use it to track down a deadlock - after a reproducable deadlock occurs
 * ... Of course if the API code itself deadlocks, it wont help :)
 */
#define LOCK_TRACKING 0

#if LOCK_TRACKING
enum cglock_typ {
	CGLOCK_MUTEX,
	CGLOCK_RW,
	CGLOCK_UNKNOWN
};

extern uint64_t api_getlock(void *lock, const char *file, const char *func, const int line);
extern void api_gotlock(uint64_t id, void *lock, const char *file, const char *func, const int line);
extern uint64_t api_trylock(void *lock, const char *file, const char *func, const int line);
extern void api_didlock(uint64_t id, int ret, void *lock, const char *file, const char *func, const int line);
extern void api_gunlock(void *lock, const char *file, const char *func, const int line);
extern void api_initlock(void *lock, enum cglock_typ typ, const char *file, const char *func, const int line);

#define GETLOCK(_lock, _file, _func, _line) uint64_t _id1 = api_getlock((void *)(_lock), _file, _func, _line)
#define GOTLOCK(_lock, _file, _func, _line) api_gotlock(_id1, (void *)(_lock), _file, _func, _line)
#define TRYLOCK(_lock, _file, _func, _line) uint64_t _id2 = api_trylock((void *)(_lock), _file, _func, _line)
#define DIDLOCK(_ret, _lock, _file, _func, _line) api_didlock(_id2, _ret, (void *)(_lock), _file, _func, _line)
#define GUNLOCK(_lock, _file, _func, _line) api_gunlock((void *)(_lock), _file, _func, _line)
#define INITLOCK(_lock, _typ, _file, _func, _line) api_initlock((void *)(_lock), _typ, _file, _func, _line)
#else
#define GETLOCK(_lock, _file, _func, _line)
#define GOTLOCK(_lock, _file, _func, _line)
#define TRYLOCK(_lock, _file, _func, _line)
#define DIDLOCK(_ret, _lock, _file, _func, _line)
#define GUNLOCK(_lock, _file, _func, _line)
#define INITLOCK(_typ, _lock, _file, _func, _line)
#endif

#define mutex_lock(_lock) _mutex_lock(_lock, __FILE__, __func__, __LINE__)
#define mutex_unlock_noyield(_lock) _mutex_unlock_noyield(_lock, __FILE__, __func__, __LINE__)
#define mutex_unlock(_lock) _mutex_unlock(_lock, __FILE__, __func__, __LINE__)
#define mutex_trylock(_lock) _mutex_trylock(_lock, __FILE__, __func__, __LINE__)
#define wr_lock(_lock) _wr_lock(_lock, __FILE__, __func__, __LINE__)
#define wr_trylock(_lock) _wr_trylock(_lock, __FILE__, __func__, __LINE__)
#define rd_lock(_lock) _rd_lock(_lock, __FILE__, __func__, __LINE__)
#define rw_unlock(_lock) _rw_unlock(_lock, __FILE__, __func__, __LINE__)
#define rd_unlock_noyield(_lock) _rd_unlock_noyield(_lock, __FILE__, __func__, __LINE__)
#define wr_unlock_noyield(_lock) _wr_unlock_noyield(_lock, __FILE__, __func__, __LINE__)
#define rd_unlock(_lock) _rd_unlock(_lock, __FILE__, __func__, __LINE__)
#define wr_unlock(_lock) _wr_unlock(_lock, __FILE__, __func__, __LINE__)
#define mutex_init(_lock) _mutex_init(_lock, __FILE__, __func__, __LINE__)
#define rwlock_init(_lock) _rwlock_init(_lock, __FILE__, __func__, __LINE__)
#define cglock_init(_lock) _cglock_init(_lock, __FILE__, __func__, __LINE__)
#define cg_rlock(_lock) _cg_rlock(_lock, __FILE__, __func__, __LINE__)
#define cg_ilock(_lock) _cg_ilock(_lock, __FILE__, __func__, __LINE__)
#define cg_uilock(_lock) _cg_uilock(_lock, __FILE__, __func__, __LINE__)
#define cg_ulock(_lock) _cg_ulock(_lock, __FILE__, __func__, __LINE__)
#define cg_wlock(_lock) _cg_wlock(_lock, __FILE__, __func__, __LINE__)
#define cg_dwlock(_lock) _cg_dwlock(_lock, __FILE__, __func__, __LINE__)
#define cg_dwilock(_lock) _cg_dwilock(_lock, __FILE__, __func__, __LINE__)
#define cg_dlock(_lock) _cg_dlock(_lock, __FILE__, __func__, __LINE__)
#define cg_runlock(_lock) _cg_runlock(_lock, __FILE__, __func__, __LINE__)
#define cg_ruwlock(_lock) _cg_ruwlock(_lock, __FILE__, __func__, __LINE__)
#define cg_wunlock(_lock) _cg_wunlock(_lock, __FILE__, __func__, __LINE__)

static inline void _mutex_lock(pthread_mutex_t *lock, const char *file, const char *func, const int line)
{
	GETLOCK(lock, file, func, line);
	if (unlikely(pthread_mutex_lock(lock)))
		quitfrom(1, file, func, line, "WTF MUTEX ERROR ON LOCK! errno=%d", errno);
	GOTLOCK(lock, file, func, line);
}

static inline void _mutex_unlock_noyield(pthread_mutex_t *lock, const char *file, const char *func, const int line)
{
	if (unlikely(pthread_mutex_unlock(lock)))
		quitfrom(1, file, func, line, "WTF MUTEX ERROR ON UNLOCK! errno=%d", errno);
	GUNLOCK(lock, file, func, line);
}

static inline void _mutex_unlock(pthread_mutex_t *lock, const char *file, const char *func, const int line)
{
	_mutex_unlock_noyield(lock, file, func, line);
	selective_yield();
}

static inline int _mutex_trylock(pthread_mutex_t *lock, __maybe_unused const char *file, __maybe_unused const char *func, __maybe_unused const int line)
{
	TRYLOCK(lock, file, func, line);
	int ret = pthread_mutex_trylock(lock);
	DIDLOCK(ret, lock, file, func, line);
	return ret;
}

static inline void _wr_lock(pthread_rwlock_t *lock, const char *file, const char *func, const int line)
{
	GETLOCK(lock, file, func, line);
	if (unlikely(pthread_rwlock_wrlock(lock)))
		quitfrom(1, file, func, line, "WTF WRLOCK ERROR ON LOCK! errno=%d", errno);
	GOTLOCK(lock, file, func, line);
}

static inline int _wr_trylock(pthread_rwlock_t *lock, __maybe_unused const char *file, __maybe_unused const char *func, __maybe_unused const int line)
{
	TRYLOCK(lock, file, func, line);
	int ret = pthread_rwlock_trywrlock(lock);
	DIDLOCK(ret, lock, file, func, line);
	return ret;
}

static inline void _rd_lock(pthread_rwlock_t *lock, const char *file, const char *func, const int line)
{
	GETLOCK(lock, file, func, line);
	if (unlikely(pthread_rwlock_rdlock(lock)))
		quitfrom(1, file, func, line, "WTF RDLOCK ERROR ON LOCK! errno=%d", errno);
	GOTLOCK(lock, file, func, line);
}

static inline void _rw_unlock(pthread_rwlock_t *lock, const char *file, const char *func, const int line)
{
	if (unlikely(pthread_rwlock_unlock(lock)))
		quitfrom(1, file, func, line, "WTF RWLOCK ERROR ON UNLOCK! errno=%d", errno);
	GUNLOCK(lock, file, func, line);
}

static inline void _rd_unlock_noyield(pthread_rwlock_t *lock, const char *file, const char *func, const int line)
{
	_rw_unlock(lock, file, func, line);
}

static inline void _wr_unlock_noyield(pthread_rwlock_t *lock, const char *file, const char *func, const int line)
{
	_rw_unlock(lock, file, func, line);
}

static inline void _rd_unlock(pthread_rwlock_t *lock, const char *file, const char *func, const int line)
{
	_rw_unlock(lock, file, func, line);
	selective_yield();
}

static inline void _wr_unlock(pthread_rwlock_t *lock, const char *file, const char *func, const int line)
{
	_rw_unlock(lock, file, func, line);
	selective_yield();
}

static inline void _mutex_init(pthread_mutex_t *lock, const char *file, const char *func, const int line)
{
	if (unlikely(pthread_mutex_init(lock, NULL)))
		quitfrom(1, file, func, line, "Failed to pthread_mutex_init errno=%d", errno);
	INITLOCK(lock, CGLOCK_MUTEX, file, func, line);
}

static inline void mutex_destroy(pthread_mutex_t *lock)
{
	/* Ignore return code. This only invalidates the mutex on linux but
	 * releases resources on windows. */
	pthread_mutex_destroy(lock);
}

static inline void _rwlock_init(pthread_rwlock_t *lock, const char *file, const char *func, const int line)
{
	if (unlikely(pthread_rwlock_init(lock, NULL)))
		quitfrom(1, file, func, line, "Failed to pthread_rwlock_init errno=%d", errno);
	INITLOCK(lock, CGLOCK_RW, file, func, line);
}

static inline void rwlock_destroy(pthread_rwlock_t *lock)
{
	pthread_rwlock_destroy(lock);
}

static inline void _cglock_init(cglock_t *lock, const char *file, const char *func, const int line)
{
	_mutex_init(&lock->mutex, file, func, line);
	_rwlock_init(&lock->rwlock, file, func, line);
}

static inline void cglock_destroy(cglock_t *lock)
{
	rwlock_destroy(&lock->rwlock);
	mutex_destroy(&lock->mutex);
}

/* Read lock variant of cglock. Cannot be promoted. */
static inline void _cg_rlock(cglock_t *lock, const char *file, const char *func, const int line)
{
	_mutex_lock(&lock->mutex, file, func, line);
	_rd_lock(&lock->rwlock, file, func, line);
	_mutex_unlock_noyield(&lock->mutex, file, func, line);
}

/* Intermediate variant of cglock - behaves as a read lock but can be promoted
 * to a write lock or demoted to read lock. */
static inline void _cg_ilock(cglock_t *lock, const char *file, const char *func, const int line)
{
	_mutex_lock(&lock->mutex, file, func, line);
}

/* Unlock intermediate variant without changing to read or write version */
static inline void _cg_uilock(cglock_t *lock, const char *file, const char *func, const int line)
{
	_mutex_unlock(&lock->mutex, file, func, line);
}

/* Upgrade intermediate variant to a write lock */
static inline void _cg_ulock(cglock_t *lock, const char *file, const char *func, const int line)
{
	_wr_lock(&lock->rwlock, file, func, line);
}

/* Write lock variant of cglock */
static inline void _cg_wlock(cglock_t *lock, const char *file, const char *func, const int line)
{
	_mutex_lock(&lock->mutex, file, func, line);
	_wr_lock(&lock->rwlock, file, func, line);
}

/* Downgrade write variant to a read lock */
static inline void _cg_dwlock(cglock_t *lock, const char *file, const char *func, const int line)
{
	_wr_unlock_noyield(&lock->rwlock, file, func, line);
	_rd_lock(&lock->rwlock, file, func, line);
	_mutex_unlock_noyield(&lock->mutex, file, func, line);
}

/* Demote a write variant to an intermediate variant */
static inline void _cg_dwilock(cglock_t *lock, const char *file, const char *func, const int line)
{
	_wr_unlock(&lock->rwlock, file, func, line);
}

/* Downgrade intermediate variant to a read lock */
static inline void _cg_dlock(cglock_t *lock, const char *file, const char *func, const int line)
{
	_rd_lock(&lock->rwlock, file, func, line);
	_mutex_unlock_noyield(&lock->mutex, file, func, line);
}

static inline void _cg_runlock(cglock_t *lock, const char *file, const char *func, const int line)
{
	_rd_unlock(&lock->rwlock, file, func, line);
}

/* This drops the read lock and grabs a write lock. It does NOT protect data
 * between the two locks! */
static inline void _cg_ruwlock(cglock_t *lock, const char *file, const char *func, const int line)
{
	_rd_unlock_noyield(&lock->rwlock, file, func, line);
	_cg_wlock(lock, file, func, line);
}

static inline void _cg_wunlock(cglock_t *lock, const char *file, const char *func, const int line)
{
	_wr_unlock_noyield(&lock->rwlock, file, func, line);
	_mutex_unlock(&lock->mutex, file, func, line);
}

struct pool;

#define API_LISTEN_ADDR "0.0.0.0"
#define API_MCAST_CODE "FTW"
#define API_MCAST_ADDR "224.0.0.75"

extern bool opt_mac_yield;
extern bool opt_widescreen;
extern bool opt_work_update;
#if defined (USE_AVALON2) || defined (USE_AVALON4) || defined (USE_AVALON7) || defined (USE_AVALON8) || defined (USE_AVALON9) || defined (USE_AVALONLC3) || defined (USE_AVALON_MINER) || defined (USE_HASHRATIO)
extern bool opt_clean_jobs;
extern int opt_force_clean_jobs;
#endif
extern bool opt_protocol;
extern bool have_longpoll;
extern char *opt_kernel_path;
extern char *opt_socks_proxy;
extern int opt_suggest_diff;
extern char *cgminer_path;
extern bool opt_lowmem;
extern bool opt_autofan;
extern bool opt_autoengine;
extern bool use_curses;
extern char *opt_api_allow;
extern bool opt_api_mcast;
extern char *opt_api_mcast_addr;
extern char *opt_api_mcast_code;
extern char *opt_api_mcast_des;
extern int opt_api_mcast_port;
extern char *opt_api_groups;
extern char *opt_api_description;
extern int opt_api_port;
extern char *opt_api_host;
extern bool opt_api_listen;
extern bool opt_api_network;
extern bool opt_delaynet;
extern time_t last_getwork;
extern bool opt_restart;
#ifdef USE_ICARUS
extern char *opt_icarus_options;
extern char *opt_icarus_timing;
extern float opt_anu_freq;
extern float opt_au3_freq;
extern int opt_au3_volt;
extern float opt_rock_freq;
#endif
extern bool opt_worktime;
#ifdef USE_AVALON
extern char *opt_avalon_options;
extern char *opt_bitburner_fury_options;
#endif
#ifdef USE_FLOW
extern char *opt_flow_serial;
extern int opt_flow_start_freq;
extern float opt_flow_step_freq;
extern float opt_flow_freq;
extern int opt_flow_tune;
#endif
#ifdef USE_GEKKO
extern char *opt_gekko_serial;
extern bool opt_gekko_noboost;
extern bool opt_gekko_lowboost;
extern bool opt_gekko_gsc_detect;
extern bool opt_gekko_gsd_detect;
extern bool opt_gekko_gse_detect;
extern bool opt_gekko_gsh_detect;
extern bool opt_gekko_gsi_detect;
extern bool opt_gekko_gsf_detect;
extern bool opt_gekko_r909_detect;
extern bool opt_gekko_gsa1_detect;
extern bool opt_gekko_gsk_detect;
extern float opt_gekko_gsc_freq;
extern float opt_gekko_gsd_freq;
extern float opt_gekko_gse_freq;
extern float opt_gekko_tune_down;
extern float opt_gekko_tune_up;
extern float opt_gekko_wait_factor;
extern float opt_gekko_step_freq;
extern int opt_gekko_bauddiv;
extern int opt_gekko_gsh_freq;
extern int opt_gekko_gsi_freq;
extern int opt_gekko_gsf_freq;
extern int opt_gekko_r909_freq;
extern int opt_gekko_gsa1_freq;
extern int opt_gekko_gsk_freq;
extern int opt_gekko_gsh_vcore;
extern int opt_gekko_start_freq;
extern int opt_gekko_step_delay;
extern int opt_gekko_tune2;
extern int opt_gekko_gsa1_start_freq;
extern int opt_gekko_gsa1_corev;
#endif
#ifdef USE_KLONDIKE
extern char *opt_klondike_options;
#endif
#ifdef USE_DRILLBIT
extern char *opt_drillbit_options;
extern char *opt_drillbit_auto;
#endif
#ifdef USE_BAB
extern char *opt_bab_options;
#endif
#ifdef USE_BITMINE_A1
extern char *opt_bitmine_a1_options;
#endif
#ifdef USE_DRAGONMINT_T1
extern char *opt_dragonmint_t1_options;
extern int opt_T1Pll[];
extern int opt_T1Vol[];
extern int opt_T1VID[];
extern bool opt_T1auto;
extern bool opt_T1_efficient;
extern bool opt_T1_performance;
extern int opt_T1_target;
#endif
#ifdef USE_ANT_S1
extern char *opt_bitmain_options;
extern char *opt_bitmain_freq;
extern bool opt_bitmain_hwerror;
#endif
#if (defined(USE_ANT_S2) || defined(USE_ANT_S3))
#ifndef USE_ANT_S3
extern char *opt_bitmain_dev;
#endif
extern char *opt_bitmain_options;
extern char *opt_bitmain_freq;
extern bool opt_bitmain_hwerror;
extern bool opt_bitmain_checkall;
extern bool opt_bitmain_checkn2diff;
extern bool opt_bitmain_beeper;
extern bool opt_bitmain_tempoverctrl;
extern char *opt_bitmain_voltage;
#endif
#ifdef USE_MINION
extern int opt_minion_chipreport;
extern char *opt_minion_cores;
extern bool opt_minion_extra;
extern char *opt_minion_freq;
extern int opt_minion_freqchange;
extern int opt_minion_freqpercent;
extern bool opt_minion_idlecount;
extern int opt_minion_ledcount;
extern int opt_minion_ledlimit;
extern bool opt_minion_noautofreq;
extern bool opt_minion_overheat;
extern int opt_minion_spidelay;
extern char *opt_minion_spireset;
extern int opt_minion_spisleep;
extern int opt_minion_spiusec;
extern char *opt_minion_temp;
#endif
#ifdef USE_USBUTILS
extern char *opt_usb_select;
extern int opt_usbdump;
extern bool opt_usb_list_all;
extern cgsem_t usb_resource_sem;
#endif
#ifdef USE_BITFORCE
extern bool opt_bfl_noncerange;
#endif
extern int swork_id;

#if LOCK_TRACKING
extern pthread_mutex_t lockstat_lock;
#endif

extern pthread_rwlock_t netacc_lock;

extern const uint32_t sha256_init_state[];
#ifdef HAVE_LIBCURL
extern json_t *json_web_config(const char *url);
extern json_t *json_rpc_call(CURL *curl, const char *url, const char *userpass,
			     const char *rpc_req, bool, bool, int *,
			     struct pool *pool, bool);
struct pool;
extern struct pool *opt_btcd;
#endif
extern const char *proxytype(proxytypes_t proxytype);
extern char *get_proxy(char *url, struct pool *pool);
extern void __bin2hex(char *s, const unsigned char *p, size_t len);
extern char *bin2hex(const unsigned char *p, size_t len);
extern bool hex2bin(unsigned char *p, const char *hexstr, size_t len);

typedef bool (*sha256_func)(struct thr_info*, const unsigned char *pmidstate,
	unsigned char *pdata,
	unsigned char *phash1, unsigned char *phash,
	const unsigned char *ptarget,
	uint32_t max_nonce,
	uint32_t *last_nonce,
	uint32_t nonce);

extern bool fulltest(const unsigned char *hash, const unsigned char *target);

extern const int max_scantime;

extern cglock_t control_lock;
extern pthread_mutex_t hash_lock;
extern pthread_mutex_t console_lock;
extern cglock_t ch_lock;
extern pthread_rwlock_t mining_thr_lock;
extern pthread_rwlock_t devices_lock;

extern pthread_mutex_t restart_lock;
extern pthread_cond_t restart_cond;

extern void clear_stratum_shares(struct pool *pool);
extern void clear_pool_work(struct pool *pool);
extern void set_target(unsigned char *dest_target, double diff);
#if defined (USE_AVALON2) || defined (USE_AVALON4) || defined (USE_AVALON7) || defined (USE_AVALON8) || defined (USE_AVALON9) || defined (USE_AVALONLC3) || defined (USE_AVALON_MINER) || defined (USE_HASHRATIO)
bool submit_nonce2_nonce(struct thr_info *thr, struct pool *pool, struct pool *real_pool,
			 uint32_t nonce2, uint32_t nonce, uint32_t ntime);
uint32_t gen_merkle_root(struct pool *pool, uint64_t nonce2);
#endif
#ifdef USE_BITMAIN_SOC
void get_work_by_nonce2(struct thr_info *thr,
						struct work **work,
						struct pool *pool,
						struct pool *real_pool,
						uint64_t nonce2,
						uint32_t version);
#endif
extern int restart_wait(struct thr_info *thr, unsigned int mstime);

extern void raise_cgminer(void);
extern void kill_work(void);

extern void reinit_device(struct cgpu_info *cgpu);

extern void api(int thr_id);

extern struct pool *current_pool(void);
extern int enabled_pools;
extern void get_intrange(char *arg, int *val1, int *val2);
extern bool detect_stratum(struct pool *pool, char *url);
extern void print_summary(void);
extern void adjust_quota_gcd(void);
extern struct pool *add_pool(void);
extern bool add_pool_details(struct pool *pool, bool live, char *url, char *user, char *pass);

#define MAX_DEVICES 4096

extern bool hotplug_mode;
extern int hotplug_time;
extern struct list_head scan_devices;
extern int nDevs;
extern int num_processors;
extern int hw_errors;
extern bool use_syslog;
extern bool opt_quiet;
extern struct thr_info *control_thr;
extern struct thr_info **mining_thr;
extern double total_secs;
extern int mining_threads;
extern int total_devices;
extern int zombie_devs;
extern struct cgpu_info **devices;
extern int total_pools;
extern struct pool **pools;
extern struct strategies strategies[];
extern enum pool_strategy pool_strategy;
extern int opt_rotate_period;
extern double rolling1, rolling5, rolling15;
extern double total_rolling;
extern double total_mhashes_done;
extern unsigned int new_blocks;
extern unsigned int found_blocks;
extern int64_t total_accepted, total_rejected, total_diff1;
extern int64_t total_getworks, total_stale, total_discarded;
extern double total_diff_accepted, total_diff_rejected, total_diff_stale;
extern unsigned int local_work;
extern unsigned int total_go, total_ro;
extern const int opt_cutofftemp;
extern int opt_log_interval;
extern uint64_t global_hashrate;
extern char current_hash[68];
extern double current_diff;
extern uint64_t best_diff;
extern struct timeval block_timeval;
extern char *workpadding;

#ifdef USE_BITMAIN_SOC
extern char displayed_hash_rate[16];
#define NONCE_BUFF 4096
extern char nonce_num10_string[NONCE_BUFF];
extern char nonce_num30_string[NONCE_BUFF];
extern char nonce_num60_string[NONCE_BUFF];
extern char g_miner_version[256];
extern char g_miner_compiletime[256];
extern char g_miner_type[256];
extern double new_total_mhashes_done;
extern double new_total_secs;
extern time_t total_tv_start_sys;
extern time_t total_tv_end_sys;
extern void writeInitLogFile(char *logstr);
#endif

struct curl_ent {
	CURL *curl;
	struct list_head node;
	struct timeval tv;
};

/* Disabled needs to be the lowest enum as a freshly calloced value will then
 * equal disabled */
enum pool_enable {
	POOL_DISABLED,
	POOL_ENABLED,
	POOL_REJECTING,
};

struct stratum_work {
	char *job_id;
	unsigned char **merkle_bin;
	bool clean;

	double diff;
};

#define RBUFSIZE 8192
#define RECVSIZE (RBUFSIZE - 4)

struct pool {
	int pool_no;
	int prio;
	int64_t accepted, rejected;
	int seq_rejects;
	int seq_getfails;
	int solved;
	int64_t diff1;
	char diff[8];
	int quota;
	int quota_gcd;
	int quota_used;
	int works;

	double diff_accepted;
	double diff_rejected;
	double diff_stale;

	/* Vmask data */
	bool vmask; /* Supports vmask */
	uint32_t vmask_001[16];
	char vmask_002[16][9];
	int vmask_003[4];

	bool submit_fail;
	bool idle;
	bool probed;
	enum pool_enable enabled;
	bool submit_old;
	bool removed;
	bool lp_started;
	bool blocking;

	char *hdr_path;
	char *lp_url;

	unsigned int getwork_requested;
	unsigned int stale_shares;
	unsigned int discarded_work;
	unsigned int getfail_occasions;
	unsigned int remotefail_occasions;
	struct timeval tv_idle;

	double utility;
	int last_shares, shares;

	char *rpc_req;
	char *rpc_url;
	char *rpc_userpass;
	char *rpc_user, *rpc_pass;
	proxytypes_t rpc_proxytype;
	char *rpc_proxy;

	pthread_mutex_t pool_lock;
	cglock_t data_lock;

	struct thread_q *submit_q;
	struct thread_q *getwork_q;

	pthread_t longpoll_thread;
	pthread_t test_thread;
	bool testing;

	int curls;
	pthread_cond_t cr_cond;
	struct list_head curlring;

	time_t last_share_time;
	double last_share_diff;
	uint64_t best_diff;
	uint64_t bad_work;

	struct cgminer_stats cgminer_stats;
	struct cgminer_pool_stats cgminer_pool_stats;

	/* The last block this particular pool knows about */
	char prev_block[32];

	/* Stratum variables */
	char *stratum_url;
#ifdef USE_XTRANONCE
	bool extranonce_subscribe;
#endif
	char *stratum_port;
	SOCKETTYPE sock;
	char *sockbuf;
	size_t sockbuf_size;
	char *sockaddr_url; /* stripped url used for sockaddr */
	char *sockaddr_proxy_url;
	char *sockaddr_proxy_port;

	char *nonce1;
	unsigned char *nonce1bin;
	uint64_t nonce2;
	int n2size;
	char *sessionid;
	bool has_stratum;
	bool stratum_active;
	bool stratum_init;
	bool stratum_notify;
	struct stratum_work swork;
	pthread_t stratum_sthread;
	pthread_t stratum_rthread;
	pthread_mutex_t stratum_lock;
	struct thread_q *stratum_q;
	int sshares; /* stratum shares submitted waiting on response */

	/* GBT  variables */
	bool has_gbt;
	cglock_t gbt_lock;
	unsigned char previousblockhash[32];
	unsigned char gbt_target[32];
	char *coinbasetxn;
	char *longpollid;
	char *gbt_workid;
	int gbt_expires;
	uint32_t gbt_version;
	uint32_t curtime;
	uint32_t gbt_bits;
	unsigned char *txn_hashes;
	int gbt_txns;
	int height;

	bool gbt_solo;
	unsigned char merklebin[16 * 32];
	int transactions;
	char *txn_data;
	unsigned char scriptsig_base[100];
	unsigned char script_pubkey[25 + 3];
	int nValue;
	CURL *gbt_curl;
	bool gbt_curl_inuse;

	/* Shared by both stratum & GBT */
	size_t n1_len;
	unsigned char *coinbase;
	int coinbase_len;
	int nonce2_offset;
	unsigned char header_bin[128];
	int merkles;
	char prev_hash[68];
	char bbversion[12];
	char nbit[12];
	char ntime[12];
	double next_diff;
	double diff_after;
	double sdiff;
	uint32_t current_height;

	struct timeval tv_lastwork;
#ifdef USE_BITMAIN_SOC
    bool support_vil;
    int version_num;
    int version[4];
#endif
};

#define GETWORK_MODE_TESTPOOL 'T'
#define GETWORK_MODE_POOL 'P'
#define GETWORK_MODE_LP 'L'
#define GETWORK_MODE_BENCHMARK 'B'
#define GETWORK_MODE_STRATUM 'S'
#define GETWORK_MODE_GBT 'G'
#define GETWORK_MODE_SOLO 'C'

struct work {
	unsigned char	data[128];
	unsigned char	midstate[32];
	unsigned char   midstate1[32];
	unsigned char   midstate2[32];
	unsigned char   midstate3[32];
	unsigned char	target[32];
	unsigned char	hash[32];

	uint16_t        micro_job_id;
	bool		direct_vmask;
	unsigned char	base_bv[4];

	/* This is the diff the device is currently aiming for and must be
	 * the minimum of work_difficulty & drv->max_diff */
	double		device_diff;
	uint64_t	share_diff;

	int		rolls;
	int		drv_rolllimit; /* How much the driver can roll ntime */
	uint32_t	nonce; /* For devices that hash sole work */

	struct thr_info	*thr;
	int		thr_id;
	struct pool	*pool;
	struct timeval	tv_staged;

	bool		mined;
	bool		clone;
	bool		cloned;
	int		rolltime;
	bool		longpoll;
	bool		stale;
	bool		mandatory;
	bool		block;

	bool		stratum;
	char 		*job_id;
	uint64_t	nonce2;
	size_t		nonce2_len;
	char		*ntime;
	double		sdiff;
	char		*nonce1;

	bool		gbt;
	char		*coinbase;
	int		gbt_txns;

	unsigned int	work_block;
	uint32_t	id;
	UT_hash_handle	hh;

	/* This is the diff work we're aiming to submit and should match the
	 * work->target binary */
	double		work_difficulty;

	// Allow devices to identify work if multiple sub-devices
	int		subid;
	// Allow devices to flag work for their own purposes
	bool		devflag;
	// Allow devices to timestamp work for their own purposes
	struct timeval	tv_stamp;

	struct timeval	tv_getwork;
	struct timeval	tv_getwork_reply;
	struct timeval	tv_cloned;
	struct timeval	tv_work_start;
	struct timeval	tv_work_found;
	char		getwork_mode;
#ifdef USE_BITMAIN_SOC
    int version;
#endif
};

// enable grossly global stratum work stats
#define STRATUM_WORK_TIMING 1

#if STRATUM_WORK_TIMING
extern cglock_t swt_lock;
extern uint64_t stratum_work_count;
extern uint64_t stratum_work_time;
extern uint64_t stratum_work_min;
extern uint64_t stratum_work_max;
extern uint64_t stratum_work_time0;
extern uint64_t stratum_work_time10;
extern uint64_t stratum_work_time100;
#endif

#ifdef USE_MODMINER
struct modminer_fpga_state {
	bool work_running;
	struct work running_work;
	struct timeval tv_workstart;
	uint32_t hashes;

	char next_work_cmd[46];
	char fpgaid;

	bool overheated;
	bool new_work;

	uint32_t shares;
	uint32_t shares_last_hw;
	uint32_t hw_errors;
	uint32_t shares_to_good;
	uint32_t timeout_fail;
	uint32_t success_more;
	struct timeval last_changed;
	struct timeval last_nonce;
	struct timeval first_work;
	bool death_stage_one;
	bool tried_two_byte_temp;
	bool one_byte_temp;
};
#endif

#define TAILBUFSIZ 64

#define tailsprintf(buf, bufsiz, fmt, ...) do { \
	char tmp13[TAILBUFSIZ]; \
	size_t len13, buflen = strlen(buf); \
	snprintf(tmp13, sizeof(tmp13), fmt, ##__VA_ARGS__); \
	len13 = strlen(tmp13); \
	if ((buflen + len13) >= bufsiz) \
		quit(1, "tailsprintf buffer overflow in %s %s line %d", __FILE__, __func__, __LINE__); \
	strcat(buf, tmp13); \
} while (0)

extern void get_datestamp(char *, size_t, struct timeval *);
extern void inc_hw_errors_n(struct thr_info *thr, int n);
extern void inc_hw_errors(struct thr_info *thr);
extern bool test_nonce(struct work *work, uint32_t nonce);
extern bool test_nonce_diff(struct work *work, uint32_t nonce, double diff);
extern double test_nonce_value(struct work *work, uint32_t nonce);
extern bool submit_tested_work(struct thr_info *thr, struct work *work);
extern bool submit_nonce(struct thr_info *thr, struct work *work, uint32_t nonce);
extern bool submit_noffset_nonce(struct thr_info *thr, struct work *work, uint32_t nonce,
			  int noffset);
extern int share_work_tdiff(struct cgpu_info *cgpu);
extern struct work *get_work(struct thr_info *thr, const int thr_id);
extern void __add_queued(struct cgpu_info *cgpu, struct work *work);
extern struct work *get_queued(struct cgpu_info *cgpu);
extern struct work *__get_queued(struct cgpu_info *cgpu);
extern void add_queued(struct cgpu_info *cgpu, struct work *work);
extern struct work *get_queue_work(struct thr_info *thr, struct cgpu_info *cgpu, int thr_id);
extern struct work *__find_work_bymidstate(struct work *que, char *midstate, size_t midstatelen, char *data, int offset, size_t datalen);
extern struct work *find_queued_work_bymidstate(struct cgpu_info *cgpu, char *midstate, size_t midstatelen, char *data, int offset, size_t datalen);
extern struct work *clone_queued_work_bymidstate(struct cgpu_info *cgpu, char *midstate, size_t midstatelen, char *data, int offset, size_t datalen);
extern struct work *__find_work_byid(struct work *que, uint32_t id);
extern struct work *find_queued_work_byid(struct cgpu_info *cgpu, uint32_t id);
extern struct work *clone_queued_work_byid(struct cgpu_info *cgpu, uint32_t id);
extern void __work_completed(struct cgpu_info *cgpu, struct work *work);
extern int age_queued_work(struct cgpu_info *cgpu, double secs);
extern void work_completed(struct cgpu_info *cgpu, struct work *work);
extern struct work *take_queued_work_bymidstate(struct cgpu_info *cgpu, char *midstate, size_t midstatelen, char *data, int offset, size_t datalen);
extern void flush_queue(struct cgpu_info *cgpu);
extern void hash_driver_work(struct thr_info *mythr);
extern void hash_queued_work(struct thr_info *mythr);
extern void _wlog(const char *str);
extern void _wlogprint(const char *str);
extern int curses_int(const char *query);
extern char *curses_input(const char *query);
extern void kill_work(void);
extern void switch_pools(struct pool *selected);
extern void _discard_work(struct work **workptr, const char *file, const char *func, const int line);
#define discard_work(WORK) _discard_work(&(WORK), __FILE__, __func__, __LINE__)
extern void remove_pool(struct pool *pool);
extern void write_config(FILE *fcfg);
extern void zero_bestshare(void);
extern void zero_stats(void);
extern void default_save_file(char *filename);
extern bool log_curses_only(int prio, const char *datetime, const char *str);
extern void clear_logwin(void);
extern void logwin_update(void);
extern bool pool_tclear(struct pool *pool, bool *var);
extern void stratum_resumed(struct pool *pool);
extern void pool_died(struct pool *pool);
extern struct thread_q *tq_new(void);
extern void tq_free(struct thread_q *tq);
extern bool tq_push(struct thread_q *tq, void *data);
#if defined (USE_AVALON2) || defined (USE_AVALON4) || defined (USE_AVALON7) || defined (USE_AVALON8) || defined (USE_AVALON9) || defined (USE_AVALON_MINER) || defined (USE_HASHRATIO)
extern void *tq_pop(struct thread_q *tq, const struct timespec *abstime);
#else
extern void *tq_pop(struct thread_q *tq);
#endif
extern void tq_freeze(struct thread_q *tq);
extern void tq_thaw(struct thread_q *tq);
extern bool successful_connect;
extern void adl(void);
extern void app_restart(void);
extern void roll_work(struct work *work);
extern void roll_work_ntime(struct work *work, int noffset);
extern struct work *make_clone(struct work *work);
extern void clean_work(struct work *work);
extern void _free_work(struct work **workptr, const char *file, const char *func, const int line);
#define free_work(WORK) _free_work(&(WORK), __FILE__, __func__, __LINE__)
extern void set_work_ntime(struct work *work, int ntime);
extern struct work *copy_work_noffset(struct work *base_work, int noffset);
#define copy_work(work_in) copy_work_noffset(work_in, 0)
extern uint64_t share_diff(const struct work *work);
extern struct thr_info *get_thread(int thr_id);
extern struct cgpu_info *get_a_device(int id);

enum api_data_type {
	API_ESCAPE,
	API_STRING,
	API_CONST,
	API_UINT8,
	API_INT16,
	API_UINT16,
	API_INT,
	API_UINT,
	API_UINT32,
	API_HEX32,
	API_UINT64,
	API_INT64,
	API_DOUBLE,
	API_FLOAT,
	API_ELAPSED,
	API_BOOL,
	API_TIMEVAL,
	API_TIME,
	API_MHS,
	API_MHTOTAL,
	API_TEMP,
	API_UTILITY,
	API_FREQ,
	API_VOLTS,
	API_HS,
	API_DIFF,
	API_PERCENT,
	API_AVG
};

struct api_data {
	enum api_data_type type;
	char *name;
	void *data;
	bool data_was_malloc;
	struct api_data *prev;
	struct api_data *next;
};

extern struct api_data *api_add_escape(struct api_data *root, char *name, char *data, bool copy_data);
extern struct api_data *api_add_string(struct api_data *root, char *name, char *data, bool copy_data);
extern struct api_data *api_add_const(struct api_data *root, char *name, const char *data, bool copy_data);
extern struct api_data *api_add_uint8(struct api_data *root, char *name, uint8_t *data, bool copy_data);
extern struct api_data *api_add_int16(struct api_data *root, char *name, uint16_t *data, bool copy_data);
extern struct api_data *api_add_uint16(struct api_data *root, char *name, uint16_t *data, bool copy_data);
extern struct api_data *api_add_int(struct api_data *root, char *name, int *data, bool copy_data);
extern struct api_data *api_add_uint(struct api_data *root, char *name, unsigned int *data, bool copy_data);
extern struct api_data *api_add_uint32(struct api_data *root, char *name, uint32_t *data, bool copy_data);
extern struct api_data *api_add_hex32(struct api_data *root, char *name, uint32_t *data, bool copy_data);
extern struct api_data *api_add_uint64(struct api_data *root, char *name, uint64_t *data, bool copy_data);
extern struct api_data *api_add_int64(struct api_data *root, char *name, int64_t *data, bool copy_data);
extern struct api_data *api_add_double(struct api_data *root, char *name, double *data, bool copy_data);
extern struct api_data *api_add_float(struct api_data *root, char *name, float *data, bool copy_data);
extern struct api_data *api_add_elapsed(struct api_data *root, char *name, double *data, bool copy_data);
extern struct api_data *api_add_bool(struct api_data *root, char *name, bool *data, bool copy_data);
extern struct api_data *api_add_timeval(struct api_data *root, char *name, struct timeval *data, bool copy_data);
extern struct api_data *api_add_time(struct api_data *root, char *name, time_t *data, bool copy_data);
extern struct api_data *api_add_mhs(struct api_data *root, char *name, double *data, bool copy_data);
extern struct api_data *api_add_mhstotal(struct api_data *root, char *name, double *data, bool copy_data);
extern struct api_data *api_add_temp(struct api_data *root, char *name, float *data, bool copy_data);
extern struct api_data *api_add_utility(struct api_data *root, char *name, double *data, bool copy_data);
extern struct api_data *api_add_freq(struct api_data *root, char *name, double *data, bool copy_data);
extern struct api_data *api_add_volts(struct api_data *root, char *name, float *data, bool copy_data);
extern struct api_data *api_add_hs(struct api_data *root, char *name, double *data, bool copy_data);
extern struct api_data *api_add_diff(struct api_data *root, char *name, double *data, bool copy_data);
extern struct api_data *api_add_percent(struct api_data *root, char *name, double *data, bool copy_data);
extern struct api_data *api_add_avg(struct api_data *root, char *name, float *data, bool copy_data);

#define ROOT_ADD_API(FUNC, NAME, VAR, BOOL) root = api_add_##FUNC(root, (NAME), &(VAR), (BOOL))

extern void dupalloc(struct cgpu_info *cgpu, int timelimit);
extern void dupcounters(struct cgpu_info *cgpu, uint64_t *checked, uint64_t *dups);
extern bool isdupnonce(struct cgpu_info *cgpu, struct work *work, uint32_t nonce);

#endif /* __MINER_H__ */
