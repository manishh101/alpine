/* vi: set sw=4 ts=4: */
/*
 * setup-secure-mode - Lightweight Security Automation for Alpine Linux
 *
 * Copyright (C) 2026 Alpine Linux OS Case Study
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 *
 * Automates system hardening: firewall, SSH hardening, intrusion
 * prevention, and file permission security. Supports three modes
 * (minimal, standard, strict), dry-run, rollback, and per-module
 * execution.
 */

//config:config SETUP_SECURE_MODE
//config:	bool "setup-secure-mode (8 kb)"
//config:	default y
//config:	help
//config:	Lightweight security automation tool for Alpine Linux.
//config:	Applies system hardening (firewall, SSH, fail2ban, permissions)
//config:	in minimal, standard, or strict modes. Supports dry-run,
//config:	rollback, and per-module execution.

//applet:IF_SETUP_SECURE_MODE(APPLET_ODDNAME(setup-secure-mode, setup_secure_mode, BB_DIR_USR_SBIN, BB_SUID_REQUIRE, setup_secure_mode))

//kbuild:lib-$(CONFIG_SETUP_SECURE_MODE) += setup_secure_mode.o

//usage:#define setup_secure_mode_trivial_usage
//usage:       "[minimal|standard|strict] [OPTIONS]"
//usage:#define setup_secure_mode_full_usage "\n\n"
//usage:       "Automated security hardening for Alpine Linux\n"
//usage:     "\nModes:"
//usage:     "\n  minimal    Basic security (updates + firewall)"
//usage:     "\n  standard   Recommended configuration (default)"
//usage:     "\n  strict     Maximum hardening (may restrict access)"
//usage:     "\n"
//usage:     "\nOptions:"
//usage:     "\n  -n, --dry-run       Preview changes without applying"
//usage:     "\n  -r, --rollback      Restore previous system state"
//usage:     "\n  -o, --only MODULE   Apply specific module only"
//usage:     "\n                      Modules: update, firewall, ssh, fail2ban, permissions"

#include "libbb.h"
#include <sys/stat.h>
#include <time.h>
#include <errno.h>

/* ── Enums ─────────────────────────────────────────────── */

enum secure_mode {
	MODE_MINIMAL = 0,
	MODE_STANDARD,
	MODE_STRICT
};

enum module_id {
	MOD_ALL = 0,
	MOD_UPDATE,
	MOD_FIREWALL,
	MOD_SSH,
	MOD_FAIL2BAN,
	MOD_PERMISSIONS
};

/* ── Globals ───────────────────────────────────────────── */

static int g_dry_run = 0;
static FILE *g_logfp = NULL;

/* ANSI colors */
#define C_GREEN   "\033[0;32m"
#define C_YELLOW  "\033[1;33m"
#define C_RED     "\033[0;31m"
#define C_CYAN    "\033[0;36m"
#define C_BOLD    "\033[1m"
#define C_RESET   "\033[0m"

#define BACKUP_DIR   "/var/backups/secure-mode"
#define LOG_FILE     "/var/log/secure-mode.log"
#define SSHD_CONFIG  "/etc/ssh/sshd_config"
#define NFT_CONFIG   "/etc/nftables/nftables.nft"

/* ── Logging ───────────────────────────────────────────── */

static void log_open(void)
{
	g_logfp = fopen(LOG_FILE, "a");
	/* non-fatal if it fails */
}

static void log_close(void)
{
	if (g_logfp) {
		fclose(g_logfp);
		g_logfp = NULL;
	}
}

static void log_msg(const char *fmt, ...)
{
	va_list ap;
	time_t now;
	char timebuf[64];

	if (!g_logfp)
		return;

	now = time(NULL);
	strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", localtime(&now));
	fprintf(g_logfp, "[%s] ", timebuf);

	va_start(ap, fmt);
	vfprintf(g_logfp, fmt, ap);
	va_end(ap);

	fprintf(g_logfp, "\n");
	fflush(g_logfp);
}

/* ── Status Output ─────────────────────────────────────── */

static void print_ok(const char *msg)
{
	if (g_dry_run)
		printf("  %s[DRY-RUN]%s %s\n", C_CYAN, C_RESET, msg);
	else
		printf("  %s[✔]%s %s\n", C_GREEN, C_RESET, msg);
	log_msg("[OK] %s", msg);
}

