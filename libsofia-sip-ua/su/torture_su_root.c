/*
 * This file is part of the Sofia-SIP package
 *
 * Copyright (C) 2005 Nokia Corporation.
 *
 * Contact: Pekka Pessi <pekka.pessi@nokia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

/**@ingroup su_root_ex
 * 
 * @file torture_su_root.c
 *
 * Test su_root_register functionality.
 *
 * @author Pekka Pessi <Pekka.Pessi@nokia.com>
 *
 * Copyright (c) 2002 Nokia Research Center.  All rights reserved.
 *
 * @date Created: Wed Jun 12 15:18:11 2002 ppessi
 */

#include "config.h"

char const *name = "su_root_test";

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define TSTFLAGS rt->rt_flags
#include <sofia-sip/tstdef.h>

typedef struct root_test_s root_test_t;
typedef struct test_ep_s   test_ep_t;

#define SU_ROOT_MAGIC_T  root_test_t
#define SU_WAKEUP_ARG_T  test_ep_t

#include <sofia-sip/su_wait.h>
#include <sofia-sip/su_alloc.h>
#include <sofia-sip/su_log.h>

struct test_ep_s {
  test_ep_t     *next, **prev, **list;
  int           i;
  int           s;
  su_wait_t     wait[1];
  int           registered;
  socklen_t     addrlen;
  su_sockaddr_t addr[1];
};

typedef struct test_ep_s   test_ep_at[1];

struct root_test_s {
  su_home_t  rt_home[1];
  int        rt_flags;

  su_root_t *rt_root;
  short      rt_family;
  int        rt_status;
  int        rt_received;
  int        rt_wakeup;

  su_clone_r rt_clone;

  unsigned   rt_fail_init:1;
  unsigned   rt_fail_deinit:1;
  unsigned   rt_success_init:1;
  unsigned   rt_success_deinit:1;

  unsigned   rt_sent_reporter:1;
  unsigned   rt_recv_reporter:1;
  unsigned   rt_reported_reporter:1;

  unsigned :0;

  test_ep_at rt_ep[5];

  int rt_sockets, rt_woken;
};

/** Test root initialization */
int init_test(root_test_t *rt)
{
  su_sockaddr_t su[1] = {{ 0 }};
  int i;

  BEGIN();

  su_init();

  su->su_family = rt->rt_family;

  TEST_1(rt->rt_root = su_root_create(rt));

  for (i = 0; i < 5; i++) {
    test_ep_t *ep = rt->rt_ep[i];
    ep->i = i;
    ep->addrlen = su_sockaddr_size(su);
    TEST_1((ep->s = su_socket(su->su_family, SOCK_DGRAM, 0)) != -1);
    TEST_1(bind(ep->s, &su->su_sa, ep->addrlen) != -1);
    TEST_1(su_wait_create(ep->wait, ep->s, SU_WAIT_IN|SU_WAIT_ERR) != -1);
    TEST_1(getsockname(ep->s, &ep->addr->su_sa, &ep->addrlen) != -1);
    if (SU_HAS_INADDR_ANY(ep->addr)) {
      inet_pton(su->su_family, 
	        su->su_family == AF_INET ? "127.0.0.1" : "::1",
		SU_ADDR(ep->addr));
    }
  }

  END();
}

static int deinit_test(root_test_t *rt)
{
  BEGIN();

  TEST_VOID(su_root_destroy(rt->rt_root)); rt->rt_root = NULL;
  TEST_VOID(su_root_destroy(NULL));

  su_deinit();

  END();
}

int wakeup(root_test_t *rt,
	   su_wait_t *w,
	   test_ep_t *ep)
{
  char buffer[64];
  int n, error;
 
  su_wait_events(w, ep->s);

  n = recv(ep->s, buffer, sizeof(buffer), 0);
  error = su_errno();
 
  if (n < 0)
    fprintf(stderr, "%s: %s\n", "recv", su_strerror(error));

  TEST_1(n > 0);

  rt->rt_received = ep->i;
	   
  return 0;
}

