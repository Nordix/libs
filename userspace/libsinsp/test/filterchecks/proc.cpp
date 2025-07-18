// SPDX-License-Identifier: Apache-2.0
/*
Copyright (C) 2023 The Falco Authors.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

*/

#include <helpers/threads_helpers.h>

TEST_F(sinsp_with_test_input, PROC_FILTER_nthreads) {
	DEFAULT_TREE

	/* we call a random event to obtain an event associated with this thread info */
	auto evt = generate_random_event(p2_t1_tid);
	ASSERT_EQ(get_field_as_string(evt, "proc.nthreads"), "3");

	/* we call proc_exit so we should decrement the count by 1 */
	evt = generate_proc_exit_event(p2_t1_tid, p2_t2_tid);
	ASSERT_EQ(get_field_as_string(evt, "proc.nthreads"), "2");

	/* we remove the thread group info from the thread so we should obtain a count equal to 0 */
	auto p2_t2_tinfo = m_inspector.m_thread_manager->get_thread_ref(p2_t2_tid, false).get();
	ASSERT_TRUE(p2_t2_tinfo);
	p2_t2_tinfo->m_tginfo.reset();

	evt = generate_random_event(p2_t2_tid);
	ASSERT_EQ(get_field_as_string(evt, "proc.nthreads"), "0");
}

TEST_F(sinsp_with_test_input, PROC_FILTER_nchilds) {
	DEFAULT_TREE

	/* we call a random event to obtain an event associated with this thread info */
	auto evt = generate_random_event(p2_t1_tid);
	/* The main thread is not included in the count */
	ASSERT_EQ(get_field_as_string(evt, "proc.nchilds"), "2");

	/* removing the main thread doesn't change the count */
	evt = generate_proc_exit_event(p2_t1_tid, p2_t2_tid);
	ASSERT_EQ(get_field_as_string(evt, "proc.nchilds"), "2");

	/* This should decrement the count by 1 */
	evt = generate_proc_exit_event(p2_t3_tid, p2_t2_tid);
	ASSERT_EQ(get_field_as_string(evt, "proc.nchilds"), "1");

	/* we remove the thread group info from the thread so we should obtain a count equal to 0 */
	auto p2_t2_tinfo = m_inspector.m_thread_manager->get_thread_ref(p2_t2_tid, false).get();
	ASSERT_TRUE(p2_t2_tinfo);
	p2_t2_tinfo->m_tginfo.reset();

	evt = generate_random_event(p2_t2_tid);
	ASSERT_EQ(get_field_as_string(evt, "proc.nchilds"), "0");
}

TEST_F(sinsp_with_test_input, PROC_FILTER_exepath) {
	DEFAULT_TREE

	/* Now we call an execve on p6_t1 */
	auto evt =
	        generate_execve_enter_and_exit_event(0,
	                                             p6_t1_tid,
	                                             p6_t1_tid,
	                                             p6_t1_pid,
	                                             p6_t1_ptid,
	                                             "/good-exe",
	                                             "good-exe",
	                                             // Please note that the `deleted` will be removed.
	                                             "/usr/bin/bad-exe (deleted)");

	ASSERT_EQ(get_field_as_string(evt, "proc.exepath"), "/usr/bin/bad-exe");
	ASSERT_EQ(get_field_as_string(evt, "proc.name"), "good-exe");
}