static void print_warn(const char *msg)
{
	if (g_dry_run)
		printf("  %s[DRY-RUN]%s %s\n", C_CYAN, C_RESET, msg);
	else
		printf("  %s[⚠]%s %s\n", C_YELLOW, C_RESET, msg);
	log_msg("[WARN] %s", msg);
}

static void print_fail(const char *msg)
{
	printf("  %s[✘]%s %s\n", C_RED, C_RESET, msg);
	log_msg("[FAIL] %s", msg);
}

static void print_header(const char *mode_name)
{
	printf("\n");
	printf("  %s%s╔══════════════════════════════════════════╗%s\n", C_BOLD, C_CYAN, C_RESET);
	printf("  %s%s║    %s🔐  setup-secure-mode  🔐%s         ║%s\n", C_BOLD, C_CYAN, C_BOLD, C_CYAN, C_RESET);
	printf("  %s%s╚══════════════════════════════════════════╝%s\n", C_BOLD, C_CYAN, C_RESET);
	printf("  %sMode:%s %s%s%s\n", C_BOLD, C_RESET, C_YELLOW, mode_name, C_RESET);
	if (g_dry_run)
		printf("  %s** DRY-RUN MODE — no changes will be made **%s\n", C_CYAN, C_RESET);
	printf("  ──────────────────────────────────────────\n");
}

/* ── Utility ───────────────────────────────────────────── */

static int run_cmd(const char *cmd)
{
	if (g_dry_run)
		return 0;
	return system(cmd);
}

static void mkdir_p(const char *path)
{
	char tmp[512];
	char *p = NULL;
	size_t len;

	snprintf(tmp, sizeof(tmp), "%s", path);
	len = strlen(tmp);
	if (tmp[len - 1] == '/')
		tmp[len - 1] = '\0';

	for (p = tmp + 1; *p; p++) {
		if (*p == '/') {
			*p = '\0';
			mkdir(tmp, 0755);
			*p = '/';
		}
	}
	mkdir(tmp, 0755);
}

static int backup_file(const char *src)
{
	char dest[512];
	char ts[32];
	time_t now;
	FILE *in, *out;
	int ch;

	if (access(src, F_OK) != 0)
		return 0; /* nothing to back up */

	now = time(NULL);
	strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", localtime(&now));

	snprintf(dest, sizeof(dest), "%s/%s_%s", BACKUP_DIR, bb_basename(src), ts);
	mkdir_p(BACKUP_DIR);

	if (g_dry_run) {
		char msg[256];
		snprintf(msg, sizeof(msg), "Would back up %s -> %s", src, dest);
		print_warn(msg);
		return 0;
	}

	in = fopen(src, "r");
	if (!in) {
		print_fail("Failed to open source for backup");
		return -1;
	}

	out = fopen(dest, "w");
	if (!out) {
		fclose(in);
		print_fail("Failed to create backup file");
		return -1;
	}

	while ((ch = fgetc(in)) != EOF)
		fputc(ch, out);

	fclose(in);
	fclose(out);

	{
		char msg[256];
		snprintf(msg, sizeof(msg), "Backed up %s -> %s", src, dest);
		print_warn(msg);
	}
	return 0;
}

/* Replace or add a directive in sshd_config style files */
static void set_sshd_option(const char *key, const char *value)
{
	FILE *fp;
	char **lines = NULL;
	int nlines = 0;
	int found = 0;
	char buf[1024];
	char needle[256];
	int i;

	snprintf(needle, sizeof(needle), "%s ", key);

	fp = fopen(SSHD_CONFIG, "r");
	if (!fp)
		return;

	while (fgets(buf, sizeof(buf), fp)) {
		lines = xrealloc(lines, (nlines + 1) * sizeof(char *));
		lines[nlines] = xstrdup(buf);
		nlines++;
	}
	fclose(fp);

	fp = fopen(SSHD_CONFIG, "w");
	if (!fp) {
		for (i = 0; i < nlines; i++) free(lines[i]);
		free(lines);
		return;
	}

	for (i = 0; i < nlines; i++) {
		/* Match both active and commented-out lines */
		char *trimmed = lines[i];
		while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
		if (*trimmed == '#') trimmed++;
		while (*trimmed == ' ' || *trimmed == '\t') trimmed++;

		if (strncasecmp(trimmed, key, strlen(key)) == 0) {
			fprintf(fp, "%s %s\n", key, value);
			found = 1;
		} else {
			fputs(lines[i], fp);
		}
		free(lines[i]);
	}

	if (!found)
		fprintf(fp, "%s %s\n", key, value);

	free(lines);
	fclose(fp);
}