static int wakeup0(root_test_t *rt, su_wait_t *w, test_ep_t *ep)
{
  rt->rt_wakeup = 0;
  return wakeup(rt, w, ep);
}
static int wakeup1(root_test_t *rt, su_wait_t *w, test_ep_t *ep)
{
  rt->rt_wakeup = 1;
  return wakeup(rt, w, ep);
}
static int wakeup2(root_test_t *rt, su_wait_t *w, test_ep_t *ep)
{
  rt->rt_wakeup = 2;
  return wakeup(rt, w, ep);
}
static int wakeup3(root_test_t *rt, su_wait_t *w, test_ep_t *ep)
{
  rt->rt_wakeup = 3;
  return wakeup(rt, w, ep);
}
static int wakeup4(root_test_t *rt, su_wait_t *w, test_ep_t *ep)
{
  rt->rt_wakeup = 4;
  return wakeup(rt, w, ep);
}

static
su_wakeup_f wakeups[5] = { wakeup0, wakeup1, wakeup2, wakeup3, wakeup4 };

static
void test_run(root_test_t *rt)
{
  rt->rt_received = -1;

  while (rt->rt_received == -1) {
    su_root_step(rt->rt_root, 200);
  }
}

static int register_test(root_test_t *rt)
{
  int i;
  int s;
  char *msg = "foo";

  BEGIN();

  TEST_1((s = su_socket(rt->rt_family, SOCK_DGRAM, 0)) != -1);

  for (i = 0; i < 5; i++) {
    rt->rt_ep[i]->registered = 
      su_root_register(rt->rt_root, rt->rt_ep[i]->wait, 
		       wakeups[i], rt->rt_ep[i], 0);
    TEST(rt->rt_ep[i]->registered, i + 1 + SU_HAVE_PTHREADS);
  }

  for (i = 0; i < 5; i++) {
    test_ep_t *ep = rt->rt_ep[i];
    TEST_SIZE(su_sendto(s, msg, sizeof(msg), 0, ep->addr, ep->addrlen), 
	      sizeof(msg));
    test_run(rt);
    TEST(rt->rt_received, i);
    TEST(rt->rt_wakeup, i);
  }

  for (i = 0; i < 5; i++) {
    TEST(su_root_unregister(rt->rt_root, rt->rt_ep[i]->wait, 
			    wakeups[i], rt->rt_ep[i]), 
	 rt->rt_ep[i]->registered);
  }


  for (i = 0; i < 5; i++) {
    rt->rt_ep[i]->registered = 
      su_root_register(rt->rt_root, rt->rt_ep[i]->wait, 
		       wakeups[i], rt->rt_ep[i], 1);
    TEST_1(rt->rt_ep[i]->registered > 0);
  }

  for (i = 0; i < 5; i++) {
    test_ep_t *ep = rt->rt_ep[i];
    TEST_SIZE(su_sendto(s, msg, sizeof(msg), 0, ep->addr, ep->addrlen), 
	      sizeof(msg));
    test_run(rt);
    TEST(rt->rt_received, i);
    TEST(rt->rt_wakeup, i);
  }

  for (i = 0; i < 5; i++) {
    TEST(su_root_deregister(rt->rt_root, rt->rt_ep[i]->registered), 
	 rt->rt_ep[i]->registered);
  }

  for (i = 0; i < 5; i++) {
    test_ep_t *ep = rt->rt_ep[i];
    TEST_1(su_wait_create(ep->wait, ep->s, SU_WAIT_IN|SU_WAIT_ERR) != -1);
    ep->registered = 
      su_root_register(rt->rt_root, ep->wait, 
		       wakeups[i], ep, 1);
    TEST_1(ep->registered > 0);
  }

  for (i = 0; i < 5; i++) {
    test_ep_t *ep = rt->rt_ep[i];
    TEST_SIZE(su_sendto(s, msg, sizeof(msg), 0, ep->addr, ep->addrlen), 
	      sizeof(msg));
    test_run(rt);
    TEST(rt->rt_received, i);
    TEST(rt->rt_wakeup, i);
  }

  for (i = 0; i < 5; i++) {
    TEST(su_root_unregister(rt->rt_root, rt->rt_ep[i]->wait, 
			    wakeups[i], rt->rt_ep[i]), 
	 rt->rt_ep[i]->registered);
  }

  END();
}


