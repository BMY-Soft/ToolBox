/*
 * Copyright 2008, The Android Open Source Project
 * Copyright 2015-2017 Rivoreo
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED 
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <termios.h>
#include <signal.h>
#include <sys/select.h>

struct cpu_info {
	unsigned long int utime, ntime, stime, itime;
	unsigned long int iowtime, irqtime, sirqtime;
};

#define PROC_NAME_LEN 64
#define THREAD_NAME_LEN 32
//#define POLICY_NAME_LEN 4

#define PRINT_BUF() \
	do { \
		if(use_tty) { \
			fprintf(stdout, "%-*.*s", sz.ws_col, sz.ws_col, buf); \
		} else { \
			fputs(buf, stdout); \
		} \
	} while(0)

struct proc_info {
	struct proc_info *next;
	pid_t pid;
	pid_t tid;
	uid_t uid;
	gid_t gid;
	char name[PROC_NAME_LEN];
	char tname[THREAD_NAME_LEN];
	char state;
	unsigned long int utime;
	unsigned long int stime;
	unsigned long int delta_utime;
	unsigned long int delta_stime;
	unsigned long int delta_time;
	unsigned long int vss;
	unsigned long int rss;
	int prs;
	int num_threads;
	//char policy[POLICY_NAME_LEN];
};

struct proc_list {
	struct proc_info **array;
	int size;
};

#define die(...) { fprintf(stderr, __VA_ARGS__); exit(EXIT_FAILURE); }

#define INIT_PROCS 50
#define THREAD_MULT 8
static struct proc_info **old_procs, **new_procs;
static int num_old_procs, num_new_procs;
static struct proc_info *free_procs;
static int num_used_procs, num_free_procs;

static int max_procs, iterations, threads;

static struct cpu_info old_cpu, new_cpu;

#ifndef _WIN32
/* windows size struct */
static struct winsize sz;
static int use_tty;
static struct termios orig_termios;
#endif

static struct proc_info *alloc_proc(void);
static void free_proc(struct proc_info *proc);
static void read_procs(void);
static int read_stat(const char *filename, struct proc_info *proc);
static void add_proc(int proc_num, struct proc_info *proc);
static int read_cmdline(const char *filename, struct proc_info *proc);
static int read_status(const char *filename, struct proc_info *proc);
static void print_procs(void);
static struct proc_info *find_old_proc(pid_t pid, pid_t tid);
static void free_old_procs(void);
static int (*proc_cmp)(const void *, const void *);
static int proc_cpu_cmp(const void *, const void *);
static int proc_vss_cmp(const void *, const void *);
static int proc_rss_cmp(const void *, const void *);
static int proc_thr_cmp(const void *, const void *);
static int numcmp(long long, long long);
static void usage(const char *);
static void SIGINT_handler(int);

static void restore_terminal() {
	if(!use_tty) return;
	//printf("\x1b[?47l\x1b[?25h");
	printf("\x1b[?25h");
	tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
}

