#include <types.h>
#include <stddef.h>
#include "sys/time.h"
#include "matrix/matrix.h"
#include "matrix/const.h"
#include "hal/isr.h"
#include "hal/hal.h"
#include "hal/cpu.h"
#include "clock.h"
#include "timer.h"
#include "proc/process.h"
#include "proc/sched.h"
#include "matrix/debug.h"
#include "div64.h"
#include "pit.h"

#define LEAPYEAR(y)	(((y) % 4) == 0 && (((y) % 100) != 0 || ((y) % 400) == 0))
#define DAYS(y)		(LEAPYEAR(y) ? 366 : 365)

extern boolean_t tmrs_exptimers(struct list *head, useconds_t now);

static int _current_frequency = 0;

uint32_t _lost_ticks = 0;
clock_t _real_time = 0;
useconds_t _boot_time;

static struct irq_hook _clock_hook;

/* Table containing number of days before a month */
static int _days_before_month[] = {
	0,
	0,
	31,
	31 + 28,
	31 + 28 + 31,
	31 + 28 + 31 + 30,
	31 + 28 + 31 + 30 + 31,
	31 + 28 + 31 + 30 + 31 + 30,
	31 + 28 + 31 + 30 + 31 + 30 + 31,
	31 + 28 + 31 + 30 + 31 + 30 + 31 + 31,
	31 + 28 + 31 + 30 + 31 + 30 + 31 + 31 + 30,
	31 + 28 + 31 + 30 + 31 + 30 + 31 + 31 + 30 + 31,
	31 + 28 + 31 + 30 + 31 + 30 + 31 + 31 + 30 + 31 + 30,
};

useconds_t time_to_unix(uint32_t year, uint32_t mon, uint32_t day,
			uint32_t hour, uint32_t min, uint32_t sec)
{
	uint32_t seconds = 0;
	uint32_t i;

	seconds += sec;
	seconds += min * 60;
	seconds += hour * 60 * 60;
	seconds += (day - 1) * 24 * 60 * 60;

	/* Convert the month into seconds */
	seconds += _days_before_month[mon] * 24 * 60 * 60;

	/* If this is a leap year and we've past February, we need to
	 * add another day.
	 */
	if (mon > 2 && LEAPYEAR(year)) {
		seconds += 24 * 60 * 60;
	}

	/* Add the days in each year before this year from 1970 */
	for (i = 1970; i < year; i++) {
		seconds += DAYS(i) * 24 * 60 * 60;
	}

	return SECS2USECS(seconds);
}

useconds_t sys_time()
{
	useconds_t value;

	value = (x86_rdtsc() - CURR_CPU->arch.sys_time_offset);
	
	do_div(value, CURR_CPU->arch.cycles_per_us);

	return value;
}

static void do_clocktick()
{
	useconds_t now;
	boolean_t prempt = FALSE;

	if (!CURR_CPU->timer_enabled) {
		return;
	}

	now = sys_time();
	
	prempt = tmrs_exptimers(&CURR_CPU->timers, now);
	if (prempt) {
		sched_reschedule(FALSE);
	}
}

static void clock_callback(struct registers *regs)
{
	uint32_t ticks;

	/* Get number of ticks and update realtime. */
	ticks = _lost_ticks + 1;
	_lost_ticks = 0;
	_real_time += ticks;

	do_clocktick();
}

void tsc_init_target()
{
	/* Calculate the offset to subtract from the TSC when calculating the
	 * system time. For the boot CPU, this is the current value of the TSC.
	 */
	if (CURR_CPU == &_boot_cpu) {
		CURR_CPU->arch.sys_time_offset = x86_rdtsc();
	} else {
		ASSERT(FALSE);
	}
}

void init_clock()
{
	uint8_t low;
	uint8_t high;
	uint32_t divisor;

	/* Save the frequency */
	_current_frequency = HZ;
	
	/* Register our timer callback first */
	register_irq_handler(IRQ0, &_clock_hook, &clock_callback);

	/* The value we send to the PIT is the value to divide. it's input
	 * clock (1193182 Hz) by, to get our required frequency. Important
	 * note that the divisor must be small enough to fit into 16-bits.
	 */
	divisor = PIT_BASE_FREQ / HZ;

	/* Send the command byte */
	outportb(0x43, 0x36);

	/* Divisor has to be sent byte-wise, so split here into upper/lower bytes */
	low = (uint8_t)(divisor & 0xFF);
	high = (uint8_t)((divisor >> 8) & 0xFF);

	outportb(0x40, low);
	outportb(0x40, high);

	_boot_time = platform_time_from_cmos() - sys_time();
}

void stop_clock()
{
	outportb(0x43, 0x36);
	outportb(0x40, 0);
	outportb(0x40, 0);
}

static uint32_t msec_2_ticks(uint32_t msec)
{
	uint32_t ticks;

	ticks = (msec * 12) / 10;

	return ticks;
}

void pit_delay(uint32_t msec)
{
	uint32_t when;

	when = _real_time + msec_2_ticks(msec);
	
	DEBUG(DL_DBG, ("msec(%d), _real_time(%d), when(%d)\n",
		       msec, _real_time, when));
	
	while (TRUE) {
		if (_real_time >= when) {
			break;
		}
		cpu_idle();
	}
}