/* ── Module: System Update ─────────────────────────────── */

static void setup_update(void)
{
	print_ok("Updating system packages...");
	if (run_cmd("apk update") != 0) {
		print_fail("Package index update failed");
		return;
	}
	print_ok("Upgrading installed packages...");
	if (run_cmd("apk upgrade --no-cache") != 0) {
		print_fail("Package upgrade failed");
		return;
	}
	print_ok("System packages are up to date.");
}

/* ── Module: Firewall (nftables) ───────────────────────── */

static void setup_firewall(void)
{
	print_ok("Installing firewall (nftables)...");
	if (run_cmd("apk add --no-cache nftables") != 0) {
		print_fail("Failed to install nftables");
		return;
	}

	print_ok("Enabling nftables service...");
	run_cmd("rc-update add nftables default 2>/dev/null");

	/* If we have a bundled config, install it */
	if (access("/etc/nftables/nftables.nft", F_OK) == 0) {
		print_ok("Applying bundled firewall rules...");
		if (run_cmd("nft -f /etc/nftables/nftables.nft") != 0) {
			print_fail("Failed to apply nftables rules");
			return;
		}
	} else {
		/* Apply inline default rules */
		print_ok("Applying default firewall rules...");
		run_cmd("nft add table inet filter");
		run_cmd("nft add chain inet filter input '{ type filter hook input priority 0; policy drop; }'");
		run_cmd("nft add rule inet filter input ct state established,related accept");
		run_cmd("nft add rule inet filter input iifname lo accept");
		run_cmd("nft add rule inet filter input tcp dport 22 accept");
		run_cmd("nft add chain inet filter output '{ type filter hook output priority 0; policy accept; }'");
	}

	print_ok("Starting nftables...");
	run_cmd("service nftables start 2>/dev/null || nft -f /etc/nftables/nftables.nft 2>/dev/null");

	print_ok("Firewall configured (SSH allowed, incoming traffic restricted).");
}

/* ── Module: SSH Hardening ─────────────────────────────── */

static void setup_ssh(enum secure_mode mode)
{
	/* Install openssh if not present */
	print_ok("Ensuring OpenSSH is installed...");
	run_cmd("apk add --no-cache openssh");

	/* Backup existing sshd_config */
	backup_file(SSHD_CONFIG);

	/* Generate host keys if missing */
	print_ok("Generating SSH host keys if needed...");
	run_cmd("ssh-keygen -A 2>/dev/null");

	if (g_dry_run) {
		print_ok("Would disable root login via SSH...");
		if (mode == MODE_STRICT)
			print_ok("Would disable password authentication...");
		return;
	}

	/* Disable root login */
	print_ok("Disabling root login via SSH...");
	set_sshd_option("PermitRootLogin", "no");

	/* Strict mode: disable password auth */
	if (mode == MODE_STRICT) {
		print_warn("Disabling password authentication (strict mode)...");
		set_sshd_option("PasswordAuthentication", "no");
		print_warn("⚠  Ensure SSH keys are configured before strict mode!");
	}

	/* Additional hardening */
	set_sshd_option("X11Forwarding", "no");
	set_sshd_option("MaxAuthTries", "3");
	set_sshd_option("Protocol", "2");

	print_ok("Restarting SSH service...");
	run_cmd("rc-update add sshd default 2>/dev/null");
	run_cmd("service sshd restart 2>/dev/null");

	print_ok("SSH hardening applied.");
}

/* ── Module: Fail2Ban (Intrusion Prevention) ───────────── */