int top_main(int argc, char *argv[]) {
	int i;
	int end_of_options;
	fd_set *fdset = NULL;
	int delay;
	struct timeval delay_tv;

	num_used_procs = num_free_procs = 0;

	max_procs = -1;
	delay = 3;
	iterations = -1;
	proc_cmp = &proc_cpu_cmp;
	use_tty = -1;
	end_of_options = 0;

	for (i = 1; i < argc; i++) {
		/* There is not the end of options? && Is an Option? */
		if(!end_of_options && argv[i][0] == '-') {
			const char *arg = argv[i] + 1;
			if(!arg[0]) {
				fprintf(stderr, "Expects an option.\n");
				usage(argv[0]);
				return EXIT_FAILURE;
			}
			while(arg[0]) {
				switch(arg[0]) {
					case 'b':
						use_tty = 0;
						break;
					case 'm':
						if(i + 1 >= argc) {
							fprintf(stderr, "Option -m expects an argument.\n");
							usage(argv[0]);
							return EXIT_FAILURE;
						}
						max_procs = atoi(argv[++i]);
						break;
					case 'n':
						if(i + 1 >= argc) {
							fprintf(stderr, "Option -n expects an argument.\n");
							usage(argv[0]);
							return EXIT_FAILURE;
						}
						iterations = atoi(argv[++i]);
						break;
					case 'd':
						if(i + 1 >= argc) {
							fprintf(stderr, "Option -d expects an argument.\n");
							usage(argv[0]);
							return EXIT_FAILURE;
						}
						delay = atoi(argv[++i]);
						break;
					case 's':
						if(i + 1 >= argc) {
							fprintf(stderr, "Option -s expects an argument.\n");
							usage(argv[0]);
							return EXIT_FAILURE;
						}
						i++;
						if(strcmp(argv[i], "cpu") == 0) { proc_cmp = &proc_cpu_cmp; }
						else if(strcmp(argv[i], "vss") == 0) { proc_cmp = &proc_vss_cmp; }
						else if(strcmp(argv[i], "rss") == 0) { proc_cmp = &proc_rss_cmp; }
						else if(strcmp(argv[i], "thr") == 0) { proc_cmp = &proc_thr_cmp; }
						else {
							fprintf(stderr, "Invalid argument \"%s\" for option -s.\n", argv[i]);
							return EXIT_FAILURE;
						}
						break;
					case 't':
						threads = 1;
						break;
					case 'h':
						usage(argv[0]);
						return EXIT_SUCCESS;
						break;
					case '-':
						if(!arg[1]) {
							end_of_options = 1;
							break;
						}
						// Fall
					default:
						fprintf(stderr, "Invalid argument \"%s\".\n", argv[i]);
						usage(argv[0]);
						return EXIT_FAILURE;
				}
				arg++;
			}
		}
	}

	if(threads && proc_cmp == &proc_thr_cmp) {
		fprintf(stderr, "%s: Sorting by threads per thread makes no sense!\n", argv[0]);
		return EXIT_FAILURE;
	}

	free_procs = NULL;

	num_new_procs = num_old_procs = 0;
	new_procs = old_procs = NULL;

	if(use_tty == -1) use_tty = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
	if(use_tty) {
		/* Test windows size */
		//sz=(struct winsize*)malloc(sizeof(struct winsize));
		//memset(sz,0x00,sizeof(struct winsize));
		if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &sz) == -1) {
			perror("Could not get terminal window size");
			return EXIT_FAILURE;
		}
		//fprintf(stdout, "Screen width: %i  Screen height: %i\n", sz.ws_col, sz.ws_row);
		//max_procs = sz.ws_row - 4;
		signal(SIGINT, SIGINT_handler);

		tcgetattr(STDIN_FILENO, &orig_termios);
		struct termios new_termios = orig_termios;
		new_termios.c_lflag &= ~(ICANON | ECHO);
		tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

		fdset = malloc(sizeof(fd_set));
	}

	read_procs();
	while(iterations == -1 || iterations-- > 0) {
		if(use_tty && fdset) {
			FD_ZERO(fdset);
			FD_SET(STDIN_FILENO, fdset);
		}
		delay_tv.tv_sec = delay;
		delay_tv.tv_usec = 0;
		old_procs = new_procs;
		num_old_procs = num_new_procs;
		memcpy(&old_cpu, &new_cpu, sizeof(old_cpu));
		//sleep(delay);
		switch(select(STDIN_FILENO + 1, fdset, NULL, NULL, &delay_tv)) {
			case -1:
				perror("select");
				return 1;
			case 0:
				break;
			default:
				if(use_tty) switch(getchar()) {
					case EOF:
					case 'q':
						iterations = 0;
						break;
				}
				break;
		}
		read_procs();
		print_procs();
		free_old_procs();
	}
	restore_terminal();
	return 0;
}

static struct proc_info *alloc_proc(void) {
	struct proc_info *proc;

	if (free_procs) {
		proc = free_procs;
		free_procs = free_procs->next;
		num_free_procs--;
	} else {
		proc = malloc(sizeof(*proc));
		if(!proc) die("Could not allocate struct process_info.\n");
	}

	num_used_procs++;

	return proc;
}

static void free_proc(struct proc_info *proc) {
	proc->next = free_procs;
	free_procs = proc;

	num_used_procs--;
	num_free_procs++;
}

#define MAX_LINE 256