TEST_F(sinsp_with_test_input, PROC_FILTER_pexepath_aexepath) {
	DEFAULT_TREE

	/* p3_t1 call execve to set an exepath */
	generate_execve_enter_and_exit_event(0,
	                                     p3_t1_tid,
	                                     p3_t1_tid,
	                                     p3_t1_pid,
	                                     p3_t1_ptid,
	                                     "/p3_t1_exepath",
	                                     "p3_t1",
	                                     "/usr/bin/p3_t1_trusted_exepath");

	/* p4_t2 call execve to set an exepath */
	generate_execve_enter_and_exit_event(0,
	                                     p4_t2_tid,
	                                     p4_t1_tid,
	                                     p4_t1_pid,
	                                     p4_t1_ptid,
	                                     "/p4_t1_exepath",
	                                     "p4_t1",
	                                     "/usr/bin/p4_t1_trusted_exepath");

	/* p5_t2 call execve to set an exepath */
	generate_execve_enter_and_exit_event(0,
	                                     p5_t2_tid,
	                                     p5_t1_tid,
	                                     p5_t1_pid,
	                                     p5_t1_ptid,
	                                     "/p5_t1_exepath",
	                                     "p5_t1",
	                                     "/usr/bin/p5_t1_trusted_exepath");

	/* Now we call an execve on p6_t1 and we check for `pexepath` and `aexepath` */
	auto evt = generate_execve_enter_and_exit_event(0,
	                                                p6_t1_tid,
	                                                p6_t1_tid,
	                                                p6_t1_pid,
	                                                p6_t1_ptid,
	                                                "/p6_t1_exepath",
	                                                "p6_t1",
	                                                "/usr/bin/p6_t1_trusted_exepath");

	ASSERT_EQ(get_field_as_string(evt, "proc.exepath"), "/usr/bin/p6_t1_trusted_exepath");
	ASSERT_EQ(get_field_as_string(evt, "proc.aexepath[0]"), "/usr/bin/p6_t1_trusted_exepath");
	ASSERT_EQ(get_field_as_string(evt, "proc.pexepath"), "/usr/bin/p5_t1_trusted_exepath");
	ASSERT_EQ(get_field_as_string(evt, "proc.aexepath[1]"),
	          get_field_as_string(evt, "proc.pexepath"));
	ASSERT_EQ(get_field_as_string(evt, "proc.aexepath[2]"), "/usr/bin/p4_t1_trusted_exepath");
	ASSERT_EQ(get_field_as_string(evt, "proc.aexepath[3]"), "/usr/bin/p3_t1_trusted_exepath");
	/* p2_t1 never calls an execve so it takes the exepath from `init` */
	ASSERT_EQ(get_field_as_string(evt, "proc.aexepath[4]"), "/sbin/init");
	/* `init` exepath */
	ASSERT_EQ(get_field_as_string(evt, "proc.aexepath[5]"), "/sbin/init");
	/* this field shouldn't exist */
	ASSERT_FALSE(field_has_value(evt, "proc.aexepath[6]"));
}

TEST_F(sinsp_with_test_input, PROC_FILTER_aname) {
	DEFAULT_TREE

	// proc.aname[0]=good-exe, proc.aname[1]=bash, proc.aname[2]=bash, proc.aname[3]=bash,
	// proc.aname[4]=bash, proc.aname[5]=init
	auto evt = generate_execve_enter_and_exit_event(0,
	                                                p6_t1_tid,
	                                                p6_t1_tid,
	                                                p6_t1_pid,
	                                                p6_t1_ptid,
	                                                "/good-exe",
	                                                "good-exe",
	                                                "/good-exe");

	EXPECT_TRUE(eval_filter(evt, "proc.aname in (init)"));
	EXPECT_TRUE(eval_filter(evt, "proc.aname in (bash)"));
	EXPECT_TRUE(eval_filter(evt, "proc.aname in (good-exe, init)"));
	EXPECT_TRUE(eval_filter(evt, "proc.aname = bash"));
	EXPECT_TRUE(eval_filter(evt, "proc.aname = init"));

	EXPECT_FALSE(eval_filter(evt, "proc.aname in (good-exe)"));
	EXPECT_FALSE(eval_filter(evt, "proc.aname = good-exe"));
	EXPECT_FALSE(eval_filter(evt, "proc.aname in (bad-exe)"));
}

