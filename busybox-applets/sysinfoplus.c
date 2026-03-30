/* vi: set sw=4 ts=4: */
/*
 * sysinfoplus - Enhanced System Information Display
 *
 * Copyright (C) 2026 Alpine Linux OS Case Study
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */

//config:config SYSINFOPLUS
//config:	bool "sysinfoplus (2.5 kb)"
//config:	default y
//config:	help
//config:	Display enhanced system information including CPU usage,
//config:	memory usage, uptime, and process count. A custom applet
//config:	created for the Alpine Linux OS Case Study.

//applet:IF_SYSINFOPLUS(APPLET(sysinfoplus, BB_DIR_USR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_SYSINFOPLUS) += sysinfoplus.o

//usage:#define sysinfoplus_trivial_usage
//usage:       ""
//usage:#define sysinfoplus_full_usage "\n\n"
//usage:       "Display enhanced system information\n"
//usage:     "\nShows CPU usage, memory usage, total/used memory,"
//usage:     "\nsystem uptime, and running process count."

#include "libbb.h"
#include <sys/sysinfo.h>
#include <dirent.h>

/* Read a value from /proc/meminfo by label */
static unsigned long read_meminfo(const char *label)
{
	FILE *fp;
	char buf[256];
	unsigned long val = 0;
	size_t label_len = strlen(label);

	fp = fopen("/proc/meminfo", "r");
	if (!fp)
		return 0;

	while (fgets(buf, sizeof(buf), fp)) {
		if (strncmp(buf, label, label_len) == 0) {
			sscanf(buf + label_len, " %lu", &val);
			break;
		}
	}
	fclose(fp);
	return val;
}

/* Get CPU usage percentage by sampling /proc/stat twice */
static int get_cpu_usage(void)
{
	FILE *fp;
	char buf[256];
	unsigned long long user1, nice1, sys1, idle1;
	unsigned long long user2, nice2, sys2, idle2;
	unsigned long long total1, total2, idle_diff, total_diff;

	fp = fopen("/proc/stat", "r");
	if (!fp)
		return 0;
	if (!fgets(buf, sizeof(buf), fp)) {
		fclose(fp);
		return 0;
	}
	fclose(fp);
	sscanf(buf, "cpu %llu %llu %llu %llu", &user1, &nice1, &sys1, &idle1);

	/* Sleep briefly to get a second sample */
	usleep(250000);

	fp = fopen("/proc/stat", "r");
	if (!fp)
		return 0;
	if (!fgets(buf, sizeof(buf), fp)) {
		fclose(fp);
		return 0;
	}
	fclose(fp);
	sscanf(buf, "cpu %llu %llu %llu %llu", &user2, &nice2, &sys2, &idle2);

	total1 = user1 + nice1 + sys1 + idle1;
	total2 = user2 + nice2 + sys2 + idle2;
	total_diff = total2 - total1;
	idle_diff = idle2 - idle1;

	if (total_diff == 0)
		return 0;

	return (int)((total_diff - idle_diff) * 100 / total_diff);
}

/* Count running processes from /proc */
static int count_processes(void)
{
	DIR *dir;
	struct dirent *entry;
	int count = 0;

	dir = opendir("/proc");
	if (!dir)
		return 0;

	while ((entry = readdir(dir)) != NULL) {
		/* Count directories that are numeric (PIDs) */
		if (entry->d_type == DT_DIR && entry->d_name[0] >= '0' && entry->d_name[0] <= '9') {
			count++;
		}
	}
	closedir(dir);
	return count;
}

/* Format memory value (kB) into human-readable string */
static void format_memory(unsigned long kb, char *out, size_t out_size)
{
	if (kb >= 1048576) {
		snprintf(out, out_size, "%.1f GB", (double)kb / 1048576.0);
	} else {
		snprintf(out, out_size, "%lu MB", kb / 1024);
	}
}

int sysinfoplus_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int sysinfoplus_main(int argc UNUSED_PARAM, char **argv UNUSED_PARAM)
{
	struct sysinfo si;
	unsigned long mem_total_kb, mem_avail_kb, mem_used_kb;
	int cpu_usage, mem_percent, procs;
	long uptime_secs, days, hours, minutes;
	char total_str[32], used_str[32];

	/* ANSI color codes */
	const char *CYAN   = "\033[0;36m";
	const char *GREEN  = "\033[0;32m";
	const char *YELLOW = "\033[1;33m";
	const char *WHITE  = "\033[1;37m";
	const char *BOLD   = "\033[1m";
	const char *RESET  = "\033[0m";
	const char *LINE   = "------------------------------------------";

	/* Gather system info */
	cpu_usage = get_cpu_usage();

	mem_total_kb = read_meminfo("MemTotal:");
	mem_avail_kb = read_meminfo("MemAvailable:");
	mem_used_kb = mem_total_kb - mem_avail_kb;
	mem_percent = (mem_total_kb > 0) ? (int)(mem_used_kb * 100 / mem_total_kb) : 0;

	format_memory(mem_total_kb, total_str, sizeof(total_str));
	format_memory(mem_used_kb, used_str, sizeof(used_str));

	/* Get uptime */
	if (sysinfo(&si) == 0) {
		uptime_secs = si.uptime;
	} else {
		uptime_secs = 0;
	}
	days = uptime_secs / 86400;
	hours = (uptime_secs % 86400) / 3600;
	minutes = (uptime_secs % 3600) / 60;

	procs = count_processes();

	/* Display */
	printf("\n");
	printf("  %s%s╔══════════════════════════════════════╗%s\n", BOLD, CYAN, RESET);
	printf("  %s%s║      %s⚙  System Information  ⚙%s       ║%s\n", BOLD, CYAN, WHITE, CYAN, RESET);
	printf("  %s%s╚══════════════════════════════════════╝%s\n", BOLD, CYAN, RESET);
	printf("  %s\n", LINE);
	printf("  %sCPU Usage%s      :  %s%d%%%s\n", GREEN, RESET, YELLOW, cpu_usage, RESET);
	printf("  %sMemory Usage%s   :  %s%d%%%s\n", GREEN, RESET, YELLOW, mem_percent, RESET);
	printf("  %sTotal Memory%s   :  %s%s%s\n", GREEN, RESET, WHITE, total_str, RESET);
	printf("  %sUsed Memory%s    :  %s%s%s\n", GREEN, RESET, WHITE, used_str, RESET);
	printf("  %sUptime%s         :  %s", GREEN, RESET, WHITE);
	if (days > 0)
		printf("%ld day(s) ", days);
	if (hours > 0)
		printf("%ld hour(s) ", hours);
	printf("%ld minute(s)%s\n", minutes, RESET);
	printf("  %sProcesses%s      :  %s%d%s\n", GREEN, RESET, WHITE, procs, RESET);
	printf("  %s\n", LINE);
	printf("\n");

	return 0;
}