static void read_procs(void) {
	DIR *proc_dir, *task_dir;
	struct dirent *pid_dir, *tid_dir;
	char filename[64];
	//FILE *file;
	int proc_num;
	struct proc_info *proc;
	pid_t pid, tid;

	int i;

	proc_dir = opendir("/proc");
	if(!proc_dir) die("Could not open /proc.\n");

	new_procs = calloc(INIT_PROCS * (threads ? THREAD_MULT : 1), sizeof(struct proc_info *));
	num_new_procs = INIT_PROCS * (threads ? THREAD_MULT : 1);
#ifdef __linux__
	FILE *file = fopen("/proc/stat", "r");
	if(!file) die("Could not open /proc/stat.\n");
	fscanf(file, "cpu  %lu %lu %lu %lu %lu %lu %lu", &new_cpu.utime, &new_cpu.ntime, &new_cpu.stime,
			&new_cpu.itime, &new_cpu.iowtime, &new_cpu.irqtime, &new_cpu.sirqtime);
	fclose(file);
#endif
	proc_num = 0;
	while((pid_dir = readdir(proc_dir))) {
		if(!isdigit(pid_dir->d_name[0])) continue;

		pid = atoi(pid_dir->d_name);

		struct proc_info cur_proc;

		if(!threads) {
			proc = alloc_proc();
			proc->pid = proc->tid = pid;

			sprintf(filename, "/proc/%d/stat", (int)pid);
			read_stat(filename, proc);

			sprintf(filename, "/proc/%d/cmdline", (int)pid);
			read_cmdline(filename, proc);

			sprintf(filename, "/proc/%d/status", (int)pid);
			read_status(filename, proc);

			//read_policy(pid, proc);

			proc->num_threads = 0;
		} else {
			sprintf(filename, "/proc/%d/cmdline", (int)pid);
			read_cmdline(filename, &cur_proc);

			sprintf(filename, "/proc/%d/status", (int)pid);
			read_status(filename, &cur_proc);

			proc = NULL;
		}

		sprintf(filename, "/proc/%d/task", (int)pid);
		task_dir = opendir(filename);
		if(!task_dir) continue;

		while((tid_dir = readdir(task_dir))) {
			if(!isdigit(tid_dir->d_name[0]))continue;

			if(threads) {
				tid = atoi(tid_dir->d_name);

				proc = alloc_proc();
				proc->pid = pid; proc->tid = tid;

				sprintf(filename, "/proc/%d/task/%d/stat", (int)pid, (int)tid);
				read_stat(filename, proc);

				//read_policy(tid, proc);

				strcpy(proc->name, cur_proc.name);
				proc->uid = cur_proc.uid;
				proc->gid = cur_proc.gid;

				add_proc(proc_num++, proc);
			} else {
				proc->num_threads++;
			}
		}

		closedir(task_dir);

		if(!threads) add_proc(proc_num++, proc);
	}

	for(i = proc_num; i < num_new_procs; i++) new_procs[i] = NULL;

	closedir(proc_dir);
}

static int read_stat(const char *filename, struct proc_info *proc) {
	FILE *file;
	char buf[MAX_LINE], *open_paren, *close_paren;

	file = fopen(filename, "r");
	if(!file) return -1;
	char *p = fgets(buf, MAX_LINE, file);
	fclose(file);
	if(!p) return -1;

	/* Split at first '(' and last ')' to get process name. */
	open_paren = strchr(buf, '(');
	close_paren = strrchr(buf, ')');
	if(!open_paren || !close_paren) return -1;

	*open_paren = *close_paren = '\0';
	strncpy(proc->tname, open_paren + 1, THREAD_NAME_LEN);
	proc->tname[THREAD_NAME_LEN-1] = 0;

	/* Scan rest of string. */
	sscanf(close_paren + 1, " %c %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d "
			"%lu %lu %*d %*d %*d %*d %*d %*d %*d %lu %lu "
			"%*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %d",
			&proc->state, &proc->utime, &proc->stime, &proc->vss, &proc->rss, &proc->prs);

	return 0;
}

static void add_proc(int proc_num, struct proc_info *proc) {
	int i;

	if (proc_num >= num_new_procs) {
		new_procs = realloc(new_procs, 2 * num_new_procs * sizeof(struct proc_info *));
		if(!new_procs) die("Could not expand procs array.\n");
		for(i = num_new_procs; i < 2 * num_new_procs; i++) new_procs[i] = NULL;
		num_new_procs = 2 * num_new_procs;
	}
	new_procs[proc_num] = proc;
}

static int read_cmdline(const char *filename, struct proc_info *proc) {
	FILE *file;
	char line[MAX_LINE];

	line[0] = 0;
	file = fopen(filename, "r");
	if(!file) return -1;
	if(fgets(line, MAX_LINE, file) && strlen(line) > 0) {
		strncpy(proc->name, line, PROC_NAME_LEN);
		proc->name[PROC_NAME_LEN-1] = 0;
	} else proc->name[0] = 0;
	fclose(file);
	return 0;
}

