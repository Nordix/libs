#include "../../event_class/event_class.h"
#include "../../helpers/proc_parsing.h"

#if defined(CAPTURE_SCHED_PROC_EXEC) && defined(__NR_clone3) && defined(__NR_wait4) && \
        defined(__NR_execve)

#include <linux/sched.h>

TEST(GenericTracepoints, sched_proc_exec) {
	auto evt_test = get_syscall_event_test(__NR_execve, EXIT_EVENT);

	evt_test->enable_capture();

	/*=============================== TRIGGER SYSCALL  ===========================*/

	/* Prepare the execve args */
	const char *pathname = "/usr/bin/true";
	const char *comm = "true";

	std::string too_long_arg(4096, 'x');
	const char *newargv[] =
	        {pathname, "", "first_argv", "", too_long_arg.c_str(), "second_argv", NULL};
	std::string truncated_too_long_arg(
	        4096 - (strlen(pathname) + 1) - (strlen("first_argv") + 1) - 2 * (strlen("") + 1) - 1,
	        'x');
	const char *expected_newargv[] =
	        {pathname, "", "first_argv", "", truncated_too_long_arg.c_str(), NULL};

	const char *newenviron[] = {"IN_TEST=yes",
	                            "3_ARGUMENT=yes",
	                            too_long_arg.c_str(),
	                            "2_ARGUMENT=no",
	                            NULL};
	std::string truncated_too_long_env(
	        4096 - (strlen("IN_TEST=yes") + 1) - (strlen("3_ARGUMENT=yes") + 1) - 1,
	        'x');
	const char *expected_newenviron[] = {"IN_TEST=yes",
	                                     "3_ARGUMENT=yes",
	                                     truncated_too_long_env.c_str(),
	                                     NULL};

	/* We need to use `SIGCHLD` otherwise the parent won't receive any signal
	 * when the child terminates.
	 */
	clone_args cl_args = {};
	cl_args.exit_signal = SIGCHLD;
	pid_t ret_pid = syscall(__NR_clone3, &cl_args, sizeof(cl_args));

	if(ret_pid == 0) {
		syscall(__NR_execve, pathname, newargv, newenviron);
		exit(EXIT_FAILURE);
	}

	assert_syscall_state(SYSCALL_SUCCESS, "clone3", ret_pid, NOT_EQUAL, -1);

	/* Catch the child before doing anything else. */
	int status = 0;
	int options = 0;
	assert_syscall_state(SYSCALL_SUCCESS,
	                     "wait4",
	                     syscall(__NR_wait4, ret_pid, &status, options, NULL),
	                     NOT_EQUAL,
	                     -1);

	if(__WEXITSTATUS(status) == EXIT_FAILURE || __WIFSIGNALED(status) != 0) {
		FAIL() << "The child execve failed." << std::endl;
	}

	/*=============================== TRIGGER SYSCALL ===========================*/

	evt_test->disable_capture();

	/* We search for a child event. */
	evt_test->assert_event_presence(ret_pid);

	if(HasFatalFailure()) {
		return;
	}

	evt_test->parse_event();

	evt_test->assert_header();

	/*=============================== ASSERT PARAMETERS  ===========================*/

	/* Please note here we cannot assert all the params, we check only the possible ones. */

	/* Parameter 1: res (type: PT_ERRNO) */
	evt_test->assert_numeric_param(1, (int64_t)0);

	/* Parameter 2: exe (type: PT_CHARBUF) */
	evt_test->assert_charbuf_param(2, pathname);

	/* Parameter 3: args (type: PT_CHARBUFARRAY) */
	/* Starting from `1` because the first is `exe`. */
	evt_test->assert_charbuf_array_param(3, &expected_newargv[1]);

	/* Parameter 4: tid (type: PT_PID) */
	evt_test->assert_numeric_param(4, (int64_t)ret_pid);

	/* Parameter 5: pid (type: PT_PID) */
	/* We are the main thread of the process so it's equal to `tid`. */
	evt_test->assert_numeric_param(5, (int64_t)ret_pid);

	/* Parameter 6: ptid (type: PT_PID) */
	evt_test->assert_numeric_param(6, (int64_t)::getpid());

	/* Parameter 7: cwd (type: PT_CHARBUF) */
	/* leave the current working directory empty like in the old probe. */
	evt_test->assert_empty_param(7);

	/* Parameter 14: comm (type: PT_CHARBUF) */
	evt_test->assert_charbuf_param(14, comm);

	/* Parameter 16: env (type: PT_CHARBUFARRAY) */
	evt_test->assert_charbuf_array_param(16, &expected_newenviron[0]);

	/* PPM_EXE_WRITABLE is set when the user that executed a process can also write to the
	 * executable file that is used to spawn it or is its owner or otherwise capable.
	 */
	evt_test->assert_numeric_param(20, (uint32_t)PPM_EXE_WRITABLE);

	/* Parameter 24: exe_file ino (type: PT_UINT64) */
	evt_test->assert_numeric_param(24, (uint64_t)1, GREATER_EQUAL);

	/* Parameter 25: exe_file ctime (last status change time, epoch value in nanoseconds) (type:
	 * PT_ABSTIME) */
	evt_test->assert_numeric_param(25, (uint64_t)1000000000000000000, GREATER_EQUAL);

	/* Parameter 26: exe_file mtime (last modification time, epoch value in nanoseconds) (type:
	 * PT_ABSTIME) */
	evt_test->assert_numeric_param(26, (uint64_t)1000000000000000000, GREATER_EQUAL);

	/* Parameter 27: euid (type: PT_UID) */
	evt_test->assert_numeric_param(27, (uint32_t)geteuid(), EQUAL);

	/* Parameter 28: trusted_exepath (type: PT_FSPATH) */
	evt_test->assert_charbuf_param(28, pathname);

	/*=============================== ASSERT PARAMETERS  ===========================*/

	evt_test->assert_num_params_pushed(30);
}

