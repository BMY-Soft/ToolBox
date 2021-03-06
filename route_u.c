/*	route - toolbox
	Copyright 2015-2016 Rivoreo

	This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.
*/


#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <stdlib.h>
#include <netdb.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/route.h>
#ifdef __GNU__		/* The GNU/Hurd system seems not implements some ioctls, porting failed */
#include <hurd/ioctl.h>
#ifndef SIOCADDRT
#define SIOCADDRT _IOW('r', 10, struct ortentry)
#endif
#elif defined __SVR4
#include <sys/sockio.h>
#endif
#ifdef __linux__
#include <linux/rtnetlink.h>
#endif

static void print_usage(const char *name) {
	fprintf(stderr, "Usage:\n"
		"	%s {add|del} [-host|-net] <target>[/<prefixlen>] [netmask <netmask>] [gw <gateway>]"
#ifdef __linux__
			" [{dev|interface} <interface>]"
#endif
			" [metric <metric>]\n"
		"	%s {add|del} [-host|-net] <target>[/<prefixlen>] <gateway> [<netmask>]\n"
		"	%s {add|del} [-host|-net] <target>[/<prefixlen>] reject\n"
		"	%s [-n] get <target>[/<prefixlen>]\n"
		"	%s [-n] {show|print}\n\n", name, name, name, name, name);
}

static inline int set_address(const char *address, struct sockaddr *sa) {
	if(isdigit(*address)) return inet_aton(address, &((struct sockaddr_in *)sa)->sin_addr);
	struct addrinfo hints = {
		.ai_family = PF_INET,
		.ai_socktype = 0,
		.ai_protocol = 0
	};
	struct addrinfo *info;
	int e = getaddrinfo(address, NULL, &hints, &info);
	if(e) {
		// Should use gai_strerror
		errno = ESRCH;
		return -1;
	}
	((struct sockaddr_in *)sa)->sin_addr = ((struct sockaddr_in *)info->ai_addr)->sin_addr;
	freeaddrinfo(info);
	return 0;
}

static inline void set_prefix_length(int length, struct sockaddr *sa) {
	((struct sockaddr_in *)sa)->sin_addr.s_addr = htonl(0xffffffff << (32 - length));
}

static int set_netmask(struct rtentry *rt, char *netmask) {
	//fprintf(stderr, "function: set_netmask(%p, %p<%s>)\n", rt, netmask, netmask);
	rt->rt_flags |= RTF_UP;
//#ifdef RTF_MASK
//	rt->rt_flags |= RTF_MASK;
//#endif
	return set_address(netmask, &rt->rt_genmask);
}

static int set_gateway(struct rtentry *rt, char *gateway) {
	//fprintf(stderr, "function: set_gateway(%p, %p<%s>)\n", rt, gateway, gateway);
	rt->rt_flags |= RTF_UP | RTF_GATEWAY;
	return set_address(gateway, &rt->rt_gateway);
}

#ifdef __linux__
static int set_device(struct rtentry *rt, char *dev) {
	rt->rt_flags |= RTF_UP;
	rt->rt_dev = dev;
	return 0;
}
#endif

static int set_metric(struct rtentry *rt, char *metric) {
	//fprintf(stderr, "function: set_metric(%p, %p<%s>)\n", rt, metric, metric);
	rt->rt_metric = atoi(metric);
	return 0;
}

static int apply_route(const struct rtentry *rt, int request) {
	if(rt->rt_flags & RTF_HOST) {
		struct in_addr *netmask = &((struct sockaddr_in *)&rt->rt_genmask)->sin_addr;
		if(netmask->s_addr != 0xffffffff) {
			//fprintf(stderr, "%s: Need a network route to apply netmask %.8x\n", argv[0], netmask->s_addr);
			errno = EINVAL;
			return -1;
		}
	}
	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	if(fd == -1) return -1;
	int r = ioctl(fd, request, rt);
	int e = errno;
	if(close(fd) < 0) return -1;
	errno = e;
	return r;
}

static struct {
	const char *name;
	int (*set)(struct rtentry *, char *);
} route_options[] = {
	{ "netmask", set_netmask },
	{ "gw", set_gateway },
	{ "gateway", set_gateway },
#ifdef __linux__
	{ "dev", set_device },
	{ "device", set_device },
	{ "interface", set_device },
#endif
	{ "metric", set_metric }
};