int wakeup_remove(root_test_t *rt, su_wait_t *w, test_ep_t *node)
{
  char buffer[64];
  ssize_t x;
  test_ep_t *n = node->next; 

  su_wait_events(w, node->s);

  x = recv(node->s, buffer, sizeof(buffer), 0);

  if (x < 0)
    fprintf(stderr, "%s: %s\n", "recv", su_strerror(su_errno()));

  if (node->prev) {		/* first run */
    *node->prev = n;

    if (n) {
      *node->prev = node->next;    
      node->next->prev = node->prev;
      sendto(n->s, "foo", 3, 0, (void *)n->addr, n->addrlen);
    }

    node->next = NULL;
    node->prev = NULL;
    
    if (!*node->list) {
      su_root_break(rt->rt_root);
    }
  }
  else {			/* second run */
    if (++rt->rt_woken == rt->rt_sockets)
      su_root_break(rt->rt_root);
  }

  return 0;
}


int event_test(root_test_t rt[1])
{
  BEGIN();
  int i = 0, N = 2048;
  test_ep_t *n, *nodes, *list = NULL;
  su_sockaddr_t su[1];
  socklen_t sulen;
  
  TEST_1(nodes = calloc(N, sizeof *nodes));
  
  memset(su, 0, sulen = sizeof su->su_sin);
  su->su_len = sizeof su->su_sin;
  su->su_family = AF_INET;
  su->su_sin.sin_addr.s_addr = htonl(0x7f000001); /* 127.0.0.1 */

  for (i = 0; i < N; i++) {
    n = nodes + i;
    n->s = su_socket(AF_INET, SOCK_DGRAM, 0);

    if (n->s == INVALID_SOCKET)
      break;

    n->addrlen = sizeof n->addr;

    n->addr->su_len = sizeof n->addr;

    if (bind(n->s, (void *)su, sulen) < 0) {
      su_perror("bind()");
      su_close(n->s);
      break;
    }
      
    if (getsockname(n->s, (void *)n->addr, &n->addrlen)) {
      su_perror("getsockname()");
      su_close(n->s);
      break;
    }

    if (su_wait_create(n->wait, n->s, SU_WAIT_IN)) {
      su_perror("su_wait_create()");
      su_close(n->s);
      break;
    }

    n->registered = su_root_register(rt->rt_root, n->wait, wakeup_remove, n, 0);
    if (n->registered < 0) {
      su_wait_destroy(n->wait);
      su_close(n->s);
      break;
    }
    
    n->list = &list, n->prev = &list;
    if ((n->next = list))
      n->next->prev = &n->next;
    list = n;
  }

  TEST_1(i >= 1);

  N = i;

  /* Wake up socket at a time */
  n = list; sendto(n->s, "foo", 3, 0, (void *)n->addr, n->addrlen);

  su_root_run(rt->rt_root);

  for (i = 0; i < N; i++) {
    n = nodes + i;
    TEST_1(n->prev == NULL);
    sendto(n->s, "bar", 3, 0, (void *)n->addr, n->addrlen);
  }

  rt->rt_sockets = N;

  /* Wake up all sockets */
  su_root_run(rt->rt_root);

  for (i = 0; i < N; i++) {
    n = nodes + i;
    su_root_deregister(rt->rt_root, n->registered);
    TEST_1(su_close(n->s) == 0);
  }

  free(nodes);

  END();
}

int fail_init(su_root_t *root, root_test_t *rt)
{
  rt->rt_fail_init = 1;
  return -1;
}

void fail_deinit(su_root_t *root, root_test_t *rt)
{
  rt->rt_fail_deinit = 1;
}

int success_init(su_root_t *root, root_test_t *rt)
{
  rt->rt_success_init = 1;
  return 0;
}