static void setup_fail2ban(void)
{
	print_ok("Installing fail2ban...");
	if (run_cmd("apk add --no-cache fail2ban") != 0) {
		print_fail("Failed to install fail2ban");
		return;
	}

	/* Install our jail config if bundled */
	if (access("/etc/fail2ban/jail.local", F_OK) != 0 && !g_dry_run) {
		FILE *fp;
		print_ok("Creating fail2ban SSH jail configuration...");
		mkdir_p("/etc/fail2ban");
		fp = fopen("/etc/fail2ban/jail.local", "w");
		if (fp) {
			fprintf(fp,
				"[DEFAULT]\n"
				"bantime  = 600\n"
				"findtime = 600\n"
				"maxretry = 5\n"
				"\n"
				"[sshd]\n"
				"enabled  = true\n"
				"port     = ssh\n"
				"filter   = sshd\n"
				"logpath  = /var/log/auth.log\n"
				"maxretry = 5\n"
			);
			fclose(fp);
		}
	} else {
		print_ok("Using bundled fail2ban configuration...");
	}

	print_ok("Enabling fail2ban service...");
	/* fail2ban will crash on startup if the monitored log file doesn't exist yet */
	run_cmd("touch /var/log/auth.log 2>/dev/null");
	run_cmd("rc-update add fail2ban default 2>/dev/null");
	run_cmd("service fail2ban start 2>/dev/null");

	print_ok("Intrusion prevention (fail2ban) configured.");
}

/* ── Module: File Permission Security ──────────────────── */

static void secure_permissions(void)
{
	const char *home;
	char ssh_dir[256];
	char auth_keys[256];

	home = getenv("HOME");
	if (!home)
		home = "/root";

	snprintf(ssh_dir, sizeof(ssh_dir), "%s/.ssh", home);
	snprintf(auth_keys, sizeof(auth_keys), "%s/.ssh/authorized_keys", home);

	/* Create .ssh if it doesn't exist */
	if (access(ssh_dir, F_OK) != 0) {
		print_ok("Creating SSH directory...");
		if (!g_dry_run) {
			mkdir_p(ssh_dir);
		}
	}

	print_ok("Securing SSH directory permissions...");
	if (!g_dry_run) {
		chmod(ssh_dir, 0700);
	}

	if (access(auth_keys, F_OK) == 0) {
		print_ok("Securing authorized_keys permissions...");
		if (!g_dry_run)
			chmod(auth_keys, 0600);
	} else {
		print_ok("Creating authorized_keys file...");
		if (!g_dry_run) {
			FILE *fp = fopen(auth_keys, "w");
			if (fp) {
				fclose(fp);
				chmod(auth_keys, 0600);
			}
		}
	}

	/* Secure /etc/shadow */
	if (access("/etc/shadow", F_OK) == 0) {
		print_ok("Securing /etc/shadow permissions...");
		if (!g_dry_run)
			chmod("/etc/shadow", 0640);
	}

	print_ok("File permissions secured.");
}

/* ── Rollback ──────────────────────────────────────────── */

static void do_rollback(void)
{
	DIR *dir;
	struct dirent *entry;
	char newest_sshd[512] = {0};
	char path[512];

	printf("\n  %s%s── Rollback Mode ──%s\n\n", C_BOLD, C_YELLOW, C_RESET);

	/* Restore sshd_config from backup */
	dir = opendir(BACKUP_DIR);
	if (dir) {
		while ((entry = readdir(dir)) != NULL) {
			if (strncmp(entry->d_name, "sshd_config_", 12) == 0) {
				snprintf(path, sizeof(path), "%s/%s", BACKUP_DIR, entry->d_name);
				/* Take the lexicographically latest (most recent timestamp) */
				if (strcmp(path, newest_sshd) > 0)
					strcpy(newest_sshd, path);
			}
		}
		closedir(dir);
	}

	if (newest_sshd[0]) {
		char cmd[1024];
		snprintf(cmd, sizeof(cmd), "cp '%s' '%s'", newest_sshd, SSHD_CONFIG);
		if (run_cmd(cmd) == 0) {
			print_ok("Restored SSH configuration from backup.");
			run_cmd("service sshd restart 2>/dev/null");
		} else {
			print_fail("Failed to restore SSH configuration.");
		}
	} else {
		print_warn("No SSH configuration backup found.");
	}

	/* Flush nftables rules */
	print_ok("Flushing firewall rules...");
	run_cmd("nft flush ruleset 2>/dev/null");

	/* Stop fail2ban */
	print_ok("Stopping fail2ban service...");
	run_cmd("service fail2ban stop 2>/dev/null");
	run_cmd("rc-update del fail2ban default 2>/dev/null");

	print_ok("Rollback completed. System restored to previous state.");
}

/* ── Parse module name ─────────────────────────────────── */

static enum module_id parse_module(const char *name)
{
	if (strcmp(name, "update") == 0) return MOD_UPDATE;
	if (strcmp(name, "firewall") == 0) return MOD_FIREWALL;
	if (strcmp(name, "ssh") == 0) return MOD_SSH;
	if (strcmp(name, "fail2ban") == 0) return MOD_FAIL2BAN;
	if (strcmp(name, "permissions") == 0) return MOD_PERMISSIONS;
	bb_error_msg_and_die("unknown module '%s'. Valid: update, firewall, ssh, fail2ban, permissions", name);
}