#if defined(__NR_memfd_create) && defined(__NR_openat) && defined(__NR_read) && defined(__NR_write)
#include <sys/mman.h>
TEST(GenericTracepoints, sched_proc_exec_success_memfd) {
	auto evt_test = get_syscall_event_test(__NR_execve, EXIT_EVENT);

	evt_test->enable_capture();

	/*=============================== TRIGGER SYSCALL  ===========================*/

	int mem_fd = syscall(__NR_memfd_create, "malware", MFD_CLOEXEC);
	assert_syscall_state(SYSCALL_SUCCESS, "memfd_create", mem_fd, NOT_EQUAL, -1);

	/* Open the executable to copy */
	int fd_to_read = syscall(__NR_openat, 0, "/usr/bin/echo", O_RDWR);
	if(fd_to_read < 0) {
		FAIL() << "failed to open the file to read\n";
	}

	char buf[200];
	ssize_t bytes_read = 200;
	while(bytes_read != 0) {
		bytes_read = syscall(__NR_read, fd_to_read, buf, sizeof(buf));
		if(bytes_read < 0) {
			syscall(__NR_close, fd_to_read);
			syscall(__NR_close, mem_fd);
			FAIL() << "unable to read from file\n";
		}

		bytes_read = syscall(__NR_write, mem_fd, buf, bytes_read);
		if(bytes_read < 0) {
			syscall(__NR_close, fd_to_read);
			syscall(__NR_close, mem_fd);
			FAIL() << "unable to write to file\n";
		}
	}
	syscall(__NR_close, fd_to_read);

	/* We need to use `SIGCHLD` otherwise the parent won't receive any signal
	 * when the child terminates.
	 */
	clone_args cl_args = {};
	cl_args.exit_signal = SIGCHLD;
	pid_t ret_pid = syscall(__NR_clone3, &cl_args, sizeof(cl_args));

	if(ret_pid == 0) {
		char pathname[200];
		snprintf(pathname, sizeof(pathname), "/proc/%d/fd/%d", getpid(), mem_fd);
		const char *newargv[] = {pathname, "[OUTPUT] SyscallExit.execveX_success_memfd", NULL};
		const char *newenviron[] = {"IN_TEST=yes", "3_ARGUMENT=yes", "2_ARGUMENT=no", NULL};
		syscall(__NR_execve, pathname, newargv, newenviron);
		exit(EXIT_FAILURE);
	}
	syscall(__NR_close, mem_fd);

	assert_syscall_state(SYSCALL_SUCCESS, "clone3", ret_pid, NOT_EQUAL, -1);

	/* Catch the child before doing anything else. */
	int status = 0;
	int options = 0;
	assert_syscall_state(SYSCALL_SUCCESS,
	                     "wait4",
	                     syscall(__NR_wait4, ret_pid, &status, options, NULL),
	                     NOT_EQUAL,
	                     -1);

	if(__WEXITSTATUS(status) == EXIT_FAILURE || __WIFSIGNALED(status) != 0) {
		FAIL() << "The child execve failed." << std::endl;
	}

	/*=============================== TRIGGER SYSCALL ===========================*/

	evt_test->disable_capture();

	/* We search for a child event. */
	evt_test->assert_event_presence(ret_pid);

	if(HasFatalFailure()) {
		return;
	}

	evt_test->parse_event();

	evt_test->assert_header();

	/*=============================== ASSERT PARAMETERS  ===========================*/

	/* Please note here we cannot assert all the params, we check only the possible ones. */

	/* Parameter 1: res (type: PT_ERRNO) */
	evt_test->assert_numeric_param(1, (int64_t)0);

	/* PPM_EXE_WRITABLE is set when the user that executed a process can also write to the
	 * executable file that is used to spawn it or is its owner or otherwise capable.
	 */
	evt_test->assert_numeric_param(20, (uint32_t)PPM_EXE_WRITABLE | PPM_EXE_FROM_MEMFD);

	/* Parameter 28: trusted_exepath (type: PT_FSPATH) */
	/* In the kmod, we use the "d_path" helper while in BPF we reconstruct the path
	 * by hand so the result is a little bit different.
	 */
	if(evt_test->is_kmod_engine()) {
		evt_test->assert_charbuf_param(28, "/memfd:malware (deleted)");
	} else {
		/* In BPF drivers we don't have the correct result but we can reconstruct part of it */
		evt_test->assert_charbuf_param(28, "memfd:malware");
	}

	/*=============================== ASSERT PARAMETERS  ===========================*/

	evt_test->assert_num_params_pushed(30);
}
#endif
#endif