/* Return values:
	0	Found and/or success
	-1	Address convert failed
	-2	Not found
	-3	rt != NULL && value == NULL
*/
static int find_and_set_route_option(const char *option, struct rtentry *rt, char *value) {
	int i = sizeof route_options / sizeof *route_options;
	while(--i >= 0) {
		if(strcmp(option, route_options[i].name) == 0) {
			if(!rt) return 0;
			if(!value) return -3;
			return route_options[i].set(rt, value);
		}
	}
	return -2;
}

int route_main(int argc, char *argv[]) {
	struct rtentry rt = {
		.rt_dst     = { .sa_family = AF_INET },
#ifndef __SVR4
		.rt_genmask = { .sa_family = AF_INET },
#endif
		.rt_gateway = { .sa_family = AF_INET },
	};
	int request = -1;
	int no_resolve = 0;
	char **v = argv + 1;
	errno = EINVAL;
	while(*v) {
		if(**v == '-') {
			const char *o = *v + 1;
			switch(*o) {
				case 'h':
					print_usage(argv[0]);
					return 0;
				case 'n':
					no_resolve = 1;
					break;
				case '-':
					if(o[1]) {
						fprintf(stderr, "%s: Unknown option '%s'\n", argv[0], *v);
						return -1;
					}
					break;
				default:
					fprintf(stderr, "%s: Unknown option '-%c'\n", argv[0], *o);
					return -1;
			}
			argv[1] = argv[0];
			argc--;
			argv++;
		} else break;
		v++;
	}

	if(argc < 2) {
		print_usage(argv[0]);
		return -1;
	}

	if(strcmp(argv[1], "add") == 0 || strncmp(argv[1], "del", 3) == 0) {
		__label__ missing_target;
		int route_type_set = 0;
		if(strcmp(argv[1], "add") == 0) {
			request = SIOCADDRT;
		} else if(!argv[1][3] || (argv[1][3] == 'e' && (!argv[1][4] || (argv[1][4] == 't' && (!argv[1][5] || (argv[1][5] == 'e' && !argv[1][6])))))) {
			request = SIOCDELRT;
		} else {
			fprintf(stderr, "%s: Invalid sub command '%s'\n", argv[0], argv[1]);
			return 1;
		}
		//if(argc < 3 || (argv[2][0] == '-' && argv < 4)) {
		if(argc < 3) {
missing_target:
			fprintf(stderr, "%s: Missing target\n", argv[0]);
			return 1;
		}
		if(argv[2][0] == '-') {
			//fprintf(stderr, "argc = %d, argv[2]: \"%s\"\n", argc, argv[2]);
			if(strcmp(argv[2], "-host") == 0) {
				rt.rt_flags |= RTF_HOST;
				route_type_set = 1;
			} else if(strcmp(argv[2], "-net") == 0) {
				rt.rt_flags &= ~RTF_HOST;
				route_type_set = 1;
			} else {
				fprintf(stderr, "%s: Invalid destination type '%s'\n", argv[0], argv[2]);
				return 1;
			}
			if(argc < 4) goto missing_target;
			memmove(argv + 2, argv + 3, (argc - 2) * sizeof(char *));
			argc--;
		}
		if(strcmp(argv[2], "default")) {
			char *slash = strrchr(argv[2], '/');
			if(slash) {
				char *endptr;
				if(strchr(argv[2], '/') != slash) {
					fprintf(stderr, "%s: Invalid destination address '%s'\n", argv[0], argv[2]);
					return 1;
				}
				int prefixlen = strtol(slash + 1, &endptr, 10);
				if(*endptr) {
					fprintf(stderr, "%s: Invalid prefix length '%s'\n", argv[0], slash + 1);
					return 1;
				}
				set_prefix_length(prefixlen, &rt.rt_genmask);
				*slash = 0;
			} else ((struct sockaddr_in *)&rt.rt_genmask)->sin_addr.s_addr = 0xffffffff;
			if(set_address(argv[2], &rt.rt_dst) < 0) {
				fprintf(stderr, "%s: %s: %s\n", argv[0], argv[2], strerror(errno));
				return 1;
			}
			if(!route_type_set) rt.rt_flags |= RTF_HOST;
		}
		if(argc > 3 && strcmp(argv[3], "reject") == 0) {
			if(argc > 4) {
				fprintf(stderr, "%s: Cannot use other options with a reject route\n", argv[0]);
				return 1;
			}
			rt.rt_flags |= RTF_REJECT;
			if(apply_route(&rt, request) < 0) {
				perror(argv[0]);
				return 1;
			}
			return 0;
		}
		if((argc == 4 || argc == 5) && find_and_set_route_option(argv[3], NULL, NULL) == -2) {
			if(set_gateway(&rt, argv[3]) < 0) {
				fprintf(stderr, "%s: set_gateway: %s: %s\n", argv[0], argv[3], strerror(errno));
				return 1;
			}
			if(argc == 5 && set_netmask(&rt, argv[4]) < 0) {
				fprintf(stderr, "%s: set_netmask: %s: %s\n", argv[0], argv[3], strerror(errno));
				return 1;
			}
			if(!route_type_set) {
				if(((struct sockaddr_in *)&rt.rt_genmask)->sin_addr.s_addr == 0xffffffff) {
					rt.rt_flags |= RTF_HOST;
				} else {
					rt.rt_flags &= ~RTF_HOST;
				}
			}
			if(apply_route(&rt, request) < 0) {
				perror(argv[0]);
				return 1;
			}
			return 0;
		}
		char **v = argv + 3;
		while(*v) {
			switch(find_and_set_route_option(*v, &rt, v[1])) {
				case -3:
					fprintf(stderr, "%s: Missing argument for %s\n", argv[0], *v);
					return 1;
				case -2:
					fprintf(stderr, "%s: Unknown option %s\n", argv[0], *v);
					return 1;
				case -1:
					fprintf(stderr, "%s: %s: %s\n", argv[0], *v, strerror(errno));
					return 1;
			}
			v += 2;
		}
		if(!((struct sockaddr_in *)&rt.rt_gateway)->sin_addr.s_addr &&
#ifdef __linux__
		!rt.rt_dev &&
#endif
		request == SIOCADDRT) {
			fprintf(stderr, "%s: Need a gateway"
#ifdef __linux__
				" or a device"
#endif
				" for add route\n", argv[0]);
			return 0;
		}
		if(apply_route(&rt, request) < 0) {
			perror(argv[0]);
			return 1;
		}
		return 0;
	} else if(strcmp(argv[1], "get") == 0) {
		if(argc < 3) {
			fprintf(stderr, "%s: Missing target\n", argv[0]);
			return 1;
		}
#ifdef __linux__
		int fd = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE);
		if(fd == -1) {
			fprintf(stderr, "%s: socket: netlink: %s\n", argv[0], strerror(errno));
			return 1;
		}
		struct {
			struct nlmsghdr hdr;
			//struct rtattr attr[4];
			struct rtmsg data;
		} nlmsg;
		memset(&nlmsg, 0, sizeof nlmsg);
		nlmsg.hdr.nlmsg_len = sizeof nlmsg;
		nlmsg.hdr.nlmsg_type = RTM_GETROUTE;
		nlmsg.hdr.nlmsg_flags = NLM_F_REQUEST;
		nlmsg.data.rtm_family = AF_INET;
		//nlmsg.data.rtm_dst_len = 0;
		nlmsg.data.rtm_type = RTN_UNSPEC;
		nlmsg.data.rtm_protocol = RTPROT_KERNEL;
		nlmsg.data.rtm_scope = RT_SCOPE_UNIVERSE;
		nlmsg.data.rtm_table = RT_TABLE_UNSPEC;
		//msg.attr[0].rta_len = sizeof(struct rtattr);
		//msg.attr[0].rta_type = RTA_DST;
		struct iovec iov = {
			.iov_base = &nlmsg,
			.iov_len = nlmsg.hdr.nlmsg_len
		};
		struct sockaddr_nl addr = {
			.nl_family = AF_NETLINK
		};
		struct msghdr msg = {
			.msg_name = &addr,
			.msg_namelen = sizeof addr,
			.msg_iov = &iov,
			.msg_iovlen = 1
		};
		if(sendmsg(fd, &msg, 0) < 0) {
			perror("sendmsg");
			return 1;
		}
		// TODO
#else
		fprintf(stderr, "%s: %s: %s\n", argv[0], argv[1], strerror(ENOSYS));
#endif
		return 1;
	} else if(strcmp(argv[1], "show") == 0 || strcmp(argv[1], "print") == 0) {
		// TODO
		fprintf(stderr, "%s: %s: %s\n", argv[0], argv[1], strerror(ENOSYS));
		return 1;
	} else {
		fprintf(stderr, "%s: Invalid sub command '%s'\n", argv[0], argv[1]);
		return 1;
	}

	return 0;
}
