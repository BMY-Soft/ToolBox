/*
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

#include <sys/time.h>
#ifdef __APPLE__
#include <sys/sysctl.h>
#else
#include <string.h>
#endif
#include <fcntl.h>
#include <stdio.h>
#include <time.h>

static void format_time(int time, char* buffer) {
    int seconds, minutes, hours, days;

    seconds = time % 60;
    time /= 60;
    minutes = time % 60;
    time /= 60;
    hours = time % 24;
    days = time / 24;

    if(days > 0)
        sprintf(buffer, "%d days, %02d:%02d:%02d", days, hours, minutes, seconds);
    else
        sprintf(buffer, "%02d:%02d:%02d", hours, minutes, seconds);
}

int uptime_main(int argc, char *argv[])
{
    float up_time, idle_time;
    char up_string[100], idle_string[100];
#ifdef __APPLE__
	struct timeval boot_tv;
	size_t len = sizeof boot_tv;
	int mib[2] = { CTL_KERN, KERN_BOOTTIME };
	if(sysctl(mib, 2, &boot_tv, &len, NULL, 0) < 0) {
		perror("sysctl");
		return 1;
	}
	up_time = difftime(time(NULL), boot_tv.tv_sec);
#else
	struct timespec up_timespec;
    FILE* file = fopen("/proc/uptime", "r");
    if(!file) {
        fprintf(stderr, "Could not open /proc/uptime\n");
        //return -1;
	strcpy(idle_string, "unknown");
    } else {
		if(fscanf(file, "%*f %f", &idle_time) != 1) {
			fprintf(stderr, "Could not parse /proc/uptime\n");
			//fclose(file);
			//return -1;
			strcpy(idle_string, "unknown");
		} else format_time(idle_time, idle_string);
		fclose(file);
    }

    if (clock_gettime(CLOCK_MONOTONIC, &up_timespec) < 0) {
        fprintf(stderr, "Could not get monotonic time\n");
	return -1;
    }
    up_time = up_timespec.tv_sec + up_timespec.tv_nsec / 1e9;
#endif

    format_time(up_time, up_string);
#ifndef __APPLE__
    printf("up time: %s,  idle time: %s\n", up_string, idle_string);
#else
	printf("up time: %s\n", up_string);
#endif
    return 0;
}