TEST_F(sinsp_with_test_input, PROC_FILTER_pgid_family) {
	DEFAULT_TREE

	const auto& thread_manager = m_inspector.m_thread_manager;

	auto p3_t1_tinfo = thread_manager->get_thread_ref(p3_t1_tid, false).get();
	ASSERT_TRUE(p3_t1_tinfo);

	auto p1_t1_tinfo = thread_manager->get_thread_ref(p1_t1_tid, false).get();
	ASSERT_TRUE(p1_t1_tinfo);

	//
	// Direct access in the thread table
	//

	// manually set the pgid to p1_t1
	p3_t1_tinfo->m_pgid = p1_t1_pid;
	// Be sure we will obtain specific values not shared by other threads
	p1_t1_tinfo->m_comm = "p1_t1_comm";
	p1_t1_tinfo->m_exe = "p1_t1_exe";
	p1_t1_tinfo->m_exepath = "p1_t1_exepath";

	// Generate random event to call filter-checks
	auto evt = generate_random_event(p3_t1_tid);

	ASSERT_EQ(get_field_as_string(evt, "proc.pgid"), std::to_string(p1_t1_pid));
	ASSERT_EQ(get_field_as_string(evt, "proc.pgid.name"), p1_t1_tinfo->get_comm());
	ASSERT_EQ(get_field_as_string(evt, "proc.pgid.exe"), p1_t1_tinfo->get_exe());
	ASSERT_EQ(get_field_as_string(evt, "proc.pgid.exepath"), p1_t1_tinfo->get_exepath());
	ASSERT_EQ(get_field_as_string(evt, "proc.is_pgid_leader"), "false");

	//
	// Current process is leader
	//

	p3_t1_tinfo->m_pgid = p3_t1_pid;
	p3_t1_tinfo->m_comm = "p3_t1_comm";
	p3_t1_tinfo->m_exe = "p3_t1_exe";
	p3_t1_tinfo->m_exepath = "p3_t1_exepath";
	evt = generate_random_event(p3_t1_tid);
	ASSERT_EQ(get_field_as_string(evt, "proc.pgid"), std::to_string(p3_t1_tid));
	ASSERT_EQ(get_field_as_string(evt, "proc.pgid.name"), p3_t1_tinfo->get_comm());
	ASSERT_EQ(get_field_as_string(evt, "proc.pgid.exe"), p3_t1_tinfo->get_exe());
	ASSERT_EQ(get_field_as_string(evt, "proc.pgid.exepath"), p3_t1_tinfo->get_exepath());
	ASSERT_EQ(get_field_as_string(evt, "proc.is_pgid_leader"), "true");

	//
	// Missing process group find ancestor
	//

	auto p2_t1_tinfo = m_inspector.m_thread_manager->get_thread_ref(p2_t1_tid, false).get();
	ASSERT_TRUE(p2_t1_tinfo);

	int64_t random_pgid = 100000;
	p3_t1_tinfo->m_pgid = random_pgid;
	// p2_t1 is the last ancestor with the same pgid
	p2_t1_tinfo->m_pgid = random_pgid;
	p2_t1_tinfo->m_comm = "p2_t1_comm";
	p2_t1_tinfo->m_exe = "p2_t1_exe";
	p2_t1_tinfo->m_exepath = "p2_t1_exepath";
	evt = generate_random_event(p3_t1_tid);
	ASSERT_EQ(get_field_as_string(evt, "proc.pgid"), std::to_string(random_pgid));
	ASSERT_EQ(get_field_as_string(evt, "proc.pgid.name"), p2_t1_tinfo->get_comm());
	ASSERT_EQ(get_field_as_string(evt, "proc.pgid.exe"), p2_t1_tinfo->get_exe());
	ASSERT_EQ(get_field_as_string(evt, "proc.pgid.exepath"), p2_t1_tinfo->get_exepath());
	ASSERT_EQ(get_field_as_string(evt, "proc.is_pgid_leader"), "false");
}

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__) && !defined(__APPLE__)
TEST_F(sinsp_with_test_input, PROC_FILTER_stdin_stdout_stderr) {
	DEFAULT_TREE
	sinsp_evt* evt = NULL;
	int64_t stdin_fd = 0, stdout_fd = 1, stderr_fd = 2;

	generate_socket_events();

	sockaddr_in client =
	        test_utils::fill_sockaddr_in(DEFAULT_CLIENT_PORT, DEFAULT_IPV4_CLIENT_STRING);

	sockaddr_in server =
	        test_utils::fill_sockaddr_in(DEFAULT_SERVER_PORT, DEFAULT_IPV4_SERVER_STRING);

	std::vector<uint8_t> server_sockaddr =
	        test_utils::pack_sockaddr(reinterpret_cast<sockaddr*>(&server));
	evt = add_event_advance_ts(
	        increasing_ts(),
	        1,
	        PPME_SOCKET_CONNECT_E,
	        2,
	        sinsp_test_input::socket_params::default_fd,
	        scap_const_sized_buffer{server_sockaddr.data(), server_sockaddr.size()});

	std::vector<uint8_t> socktuple =
	        test_utils::pack_socktuple(reinterpret_cast<sockaddr*>(&client),
	                                   reinterpret_cast<sockaddr*>(&server));
	evt = add_event_advance_ts(
	        increasing_ts(),
	        1,
	        PPME_SOCKET_CONNECT_X,
	        4,
	        (int64_t)0,
	        scap_const_sized_buffer{socktuple.data(), socktuple.size()},
	        sinsp_test_input::socket_params::default_fd,
	        scap_const_sized_buffer{server_sockaddr.data(), server_sockaddr.size()});

	// The socket is duped to stdin, stdout, stderr
	evt = add_event_advance_ts(increasing_ts(),
	                           1,
	                           PPME_SYSCALL_DUP2_E,
	                           1,
	                           sinsp_test_input::socket_params::default_fd);
	evt = add_event_advance_ts(increasing_ts(),
	                           1,
	                           PPME_SYSCALL_DUP2_X,
	                           3,
	                           stdin_fd,
	                           sinsp_test_input::socket_params::default_fd,
	                           stdin_fd);
	evt = add_event_advance_ts(increasing_ts(),
	                           1,
	                           PPME_SYSCALL_DUP2_E,
	                           1,
	                           sinsp_test_input::socket_params::default_fd);
	evt = add_event_advance_ts(increasing_ts(),
	                           1,
	                           PPME_SYSCALL_DUP2_X,
	                           3,
	                           stdout_fd,
	                           sinsp_test_input::socket_params::default_fd,
	                           stdout_fd);
	evt = add_event_advance_ts(increasing_ts(),
	                           1,
	                           PPME_SYSCALL_DUP2_E,
	                           1,
	                           sinsp_test_input::socket_params::default_fd);
	evt = add_event_advance_ts(increasing_ts(),
	                           1,
	                           PPME_SYSCALL_DUP2_X,
	                           3,
	                           stderr_fd,
	                           sinsp_test_input::socket_params::default_fd,
	                           stderr_fd);

	// Exec a process and check stdin, stdout and stderr types and names
	evt = generate_execve_enter_and_exit_event(0,
	                                           1,
	                                           1,
	                                           1,
	                                           1,
	                                           "/proc_filter_stdin_stdout_stderr",
	                                           "proc_filter_stdin_stdout_stderr",
	                                           "/usr/bin/proc_filter_stdin_stdout_stderr");
	ASSERT_EQ(get_field_as_string(evt, "proc.stdin.type"), "ipv4");
	ASSERT_EQ(get_field_as_string(evt, "proc.stdout.type"), "ipv4");
	ASSERT_EQ(get_field_as_string(evt, "proc.stderr.type"), "ipv4");

	std::string tuple_str = std::string(DEFAULT_IPV4_CLIENT_STRING) + ":" +
	                        std::to_string(DEFAULT_CLIENT_PORT) + "->" +
	                        std::string(DEFAULT_IPV4_SERVER_STRING) + ":" +
	                        std::to_string(DEFAULT_SERVER_PORT);
	ASSERT_EQ(get_field_as_string(evt, "proc.stdin.name"), tuple_str);
	ASSERT_EQ(get_field_as_string(evt, "proc.stdout.name"), tuple_str);
	ASSERT_EQ(get_field_as_string(evt, "proc.stderr.name"), tuple_str);
}
#endif