static int read_status(const char *filename, struct proc_info *proc) {
	FILE *file;
	char line[MAX_LINE];
	unsigned int uid, gid;

	file = fopen(filename, "r");
	if(!file) return -1;
	while(fgets(line, MAX_LINE, file)) {
		sscanf(line, "Uid: %u", &uid);
		sscanf(line, "Gid: %u", &gid);
	}
	fclose(file);
	proc->uid = uid; proc->gid = gid;
	return 0;
}

static void print_procs(void) {
	int i;
	struct proc_info *old_proc, *proc;
	unsigned long int total_delta_time;
	struct passwd *user;
	//struct group *group;
	char *user_str, user_buf[20], buf[4096];
	//char *group_str, group_buf[20];
	int current_max_processes = max_procs;

	if(use_tty) {
		/* ANSI/VT100 Terminal Support */

		/* Clear the screen */
		printf("\x1b[1J");
		
		/* Save screen */
		//printf("\x1b[?47h");

		/* Home-positioning to 0 and 0 coordinates */
		printf("\x1b[1;1H");

		/* Save current cursor position */
		// But some terminals doesn't support this...
		//printf("\x1b[7");

		/* Switch cursor invisible */
		printf("\x1b[?25l");

		/* Clear whole line (cursor position unchanged) */
		printf("\x1b[2K");

		if(ioctl(0, TIOCGWINSZ, &sz) == -1) {
			perror("Could not get Terminal window size");
			return;
		}
		if(max_procs == -1 || max_procs > sz.ws_row - 3) {
			/* To change the max proc row, when terminal size change */
			current_max_processes = sz.ws_row - 3;
		}
	}

	for (i = 0; i < num_new_procs; i++) {
		if (new_procs[i]) {
			old_proc = find_old_proc(new_procs[i]->pid, new_procs[i]->tid);
			if (old_proc) {
				new_procs[i]->delta_utime = new_procs[i]->utime - old_proc->utime;
				new_procs[i]->delta_stime = new_procs[i]->stime - old_proc->stime;
			} else {
				new_procs[i]->delta_utime = 0;
				new_procs[i]->delta_stime = 0;
			}
			new_procs[i]->delta_time = new_procs[i]->delta_utime + new_procs[i]->delta_stime;
		}
	}

	total_delta_time = (new_cpu.utime + new_cpu.ntime + new_cpu.stime + new_cpu.itime + new_cpu.iowtime + new_cpu.irqtime + new_cpu.sirqtime) -
		(old_cpu.utime + old_cpu.ntime + old_cpu.stime + old_cpu.itime + old_cpu.iowtime + old_cpu.irqtime + old_cpu.sirqtime);

	qsort(new_procs, num_new_procs, sizeof(struct proc_info *), proc_cmp);

	//printf("\n\n\n");
	if(!use_tty) putchar('\n');

#ifdef __linux__
	snprintf(buf, sizeof(buf), "User %ld%%, System %ld%%, IOW %ld%%, IRQ %ld%%",
			((new_cpu.utime + new_cpu.ntime) - (old_cpu.utime + old_cpu.ntime)) * 100  / total_delta_time,
			((new_cpu.stime ) - (old_cpu.stime)) * 100 / total_delta_time,
			((new_cpu.iowtime) - (old_cpu.iowtime)) * 100 / total_delta_time,
			((new_cpu.irqtime + new_cpu.sirqtime)
			 - (old_cpu.irqtime + old_cpu.sirqtime)) * 100 / total_delta_time);
	PRINT_BUF();
	putchar('\n');
	snprintf(buf, sizeof(buf), "User %ld + Nice %ld + Sys %ld + Idle %ld + IOW %ld + IRQ %ld + SIRQ %ld = %ld",
			new_cpu.utime - old_cpu.utime,
			new_cpu.ntime - old_cpu.ntime,
			new_cpu.stime - old_cpu.stime,
			new_cpu.itime - old_cpu.itime,
			new_cpu.iowtime - old_cpu.iowtime,
			new_cpu.irqtime - old_cpu.irqtime,
			new_cpu.sirqtime - old_cpu.sirqtime,
			total_delta_time);
	PRINT_BUF();
	putchar('\n');
#endif

	if(!threads) {
		snprintf(buf, sizeof(buf), "%5s %2s %4s %1s %5s %9s %9s %-8s %s", "PID", "PR", "CPU%", "S", "#THR", "VSS", "RSS", "USER", "COMMAND");
	} else {
		snprintf(buf, sizeof(buf), "%5s %5s %2s %4s %1s %9s %9s %-8s %-15s %s", "PID", "TID", "PR", "CPU%", "S", "VSS", "RSS", "USER", "Thread", "Proc");
	}
	if(use_tty) printf("\x1b[30;47m");
	PRINT_BUF();
	if(use_tty) printf("\x1b[39;49m");
	// No new line for this line

	for(i = 0; i < num_new_procs; i++) {
		proc = new_procs[i];

		if(!proc || (current_max_processes != -1 && (i >= current_max_processes))) break;
		user = getpwuid(proc->uid);
		//group = getgrgid(proc->gid);
		if(user && user->pw_name) {
			user_str = user->pw_name;
		} else {
			snprintf(user_buf, 20, "%d", (int)proc->uid);
			user_str = user_buf;
		}
		/*
		   if (group && group->gr_name) {
		   group_str = group->gr_name;
		   } else {
		   snprintf(group_buf, 20, "%d", proc->gid);
		   group_str = group_buf;
		   }*/
		putchar('\n');
		if(!threads) {
			snprintf(buf, sizeof(buf), "%5d %2d %3ld%% %c %5d %7luKi %7luKi %-8.8s %s", (int)proc->pid, proc->prs, proc->delta_time * 100 / total_delta_time, proc->state, proc->num_threads,
				proc->vss / 1024, proc->rss * getpagesize() / 1024, user_str, *proc->name ? proc->name : proc->tname);
			PRINT_BUF();
		} else {
			snprintf(buf, sizeof(buf), "%5d %5d %2d %3ld%% %c %7luKi %7luKi %-8.8s %-15s %s", (int)proc->pid, (int)proc->tid, proc->prs, proc->delta_time * 100 / total_delta_time, proc->state,
				proc->vss / 1024, proc->rss * getpagesize() / 1024, user_str, proc->tname, proc->name);
			PRINT_BUF();
		}
	}
	fflush(stdout);
	if(use_tty) {
		/* Restore current cursor position */
		//printf("\x1b[8");
	} else {
		putchar('\n');
	}
}