void success_deinit(su_root_t *root, root_test_t *rt)
{
  rt->rt_success_deinit = 1;
}

void receive_a_reporter(root_test_t *rt, 
			su_msg_r msg,
			su_msg_arg_t *arg)
{
  rt->rt_recv_reporter = 1;
}

void receive_recv_report(root_test_t *rt, 
			 su_msg_r msg,
			 su_msg_arg_t *arg)
{
  rt->rt_reported_reporter = 1;
}

void send_a_reporter_msg(root_test_t *rt, 
			 su_msg_r msg,
			 su_msg_arg_t *arg)
{
  su_msg_r m = SU_MSG_R_INIT;

  if (su_msg_create(m,
		    su_msg_from(msg),
		    su_msg_to(msg),
		    receive_a_reporter,
		    0) == 0 &&
      su_msg_report(m, receive_recv_report) == 0 &&
      su_msg_send(m) == 0)
    rt->rt_sent_reporter = 1;
}

static int clone_test(root_test_t rt[1])
{
  BEGIN();

  su_msg_r m = SU_MSG_R_INIT;

  rt->rt_fail_init = 0;
  rt->rt_fail_deinit = 0;
  rt->rt_success_init = 0;
  rt->rt_success_deinit = 0;
  rt->rt_sent_reporter = 0;
  rt->rt_recv_reporter = 0;
  rt->rt_reported_reporter = 0;

  TEST(su_clone_start(rt->rt_root,
		      rt->rt_clone,
		      rt,
		      fail_init,
		      fail_deinit), SU_FAILURE);
  TEST_1(rt->rt_fail_init);
  TEST_1(rt->rt_fail_deinit);

  TEST(su_clone_start(rt->rt_root,
		      rt->rt_clone,
		      rt,
		      success_init,
		      success_deinit), SU_SUCCESS);
  TEST_1(rt->rt_success_init);
  TEST_1(!rt->rt_success_deinit);

  /* Make sure 3-way handshake is done as expected */
  TEST(su_msg_create(m,
		     su_clone_task(rt->rt_clone),
		     su_root_task(rt->rt_root),
		     send_a_reporter_msg,
		     0), 0);
  TEST(su_msg_send(m), 0);

  TEST_VOID(su_clone_wait(rt->rt_root, rt->rt_clone));

  TEST_1(rt->rt_success_deinit);
  TEST_1(rt->rt_sent_reporter);
  TEST_1(rt->rt_recv_reporter);
  TEST_1(rt->rt_reported_reporter);

  rt->rt_recv_reporter = 0;

  /* Make sure we can handle messages done as expected */
  TEST(su_msg_create(m,
		     su_root_task(rt->rt_root),
		     su_task_null,
		     receive_a_reporter,
		     0), 0);
  TEST(su_msg_send(m), 0);
  su_root_step(rt->rt_root, 0);
  TEST_1(rt->rt_recv_reporter);

  rt->rt_success_init = 0;
  rt->rt_success_deinit = 0;

  END();
}

void usage(void)
{
  fprintf(stderr, 
	  "usage: %s [-v]\n", 
	  name);
}

int main(int argc, char *argv[])
{
  root_test_t rt[1] = {{{ SU_HOME_INIT(rt) }}};
  int retval = 0;
  int i;

  rt->rt_family = AF_INET;

  for (i = 1; argv[i]; i++) {
    if (strcmp(argv[i], "-v") == 0)
      rt->rt_flags |= tst_verbatim;
#if SU_HAVE_IN6
    else if (strcmp(argv[i], "-6") == 0)
      rt->rt_family = AF_INET6;
#endif
    else
      usage();
  }

  retval |= init_test(rt);
  retval |= register_test(rt);
  retval |= event_test(rt);
  su_root_threading(rt->rt_root, 1);
  retval |= clone_test(rt);
  su_root_threading(rt->rt_root, 0);
  retval |= clone_test(rt);
  retval |= deinit_test(rt);

  return retval;
}