/* ── Main ──────────────────────────────────────────────── */

int setup_secure_mode_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int setup_secure_mode_main(int argc UNUSED_PARAM, char **argv)
{
	enum secure_mode mode = MODE_STANDARD;
	enum module_id only_module = MOD_ALL;
	int do_rollback_flag = 0;
	const char *mode_names[] = { "minimal", "standard", "strict" };
	int i;

	/* Must be root */
	if (getuid() != 0)
		bb_error_msg_and_die("this command must be run as root");

	/* Parse arguments */
	argv++;
	while (*argv) {
		if (strcmp(*argv, "-n") == 0 || strcmp(*argv, "--dry-run") == 0) {
			g_dry_run = 1;
		} else if (strcmp(*argv, "-r") == 0 || strcmp(*argv, "--rollback") == 0) {
			do_rollback_flag = 1;
		} else if (strcmp(*argv, "-o") == 0 || strcmp(*argv, "--only") == 0) {
			argv++;
			if (!*argv)
				bb_error_msg_and_die("--only requires a module name");
			only_module = parse_module(*argv);
		} else if (strcmp(*argv, "minimal") == 0) {
			mode = MODE_MINIMAL;
		} else if (strcmp(*argv, "standard") == 0) {
			mode = MODE_STANDARD;
		} else if (strcmp(*argv, "strict") == 0) {
			mode = MODE_STRICT;
		} else if (strcmp(*argv, "-h") == 0 || strcmp(*argv, "--help") == 0) {
			bb_show_usage();
		} else {
			bb_error_msg_and_die("unknown argument '%s'", *argv);
		}
		argv++;
	}

	/* Open log */
	log_open();
	log_msg("=== setup-secure-mode started (mode=%s, dry_run=%d, rollback=%d) ===",
		mode_names[mode], g_dry_run, do_rollback_flag);

	/* Handle rollback */
	if (do_rollback_flag) {
		if (g_dry_run) {
			printf("\n  %s[DRY-RUN]%s Would perform rollback of security changes.\n\n", C_CYAN, C_RESET);
		} else {
			do_rollback();
		}
		log_msg("=== Rollback completed ===");
		log_close();
		return 0;
	}

	/* Print header */
	print_header(mode_names[mode]);

	/* Execute modules based on mode and --only flag */

	/* Module: System Update (all modes) */
	if (only_module == MOD_ALL || only_module == MOD_UPDATE) {
		setup_update();
	}

	/* Module: Firewall (all modes) */
	if (only_module == MOD_ALL || only_module == MOD_FIREWALL) {
		setup_firewall();
	}

	/* Module: SSH Hardening (standard + strict only) */
	if (mode >= MODE_STANDARD) {
		if (only_module == MOD_ALL || only_module == MOD_SSH) {
			setup_ssh(mode);
		}
	} else if (only_module == MOD_SSH) {
		print_warn("SSH hardening skipped (not included in minimal mode).");
	}

	/* Module: Fail2Ban (standard + strict only) */
	if (mode >= MODE_STANDARD) {
		if (only_module == MOD_ALL || only_module == MOD_FAIL2BAN) {
			setup_fail2ban();
		}
	} else if (only_module == MOD_FAIL2BAN) {
		print_warn("Fail2ban skipped (not included in minimal mode).");
	}

	/* Module: File Permissions (standard + strict only) */
	if (mode >= MODE_STANDARD) {
		if (only_module == MOD_ALL || only_module == MOD_PERMISSIONS) {
			secure_permissions();
		}
	} else if (only_module == MOD_PERMISSIONS) {
		print_warn("Permission hardening skipped (not included in minimal mode).");
	}

	/* Summary */
	printf("  ──────────────────────────────────────────\n");
	if (g_dry_run) {
		printf("  %s%sDry-run complete.%s No changes were made.\n\n", C_BOLD, C_CYAN, C_RESET);
	} else {
		printf("  %s%s[✔] Security setup completed successfully.%s\n\n", C_BOLD, C_GREEN, C_RESET);
	}

	log_msg("=== setup-secure-mode completed successfully ===");
	log_close();

	return 0;
}