static struct proc_info *find_old_proc(pid_t pid, pid_t tid) {
	int i;

	for(i = 0; i < num_old_procs; i++) {
		if(old_procs[i] && old_procs[i]->pid == pid && old_procs[i]->tid == tid) return old_procs[i];
	}

	return NULL;
}

static void free_old_procs(void) {
	int i;

	for(i = 0; i < num_old_procs; i++) {
		if(old_procs[i]) free_proc(old_procs[i]);
	}

	free(old_procs);
}

static int proc_cpu_cmp(const void *a, const void *b) {
	struct proc_info *pa, *pb;

	pa = *((struct proc_info **)a); pb = *((struct proc_info **)b);

	if(!pa && !pb) return 0;
	if(!pa) return 1;
	if(!pb) return -1;

	return -numcmp(pa->delta_time, pb->delta_time);
}

static int proc_vss_cmp(const void *a, const void *b) {
	struct proc_info *pa, *pb;

	pa = *((struct proc_info **)a); pb = *((struct proc_info **)b);

	if(!pa && !pb) return 0;
	if(!pa) return 1;
	if(!pb) return -1;

	return -numcmp(pa->vss, pb->vss);
}

static int proc_rss_cmp(const void *a, const void *b) {
	struct proc_info *pa, *pb;

	pa = *((struct proc_info **)a); pb = *((struct proc_info **)b);

	if(!pa && !pb) return 0;
	if(!pa) return 1;
	if(!pb) return -1;

	return -numcmp(pa->rss, pb->rss);
}

static int proc_thr_cmp(const void *a, const void *b) {
	struct proc_info *pa, *pb;

	pa = *((struct proc_info **)a); pb = *((struct proc_info **)b);

	if(!pa && !pb) return 0;
	if(!pa) return 1;
	if(!pb) return -1;

	return -numcmp(pa->num_threads, pb->num_threads);
}

static int numcmp(long long int a, long long int b) {
	if(a < b) return -1;
	if(a > b) return 1;
	return 0;
}

static void usage(const char *name) {
	fprintf(stderr, "Usage: %s [-m <max_procs>] [-n <iterations>] [-d <delay>] [-s <sort_column>] [-t] [-h]\n\n"
		"	-b        Batch mode.\n"
		"	-m <num>  Maximum number of processes to display.\n"
		"	-n <num>  Updates to show before exiting.\n"
		"	-d <num>  Seconds to wait between updates.\n"
		"	-s <col>  Column to sort by (cpu,vss,rss,thr).\n"
		"	-t        Show threads instead of processes.\n"
		"	-h        Display this help screen.\n\n", name);
}

static void SIGINT_handler(int signal) {
	restore_terminal();
	exit(0);
}
